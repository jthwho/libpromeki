/**
 * @file      videocodec.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 */

#include <promeki/videocodec.h>
#include <promeki/pixelformat.h>
#include <promeki/atomic.h>
#include <promeki/map.h>
#include <promeki/set.h>
#include <promeki/stringlist.h>
#include <promeki/logger.h>
#include <promeki/enums.h>

PROMEKI_NAMESPACE_BEGIN

// ---------------------------------------------------------------------------
// Atomic ID counter for user-registered codecs
// ---------------------------------------------------------------------------

static Atomic<int> _nextType{VideoCodec::UserDefined};

VideoCodec::ID VideoCodec::registerType() {
        return static_cast<ID>(_nextType.fetchAndAdd(1));
}

// ---------------------------------------------------------------------------
// Well-known codec Data records
// ---------------------------------------------------------------------------

static VideoCodec::Data makeInvalid() {
        VideoCodec::Data d;
        d.id   = VideoCodec::Invalid;
        d.name = "Invalid";
        d.desc = "Invalid or uninitialised video codec";
        return d;
}

// Common rate-control list for modern inter-frame codecs: CBR / VBR / CQP.
static List<RateControlMode> commonRateControls() {
        return {
                RateControlMode::CBR,
                RateControlMode::VBR,
                RateControlMode::CQP,
        };
}

static VideoCodec::Data makeH264() {
        VideoCodec::Data d;
        d.id                         = VideoCodec::H264;
        d.name                       = "H264";
        d.desc                       = "H.264 / MPEG-4 Part 10 AVC";
        d.fourccList                 = { "avc1", "avc3", "H264" };
        d.compressedPixelFormats     = { static_cast<int>(PixelFormat::H264) };
        d.codingType                 = VideoCodec::CodingTemporal;
        d.randomAccessGranularity    = VideoCodec::AccessGOP;
        d.supportsBFrames            = true;
        d.supportsLossless           = true;   // High 4:4:4 Predictive lossless mode
        d.supportsAlpha              = false;
        d.supportsVariableFrameSize  = false;  // only at IDR
        d.supportsHDRMetadata        = true;   // VUI + SEI
        d.supportsInterlaced         = true;   // PAFF / MBAFF
        d.supportedBitDepths         = { 8, 10 };
        d.rateControlModes           = commonRateControls();
        return d;
}

static VideoCodec::Data makeHEVC() {
        VideoCodec::Data d;
        d.id                         = VideoCodec::HEVC;
        d.name                       = "HEVC";
        d.desc                       = "H.265 / HEVC";
        d.fourccList                 = { "hvc1", "hev1", "HEVC" };
        d.compressedPixelFormats     = { static_cast<int>(PixelFormat::HEVC) };
        d.codingType                 = VideoCodec::CodingTemporal;
        d.randomAccessGranularity    = VideoCodec::AccessGOP;
        d.supportsBFrames            = true;
        d.supportsLossless           = true;
        d.supportsAlpha              = false;  // Main profile; Alpha extension is rare
        d.supportsVariableFrameSize  = false;
        d.supportsHDRMetadata        = true;
        d.supportsInterlaced         = true;
        d.supportedBitDepths         = { 8, 10, 12 };
        d.rateControlModes           = commonRateControls();
        return d;
}

static VideoCodec::Data makeAV1() {
        VideoCodec::Data d;
        d.id                         = VideoCodec::AV1;
        d.name                       = "AV1";
        d.desc                       = "AV1 (AOMedia Video 1)";
        d.fourccList                 = { "av01" };
        d.compressedPixelFormats     = { static_cast<int>(PixelFormat::AV1) };
        d.codingType                 = VideoCodec::CodingTemporal;
        d.randomAccessGranularity    = VideoCodec::AccessGOP;
        d.supportsBFrames            = true;  // via alt-ref / reorder
        d.supportsLossless           = true;
        d.supportsAlpha              = false;
        d.supportsVariableFrameSize  = true;
        d.supportsHDRMetadata        = true;
        d.supportsInterlaced         = false;
        d.supportedBitDepths         = { 8, 10, 12 };
        d.rateControlModes           = commonRateControls();
        return d;
}

