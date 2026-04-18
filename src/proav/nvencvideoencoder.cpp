/**
 * @file      nvencvideoencoder.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 */

#include <promeki/nvencvideoencoder.h>

#if PROMEKI_ENABLE_NVENC

#include <promeki/mediapacket.h>
#include <promeki/mediaconfig.h>
#include <promeki/image.h>
#include <promeki/imagedesc.h>
#include <promeki/pixeldesc.h>
#include <promeki/buffer.h>
#include <promeki/cuda.h>
#include <promeki/framerate.h>
#include <promeki/logger.h>
#include <promeki/enums.h>
#include <promeki/metadata.h>
#include <promeki/videocodec.h>
#include <promeki/masteringdisplay.h>
#include <promeki/contentlightlevel.h>
#include <promeki/timecode.h>

#include <deque>
#include <mutex>
#include <vector>
#include <cstring>
#include <cstdint>
#include <dlfcn.h>

#include <cuda.h>          // Driver API — CUcontext / cuInit / cuDevicePrimaryCtxRetain.
#include <nvEncodeAPI.h>

PROMEKI_NAMESPACE_BEGIN

// ---------------------------------------------------------------------------
// Dynamic loader for libnvidia-encode.so.1.  The NVENC SDK ships only
// headers; the actual entry point lives in the driver user-mode library
// so we dlopen it once per process and cache the populated function
// list.  loadNvenc() is cheap on the hot path thanks to the
// once-style guard.
// ---------------------------------------------------------------------------

