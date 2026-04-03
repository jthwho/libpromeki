/**
 * @file      pixeldesc.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/core/pixeldesc.h>
#include <promeki/core/atomic.h>
#include <promeki/core/map.h>

PROMEKI_NAMESPACE_BEGIN

// ---------------------------------------------------------------------------
// Atomic ID counter for user-registered types
// ---------------------------------------------------------------------------

static Atomic<int> _nextType{PixelDesc::UserDefined};

PixelDesc::ID PixelDesc::registerType() {
        return static_cast<ID>(_nextType.fetchAndAdd(1));
}

// ---------------------------------------------------------------------------
// Factory functions for well-known pixel descriptions
// ---------------------------------------------------------------------------

static PixelDesc::Data makeInvalid() {
        PixelDesc::Data d;
        d.id   = PixelDesc::Invalid;
        d.name = "Invalid";
        d.desc = "Invalid pixel description";
        return d;
}

static PixelDesc::Data makeRGBA8() {
        PixelDesc::Data d;
        d.id                    = PixelDesc::RGBA8_sRGB_Full;
        d.name                  = "RGBA8_sRGB_Full";
        d.desc                  = "8-bit RGBA, sRGB, full range";
        d.pixelFormat           = PixelFormat(PixelFormat::Interleaved_4x8);
        d.colorModel            = ColorModel(ColorModel::sRGB);
        d.hasAlpha              = true;
        d.alphaCompIndex        = 3;
        d.fourccList            = { "RGBA" };
        d.compSemantics[0]      = { "Red",   "R", 0, 255 };
        d.compSemantics[1]      = { "Green", "G", 0, 255 };
        d.compSemantics[2]      = { "Blue",  "B", 0, 255 };
        d.compSemantics[3]      = { "Alpha", "A", 0, 255 };
        return d;
}

static PixelDesc::Data makeRGB8() {
        PixelDesc::Data d;
        d.id                    = PixelDesc::RGB8_sRGB_Full;
        d.name                  = "RGB8_sRGB_Full";
        d.desc                  = "8-bit RGB, sRGB, full range";
        d.pixelFormat           = PixelFormat(PixelFormat::Interleaved_3x8);
        d.colorModel            = ColorModel(ColorModel::sRGB);
        d.fourccList            = { "RGB2" };
        d.compSemantics[0]      = { "Red",   "R", 0, 255 };
        d.compSemantics[1]      = { "Green", "G", 0, 255 };
        d.compSemantics[2]      = { "Blue",  "B", 0, 255 };
        return d;
}

static PixelDesc::Data makeRGB10() {
        PixelDesc::Data d;
        d.id                    = PixelDesc::RGB10_sRGB_Full;
        d.name                  = "RGB10_sRGB_Full";
        d.desc                  = "10-bit RGB, sRGB, full range";
        d.pixelFormat           = PixelFormat(PixelFormat::Interleaved_3x10);
        d.colorModel            = ColorModel(ColorModel::sRGB);
        d.compSemantics[0]      = { "Red",   "R", 0, 1023 };
        d.compSemantics[1]      = { "Green", "G", 0, 1023 };
        d.compSemantics[2]      = { "Blue",  "B", 0, 1023 };
        return d;
}

static PixelDesc::Data makeYUV8_422() {
        PixelDesc::Data d;
        d.id                    = PixelDesc::YUV8_422_Rec709_Limited;
        d.name                  = "YUV8_422_Rec709_Limited";
        d.desc                  = "8-bit YCbCr 4:2:2, Rec.709, limited range";
        d.pixelFormat           = PixelFormat(PixelFormat::Interleaved_422_3x8);
        d.colorModel            = ColorModel(ColorModel::YCbCr_Rec709);
        d.compSemantics[0]      = { "Luma",           "Y",  16, 235 };
        d.compSemantics[1]      = { "Chroma Blue",    "Cb", 16, 240 };
        d.compSemantics[2]      = { "Chroma Red",     "Cr", 16, 240 };
        return d;
}

static PixelDesc::Data makeYUV10_422() {
        PixelDesc::Data d;
        d.id                    = PixelDesc::YUV10_422_Rec709_Limited;
        d.name                  = "YUV10_422_Rec709_Limited";
        d.desc                  = "10-bit YCbCr 4:2:2, Rec.709, limited range";
        d.pixelFormat           = PixelFormat(PixelFormat::Interleaved_422_3x10);
        d.colorModel            = ColorModel(ColorModel::YCbCr_Rec709);
        d.compSemantics[0]      = { "Luma",           "Y",  64,  940 };
        d.compSemantics[1]      = { "Chroma Blue",    "Cb", 64,  960 };
        d.compSemantics[2]      = { "Chroma Red",     "Cr", 64,  960 };
        return d;
}

static PixelDesc::Data makeJPEG_RGBA8() {
        PixelDesc::Data d;
        d.id                        = PixelDesc::JPEG_RGBA8_sRGB_Full;
        d.name                      = "JPEG_RGBA8_sRGB_Full";
        d.desc                      = "JPEG-compressed 8-bit RGBA";
        d.pixelFormat               = PixelFormat(PixelFormat::Interleaved_4x8);
        d.colorModel                = ColorModel(ColorModel::sRGB);
        d.hasAlpha                  = true;
        d.alphaCompIndex            = 3;
        d.compressed                = true;
        d.codecName                 = "jpeg";
        d.encodeSources             = { PixelDesc::RGBA8_sRGB_Full };
        d.decodeTargets             = { PixelDesc::RGBA8_sRGB_Full };
        d.fourccList                = { "jpeg", "mjpa", "mjpb", "mjpg", "AVRn", "AVDJ", "ADJV" };
        d.compSemantics[0]          = { "Red",   "R", 0, 255 };
        d.compSemantics[1]          = { "Green", "G", 0, 255 };
        d.compSemantics[2]          = { "Blue",  "B", 0, 255 };
        d.compSemantics[3]          = { "Alpha", "A", 0, 255 };
        return d;
}

static PixelDesc::Data makeJPEG_RGB8() {
        PixelDesc::Data d;
        d.id                        = PixelDesc::JPEG_RGB8_sRGB_Full;
        d.name                      = "JPEG_RGB8_sRGB_Full";
        d.desc                      = "JPEG-compressed 8-bit RGB";
        d.pixelFormat               = PixelFormat(PixelFormat::Interleaved_3x8);
        d.colorModel                = ColorModel(ColorModel::sRGB);
        d.compressed                = true;
        d.codecName                 = "jpeg";
        d.encodeSources             = { PixelDesc::RGB8_sRGB_Full, PixelDesc::RGBA8_sRGB_Full };
        d.decodeTargets             = { PixelDesc::RGB8_sRGB_Full };
        d.fourccList                = { "jpeg", "mjpa", "mjpb", "mjpg", "AVRn", "AVDJ", "ADJV" };
        d.compSemantics[0]          = { "Red",   "R", 0, 255 };
        d.compSemantics[1]          = { "Green", "G", 0, 255 };
        d.compSemantics[2]          = { "Blue",  "B", 0, 255 };
        return d;
}

static PixelDesc::Data makeJPEG_YUV8_422() {
        PixelDesc::Data d;
        d.id                        = PixelDesc::JPEG_YUV8_422_Rec709_Limited;
        d.name                      = "JPEG_YUV8_422_Rec709_Limited";
        d.desc                      = "JPEG-compressed 8-bit YCbCr 4:2:2";
        d.pixelFormat               = PixelFormat(PixelFormat::Interleaved_422_3x8);
        d.colorModel                = ColorModel(ColorModel::YCbCr_Rec709);
        d.compressed                = true;
        d.codecName                 = "jpeg";
        d.encodeSources             = { PixelDesc::RGB8_sRGB_Full, PixelDesc::RGBA8_sRGB_Full };
        d.decodeTargets             = { PixelDesc::YUV8_422_Rec709_Limited };
        d.fourccList                = { "jpeg", "mjpa", "mjpb", "mjpg", "AVRn", "AVDJ", "ADJV" };
        d.compSemantics[0]          = { "Luma",           "Y",  16, 235 };
        d.compSemantics[1]          = { "Chroma Blue",    "Cb", 16, 240 };
        d.compSemantics[2]          = { "Chroma Red",     "Cr", 16, 240 };
        return d;
}

// ---------------------------------------------------------------------------
// Construct-on-first-use registry
// ---------------------------------------------------------------------------

struct PixelDescRegistry {
        Map<PixelDesc::ID, PixelDesc::Data> entries;
        Map<String, PixelDesc::ID> nameMap;

        PixelDescRegistry() {
                add(makeInvalid());
                add(makeRGBA8());
                add(makeRGB8());
                add(makeRGB10());
                add(makeYUV8_422());
                add(makeYUV10_422());
                add(makeJPEG_RGBA8());
                add(makeJPEG_RGB8());
                add(makeJPEG_YUV8_422());
        }

        void add(PixelDesc::Data d) {
                PixelDesc::ID id = d.id;
                if(d.id != PixelDesc::Invalid) {
                        nameMap[d.name] = id;
                }
                entries[id] = std::move(d);
        }
};

static PixelDescRegistry &registry() {
        static PixelDescRegistry reg;
        return reg;
}

const PixelDesc::Data *PixelDesc::lookupData(ID id) {
        auto &reg = registry();
        auto it = reg.entries.find(id);
        if(it != reg.entries.end()) return &it->second;
        return &reg.entries[Invalid];
}

void PixelDesc::registerData(Data &&data) {
        auto &reg = registry();
        if(data.id != Invalid) {
                reg.nameMap[data.name] = data.id;
        }
        reg.entries[data.id] = std::move(data);
}

PixelDesc PixelDesc::lookup(const String &name) {
        auto &reg = registry();
        auto it = reg.nameMap.find(name);
        return (it != reg.nameMap.end()) ? PixelDesc(it->second) : PixelDesc(Invalid);
}

PixelDesc::IDList PixelDesc::registeredIDs() {
        auto &reg = registry();
        IDList ret;
        for(const auto &[id, data] : reg.entries) {
                if(id != Invalid) ret.pushToBack(id);
        }
        return ret;
}

PROMEKI_NAMESPACE_END