static VideoCodec::Data makeVP9() {
        VideoCodec::Data d;
        d.id                         = VideoCodec::VP9;
        d.name                       = "VP9";
        d.desc                       = "VP9";
        d.fourccList                 = { "vp09" };
        d.codingType                 = VideoCodec::CodingTemporal;
        d.randomAccessGranularity    = VideoCodec::AccessGOP;
        d.supportsBFrames            = false;  // VP9 uses alt-ref but no explicit B-frame reorder
        d.supportsLossless           = true;
        d.supportsAlpha              = true;   // coded alpha frame profile
        d.supportsVariableFrameSize  = true;
        d.supportsHDRMetadata        = true;
        d.supportsInterlaced         = false;
        d.supportedBitDepths         = { 8, 10, 12 };
        d.rateControlModes           = commonRateControls();
        return d;
}

static VideoCodec::Data makeJPEG() {
        VideoCodec::Data d;
        d.id                         = VideoCodec::JPEG;
        d.name                       = "JPEG";
        d.desc                       = "JPEG (ISO/IEC 10918-1 / JFIF)";
        d.fourccList                 = { "mjpg", "JPEG" };
        d.compressedPixelFormats     = {
                static_cast<int>(PixelFormat::JPEG_RGB8_sRGB),
                static_cast<int>(PixelFormat::JPEG_RGBA8_sRGB),
                static_cast<int>(PixelFormat::JPEG_YUV8_422_Rec709),
                static_cast<int>(PixelFormat::JPEG_YUV8_420_Rec709),
                static_cast<int>(PixelFormat::JPEG_YUV8_422_Rec601),
                static_cast<int>(PixelFormat::JPEG_YUV8_420_Rec601),
                static_cast<int>(PixelFormat::JPEG_YUV8_422_Rec709_Full),
                static_cast<int>(PixelFormat::JPEG_YUV8_420_Rec709_Full),
                static_cast<int>(PixelFormat::JPEG_YUV8_422_Rec601_Full),
                static_cast<int>(PixelFormat::JPEG_YUV8_420_Rec601_Full),
        };
        d.codingType                 = VideoCodec::CodingIntraOnly;
        d.randomAccessGranularity    = VideoCodec::AccessFrame;
        d.supportsBFrames            = false;
        d.supportsLossless           = true;   // JPEG has a lossless mode (rarely used)
        d.supportsAlpha              = false;  // standard JPEG has no alpha
        d.supportsVariableFrameSize  = true;
        d.supportsHDRMetadata        = false;
        d.supportsInterlaced         = false;
        d.supportedBitDepths         = { 8, 12 };
        // JPEG is quality-driven rather than bitrate-driven; leave
        // rateControlModes empty so planners know not to wire a
        // RateControlMode expectation against it.
        return d;
}

static VideoCodec::Data makeJPEG_XS() {
        VideoCodec::Data d;
        d.id                         = VideoCodec::JPEG_XS;
        d.name                       = "JPEG_XS";
        d.desc                       = "JPEG XS (ISO/IEC 21122)";
        d.fourccList                 = { "jxsv" };
        d.compressedPixelFormats     = {
                static_cast<int>(PixelFormat::JPEG_XS_YUV8_422_Rec709),
                static_cast<int>(PixelFormat::JPEG_XS_YUV10_422_Rec709),
                static_cast<int>(PixelFormat::JPEG_XS_YUV12_422_Rec709),
                static_cast<int>(PixelFormat::JPEG_XS_YUV8_420_Rec709),
                static_cast<int>(PixelFormat::JPEG_XS_YUV10_420_Rec709),
                static_cast<int>(PixelFormat::JPEG_XS_YUV12_420_Rec709),
                static_cast<int>(PixelFormat::JPEG_XS_RGB8_sRGB),
        };
        d.codingType                 = VideoCodec::CodingIntraOnly;
        d.randomAccessGranularity    = VideoCodec::AccessFrame;
        d.supportsBFrames            = false;
        d.supportsLossless           = false;  // visually lossless, not mathematically
        d.supportsAlpha              = true;   // up to 4 components
        d.supportsVariableFrameSize  = true;
        d.supportsHDRMetadata        = false;
        d.supportsInterlaced         = false;
        d.supportedBitDepths         = { 8, 10, 12 };
        d.rateControlModes           = { RateControlMode::CBR };
        return d;
}