namespace {

NV_ENCODE_API_FUNCTION_LIST gNvenc{};
bool                        gNvencLoaded = false;
std::mutex                  gNvencMutex;

bool loadNvencLocked() {
        if(gNvencLoaded) return true;

        void *lib = dlopen("libnvidia-encode.so.1", RTLD_NOW | RTLD_LOCAL);
        if(!lib) {
                promekiErr("NVENC: dlopen(libnvidia-encode.so.1) failed: %s", dlerror());
                return false;
        }

        using CreateFn = NVENCSTATUS (NVENCAPI *)(NV_ENCODE_API_FUNCTION_LIST *);
        auto createFn = reinterpret_cast<CreateFn>(
                dlsym(lib, "NvEncodeAPICreateInstance"));
        if(!createFn) {
                promekiErr("NVENC: dlsym(NvEncodeAPICreateInstance) failed: %s", dlerror());
                dlclose(lib);
                return false;
        }

        NV_ENCODE_API_FUNCTION_LIST fl{};
        fl.version = NV_ENCODE_API_FUNCTION_LIST_VER;
        NVENCSTATUS st = createFn(&fl);
        if(st != NV_ENC_SUCCESS) {
                promekiErr("NVENC: NvEncodeAPICreateInstance failed (status %d)", (int)st);
                return false;
        }

        gNvenc = fl;
        gNvencLoaded = true;
        return true;
}

bool loadNvenc() {
        std::lock_guard<std::mutex> lock(gNvencMutex);
        return loadNvencLocked();
}

// ---------------------------------------------------------------------------
// Format dispatch table.  Each row maps a PixelDesc to the NVENC
// buffer format, chroma / bit-depth config values, and the byte
// geometry needed by the generic uploadFrame() routine.
// ---------------------------------------------------------------------------

struct FormatEntry {
        PixelDesc::ID           pixelDescId;
        NV_ENC_BUFFER_FORMAT    nvencFmt;
        uint32_t                chromaFormatIDC;  // 1=420, 2=422, 3=444
        NV_ENC_BIT_DEPTH        inputBitDepth;
        NV_ENC_BIT_DEPTH        outputBitDepth;
        uint32_t                bytesPerPixelY;   // 1 for 8-bit, 2 for 10/16-bit
        uint32_t                uvHeightDivisor;  // 2 for 420, 1 for 422/444
        uint32_t                planeCount;       // 2 for semi-planar, 3 for planar
};

static constexpr FormatEntry kFormatTable[] = {
        { PixelDesc::YUV8_420_SemiPlanar_Rec709,
          NV_ENC_BUFFER_FORMAT_NV12, 1,
          NV_ENC_BIT_DEPTH_8, NV_ENC_BIT_DEPTH_8, 1, 2, 2 },
        { PixelDesc::YUV10_420_SemiPlanar_LE_Rec709,
          NV_ENC_BUFFER_FORMAT_YUV420_10BIT, 1,
          NV_ENC_BIT_DEPTH_10, NV_ENC_BIT_DEPTH_10, 2, 2, 2 },
        { PixelDesc::YUV8_422_SemiPlanar_Rec709,
          NV_ENC_BUFFER_FORMAT_NV16, 2,
          NV_ENC_BIT_DEPTH_8, NV_ENC_BIT_DEPTH_8, 1, 1, 2 },
        { PixelDesc::YUV10_422_SemiPlanar_LE_Rec709,
          NV_ENC_BUFFER_FORMAT_P210, 2,
          NV_ENC_BIT_DEPTH_10, NV_ENC_BIT_DEPTH_10, 2, 1, 2 },
        { PixelDesc::YUV8_444_Planar_Rec709,
          NV_ENC_BUFFER_FORMAT_YUV444, 3,
          NV_ENC_BIT_DEPTH_8, NV_ENC_BIT_DEPTH_8, 1, 1, 3 },
        { PixelDesc::YUV10_444_Planar_LE_Rec709,
          NV_ENC_BUFFER_FORMAT_YUV444_10BIT, 3,
          NV_ENC_BIT_DEPTH_10, NV_ENC_BIT_DEPTH_10, 2, 1, 3 },
};

const FormatEntry *lookupFormat(PixelDesc::ID id) {
        for(const auto &e : kFormatTable) {
                if(e.pixelDescId == id) return &e;
        }
        return nullptr;
}

// ---------------------------------------------------------------------------
// MediaConfig → NVENC parameter translation.
// ---------------------------------------------------------------------------

NV_ENC_PARAMS_RC_MODE toNvencRc(const Enum &rc) {
        if(rc == VideoRateControl::CBR) return NV_ENC_PARAMS_RC_CBR;
        if(rc == VideoRateControl::CQP) return NV_ENC_PARAMS_RC_CONSTQP;
        return NV_ENC_PARAMS_RC_VBR;
}

GUID toNvencPreset(const Enum &p) {
        if(p == VideoEncoderPreset::UltraLowLatency) return NV_ENC_PRESET_P1_GUID;
        if(p == VideoEncoderPreset::LowLatency)      return NV_ENC_PRESET_P3_GUID;
        if(p == VideoEncoderPreset::HighQuality)     return NV_ENC_PRESET_P6_GUID;
        if(p == VideoEncoderPreset::Lossless)        return NV_ENC_PRESET_P2_GUID;
        return NV_ENC_PRESET_P4_GUID;
}

NV_ENC_TUNING_INFO toNvencTuning(const Enum &p) {
        if(p == VideoEncoderPreset::UltraLowLatency) return NV_ENC_TUNING_INFO_ULTRA_LOW_LATENCY;
        if(p == VideoEncoderPreset::LowLatency)      return NV_ENC_TUNING_INFO_LOW_LATENCY;
        if(p == VideoEncoderPreset::HighQuality)     return NV_ENC_TUNING_INFO_HIGH_QUALITY;
        if(p == VideoEncoderPreset::Lossless)        return NV_ENC_TUNING_INFO_LOSSLESS;
        // Default / Balanced: high-quality tuning matches ffmpeg's
        // h264_nvenc default (p4 / hq) and produces a bitstream
        // that hardware decoders consistently handle. Low-latency
        // tuning is a streaming-specific rate-distribution choice
        // that some NVDEC / VDPAU paths struggle with for offline
        // file playback — use it only when explicitly requested.
        return NV_ENC_TUNING_INFO_HIGH_QUALITY;
}

// ---------------------------------------------------------------------------
// Profile / level string → NVENC GUID / enum mapping.
// Empty strings trigger auto-selection from the input format.
// ---------------------------------------------------------------------------

GUID h264ProfileGuid(const String &name, const FormatEntry *fmt) {
        if(name == "baseline")    return NV_ENC_H264_PROFILE_BASELINE_GUID;
        if(name == "main")        return NV_ENC_H264_PROFILE_MAIN_GUID;
        if(name == "high")        return NV_ENC_H264_PROFILE_HIGH_GUID;
        if(name == "high10")      return NV_ENC_H264_PROFILE_HIGH_10_GUID;
        if(name == "high422")     return NV_ENC_H264_PROFILE_HIGH_422_GUID;
        if(name == "high444")     return NV_ENC_H264_PROFILE_HIGH_444_GUID;
        if(name == "progressive") return NV_ENC_H264_PROFILE_PROGRESSIVE_HIGH_GUID;
        if(!name.isEmpty())       return NV_ENC_CODEC_PROFILE_AUTOSELECT_GUID;
        if(fmt->chromaFormatIDC == 3)                           return NV_ENC_H264_PROFILE_HIGH_444_GUID;
        if(fmt->chromaFormatIDC == 2)                           return NV_ENC_H264_PROFILE_HIGH_422_GUID;
        if(fmt->inputBitDepth == NV_ENC_BIT_DEPTH_10)           return NV_ENC_H264_PROFILE_HIGH_10_GUID;
        return NV_ENC_CODEC_PROFILE_AUTOSELECT_GUID;
}

GUID hevcProfileGuid(const String &name, const FormatEntry *fmt) {
        if(name == "main")        return NV_ENC_HEVC_PROFILE_MAIN_GUID;
        if(name == "main10")      return NV_ENC_HEVC_PROFILE_MAIN10_GUID;
        if(name == "rext")        return NV_ENC_HEVC_PROFILE_FREXT_GUID;
        if(!name.isEmpty())       return NV_ENC_CODEC_PROFILE_AUTOSELECT_GUID;
        if(fmt->chromaFormatIDC >= 2)                           return NV_ENC_HEVC_PROFILE_FREXT_GUID;
        if(fmt->inputBitDepth == NV_ENC_BIT_DEPTH_10)           return NV_ENC_HEVC_PROFILE_MAIN10_GUID;
        return NV_ENC_CODEC_PROFILE_AUTOSELECT_GUID;
}

GUID av1ProfileGuid(const String &) {
        return NV_ENC_AV1_PROFILE_MAIN_GUID;
}

uint32_t h264Level(const String &s) {
        if(s.isEmpty()) return NV_ENC_LEVEL_AUTOSELECT;
        if(s == "1.0" || s == "1")   return NV_ENC_LEVEL_H264_1;
        if(s == "1b")                return NV_ENC_LEVEL_H264_1b;
        if(s == "1.1")               return NV_ENC_LEVEL_H264_11;
        if(s == "1.2")               return NV_ENC_LEVEL_H264_12;
        if(s == "1.3")               return NV_ENC_LEVEL_H264_13;
        if(s == "2.0" || s == "2")   return NV_ENC_LEVEL_H264_2;
        if(s == "2.1")               return NV_ENC_LEVEL_H264_21;
        if(s == "2.2")               return NV_ENC_LEVEL_H264_22;
        if(s == "3.0" || s == "3")   return NV_ENC_LEVEL_H264_3;
        if(s == "3.1")               return NV_ENC_LEVEL_H264_31;
        if(s == "3.2")               return NV_ENC_LEVEL_H264_32;
        if(s == "4.0" || s == "4")   return NV_ENC_LEVEL_H264_4;
        if(s == "4.1")               return NV_ENC_LEVEL_H264_41;
        if(s == "4.2")               return NV_ENC_LEVEL_H264_42;
        if(s == "5.0" || s == "5")   return NV_ENC_LEVEL_H264_5;
        if(s == "5.1")               return NV_ENC_LEVEL_H264_51;
        if(s == "5.2")               return NV_ENC_LEVEL_H264_52;
        if(s == "6.0" || s == "6")   return NV_ENC_LEVEL_H264_60;
        if(s == "6.1")               return NV_ENC_LEVEL_H264_61;
        if(s == "6.2")               return NV_ENC_LEVEL_H264_62;
        return NV_ENC_LEVEL_AUTOSELECT;
}

uint32_t hevcLevel(const String &s) {
        if(s.isEmpty()) return NV_ENC_LEVEL_AUTOSELECT;
        if(s == "1.0" || s == "1")   return NV_ENC_LEVEL_HEVC_1;
        if(s == "2.0" || s == "2")   return NV_ENC_LEVEL_HEVC_2;
        if(s == "2.1")               return NV_ENC_LEVEL_HEVC_21;
        if(s == "3.0" || s == "3")   return NV_ENC_LEVEL_HEVC_3;
        if(s == "3.1")               return NV_ENC_LEVEL_HEVC_31;
        if(s == "4.0" || s == "4")   return NV_ENC_LEVEL_HEVC_4;
        if(s == "4.1")               return NV_ENC_LEVEL_HEVC_41;
        if(s == "5.0" || s == "5")   return NV_ENC_LEVEL_HEVC_5;
        if(s == "5.1")               return NV_ENC_LEVEL_HEVC_51;
        if(s == "5.2")               return NV_ENC_LEVEL_HEVC_52;
        if(s == "6.0" || s == "6")   return NV_ENC_LEVEL_HEVC_6;
        if(s == "6.1")               return NV_ENC_LEVEL_HEVC_61;
        if(s == "6.2")               return NV_ENC_LEVEL_HEVC_62;
        return NV_ENC_LEVEL_AUTOSELECT;
}

uint32_t av1Level(const String &s) {
        if(s.isEmpty()) return NV_ENC_LEVEL_AV1_AUTOSELECT;
        if(s == "2.0" || s == "2")   return NV_ENC_LEVEL_AV1_2;
        if(s == "2.1")               return NV_ENC_LEVEL_AV1_21;
        if(s == "2.2")               return NV_ENC_LEVEL_AV1_22;
        if(s == "2.3")               return NV_ENC_LEVEL_AV1_23;
        if(s == "3.0" || s == "3")   return NV_ENC_LEVEL_AV1_3;
        if(s == "3.1")               return NV_ENC_LEVEL_AV1_31;
        if(s == "3.2")               return NV_ENC_LEVEL_AV1_32;
        if(s == "3.3")               return NV_ENC_LEVEL_AV1_33;
        if(s == "4.0" || s == "4")   return NV_ENC_LEVEL_AV1_4;
        if(s == "4.1")               return NV_ENC_LEVEL_AV1_41;
        if(s == "4.2")               return NV_ENC_LEVEL_AV1_42;
        if(s == "4.3")               return NV_ENC_LEVEL_AV1_43;
        if(s == "5.0" || s == "5")   return NV_ENC_LEVEL_AV1_5;
        if(s == "5.1")               return NV_ENC_LEVEL_AV1_51;
        if(s == "5.2")               return NV_ENC_LEVEL_AV1_52;
        if(s == "5.3")               return NV_ENC_LEVEL_AV1_53;
        if(s == "6.0" || s == "6")   return NV_ENC_LEVEL_AV1_6;
        if(s == "6.1")               return NV_ENC_LEVEL_AV1_61;
        if(s == "6.2")               return NV_ENC_LEVEL_AV1_62;
        if(s == "6.3")               return NV_ENC_LEVEL_AV1_63;
        if(s == "7.0" || s == "7")   return NV_ENC_LEVEL_AV1_7;
        if(s == "7.1")               return NV_ENC_LEVEL_AV1_71;
        if(s == "7.2")               return NV_ENC_LEVEL_AV1_72;
        if(s == "7.3")               return NV_ENC_LEVEL_AV1_73;
        return NV_ENC_LEVEL_AV1_AUTOSELECT;
}

MASTERING_DISPLAY_INFO toNvencMastering(const MasteringDisplay &md) {
        MASTERING_DISPLAY_INFO info{};
        info.r.x          = static_cast<uint16_t>(md.red().x()        * 50000.0 + 0.5);
        info.r.y          = static_cast<uint16_t>(md.red().y()        * 50000.0 + 0.5);
        info.g.x          = static_cast<uint16_t>(md.green().x()      * 50000.0 + 0.5);
        info.g.y          = static_cast<uint16_t>(md.green().y()      * 50000.0 + 0.5);
        info.b.x          = static_cast<uint16_t>(md.blue().x()       * 50000.0 + 0.5);
        info.b.y          = static_cast<uint16_t>(md.blue().y()       * 50000.0 + 0.5);
        info.whitePoint.x = static_cast<uint16_t>(md.whitePoint().x() * 50000.0 + 0.5);
        info.whitePoint.y = static_cast<uint16_t>(md.whitePoint().y() * 50000.0 + 0.5);
        info.maxLuma      = static_cast<uint32_t>(md.maxLuminance()   * 10000.0 + 0.5);
        info.minLuma      = static_cast<uint32_t>(md.minLuminance()   * 10000.0 + 0.5);
        return info;
}

CONTENT_LIGHT_LEVEL toNvencCll(const ContentLightLevel &cll) {
        CONTENT_LIGHT_LEVEL info{};
        info.maxContentLightLevel    = static_cast<uint16_t>(std::min(cll.maxCLL(),  uint32_t(65535)));
        info.maxPicAverageLightLevel = static_cast<uint16_t>(std::min(cll.maxFALL(), uint32_t(65535)));
        return info;
}

// Populate the H.264/HEVC VUI color-description block.  H.264 and HEVC
// share the same struct layout (NV_ENC_CONFIG_HEVC_VUI_PARAMETERS is a
// typedef of the H.264 struct), so a single helper covers both codecs.
//
// Arguments:
//   vui       — destination VUI parameter block.
//   primaries — ISO/IEC 23091-4 numeric value already resolved from the
//               caller's Auto/Unspecified preference.  0 or 2 mean
//               "don't set colourDescriptionPresentFlag for this field".
//   transfer  — ditto for transferCharacteristics.
//   matrix    — ditto for colourMatrix.
//   fullRange — VideoRange enum value (0=Unknown, 1=Limited, 2=Full).
//               Unknown leaves videoFullRangeFlag / videoSignalTypePresentFlag
//               off so NVENC omits the block entirely unless color
//               description is present.
//
// The VUI color-description block becomes fully self-consistent only
// when all three H.273 fields are set; signalling just one or two can
// confuse downstream players.  Callers that set some-but-not-all will
// have the remaining field written as Unspecified (2), which every
// decoder interprets as "assume the default for the content type".
void populateVuiColorDescription(NV_ENC_CONFIG_H264_VUI_PARAMETERS &vui,
                                 uint32_t primaries, uint32_t transfer,
                                 uint32_t matrix, uint32_t videoRange) {
        const bool haveAny = (primaries != 0 && primaries != 2) ||
                             (transfer  != 0 && transfer  != 2) ||
                             (matrix    != 0 && matrix    != 2);
        const bool haveRange = (videoRange == 1 /*Limited*/ || videoRange == 2 /*Full*/);
        if(!haveAny && !haveRange) return;

        vui.videoSignalTypePresentFlag  = 1;
        vui.videoFormat                 = NV_ENC_VUI_VIDEO_FORMAT_UNSPECIFIED;
        // videoFullRangeFlag is a single bit — map Limited→0, Full→1,
        // Unknown→0 (safest SDR default when only colour description
        // is present).
        vui.videoFullRangeFlag = (videoRange == 2 /*Full*/) ? 1 : 0;

        if(haveAny) {
                vui.colourDescriptionPresentFlag = 1;
                vui.colourPrimaries = static_cast<NV_ENC_VUI_COLOR_PRIMARIES>(
                        (primaries != 0) ? primaries : 2);
                vui.transferCharacteristics = static_cast<NV_ENC_VUI_TRANSFER_CHARACTERISTIC>(
                        (transfer != 0) ? transfer : 2);
                vui.colourMatrix = static_cast<NV_ENC_VUI_MATRIX_COEFFS>(
                        (matrix != 0) ? matrix : 2);
        }
}

// Map a promeki VideoScanMode to NVENC's per-picture
// NV_ENC_DISPLAY_PIC_STRUCT.  Values outside the interlaced set fold
// to @c _DISPLAY_FRAME so progressive and @c PsF both behave as
// display-frame.  @c PsF is progressive content being transported as
// two fields — the bitstream carries it as frames, so signalling
// frame is correct for downstream displays.
NV_ENC_DISPLAY_PIC_STRUCT toNvencDisplayPicStruct(VideoScanMode mode) {
        if(mode == VideoScanMode::InterlacedEvenFirst ||
           mode == VideoScanMode::Interlaced) {
                // InterlacedEvenFirst == top field first.  Plain
                // @c Interlaced inherits the broadcast norm (top
                // first) because without a field-order hint the
                // safest default is the HD/SDI-style top-first order.
                return NV_ENC_PIC_STRUCT_DISPLAY_FIELD_TOP_BOTTOM;
        }
        if(mode == VideoScanMode::InterlacedOddFirst) {
                return NV_ENC_PIC_STRUCT_DISPLAY_FIELD_BOTTOM_TOP;
        }
        return NV_ENC_PIC_STRUCT_DISPLAY_FRAME;
}

// Convert a promeki Timecode to NVENC's NV_ENC_TIME_CODE.  NVENC feeds
// the clockTimestamp[0] set straight through to the picture timing
// (H.264) / time code (HEVC) SEI payload — see Annex D of ITU-T H.264
// and HEVC specifications.  We populate one clock-timestamp set;
// MAX_NUM_CLOCK_TS = 3 is used for frame-doubling / tripling which we
// don't exercise here.
//
// @p scanMode controls the @c displayPicStruct field; set to
// @c Progressive (or any non-interlaced value) for normal content,
// @c InterlacedEvenFirst / @c InterlacedOddFirst for interlaced.
NV_ENC_TIME_CODE toNvencTimeCode(const Timecode &tc, VideoScanMode scanMode) {
        NV_ENC_TIME_CODE out{};
        out.displayPicStruct = toNvencDisplayPicStruct(scanMode);
        auto &ts = out.clockTimestamp[0];
        ts.countingType      = 0;
        ts.discontinuityFlag = 0;
        ts.cntDroppedFrames  = tc.isDropFrame() ? 1 : 0;
        ts.nFrames           = static_cast<uint32_t>(tc.frame()) & 0xFFu;
        ts.secondsValue      = static_cast<uint32_t>(tc.sec())   & 0x3Fu;
        ts.minutesValue      = static_cast<uint32_t>(tc.min())   & 0x3Fu;
        ts.hoursValue        = static_cast<uint32_t>(tc.hour())  & 0x1Fu;
        ts.timeOffset        = 0;
        out.skipClockTimestampInsertion = 0;
        return out;
}

} // namespace

