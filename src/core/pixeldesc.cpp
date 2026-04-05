/**
 * @file      pixeldesc.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/pixeldesc.h>
#include <promeki/atomic.h>
#include <promeki/map.h>
#include <promeki/paintengine.h>
#include <promeki/imagedesc.h>
#include <promeki/metadata.h>

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
        d.desc                  = "8-bit YCbCr 4:2:2 YUYV, Rec.709, limited range";
        d.pixelFormat           = PixelFormat(PixelFormat::Interleaved_422_3x8);
        d.colorModel            = ColorModel(ColorModel::YCbCr_Rec709);
        d.fourccList            = { "YUY2", "YUYV" };
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
        d.encodeSources             = { PixelDesc::RGB8_sRGB_Full, PixelDesc::RGBA8_sRGB_Full,
                                        PixelDesc::YUV8_422_Rec709_Limited,
                                        PixelDesc::YUV8_422_UYVY_Rec709_Limited,
                                        PixelDesc::YUV8_422_Planar_Rec709_Limited };
        d.decodeTargets             = { PixelDesc::YUV8_422_Rec709_Limited,
                                        PixelDesc::YUV8_422_UYVY_Rec709_Limited,
                                        PixelDesc::YUV8_422_Planar_Rec709_Limited,
                                        PixelDesc::RGB8_sRGB_Full,
                                        PixelDesc::RGBA8_sRGB_Full };
        d.fourccList                = { "jpeg", "mjpa", "mjpb", "mjpg", "AVRn", "AVDJ", "ADJV" };
        d.compSemantics[0]          = { "Luma",           "Y",  16, 235 };
        d.compSemantics[1]          = { "Chroma Blue",    "Cb", 16, 240 };
        d.compSemantics[2]          = { "Chroma Red",     "Cr", 16, 240 };
        return d;
}

static PixelDesc::Data makeJPEG_YUV8_420() {
        PixelDesc::Data d;
        d.id                        = PixelDesc::JPEG_YUV8_420_Rec709_Limited;
        d.name                      = "JPEG_YUV8_420_Rec709_Limited";
        d.desc                      = "JPEG-compressed 8-bit YCbCr 4:2:0";
        d.pixelFormat               = PixelFormat(PixelFormat::Planar_420_3x8);
        d.colorModel                = ColorModel(ColorModel::YCbCr_Rec709);
        d.compressed                = true;
        d.codecName                 = "jpeg";
        d.encodeSources             = { PixelDesc::RGB8_sRGB_Full, PixelDesc::RGBA8_sRGB_Full,
                                        PixelDesc::YUV8_420_Planar_Rec709_Limited,
                                        PixelDesc::YUV8_420_SemiPlanar_Rec709_Limited };
        d.decodeTargets             = { PixelDesc::YUV8_420_Planar_Rec709_Limited,
                                        PixelDesc::YUV8_420_SemiPlanar_Rec709_Limited,
                                        PixelDesc::YUV8_422_UYVY_Rec709_Limited,
                                        PixelDesc::YUV8_422_Rec709_Limited,
                                        PixelDesc::RGB8_sRGB_Full,
                                        PixelDesc::RGBA8_sRGB_Full };
        d.fourccList                = { "jpeg", "mjpg" };
        d.compSemantics[0]          = { "Luma",           "Y",  16, 235 };
        d.compSemantics[1]          = { "Chroma Blue",    "Cb", 16, 240 };
        d.compSemantics[2]          = { "Chroma Red",     "Cr", 16, 240 };
        return d;
}

static PixelDesc::Data makeYUV8_422_UYVY() {
        PixelDesc::Data d;
        d.id                    = PixelDesc::YUV8_422_UYVY_Rec709_Limited;
        d.name                  = "YUV8_422_UYVY_Rec709_Limited";
        d.desc                  = "8-bit YCbCr 4:2:2 UYVY, Rec.709, limited range";
        d.pixelFormat           = PixelFormat(PixelFormat::Interleaved_422_UYVY_3x8);
        d.colorModel            = ColorModel(ColorModel::YCbCr_Rec709);
        d.fourccList            = { "UYVY" };
        d.compSemantics[0]      = { "Luma",           "Y",  16, 235 };
        d.compSemantics[1]      = { "Chroma Blue",    "Cb", 16, 240 };
        d.compSemantics[2]      = { "Chroma Red",     "Cr", 16, 240 };
        return d;
}

static PixelDesc::Data makeYUV10_422_UYVY_LE() {
        PixelDesc::Data d;
        d.id                    = PixelDesc::YUV10_422_UYVY_LE_Rec709_Limited;
        d.name                  = "YUV10_422_UYVY_LE_Rec709_Limited";
        d.desc                  = "10-bit YCbCr 4:2:2 UYVY LE, Rec.709, limited range";
        d.pixelFormat           = PixelFormat(PixelFormat::Interleaved_422_UYVY_3x10_LE);
        d.colorModel            = ColorModel(ColorModel::YCbCr_Rec709);
        d.compSemantics[0]      = { "Luma",           "Y",  64,  940 };
        d.compSemantics[1]      = { "Chroma Blue",    "Cb", 64,  960 };
        d.compSemantics[2]      = { "Chroma Red",     "Cr", 64,  960 };
        return d;
}

static PixelDesc::Data makeYUV10_422_UYVY_BE() {
        PixelDesc::Data d;
        d.id                    = PixelDesc::YUV10_422_UYVY_BE_Rec709_Limited;
        d.name                  = "YUV10_422_UYVY_BE_Rec709_Limited";
        d.desc                  = "10-bit YCbCr 4:2:2 UYVY BE, Rec.709, limited range";
        d.pixelFormat           = PixelFormat(PixelFormat::Interleaved_422_UYVY_3x10_BE);
        d.colorModel            = ColorModel(ColorModel::YCbCr_Rec709);
        d.compSemantics[0]      = { "Luma",           "Y",  64,  940 };
        d.compSemantics[1]      = { "Chroma Blue",    "Cb", 64,  960 };
        d.compSemantics[2]      = { "Chroma Red",     "Cr", 64,  960 };
        return d;
}

static PixelDesc::Data makeYUV12_422_UYVY_LE() {
        PixelDesc::Data d;
        d.id                    = PixelDesc::YUV12_422_UYVY_LE_Rec709_Limited;
        d.name                  = "YUV12_422_UYVY_LE_Rec709_Limited";
        d.desc                  = "12-bit YCbCr 4:2:2 UYVY LE, Rec.709, limited range";
        d.pixelFormat           = PixelFormat(PixelFormat::Interleaved_422_UYVY_3x12_LE);
        d.colorModel            = ColorModel(ColorModel::YCbCr_Rec709);
        d.compSemantics[0]      = { "Luma",           "Y",  256, 3760 };
        d.compSemantics[1]      = { "Chroma Blue",    "Cb", 256, 3840 };
        d.compSemantics[2]      = { "Chroma Red",     "Cr", 256, 3840 };
        return d;
}

static PixelDesc::Data makeYUV12_422_UYVY_BE() {
        PixelDesc::Data d;
        d.id                    = PixelDesc::YUV12_422_UYVY_BE_Rec709_Limited;
        d.name                  = "YUV12_422_UYVY_BE_Rec709_Limited";
        d.desc                  = "12-bit YCbCr 4:2:2 UYVY BE, Rec.709, limited range";
        d.pixelFormat           = PixelFormat(PixelFormat::Interleaved_422_UYVY_3x12_BE);
        d.colorModel            = ColorModel(ColorModel::YCbCr_Rec709);
        d.compSemantics[0]      = { "Luma",           "Y",  256, 3760 };
        d.compSemantics[1]      = { "Chroma Blue",    "Cb", 256, 3840 };
        d.compSemantics[2]      = { "Chroma Red",     "Cr", 256, 3840 };
        return d;
}

static PixelDesc::Data makeYUV10_422_v210() {
        PixelDesc::Data d;
        d.id                    = PixelDesc::YUV10_422_v210_Rec709_Limited;
        d.name                  = "YUV10_422_v210_Rec709_Limited";
        d.desc                  = "10-bit YCbCr 4:2:2 v210 packed, Rec.709, limited range";
        d.pixelFormat           = PixelFormat(PixelFormat::Interleaved_422_v210);
        d.colorModel            = ColorModel(ColorModel::YCbCr_Rec709);
        d.fourccList            = { "v210" };
        d.compSemantics[0]      = { "Luma",           "Y",  64,  940 };
        d.compSemantics[1]      = { "Chroma Blue",    "Cb", 64,  960 };
        d.compSemantics[2]      = { "Chroma Red",     "Cr", 64,  960 };
        return d;
}

// ---------------------------------------------------------------------------
// Planar 4:2:2 PixelDesc factory functions
// ---------------------------------------------------------------------------

static const PixelDesc::CompSemantic ycbcrSem8[]  = { {"Luma","Y",16,235}, {"Chroma Blue","Cb",16,240}, {"Chroma Red","Cr",16,240} };
static const PixelDesc::CompSemantic ycbcrSem10[] = { {"Luma","Y",64,940}, {"Chroma Blue","Cb",64,960}, {"Chroma Red","Cr",64,960} };
static const PixelDesc::CompSemantic ycbcrSem12[] = { {"Luma","Y",256,3760}, {"Chroma Blue","Cb",256,3840}, {"Chroma Red","Cr",256,3840} };

static PixelDesc::Data makeYCbCrDesc(PixelDesc::ID id, const char *name, const char *desc,
                                     PixelFormat::ID pfId, const PixelDesc::CompSemantic *sem) {
        PixelDesc::Data d;
        d.id              = id;
        d.name            = name;
        d.desc            = desc;
        d.pixelFormat     = PixelFormat(pfId);
        d.colorModel      = ColorModel(ColorModel::YCbCr_Rec709);
        d.compSemantics[0] = sem[0];
        d.compSemantics[1] = sem[1];
        d.compSemantics[2] = sem[2];
        return d;
}

static PixelDesc::Data makeYUV8_422_Planar() {
        auto d = makeYCbCrDesc(PixelDesc::YUV8_422_Planar_Rec709_Limited,
                "YUV8_422_Planar_Rec709_Limited", "8-bit YCbCr 4:2:2 planar, Rec.709, limited range",
                PixelFormat::Planar_422_3x8, ycbcrSem8);
        d.fourccList = { "I422" };
        return d;
}

static PixelDesc::Data makeYUV10_422_Planar_LE() {
        return makeYCbCrDesc(PixelDesc::YUV10_422_Planar_LE_Rec709_Limited,
                "YUV10_422_Planar_LE_Rec709_Limited", "10-bit YCbCr 4:2:2 planar LE, Rec.709, limited range",
                PixelFormat::Planar_422_3x10_LE, ycbcrSem10);
}

static PixelDesc::Data makeYUV10_422_Planar_BE() {
        return makeYCbCrDesc(PixelDesc::YUV10_422_Planar_BE_Rec709_Limited,
                "YUV10_422_Planar_BE_Rec709_Limited", "10-bit YCbCr 4:2:2 planar BE, Rec.709, limited range",
                PixelFormat::Planar_422_3x10_BE, ycbcrSem10);
}

static PixelDesc::Data makeYUV12_422_Planar_LE() {
        return makeYCbCrDesc(PixelDesc::YUV12_422_Planar_LE_Rec709_Limited,
                "YUV12_422_Planar_LE_Rec709_Limited", "12-bit YCbCr 4:2:2 planar LE, Rec.709, limited range",
                PixelFormat::Planar_422_3x12_LE, ycbcrSem12);
}

static PixelDesc::Data makeYUV12_422_Planar_BE() {
        return makeYCbCrDesc(PixelDesc::YUV12_422_Planar_BE_Rec709_Limited,
                "YUV12_422_Planar_BE_Rec709_Limited", "12-bit YCbCr 4:2:2 planar BE, Rec.709, limited range",
                PixelFormat::Planar_422_3x12_BE, ycbcrSem12);
}

// ---------------------------------------------------------------------------
// Planar 4:2:0 PixelDesc factory functions
// ---------------------------------------------------------------------------

static PixelDesc::Data makeYUV8_420_Planar() {
        auto d = makeYCbCrDesc(PixelDesc::YUV8_420_Planar_Rec709_Limited,
                "YUV8_420_Planar_Rec709_Limited", "8-bit YCbCr 4:2:0 planar, Rec.709, limited range",
                PixelFormat::Planar_420_3x8, ycbcrSem8);
        d.fourccList = { "I420" };
        return d;
}

static PixelDesc::Data makeYUV10_420_Planar_LE() {
        return makeYCbCrDesc(PixelDesc::YUV10_420_Planar_LE_Rec709_Limited,
                "YUV10_420_Planar_LE_Rec709_Limited", "10-bit YCbCr 4:2:0 planar LE, Rec.709, limited range",
                PixelFormat::Planar_420_3x10_LE, ycbcrSem10);
}

static PixelDesc::Data makeYUV10_420_Planar_BE() {
        return makeYCbCrDesc(PixelDesc::YUV10_420_Planar_BE_Rec709_Limited,
                "YUV10_420_Planar_BE_Rec709_Limited", "10-bit YCbCr 4:2:0 planar BE, Rec.709, limited range",
                PixelFormat::Planar_420_3x10_BE, ycbcrSem10);
}

static PixelDesc::Data makeYUV12_420_Planar_LE() {
        return makeYCbCrDesc(PixelDesc::YUV12_420_Planar_LE_Rec709_Limited,
                "YUV12_420_Planar_LE_Rec709_Limited", "12-bit YCbCr 4:2:0 planar LE, Rec.709, limited range",
                PixelFormat::Planar_420_3x12_LE, ycbcrSem12);
}

static PixelDesc::Data makeYUV12_420_Planar_BE() {
        return makeYCbCrDesc(PixelDesc::YUV12_420_Planar_BE_Rec709_Limited,
                "YUV12_420_Planar_BE_Rec709_Limited", "12-bit YCbCr 4:2:0 planar BE, Rec.709, limited range",
                PixelFormat::Planar_420_3x12_BE, ycbcrSem12);
}

// ---------------------------------------------------------------------------
// Semi-planar 4:2:0 (NV12) PixelDesc factory functions
// ---------------------------------------------------------------------------

static PixelDesc::Data makeYUV8_420_SemiPlanar() {
        auto d = makeYCbCrDesc(PixelDesc::YUV8_420_SemiPlanar_Rec709_Limited,
                "YUV8_420_SemiPlanar_Rec709_Limited", "8-bit YCbCr 4:2:0 NV12, Rec.709, limited range",
                PixelFormat::SemiPlanar_420_8, ycbcrSem8);
        d.fourccList = { "NV12" };
        return d;
}

static PixelDesc::Data makeYUV10_420_SemiPlanar_LE() {
        return makeYCbCrDesc(PixelDesc::YUV10_420_SemiPlanar_LE_Rec709_Limited,
                "YUV10_420_SemiPlanar_LE_Rec709_Limited", "10-bit YCbCr 4:2:0 NV12 LE, Rec.709, limited range",
                PixelFormat::SemiPlanar_420_10_LE, ycbcrSem10);
}

static PixelDesc::Data makeYUV10_420_SemiPlanar_BE() {
        return makeYCbCrDesc(PixelDesc::YUV10_420_SemiPlanar_BE_Rec709_Limited,
                "YUV10_420_SemiPlanar_BE_Rec709_Limited", "10-bit YCbCr 4:2:0 NV12 BE, Rec.709, limited range",
                PixelFormat::SemiPlanar_420_10_BE, ycbcrSem10);
}

static PixelDesc::Data makeYUV12_420_SemiPlanar_LE() {
        return makeYCbCrDesc(PixelDesc::YUV12_420_SemiPlanar_LE_Rec709_Limited,
                "YUV12_420_SemiPlanar_LE_Rec709_Limited", "12-bit YCbCr 4:2:0 NV12 LE, Rec.709, limited range",
                PixelFormat::SemiPlanar_420_12_LE, ycbcrSem12);
}

static PixelDesc::Data makeYUV12_420_SemiPlanar_BE() {
        return makeYCbCrDesc(PixelDesc::YUV12_420_SemiPlanar_BE_Rec709_Limited,
                "YUV12_420_SemiPlanar_BE_Rec709_Limited", "12-bit YCbCr 4:2:0 NV12 BE, Rec.709, limited range",
                PixelFormat::SemiPlanar_420_12_BE, ycbcrSem12);
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
                add(makeJPEG_YUV8_420());
                add(makeYUV8_422_UYVY());
                add(makeYUV10_422_UYVY_LE());
                add(makeYUV10_422_UYVY_BE());
                add(makeYUV12_422_UYVY_LE());
                add(makeYUV12_422_UYVY_BE());
                add(makeYUV10_422_v210());
                add(makeYUV8_422_Planar());
                add(makeYUV10_422_Planar_LE());
                add(makeYUV10_422_Planar_BE());
                add(makeYUV12_422_Planar_LE());
                add(makeYUV12_422_Planar_BE());
                add(makeYUV8_420_Planar());
                add(makeYUV10_420_Planar_LE());
                add(makeYUV10_420_Planar_BE());
                add(makeYUV12_420_Planar_LE());
                add(makeYUV12_420_Planar_BE());
                add(makeYUV8_420_SemiPlanar());
                add(makeYUV10_420_SemiPlanar_LE());
                add(makeYUV10_420_SemiPlanar_BE());
                add(makeYUV12_420_SemiPlanar_LE());
                add(makeYUV12_420_SemiPlanar_BE());
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

// ---------------------------------------------------------------------------
// Paint engine factory declarations from implementation files
// ---------------------------------------------------------------------------

PaintEngine createPaintEngine_RGBA8(const PixelDesc::Data *d, const Image &img);
PaintEngine createPaintEngine_RGB8(const PixelDesc::Data *d, const Image &img);

// ---------------------------------------------------------------------------
// Register paint engine factories with PixelDesc entries.
// ---------------------------------------------------------------------------

static struct PixelDescPaintEngineInit {
        PixelDescPaintEngineInit() {
                auto patch = [](PixelDesc::ID id, PaintEngine (*func)(const PixelDesc::Data *, const Image &)) {
                        PixelDesc pd(id);
                        if(!pd.isValid()) return;
                        PixelDesc::Data d = *pd.data();
                        d.createPaintEngineFunc = func;
                        PixelDesc::registerData(std::move(d));
                };
                patch(PixelDesc::RGBA8_sRGB_Full, createPaintEngine_RGBA8);
                patch(PixelDesc::RGB8_sRGB_Full, createPaintEngine_RGB8);
        }
} __pixelDescPaintEngineInit;

// ---------------------------------------------------------------------------
// Methods that depend on ImageDesc and related types
// ---------------------------------------------------------------------------

size_t PixelDesc::lineStride(size_t planeIndex, const ImageDesc &desc) const {
        if(d->compressed) return 0;
        return d->pixelFormat.lineStride(planeIndex, desc.width(), desc.linePad(), desc.lineAlign());
}

size_t PixelDesc::planeSize(size_t planeIndex, const ImageDesc &desc) const {
        if(d->compressed) {
                if(!desc.metadata().contains(Metadata::CompressedSize)) return 0;
                return desc.metadata().get(Metadata::CompressedSize).get<size_t>();
        }
        return d->pixelFormat.planeSize(planeIndex, desc.width(), desc.height(),
                                        desc.linePad(), desc.lineAlign());
}

PaintEngine PixelDesc::createPaintEngine(const Image &img) const {
        if(d->createPaintEngineFunc == nullptr) return PaintEngine();
        return d->createPaintEngineFunc(d, img);
}

PROMEKI_NAMESPACE_END