static VideoCodec::Data makeProRes(VideoCodec::ID id, const char *name,
                                   const char *desc, FourCC fourcc,
                                   PixelFormat::ID compressedPd,
                                   bool alpha, int bitDepth) {
        VideoCodec::Data d;
        d.id                         = id;
        d.name                       = name;
        d.desc                       = desc;
        d.fourccList                 = { fourcc };
        d.compressedPixelFormats     = { static_cast<int>(compressedPd) };
        d.codingType                 = VideoCodec::CodingIntraOnly;
        d.randomAccessGranularity    = VideoCodec::AccessFrame;
        d.supportsBFrames            = false;
        d.supportsLossless           = false;   // ProRes is high-quality lossy
        d.supportsAlpha              = alpha;
        d.supportsVariableFrameSize  = false;
        d.supportsHDRMetadata        = true;    // carries colorspace / gamma tags
        d.supportsInterlaced         = true;
        d.supportedBitDepths         = { bitDepth };
        d.rateControlModes           = { RateControlMode::CBR };  // effectively CBR per-tier
        return d;
}

// ---------------------------------------------------------------------------
// Construct-on-first-use registry.  Uniquely named (rather than the
// generic "DataRegistry") so it never clashes with sibling TypeRegistry
// helpers in other TUs at link time.
// ---------------------------------------------------------------------------

struct VideoCodecRegistry {
        Map<VideoCodec::ID, VideoCodec::Data> entries;
        Map<String, VideoCodec::ID>           nameMap;

        VideoCodecRegistry() {
                add(makeInvalid());
                add(makeH264());
                add(makeHEVC());
                add(makeAV1());
                add(makeVP9());
                add(makeJPEG());
                add(makeJPEG_XS());
                add(makeProRes(VideoCodec::ProRes_422_Proxy, "ProRes_422_Proxy",
                               "Apple ProRes 422 Proxy", FourCC("apco"),
                               PixelFormat::ProRes_422_Proxy, /*alpha=*/false, /*bd=*/10));
                add(makeProRes(VideoCodec::ProRes_422_LT,    "ProRes_422_LT",
                               "Apple ProRes 422 LT",    FourCC("apcs"),
                               PixelFormat::ProRes_422_LT, false, 10));
                add(makeProRes(VideoCodec::ProRes_422,       "ProRes_422",
                               "Apple ProRes 422",       FourCC("apcn"),
                               PixelFormat::ProRes_422, false, 10));
                add(makeProRes(VideoCodec::ProRes_422_HQ,    "ProRes_422_HQ",
                               "Apple ProRes 422 HQ",    FourCC("apch"),
                               PixelFormat::ProRes_422_HQ, false, 10));
                add(makeProRes(VideoCodec::ProRes_4444,      "ProRes_4444",
                               "Apple ProRes 4444",      FourCC("ap4h"),
                               PixelFormat::ProRes_4444, true, 12));
                add(makeProRes(VideoCodec::ProRes_4444_XQ,   "ProRes_4444_XQ",
                               "Apple ProRes 4444 XQ",   FourCC("ap4x"),
                               PixelFormat::ProRes_4444_XQ, true, 12));
        }

        void add(VideoCodec::Data d) {
                VideoCodec::ID id = d.id;
                // Register every name including the "Invalid" sentinel
                // so a Variant String round-trip (VideoCodec() →
                // "Invalid" → VideoCodec()) is lossless.  See the
                // PixelFormat::registerData rationale; the same
                // self-consistency rule applies to every TypeRegistry
                // type.
                nameMap[d.name] = id;
                entries[id] = std::move(d);
        }
};

static VideoCodecRegistry &registry() {
        static VideoCodecRegistry reg;
        return reg;
}

// ---------------------------------------------------------------------------
// Static methods — Data registry
// ---------------------------------------------------------------------------

const VideoCodec::Data *VideoCodec::lookupData(ID id) {
        auto &reg = registry();
        auto it = reg.entries.find(id);
        if(it != reg.entries.end()) return &it->second;
        return &reg.entries[Invalid];
}