// ---------------------------------------------------------------------------
// NvencVideoEncoder::Impl — everything that needs NVENC + CUDA types.
// ---------------------------------------------------------------------------

class NvencVideoEncoder::Impl {
        public:
                explicit Impl(Codec codec) : _codec(codec) {}

                ~Impl() { destroySession(); }

                // Copies caller-visible configuration.  Actual encoder
                // creation happens lazily in ensureSession() once we
                // know the input dimensions from the first frame.
                void configure(const MediaConfig &cfg) {
                        _cfg = cfg;
                        _masteringDisplay  = cfg.getAs<MasteringDisplay>(MediaConfig::HdrMasteringDisplay);
                        _contentLightLevel = cfg.getAs<ContentLightLevel>(MediaConfig::HdrContentLightLevel);
                        // Cache the VUI color description as raw H.273
                        // numeric values.  Enum::value() returns the
                        // integer stored in the Enum, which by design is
                        // the spec-registered value.
                        //
                        // VariantDatabase::getAs<Enum> returns a default-
                        // constructed Enum (value -1 / InvalidValue)
                        // when the key is absent, not the VariantSpec's
                        // registered default.  Cast to uint32_t that
                        // becomes 0xFFFFFFFF and then flows through the
                        // resolveColorDescription() Auto check (which
                        // only recognises 255) as a concrete override,
                        // poisoning the downstream VUI.  Look up the
                        // spec default explicitly so missing keys land
                        // on @c Auto / @c Unknown as intended.
                        auto readEnum = [&cfg](MediaConfig::ID key) -> Enum {
                                const VariantSpec *s = MediaConfig::spec(key);
                                if(!cfg.contains(key)) {
                                        return s ? s->defaultValue().get<Enum>()
                                                 : Enum();
                                }
                                return cfg.getAs<Enum>(key);
                        };
                        _colorPrimaries = static_cast<uint32_t>(
                                readEnum(MediaConfig::VideoColorPrimaries).value());
                        _transferCharacteristics = static_cast<uint32_t>(
                                readEnum(MediaConfig::VideoTransferCharacteristics).value());
                        _matrixCoefficients = static_cast<uint32_t>(
                                readEnum(MediaConfig::VideoMatrixCoefficients).value());
                        _videoRange = static_cast<uint32_t>(
                                readEnum(MediaConfig::VideoRange).value());
                        _cfgScanMode = VideoScanMode(readEnum(
                                MediaConfig::VideoScanMode).value());
                        _needReconfigure = _sessionOpen;
                }

