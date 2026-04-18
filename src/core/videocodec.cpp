/**
 * @file      videocodec.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 */

#include <promeki/videocodec.h>
#include <promeki/pixeldesc.h>
#include <promeki/atomic.h>
#include <promeki/map.h>

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

static VideoCodec::Data makeH264() {
        VideoCodec::Data d;
        d.id   = VideoCodec::H264;
        d.name = "H264";
        d.desc = "H.264 / MPEG-4 Part 10 AVC";
        d.fourccList = { "avc1", "avc3", "H264" };
        d.compressedPixelDescs = { static_cast<int>(PixelDesc::H264) };
        return d;
}

static VideoCodec::Data makeHEVC() {
        VideoCodec::Data d;
        d.id   = VideoCodec::HEVC;
        d.name = "HEVC";
        d.desc = "H.265 / HEVC";
        d.fourccList = { "hvc1", "hev1", "HEVC" };
        d.compressedPixelDescs = { static_cast<int>(PixelDesc::HEVC) };
        return d;
}

static VideoCodec::Data makeAV1() {
        VideoCodec::Data d;
        d.id   = VideoCodec::AV1;
        d.name = "AV1";
        d.desc = "AV1 (AOMedia Video 1)";
        d.fourccList = { "av01" };
        d.compressedPixelDescs = { static_cast<int>(PixelDesc::AV1) };
        return d;
}

static VideoCodec::Data makeVP9() {
        VideoCodec::Data d;
        d.id   = VideoCodec::VP9;
        d.name = "VP9";
        d.desc = "VP9";
        d.fourccList = { "vp09" };
        return d;
}

static VideoCodec::Data makeJPEG() {
        VideoCodec::Data d;
        d.id   = VideoCodec::JPEG;
        d.name = "JPEG";
        d.desc = "JPEG (ISO/IEC 10918-1 / JFIF)";
        d.fourccList = { "mjpg", "JPEG" };
        d.compressedPixelDescs = {
                static_cast<int>(PixelDesc::JPEG_RGB8_sRGB),
                static_cast<int>(PixelDesc::JPEG_RGBA8_sRGB),
                static_cast<int>(PixelDesc::JPEG_YUV8_422_Rec709),
                static_cast<int>(PixelDesc::JPEG_YUV8_420_Rec709),
                static_cast<int>(PixelDesc::JPEG_YUV8_422_Rec601),
                static_cast<int>(PixelDesc::JPEG_YUV8_420_Rec601),
                static_cast<int>(PixelDesc::JPEG_YUV8_422_Rec709_Full),
                static_cast<int>(PixelDesc::JPEG_YUV8_420_Rec709_Full),
                static_cast<int>(PixelDesc::JPEG_YUV8_422_Rec601_Full),
                static_cast<int>(PixelDesc::JPEG_YUV8_420_Rec601_Full),
        };
        return d;
}

static VideoCodec::Data makeJPEG_XS() {
        VideoCodec::Data d;
        d.id   = VideoCodec::JPEG_XS;
        d.name = "JPEG_XS";
        d.desc = "JPEG XS (ISO/IEC 21122)";
        d.fourccList = { "jxsv" };
        d.compressedPixelDescs = {
                static_cast<int>(PixelDesc::JPEG_XS_YUV8_422_Rec709),
                static_cast<int>(PixelDesc::JPEG_XS_YUV10_422_Rec709),
                static_cast<int>(PixelDesc::JPEG_XS_YUV12_422_Rec709),
                static_cast<int>(PixelDesc::JPEG_XS_YUV8_420_Rec709),
                static_cast<int>(PixelDesc::JPEG_XS_YUV10_420_Rec709),
                static_cast<int>(PixelDesc::JPEG_XS_YUV12_420_Rec709),
                static_cast<int>(PixelDesc::JPEG_XS_RGB8_sRGB),
        };
        return d;
}

static VideoCodec::Data makeProRes(VideoCodec::ID id, const char *name,
                                   const char *desc, FourCC fourcc,
                                   PixelDesc::ID compressedPd) {
        VideoCodec::Data d;
        d.id   = id;
        d.name = name;
        d.desc = desc;
        d.fourccList = { fourcc };
        d.compressedPixelDescs = { static_cast<int>(compressedPd) };
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
                               PixelDesc::ProRes_422_Proxy));
                add(makeProRes(VideoCodec::ProRes_422_LT,    "ProRes_422_LT",
                               "Apple ProRes 422 LT",    FourCC("apcs"),
                               PixelDesc::ProRes_422_LT));
                add(makeProRes(VideoCodec::ProRes_422,       "ProRes_422",
                               "Apple ProRes 422",       FourCC("apcn"),
                               PixelDesc::ProRes_422));
                add(makeProRes(VideoCodec::ProRes_422_HQ,    "ProRes_422_HQ",
                               "Apple ProRes 422 HQ",    FourCC("apch"),
                               PixelDesc::ProRes_422_HQ));
                add(makeProRes(VideoCodec::ProRes_4444,      "ProRes_4444",
                               "Apple ProRes 4444",      FourCC("ap4h"),
                               PixelDesc::ProRes_4444));
                add(makeProRes(VideoCodec::ProRes_4444_XQ,   "ProRes_4444_XQ",
                               "Apple ProRes 4444 XQ",   FourCC("ap4x"),
                               PixelDesc::ProRes_4444_XQ));
        }

        void add(VideoCodec::Data d) {
                VideoCodec::ID id = d.id;
                if(id != VideoCodec::Invalid) nameMap[d.name] = id;
                entries[id] = std::move(d);
        }
};

static VideoCodecRegistry &registry() {
        static VideoCodecRegistry reg;
        return reg;
}

// ---------------------------------------------------------------------------
// Static methods
// ---------------------------------------------------------------------------

const VideoCodec::Data *VideoCodec::lookupData(ID id) {
        auto &reg = registry();
        auto it = reg.entries.find(id);
        if(it != reg.entries.end()) return &it->second;
        return &reg.entries[Invalid];
}

void VideoCodec::registerData(Data &&data) {
        auto &reg = registry();
        if(data.id != Invalid) reg.nameMap[data.name] = data.id;
        reg.entries[data.id] = std::move(data);
}

VideoCodec VideoCodec::lookup(const String &name) {
        auto &reg = registry();
        auto it = reg.nameMap.find(name);
        return (it != reg.nameMap.end()) ? VideoCodec(it->second)
                                         : VideoCodec(Invalid);
}

VideoCodec VideoCodec::fromPixelDesc(const PixelDesc &pd) {
        if(!pd.isValid()) return VideoCodec(Invalid);
        int target = static_cast<int>(pd.id());
        auto &reg = registry();
        for(const auto &[id, data] : reg.entries) {
                if(id == Invalid) continue;
                for(int cpd : data.compressedPixelDescs) {
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

PROMEKI_NAMESPACE_END