void VideoCodec::registerData(Data &&data) {
        if(data.id != Invalid && !data.name.isIdentifier()) {
                promekiWarn("VideoCodec::registerData rejected name '%s' "
                            "(must be a valid C identifier)",
                            data.name.cstr());
                return;
        }
        auto &reg = registry();
        // Register every name including the "Invalid" sentinel so a
        // Variant String round-trip is lossless — see add() / the
        // PixelFormat::registerData rationale.
        reg.nameMap[data.name] = data.id;
        reg.entries[data.id] = std::move(data);
}

Result<VideoCodec> VideoCodec::lookup(const String &name) {
        auto &reg = registry();
        auto it = reg.nameMap.find(name);
        if(it == reg.nameMap.end()) return makeError<VideoCodec>(Error::IdNotFound);
        return makeResult(VideoCodec(it->second));
}

VideoCodec VideoCodec::fromPixelFormat(const PixelFormat &pd) {
        if(!pd.isValid()) return VideoCodec(Invalid);
        int target = static_cast<int>(pd.id());
        auto &reg = registry();
        for(const auto &[id, data] : reg.entries) {
                if(id == Invalid) continue;
                for(int cpd : data.compressedPixelFormats) {
                        if(cpd == target) return VideoCodec(id);
                }
        }
        return VideoCodec(Invalid);
}

VideoCodec::IDList VideoCodec::registeredIDs() {
        auto &reg = registry();
        IDList ret;
        for(const auto &[id, data] : reg.entries) {
                if(id != Invalid) ret.pushToBack(id);
        }
        return ret;
}

// ---------------------------------------------------------------------------
// Backend name/ID registry — typed handles via StringRegistry
// ---------------------------------------------------------------------------

Result<VideoCodec::Backend> VideoCodec::registerBackend(const String &name) {
        if(!name.isIdentifier()) {
                return makeError<Backend>(Error::Invalid);
        }
        uint64_t id = VideoCodecBackendRegistry::instance().findOrCreateProbe(name);
        return makeResult(Backend::fromId(id));
}

Result<VideoCodec::Backend> VideoCodec::lookupBackend(const String &name) {
        if(!name.isIdentifier()) {
                return makeError<Backend>(Error::Invalid);
        }
        uint64_t id = VideoCodecBackendRegistry::instance().findId(name);
        if(id == VideoCodecBackendRegistry::InvalidID) {
                return makeError<Backend>(Error::IdNotFound);
        }
        return makeResult(Backend::fromId(id));
}

// registeredBackends() — lives in videoencoder.cpp because it needs
// access to the encoder/decoder backend-registration tables (the ones
// that actually install providers).  Declared here but defined there.

// ---------------------------------------------------------------------------
// String form parsing / emission
// ---------------------------------------------------------------------------

Result<VideoCodec> VideoCodec::fromString(const String &spec) {
        if(spec.isEmpty()) return makeError<VideoCodec>(Error::Invalid);
        // Split on ':' — at most one colon, dividing codec-name from
        // backend-name.  Anything more exotic is malformed.
        StringList parts = spec.split(":");
        if(parts.size() > 2) {
                return makeError<VideoCodec>(Error::Invalid);
        }
        const String &codecName = parts[0];
        auto codecResult = lookup(codecName);
        if(error(codecResult).isError()) {
                return makeError<VideoCodec>(error(codecResult));
        }
        VideoCodec codec = value(codecResult);
        if(parts.size() == 1) return makeResult(codec);

        auto backendResult = lookupBackend(parts[1]);
        if(error(backendResult).isError()) {
                return makeError<VideoCodec>(error(backendResult));
        }
        return makeResult(VideoCodec(codec.id(), value(backendResult)));
}

String VideoCodec::toString() const {
        // Always emit the registered name (including the "Invalid"
        // sentinel) so a Variant String round-trip is lossless —
        // returning empty here would force fromString back through
        // its Error::Invalid early-return and trip spec validation
        // for any default declared as setDefault(VideoCodec()).
        if(_backend.isValid()) {
                String bn = _backend.name();
                if(!bn.isEmpty()) return name() + ":" + bn;
        }
        return name();
}

PROMEKI_NAMESPACE_END