                Error submitFrame(const Image &frame, const MediaTimeStamp &pts,
                                  bool forceKey) {
                        if(!frame.isValid() || frame.planes().isEmpty()) {
                                return setError(Error::Invalid, "invalid frame");
                        }
                        const FormatEntry *fmt = lookupFormat(frame.pixelDesc().id());
                        if(!fmt) {
                                return setError(Error::PixelFormatNotSupported,
                                        String::sprintf("NvencVideoEncoder: unsupported input format %s",
                                                frame.pixelDesc().name().cstr()));
                        }
                        if(Error err = ensureSession(frame.width(), frame.height(), fmt,
                                                     frame.desc().videoScanMode());
                           err.isError()) {
                                return err;
                        }

                        Slot *slot = acquireFreeSlot();
                        if(!slot) return setError(Error::TryAgain, "no free NVENC slot");

                        if(Error err = uploadFrame(frame, slot); err.isError()) {
                                _freeSlots.push_back(slot);
                                return err;
                        }
                        slot->imageMeta = frame.metadata();

                        NV_ENC_PIC_PARAMS pic{};
                        pic.version        = NV_ENC_PIC_PARAMS_VER;
                        pic.inputBuffer    = slot->in;
                        pic.outputBitstream = slot->out;
                        pic.bufferFmt      = _fmt->nvencFmt;
                        pic.inputWidth     = _width;
                        pic.inputHeight    = _height;
                        pic.inputPitch     = slot->pitch;
                        pic.pictureStruct  = NV_ENC_PIC_STRUCT_FRAME;
                        // NVENC's frameIdx is uint32 while our counter is
                        // uint64.  The field is only an ordering hint for
                        // NVENC, so wrapping at 2^32 is benign; be explicit
                        // about the truncation to silence the warning.
                        pic.frameIdx       = static_cast<uint32_t>(_frameIdx);
                        pic.inputTimeStamp = _frameIdx;
                        pic.encodePicFlags = forceKey ? NV_ENC_PIC_FLAG_FORCEIDR : 0;

                        slot->hasMd  = false;
                        slot->hasCll = false;
                        MasteringDisplay md = frame.metadata().getAs<MasteringDisplay>(
                                Metadata::MasteringDisplay, _masteringDisplay);
                        ContentLightLevel cll = frame.metadata().getAs<ContentLightLevel>(
                                Metadata::ContentLightLevel, _contentLightLevel);

                        // Per-frame Picture Timing SEI plumbing.  Unlike
                        // the HDR pointers below, NV_ENC_TIME_CODE lives
                        // inline inside NV_ENC_PIC_PARAMS_H264 / _HEVC
                        // (see the SDK header), so a direct copy is safe
                        // — no slot storage needed.
                        //
                        // H.264: pinned on unconditionally to match the
                        // session-init configuration (every H.264 stream
                        // we emit carries pic_timing so hardware decoders
                        // get the pic_struct signal they rely on for DPB
                        // allocation — see the rationale at the
                        // outputPictureTimingSEI=1 site below).
                        //
                        // HEVC: still gated by @c _timecodeSEI or an
                        // interlaced session.  HEVC's time_code SEI is a
                        // separate SEI payload and some HEVC decoders
                        // handle its absence fine.
                        //
                        // Per-frame scan mode override comes from
                        // @c Metadata::VideoScanMode on the input Image
                        // and falls back to the session's resolved mode.
                        const bool frameNeedsTiming =
                                _codec == Codec_H264 ||
                                (_codec == Codec_HEVC &&
                                 (_timecodeSEI || _effectiveScanMode.isInterlaced()));
                        if(frameNeedsTiming) {
                                VideoScanMode frameScan = _effectiveScanMode;
                                if(frame.metadata().contains(Metadata::VideoScanMode)) {
                                        VideoScanMode m(frame.metadata().getAs<Enum>(
                                                Metadata::VideoScanMode).value());
                                        if(m.value() != VideoScanMode::Unknown.value()) {
                                                frameScan = m;
                                        }
                                }
                                Timecode tc;
                                if(_timecodeSEI) {
                                        tc = frame.metadata().getAs<Timecode>(Metadata::Timecode);
                                }
                                NV_ENC_TIME_CODE nvTc{};
                                if(tc.isValid()) {
                                        nvTc = toNvencTimeCode(tc, frameScan);
                                } else {
                                        // Either the session doesn't want
                                        // timecode at all, or this frame has
                                        // no Timecode set — skip the clock
                                        // timestamp but still pass the
                                        // displayPicStruct so pic_struct
                                        // lands in the SEI.
                                        nvTc.displayPicStruct            = toNvencDisplayPicStruct(frameScan);
                                        nvTc.skipClockTimestampInsertion = 1;
                                }
                                if(_codec == Codec_H264) {
                                        pic.codecPicParams.h264PicParams.timeCode = nvTc;
                                } else {
                                        pic.codecPicParams.hevcPicParams.timeCode = nvTc;
                                }
                        }

                        if(_codec == Codec_HEVC) {
                                if(md.isValid()) {
                                        slot->nvMd  = toNvencMastering(md);
                                        slot->hasMd = true;
                                        pic.codecPicParams.hevcPicParams.pMasteringDisplay = &slot->nvMd;
                                }
                                if(cll.isValid()) {
                                        slot->nvCll  = toNvencCll(cll);
                                        slot->hasCll = true;
                                        pic.codecPicParams.hevcPicParams.pMaxCll = &slot->nvCll;
                                }
                        } else if(_codec == Codec_AV1) {
                                if(md.isValid()) {
                                        slot->nvMd  = toNvencMastering(md);
                                        slot->hasMd = true;
                                        pic.codecPicParams.av1PicParams.pMasteringDisplay = &slot->nvMd;
                                }
                                if(cll.isValid()) {
                                        slot->nvCll  = toNvencCll(cll);
                                        slot->hasCll = true;
                                        pic.codecPicParams.av1PicParams.pMaxCll = &slot->nvCll;
                                }
                        }

                        NVENCSTATUS st = gNvenc.nvEncEncodePicture(_encoder, &pic);
                        if(st != NV_ENC_SUCCESS && st != NV_ENC_ERR_NEED_MORE_INPUT) {
                                _freeSlots.push_back(slot);
                                return setError(Error::LibraryFailure,
                                        String::sprintf("nvEncEncodePicture failed (%d)", (int)st));
                        }

                        slot->pts = pts;
                        slot->hasOutput = (st == NV_ENC_SUCCESS);
                        _inFlight.push_back(slot);
                        ++_frameIdx;
                        return Error::Ok;
                }

                MediaPacket::Ptr receivePacket() {
                        if(!_pendingPackets.empty()) {
                                auto pkt = _pendingPackets.front();
                                _pendingPackets.pop_front();
                                return pkt;
                        }

                        if(!_inFlight.empty() && _inFlight.front()->hasOutput) {
                                Slot *slot = _inFlight.front();
                                _inFlight.pop_front();
                                auto pkt = lockAndBuildPacket(slot);
                                _freeSlots.push_back(slot);
                                return pkt;
                        }

                        if(_eosPending && _inFlight.empty()) {
                                _eosPending = false;
                                auto pkt = MediaPacket::Ptr::create();
                                pkt.modify()->setPixelDesc(outputPixelDesc());
                                pkt.modify()->addFlag(MediaPacket::EndOfStream);
                                return pkt;
                        }

                        return MediaPacket::Ptr();
                }

                Error flush() {
                        if(!_sessionOpen) {
                                // Nothing to drain — still report EOS so
                                // the caller's drain loop terminates.
                                _eosPending = true;
                                return Error::Ok;
                        }

                        // Submit an EOS pseudo-frame; NVENC will emit any
                        // buffered output on the subsequent lockBitstream
                        // calls against slots still in _inFlight.
                        NV_ENC_PIC_PARAMS pic{};
                        pic.version        = NV_ENC_PIC_PARAMS_VER;
                        pic.encodePicFlags = NV_ENC_PIC_FLAG_EOS;
                        pic.pictureStruct  = NV_ENC_PIC_STRUCT_FRAME;
                        NVENCSTATUS st = gNvenc.nvEncEncodePicture(_encoder, &pic);
                        if(st != NV_ENC_SUCCESS) {
                                return setError(Error::LibraryFailure,
                                        String::sprintf("nvEncEncodePicture(EOS) failed (%d)", (int)st));
                        }

                        // Any previously-NEED_MORE_INPUT slots are now
                        // guaranteed to have bitstream data — mark them.
                        for(Slot *s : _inFlight) s->hasOutput = true;
                        _eosPending = true;
                        return Error::Ok;
                }

                Error reset() {
                        destroySession();
                        return Error::Ok;
                }

                PixelDesc outputPixelDesc() const {
                        if(_codec == Codec_H264) return PixelDesc(PixelDesc::H264);
                        if(_codec == Codec_AV1)  return PixelDesc(PixelDesc::AV1);
                        return PixelDesc(PixelDesc::HEVC);
                }

                Error lastError() const { return _lastError; }
                const String &lastErrorMessage() const { return _lastErrorMessage; }
                void clearError() {
                        _lastError = Error::Ok;
                        _lastErrorMessage = String();
                }

        private:
                struct Slot {
                        NV_ENC_INPUT_PTR       in         = nullptr;
                        NV_ENC_OUTPUT_PTR      out        = nullptr;
                        MediaTimeStamp         pts;
                        Metadata               imageMeta;
                        uint32_t               pitch      = 0;
                        bool                   hasOutput  = false;
                        // Per-frame HDR SEI payload.  Storage lives on
                        // the Slot (not the stack of submitFrame) because
                        // NVENC may defer consumption of the pointers in
                        // NV_ENC_PIC_PARAMS across NEED_MORE_INPUT returns
                        // when B-frames or lookahead are active.  The
                        // Slot lifetime spans submit → bitstream lock, so
                        // these pointers remain valid until NVENC has
                        // emitted the matching output packet.
                        MASTERING_DISPLAY_INFO nvMd{};
                        CONTENT_LIGHT_LEVEL    nvCll{};
                        bool                   hasMd      = false;
                        bool                   hasCll     = false;
                };

                struct Caps {
                        bool support10Bit      = false;
                        bool support422        = false;
                        bool support444        = false;
                        bool supportLossless   = false;
                        bool supportLookahead  = false;
                        bool supportTemporalAQ = false;
                        bool supportAlpha      = false;
                        int  maxBFrames        = 0;
                };

                Codec          _codec;
                MediaConfig    _cfg;
                bool           _needReconfigure = false;
                bool           _timecodeSEI     = false;

                // Session-level raster scan mode.  @c _cfgScanMode holds
                // the unresolved value as authored by the caller via
                // @ref MediaConfig::VideoScanMode (typically @c Unknown
                // for "derive from input").  @c _effectiveScanMode is the
                // resolved value applied for this session — filled in
                // at session init from @c _cfgScanMode, falling back to
                // the first frame's @c ImageDesc::videoScanMode and
                // finally to @c Progressive.  The per-frame Metadata
                // override path uses @c _effectiveScanMode as its default
                // when @c Metadata::VideoScanMode is absent.
                VideoScanMode  _cfgScanMode       { VideoScanMode::Unknown };
                VideoScanMode  _effectiveScanMode { VideoScanMode::Progressive };

                // VUI color description, captured from MediaConfig at
                // configure() time.  Values are raw H.273 numeric
                // codepoints so ensureSession() doesn't have to
                // re-resolve Enum lookups.  Special values:
                //
                //   255 — "Auto" / "Unknown" — resolve from the first
                //         frame's PixelDesc::colorModel() /
                //         PixelDesc::videoRange() when ensureSession()
                //         runs.
                //   0   — unset; treat as Unspecified on the wire.
                //   1..22 — spec-registered value written verbatim.
                uint32_t       _colorPrimaries         = 255;
                uint32_t       _transferCharacteristics = 255;
                uint32_t       _matrixCoefficients     = 255;
                // VideoRange mirrors the Unknown/Limited/Full tri-state
                // from the VideoRange TypedEnum.  Resolved against the
                // first frame at session init when Unknown.
                uint32_t       _videoRange             = 0; // VideoRange::Unknown

                const FormatEntry *_fmt = nullptr;
                Caps           _caps;
                MasteringDisplay    _masteringDisplay;
                ContentLightLevel   _contentLightLevel;

                CUdevice       _device = 0;
                CUcontext      _cudaCtx = nullptr;
                bool           _ctxRetained = false;

                void          *_encoder   = nullptr;
                bool           _sessionOpen = false;
                uint32_t       _width    = 0;
                uint32_t       _height   = 0;
                uint64_t       _frameIdx = 0;
                size_t         _numSlots = 4;

