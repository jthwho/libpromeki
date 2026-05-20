/**
 * @file      nvencvideoencoder.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 */

#include <promeki/nvencvideoencoder.h>
#include <promeki/compressedvideopayload.h>
#include <promeki/uncompressedvideopayload.h>

#if PROMEKI_ENABLE_NVENC

#include <promeki/mediaconfig.h>
#include <promeki/imagedesc.h>
#include <promeki/pixelformat.h>
#include <promeki/buffer.h>
#include <promeki/cuda.h>
#include <promeki/duration.h>
#include <promeki/framerate.h>
#include <promeki/logger.h>
#include <promeki/enums.h>
#include <promeki/metadata.h>
#include <promeki/videocodec.h>
#include <promeki/masteringdisplay.h>
#include <promeki/contentlightlevel.h>
#include <promeki/timecode.h>
#include <promeki/deque.h>
#include <promeki/list.h>
#include <promeki/mutex.h>
#include <promeki/pair.h>
#include <promeki/ancformat.h>
#include <promeki/ancpacket.h>
#include <promeki/anctranslator.h>
#include <promeki/anctranslateconfig.h>

#include <cstring>
#include <cstdint>
#include <dlfcn.h>

#include <cuda.h> // Driver API — CUcontext / cuInit / cuDevicePrimaryCtxRetain.
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
        Mutex                       gNvencMutex;

        bool loadNvencLocked() {
                if (gNvencLoaded) return true;

                void *lib = dlopen("libnvidia-encode.so.1", RTLD_NOW | RTLD_LOCAL);
                if (!lib) {
                        promekiErr("NVENC: dlopen(libnvidia-encode.so.1) failed: %s", dlerror());
                        return false;
                }

                using CreateFn = NVENCSTATUS(NVENCAPI *)(NV_ENCODE_API_FUNCTION_LIST *);
                auto createFn = reinterpret_cast<CreateFn>(dlsym(lib, "NvEncodeAPICreateInstance"));
                if (!createFn) {
                        promekiErr("NVENC: dlsym(NvEncodeAPICreateInstance) failed: %s", dlerror());
                        dlclose(lib);
                        return false;
                }

                NV_ENCODE_API_FUNCTION_LIST fl{};
                fl.version = NV_ENCODE_API_FUNCTION_LIST_VER;
                NVENCSTATUS st = createFn(&fl);
                if (st != NV_ENC_SUCCESS) {
                        promekiErr("NVENC: NvEncodeAPICreateInstance failed (status %d)", (int)st);
                        return false;
                }

                gNvenc = fl;
                gNvencLoaded = true;
                return true;
        }

        bool loadNvenc() {
                Mutex::Locker lock(gNvencMutex);
                return loadNvencLocked();
        }

        // ---------------------------------------------------------------------------
        // Format dispatch table.  Each row maps a PixelFormat to the NVENC
        // buffer format, chroma / bit-depth config values, and the byte
        // geometry needed by the generic uploadFrame() routine.
        // ---------------------------------------------------------------------------

        struct FormatEntry {
                        PixelFormat::ID      pixelFormatId;
                        NV_ENC_BUFFER_FORMAT nvencFmt;
                        uint32_t             chromaFormatIDC; // 1=420, 2=422, 3=444
                        NV_ENC_BIT_DEPTH     inputBitDepth;
                        NV_ENC_BIT_DEPTH     outputBitDepth;
                        uint32_t             bytesPerPixelY;  // 1 for 8-bit, 2 for 10/16-bit
                        uint32_t             uvHeightDivisor; // 2 for 420, 1 for 422/444
                        uint32_t             planeCount;      // 2 for semi-planar, 3 for planar
        };

        static constexpr FormatEntry kFormatTable[] = {
                {PixelFormat::YUV8_420_SemiPlanar_Rec709, NV_ENC_BUFFER_FORMAT_NV12, 1, NV_ENC_BIT_DEPTH_8,
                 NV_ENC_BIT_DEPTH_8, 1, 2, 2},
                {PixelFormat::YUV10_420_SemiPlanar_LE_Rec709, NV_ENC_BUFFER_FORMAT_YUV420_10BIT, 1, NV_ENC_BIT_DEPTH_10,
                 NV_ENC_BIT_DEPTH_10, 2, 2, 2},
                {PixelFormat::YUV8_422_SemiPlanar_Rec709, NV_ENC_BUFFER_FORMAT_NV16, 2, NV_ENC_BIT_DEPTH_8,
                 NV_ENC_BIT_DEPTH_8, 1, 1, 2},
                {PixelFormat::YUV10_422_SemiPlanar_LE_Rec709, NV_ENC_BUFFER_FORMAT_P210, 2, NV_ENC_BIT_DEPTH_10,
                 NV_ENC_BIT_DEPTH_10, 2, 1, 2},
                {PixelFormat::YUV8_444_Planar_Rec709, NV_ENC_BUFFER_FORMAT_YUV444, 3, NV_ENC_BIT_DEPTH_8,
                 NV_ENC_BIT_DEPTH_8, 1, 1, 3},
                {PixelFormat::YUV10_444_Planar_LE_Rec709, NV_ENC_BUFFER_FORMAT_YUV444_10BIT, 3, NV_ENC_BIT_DEPTH_10,
                 NV_ENC_BIT_DEPTH_10, 2, 1, 3},
                // HDR variants: same NVENC buffer format / byte layout
                // as the SDR sibling above, only the bound ColorModel
                // differs.  resolveColorDescription() reads the
                // PixelFormat's ColorModel via ColorModel::toH273() so
                // these rows naturally signal transfer=16 (PQ) /
                // transfer=18 (HLG) and primaries=9 (BT.2020) on the
                // VUI without further codec-side work.  P010 is the
                // practical HEVC Main 10 / AV1 HDR carrier; the other
                // HDR PixelFormat IDs in the catalog (V210, P216,
                // RGB10A2, RGB16, half-float linear, DCI-P3) have no
                // matching NV_ENC_BUFFER_FORMAT_* in NVENC, so they
                // are intentionally not represented here.
                {PixelFormat::YUV10_420_SemiPlanar_LE_Rec2020_PQ, NV_ENC_BUFFER_FORMAT_YUV420_10BIT, 1,
                 NV_ENC_BIT_DEPTH_10, NV_ENC_BIT_DEPTH_10, 2, 2, 2},
                {PixelFormat::YUV10_420_SemiPlanar_LE_Rec2020_HLG, NV_ENC_BUFFER_FORMAT_YUV420_10BIT, 1,
                 NV_ENC_BIT_DEPTH_10, NV_ENC_BIT_DEPTH_10, 2, 2, 2},
        };

        const FormatEntry *lookupFormat(PixelFormat::ID id) {
                for (const auto &e : kFormatTable) {
                        if (e.pixelFormatId == id) return &e;
                }
                return nullptr;
        }

        // ---------------------------------------------------------------------------
        // MediaConfig → NVENC parameter translation.
        // ---------------------------------------------------------------------------

        NV_ENC_PARAMS_RC_MODE toNvencRc(const Enum &rc) {
                if (rc == RateControlMode::CBR) return NV_ENC_PARAMS_RC_CBR;
                if (rc == RateControlMode::CQP) return NV_ENC_PARAMS_RC_CONSTQP;
                return NV_ENC_PARAMS_RC_VBR;
        }

        GUID toNvencPreset(const Enum &p) {
                if (p == VideoEncoderPreset::UltraLowLatency) return NV_ENC_PRESET_P1_GUID;
                if (p == VideoEncoderPreset::LowLatency) return NV_ENC_PRESET_P3_GUID;
                if (p == VideoEncoderPreset::HighQuality) return NV_ENC_PRESET_P6_GUID;
                if (p == VideoEncoderPreset::Lossless) return NV_ENC_PRESET_P2_GUID;
                return NV_ENC_PRESET_P4_GUID;
        }

        FrameType toFrameType(NV_ENC_PIC_TYPE t) {
                switch (t) {
                        case NV_ENC_PIC_TYPE_P: return FrameType::P;
                        case NV_ENC_PIC_TYPE_B: return FrameType::B;
                        case NV_ENC_PIC_TYPE_I: return FrameType::I;
                        case NV_ENC_PIC_TYPE_IDR: return FrameType::IDR;
                        case NV_ENC_PIC_TYPE_BI: return FrameType::B;
                        case NV_ENC_PIC_TYPE_SKIPPED: return FrameType::P;
                        case NV_ENC_PIC_TYPE_INTRA_REFRESH: return FrameType::I;
                        case NV_ENC_PIC_TYPE_NONREF_P: return FrameType::P;
                        case NV_ENC_PIC_TYPE_SWITCH: return FrameType::P;
                        case NV_ENC_PIC_TYPE_UNKNOWN: return FrameType::Unknown;
                }
                return FrameType::Unknown;
        }

        NV_ENC_TUNING_INFO toNvencTuning(const Enum &p) {
                if (p == VideoEncoderPreset::UltraLowLatency) return NV_ENC_TUNING_INFO_ULTRA_LOW_LATENCY;
                if (p == VideoEncoderPreset::LowLatency) return NV_ENC_TUNING_INFO_LOW_LATENCY;
                if (p == VideoEncoderPreset::HighQuality) return NV_ENC_TUNING_INFO_HIGH_QUALITY;
                if (p == VideoEncoderPreset::Lossless) return NV_ENC_TUNING_INFO_LOSSLESS;
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
                if (name == "baseline") return NV_ENC_H264_PROFILE_BASELINE_GUID;
                if (name == "main") return NV_ENC_H264_PROFILE_MAIN_GUID;
                if (name == "high") return NV_ENC_H264_PROFILE_HIGH_GUID;
                if (name == "high10") return NV_ENC_H264_PROFILE_HIGH_10_GUID;
                if (name == "high422") return NV_ENC_H264_PROFILE_HIGH_422_GUID;
                if (name == "high444") return NV_ENC_H264_PROFILE_HIGH_444_GUID;
                if (name == "progressive") return NV_ENC_H264_PROFILE_PROGRESSIVE_HIGH_GUID;
                if (!name.isEmpty()) return NV_ENC_CODEC_PROFILE_AUTOSELECT_GUID;
                if (fmt->chromaFormatIDC == 3) return NV_ENC_H264_PROFILE_HIGH_444_GUID;
                if (fmt->chromaFormatIDC == 2) return NV_ENC_H264_PROFILE_HIGH_422_GUID;
                if (fmt->inputBitDepth == NV_ENC_BIT_DEPTH_10) return NV_ENC_H264_PROFILE_HIGH_10_GUID;
                return NV_ENC_CODEC_PROFILE_AUTOSELECT_GUID;
        }

        GUID hevcProfileGuid(const String &name, const FormatEntry *fmt) {
                if (name == "main") return NV_ENC_HEVC_PROFILE_MAIN_GUID;
                if (name == "main10") return NV_ENC_HEVC_PROFILE_MAIN10_GUID;
                if (name == "rext") return NV_ENC_HEVC_PROFILE_FREXT_GUID;
                if (!name.isEmpty()) return NV_ENC_CODEC_PROFILE_AUTOSELECT_GUID;
                if (fmt->chromaFormatIDC >= 2) return NV_ENC_HEVC_PROFILE_FREXT_GUID;
                if (fmt->inputBitDepth == NV_ENC_BIT_DEPTH_10) return NV_ENC_HEVC_PROFILE_MAIN10_GUID;
                return NV_ENC_CODEC_PROFILE_AUTOSELECT_GUID;
        }

        GUID av1ProfileGuid(const String &) {
                return NV_ENC_AV1_PROFILE_MAIN_GUID;
        }

        uint32_t h264Level(const String &s) {
                if (s.isEmpty()) return NV_ENC_LEVEL_AUTOSELECT;
                if (s == "1.0" || s == "1") return NV_ENC_LEVEL_H264_1;
                if (s == "1b") return NV_ENC_LEVEL_H264_1b;
                if (s == "1.1") return NV_ENC_LEVEL_H264_11;
                if (s == "1.2") return NV_ENC_LEVEL_H264_12;
                if (s == "1.3") return NV_ENC_LEVEL_H264_13;
                if (s == "2.0" || s == "2") return NV_ENC_LEVEL_H264_2;
                if (s == "2.1") return NV_ENC_LEVEL_H264_21;
                if (s == "2.2") return NV_ENC_LEVEL_H264_22;
                if (s == "3.0" || s == "3") return NV_ENC_LEVEL_H264_3;
                if (s == "3.1") return NV_ENC_LEVEL_H264_31;
                if (s == "3.2") return NV_ENC_LEVEL_H264_32;
                if (s == "4.0" || s == "4") return NV_ENC_LEVEL_H264_4;
                if (s == "4.1") return NV_ENC_LEVEL_H264_41;
                if (s == "4.2") return NV_ENC_LEVEL_H264_42;
                if (s == "5.0" || s == "5") return NV_ENC_LEVEL_H264_5;
                if (s == "5.1") return NV_ENC_LEVEL_H264_51;
                if (s == "5.2") return NV_ENC_LEVEL_H264_52;
                if (s == "6.0" || s == "6") return NV_ENC_LEVEL_H264_60;
                if (s == "6.1") return NV_ENC_LEVEL_H264_61;
                if (s == "6.2") return NV_ENC_LEVEL_H264_62;
                return NV_ENC_LEVEL_AUTOSELECT;
        }

        uint32_t hevcLevel(const String &s) {
                if (s.isEmpty()) return NV_ENC_LEVEL_AUTOSELECT;
                if (s == "1.0" || s == "1") return NV_ENC_LEVEL_HEVC_1;
                if (s == "2.0" || s == "2") return NV_ENC_LEVEL_HEVC_2;
                if (s == "2.1") return NV_ENC_LEVEL_HEVC_21;
                if (s == "3.0" || s == "3") return NV_ENC_LEVEL_HEVC_3;
                if (s == "3.1") return NV_ENC_LEVEL_HEVC_31;
                if (s == "4.0" || s == "4") return NV_ENC_LEVEL_HEVC_4;
                if (s == "4.1") return NV_ENC_LEVEL_HEVC_41;
                if (s == "5.0" || s == "5") return NV_ENC_LEVEL_HEVC_5;
                if (s == "5.1") return NV_ENC_LEVEL_HEVC_51;
                if (s == "5.2") return NV_ENC_LEVEL_HEVC_52;
                if (s == "6.0" || s == "6") return NV_ENC_LEVEL_HEVC_6;
                if (s == "6.1") return NV_ENC_LEVEL_HEVC_61;
                if (s == "6.2") return NV_ENC_LEVEL_HEVC_62;
                return NV_ENC_LEVEL_AUTOSELECT;
        }

        uint32_t av1Level(const String &s) {
                if (s.isEmpty()) return NV_ENC_LEVEL_AV1_AUTOSELECT;
                if (s == "2.0" || s == "2") return NV_ENC_LEVEL_AV1_2;
                if (s == "2.1") return NV_ENC_LEVEL_AV1_21;
                if (s == "2.2") return NV_ENC_LEVEL_AV1_22;
                if (s == "2.3") return NV_ENC_LEVEL_AV1_23;
                if (s == "3.0" || s == "3") return NV_ENC_LEVEL_AV1_3;
                if (s == "3.1") return NV_ENC_LEVEL_AV1_31;
                if (s == "3.2") return NV_ENC_LEVEL_AV1_32;
                if (s == "3.3") return NV_ENC_LEVEL_AV1_33;
                if (s == "4.0" || s == "4") return NV_ENC_LEVEL_AV1_4;
                if (s == "4.1") return NV_ENC_LEVEL_AV1_41;
                if (s == "4.2") return NV_ENC_LEVEL_AV1_42;
                if (s == "4.3") return NV_ENC_LEVEL_AV1_43;
                if (s == "5.0" || s == "5") return NV_ENC_LEVEL_AV1_5;
                if (s == "5.1") return NV_ENC_LEVEL_AV1_51;
                if (s == "5.2") return NV_ENC_LEVEL_AV1_52;
                if (s == "5.3") return NV_ENC_LEVEL_AV1_53;
                if (s == "6.0" || s == "6") return NV_ENC_LEVEL_AV1_6;
                if (s == "6.1") return NV_ENC_LEVEL_AV1_61;
                if (s == "6.2") return NV_ENC_LEVEL_AV1_62;
                if (s == "6.3") return NV_ENC_LEVEL_AV1_63;
                if (s == "7.0" || s == "7") return NV_ENC_LEVEL_AV1_7;
                if (s == "7.1") return NV_ENC_LEVEL_AV1_71;
                if (s == "7.2") return NV_ENC_LEVEL_AV1_72;
                if (s == "7.3") return NV_ENC_LEVEL_AV1_73;
                return NV_ENC_LEVEL_AV1_AUTOSELECT;
        }

        MASTERING_DISPLAY_INFO toNvencMastering(const MasteringDisplay &md) {
                MASTERING_DISPLAY_INFO info{};
                info.r.x = static_cast<uint16_t>(md.red().x() * 50000.0 + 0.5);
                info.r.y = static_cast<uint16_t>(md.red().y() * 50000.0 + 0.5);
                info.g.x = static_cast<uint16_t>(md.green().x() * 50000.0 + 0.5);
                info.g.y = static_cast<uint16_t>(md.green().y() * 50000.0 + 0.5);
                info.b.x = static_cast<uint16_t>(md.blue().x() * 50000.0 + 0.5);
                info.b.y = static_cast<uint16_t>(md.blue().y() * 50000.0 + 0.5);
                info.whitePoint.x = static_cast<uint16_t>(md.whitePoint().x() * 50000.0 + 0.5);
                info.whitePoint.y = static_cast<uint16_t>(md.whitePoint().y() * 50000.0 + 0.5);
                info.maxLuma = static_cast<uint32_t>(md.maxLuminance() * 10000.0 + 0.5);
                info.minLuma = static_cast<uint32_t>(md.minLuminance() * 10000.0 + 0.5);
                return info;
        }

        CONTENT_LIGHT_LEVEL toNvencCll(const ContentLightLevel &cll) {
                CONTENT_LIGHT_LEVEL info{};
                info.maxContentLightLevel = static_cast<uint16_t>(std::min(cll.maxCLL(), uint32_t(65535)));
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
        void populateVuiColorDescription(NV_ENC_CONFIG_H264_VUI_PARAMETERS &vui, uint32_t primaries, uint32_t transfer,
                                         uint32_t matrix, uint32_t videoRange) {
                const bool haveAny = (primaries != 0 && primaries != 2) || (transfer != 0 && transfer != 2) ||
                                     (matrix != 0 && matrix != 2);
                const bool haveRange = (videoRange == 1 /*Limited*/ || videoRange == 2 /*Full*/);
                if (!haveAny && !haveRange) return;

                vui.videoSignalTypePresentFlag = 1;
                vui.videoFormat = NV_ENC_VUI_VIDEO_FORMAT_UNSPECIFIED;
                // videoFullRangeFlag is a single bit — map Limited→0, Full→1,
                // Unknown→0 (safest SDR default when only colour description
                // is present).
                vui.videoFullRangeFlag = (videoRange == 2 /*Full*/) ? 1 : 0;

                if (haveAny) {
                        vui.colourDescriptionPresentFlag = 1;
                        vui.colourPrimaries = static_cast<NV_ENC_VUI_COLOR_PRIMARIES>((primaries != 0) ? primaries : 2);
                        vui.transferCharacteristics =
                                static_cast<NV_ENC_VUI_TRANSFER_CHARACTERISTIC>((transfer != 0) ? transfer : 2);
                        vui.colourMatrix = static_cast<NV_ENC_VUI_MATRIX_COEFFS>((matrix != 0) ? matrix : 2);
                }
        }

        // Map a promeki VideoScanMode to NVENC's per-picture
        // NV_ENC_DISPLAY_PIC_STRUCT.  Values outside the interlaced set fold
        // to @c _DISPLAY_FRAME so progressive and @c PsF both behave as
        // display-frame.  @c PsF is progressive content being transported as
        // two fields — the bitstream carries it as frames, so signalling
        // frame is correct for downstream displays.
        NV_ENC_DISPLAY_PIC_STRUCT toNvencDisplayPicStruct(VideoScanMode mode) {
                if (mode == VideoScanMode::InterlacedEvenFirst || mode == VideoScanMode::Interlaced) {
                        // InterlacedEvenFirst == top field first.  Plain
                        // @c Interlaced inherits the broadcast norm (top
                        // first) because without a field-order hint the
                        // safest default is the HD/SDI-style top-first order.
                        return NV_ENC_PIC_STRUCT_DISPLAY_FIELD_TOP_BOTTOM;
                }
                if (mode == VideoScanMode::InterlacedOddFirst) {
                        return NV_ENC_PIC_STRUCT_DISPLAY_FIELD_BOTTOM_TOP;
                }
                return NV_ENC_PIC_STRUCT_DISPLAY_FRAME;
        }

        // Map NVENC's output NV_ENC_PIC_STRUCT (as reported on
        // NV_ENC_LOCK_BITSTREAM::pictureStruct) back to a promeki VideoScanMode
        // so downstream consumers can read the encoded picture's scan type off
        // the emitted CompressedVideoPayload's metadata.  Note this is a
        // separate enum from NV_ENC_DISPLAY_PIC_STRUCT used at submit time.
        VideoScanMode fromNvencLockedPicStruct(NV_ENC_PIC_STRUCT ps) {
                switch (ps) {
                        case NV_ENC_PIC_STRUCT_FRAME: return VideoScanMode::Progressive;
                        case NV_ENC_PIC_STRUCT_FIELD_TOP_BOTTOM: return VideoScanMode::InterlacedEvenFirst;
                        case NV_ENC_PIC_STRUCT_FIELD_BOTTOM_TOP: return VideoScanMode::InterlacedOddFirst;
                }
                return VideoScanMode::Unknown;
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
                ts.countingType = 0;
                ts.discontinuityFlag = 0;
                ts.cntDroppedFrames = tc.isDropFrame() ? 1 : 0;
                ts.nFrames = static_cast<uint32_t>(tc.frame()) & 0xFFu;
                ts.secondsValue = static_cast<uint32_t>(tc.sec()) & 0x3Fu;
                ts.minutesValue = static_cast<uint32_t>(tc.min()) & 0x3Fu;
                ts.hoursValue = static_cast<uint32_t>(tc.hour()) & 0x1Fu;
                ts.timeOffset = 0;
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
                        _masteringDisplay = cfg.getAs<MasteringDisplay>(MediaConfig::HdrMasteringDisplay);
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
                                if (!cfg.contains(key)) {
                                        return s ? s->defaultValue().get<Enum>() : Enum();
                                }
                                return cfg.getAs<Enum>(key);
                        };
                        _colorPrimaries = static_cast<uint32_t>(readEnum(MediaConfig::VideoColorPrimaries).value());
                        _transferCharacteristics =
                                static_cast<uint32_t>(readEnum(MediaConfig::VideoTransferCharacteristics).value());
                        _matrixCoefficients =
                                static_cast<uint32_t>(readEnum(MediaConfig::VideoMatrixCoefficients).value());
                        _videoRange = static_cast<uint32_t>(readEnum(MediaConfig::VideoRange).value());
                        _cfgScanMode = VideoScanMode(readEnum(MediaConfig::VideoScanMode).value());
                        _enableRCStats = cfg.getAs<bool>(MediaConfig::VideoEncoderStats, false);
                        _seiCaptionsEnabled = cfg.getAs<bool>(MediaConfig::VideoSeiCaptionsEnabled, true);
                        if (_seiCaptionsEnabled && _codec == Codec_AV1) {
                                // One-shot warn: AV1 captions ride as
                                // metadata OBUs (ITUT_T35) which NVENC does
                                // not currently expose.  The H.264 / HEVC
                                // SEI path doesn't apply, so the flag is
                                // effectively ignored; warn here at
                                // configure time so callers notice during
                                // setup rather than wondering why their AV1
                                // YouTube stream lacks captions.
                                promekiWarn("NvencVideoEncoder: VideoSeiCaptionsEnabled requested for AV1 "
                                            "but NVENC does not expose an AV1 caption-OBU path; "
                                            "no caption metadata will be emitted on the bitstream.");
                        }
                        _needReconfigure = _sessionOpen;
                }

                Error submitFrame(const Frame &source, const UncompressedVideoPayload &frame,
                                  const MediaTimeStamp &pts, bool forceKey) {
                        if (!frame.isValid() || frame.planeCount() == 0) {
                                return setError(Error::Invalid, "invalid frame");
                        }
                        const ImageDesc   &idesc = frame.desc();
                        const FormatEntry *fmt = lookupFormat(idesc.pixelFormat().id());
                        if (!fmt) {
                                return setError(Error::PixelFormatNotSupported,
                                                String::sprintf("NvencVideoEncoder: unsupported input format %s",
                                                                idesc.pixelFormat().name().cstr()));
                        }
                        if (Error err = ensureSession(idesc.size().width(), idesc.size().height(), fmt,
                                                      idesc.videoScanMode());
                            err.isError()) {
                                return err;
                        }

                        Slot *slot = acquireFreeSlot();
                        if (!slot) return setError(Error::TryAgain, "no free NVENC slot");

                        if (Error err = uploadFrame(frame, slot); err.isError()) {
                                _freeSlots.pushToBack(slot);
                                return err;
                        }
                        slot->imageMeta = idesc.metadata();
                        slot->sourceFrame = source;

                        NV_ENC_PIC_PARAMS pic{};
                        pic.version = NV_ENC_PIC_PARAMS_VER;
                        pic.inputBuffer = slot->in;
                        pic.outputBitstream = slot->out;
                        pic.bufferFmt = _fmt->nvencFmt;
                        pic.inputWidth = _width;
                        pic.inputHeight = _height;
                        pic.inputPitch = slot->pitch;
                        pic.pictureStruct = NV_ENC_PIC_STRUCT_FRAME;
                        // NVENC's frameIdx is uint32 while our counter is
                        // uint64.  The field is only an ordering hint for
                        // NVENC, so wrapping at 2^32 is benign; be explicit
                        // about the truncation to silence the warning.
                        pic.frameIdx = static_cast<uint32_t>(_frameIdx);
                        pic.inputTimeStamp = _frameIdx;
                        pic.encodePicFlags = forceKey ? NV_ENC_PIC_FLAG_FORCEIDR : 0;

                        slot->hasMd = false;
                        slot->hasCll = false;
                        MasteringDisplay md =
                                idesc.metadata().getAs<MasteringDisplay>(Metadata::MasteringDisplay, _masteringDisplay);
                        ContentLightLevel cll = idesc.metadata().getAs<ContentLightLevel>(Metadata::ContentLightLevel,
                                                                                          _contentLightLevel);

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
                                (_codec == Codec_HEVC && (_timecodeSEI || _effectiveScanMode.isInterlaced()));
                        if (frameNeedsTiming) {
                                VideoScanMode frameScan = _effectiveScanMode;
                                if (idesc.metadata().contains(Metadata::VideoScanMode)) {
                                        VideoScanMode m(idesc.metadata().getAs<Enum>(Metadata::VideoScanMode).value());
                                        if (m.value() != VideoScanMode::Unknown.value()) {
                                                frameScan = m;
                                        }
                                }
                                Timecode tc;
                                if (_timecodeSEI) {
                                        tc = idesc.metadata().getAs<Timecode>(Metadata::Timecode);
                                }
                                NV_ENC_TIME_CODE nvTc{};
                                if (tc.isValid()) {
                                        nvTc = toNvencTimeCode(tc, frameScan);
                                } else {
                                        // Either the session doesn't want
                                        // timecode at all, or this frame has
                                        // no Timecode set — skip the clock
                                        // timestamp but still pass the
                                        // displayPicStruct so pic_struct
                                        // lands in the SEI.
                                        nvTc.displayPicStruct = toNvencDisplayPicStruct(frameScan);
                                        nvTc.skipClockTimestampInsertion = 1;
                                }
                                if (_codec == Codec_H264) {
                                        pic.codecPicParams.h264PicParams.timeCode = nvTc;
                                } else {
                                        pic.codecPicParams.hevcPicParams.timeCode = nvTc;
                                }
                        }

                        if (_codec == Codec_HEVC) {
                                if (md.isValid()) {
                                        slot->nvMd = toNvencMastering(md);
                                        slot->hasMd = true;
                                        pic.codecPicParams.hevcPicParams.pMasteringDisplay = &slot->nvMd;
                                }
                                if (cll.isValid()) {
                                        slot->nvCll = toNvencCll(cll);
                                        slot->hasCll = true;
                                        pic.codecPicParams.hevcPicParams.pMaxCll = &slot->nvCll;
                                }
                        } else if (_codec == Codec_AV1) {
                                if (md.isValid()) {
                                        slot->nvMd = toNvencMastering(md);
                                        slot->hasMd = true;
                                        pic.codecPicParams.av1PicParams.pMasteringDisplay = &slot->nvMd;
                                }
                                if (cll.isValid()) {
                                        slot->nvCll = toNvencCll(cll);
                                        slot->hasCll = true;
                                        pic.codecPicParams.av1PicParams.pMaxCll = &slot->nvCll;
                                }
                        }

                        // Per-frame closed-caption SEI injection.  Walks the
                        // source Frame's ANC payloads, translates each
                        // CEA-708 packet onto the H.264 / HEVC HlsSei wire
                        // transport (ATSC A/53
                        // user_data_registered_itu_t_t35), and stashes the
                        // resulting payload bytes on the slot for NVENC to
                        // wrap with the SEI message header + emulation
                        // prevention.  Display-order pairing is automatic:
                        // NVENC attaches @c seiPayloadArray to the encoded
                        // picture for *this* input frame regardless of
                        // B-frame reordering.
                        //
                        // The @c selectAncForSei stream-index argument is 0
                        // — the NVENC backend is single-stream so any ANC
                        // payload paired to video stream 0 (TPG's default)
                        // or unbound (-1) is in scope.  When the source
                        // Frame has no matching ANC the call returns an
                        // empty list and the feature is silent.
                        slot->captionSeiPayloads.clear();
                        slot->captionSeiArray.clear();
                        if (_seiCaptionsEnabled && _codec != Codec_AV1) {
                                static const AncFormat::IDList kCaptionFormats{AncFormat::Cea708};
                                AncPacket::List ancPackets =
                                        VideoEncoder::selectAncForSei(source, /*pairedVideoStreamIndex=*/0,
                                                                       kCaptionFormats);
                                for (const AncPacket &pkt : ancPackets) {
                                        AncTranslator::PacketsResult r =
                                                _ancTranslator.translate(pkt, AncTransport::HlsSei);
                                        if (error(r).isError()) {
                                                promekiWarn("NvencVideoEncoder: AncTranslator::translate("
                                                            "Cea708, %s → HlsSei) failed: %s",
                                                            pkt.transport().toString().cstr(),
                                                            error(r).name().cstr());
                                                continue;
                                        }
                                        for (const AncPacket &out : value(r)) {
                                                const Buffer &b = out.data();
                                                if (b.size() == 0) continue;
                                                List<uint8_t> bytes(b.size());
                                                std::memcpy(bytes.data(), b.data(), b.size());
                                                slot->captionSeiPayloads.pushToBack(std::move(bytes));
                                        }
                                }
                                if (!slot->captionSeiPayloads.isEmpty()) {
                                        slot->captionSeiArray.reserve(slot->captionSeiPayloads.size());
                                        for (auto &p : slot->captionSeiPayloads) {
                                                NV_ENC_SEI_PAYLOAD entry{};
                                                // SEI payloadType 4 =
                                                // user_data_registered_itu_t_t35
                                                // (ITU-T H.264 / H.265 Annex D).
                                                // NVENC writes the SEI message
                                                // header + emulation-prevention
                                                // bytes; we supply the
                                                // application-layer payload
                                                // bytes only.
                                                entry.payloadType = 4;
                                                entry.payloadSize = static_cast<uint32_t>(p.size());
                                                entry.payload = p.data();
                                                slot->captionSeiArray.pushToBack(entry);
                                        }
                                        if (_codec == Codec_H264) {
                                                pic.codecPicParams.h264PicParams.seiPayloadArray =
                                                        slot->captionSeiArray.data();
                                                pic.codecPicParams.h264PicParams.seiPayloadArrayCnt =
                                                        static_cast<uint32_t>(slot->captionSeiArray.size());
                                        } else {
                                                pic.codecPicParams.hevcPicParams.seiPayloadArray =
                                                        slot->captionSeiArray.data();
                                                pic.codecPicParams.hevcPicParams.seiPayloadArrayCnt =
                                                        static_cast<uint32_t>(slot->captionSeiArray.size());
                                        }
                                }
                        }

                        NVENCSTATUS st = gNvenc.nvEncEncodePicture(_encoder, &pic);
                        if (st != NV_ENC_SUCCESS && st != NV_ENC_ERR_NEED_MORE_INPUT) {
                                _freeSlots.pushToBack(slot);
                                return setError(Error::LibraryFailure,
                                                String::sprintf("nvEncEncodePicture failed (%d)", (int)st));
                        }

                        slot->pts = pts;
                        slot->hasOutput = (st == NV_ENC_SUCCESS);
                        _inFlight.pushToBack(slot);
                        ++_frameIdx;
                        return Error::Ok;
                }

                Frame receiveFrame() {
                        if (!_pendingPackets.isEmpty()) {
                                PendingPacketEntry entry = _pendingPackets.popFromFront();
                                return VideoEncoder::buildOutputFrame(entry.first(), std::move(entry.second()));
                        }

                        if (!_inFlight.isEmpty() && _inFlight.front()->hasOutput) {
                                Slot *slot = _inFlight.popFromFront();
                                Frame source = std::move(slot->sourceFrame);
                                slot->sourceFrame = Frame();
                                auto pkt = lockAndBuildPacket(slot);
                                _freeSlots.pushToBack(slot);
                                if (!pkt.isValid()) return Frame();
                                return VideoEncoder::buildOutputFrame(source, std::move(pkt));
                        }

                        if (_eosPending && _inFlight.isEmpty()) {
                                _eosPending = false;
                                ImageDesc cdesc(Size2Du32(0, 0), outputPixelFormat());
                                auto      pkt = CompressedVideoPayload::Ptr::create(cdesc);
                                pkt.modify()->markEndOfStream();
                                Frame out;
                                out.addPayload(pkt);
                                return out;
                        }

                        return Frame();
                }

                Error flush() {
                        if (!_sessionOpen) {
                                // Nothing to drain — still report EOS so
                                // the caller's drain loop terminates.
                                _eosPending = true;
                                return Error::Ok;
                        }

                        // Submit an EOS pseudo-frame; NVENC will emit any
                        // buffered output on the subsequent lockBitstream
                        // calls against slots still in _inFlight.
                        NV_ENC_PIC_PARAMS pic{};
                        pic.version = NV_ENC_PIC_PARAMS_VER;
                        pic.encodePicFlags = NV_ENC_PIC_FLAG_EOS;
                        pic.pictureStruct = NV_ENC_PIC_STRUCT_FRAME;
                        NVENCSTATUS st = gNvenc.nvEncEncodePicture(_encoder, &pic);
                        if (st != NV_ENC_SUCCESS) {
                                return setError(Error::LibraryFailure,
                                                String::sprintf("nvEncEncodePicture(EOS) failed (%d)", (int)st));
                        }

                        // Any previously-NEED_MORE_INPUT slots are now
                        // guaranteed to have bitstream data — mark them.
                        for (Slot *s : _inFlight) s->hasOutput = true;
                        _eosPending = true;
                        return Error::Ok;
                }

                Error reset() {
                        destroySession();
                        return Error::Ok;
                }

                PixelFormat outputPixelFormat() const {
                        if (_codec == Codec_H264) return PixelFormat(PixelFormat::H264);
                        if (_codec == Codec_AV1) return PixelFormat(PixelFormat::AV1);
                        return PixelFormat(PixelFormat::HEVC);
                }

                Error         lastError() const { return _lastError; }
                const String &lastErrorMessage() const { return _lastErrorMessage; }
                void          clearError() {
                        _lastError = Error::Ok;
                        _lastErrorMessage = String();
                }

        private:
                struct Slot {
                                NV_ENC_INPUT_PTR  in = nullptr;
                                NV_ENC_OUTPUT_PTR out = nullptr;
                                MediaTimeStamp    pts;
                                Metadata          imageMeta;
                                // Source Frame the slot was submitted for —
                                // carries the audio / ANC / metadata that
                                // the emitted output Frame echoes through.
                                // Default-constructed when the slot is free.
                                Frame             sourceFrame;
                                uint32_t          pitch = 0;
                                bool              hasOutput = false;
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
                                bool                   hasMd = false;
                                bool                   hasCll = false;

                                // Per-frame closed-caption SEI payloads.  Each
                                // entry holds the @c user_data_registered_itu_t_t35
                                // body bytes produced by
                                // @c AncTranslator::translate(pkt, AncTransport::HlsSei)
                                // — i.e. the bytes that go inside the SEI
                                // payload (NVENC adds the SEI NAL framing,
                                // payloadType, and emulation prevention).
                                // Storage lives on the Slot so it outlives
                                // the @c nvEncEncodePicture call (NVENC may
                                // defer reading these across NEED_MORE_INPUT
                                // returns when B-frames or lookahead are
                                // active).  The parallel
                                // @c captionSeiArray of NV_ENC_SEI_PAYLOAD
                                // descriptors is rebuilt on every submit so
                                // its @c payload pointers always reference
                                // the current @c captionSeiPayloads vector.
                                List<List<uint8_t>> captionSeiPayloads;
                                List<NV_ENC_SEI_PAYLOAD>   captionSeiArray;
                };

                struct Caps {
                                bool support10Bit = false;
                                bool support422 = false;
                                bool support444 = false;
                                bool supportLossless = false;
                                bool supportLookahead = false;
                                bool supportTemporalAQ = false;
                                bool supportAlpha = false;
                                int  maxBFrames = 0;
                };

                Codec       _codec;
                MediaConfig _cfg;
                bool        _needReconfigure = false;
                bool        _timecodeSEI = false;
                // Caption-SEI feature gate, read from
                // @ref MediaConfig::VideoSeiCaptionsEnabled in
                // @c configure.  When true and the codec is H.264 or
                // HEVC, @c submitFrame walks the source Frame's ANC
                // payloads via @ref VideoEncoder::selectAncForSei,
                // translates each CEA-708 packet through
                // @c _ancTranslator to the @c HlsSei wire transport,
                // and stashes the resulting payload bytes on the slot
                // for injection alongside any HDR / timecode SEI on
                // the matching @c nvEncEncodePicture call.
                //
                // AV1 has no NVENC SEI path (captions ride as metadata
                // OBUs which the SDK does not expose) — the flag is
                // honoured only by H.264 and HEVC.
                bool _seiCaptionsEnabled = false;
                // Free-standing translator session used to convert
                // source-Frame ANC packets onto the H.264 / HEVC
                // @c HlsSei carrier.  Default-constructed config is
                // fine here — the @c Cea708 → @c HlsSei builder is
                // pure (cc_data triples → ATSC A/53 wrapper) and
                // doesn't read any tunable config keys.
                AncTranslator _ancTranslator;
                // When true, lockBitstream is called with getRCStats=1
                // so NVENC aggregates intra/inter block counts and
                // average motion vectors for the emitted frame.  This
                // is slightly more expensive on the encoder; keep it
                // off unless downstream actually consumes those stats.
                bool _enableRCStats = false;
                // Display-order frame index of the most recent emitted
                // keyframe.  GOP position for each non-keyframe is
                // computed as NV_ENC_LOCK_BITSTREAM::frameIdxDisplay
                // minus this value so it reflects display-order offset
                // even when B-frames reorder the encode stream.  Reset
                // to 0 at session teardown so the first keyframe after
                // restart anchors a fresh GOP.
                uint32_t _lastKeyDisplayIdx = 0;

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
                VideoScanMode _cfgScanMode{VideoScanMode::Unknown};
                VideoScanMode _effectiveScanMode{VideoScanMode::Progressive};

                // VUI color description, captured from MediaConfig at
                // configure() time.  Values are raw H.273 numeric
                // codepoints so ensureSession() doesn't have to
                // re-resolve Enum lookups.  Special values:
                //
                //   255 — "Auto" / "Unknown" — resolve from the first
                //         frame's PixelFormat::colorModel() /
                //         PixelFormat::videoRange() when ensureSession()
                //         runs.
                //   0   — unset; treat as Unspecified on the wire.
                //   1..22 — spec-registered value written verbatim.
                uint32_t _colorPrimaries = 255;
                uint32_t _transferCharacteristics = 255;
                uint32_t _matrixCoefficients = 255;
                // VideoRange mirrors the Unknown/Limited/Full tri-state
                // from the VideoRange TypedEnum.  Resolved against the
                // first frame at session init when Unknown.
                uint32_t _videoRange = 0; // VideoRange::Unknown

                const FormatEntry *_fmt = nullptr;
                Caps               _caps;
                MasteringDisplay   _masteringDisplay;
                ContentLightLevel  _contentLightLevel;

                CUdevice  _device = 0;
                CUcontext _cudaCtx = nullptr;
                bool      _ctxRetained = false;

                void    *_encoder = nullptr;
                bool     _sessionOpen = false;
                uint32_t _width = 0;
                uint32_t _height = 0;
                uint64_t _frameIdx = 0;
                size_t   _numSlots = 4;

                // FrameRate resolved at session init and used to
                // stamp each emitted packet's duration.  Captured
                // here (not re-read from _cfg in lockAndBuildPacket)
                // so later reconfigure() calls can replace it
                // atomically alongside the other session-scoped
                // state.
                FrameRate _sessionFrameRate;

                // Annex-B blob carrying the codec parameter sets for
                // the active session — SPS+PPS for H.264, VPS+SPS+PPS
                // for HEVC.  Pulled once via @c nvEncGetSequenceParams
                // immediately after @c nvEncInitializeEncoder succeeds
                // (NVENC has the bitstream headers fully resolved at
                // that point) and stamped onto every emitted
                // @ref CompressedVideoPayload via
                // @ref Metadata::CodecParameterSets.  Each downstream
                // MediaIO that needs out-of-band parameter sets — RTP
                // for SDP @c sprop-*, MP4 / MOV for @c avcC / @c hvcC,
                // future SRT and ST 2110 sinks — reads it directly off
                // the frame instead of waiting for the first IDR to
                // flow through self-healing.  Cleared by
                // @ref destroySession.  Reconfigure recaptures it
                // because the parameter sets can change with codec
                // config (profile / level / size).
                String _paramSetsBlob;

                using SlotList   = List<Slot>;
                using SlotDeque  = Deque<Slot *>;

                SlotList  _slots;
                SlotDeque _freeSlots;
                SlotDeque _inFlight;

                bool _eosPending = false;

                Error  _lastError;
                String _lastErrorMessage;

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
                // folded against the first frame's PixelFormat.
                struct ResolvedColorDesc {
                                uint32_t primaries = 0;
                                uint32_t transfer = 0;
                                uint32_t matrix = 0;
                                uint32_t range = 0; // VideoRange numeric.
                };

                // Resolve the cached Auto/Unspecified/Unknown sentinels
                // against _fmt->pixelFormatId, giving each VUI field its
                // concrete value for this session.  Caller-supplied
                // explicit values (BT709, SMPTE2084, Limited, ...) pass
                // through untouched so a downstream HDR10 override is
                // always honoured.  Must be called after _fmt is set.
                ResolvedColorDesc resolveColorDescription() const {
                        ResolvedColorDesc      out;
                        const PixelFormat      pd(_fmt->pixelFormatId);
                        const ColorModel::H273 h = ColorModel::toH273(pd.colorModel().id());

                        out.primaries = (_colorPrimaries == 255u) ? h.primaries : _colorPrimaries;
                        out.transfer = (_transferCharacteristics == 255u) ? h.transfer : _transferCharacteristics;
                        out.matrix = (_matrixCoefficients == 255u) ? h.matrix : _matrixCoefficients;

                        if (_videoRange == 0u /*Unknown*/) {
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
                        int         val = 0;
                        NVENCSTATUS st = gNvenc.nvEncGetEncodeCaps(_encoder, codecGuid(), &param, &val);
                        if (st != NV_ENC_SUCCESS) {
                                promekiWarn("NvencVideoEncoder: nvEncGetEncodeCaps(cap=%d) failed (%d); "
                                            "treating as unsupported.",
                                            (int)cap, (int)st);
                                return 0;
                        }
                        return val;
                }

                void populateCaps() {
                        _caps.support10Bit = queryCap(NV_ENC_CAPS_SUPPORT_10BIT_ENCODE) != 0;
                        _caps.support422 = queryCap(NV_ENC_CAPS_SUPPORT_YUV422_ENCODE) != 0;
                        _caps.support444 = queryCap(NV_ENC_CAPS_SUPPORT_YUV444_ENCODE) != 0;
                        _caps.supportLossless = queryCap(NV_ENC_CAPS_SUPPORT_LOSSLESS_ENCODE) != 0;
                        _caps.supportLookahead = queryCap(NV_ENC_CAPS_SUPPORT_LOOKAHEAD) != 0;
                        _caps.supportTemporalAQ = queryCap(NV_ENC_CAPS_SUPPORT_TEMPORAL_AQ) != 0;
                        _caps.supportAlpha = queryCap(NV_ENC_CAPS_SUPPORT_ALPHA_LAYER_ENCODING) != 0;
                        _caps.maxBFrames = queryCap(NV_ENC_CAPS_NUM_MAX_BFRAMES);
                }

                Error validateFormatCaps() const {
                        if (_fmt->inputBitDepth == NV_ENC_BIT_DEPTH_10 && !_caps.support10Bit) {
                                return Error::PixelFormatNotSupported;
                        }
                        if (_fmt->chromaFormatIDC == 2 && !_caps.support422) {
                                return Error::PixelFormatNotSupported;
                        }
                        if (_fmt->chromaFormatIDC == 3 && !_caps.support444) {
                                return Error::PixelFormatNotSupported;
                        }
                        return Error::Ok;
                }

                GUID codecGuid() const {
                        if (_codec == Codec_H264) return NV_ENC_CODEC_H264_GUID;
                        if (_codec == Codec_AV1) return NV_ENC_CODEC_AV1_GUID;
                        return NV_ENC_CODEC_HEVC_GUID;
                }

                Error ensureSession(uint32_t w, uint32_t h, const FormatEntry *fmt, VideoScanMode firstFrameScanMode) {
                        if (_sessionOpen) {
                                if (w != _width || h != _height) {
                                        return setError(
                                                Error::Invalid,
                                                "NvencVideoEncoder does not support mid-stream resolution changes");
                                }
                                if (fmt != _fmt) {
                                        return setError(Error::Invalid,
                                                        "NvencVideoEncoder does not support mid-stream format changes");
                                }
                                if (_needReconfigure) {
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
                        if (_cfgScanMode.value() != VideoScanMode::Unknown.value()) {
                                _effectiveScanMode = _cfgScanMode;
                        } else if (firstFrameScanMode.value() != VideoScanMode::Unknown.value()) {
                                _effectiveScanMode = firstFrameScanMode;
                        } else {
                                _effectiveScanMode = VideoScanMode::Progressive;
                        }

                        if (!loadNvenc()) {
                                return setError(Error::LibraryFailure, "failed to load libnvidia-encode.so.1 (install "
                                                                       "libnvidia-encode-NNN matching your driver)");
                        }

                        if (Error err = retainCudaContext(); err.isError()) return err;

                        NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS sp{};
                        sp.version = NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER;
                        sp.deviceType = NV_ENC_DEVICE_TYPE_CUDA;
                        sp.device = _cudaCtx;
                        sp.apiVersion = NVENCAPI_VERSION;
                        NVENCSTATUS st = gNvenc.nvEncOpenEncodeSessionEx(&sp, &_encoder);
                        if (st != NV_ENC_SUCCESS || _encoder == nullptr) {
                                return setError(Error::LibraryFailure,
                                                String::sprintf("nvEncOpenEncodeSessionEx failed (%d)", (int)st));
                        }

                        _fmt = fmt;
                        _width = w;
                        _height = h;

                        populateCaps();
                        if (Error err = validateFormatCaps(); err.isError()) {
                                Error e =
                                        setError(err, String::sprintf("GPU does not support the requested format "
                                                                      "(chroma=%u, bitDepth=%d)",
                                                                      _fmt->chromaFormatIDC, (int)_fmt->inputBitDepth));
                                destroySession();
                                return e;
                        }
                        const GUID               encGuid = codecGuid();
                        const Enum               presetEnum = _cfg.getAs<Enum>(MediaConfig::VideoPreset);
                        const GUID               presetGuid = toNvencPreset(presetEnum);
                        const NV_ENC_TUNING_INFO tuning = toNvencTuning(presetEnum);

                        NV_ENC_PRESET_CONFIG presetCfg{};
                        presetCfg.version = NV_ENC_PRESET_CONFIG_VER;
                        presetCfg.presetCfg.version = NV_ENC_CONFIG_VER;
                        st = gNvenc.nvEncGetEncodePresetConfigEx(_encoder, encGuid, presetGuid, tuning, &presetCfg);
                        if (st != NV_ENC_SUCCESS) {
                                Error e =
                                        setError(Error::LibraryFailure,
                                                 String::sprintf("nvEncGetEncodePresetConfigEx failed (%d)", (int)st));
                                destroySession();
                                return e;
                        }

                        NV_ENC_CONFIG encCfg = presetCfg.presetCfg;
                        encCfg.version = NV_ENC_CONFIG_VER;

                        const int32_t bitrateKbps = _cfg.getAs<int32_t>(MediaConfig::BitrateKbps);
                        const int32_t maxBitrateKbps = _cfg.getAs<int32_t>(MediaConfig::MaxBitrateKbps);
                        const int32_t gopLength = _cfg.getAs<int32_t>(MediaConfig::GopLength);
                        const int32_t idrInterval = _cfg.getAs<int32_t>(MediaConfig::IdrInterval);
                        const int32_t qp = _cfg.getAs<int32_t>(MediaConfig::VideoQp);
                        const Enum    rcEnum = _cfg.getAs<Enum>(MediaConfig::VideoRcMode);

                        encCfg.rcParams.rateControlMode = toNvencRc(rcEnum);
                        encCfg.rcParams.averageBitRate = static_cast<uint32_t>(bitrateKbps) * 1000u;
                        encCfg.rcParams.maxBitRate = static_cast<uint32_t>(maxBitrateKbps) * 1000u;
                        const bool lossless = (presetEnum == VideoEncoderPreset::Lossless);
                        if (lossless && _caps.supportLossless) {
                                encCfg.rcParams.rateControlMode = NV_ENC_PARAMS_RC_CONSTQP;
                                encCfg.rcParams.constQP = {0, 0, 0};
                        } else if (encCfg.rcParams.rateControlMode == NV_ENC_PARAMS_RC_CONSTQP) {
                                encCfg.rcParams.constQP.qpIntra = qp;
                                encCfg.rcParams.constQP.qpInterP = qp;
                                encCfg.rcParams.constQP.qpInterB = qp;
                        }
                        if (gopLength > 0) encCfg.gopLength = gopLength;

                        const int32_t bFrames = _cfg.getAs<int32_t>(MediaConfig::BFrames);
                        int           effectiveB = std::min(bFrames, _caps.maxBFrames);
                        if (effectiveB < 0) effectiveB = 0;
                        encCfg.frameIntervalP = effectiveB + 1;

                        const int32_t laFrames = _cfg.getAs<int32_t>(MediaConfig::LookaheadFrames);
                        if (laFrames > 0 && _caps.supportLookahead) {
                                encCfg.rcParams.enableLookahead = 1;
                                encCfg.rcParams.lookaheadDepth = static_cast<uint16_t>(laFrames);
                        }

                        _numSlots = std::max(size_t(32), size_t(effectiveB) * 4 + size_t(laFrames) + 32);

                        if (_cfg.getAs<bool>(MediaConfig::VideoSpatialAQ)) {
                                encCfg.rcParams.enableAQ = 1;
                                int aqStr = _cfg.getAs<int32_t>(MediaConfig::VideoSpatialAQStrength);
                                if (aqStr > 0) encCfg.rcParams.aqStrength = aqStr;
                        }
                        if (_cfg.getAs<bool>(MediaConfig::VideoTemporalAQ) && _caps.supportTemporalAQ) {
                                encCfg.rcParams.enableTemporalAQ = 1;
                        }
                        int mp = _cfg.getAs<int32_t>(MediaConfig::VideoMultiPass);
                        if (mp == 1)
                                encCfg.rcParams.multiPass = NV_ENC_TWO_PASS_QUARTER_RESOLUTION;
                        else if (mp == 2)
                                encCfg.rcParams.multiPass = NV_ENC_TWO_PASS_FULL_RESOLUTION;

                        const bool repeatHdrs = _cfg.getAs<bool>(MediaConfig::VideoRepeatHeaders);
                        _timecodeSEI = _cfg.getAs<bool>(MediaConfig::VideoTimecodeSEI);
                        if (_timecodeSEI && _codec == Codec_AV1) {
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
                        if (sessionInterlaced && _codec == Codec_AV1) {
                                promekiWarn("NvencVideoEncoder: interlaced scan mode requested "
                                            "for AV1 but NVENC does not expose an AV1 interlaced "
                                            "signalling path; emitting progressive bitstream.");
                        }

                        const String profileStr = _cfg.getAs<String>(MediaConfig::VideoProfile);
                        const String levelStr = _cfg.getAs<String>(MediaConfig::VideoLevel);

                        const int               effectiveIdr = (idrInterval > 0) ? idrInterval : encCfg.gopLength;
                        const ResolvedColorDesc color = resolveColorDescription();
                        if (_codec == Codec_H264) {
                                encCfg.profileGUID = h264ProfileGuid(profileStr, _fmt);
                                auto &h = encCfg.encodeCodecConfig.h264Config;
                                h.level = h264Level(levelStr);
                                h.idrPeriod = effectiveIdr;
                                h.chromaFormatIDC = _fmt->chromaFormatIDC;
                                h.inputBitDepth = _fmt->inputBitDepth;
                                h.outputBitDepth = _fmt->outputBitDepth;
                                if (repeatHdrs) h.repeatSPSPPS = 1;
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
                                h.enableTimeCode = 1;
                                h.outputPictureTimingSEI = 1;
                                populateVuiColorDescription(h.h264VUIParameters, color.primaries, color.transfer,
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
                                if (lossless && _caps.supportLossless && _fmt->chromaFormatIDC == 3) {
                                        h.qpPrimeYZeroTransformBypassFlag = 1;
                                        encCfg.profileGUID = NV_ENC_H264_PROFILE_HIGH_444_GUID;
                                } else if (lossless) {
                                        promekiWarn("NvencVideoEncoder: H.264 lossless requires "
                                                    "4:4:4 input (got chroma=%u); falling back to "
                                                    "high-quality CQP.",
                                                    _fmt->chromaFormatIDC);
                                }
                        } else if (_codec == Codec_HEVC) {
                                encCfg.profileGUID = hevcProfileGuid(profileStr, _fmt);
                                auto &h = encCfg.encodeCodecConfig.hevcConfig;
                                h.level = hevcLevel(levelStr);
                                h.idrPeriod = effectiveIdr;
                                h.chromaFormatIDC = _fmt->chromaFormatIDC;
                                h.inputBitDepth = _fmt->inputBitDepth;
                                h.outputBitDepth = _fmt->outputBitDepth;
                                if (repeatHdrs) h.repeatSPSPPS = 1;
                                if (_timecodeSEI || sessionInterlaced) {
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
                                if (sessionInterlaced) {
                                        promekiWarn("NvencVideoEncoder: HEVC interlaced scan mode "
                                                    "requested; NVENC's public API does not expose "
                                                    "pic_struct in HEVC pic_timing SEI, so field "
                                                    "order will be carried only in the Time Code SEI "
                                                    "displayPicStruct (non-standard for pic_struct "
                                                    "semantics).  Most HEVC players read pic_struct "
                                                    "from pic_timing only and will treat the output "
                                                    "as progressive.");
                                }
                                populateVuiColorDescription(h.hevcVUIParameters, color.primaries, color.transfer,
                                                            color.matrix, color.range);
                                if (lossless && _caps.supportLossless) {
                                        encCfg.profileGUID = NV_ENC_HEVC_PROFILE_FREXT_GUID;
                                }
                                if (_masteringDisplay.isValid()) h.outputMasteringDisplay = 1;
                                if (_contentLightLevel.isValid()) h.outputMaxCll = 1;
                        } else {
                                encCfg.profileGUID = av1ProfileGuid(profileStr);
                                auto &a = encCfg.encodeCodecConfig.av1Config;
                                a.level = av1Level(levelStr);
                                a.idrPeriod = effectiveIdr;
                                a.chromaFormatIDC = _fmt->chromaFormatIDC;
                                a.inputBitDepth = _fmt->inputBitDepth;
                                a.outputBitDepth = _fmt->outputBitDepth;
                                a.repeatSeqHdr = repeatHdrs ? 1 : a.repeatSeqHdr;
                                if (_masteringDisplay.isValid()) a.outputMasteringDisplay = 1;
                                if (_contentLightLevel.isValid()) a.outputMaxCll = 1;
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
                                a.matrixCoefficients =
                                        static_cast<NV_ENC_VUI_MATRIX_COEFFS>((color.matrix != 0) ? color.matrix : 2);
                                a.colorRange = (color.range == 2u /*Full*/) ? 1u : 0u;
                        }

                        // Honour the caller-supplied FrameRate — NVENC
                        // uses this for rate-control math (CBR / VBR
                        // bits-per-frame target, HRD buffer math) and
                        // for SPS/VUI timing info in H.264 / HEVC.  The
                        // library hook is MediaConfig::FrameRate; the
                        // VideoEncoderMediaIO stamps it from the
                        // pending MediaDesc before calling configure().
                        const FrameRate fallback(FrameRate::RationalType(30, 1));
                        FrameRate       fr = _cfg.getAs<FrameRate>(MediaConfig::FrameRate, fallback);
                        if (!fr.isValid()) fr = fallback;

                        NV_ENC_INITIALIZE_PARAMS init{};
                        init.version = NV_ENC_INITIALIZE_PARAMS_VER;
                        init.encodeGUID = encGuid;
                        init.presetGUID = presetGuid;
                        init.tuningInfo = tuning;
                        init.encodeWidth = _width;
                        init.encodeHeight = _height;
                        init.darWidth = _width;
                        init.darHeight = _height;
                        init.frameRateNum = fr.numerator();
                        init.frameRateDen = fr.denominator();
                        _sessionFrameRate = fr;
                        init.enablePTD = 1;
                        init.encodeConfig = &encCfg;

                        st = gNvenc.nvEncInitializeEncoder(_encoder, &init);
                        if (st != NV_ENC_SUCCESS) {
                                Error e = setError(Error::LibraryFailure,
                                                   String::sprintf("nvEncInitializeEncoder failed (%d)", (int)st));
                                destroySession();
                                return e;
                        }

                        if (Error err = allocateSlots(); err.isError()) {
                                destroySession();
                                return err;
                        }

                        captureSequenceParams();

                        _sessionOpen = true;
                        clearError();
                        return Error::Ok;
                }

                // Pull the Annex-B SPS/PPS (H.264) or VPS/SPS/PPS
                // (HEVC) out of the now-initialized encoder via
                // @c nvEncGetSequenceParams and stash them in
                // @ref _paramSetsBlob so every emitted packet can
                // republish them through @ref Metadata::CodecParameterSets.
                //
                // Best-effort: a failed lookup just leaves the blob
                // empty.  In that case downstream MediaIOs fall back
                // to the existing self-healing path (cache parameter
                // sets as they pass in-band on the first IDR).  No
                // session-level error is raised because the encoder
                // itself is fully usable — only the out-of-band
                // signaling channel is missing.
                void captureSequenceParams() {
                        _paramSetsBlob = String();
                        if (_encoder == nullptr) return;

                        constexpr size_t kSeqParamBufBytes = 1024;
                        Buffer           buf(kSeqParamBufBytes);
                        if (!buf) return;

                        NV_ENC_SEQUENCE_PARAM_PAYLOAD spp{};
                        spp.version = NV_ENC_SEQUENCE_PARAM_PAYLOAD_VER;
                        spp.inBufferSize = static_cast<uint32_t>(buf.size());
                        spp.spsppsBuffer = buf.data();
                        uint32_t outSize = 0;
                        spp.outSPSPPSPayloadSize = &outSize;

                        NVENCSTATUS st = gNvenc.nvEncGetSequenceParams(_encoder, &spp);
                        if (st != NV_ENC_SUCCESS) {
                                promekiWarn("NvencVideoEncoder: nvEncGetSequenceParams failed (%d); "
                                            "Metadata::CodecParameterSets will be empty.",
                                            (int)st);
                                return;
                        }
                        if (outSize == 0 || outSize > buf.size()) {
                                promekiWarn("NvencVideoEncoder: nvEncGetSequenceParams returned an "
                                            "out-of-range payload size (%u, buffer = %zu); skipping.",
                                            outSize, buf.size());
                                return;
                        }
                        _paramSetsBlob = String(static_cast<const char *>(buf.data()), outSize);
                }

                Error retainCudaContext() {
                        if (_ctxRetained) return Error::Ok;

                        if (Error err = CudaBootstrap::ensureRegistered(); err.isError()) {
                                return setError(err, "CudaBootstrap::ensureRegistered failed");
                        }
                        // The library doesn't pick a device for the user
                        // — honour whatever the current thread already
                        // selected via CudaDevice::setCurrent (defaulting
                        // to device 0 when nothing was selected yet).
                        if (Error err = CudaDevice::setCurrent(0); err.isError()) {
                                return setError(err, "CudaDevice::setCurrent failed");
                        }

                        CUresult cr = cuInit(0);
                        if (cr != CUDA_SUCCESS) {
                                return setError(Error::LibraryFailure, String::sprintf("cuInit failed (%d)", (int)cr));
                        }
                        cr = cuDeviceGet(&_device, 0);
                        if (cr != CUDA_SUCCESS) {
                                return setError(Error::LibraryFailure,
                                                String::sprintf("cuDeviceGet failed (%d)", (int)cr));
                        }
                        cr = cuDevicePrimaryCtxRetain(&_cudaCtx, _device);
                        if (cr != CUDA_SUCCESS || _cudaCtx == nullptr) {
                                return setError(Error::LibraryFailure,
                                                String::sprintf("cuDevicePrimaryCtxRetain failed (%d)", (int)cr));
                        }
                        _ctxRetained = true;
                        return Error::Ok;
                }

                Error allocateSlots() {
                        _slots.resize(_numSlots);
                        for (size_t i = 0; i < _numSlots; ++i) {
                                Slot &s = _slots[i];

                                NV_ENC_CREATE_INPUT_BUFFER cin{};
                                cin.version = NV_ENC_CREATE_INPUT_BUFFER_VER;
                                cin.width = _width;
                                cin.height = _height;
                                cin.bufferFmt = _fmt->nvencFmt;
                                NVENCSTATUS st = gNvenc.nvEncCreateInputBuffer(_encoder, &cin);
                                if (st != NV_ENC_SUCCESS) {
                                        return setError(Error::LibraryFailure,
                                                        String::sprintf("nvEncCreateInputBuffer failed (%d)", (int)st));
                                }
                                s.in = cin.inputBuffer;

                                NV_ENC_CREATE_BITSTREAM_BUFFER cb{};
                                cb.version = NV_ENC_CREATE_BITSTREAM_BUFFER_VER;
                                st = gNvenc.nvEncCreateBitstreamBuffer(_encoder, &cb);
                                if (st != NV_ENC_SUCCESS) {
                                        return setError(
                                                Error::LibraryFailure,
                                                String::sprintf("nvEncCreateBitstreamBuffer failed (%d)", (int)st));
                                }
                                s.out = cb.bitstreamBuffer;

                                _freeSlots.pushToBack(&s);
                        }
                        return Error::Ok;
                }

                Slot *acquireFreeSlot() {
                        if (_freeSlots.isEmpty()) {
                                // Drain completed slots from the head of
                                // the in-flight queue.  With B-frames,
                                // only slots with hasOutput can be safely
                                // recycled — others are still held by
                                // the encoder as reference frames.
                                while (!_inFlight.isEmpty() && _inFlight.front()->hasOutput) {
                                        Slot *head = _inFlight.popFromFront();
                                        Frame source = std::move(head->sourceFrame);
                                        head->sourceFrame = Frame();
                                        if (auto pkt = lockAndBuildPacket(head)) {
                                                _pendingPackets.pushToBack(
                                                        PendingPacketEntry(std::move(source), std::move(pkt)));
                                        }
                                        _freeSlots.pushToBack(head);
                                }
                                if (_freeSlots.isEmpty()) return nullptr;
                        }
                        Slot *s = _freeSlots.popFromFront();
                        s->hasOutput = false;
                        return s;
                }

                Error uploadFrame(const UncompressedVideoPayload &frame, Slot *slot) {
                        NV_ENC_LOCK_INPUT_BUFFER lk{};
                        lk.version = NV_ENC_LOCK_INPUT_BUFFER_VER;
                        lk.inputBuffer = slot->in;
                        NVENCSTATUS st = gNvenc.nvEncLockInputBuffer(_encoder, &lk);
                        if (st != NV_ENC_SUCCESS) {
                                return setError(Error::LibraryFailure,
                                                String::sprintf("nvEncLockInputBuffer failed (%d)", (int)st));
                        }

                        auto      *dst = static_cast<uint8_t *>(lk.bufferDataPtr);
                        const auto pitch = lk.pitch;
                        slot->pitch = pitch;

                        const uint32_t        rowBytes = _fmt->bytesPerPixelY * _width;
                        const PixelMemLayout &ml = frame.desc().pixelFormat().memLayout();
                        const size_t          imgWidth = frame.desc().size().width();

                        if (_fmt->planeCount == 3) {
                                // Accumulate the destination offset from the
                                // actual per-plane row counts so adding a
                                // planar 4:2:0 / 4:2:2 entry to kFormatTable
                                // later can't silently overrun the NVENC
                                // surface — a naive `p * _height * pitch`
                                // only works when every plane is full-height.
                                size_t planeOffset = 0;
                                for (uint32_t p = 0; p < 3; ++p) {
                                        const uint32_t planeRows = (p == 0) ? _height : _height / _fmt->uvHeightDivisor;
                                        const uint8_t *src = frame.plane(p).data();
                                        const size_t   srcStride = ml.lineStride(p, imgWidth);
                                        uint8_t       *planeDst = dst + planeOffset;
                                        for (uint32_t row = 0; row < planeRows; ++row) {
                                                std::memcpy(planeDst + row * pitch, src + row * srcStride, rowBytes);
                                        }
                                        planeOffset += size_t(planeRows) * pitch;
                                }
                        } else {
                                const uint8_t *yPlane = frame.plane(0).data();
                                const size_t   srcYStride = ml.lineStride(0, imgWidth);
                                for (uint32_t row = 0; row < _height; ++row) {
                                        std::memcpy(dst + row * pitch, yPlane + row * srcYStride, rowBytes);
                                }

                                const uint32_t uvRows = _height / _fmt->uvHeightDivisor;
                                const uint8_t *uvPlane =
                                        frame.planeCount() > 1 ? frame.plane(1).data() : yPlane + srcYStride * _height;
                                const size_t srcUVStride =
                                        frame.planeCount() > 1 ? ml.lineStride(1, imgWidth) : rowBytes;

                                uint8_t *uvDst = dst + _height * pitch;
                                for (uint32_t row = 0; row < uvRows; ++row) {
                                        std::memcpy(uvDst + row * pitch, uvPlane + row * srcUVStride, rowBytes);
                                }
                        }

                        st = gNvenc.nvEncUnlockInputBuffer(_encoder, slot->in);
                        if (st != NV_ENC_SUCCESS) {
                                return setError(Error::LibraryFailure,
                                                String::sprintf("nvEncUnlockInputBuffer failed (%d)", (int)st));
                        }
                        return Error::Ok;
                }

                CompressedVideoPayload::Ptr lockAndBuildPacket(Slot *slot) {
                        NV_ENC_LOCK_BITSTREAM lb{};
                        lb.version = NV_ENC_LOCK_BITSTREAM_VER;
                        lb.outputBitstream = slot->out;
                        // getRCStats enables NVENC's per-block intra /
                        // inter counting and motion-vector averaging.
                        // Cheap stats (avg QP, SATD, indices, temporal
                        // id) come back unconditionally, so only pay
                        // the extra work when the caller asked for it
                        // via MediaConfig::VideoEncoderStats.
                        lb.getRCStats = _enableRCStats ? 1 : 0;
                        NVENCSTATUS st = gNvenc.nvEncLockBitstream(_encoder, &lb);
                        if (st != NV_ENC_SUCCESS) {
                                setError(Error::LibraryFailure,
                                         String::sprintf("nvEncLockBitstream failed (%d)", (int)st));
                                return CompressedVideoPayload::Ptr();
                        }

                        auto buf = Buffer(lb.bitstreamSizeInBytes);
                        std::memcpy(buf.data(), lb.bitstreamBufferPtr, lb.bitstreamSizeInBytes);
                        buf.setSize(lb.bitstreamSizeInBytes);
                        const FrameType ft = toFrameType(lb.pictureType);
                        // Random-access keyframes are only true IDR or
                        // I pictures — INTRA_REFRESH is intra-coded but
                        // not a valid random-access point, so it rides
                        // on the IntraRefresh flag instead.  NONREF_P
                        // is a regular P that later frames do not
                        // reference, which maps to the generic
                        // Discardable flag for droppable-frame transport.
                        const bool isKey =
                                (lb.pictureType == NV_ENC_PIC_TYPE_IDR || lb.pictureType == NV_ENC_PIC_TYPE_I);
                        const bool isIntraRefresh = (lb.pictureType == NV_ENC_PIC_TYPE_INTRA_REFRESH);
                        const bool isNonRef = (lb.pictureType == NV_ENC_PIC_TYPE_NONREF_P);

                        // Snapshot the fields we care about before
                        // unlocking — the lb struct is safe to read
                        // after unlock, but keep the dependency on
                        // NVENC's memory explicit by pulling them out
                        // first.
                        const uint32_t          frameIdxEncode = lb.frameIdx;
                        const uint32_t          frameIdxDisplay = lb.frameIdxDisplay;
                        const uint32_t          avgQP = lb.frameAvgQP;
                        const uint32_t          satd = lb.frameSatd;
                        const uint32_t          temporalId = lb.temporalId;
                        const NV_ENC_PIC_STRUCT picStruct = lb.pictureStruct;
                        const uint32_t          intraBlocks = lb.intraMBCount;
                        const uint32_t          interBlocks = lb.interMBCount;
                        const int32_t           avgMVX = lb.averageMVX;
                        const int32_t           avgMVY = lb.averageMVY;

                        gNvenc.nvEncUnlockBitstream(_encoder, slot->out);

                        // GOP position in display order: on a
                        // keyframe, anchor this frame as the start of
                        // a new GOP (position 0); on non-keyframes,
                        // take the signed offset from the anchor so
                        // reordered B-frames report their true
                        // display-order distance, not their encode
                        // position.  int32_t comparison handles
                        // frameIdxDisplay wrap safely for session
                        // lifetimes under ~2^31 frames.
                        uint32_t gopPosition = 0;
                        if (isKey) {
                                _lastKeyDisplayIdx = frameIdxDisplay;
                        } else {
                                gopPosition = static_cast<uint32_t>(static_cast<int32_t>(frameIdxDisplay) -
                                                                    static_cast<int32_t>(_lastKeyDisplayIdx));
                        }

                        BufferView view(buf, 0, lb.bitstreamSizeInBytes);
                        ImageDesc  cdesc(Size2Du32(_width, _height), outputPixelFormat());
                        auto       pkt = CompressedVideoPayload::Ptr::create(cdesc, view);
                        pkt.modify()->setPts(slot->pts);
                        pkt.modify()->setDts(slot->pts);
                        pkt.modify()->setFrameType(ft);
                        pkt.modify()->setFlag(MediaPayload::Keyframe, isKey);
                        pkt.modify()->setFlag(MediaPayload::IntraRefresh, isIntraRefresh);
                        pkt.modify()->setFlag(MediaPayload::Discardable, isNonRef);

                        // Carry per-image metadata across the codec
                        // boundary: things like Timecode and user keys
                        // that don't live in the H.264 / HEVC bitstream
                        // ride along on the payload and get re-applied
                        // by the matching VideoDecoder.
                        Metadata &pmeta = pkt.modify()->metadata();
                        if (!slot->imageMeta.isEmpty()) {
                                pmeta = slot->imageMeta;
                                slot->imageMeta = Metadata();
                        }

                        // Encoder output statistics — stamp the cheap
                        // family unconditionally, and the RC-stats
                        // family only when enabled at configure time.
                        pmeta.set(Metadata::CodecFrameAvgQP, static_cast<int32_t>(avgQP));
                        pmeta.set(Metadata::CodecFrameSatd, static_cast<int32_t>(satd));
                        pmeta.set(Metadata::CodecEncodeOrderIdx, frameIdxEncode);
                        pmeta.set(Metadata::CodecDisplayOrderIdx, frameIdxDisplay);
                        pmeta.set(Metadata::CodecTemporalId, static_cast<int32_t>(temporalId));
                        pmeta.set(Metadata::CodecGopPosition, static_cast<int32_t>(gopPosition));

                        // Scan mode observed at the output.  Write
                        // through as an Enum wrapped around the
                        // VideoScanMode value so the payload carries
                        // the same encoding the decoder stamps.
                        const VideoScanMode picScan = fromNvencLockedPicStruct(picStruct);
                        pmeta.set(Metadata::VideoScanMode, Enum(VideoScanMode::Type, picScan.value()));

                        // Duration: derive one frame's wall-clock
                        // duration from the session frame rate when
                        // valid.  Video payloads have no intrinsic
                        // rate, so the encoder is responsible for
                        // stamping it.
                        if (_sessionFrameRate.isValid()) {
                                Rational<int> r(static_cast<int>(_sessionFrameRate.numerator()),
                                                static_cast<int>(_sessionFrameRate.denominator()));
                                pkt.modify()->setDuration(Duration::fromSamples(int64_t(1), r));
                        }

                        if (_enableRCStats) {
                                pmeta.set(Metadata::CodecIntraBlockCount, static_cast<int32_t>(intraBlocks));
                                pmeta.set(Metadata::CodecInterBlockCount, static_cast<int32_t>(interBlocks));
                                pmeta.set(Metadata::CodecAvgMotionVectorX, avgMVX);
                                pmeta.set(Metadata::CodecAvgMotionVectorY, avgMVY);
                        }

                        // Out-of-band parameter sets — published on
                        // every emitted packet so downstream MediaIO
                        // stages have an in-frame channel for SPS /
                        // PPS / VPS, independent of any in-band
                        // repetition the bitstream carries.  Stamping
                        // per frame is cheap (a few hundred bytes)
                        // and means consumers don't need to track
                        // first-frame state.
                        if (!_paramSetsBlob.isEmpty()) {
                                pmeta.set(Metadata::CodecParameterSets, _paramSetsBlob);
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
                        if (_encoder) {
                                for (Slot &s : _slots) {
                                        if (s.in) gNvenc.nvEncDestroyInputBuffer(_encoder, s.in);
                                        if (s.out) gNvenc.nvEncDestroyBitstreamBuffer(_encoder, s.out);
                                }
                                _slots.clear();
                                _freeSlots.clear();
                                _inFlight.clear();
                                _pendingPackets.clear();
                                gNvenc.nvEncDestroyEncoder(_encoder);
                                _encoder = nullptr;
                        }
                        _sessionOpen = false;
                        if (_ctxRetained) {
                                cuDevicePrimaryCtxRelease(_device);
                                _ctxRetained = false;
                                _cudaCtx = nullptr;
                        }
                        _width = _height = 0;
                        _frameIdx = 0;
                        _lastKeyDisplayIdx = 0;
                        _eosPending = false;
                        _paramSetsBlob = String();
                }

                // Pending output entries — each pair is (source Frame, emitted
                // compressed packet) so the matching audio / ANC / metadata can
                // be echoed onto the output Frame via VideoEncoder::buildOutputFrame
                // when receiveFrame() pops the entry.  Populated opportunistically
                // by acquireFreeSlot() when it drains completed slots ahead of
                // the next submit; consumed by receiveFrame().
                using PendingPacketEntry = Pair<Frame, CompressedVideoPayload::Ptr>;
                Deque<PendingPacketEntry> _pendingPackets;
};

// ---------------------------------------------------------------------------
// NvencVideoEncoder thin shims that forward to Impl.
// ---------------------------------------------------------------------------

NvencVideoEncoder::NvencVideoEncoder(Codec codec) : _impl(ImplPtr::create(codec)), _codec(codec) {}

NvencVideoEncoder::~NvencVideoEncoder() = default;

List<int> NvencVideoEncoder::supportedInputList() {
        List<int> ret;
        for (const auto &e : kFormatTable) {
                ret.pushToBack(static_cast<int>(e.pixelFormatId));
        }
        return ret;
}

void NvencVideoEncoder::onConfigure(const MediaConfig &config) {
        _impl->configure(config);
}

Error NvencVideoEncoder::submitFrame(const Frame &frame) {
        _impl->clearError();
        UncompressedVideoPayload::Ptr payload = selectInputPayload(frame);
        if (!payload.isValid()) {
                _lastError = Error::Invalid;
                _lastErrorMessage = "NvencVideoEncoder: no uncompressed video payload on frame";
                return _lastError;
        }
        Error err = _impl->submitFrame(frame, *payload, payload->pts(), _requestKey);
        _requestKey = false;
        _lastError = _impl->lastError();
        _lastErrorMessage = _impl->lastErrorMessage();
        return err;
}

Frame NvencVideoEncoder::receiveFrame() {
        return _impl->receiveFrame();
}

Error NvencVideoEncoder::flush() {
        _impl->clearError();
        Error err = _impl->flush();
        _lastError = _impl->lastError();
        _lastErrorMessage = _impl->lastErrorMessage();
        return err;
}

Error NvencVideoEncoder::reset() {
        _impl->clearError();
        Error err = _impl->reset();
        _lastError = _impl->lastError();
        _lastErrorMessage = _impl->lastErrorMessage();
        return err;
}

void NvencVideoEncoder::requestKeyframe() {
        _requestKey = true;
}

// ---------------------------------------------------------------------------
// Backend registration — typed (codec, backend) pair on the
// VideoEncoder registry.  Registered under the "Nvidia" backend name
// for H264 / HEVC / AV1.
// ---------------------------------------------------------------------------

namespace {

        struct NvencRegistrar {
                        NvencRegistrar() {
                                auto bk = VideoCodec::registerBackend("Nvidia");
                                if (error(bk).isError()) return;
                                const VideoCodec::Backend backend = value(bk);

                                const List<int> nvencInputs = NvencVideoEncoder::supportedInputList();

                                VideoEncoder::registerBackend({
                                        .codecId = VideoCodec::H264,
                                        .backend = backend,
                                        .weight = BackendWeight::Vendored,
                                        .supportedInputs = nvencInputs,
                                        .factory = []() -> VideoEncoder * {
                                                return new NvencVideoEncoder(NvencVideoEncoder::Codec_H264);
                                        },
                                });
                                VideoEncoder::registerBackend({
                                        .codecId = VideoCodec::HEVC,
                                        .backend = backend,
                                        .weight = BackendWeight::Vendored,
                                        .supportedInputs = nvencInputs,
                                        .factory = []() -> VideoEncoder * {
                                                return new NvencVideoEncoder(NvencVideoEncoder::Codec_HEVC);
                                        },
                                });
                                VideoEncoder::registerBackend({
                                        .codecId = VideoCodec::AV1,
                                        .backend = backend,
                                        .weight = BackendWeight::Vendored,
                                        .supportedInputs = nvencInputs,
                                        .factory = []() -> VideoEncoder * {
                                                return new NvencVideoEncoder(NvencVideoEncoder::Codec_AV1);
                                        },
                                });
                        }
        };

        static NvencRegistrar _nvencRegistrar;

} // namespace

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NVENC