                std::vector<Slot> _slots;
                std::deque<Slot*> _freeSlots;
                std::deque<Slot*> _inFlight;

                bool           _eosPending = false;

                Error          _lastError;
                String         _lastErrorMessage;

                Error setError(Error err, const String &msg) {
                        _lastError = err;
                        _lastErrorMessage = msg;
                        promekiErr("NvencVideoEncoder: %s", msg.cstr());
                        return err;
                }

                // Holds the resolved H.273 color-description tuple
                // that actually gets written into the bitstream — the
                // output of resolveColorDescription().  Consumers use
                // these instead of the raw _colorPrimaries / _videoRange
                // members so an explicit @c Unspecified still flows
                // through unchanged while @c Auto / @c Unknown are
                // folded against the first frame's PixelDesc.
                struct ResolvedColorDesc {
                        uint32_t primaries = 0;
                        uint32_t transfer  = 0;
                        uint32_t matrix    = 0;
                        uint32_t range     = 0; // VideoRange numeric.
                };

                // Resolve the cached Auto/Unspecified/Unknown sentinels
                // against _fmt->pixelDescId, giving each VUI field its
                // concrete value for this session.  Caller-supplied
                // explicit values (BT709, SMPTE2084, Limited, ...) pass
                // through untouched so a downstream HDR10 override is
                // always honoured.  Must be called after _fmt is set.
                ResolvedColorDesc resolveColorDescription() const {
                        ResolvedColorDesc out;
                        const PixelDesc pd(_fmt->pixelDescId);
                        const ColorModel::H273 h = ColorModel::toH273(pd.colorModel().id());

                        out.primaries = (_colorPrimaries == 255u) ? h.primaries : _colorPrimaries;
                        out.transfer  = (_transferCharacteristics == 255u) ? h.transfer : _transferCharacteristics;
                        out.matrix    = (_matrixCoefficients == 255u) ? h.matrix   : _matrixCoefficients;

                        if(_videoRange == 0u /*Unknown*/) {
                                const VideoRange pdr = pd.videoRange();
                                // Map VideoRange::Limited(1) / Full(2)
                                // through; anything else stays at 0 so
                                // the VUI omits the range signalling.
                                out.range = static_cast<uint32_t>(pdr.value());
                        } else {
                                out.range = _videoRange;
                        }
                        return out;
                }

                int queryCap(NV_ENC_CAPS cap) const {
                        NV_ENC_CAPS_PARAM param{};
                        param.version = NV_ENC_CAPS_PARAM_VER;
                        param.capsToQuery = cap;
                        int val = 0;
                        NVENCSTATUS st = gNvenc.nvEncGetEncodeCaps(_encoder, codecGuid(), &param, &val);
                        if(st != NV_ENC_SUCCESS) {
                                promekiWarn("NvencVideoEncoder: nvEncGetEncodeCaps(cap=%d) failed (%d); "
                                        "treating as unsupported.", (int)cap, (int)st);
                                return 0;
                        }
                        return val;
                }

                void populateCaps() {
                        _caps.support10Bit      = queryCap(NV_ENC_CAPS_SUPPORT_10BIT_ENCODE) != 0;
                        _caps.support422        = queryCap(NV_ENC_CAPS_SUPPORT_YUV422_ENCODE) != 0;
                        _caps.support444        = queryCap(NV_ENC_CAPS_SUPPORT_YUV444_ENCODE) != 0;
                        _caps.supportLossless   = queryCap(NV_ENC_CAPS_SUPPORT_LOSSLESS_ENCODE) != 0;
                        _caps.supportLookahead  = queryCap(NV_ENC_CAPS_SUPPORT_LOOKAHEAD) != 0;
                        _caps.supportTemporalAQ = queryCap(NV_ENC_CAPS_SUPPORT_TEMPORAL_AQ) != 0;
                        _caps.supportAlpha      = queryCap(NV_ENC_CAPS_SUPPORT_ALPHA_LAYER_ENCODING) != 0;
                        _caps.maxBFrames        = queryCap(NV_ENC_CAPS_NUM_MAX_BFRAMES);
                }

                Error validateFormatCaps() const {
                        if(_fmt->inputBitDepth == NV_ENC_BIT_DEPTH_10 && !_caps.support10Bit) {
                                return Error::PixelFormatNotSupported;
                        }
                        if(_fmt->chromaFormatIDC == 2 && !_caps.support422) {
                                return Error::PixelFormatNotSupported;
                        }
                        if(_fmt->chromaFormatIDC == 3 && !_caps.support444) {
                                return Error::PixelFormatNotSupported;
                        }
                        return Error::Ok;
                }

                GUID codecGuid() const {
                        if(_codec == Codec_H264) return NV_ENC_CODEC_H264_GUID;
                        if(_codec == Codec_AV1)  return NV_ENC_CODEC_AV1_GUID;
                        return NV_ENC_CODEC_HEVC_GUID;
                }

                Error ensureSession(uint32_t w, uint32_t h, const FormatEntry *fmt,
                                    VideoScanMode firstFrameScanMode) {
                        if(_sessionOpen) {
                                if(w != _width || h != _height) {
                                        return setError(Error::Invalid,
                                                "NvencVideoEncoder does not support mid-stream resolution changes");
                                }
                                if(fmt != _fmt) {
                                        return setError(Error::Invalid,
                                                "NvencVideoEncoder does not support mid-stream format changes");
                                }
                                if(_needReconfigure) {
                                        _needReconfigure = false;
                                }
                                return Error::Ok;
                        }

                        // Resolve the session scan mode once, now that the
                        // first frame has arrived.  Caller-supplied
                        // @c MediaConfig::VideoScanMode wins when it names
                        // a concrete mode; otherwise fall through to the
                        // input @c ImageDesc::videoScanMode (populated by
                        // the previous decoder / test pattern / …); if
                        // that is also @c Unknown, settle on
                        // @c Progressive — the historical default and the
                        // safe choice when nothing upstream claimed to
                        // know the scan order.
                        if(_cfgScanMode.value() != VideoScanMode::Unknown.value()) {
                                _effectiveScanMode = _cfgScanMode;
                        } else if(firstFrameScanMode.value() != VideoScanMode::Unknown.value()) {
                                _effectiveScanMode = firstFrameScanMode;
                        } else {
                                _effectiveScanMode = VideoScanMode::Progressive;
                        }

                        if(!loadNvenc()) {
                                return setError(Error::LibraryFailure,
                                        "failed to load libnvidia-encode.so.1 (install libnvidia-encode-NNN matching your driver)");
                        }

                        if(Error err = retainCudaContext(); err.isError()) return err;

                        NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS sp{};
                        sp.version    = NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER;
                        sp.deviceType = NV_ENC_DEVICE_TYPE_CUDA;
                        sp.device     = _cudaCtx;
                        sp.apiVersion = NVENCAPI_VERSION;
                        NVENCSTATUS st = gNvenc.nvEncOpenEncodeSessionEx(&sp, &_encoder);
                        if(st != NV_ENC_SUCCESS || _encoder == nullptr) {
                                return setError(Error::LibraryFailure,
                                        String::sprintf("nvEncOpenEncodeSessionEx failed (%d)", (int)st));
                        }

                        _fmt    = fmt;
                        _width  = w;
                        _height = h;

                        populateCaps();
                        if(Error err = validateFormatCaps(); err.isError()) {
                                Error e = setError(err,
                                        String::sprintf("GPU does not support the requested format "
                                                "(chroma=%u, bitDepth=%d)",
                                                _fmt->chromaFormatIDC, (int)_fmt->inputBitDepth));
                                destroySession();
                                return e;
                        }
                        const GUID encGuid    = codecGuid();
                        const Enum presetEnum = _cfg.getAs<Enum>(MediaConfig::VideoPreset);
                        const GUID presetGuid = toNvencPreset(presetEnum);
                        const NV_ENC_TUNING_INFO tuning = toNvencTuning(presetEnum);

                        NV_ENC_PRESET_CONFIG presetCfg{};
                        presetCfg.version = NV_ENC_PRESET_CONFIG_VER;
                        presetCfg.presetCfg.version = NV_ENC_CONFIG_VER;
                        st = gNvenc.nvEncGetEncodePresetConfigEx(_encoder, encGuid, presetGuid,
                                                                 tuning, &presetCfg);
                        if(st != NV_ENC_SUCCESS) {
                                Error e = setError(Error::LibraryFailure,
                                        String::sprintf("nvEncGetEncodePresetConfigEx failed (%d)", (int)st));
                                destroySession();
                                return e;
                        }

                        NV_ENC_CONFIG encCfg = presetCfg.presetCfg;
                        encCfg.version  = NV_ENC_CONFIG_VER;

                        const int32_t bitrateKbps    = _cfg.getAs<int32_t>(MediaConfig::BitrateKbps);
                        const int32_t maxBitrateKbps = _cfg.getAs<int32_t>(MediaConfig::MaxBitrateKbps);
                        const int32_t gopLength      = _cfg.getAs<int32_t>(MediaConfig::GopLength);
                        const int32_t idrInterval    = _cfg.getAs<int32_t>(MediaConfig::IdrInterval);
                        const int32_t qp             = _cfg.getAs<int32_t>(MediaConfig::VideoQp);
                        const Enum rcEnum = _cfg.getAs<Enum>(MediaConfig::VideoRcMode);

                        encCfg.rcParams.rateControlMode = toNvencRc(rcEnum);
                        encCfg.rcParams.averageBitRate  = static_cast<uint32_t>(bitrateKbps) * 1000u;
                        encCfg.rcParams.maxBitRate      = static_cast<uint32_t>(maxBitrateKbps) * 1000u;
                        const bool lossless = (presetEnum == VideoEncoderPreset::Lossless);
                        if(lossless && _caps.supportLossless) {
                                encCfg.rcParams.rateControlMode = NV_ENC_PARAMS_RC_CONSTQP;
                                encCfg.rcParams.constQP = { 0, 0, 0 };
                        } else if(encCfg.rcParams.rateControlMode == NV_ENC_PARAMS_RC_CONSTQP) {
                                encCfg.rcParams.constQP.qpIntra  = qp;
                                encCfg.rcParams.constQP.qpInterP = qp;
                                encCfg.rcParams.constQP.qpInterB = qp;
                        }
                        if(gopLength > 0) encCfg.gopLength = gopLength;

                        const int32_t bFrames = _cfg.getAs<int32_t>(MediaConfig::BFrames);
                        int effectiveB = std::min(bFrames, _caps.maxBFrames);
                        if(effectiveB < 0) effectiveB = 0;
                        encCfg.frameIntervalP = effectiveB + 1;

                        const int32_t laFrames = _cfg.getAs<int32_t>(MediaConfig::LookaheadFrames);
                        if(laFrames > 0 && _caps.supportLookahead) {
                                encCfg.rcParams.enableLookahead = 1;
                                encCfg.rcParams.lookaheadDepth  = static_cast<uint16_t>(laFrames);
                        }

                        _numSlots = std::max(size_t(32),
                                size_t(effectiveB) * 4 + size_t(laFrames) + 32);

                        if(_cfg.getAs<bool>(MediaConfig::VideoSpatialAQ)) {
                                encCfg.rcParams.enableAQ = 1;
                                int aqStr = _cfg.getAs<int32_t>(MediaConfig::VideoSpatialAQStrength);
                                if(aqStr > 0) encCfg.rcParams.aqStrength = aqStr;
                        }
                        if(_cfg.getAs<bool>(MediaConfig::VideoTemporalAQ) && _caps.supportTemporalAQ) {
                                encCfg.rcParams.enableTemporalAQ = 1;
                        }
                        int mp = _cfg.getAs<int32_t>(MediaConfig::VideoMultiPass);
                        if(mp == 1)      encCfg.rcParams.multiPass = NV_ENC_TWO_PASS_QUARTER_RESOLUTION;
                        else if(mp == 2) encCfg.rcParams.multiPass = NV_ENC_TWO_PASS_FULL_RESOLUTION;

                        const bool repeatHdrs = _cfg.getAs<bool>(MediaConfig::VideoRepeatHeaders);
                        _timecodeSEI = _cfg.getAs<bool>(MediaConfig::VideoTimecodeSEI);
                        if(_timecodeSEI && _codec == Codec_AV1) {
                                promekiWarn("NvencVideoEncoder: VideoTimecodeSEI requested for AV1 "
                                        "but NVENC does not expose an AV1 timecode OBU path; "
                                        "timecode will not be embedded in the bitstream.");
                        }

                        // Interlaced scan modes drive codec-specific plumbing:
                        //
                        // * H.264 / HEVC: flip on @c outputPictureTimingSEI
                        //   so the bitstream carries a Picture Timing SEI
                        //   with @c pic_struct on every picture.  Field
                        //   order then rides through NV_ENC_TIME_CODE's
                        //   @c displayPicStruct, populated per-frame in
                        //   @c submitFrame.  We keep @c frameFieldMode at
                        //   the preset default (FRAME) — true field-mode
                        //   coding (MBAFF or separate-field submission)
                        //   requires splitting each frame into two field
                        //   images, which is out of scope for this pass.
                        //   Progressive-tagged pic_timing is harmless, so
                        //   we always emit the SEI when the session is
                        //   interlaced, even for otherwise "progressive"
                        //   per-frame Metadata overrides.
                        //
                        // * AV1: no interlaced signalling is exposed by
                        //   NVENC (and AV1 itself only signals progressive
                        //   / interlaced via the film-grain / AFD OBUs,
                        //   which the SDK does not emit).  Warn once and
                        //   fall through as progressive.
                        const bool sessionInterlaced = _effectiveScanMode.isInterlaced();
                        if(sessionInterlaced && _codec == Codec_AV1) {
                                promekiWarn("NvencVideoEncoder: interlaced scan mode requested "
                                        "for AV1 but NVENC does not expose an AV1 interlaced "
                                        "signalling path; emitting progressive bitstream.");
                        }

                        const String profileStr = _cfg.getAs<String>(MediaConfig::VideoProfile);
                        const String levelStr   = _cfg.getAs<String>(MediaConfig::VideoLevel);

                        const int effectiveIdr = (idrInterval > 0) ? idrInterval : encCfg.gopLength;
                        const ResolvedColorDesc color = resolveColorDescription();
                        if(_codec == Codec_H264) {
                                encCfg.profileGUID = h264ProfileGuid(profileStr, _fmt);
                                auto &h = encCfg.encodeCodecConfig.h264Config;
                                h.level           = h264Level(levelStr);
                                h.idrPeriod       = effectiveIdr;
                                h.chromaFormatIDC = _fmt->chromaFormatIDC;
                                h.inputBitDepth   = _fmt->inputBitDepth;
                                h.outputBitDepth  = _fmt->outputBitDepth;
                                if(repeatHdrs) h.repeatSPSPPS = 1;
                                // Picture Timing SEI: always on for H.264.
                                // The SPS carries pic_struct_present_flag=1
                                // and the bitstream gets a pic_timing SEI on
                                // every picture.  Two reasons to pin it on:
                                //
                                //  1. Some hardware decoders (observed:
                                //     NVDEC / VDPAU paths on certain driver
                                //     versions) assume progressive content
                                //     and allocate a compact DPB when
                                //     pic_struct is not signalled; feeding
                                //     them our streams then corrupts DPB
                                //     bookkeeping and wedges the decode
                                //     engine.  Matches what ffmpeg's
                                //     h264_nvenc emits by default.
                                //  2. enableTimeCode=1 MUST go with
                                //     outputPictureTimingSEI=1 — the other
                                //     way around causes NVENC to dereference
                                //     uninitialized HRD/timing state inside
                                //     the kernel driver and trip a panic.
                                //     Pinned-on keeps the pair consistent.
                                //
                                // The per-frame path (submitFrame) populates
                                // NV_ENC_PIC_PARAMS_H264::timeCode every
                                // frame.  When the caller doesn't want a
                                // clock-timestamp set, that path sets
                                // skipClockTimestampInsertion=1 so only
                                // displayPicStruct lands in the SEI payload.
                                h.enableTimeCode         = 1;
                                h.outputPictureTimingSEI = 1;
                                populateVuiColorDescription(h.h264VUIParameters,
                                        color.primaries, color.transfer,
                                        color.matrix, color.range);
                                // H.264 lossless (qpPrimeYZeroTransformBypassFlag)
                                // is only valid in the High 4:4:4 Predictive
                                // profile per the NVENC header and the H.264
                                // spec.  Forcing HIGH_444 on 4:2:0/4:2:2 input
                                // produces a bitstream whose chroma_format_idc
                                // conflicts with the profile — some driver
                                // versions trip validation failures inside
                                // kernel space.  Only enable the override when
                                // the input is actually 4:4:4; otherwise fall
                                // through to non-lossless CQP.
                                if(lossless && _caps.supportLossless &&
                                   _fmt->chromaFormatIDC == 3) {
                                        h.qpPrimeYZeroTransformBypassFlag = 1;
                                        encCfg.profileGUID = NV_ENC_H264_PROFILE_HIGH_444_GUID;
                                } else if(lossless) {
                                        promekiWarn("NvencVideoEncoder: H.264 lossless requires "
                                                "4:4:4 input (got chroma=%u); falling back to "
                                                "high-quality CQP.",
                                                _fmt->chromaFormatIDC);
                                }
                        } else if(_codec == Codec_HEVC) {
                                encCfg.profileGUID = hevcProfileGuid(profileStr, _fmt);
                                auto &h = encCfg.encodeCodecConfig.hevcConfig;
                                h.level           = hevcLevel(levelStr);
                                h.idrPeriod       = effectiveIdr;
                                h.chromaFormatIDC = _fmt->chromaFormatIDC;
                                h.inputBitDepth   = _fmt->inputBitDepth;
                                h.outputBitDepth  = _fmt->outputBitDepth;
                                if(repeatHdrs) h.repeatSPSPPS = 1;
                                if(_timecodeSEI || sessionInterlaced) {
                                        // HEVC routes BOTH the clock-timestamp
                                        // set and our displayPicStruct through
                                        // the Time Code SEI (payloadType 136).
                                        // The SDK header documents
                                        // NV_ENC_TIME_CODE as feeding HEVC's
                                        // Time Code SEI gated by
                                        // outputTimeCodeSEI — it does NOT feed
                                        // pic_timing SEI's pic_struct, which
                                        // is unreachable through NVENC's
                                        // public API.  Enabling
                                        // outputPictureTimingSEI without a
                                        // valid pic_struct source has been
                                        // observed to crash the kernel driver
                                        // (same mode as fix #2: SEI emission
                                        // walks uninitialised internal state),
                                        // so we leave it off and route
                                        // displayPicStruct via Time Code SEI.
                                        // Standards-compliant HEVC interlaced
                                        // signalling (pic_struct in
                                        // pic_timing) requires bypassing
                                        // NVENC's PTD entirely or rewriting
                                        // the SEI in a post-pass — both out
                                        // of scope for this iteration.
                                        h.outputTimeCodeSEI = 1;
                                }
                                if(sessionInterlaced) {
                                        promekiWarn("NvencVideoEncoder: HEVC interlaced scan mode "
                                                "requested; NVENC's public API does not expose "
                                                "pic_struct in HEVC pic_timing SEI, so field "
                                                "order will be carried only in the Time Code SEI "
                                                "displayPicStruct (non-standard for pic_struct "
                                                "semantics).  Most HEVC players read pic_struct "
                                                "from pic_timing only and will treat the output "
                                                "as progressive.");
                                }
                                populateVuiColorDescription(h.hevcVUIParameters,
                                        color.primaries, color.transfer,
                                        color.matrix, color.range);
                                if(lossless && _caps.supportLossless) {
                                        encCfg.profileGUID = NV_ENC_HEVC_PROFILE_FREXT_GUID;
                                }
                                if(_masteringDisplay.isValid())  h.outputMasteringDisplay = 1;
                                if(_contentLightLevel.isValid()) h.outputMaxCll = 1;
                        } else {
                                encCfg.profileGUID = av1ProfileGuid(profileStr);
                                auto &a = encCfg.encodeCodecConfig.av1Config;
                                a.level           = av1Level(levelStr);
                                a.idrPeriod       = effectiveIdr;
                                a.chromaFormatIDC = _fmt->chromaFormatIDC;
                                a.inputBitDepth   = _fmt->inputBitDepth;
                                a.outputBitDepth  = _fmt->outputBitDepth;
                                a.repeatSeqHdr    = repeatHdrs ? 1 : a.repeatSeqHdr;
                                if(_masteringDisplay.isValid())  a.outputMasteringDisplay = 1;
                                if(_contentLightLevel.isValid()) a.outputMaxCll = 1;
                                // AV1 carries color description as
                                // first-class fields on the codec config
                                // (not inside a VUI struct).  Unspecified
                                // (2) matches the AV1 "use default" spec
                                // convention, so we forward the resolved
                                // values even when they landed at 0
                                // after Auto-derivation failed (e.g.
                                // user-registered ColorModel with no
                                // H.273 mapping).
                                a.colorPrimaries = static_cast<NV_ENC_VUI_COLOR_PRIMARIES>(
                                        (color.primaries != 0) ? color.primaries : 2);
                                a.transferCharacteristics = static_cast<NV_ENC_VUI_TRANSFER_CHARACTERISTIC>(
                                        (color.transfer != 0) ? color.transfer : 2);
                                a.matrixCoefficients = static_cast<NV_ENC_VUI_MATRIX_COEFFS>(
                                        (color.matrix != 0) ? color.matrix : 2);
                                a.colorRange = (color.range == 2u /*Full*/) ? 1u : 0u;
                        }

                        // Honour the caller-supplied FrameRate — NVENC
                        // uses this for rate-control math (CBR / VBR
                        // bits-per-frame target, HRD buffer math) and
                        // for SPS/VUI timing info in H.264 / HEVC.  The
                        // library hook is MediaConfig::FrameRate; the
                        // MediaIOTask_VideoEncoder stamps it from the
                        // pending MediaDesc before calling configure().
                        const FrameRate fallback(FrameRate::RationalType(30, 1));
                        FrameRate fr = _cfg.getAs<FrameRate>(MediaConfig::FrameRate, fallback);
                        if(!fr.isValid()) fr = fallback;

                        NV_ENC_INITIALIZE_PARAMS init{};
                        init.version         = NV_ENC_INITIALIZE_PARAMS_VER;
                        init.encodeGUID      = encGuid;
                        init.presetGUID      = presetGuid;
                        init.tuningInfo      = tuning;
                        init.encodeWidth     = _width;
                        init.encodeHeight    = _height;
                        init.darWidth        = _width;
                        init.darHeight       = _height;
                        init.frameRateNum    = fr.numerator();
                        init.frameRateDen    = fr.denominator();
                        init.enablePTD       = 1;
                        init.encodeConfig    = &encCfg;

                        st = gNvenc.nvEncInitializeEncoder(_encoder, &init);
                        if(st != NV_ENC_SUCCESS) {
                                Error e = setError(Error::LibraryFailure,
                                        String::sprintf("nvEncInitializeEncoder failed (%d)", (int)st));
                                destroySession();
                                return e;
                        }

                        if(Error err = allocateSlots(); err.isError()) {
                                destroySession();
                                return err;
                        }

                        _sessionOpen = true;
                        clearError();
                        return Error::Ok;
                }

                Error retainCudaContext() {
                        if(_ctxRetained) return Error::Ok;

                        if(Error err = CudaBootstrap::ensureRegistered(); err.isError()) {
                                return setError(err, "CudaBootstrap::ensureRegistered failed");
                        }
                        // The library doesn't pick a device for the user
                        // — honour whatever the current thread already
                        // selected via CudaDevice::setCurrent (defaulting
                        // to device 0 when nothing was selected yet).
                        if(Error err = CudaDevice::setCurrent(0); err.isError()) {
                                return setError(err, "CudaDevice::setCurrent failed");
                        }

                        CUresult cr = cuInit(0);
                        if(cr != CUDA_SUCCESS) {
                                return setError(Error::LibraryFailure,
                                        String::sprintf("cuInit failed (%d)", (int)cr));
                        }
                        cr = cuDeviceGet(&_device, 0);
                        if(cr != CUDA_SUCCESS) {
                                return setError(Error::LibraryFailure,
                                        String::sprintf("cuDeviceGet failed (%d)", (int)cr));
                        }
                        cr = cuDevicePrimaryCtxRetain(&_cudaCtx, _device);
                        if(cr != CUDA_SUCCESS || _cudaCtx == nullptr) {
                                return setError(Error::LibraryFailure,
                                        String::sprintf("cuDevicePrimaryCtxRetain failed (%d)", (int)cr));
                        }
                        _ctxRetained = true;
                        return Error::Ok;
                }

                Error allocateSlots() {
                        _slots.resize(_numSlots);
                        for(size_t i = 0; i < _numSlots; ++i) {
                                Slot &s = _slots[i];

                                NV_ENC_CREATE_INPUT_BUFFER cin{};
                                cin.version    = NV_ENC_CREATE_INPUT_BUFFER_VER;
                                cin.width      = _width;
                                cin.height     = _height;
                                cin.bufferFmt  = _fmt->nvencFmt;
                                NVENCSTATUS st = gNvenc.nvEncCreateInputBuffer(_encoder, &cin);
                                if(st != NV_ENC_SUCCESS) {
                                        return setError(Error::LibraryFailure,
                                                String::sprintf("nvEncCreateInputBuffer failed (%d)", (int)st));
                                }
                                s.in = cin.inputBuffer;

                                NV_ENC_CREATE_BITSTREAM_BUFFER cb{};
                                cb.version = NV_ENC_CREATE_BITSTREAM_BUFFER_VER;
                                st = gNvenc.nvEncCreateBitstreamBuffer(_encoder, &cb);
                                if(st != NV_ENC_SUCCESS) {
                                        return setError(Error::LibraryFailure,
                                                String::sprintf("nvEncCreateBitstreamBuffer failed (%d)", (int)st));
                                }
                                s.out = cb.bitstreamBuffer;

                                _freeSlots.push_back(&s);
                        }
                        return Error::Ok;
                }

                Slot *acquireFreeSlot() {
                        if(_freeSlots.empty()) {
                                // Drain completed slots from the head of
                                // the in-flight queue.  With B-frames,
                                // only slots with hasOutput can be safely
                                // recycled — others are still held by
                                // the encoder as reference frames.
                                while(!_inFlight.empty() && _inFlight.front()->hasOutput) {
                                        Slot *head = _inFlight.front();
                                        _inFlight.pop_front();
                                        if(auto pkt = lockAndBuildPacket(head)) {
                                                _pendingPackets.push_back(pkt);
                                        }
                                        _freeSlots.push_back(head);
                                }
                                if(_freeSlots.empty()) return nullptr;
                        }
                        Slot *s = _freeSlots.front();
                        _freeSlots.pop_front();
                        s->hasOutput = false;
                        return s;
                }

                Error uploadFrame(const Image &frame, Slot *slot) {
                        NV_ENC_LOCK_INPUT_BUFFER lk{};
                        lk.version = NV_ENC_LOCK_INPUT_BUFFER_VER;
                        lk.inputBuffer = slot->in;
                        NVENCSTATUS st = gNvenc.nvEncLockInputBuffer(_encoder, &lk);
                        if(st != NV_ENC_SUCCESS) {
                                return setError(Error::LibraryFailure,
                                        String::sprintf("nvEncLockInputBuffer failed (%d)", (int)st));
                        }

                        auto *dst      = static_cast<uint8_t *>(lk.bufferDataPtr);
                        const auto pitch = lk.pitch;
                        slot->pitch    = pitch;

                        const uint32_t rowBytes = _fmt->bytesPerPixelY * _width;

                        if(_fmt->planeCount == 3) {
                                // Accumulate the destination offset from the
                                // actual per-plane row counts so adding a
                                // planar 4:2:0 / 4:2:2 entry to kFormatTable
                                // later can't silently overrun the NVENC
                                // surface — a naive `p * _height * pitch`
                                // only works when every plane is full-height.
                                size_t planeOffset = 0;
                                for(uint32_t p = 0; p < 3; ++p) {
                                        const uint32_t planeRows = (p == 0)
                                                ? _height
                                                : _height / _fmt->uvHeightDivisor;
                                        const uint8_t *src = static_cast<const uint8_t *>(
                                                frame.plane(p)->data());
                                        const size_t srcStride = frame.lineStride(p);
                                        uint8_t *planeDst = dst + planeOffset;
                                        for(uint32_t row = 0; row < planeRows; ++row) {
                                                std::memcpy(planeDst + row * pitch,
                                                            src + row * srcStride,
                                                            rowBytes);
                                        }
                                        planeOffset += size_t(planeRows) * pitch;
                                }
                        } else {
                                const uint8_t *yPlane = static_cast<const uint8_t *>(
                                        frame.plane(0)->data());
                                const size_t srcYStride = frame.lineStride(0);
                                for(uint32_t row = 0; row < _height; ++row) {
                                        std::memcpy(dst + row * pitch,
                                                    yPlane + row * srcYStride,
                                                    rowBytes);
                                }

                                const uint32_t uvRows = _height / _fmt->uvHeightDivisor;
                                const uint8_t *uvPlane = frame.planes().size() > 1
                                        ? static_cast<const uint8_t *>(frame.plane(1)->data())
                                        : yPlane + srcYStride * _height;
                                const size_t srcUVStride = frame.planes().size() > 1
                                        ? frame.lineStride(1)
                                        : rowBytes;

                                uint8_t *uvDst = dst + _height * pitch;
                                for(uint32_t row = 0; row < uvRows; ++row) {
                                        std::memcpy(uvDst + row * pitch,
                                                    uvPlane + row * srcUVStride,
                                                    rowBytes);
                                }
                        }

                        st = gNvenc.nvEncUnlockInputBuffer(_encoder, slot->in);
                        if(st != NV_ENC_SUCCESS) {
                                return setError(Error::LibraryFailure,
                                        String::sprintf("nvEncUnlockInputBuffer failed (%d)", (int)st));
                        }
                        return Error::Ok;
                }

                MediaPacket::Ptr lockAndBuildPacket(Slot *slot) {
                        NV_ENC_LOCK_BITSTREAM lb{};
                        lb.version = NV_ENC_LOCK_BITSTREAM_VER;
                        lb.outputBitstream = slot->out;
                        NVENCSTATUS st = gNvenc.nvEncLockBitstream(_encoder, &lb);
                        if(st != NV_ENC_SUCCESS) {
                                setError(Error::LibraryFailure,
                                        String::sprintf("nvEncLockBitstream failed (%d)", (int)st));
                                return MediaPacket::Ptr();
                        }

                        auto buf = Buffer::Ptr::create(lb.bitstreamSizeInBytes);
                        std::memcpy(buf.modify()->data(), lb.bitstreamBufferPtr,
                                    lb.bitstreamSizeInBytes);
                        buf.modify()->setSize(lb.bitstreamSizeInBytes);
                        const bool isKey = (lb.pictureType == NV_ENC_PIC_TYPE_IDR
                                         || lb.pictureType == NV_ENC_PIC_TYPE_I);

                        gNvenc.nvEncUnlockBitstream(_encoder, slot->out);

                        auto pkt = MediaPacket::Ptr::create(buf, outputPixelDesc());
                        pkt.modify()->setPts(slot->pts);
                        pkt.modify()->setDts(slot->pts);
                        if(isKey) pkt.modify()->addFlag(MediaPacket::Keyframe);
                        // Carry per-image metadata across the codec
                        // boundary: things like Timecode and user keys
                        // that don't live in the H.264 / HEVC bitstream
                        // ride along on the MediaPacket and get
                        // re-applied to the decoded Image by the
                        // matching VideoDecoder.
                        if(!slot->imageMeta.isEmpty()) {
                                pkt.modify()->metadata() = slot->imageMeta;
                                slot->imageMeta = Metadata();
                        }
                        return pkt;
                }

                void destroySession() {
                        // Tolerate being called on a partially-initialized
                        // session: ensureSession() acquires the encoder
                        // handle before it has flipped _sessionOpen to true,
                        // and any error path between those two points
                        // must still reclaim the handle or it leaks until
                        // process exit (with the associated GPU-side state).
                        if(_encoder) {
                                for(Slot &s : _slots) {
                                        if(s.in)  gNvenc.nvEncDestroyInputBuffer(_encoder, s.in);
                                        if(s.out) gNvenc.nvEncDestroyBitstreamBuffer(_encoder, s.out);
                                }
                                _slots.clear();
                                _freeSlots.clear();
                                _inFlight.clear();
                                _pendingPackets.clear();
                                gNvenc.nvEncDestroyEncoder(_encoder);
                                _encoder = nullptr;
                        }
                        _sessionOpen = false;
                        if(_ctxRetained) {
                                cuDevicePrimaryCtxRelease(_device);
                                _ctxRetained = false;
                                _cudaCtx = nullptr;
                        }
                        _width = _height = 0;
                        _frameIdx = 0;
                        _eosPending = false;
                }

                std::deque<MediaPacket::Ptr> _pendingPackets;
};

// ---------------------------------------------------------------------------
// NvencVideoEncoder thin shims that forward to Impl.
// ---------------------------------------------------------------------------

NvencVideoEncoder::NvencVideoEncoder(Codec codec)
        : _impl(new Impl(codec)), _codec(codec) {}

NvencVideoEncoder::~NvencVideoEncoder() { delete _impl; }

String NvencVideoEncoder::name() const {
        if(_codec == Codec_H264) return String("H264");
        if(_codec == Codec_AV1)  return String("AV1");
        return String("HEVC");
}

String NvencVideoEncoder::description() const {
        if(_codec == Codec_H264) return String("NVIDIA NVENC H.264 hardware encoder");
        if(_codec == Codec_AV1)  return String("NVIDIA NVENC AV1 hardware encoder");
        return String("NVIDIA NVENC HEVC hardware encoder");
}

PixelDesc NvencVideoEncoder::outputPixelDesc() const {
        return _impl->outputPixelDesc();
}

List<int> NvencVideoEncoder::supportedInputs() const {
        List<int> ret;
        for(const auto &e : kFormatTable) {
                ret.pushToBack(static_cast<int>(e.pixelDescId));
        }
        return ret;
}

void NvencVideoEncoder::configure(const MediaConfig &config) {
        _impl->configure(config);
}

Error NvencVideoEncoder::submitFrame(const Image &frame, const MediaTimeStamp &pts) {
        _impl->clearError();
        Error err = _impl->submitFrame(frame, pts, _requestKey);
        _requestKey = false;
        // Propagate the last error onto the public-facing storage so
        // callers reading lastError() / lastErrorMessage() see the same
        // code the Impl recorded internally.
        _lastError        = _impl->lastError();
        _lastErrorMessage = _impl->lastErrorMessage();
        return err;
}

MediaPacket::Ptr NvencVideoEncoder::receivePacket() {
        return _impl->receivePacket();
}

Error NvencVideoEncoder::flush() {
        _impl->clearError();
        Error err = _impl->flush();
        _lastError        = _impl->lastError();
        _lastErrorMessage = _impl->lastErrorMessage();
        return err;
}

Error NvencVideoEncoder::reset() {
        _impl->clearError();
        Error err = _impl->reset();
        _lastError        = _impl->lastError();
        _lastErrorMessage = _impl->lastErrorMessage();
        return err;
}

void NvencVideoEncoder::requestKeyframe() { _requestKey = true; }

// ---------------------------------------------------------------------------
// Factory registration.  Two surfaces:
//   1. The legacy string-keyed VideoEncoder::registerEncoder registry —
//      what MediaIOTask_VideoEncoder still looks up via MediaConfig::VideoCodec.
//   2. The typed VideoCodec::H264 / VideoCodec::HEVC factory hooks —
//      the long-term path; populated by re-registering each codec's
//      Data record with createEncoder filled in.
// Both surfaces will collapse into one once MediaConfig::VideoCodec
// flips from TypeString to TypeVideoCodec.
// ---------------------------------------------------------------------------

namespace {

struct NvencRegistrar {
        NvencRegistrar() {
                auto h264Factory = []() -> VideoEncoder * {
                        return new NvencVideoEncoder(NvencVideoEncoder::Codec_H264);
                };
                auto hevcFactory = []() -> VideoEncoder * {
                        return new NvencVideoEncoder(NvencVideoEncoder::Codec_HEVC);
                };
                auto av1Factory = []() -> VideoEncoder * {
                        return new NvencVideoEncoder(NvencVideoEncoder::Codec_AV1);
                };

                VideoEncoder::registerEncoder("H264", h264Factory);
                VideoEncoder::registerEncoder("HEVC", hevcFactory);
                VideoEncoder::registerEncoder("AV1",  av1Factory);

                if(VideoCodec h264(VideoCodec::H264); h264.isValid()) {
                        VideoCodec::Data d = *h264.data();
                        d.createEncoder = h264Factory;
                        VideoCodec::registerData(std::move(d));
                }
                if(VideoCodec hevc(VideoCodec::HEVC); hevc.isValid()) {
                        VideoCodec::Data d = *hevc.data();
                        d.createEncoder = hevcFactory;
                        VideoCodec::registerData(std::move(d));
                }
                if(VideoCodec av1(VideoCodec::AV1); av1.isValid()) {
                        VideoCodec::Data d = *av1.data();
                        d.createEncoder = av1Factory;
                        VideoCodec::registerData(std::move(d));
                }
        }
};

static NvencRegistrar _nvencRegistrar;

} // namespace

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NVENC
