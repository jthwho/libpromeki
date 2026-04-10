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
        d.id                    = PixelDesc::RGBA8_sRGB;
        d.name                  = "RGBA8_sRGB";
        d.desc                  = "8-bit RGBA, sRGB, full range";
        d.pixelFormat           = PixelFormat(PixelFormat::I_4x8);
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
        d.id                    = PixelDesc::RGB8_sRGB;
        d.name                  = "RGB8_sRGB";
        d.desc                  = "8-bit RGB, sRGB, full range";
        d.pixelFormat           = PixelFormat(PixelFormat::I_3x8);
        d.colorModel            = ColorModel(ColorModel::sRGB);
        // "raw " — QuickTime canonical FourCC for packed 24-bit.
        //          FIXME: players disagree on byte order. The historical
        //          QuickTime spec says 'raw ' + depth 24 is B,G,R, but
        //          ffmpeg/VLC/our reader treat it as R,G,B (matching what
        //          ffmpeg's rawvideo encoder emits). mplayer follows the
        //          spec and swaps red/blue on our files. See
        //          devplan/fixme.md → "QuickTime: 'raw ' Codec Byte Order".
        // "RGB2" — historical/AVI alias for the same byte layout.
        d.fourccList            = { "raw ", "RGB2" };
        d.compSemantics[0]      = { "Red",   "R", 0, 255 };
        d.compSemantics[1]      = { "Green", "G", 0, 255 };
        d.compSemantics[2]      = { "Blue",  "B", 0, 255 };
        return d;
}

static PixelDesc::Data makeRGB10() {
        PixelDesc::Data d;
        d.id                    = PixelDesc::RGB10_DPX_sRGB;
        d.name                  = "RGB10_DPX_sRGB";
        d.desc                  = "10-bit RGB, sRGB, full range";
        d.pixelFormat           = PixelFormat(PixelFormat::I_3x10_DPX);
        d.colorModel            = ColorModel(ColorModel::sRGB);
        d.compSemantics[0]      = { "Red",   "R", 0, 1023 };
        d.compSemantics[1]      = { "Green", "G", 0, 1023 };
        d.compSemantics[2]      = { "Blue",  "B", 0, 1023 };
        return d;
}

static PixelDesc::Data makeYUV8_422() {
        PixelDesc::Data d;
        d.id                    = PixelDesc::YUV8_422_Rec709;
        d.name                  = "YUV8_422_Rec709";
        d.desc                  = "8-bit YCbCr 4:2:2 YUYV, Rec.709, limited range";
        d.pixelFormat           = PixelFormat(PixelFormat::I_422_3x8);
        d.colorModel            = ColorModel(ColorModel::YCbCr_Rec709);
        d.fourccList            = { "YUY2", "YUYV" };
        d.compSemantics[0]      = { "Luma",           "Y",  16, 235 };
        d.compSemantics[1]      = { "Chroma Blue",    "Cb", 16, 240 };
        d.compSemantics[2]      = { "Chroma Red",     "Cr", 16, 240 };
        return d;
}

static PixelDesc::Data makeYUV10_422() {
        PixelDesc::Data d;
        d.id                    = PixelDesc::YUV10_422_Rec709;
        d.name                  = "YUV10_422_Rec709";
        d.desc                  = "10-bit YCbCr 4:2:2, Rec.709, limited range";
        d.pixelFormat           = PixelFormat(PixelFormat::I_422_3x10);
        d.colorModel            = ColorModel(ColorModel::YCbCr_Rec709);
        d.compSemantics[0]      = { "Luma",           "Y",  64,  940 };
        d.compSemantics[1]      = { "Chroma Blue",    "Cb", 64,  960 };
        d.compSemantics[2]      = { "Chroma Red",     "Cr", 64,  960 };
        return d;
}

static PixelDesc::Data makeJPEG_RGBA8() {
        PixelDesc::Data d;
        d.id                        = PixelDesc::JPEG_RGBA8_sRGB;
        d.name                      = "JPEG_RGBA8_sRGB";
        d.desc                      = "JPEG-compressed 8-bit RGBA";
        d.pixelFormat               = PixelFormat(PixelFormat::I_4x8);
        d.colorModel                = ColorModel(ColorModel::sRGB);
        d.hasAlpha                  = true;
        d.alphaCompIndex            = 3;
        d.compressed                = true;
        d.codecName                 = "jpeg";
        d.encodeSources             = { PixelDesc::RGBA8_sRGB };
        d.decodeTargets             = { PixelDesc::RGBA8_sRGB };
        d.fourccList                = { "jpeg", "mjpa", "mjpb", "mjpg", "AVRn", "AVDJ", "ADJV" };
        d.compSemantics[0]          = { "Red",   "R", 0, 255 };
        d.compSemantics[1]          = { "Green", "G", 0, 255 };
        d.compSemantics[2]          = { "Blue",  "B", 0, 255 };
        d.compSemantics[3]          = { "Alpha", "A", 0, 255 };
        return d;
}

static PixelDesc::Data makeJPEG_RGB8() {
        PixelDesc::Data d;
        d.id                        = PixelDesc::JPEG_RGB8_sRGB;
        d.name                      = "JPEG_RGB8_sRGB";
        d.desc                      = "JPEG-compressed 8-bit RGB";
        d.pixelFormat               = PixelFormat(PixelFormat::I_3x8);
        d.colorModel                = ColorModel(ColorModel::sRGB);
        d.compressed                = true;
        d.codecName                 = "jpeg";
        // Only formats from the natural RGB family belong here — see
        // JpegImageCodec::encode(), which tags the output based on the
        // input component order.  A different family (e.g. RGBA or YUV)
        // as input would produce a different JPEG sub-format and
        // contradict this PixelDesc's identity.
        d.encodeSources             = { PixelDesc::RGB8_sRGB };
        d.decodeTargets             = { PixelDesc::RGB8_sRGB };
        d.fourccList                = { "jpeg", "mjpa", "mjpb", "mjpg", "AVRn", "AVDJ", "ADJV" };
        d.compSemantics[0]          = { "Red",   "R", 0, 255 };
        d.compSemantics[1]          = { "Green", "G", 0, 255 };
        d.compSemantics[2]          = { "Blue",  "B", 0, 255 };
        return d;
}

// ---------------------------------------------------------------------------
// JPEG YCbCr factory helper
// ---------------------------------------------------------------------------
//
// Builds one entry from the full complement of JPEG YCbCr variants
// (matrix × range × subsampling).  Every variant has the same
// general shape — a compressed PixelDesc with the "jpeg" codec, a
// matching colour model, and encodeSources / decodeTargets lists
// drawn from the uncompressed YCbCr family that matches its own
// (matrix, range) pair — so a single helper keeps the eight
// definitions (2 subsampling × 2 matrix × 2 range) short and
// consistent.  The helper sets compSemantics and a default
// descriptor string automatically so all variants stay in sync.
struct JpegYuvEntry {
        PixelDesc::ID         id;
        const char           *name;
        PixelFormat::ID       pixelFormat;  // I_422_3x8 or P_420_3x8
        ColorModel::ID        colorModel;   // YCbCr_Rec709 or YCbCr_Rec601
        bool                  limited;      // true = [16..235]/[16..240], false = [0..255]
        bool                  is420;        // true = 4:2:0, false = 4:2:2
        List<PixelDesc::ID>   encodeSources;
        List<PixelDesc::ID>   decodeTargets;
};

static PixelDesc::Data makeJPEG_YUV(const JpegYuvEntry &e) {
        PixelDesc::Data d;
        d.id            = e.id;
        d.name          = e.name;
        // Build a short, consistent description string.
        const char *matrixName = (e.colorModel == ColorModel::YCbCr_Rec709)
                                ? "Rec.709" : "Rec.601";
        const char *rangeName  = e.limited ? "limited range" : "full range";
        const char *subsampling = e.is420 ? "4:2:0" : "4:2:2";
        d.desc = String("JPEG-compressed 8-bit YCbCr ") + String(subsampling) +
                 String(" (") + String(matrixName) + String(" matrix, ") +
                 String(rangeName) + String(")");
        d.pixelFormat   = PixelFormat(e.pixelFormat);
        d.colorModel    = ColorModel(e.colorModel);
        d.compressed    = true;
        d.codecName     = "jpeg";
        d.encodeSources = e.encodeSources;
        d.decodeTargets = e.decodeTargets;
        d.fourccList    = e.is420
                ? List<FourCC>{ "jpeg", "mjpg" }
                : List<FourCC>{ "jpeg", "mjpa", "mjpb", "mjpg", "AVRn", "AVDJ", "ADJV" };

        if(e.limited) {
                d.compSemantics[0] = { "Luma",        "Y",  16, 235 };
                d.compSemantics[1] = { "Chroma Blue", "Cb", 16, 240 };
                d.compSemantics[2] = { "Chroma Red",  "Cr", 16, 240 };
        } else {
                // Full range per JFIF convention.  libjpeg writes the
                // input bytes verbatim into the JPEG bitstream, so the
                // comp semantics must match the byte range the CSC
                // pipeline produces on the input side.  A JFIF-assuming
                // decoder (ffplay, browsers, libjpeg-turbo) interprets
                // the decoded bytes as full range regardless — using
                // limited range here would make black (Y=16) display as
                // dark grey on the receiver.
                d.compSemantics[0] = { "Luma",        "Y",  0, 255 };
                d.compSemantics[1] = { "Chroma Blue", "Cb", 0, 255 };
                d.compSemantics[2] = { "Chroma Red",  "Cr", 0, 255 };
        }
        return d;
}

// -- Rec.709 limited (the default YCbCr convention — existing IDs) --

static PixelDesc::Data makeJPEG_YUV8_422_Rec709() {
        return makeJPEG_YUV({
                PixelDesc::JPEG_YUV8_422_Rec709,
                "JPEG_YUV8_422_Rec709",
                PixelFormat::I_422_3x8, ColorModel::YCbCr_Rec709,
                /*limited*/ true, /*is420*/ false,
                { PixelDesc::YUV8_422_Rec709,
                  PixelDesc::YUV8_422_UYVY_Rec709,
                  PixelDesc::YUV8_422_Planar_Rec709 },
                { PixelDesc::YUV8_422_Rec709,
                  PixelDesc::YUV8_422_UYVY_Rec709,
                  PixelDesc::YUV8_422_Planar_Rec709,
                  PixelDesc::RGB8_sRGB,
                  PixelDesc::RGBA8_sRGB }
        });
}

static PixelDesc::Data makeJPEG_YUV8_420_Rec709() {
        return makeJPEG_YUV({
                PixelDesc::JPEG_YUV8_420_Rec709,
                "JPEG_YUV8_420_Rec709",
                PixelFormat::P_420_3x8, ColorModel::YCbCr_Rec709,
                /*limited*/ true, /*is420*/ true,
                { PixelDesc::YUV8_420_Planar_Rec709,
                  PixelDesc::YUV8_420_SemiPlanar_Rec709 },
                { PixelDesc::YUV8_420_Planar_Rec709,
                  PixelDesc::YUV8_420_SemiPlanar_Rec709,
                  PixelDesc::YUV8_422_UYVY_Rec709,
                  PixelDesc::YUV8_422_Rec709,
                  PixelDesc::RGB8_sRGB,
                  PixelDesc::RGBA8_sRGB }
        });
}

// -- Rec.601 limited --

static PixelDesc::Data makeJPEG_YUV8_422_Rec601() {
        return makeJPEG_YUV({
                PixelDesc::JPEG_YUV8_422_Rec601,
                "JPEG_YUV8_422_Rec601",
                PixelFormat::I_422_3x8, ColorModel::YCbCr_Rec601,
                /*limited*/ true, /*is420*/ false,
                { PixelDesc::YUV8_422_Rec601,
                  PixelDesc::YUV8_422_UYVY_Rec601 },
                { PixelDesc::YUV8_422_Rec601,
                  PixelDesc::YUV8_422_UYVY_Rec601,
                  PixelDesc::RGB8_sRGB,
                  PixelDesc::RGBA8_sRGB }
        });
}

static PixelDesc::Data makeJPEG_YUV8_420_Rec601() {
        return makeJPEG_YUV({
                PixelDesc::JPEG_YUV8_420_Rec601,
                "JPEG_YUV8_420_Rec601",
                PixelFormat::P_420_3x8, ColorModel::YCbCr_Rec601,
                /*limited*/ true, /*is420*/ true,
                { PixelDesc::YUV8_420_Planar_Rec601,
                  PixelDesc::YUV8_420_SemiPlanar_Rec601 },
                { PixelDesc::YUV8_420_Planar_Rec601,
                  PixelDesc::YUV8_420_SemiPlanar_Rec601,
                  PixelDesc::RGB8_sRGB,
                  PixelDesc::RGBA8_sRGB }
        });
}

// -- Rec.709 full (encode sources are the new full-range uncompressed intermediates) --

static PixelDesc::Data makeJPEG_YUV8_422_Rec709_Full() {
        return makeJPEG_YUV({
                PixelDesc::JPEG_YUV8_422_Rec709_Full,
                "JPEG_YUV8_422_Rec709_Full",
                PixelFormat::I_422_3x8, ColorModel::YCbCr_Rec709,
                /*limited*/ false, /*is420*/ false,
                { PixelDesc::YUV8_422_Rec709_Full },
                { PixelDesc::YUV8_422_Rec709_Full,
                  PixelDesc::RGB8_sRGB,
                  PixelDesc::RGBA8_sRGB }
        });
}

static PixelDesc::Data makeJPEG_YUV8_420_Rec709_Full() {
        return makeJPEG_YUV({
                PixelDesc::JPEG_YUV8_420_Rec709_Full,
                "JPEG_YUV8_420_Rec709_Full",
                PixelFormat::P_420_3x8, ColorModel::YCbCr_Rec709,
                /*limited*/ false, /*is420*/ true,
                { PixelDesc::YUV8_420_Planar_Rec709_Full },
                { PixelDesc::YUV8_420_Planar_Rec709_Full,
                  PixelDesc::RGB8_sRGB,
                  PixelDesc::RGBA8_sRGB }
        });
}

// -- Rec.601 full (the strict JFIF-compatible variants) --

static PixelDesc::Data makeJPEG_YUV8_422_Rec601_Full() {
        return makeJPEG_YUV({
                PixelDesc::JPEG_YUV8_422_Rec601_Full,
                "JPEG_YUV8_422_Rec601_Full",
                PixelFormat::I_422_3x8, ColorModel::YCbCr_Rec601,
                /*limited*/ false, /*is420*/ false,
                { PixelDesc::YUV8_422_Rec601_Full },
                { PixelDesc::YUV8_422_Rec601_Full,
                  PixelDesc::RGB8_sRGB,
                  PixelDesc::RGBA8_sRGB }
        });
}

static PixelDesc::Data makeJPEG_YUV8_420_Rec601_Full() {
        return makeJPEG_YUV({
                PixelDesc::JPEG_YUV8_420_Rec601_Full,
                "JPEG_YUV8_420_Rec601_Full",
                PixelFormat::P_420_3x8, ColorModel::YCbCr_Rec601,
                /*limited*/ false, /*is420*/ true,
                { PixelDesc::YUV8_420_Planar_Rec601_Full },
                { PixelDesc::YUV8_420_Planar_Rec601_Full,
                  PixelDesc::RGB8_sRGB,
                  PixelDesc::RGBA8_sRGB }
        });
}

// ---------------------------------------------------------------------------
// JPEG XS YCbCr factory helper
// ---------------------------------------------------------------------------
//
// Builds one entry from the JPEG XS YCbCr family.  Unlike the JPEG
// variants above, matrix / range are carried out-of-band (RFC 9134
// SDP for RTP, ISO/IEC 21122-3 sample entry for MP4) and not written
// into the JPEG XS codestream itself, so only bit depth and
// subsampling vary.  All entries are Rec.709 limited range — the
// canonical broadcast default that matches what ST 2110 JPEG XS
// carriage expects.  Bit depth drives the pixelFormat choice
// (P_XXX_3x8 for 8-bit, P_XXX_3x10_LE for 10-bit, etc.) and the
// CompSemantic ranges.
struct JpegXsYuvEntry {
        PixelDesc::ID         id;
        const char           *name;
        PixelFormat::ID       pixelFormat;  // P_422_3x8 / P_422_3x10_LE / ... / P_420_*
        int                   bitDepth;     // 8, 10, or 12
        bool                  is420;
        List<PixelDesc::ID>   encodeSources;
        List<PixelDesc::ID>   decodeTargets;
};

static PixelDesc::Data makeJPEG_XS_YUV(const JpegXsYuvEntry &e) {
        PixelDesc::Data d;
        d.id            = e.id;
        d.name          = e.name;
        const char *subsampling = e.is420 ? "4:2:0" : "4:2:2";
        d.desc = String("JPEG XS-compressed ") +
                 String::number(e.bitDepth) + String("-bit YCbCr ") +
                 String(subsampling) + String(" (Rec.709, limited range)");
        d.pixelFormat   = PixelFormat(e.pixelFormat);
        d.colorModel    = ColorModel(ColorModel::YCbCr_Rec709);
        d.compressed    = true;
        d.codecName     = "jpegxs";
        d.encodeSources = e.encodeSources;
        d.decodeTargets = e.decodeTargets;
        // JPEG XS ISOBMFF sample entry is "jxsm" (ISO/IEC 21122-3).
        d.fourccList    = { "jxsm" };
        // Component ranges scale with bit depth, matching the other
        // limited-range YCbCr descriptors in this file.
        switch(e.bitDepth) {
                case 8:
                        d.compSemantics[0] = { "Luma",        "Y",  16,  235  };
                        d.compSemantics[1] = { "Chroma Blue", "Cb", 16,  240  };
                        d.compSemantics[2] = { "Chroma Red",  "Cr", 16,  240  };
                        break;
                case 10:
                        d.compSemantics[0] = { "Luma",        "Y",  64,  940  };
                        d.compSemantics[1] = { "Chroma Blue", "Cb", 64,  960  };
                        d.compSemantics[2] = { "Chroma Red",  "Cr", 64,  960  };
                        break;
                case 12:
                        d.compSemantics[0] = { "Luma",        "Y",  256, 3760 };
                        d.compSemantics[1] = { "Chroma Blue", "Cb", 256, 3840 };
                        d.compSemantics[2] = { "Chroma Red",  "Cr", 256, 3840 };
                        break;
        }
        return d;
}

static PixelDesc::Data makeJPEG_XS_YUV8_422_Rec709() {
        return makeJPEG_XS_YUV({
                PixelDesc::JPEG_XS_YUV8_422_Rec709,
                "JPEG_XS_YUV8_422_Rec709",
                PixelFormat::P_422_3x8, 8, /*is420*/ false,
                { PixelDesc::YUV8_422_Planar_Rec709 },
                { PixelDesc::YUV8_422_Planar_Rec709,
                  PixelDesc::RGB8_sRGB }
        });
}

static PixelDesc::Data makeJPEG_XS_YUV10_422_Rec709() {
        return makeJPEG_XS_YUV({
                PixelDesc::JPEG_XS_YUV10_422_Rec709,
                "JPEG_XS_YUV10_422_Rec709",
                PixelFormat::P_422_3x10_LE, 10, /*is420*/ false,
                { PixelDesc::YUV10_422_Planar_LE_Rec709 },
                { PixelDesc::YUV10_422_Planar_LE_Rec709 }
        });
}

static PixelDesc::Data makeJPEG_XS_YUV12_422_Rec709() {
        return makeJPEG_XS_YUV({
                PixelDesc::JPEG_XS_YUV12_422_Rec709,
                "JPEG_XS_YUV12_422_Rec709",
                PixelFormat::P_422_3x12_LE, 12, /*is420*/ false,
                { PixelDesc::YUV12_422_Planar_LE_Rec709 },
                { PixelDesc::YUV12_422_Planar_LE_Rec709 }
        });
}

static PixelDesc::Data makeJPEG_XS_YUV8_420_Rec709() {
        return makeJPEG_XS_YUV({
                PixelDesc::JPEG_XS_YUV8_420_Rec709,
                "JPEG_XS_YUV8_420_Rec709",
                PixelFormat::P_420_3x8, 8, /*is420*/ true,
                { PixelDesc::YUV8_420_Planar_Rec709 },
                { PixelDesc::YUV8_420_Planar_Rec709,
                  PixelDesc::RGB8_sRGB }
        });
}

static PixelDesc::Data makeJPEG_XS_YUV10_420_Rec709() {
        return makeJPEG_XS_YUV({
                PixelDesc::JPEG_XS_YUV10_420_Rec709,
                "JPEG_XS_YUV10_420_Rec709",
                PixelFormat::P_420_3x10_LE, 10, /*is420*/ true,
                { PixelDesc::YUV10_420_Planar_LE_Rec709 },
                { PixelDesc::YUV10_420_Planar_LE_Rec709 }
        });
}

static PixelDesc::Data makeJPEG_XS_YUV12_420_Rec709() {
        return makeJPEG_XS_YUV({
                PixelDesc::JPEG_XS_YUV12_420_Rec709,
                "JPEG_XS_YUV12_420_Rec709",
                PixelFormat::P_420_3x12_LE, 12, /*is420*/ true,
                { PixelDesc::YUV12_420_Planar_LE_Rec709 },
                { PixelDesc::YUV12_420_Planar_LE_Rec709 }
        });
}

static PixelDesc::Data makeJPEG_XS_RGB8_sRGB() {
        PixelDesc::Data d;
        d.id            = PixelDesc::JPEG_XS_RGB8_sRGB;
        d.name          = "JPEG_XS_RGB8_sRGB";
        d.desc          = "JPEG XS-compressed 8-bit RGB, sRGB, full range";
        d.pixelFormat   = PixelFormat(PixelFormat::I_3x8);
        d.colorModel    = ColorModel(ColorModel::sRGB);
        d.compressed    = true;
        d.codecName     = "jpegxs";
        d.encodeSources = { PixelDesc::RGB8_sRGB };
        d.decodeTargets = { PixelDesc::RGB8_sRGB };
        d.fourccList    = { "jxsm" };
        d.compSemantics[0] = { "Red",   "R", 0, 255 };
        d.compSemantics[1] = { "Green", "G", 0, 255 };
        d.compSemantics[2] = { "Blue",  "B", 0, 255 };
        return d;
}

// ---------------------------------------------------------------------------
// Legacy single-entry wrappers used by the registry init block
// ---------------------------------------------------------------------------

static PixelDesc::Data makeJPEG_YUV8_422() {
        return makeJPEG_YUV8_422_Rec709();
}

static PixelDesc::Data makeJPEG_YUV8_420() {
        return makeJPEG_YUV8_420_Rec709();
}

static PixelDesc::Data makeYUV8_422_UYVY() {
        PixelDesc::Data d;
        d.id                    = PixelDesc::YUV8_422_UYVY_Rec709;
        d.name                  = "YUV8_422_UYVY_Rec709";
        d.desc                  = "8-bit YCbCr 4:2:2 UYVY, Rec.709, limited range";
        d.pixelFormat           = PixelFormat(PixelFormat::I_422_UYVY_3x8);
        d.colorModel            = ColorModel(ColorModel::YCbCr_Rec709);
        // "2vuy" — QuickTime sample-entry FourCC for 8-bit 4:2:2 UYVY (canonical).
        // "UYVY" — generic / AVI / FFmpeg name for the same byte layout.
        // The first entry is the writer-preferred FourCC; QuickTime files
        // written by us emit "2vuy".
        d.fourccList            = { "2vuy", "UYVY" };
        d.compSemantics[0]      = { "Luma",           "Y",  16, 235 };
        d.compSemantics[1]      = { "Chroma Blue",    "Cb", 16, 240 };
        d.compSemantics[2]      = { "Chroma Red",     "Cr", 16, 240 };
        return d;
}

static PixelDesc::Data makeYUV10_422_UYVY_LE() {
        PixelDesc::Data d;
        d.id                    = PixelDesc::YUV10_422_UYVY_LE_Rec709;
        d.name                  = "YUV10_422_UYVY_LE_Rec709";
        d.desc                  = "10-bit YCbCr 4:2:2 UYVY LE, Rec.709, limited range";
        d.pixelFormat           = PixelFormat(PixelFormat::I_422_UYVY_3x10_LE);
        d.colorModel            = ColorModel(ColorModel::YCbCr_Rec709);
        d.compSemantics[0]      = { "Luma",           "Y",  64,  940 };
        d.compSemantics[1]      = { "Chroma Blue",    "Cb", 64,  960 };
        d.compSemantics[2]      = { "Chroma Red",     "Cr", 64,  960 };
        return d;
}

static PixelDesc::Data makeYUV10_422_UYVY_BE() {
        PixelDesc::Data d;
        d.id                    = PixelDesc::YUV10_422_UYVY_BE_Rec709;
        d.name                  = "YUV10_422_UYVY_BE_Rec709";
        d.desc                  = "10-bit YCbCr 4:2:2 UYVY BE, Rec.709, limited range";
        d.pixelFormat           = PixelFormat(PixelFormat::I_422_UYVY_3x10_BE);
        d.colorModel            = ColorModel(ColorModel::YCbCr_Rec709);
        d.compSemantics[0]      = { "Luma",           "Y",  64,  940 };
        d.compSemantics[1]      = { "Chroma Blue",    "Cb", 64,  960 };
        d.compSemantics[2]      = { "Chroma Red",     "Cr", 64,  960 };
        return d;
}

static PixelDesc::Data makeYUV12_422_UYVY_LE() {
        PixelDesc::Data d;
        d.id                    = PixelDesc::YUV12_422_UYVY_LE_Rec709;
        d.name                  = "YUV12_422_UYVY_LE_Rec709";
        d.desc                  = "12-bit YCbCr 4:2:2 UYVY LE, Rec.709, limited range";
        d.pixelFormat           = PixelFormat(PixelFormat::I_422_UYVY_3x12_LE);
        d.colorModel            = ColorModel(ColorModel::YCbCr_Rec709);
        d.compSemantics[0]      = { "Luma",           "Y",  256, 3760 };
        d.compSemantics[1]      = { "Chroma Blue",    "Cb", 256, 3840 };
        d.compSemantics[2]      = { "Chroma Red",     "Cr", 256, 3840 };
        return d;
}

static PixelDesc::Data makeYUV12_422_UYVY_BE() {
        PixelDesc::Data d;
        d.id                    = PixelDesc::YUV12_422_UYVY_BE_Rec709;
        d.name                  = "YUV12_422_UYVY_BE_Rec709";
        d.desc                  = "12-bit YCbCr 4:2:2 UYVY BE, Rec.709, limited range";
        d.pixelFormat           = PixelFormat(PixelFormat::I_422_UYVY_3x12_BE);
        d.colorModel            = ColorModel(ColorModel::YCbCr_Rec709);
        d.compSemantics[0]      = { "Luma",           "Y",  256, 3760 };
        d.compSemantics[1]      = { "Chroma Blue",    "Cb", 256, 3840 };
        d.compSemantics[2]      = { "Chroma Red",     "Cr", 256, 3840 };
        return d;
}

static PixelDesc::Data makeYUV10_422_v210() {
        PixelDesc::Data d;
        d.id                    = PixelDesc::YUV10_422_v210_Rec709;
        d.name                  = "YUV10_422_v210_Rec709";
        d.desc                  = "10-bit YCbCr 4:2:2 v210 packed, Rec.709, limited range";
        d.pixelFormat           = PixelFormat(PixelFormat::I_422_v210);
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
static const PixelDesc::CompSemantic ycbcrSem16[] = { {"Luma","Y",4096,60160}, {"Chroma Blue","Cb",4096,61440}, {"Chroma Red","Cr",4096,61440} };

// Full-range (0..255 / 0..1023 / 0..4095 / 0..65535) YCbCr comp
// semantics.  The library-wide YCbCr default is limited range (the
// unsuffixed sem arrays above) because broadcast / SDI / ST 2110
// pipelines are overwhelmingly limited-range.  These "_Full"
// variants are the explicit full-range opt-in, used by JPEG / JFIF
// interop paths and by any future pipeline that needs a full-range
// YCbCr intermediate (e.g. a CSC fast-path into a JFIF-compatible
// JPEG encode).
static const PixelDesc::CompSemantic ycbcrSem8Full[]  = { {"Luma","Y",0,255}, {"Chroma Blue","Cb",0,255}, {"Chroma Red","Cr",0,255} };

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

static PixelDesc::Data makeYCbCrDescWithModel(PixelDesc::ID id, const char *name, const char *desc,
                                              PixelFormat::ID pfId, const PixelDesc::CompSemantic *sem,
                                              ColorModel::ID colorModelId) {
        PixelDesc::Data d;
        d.id              = id;
        d.name            = name;
        d.desc            = desc;
        d.pixelFormat     = PixelFormat(pfId);
        d.colorModel      = ColorModel(colorModelId);
        d.compSemantics[0] = sem[0];
        d.compSemantics[1] = sem[1];
        d.compSemantics[2] = sem[2];
        return d;
}

// -- Full-range uncompressed YCbCr 4:2:2 / 4:2:0 --
//
// The default YCbCr variants are limited range per the library
// convention; these "_Full" entries are the explicit full-range
// opt-in.  They exist primarily as encode-source intermediates
// for the full-range JPEG PixelDescs so the CSC pipeline can land
// in a byte range that matches what the JPEG codec will write
// verbatim into the JFIF bitstream.  General pipeline code is
// also free to use them any time a full-range YCbCr
// representation is more appropriate than the broadcast default.

static PixelDesc::Data makeYUV8_422_Rec709_Full() {
        return makeYCbCrDescWithModel(PixelDesc::YUV8_422_Rec709_Full,
                "YUV8_422_Rec709_Full",
                "8-bit YCbCr 4:2:2 YUYV, Rec.709, full range",
                PixelFormat::I_422_3x8, ycbcrSem8Full,
                ColorModel::YCbCr_Rec709);
}

static PixelDesc::Data makeYUV8_422_Rec601_Full() {
        return makeYCbCrDescWithModel(PixelDesc::YUV8_422_Rec601_Full,
                "YUV8_422_Rec601_Full",
                "8-bit YCbCr 4:2:2 YUYV, Rec.601, full range",
                PixelFormat::I_422_3x8, ycbcrSem8Full,
                ColorModel::YCbCr_Rec601);
}

static PixelDesc::Data makeYUV8_420_Planar_Rec709_Full() {
        return makeYCbCrDescWithModel(PixelDesc::YUV8_420_Planar_Rec709_Full,
                "YUV8_420_Planar_Rec709_Full",
                "8-bit YCbCr 4:2:0 planar, Rec.709, full range",
                PixelFormat::P_420_3x8, ycbcrSem8Full,
                ColorModel::YCbCr_Rec709);
}

static PixelDesc::Data makeYUV8_420_Planar_Rec601_Full() {
        return makeYCbCrDescWithModel(PixelDesc::YUV8_420_Planar_Rec601_Full,
                "YUV8_420_Planar_Rec601_Full",
                "8-bit YCbCr 4:2:0 planar, Rec.601, full range",
                PixelFormat::P_420_3x8, ycbcrSem8Full,
                ColorModel::YCbCr_Rec601);
}

static PixelDesc::Data makeBGRDesc(PixelDesc::ID id, const char *name, const char *desc,
                                   PixelFormat::ID pfId, bool alpha, float rangeMax) {
        PixelDesc::Data d;
        d.id              = id;
        d.name            = name;
        d.desc            = desc;
        d.pixelFormat     = PixelFormat(pfId);
        d.colorModel      = ColorModel(ColorModel::sRGB);
        d.hasAlpha        = alpha;
        d.alphaCompIndex  = alpha ? 3 : -1;
        d.compSemantics[0] = { "Blue",  "B", 0, rangeMax };
        d.compSemantics[1] = { "Green", "G", 0, rangeMax };
        d.compSemantics[2] = { "Red",   "R", 0, rangeMax };
        if(alpha) d.compSemantics[3] = { "Alpha", "A", 0, rangeMax };
        return d;
}

static PixelDesc::Data makeARGBDesc(PixelDesc::ID id, const char *name, const char *desc,
                                    PixelFormat::ID pfId, float rangeMax) {
        PixelDesc::Data d;
        d.id              = id;
        d.name            = name;
        d.desc            = desc;
        d.pixelFormat     = PixelFormat(pfId);
        d.colorModel      = ColorModel(ColorModel::sRGB);
        d.hasAlpha        = true;
        d.alphaCompIndex  = 0;
        d.compSemantics[0] = { "Alpha", "A", 0, rangeMax };
        d.compSemantics[1] = { "Red",   "R", 0, rangeMax };
        d.compSemantics[2] = { "Green", "G", 0, rangeMax };
        d.compSemantics[3] = { "Blue",  "B", 0, rangeMax };
        return d;
}

static PixelDesc::Data makeABGRDesc(PixelDesc::ID id, const char *name, const char *desc,
                                    PixelFormat::ID pfId, float rangeMax) {
        PixelDesc::Data d;
        d.id              = id;
        d.name            = name;
        d.desc            = desc;
        d.pixelFormat     = PixelFormat(pfId);
        d.colorModel      = ColorModel(ColorModel::sRGB);
        d.hasAlpha        = true;
        d.alphaCompIndex  = 0;
        d.compSemantics[0] = { "Alpha", "A", 0, rangeMax };
        d.compSemantics[1] = { "Blue",  "B", 0, rangeMax };
        d.compSemantics[2] = { "Green", "G", 0, rangeMax };
        d.compSemantics[3] = { "Red",   "R", 0, rangeMax };
        return d;
}

static PixelDesc::Data makeMonoDesc(PixelDesc::ID id, const char *name, const char *desc,
                                    PixelFormat::ID pfId, ColorModel::ID colorModelId, float rangeMax) {
        PixelDesc::Data d;
        d.id              = id;
        d.name            = name;
        d.desc            = desc;
        d.pixelFormat     = PixelFormat(pfId);
        d.colorModel      = ColorModel(colorModelId);
        d.compSemantics[0] = { "Luminance", "L", 0, rangeMax };
        return d;
}

static PixelDesc::Data makeFloatRGBDesc(PixelDesc::ID id, const char *name, const char *desc,
                                        PixelFormat::ID pfId, bool alpha, ColorModel::ID colorModelId) {
        PixelDesc::Data d;
        d.id              = id;
        d.name            = name;
        d.desc            = desc;
        d.pixelFormat     = PixelFormat(pfId);
        d.colorModel      = ColorModel(colorModelId);
        d.hasAlpha        = alpha;
        d.alphaCompIndex  = alpha ? 3 : -1;
        d.compSemantics[0] = { "Red",   "R", 0.0, 1.0 };
        d.compSemantics[1] = { "Green", "G", 0.0, 1.0 };
        d.compSemantics[2] = { "Blue",  "B", 0.0, 1.0 };
        if(alpha) d.compSemantics[3] = { "Alpha", "A", 0.0, 1.0 };
        return d;
}

static PixelDesc::Data makeYUV8_422_Planar() {
        auto d = makeYCbCrDesc(PixelDesc::YUV8_422_Planar_Rec709,
                "YUV8_422_Planar_Rec709", "8-bit YCbCr 4:2:2 planar, Rec.709, limited range",
                PixelFormat::P_422_3x8, ycbcrSem8);
        d.fourccList = { "I422" };
        return d;
}

static PixelDesc::Data makeYUV10_422_Planar_LE() {
        return makeYCbCrDesc(PixelDesc::YUV10_422_Planar_LE_Rec709,
                "YUV10_422_Planar_LE_Rec709", "10-bit YCbCr 4:2:2 planar LE, Rec.709, limited range",
                PixelFormat::P_422_3x10_LE, ycbcrSem10);
}

static PixelDesc::Data makeYUV10_422_Planar_BE() {
        return makeYCbCrDesc(PixelDesc::YUV10_422_Planar_BE_Rec709,
                "YUV10_422_Planar_BE_Rec709", "10-bit YCbCr 4:2:2 planar BE, Rec.709, limited range",
                PixelFormat::P_422_3x10_BE, ycbcrSem10);
}

static PixelDesc::Data makeYUV12_422_Planar_LE() {
        return makeYCbCrDesc(PixelDesc::YUV12_422_Planar_LE_Rec709,
                "YUV12_422_Planar_LE_Rec709", "12-bit YCbCr 4:2:2 planar LE, Rec.709, limited range",
                PixelFormat::P_422_3x12_LE, ycbcrSem12);
}

static PixelDesc::Data makeYUV12_422_Planar_BE() {
        return makeYCbCrDesc(PixelDesc::YUV12_422_Planar_BE_Rec709,
                "YUV12_422_Planar_BE_Rec709", "12-bit YCbCr 4:2:2 planar BE, Rec.709, limited range",
                PixelFormat::P_422_3x12_BE, ycbcrSem12);
}

// ---------------------------------------------------------------------------
// Planar 4:2:0 PixelDesc factory functions
// ---------------------------------------------------------------------------

static PixelDesc::Data makeYUV8_420_Planar() {
        auto d = makeYCbCrDesc(PixelDesc::YUV8_420_Planar_Rec709,
                "YUV8_420_Planar_Rec709", "8-bit YCbCr 4:2:0 planar, Rec.709, limited range",
                PixelFormat::P_420_3x8, ycbcrSem8);
        d.fourccList = { "I420" };
        return d;
}

static PixelDesc::Data makeYUV10_420_Planar_LE() {
        return makeYCbCrDesc(PixelDesc::YUV10_420_Planar_LE_Rec709,
                "YUV10_420_Planar_LE_Rec709", "10-bit YCbCr 4:2:0 planar LE, Rec.709, limited range",
                PixelFormat::P_420_3x10_LE, ycbcrSem10);
}

static PixelDesc::Data makeYUV10_420_Planar_BE() {
        return makeYCbCrDesc(PixelDesc::YUV10_420_Planar_BE_Rec709,
                "YUV10_420_Planar_BE_Rec709", "10-bit YCbCr 4:2:0 planar BE, Rec.709, limited range",
                PixelFormat::P_420_3x10_BE, ycbcrSem10);
}

static PixelDesc::Data makeYUV12_420_Planar_LE() {
        return makeYCbCrDesc(PixelDesc::YUV12_420_Planar_LE_Rec709,
                "YUV12_420_Planar_LE_Rec709", "12-bit YCbCr 4:2:0 planar LE, Rec.709, limited range",
                PixelFormat::P_420_3x12_LE, ycbcrSem12);
}

static PixelDesc::Data makeYUV12_420_Planar_BE() {
        return makeYCbCrDesc(PixelDesc::YUV12_420_Planar_BE_Rec709,
                "YUV12_420_Planar_BE_Rec709", "12-bit YCbCr 4:2:0 planar BE, Rec.709, limited range",
                PixelFormat::P_420_3x12_BE, ycbcrSem12);
}

// ---------------------------------------------------------------------------
// Semi-planar 4:2:0 (NV12) PixelDesc factory functions
// ---------------------------------------------------------------------------

static PixelDesc::Data makeYUV8_420_SemiPlanar() {
        auto d = makeYCbCrDesc(PixelDesc::YUV8_420_SemiPlanar_Rec709,
                "YUV8_420_SemiPlanar_Rec709", "8-bit YCbCr 4:2:0 NV12, Rec.709, limited range",
                PixelFormat::SP_420_8, ycbcrSem8);
        d.fourccList = { "NV12" };
        return d;
}

static PixelDesc::Data makeYUV10_420_SemiPlanar_LE() {
        return makeYCbCrDesc(PixelDesc::YUV10_420_SemiPlanar_LE_Rec709,
                "YUV10_420_SemiPlanar_LE_Rec709", "10-bit YCbCr 4:2:0 NV12 LE, Rec.709, limited range",
                PixelFormat::SP_420_10_LE, ycbcrSem10);
}

static PixelDesc::Data makeYUV10_420_SemiPlanar_BE() {
        return makeYCbCrDesc(PixelDesc::YUV10_420_SemiPlanar_BE_Rec709,
                "YUV10_420_SemiPlanar_BE_Rec709", "10-bit YCbCr 4:2:0 NV12 BE, Rec.709, limited range",
                PixelFormat::SP_420_10_BE, ycbcrSem10);
}

static PixelDesc::Data makeYUV12_420_SemiPlanar_LE() {
        return makeYCbCrDesc(PixelDesc::YUV12_420_SemiPlanar_LE_Rec709,
                "YUV12_420_SemiPlanar_LE_Rec709", "12-bit YCbCr 4:2:0 NV12 LE, Rec.709, limited range",
                PixelFormat::SP_420_12_LE, ycbcrSem12);
}

static PixelDesc::Data makeYUV12_420_SemiPlanar_BE() {
        return makeYCbCrDesc(PixelDesc::YUV12_420_SemiPlanar_BE_Rec709,
                "YUV12_420_SemiPlanar_BE_Rec709", "12-bit YCbCr 4:2:0 NV12 BE, Rec.709, limited range",
                PixelFormat::SP_420_12_BE, ycbcrSem12);
}

// ---------------------------------------------------------------------------
// RGB/RGBA 10/12/16-bit PixelDesc factory functions
// ---------------------------------------------------------------------------

static PixelDesc::Data makeRGBDesc(PixelDesc::ID id, const char *name, const char *desc,
                                   PixelFormat::ID pfId, bool alpha, float rangeMax) {
        PixelDesc::Data d;
        d.id              = id;
        d.name            = name;
        d.desc            = desc;
        d.pixelFormat     = PixelFormat(pfId);
        d.colorModel      = ColorModel(ColorModel::sRGB);
        d.hasAlpha        = alpha;
        d.alphaCompIndex  = alpha ? 3 : -1;
        d.compSemantics[0] = { "Red",   "R", 0, rangeMax };
        d.compSemantics[1] = { "Green", "G", 0, rangeMax };
        d.compSemantics[2] = { "Blue",  "B", 0, rangeMax };
        if(alpha) d.compSemantics[3] = { "Alpha", "A", 0, rangeMax };
        return d;
}

static PixelDesc::Data makeRGBA10_LE() {
        return makeRGBDesc(PixelDesc::RGBA10_LE_sRGB,
                "RGBA10_LE_sRGB", "10-bit RGBA in 16-bit LE words, sRGB, full range",
                PixelFormat::I_4x10_LE, true, 1023);
}

static PixelDesc::Data makeRGBA10_BE() {
        return makeRGBDesc(PixelDesc::RGBA10_BE_sRGB,
                "RGBA10_BE_sRGB", "10-bit RGBA in 16-bit BE words, sRGB, full range",
                PixelFormat::I_4x10_BE, true, 1023);
}

static PixelDesc::Data makeRGB10_LE() {
        return makeRGBDesc(PixelDesc::RGB10_LE_sRGB,
                "RGB10_LE_sRGB", "10-bit RGB in 16-bit LE words, sRGB, full range",
                PixelFormat::I_3x10_LE, false, 1023);
}

static PixelDesc::Data makeRGB10_BE() {
        return makeRGBDesc(PixelDesc::RGB10_BE_sRGB,
                "RGB10_BE_sRGB", "10-bit RGB in 16-bit BE words, sRGB, full range",
                PixelFormat::I_3x10_BE, false, 1023);
}

static PixelDesc::Data makeRGBA12_LE() {
        return makeRGBDesc(PixelDesc::RGBA12_LE_sRGB,
                "RGBA12_LE_sRGB", "12-bit RGBA in 16-bit LE words, sRGB, full range",
                PixelFormat::I_4x12_LE, true, 4095);
}

static PixelDesc::Data makeRGBA12_BE() {
        return makeRGBDesc(PixelDesc::RGBA12_BE_sRGB,
                "RGBA12_BE_sRGB", "12-bit RGBA in 16-bit BE words, sRGB, full range",
                PixelFormat::I_4x12_BE, true, 4095);
}

static PixelDesc::Data makeRGB12_LE() {
        return makeRGBDesc(PixelDesc::RGB12_LE_sRGB,
                "RGB12_LE_sRGB", "12-bit RGB in 16-bit LE words, sRGB, full range",
                PixelFormat::I_3x12_LE, false, 4095);
}

static PixelDesc::Data makeRGB12_BE() {
        return makeRGBDesc(PixelDesc::RGB12_BE_sRGB,
                "RGB12_BE_sRGB", "12-bit RGB in 16-bit BE words, sRGB, full range",
                PixelFormat::I_3x12_BE, false, 4095);
}

static PixelDesc::Data makeRGBA16_LE() {
        return makeRGBDesc(PixelDesc::RGBA16_LE_sRGB,
                "RGBA16_LE_sRGB", "16-bit RGBA LE, sRGB, full range",
                PixelFormat::I_4x16_LE, true, 65535);
}

static PixelDesc::Data makeRGBA16_BE() {
        return makeRGBDesc(PixelDesc::RGBA16_BE_sRGB,
                "RGBA16_BE_sRGB", "16-bit RGBA BE, sRGB, full range",
                PixelFormat::I_4x16_BE, true, 65535);
}

static PixelDesc::Data makeRGB16_LE() {
        return makeRGBDesc(PixelDesc::RGB16_LE_sRGB,
                "RGB16_LE_sRGB", "16-bit RGB LE, sRGB, full range",
                PixelFormat::I_3x16_LE, false, 65535);
}

static PixelDesc::Data makeRGB16_BE() {
        return makeRGBDesc(PixelDesc::RGB16_BE_sRGB,
                "RGB16_BE_sRGB", "16-bit RGB BE, sRGB, full range",
                PixelFormat::I_3x16_BE, false, 65535);
}

// ---------------------------------------------------------------------------
// YCbCr 4:4:4 DPX packed PixelDesc factory function
// ---------------------------------------------------------------------------

static PixelDesc::Data makeYUV10_DPX() {
        return makeYCbCrDesc(PixelDesc::YUV10_DPX_Rec709,
                "YUV10_DPX_Rec709", "10-bit YCbCr 4:4:4 DPX packed, Rec.709, limited range",
                PixelFormat::I_3x10_DPX, ycbcrSem10);
}

// ---------------------------------------------------------------------------
// DPX additional packed PixelDesc factory functions
// ---------------------------------------------------------------------------

static PixelDesc::Data makeRGB10_DPX_LE() {
        PixelDesc::Data d;
        d.id                    = PixelDesc::RGB10_DPX_LE_sRGB;
        d.name                  = "RGB10_DPX_LE_sRGB";
        d.desc                  = "10-bit RGB, DPX packed LE, sRGB, full range";
        d.pixelFormat           = PixelFormat(PixelFormat::I_3x10_DPX);
        d.colorModel            = ColorModel(ColorModel::sRGB);
        d.compSemantics[0]      = { "Red",   "R", 0, 1023 };
        d.compSemantics[1]      = { "Green", "G", 0, 1023 };
        d.compSemantics[2]      = { "Blue",  "B", 0, 1023 };
        return d;
}

static PixelDesc::Data makeYUV10_DPX_B() {
        return makeYCbCrDesc(PixelDesc::YUV10_DPX_B_Rec709,
                "YUV10_DPX_B_Rec709", "10-bit YCbCr 4:4:4 DPX packed method B, Rec.709, limited range",
                PixelFormat::I_3x10_DPX, ycbcrSem10);
}

// ---------------------------------------------------------------------------
// BGRA/BGR PixelDesc factory functions
// ---------------------------------------------------------------------------

static PixelDesc::Data makeBGRA8() {
        return makeBGRDesc(PixelDesc::BGRA8_sRGB,
                "BGRA8_sRGB", "8-bit BGRA, sRGB, full range",
                PixelFormat::I_4x8, true, 255);
}

static PixelDesc::Data makeBGR8() {
        return makeBGRDesc(PixelDesc::BGR8_sRGB,
                "BGR8_sRGB", "8-bit BGR, sRGB, full range",
                PixelFormat::I_3x8, false, 255);
}

static PixelDesc::Data makeBGRA10_LE() {
        return makeBGRDesc(PixelDesc::BGRA10_LE_sRGB,
                "BGRA10_LE_sRGB", "10-bit BGRA in 16-bit LE words, sRGB, full range",
                PixelFormat::I_4x10_LE, true, 1023);
}

static PixelDesc::Data makeBGRA10_BE() {
        return makeBGRDesc(PixelDesc::BGRA10_BE_sRGB,
                "BGRA10_BE_sRGB", "10-bit BGRA in 16-bit BE words, sRGB, full range",
                PixelFormat::I_4x10_BE, true, 1023);
}

static PixelDesc::Data makeBGR10_LE() {
        return makeBGRDesc(PixelDesc::BGR10_LE_sRGB,
                "BGR10_LE_sRGB", "10-bit BGR in 16-bit LE words, sRGB, full range",
                PixelFormat::I_3x10_LE, false, 1023);
}

static PixelDesc::Data makeBGR10_BE() {
        return makeBGRDesc(PixelDesc::BGR10_BE_sRGB,
                "BGR10_BE_sRGB", "10-bit BGR in 16-bit BE words, sRGB, full range",
                PixelFormat::I_3x10_BE, false, 1023);
}

static PixelDesc::Data makeBGRA12_LE() {
        return makeBGRDesc(PixelDesc::BGRA12_LE_sRGB,
                "BGRA12_LE_sRGB", "12-bit BGRA in 16-bit LE words, sRGB, full range",
                PixelFormat::I_4x12_LE, true, 4095);
}

static PixelDesc::Data makeBGRA12_BE() {
        return makeBGRDesc(PixelDesc::BGRA12_BE_sRGB,
                "BGRA12_BE_sRGB", "12-bit BGRA in 16-bit BE words, sRGB, full range",
                PixelFormat::I_4x12_BE, true, 4095);
}

static PixelDesc::Data makeBGR12_LE() {
        return makeBGRDesc(PixelDesc::BGR12_LE_sRGB,
                "BGR12_LE_sRGB", "12-bit BGR in 16-bit LE words, sRGB, full range",
                PixelFormat::I_3x12_LE, false, 4095);
}

static PixelDesc::Data makeBGR12_BE() {
        return makeBGRDesc(PixelDesc::BGR12_BE_sRGB,
                "BGR12_BE_sRGB", "12-bit BGR in 16-bit BE words, sRGB, full range",
                PixelFormat::I_3x12_BE, false, 4095);
}

static PixelDesc::Data makeBGRA16_LE() {
        return makeBGRDesc(PixelDesc::BGRA16_LE_sRGB,
                "BGRA16_LE_sRGB", "16-bit BGRA LE, sRGB, full range",
                PixelFormat::I_4x16_LE, true, 65535);
}

static PixelDesc::Data makeBGRA16_BE() {
        return makeBGRDesc(PixelDesc::BGRA16_BE_sRGB,
                "BGRA16_BE_sRGB", "16-bit BGRA BE, sRGB, full range",
                PixelFormat::I_4x16_BE, true, 65535);
}

static PixelDesc::Data makeBGR16_LE() {
        return makeBGRDesc(PixelDesc::BGR16_LE_sRGB,
                "BGR16_LE_sRGB", "16-bit BGR LE, sRGB, full range",
                PixelFormat::I_3x16_LE, false, 65535);
}

static PixelDesc::Data makeBGR16_BE() {
        return makeBGRDesc(PixelDesc::BGR16_BE_sRGB,
                "BGR16_BE_sRGB", "16-bit BGR BE, sRGB, full range",
                PixelFormat::I_3x16_BE, false, 65535);
}

// ---------------------------------------------------------------------------
// ARGB PixelDesc factory functions
// ---------------------------------------------------------------------------

static PixelDesc::Data makeARGB8() {
        return makeARGBDesc(PixelDesc::ARGB8_sRGB,
                "ARGB8_sRGB", "8-bit ARGB, sRGB, full range",
                PixelFormat::I_4x8, 255);
}

static PixelDesc::Data makeARGB10_LE() {
        return makeARGBDesc(PixelDesc::ARGB10_LE_sRGB,
                "ARGB10_LE_sRGB", "10-bit ARGB in 16-bit LE words, sRGB, full range",
                PixelFormat::I_4x10_LE, 1023);
}

static PixelDesc::Data makeARGB10_BE() {
        return makeARGBDesc(PixelDesc::ARGB10_BE_sRGB,
                "ARGB10_BE_sRGB", "10-bit ARGB in 16-bit BE words, sRGB, full range",
                PixelFormat::I_4x10_BE, 1023);
}

static PixelDesc::Data makeARGB12_LE() {
        return makeARGBDesc(PixelDesc::ARGB12_LE_sRGB,
                "ARGB12_LE_sRGB", "12-bit ARGB in 16-bit LE words, sRGB, full range",
                PixelFormat::I_4x12_LE, 4095);
}

static PixelDesc::Data makeARGB12_BE() {
        return makeARGBDesc(PixelDesc::ARGB12_BE_sRGB,
                "ARGB12_BE_sRGB", "12-bit ARGB in 16-bit BE words, sRGB, full range",
                PixelFormat::I_4x12_BE, 4095);
}

static PixelDesc::Data makeARGB16_LE() {
        return makeARGBDesc(PixelDesc::ARGB16_LE_sRGB,
                "ARGB16_LE_sRGB", "16-bit ARGB LE, sRGB, full range",
                PixelFormat::I_4x16_LE, 65535);
}

static PixelDesc::Data makeARGB16_BE() {
        return makeARGBDesc(PixelDesc::ARGB16_BE_sRGB,
                "ARGB16_BE_sRGB", "16-bit ARGB BE, sRGB, full range",
                PixelFormat::I_4x16_BE, 65535);
}

// ---------------------------------------------------------------------------
// ABGR PixelDesc factory functions
// ---------------------------------------------------------------------------

static PixelDesc::Data makeABGR8() {
        return makeABGRDesc(PixelDesc::ABGR8_sRGB,
                "ABGR8_sRGB", "8-bit ABGR, sRGB, full range",
                PixelFormat::I_4x8, 255);
}

static PixelDesc::Data makeABGR10_LE() {
        return makeABGRDesc(PixelDesc::ABGR10_LE_sRGB,
                "ABGR10_LE_sRGB", "10-bit ABGR in 16-bit LE words, sRGB, full range",
                PixelFormat::I_4x10_LE, 1023);
}

static PixelDesc::Data makeABGR10_BE() {
        return makeABGRDesc(PixelDesc::ABGR10_BE_sRGB,
                "ABGR10_BE_sRGB", "10-bit ABGR in 16-bit BE words, sRGB, full range",
                PixelFormat::I_4x10_BE, 1023);
}

static PixelDesc::Data makeABGR12_LE() {
        return makeABGRDesc(PixelDesc::ABGR12_LE_sRGB,
                "ABGR12_LE_sRGB", "12-bit ABGR in 16-bit LE words, sRGB, full range",
                PixelFormat::I_4x12_LE, 4095);
}

static PixelDesc::Data makeABGR12_BE() {
        return makeABGRDesc(PixelDesc::ABGR12_BE_sRGB,
                "ABGR12_BE_sRGB", "12-bit ABGR in 16-bit BE words, sRGB, full range",
                PixelFormat::I_4x12_BE, 4095);
}

static PixelDesc::Data makeABGR16_LE() {
        return makeABGRDesc(PixelDesc::ABGR16_LE_sRGB,
                "ABGR16_LE_sRGB", "16-bit ABGR LE, sRGB, full range",
                PixelFormat::I_4x16_LE, 65535);
}

static PixelDesc::Data makeABGR16_BE() {
        return makeABGRDesc(PixelDesc::ABGR16_BE_sRGB,
                "ABGR16_BE_sRGB", "16-bit ABGR BE, sRGB, full range",
                PixelFormat::I_4x16_BE, 65535);
}

// ---------------------------------------------------------------------------
// Monochrome sRGB PixelDesc factory functions
// ---------------------------------------------------------------------------

static PixelDesc::Data makeMono8_sRGB() {
        return makeMonoDesc(PixelDesc::Mono8_sRGB,
                "Mono8_sRGB", "8-bit grayscale, sRGB",
                PixelFormat::I_1x8, ColorModel::sRGB, 255);
}

static PixelDesc::Data makeMono10_LE_sRGB() {
        return makeMonoDesc(PixelDesc::Mono10_LE_sRGB,
                "Mono10_LE_sRGB", "10-bit grayscale in 16-bit LE word, sRGB",
                PixelFormat::I_1x10_LE, ColorModel::sRGB, 1023);
}

static PixelDesc::Data makeMono10_BE_sRGB() {
        return makeMonoDesc(PixelDesc::Mono10_BE_sRGB,
                "Mono10_BE_sRGB", "10-bit grayscale in 16-bit BE word, sRGB",
                PixelFormat::I_1x10_BE, ColorModel::sRGB, 1023);
}

static PixelDesc::Data makeMono12_LE_sRGB() {
        return makeMonoDesc(PixelDesc::Mono12_LE_sRGB,
                "Mono12_LE_sRGB", "12-bit grayscale in 16-bit LE word, sRGB",
                PixelFormat::I_1x12_LE, ColorModel::sRGB, 4095);
}

static PixelDesc::Data makeMono12_BE_sRGB() {
        return makeMonoDesc(PixelDesc::Mono12_BE_sRGB,
                "Mono12_BE_sRGB", "12-bit grayscale in 16-bit BE word, sRGB",
                PixelFormat::I_1x12_BE, ColorModel::sRGB, 4095);
}

static PixelDesc::Data makeMono16_LE_sRGB() {
        return makeMonoDesc(PixelDesc::Mono16_LE_sRGB,
                "Mono16_LE_sRGB", "16-bit grayscale LE, sRGB",
                PixelFormat::I_1x16_LE, ColorModel::sRGB, 65535);
}

static PixelDesc::Data makeMono16_BE_sRGB() {
        return makeMonoDesc(PixelDesc::Mono16_BE_sRGB,
                "Mono16_BE_sRGB", "16-bit grayscale BE, sRGB",
                PixelFormat::I_1x16_BE, ColorModel::sRGB, 65535);
}

// ---------------------------------------------------------------------------
// Float RGBA/RGB/Mono LinearRec709 PixelDesc factory functions
// ---------------------------------------------------------------------------

static PixelDesc::Data makeRGBAF16_LE_LinearRec709() {
        return makeFloatRGBDesc(PixelDesc::RGBAF16_LE_LinearRec709,
                "RGBAF16_LE_LinearRec709", "Half-float RGBA LE, linear Rec.709",
                PixelFormat::I_4xF16_LE, true, ColorModel::LinearRec709);
}

static PixelDesc::Data makeRGBAF16_BE_LinearRec709() {
        return makeFloatRGBDesc(PixelDesc::RGBAF16_BE_LinearRec709,
                "RGBAF16_BE_LinearRec709", "Half-float RGBA BE, linear Rec.709",
                PixelFormat::I_4xF16_BE, true, ColorModel::LinearRec709);
}

static PixelDesc::Data makeRGBF16_LE_LinearRec709() {
        return makeFloatRGBDesc(PixelDesc::RGBF16_LE_LinearRec709,
                "RGBF16_LE_LinearRec709", "Half-float RGB LE, linear Rec.709",
                PixelFormat::I_3xF16_LE, false, ColorModel::LinearRec709);
}

static PixelDesc::Data makeRGBF16_BE_LinearRec709() {
        return makeFloatRGBDesc(PixelDesc::RGBF16_BE_LinearRec709,
                "RGBF16_BE_LinearRec709", "Half-float RGB BE, linear Rec.709",
                PixelFormat::I_3xF16_BE, false, ColorModel::LinearRec709);
}

static PixelDesc::Data makeMonoF16_LE_LinearRec709() {
        return makeMonoDesc(PixelDesc::MonoF16_LE_LinearRec709,
                "MonoF16_LE_LinearRec709", "Half-float mono LE, linear Rec.709",
                PixelFormat::I_1xF16_LE, ColorModel::LinearRec709, 1.0);
}

static PixelDesc::Data makeMonoF16_BE_LinearRec709() {
        return makeMonoDesc(PixelDesc::MonoF16_BE_LinearRec709,
                "MonoF16_BE_LinearRec709", "Half-float mono BE, linear Rec.709",
                PixelFormat::I_1xF16_BE, ColorModel::LinearRec709, 1.0);
}

static PixelDesc::Data makeRGBAF32_LE_LinearRec709() {
        return makeFloatRGBDesc(PixelDesc::RGBAF32_LE_LinearRec709,
                "RGBAF32_LE_LinearRec709", "Float RGBA LE, linear Rec.709",
                PixelFormat::I_4xF32_LE, true, ColorModel::LinearRec709);
}

static PixelDesc::Data makeRGBAF32_BE_LinearRec709() {
        return makeFloatRGBDesc(PixelDesc::RGBAF32_BE_LinearRec709,
                "RGBAF32_BE_LinearRec709", "Float RGBA BE, linear Rec.709",
                PixelFormat::I_4xF32_BE, true, ColorModel::LinearRec709);
}

static PixelDesc::Data makeRGBF32_LE_LinearRec709() {
        return makeFloatRGBDesc(PixelDesc::RGBF32_LE_LinearRec709,
                "RGBF32_LE_LinearRec709", "Float RGB LE, linear Rec.709",
                PixelFormat::I_3xF32_LE, false, ColorModel::LinearRec709);
}

static PixelDesc::Data makeRGBF32_BE_LinearRec709() {
        return makeFloatRGBDesc(PixelDesc::RGBF32_BE_LinearRec709,
                "RGBF32_BE_LinearRec709", "Float RGB BE, linear Rec.709",
                PixelFormat::I_3xF32_BE, false, ColorModel::LinearRec709);
}

static PixelDesc::Data makeMonoF32_LE_LinearRec709() {
        return makeMonoDesc(PixelDesc::MonoF32_LE_LinearRec709,
                "MonoF32_LE_LinearRec709", "Float mono LE, linear Rec.709",
                PixelFormat::I_1xF32_LE, ColorModel::LinearRec709, 1.0);
}

static PixelDesc::Data makeMonoF32_BE_LinearRec709() {
        return makeMonoDesc(PixelDesc::MonoF32_BE_LinearRec709,
                "MonoF32_BE_LinearRec709", "Float mono BE, linear Rec.709",
                PixelFormat::I_1xF32_BE, ColorModel::LinearRec709, 1.0);
}

// ---------------------------------------------------------------------------
// 10:10:10:2 packed sRGB PixelDesc factory functions
// ---------------------------------------------------------------------------

static PixelDesc::Data makeRGB10A2_LE() {
        PixelDesc::Data d;
        d.id              = PixelDesc::RGB10A2_LE_sRGB;
        d.name            = "RGB10A2_LE_sRGB";
        d.desc            = "RGB 10-bit + Alpha 2-bit in 32-bit LE, sRGB, full range";
        d.pixelFormat     = PixelFormat(PixelFormat::I_10_10_10_2_LE);
        d.colorModel      = ColorModel(ColorModel::sRGB);
        d.hasAlpha        = true;
        d.alphaCompIndex  = 3;
        d.compSemantics[0] = { "Red",   "R", 0, 1023 };
        d.compSemantics[1] = { "Green", "G", 0, 1023 };
        d.compSemantics[2] = { "Blue",  "B", 0, 1023 };
        d.compSemantics[3] = { "Alpha", "A", 0, 3 };
        return d;
}

static PixelDesc::Data makeRGB10A2_BE() {
        PixelDesc::Data d;
        d.id              = PixelDesc::RGB10A2_BE_sRGB;
        d.name            = "RGB10A2_BE_sRGB";
        d.desc            = "RGB 10-bit + Alpha 2-bit in 32-bit BE, sRGB, full range";
        d.pixelFormat     = PixelFormat(PixelFormat::I_10_10_10_2_BE);
        d.colorModel      = ColorModel(ColorModel::sRGB);
        d.hasAlpha        = true;
        d.alphaCompIndex  = 3;
        d.compSemantics[0] = { "Red",   "R", 0, 1023 };
        d.compSemantics[1] = { "Green", "G", 0, 1023 };
        d.compSemantics[2] = { "Blue",  "B", 0, 1023 };
        d.compSemantics[3] = { "Alpha", "A", 0, 3 };
        return d;
}

static PixelDesc::Data makeBGR10A2_LE() {
        PixelDesc::Data d;
        d.id              = PixelDesc::BGR10A2_LE_sRGB;
        d.name            = "BGR10A2_LE_sRGB";
        d.desc            = "BGR 10-bit + Alpha 2-bit in 32-bit LE, sRGB, full range";
        d.pixelFormat     = PixelFormat(PixelFormat::I_10_10_10_2_LE);
        d.colorModel      = ColorModel(ColorModel::sRGB);
        d.hasAlpha        = true;
        d.alphaCompIndex  = 3;
        d.compSemantics[0] = { "Blue",  "B", 0, 1023 };
        d.compSemantics[1] = { "Green", "G", 0, 1023 };
        d.compSemantics[2] = { "Red",   "R", 0, 1023 };
        d.compSemantics[3] = { "Alpha", "A", 0, 3 };
        return d;
}

static PixelDesc::Data makeBGR10A2_BE() {
        PixelDesc::Data d;
        d.id              = PixelDesc::BGR10A2_BE_sRGB;
        d.name            = "BGR10A2_BE_sRGB";
        d.desc            = "BGR 10-bit + Alpha 2-bit in 32-bit BE, sRGB, full range";
        d.pixelFormat     = PixelFormat(PixelFormat::I_10_10_10_2_BE);
        d.colorModel      = ColorModel(ColorModel::sRGB);
        d.hasAlpha        = true;
        d.alphaCompIndex  = 3;
        d.compSemantics[0] = { "Blue",  "B", 0, 1023 };
        d.compSemantics[1] = { "Green", "G", 0, 1023 };
        d.compSemantics[2] = { "Red",   "R", 0, 1023 };
        d.compSemantics[3] = { "Alpha", "A", 0, 3 };
        return d;
}

// ---------------------------------------------------------------------------
// 4:4:4 YCbCr Rec.709 PixelDesc factory functions
// ---------------------------------------------------------------------------

static PixelDesc::Data makeYUV8_444() {
        return makeYCbCrDesc(PixelDesc::YUV8_Rec709,
                "YUV8_Rec709", "8-bit YCbCr 4:4:4, Rec.709, limited range",
                PixelFormat::I_3x8, ycbcrSem8);
}

static PixelDesc::Data makeYUV10_LE_444() {
        return makeYCbCrDesc(PixelDesc::YUV10_LE_Rec709,
                "YUV10_LE_Rec709", "10-bit YCbCr 4:4:4 LE, Rec.709, limited range",
                PixelFormat::I_3x10_LE, ycbcrSem10);
}

static PixelDesc::Data makeYUV10_BE_444() {
        return makeYCbCrDesc(PixelDesc::YUV10_BE_Rec709,
                "YUV10_BE_Rec709", "10-bit YCbCr 4:4:4 BE, Rec.709, limited range",
                PixelFormat::I_3x10_BE, ycbcrSem10);
}

static PixelDesc::Data makeYUV12_LE_444() {
        return makeYCbCrDesc(PixelDesc::YUV12_LE_Rec709,
                "YUV12_LE_Rec709", "12-bit YCbCr 4:4:4 LE, Rec.709, limited range",
                PixelFormat::I_3x12_LE, ycbcrSem12);
}

static PixelDesc::Data makeYUV12_BE_444() {
        return makeYCbCrDesc(PixelDesc::YUV12_BE_Rec709,
                "YUV12_BE_Rec709", "12-bit YCbCr 4:4:4 BE, Rec.709, limited range",
                PixelFormat::I_3x12_BE, ycbcrSem12);
}

static PixelDesc::Data makeYUV16_LE_444() {
        return makeYCbCrDesc(PixelDesc::YUV16_LE_Rec709,
                "YUV16_LE_Rec709", "16-bit YCbCr 4:4:4 LE, Rec.709, limited range",
                PixelFormat::I_3x16_LE, ycbcrSem16);
}

static PixelDesc::Data makeYUV16_BE_444() {
        return makeYCbCrDesc(PixelDesc::YUV16_BE_Rec709,
                "YUV16_BE_Rec709", "16-bit YCbCr 4:4:4 BE, Rec.709, limited range",
                PixelFormat::I_3x16_BE, ycbcrSem16);
}

// ---------------------------------------------------------------------------
// Rec.2020 YCbCr PixelDesc factory functions
// ---------------------------------------------------------------------------

static PixelDesc::Data makeYUV10_422_UYVY_LE_Rec2020() {
        return makeYCbCrDescWithModel(PixelDesc::YUV10_422_UYVY_LE_Rec2020,
                "YUV10_422_UYVY_LE_Rec2020", "10-bit YCbCr 4:2:2 UYVY LE, Rec.2020, limited range",
                PixelFormat::I_422_UYVY_3x10_LE, ycbcrSem10, ColorModel::YCbCr_Rec2020);
}

static PixelDesc::Data makeYUV10_422_UYVY_BE_Rec2020() {
        return makeYCbCrDescWithModel(PixelDesc::YUV10_422_UYVY_BE_Rec2020,
                "YUV10_422_UYVY_BE_Rec2020", "10-bit YCbCr 4:2:2 UYVY BE, Rec.2020, limited range",
                PixelFormat::I_422_UYVY_3x10_BE, ycbcrSem10, ColorModel::YCbCr_Rec2020);
}

static PixelDesc::Data makeYUV12_422_UYVY_LE_Rec2020() {
        return makeYCbCrDescWithModel(PixelDesc::YUV12_422_UYVY_LE_Rec2020,
                "YUV12_422_UYVY_LE_Rec2020", "12-bit YCbCr 4:2:2 UYVY LE, Rec.2020, limited range",
                PixelFormat::I_422_UYVY_3x12_LE, ycbcrSem12, ColorModel::YCbCr_Rec2020);
}

static PixelDesc::Data makeYUV12_422_UYVY_BE_Rec2020() {
        return makeYCbCrDescWithModel(PixelDesc::YUV12_422_UYVY_BE_Rec2020,
                "YUV12_422_UYVY_BE_Rec2020", "12-bit YCbCr 4:2:2 UYVY BE, Rec.2020, limited range",
                PixelFormat::I_422_UYVY_3x12_BE, ycbcrSem12, ColorModel::YCbCr_Rec2020);
}

static PixelDesc::Data makeYUV10_420_Planar_LE_Rec2020() {
        return makeYCbCrDescWithModel(PixelDesc::YUV10_420_Planar_LE_Rec2020,
                "YUV10_420_Planar_LE_Rec2020", "10-bit YCbCr 4:2:0 planar LE, Rec.2020, limited range",
                PixelFormat::P_420_3x10_LE, ycbcrSem10, ColorModel::YCbCr_Rec2020);
}

static PixelDesc::Data makeYUV10_420_Planar_BE_Rec2020() {
        return makeYCbCrDescWithModel(PixelDesc::YUV10_420_Planar_BE_Rec2020,
                "YUV10_420_Planar_BE_Rec2020", "10-bit YCbCr 4:2:0 planar BE, Rec.2020, limited range",
                PixelFormat::P_420_3x10_BE, ycbcrSem10, ColorModel::YCbCr_Rec2020);
}

static PixelDesc::Data makeYUV12_420_Planar_LE_Rec2020() {
        return makeYCbCrDescWithModel(PixelDesc::YUV12_420_Planar_LE_Rec2020,
                "YUV12_420_Planar_LE_Rec2020", "12-bit YCbCr 4:2:0 planar LE, Rec.2020, limited range",
                PixelFormat::P_420_3x12_LE, ycbcrSem12, ColorModel::YCbCr_Rec2020);
}

static PixelDesc::Data makeYUV12_420_Planar_BE_Rec2020() {
        return makeYCbCrDescWithModel(PixelDesc::YUV12_420_Planar_BE_Rec2020,
                "YUV12_420_Planar_BE_Rec2020", "12-bit YCbCr 4:2:0 planar BE, Rec.2020, limited range",
                PixelFormat::P_420_3x12_BE, ycbcrSem12, ColorModel::YCbCr_Rec2020);
}

// ---------------------------------------------------------------------------
// Rec.601 YCbCr PixelDesc factory functions
// ---------------------------------------------------------------------------

static PixelDesc::Data makeYUV8_422_Rec601() {
        return makeYCbCrDescWithModel(PixelDesc::YUV8_422_Rec601,
                "YUV8_422_Rec601", "8-bit YCbCr 4:2:2, Rec.601, limited range",
                PixelFormat::I_422_3x8, ycbcrSem8, ColorModel::YCbCr_Rec601);
}

static PixelDesc::Data makeYUV8_422_UYVY_Rec601() {
        return makeYCbCrDescWithModel(PixelDesc::YUV8_422_UYVY_Rec601,
                "YUV8_422_UYVY_Rec601", "8-bit YCbCr 4:2:2 UYVY, Rec.601, limited range",
                PixelFormat::I_422_UYVY_3x8, ycbcrSem8, ColorModel::YCbCr_Rec601);
}

static PixelDesc::Data makeYUV8_420_Planar_Rec601() {
        return makeYCbCrDescWithModel(PixelDesc::YUV8_420_Planar_Rec601,
                "YUV8_420_Planar_Rec601", "8-bit YCbCr 4:2:0 planar, Rec.601, limited range",
                PixelFormat::P_420_3x8, ycbcrSem8, ColorModel::YCbCr_Rec601);
}

static PixelDesc::Data makeYUV8_420_SemiPlanar_Rec601() {
        return makeYCbCrDescWithModel(PixelDesc::YUV8_420_SemiPlanar_Rec601,
                "YUV8_420_SemiPlanar_Rec601", "8-bit YCbCr 4:2:0 NV12, Rec.601, limited range",
                PixelFormat::SP_420_8, ycbcrSem8, ColorModel::YCbCr_Rec601);
}

// ---------------------------------------------------------------------------
// NV21 Rec.709 PixelDesc factory functions
// ---------------------------------------------------------------------------

static PixelDesc::Data makeYUV8_420_NV21() {
        auto d = makeYCbCrDesc(PixelDesc::YUV8_420_NV21_Rec709,
                "YUV8_420_NV21_Rec709", "8-bit YCbCr 4:2:0 NV21, Rec.709, limited range",
                PixelFormat::SP_420_NV21_8, ycbcrSem8);
        d.fourccList = { "NV21" };
        return d;
}

static PixelDesc::Data makeYUV10_420_NV21_LE() {
        return makeYCbCrDesc(PixelDesc::YUV10_420_NV21_LE_Rec709,
                "YUV10_420_NV21_LE_Rec709", "10-bit YCbCr 4:2:0 NV21 LE, Rec.709, limited range",
                PixelFormat::SP_420_NV21_10_LE, ycbcrSem10);
}

static PixelDesc::Data makeYUV10_420_NV21_BE() {
        return makeYCbCrDesc(PixelDesc::YUV10_420_NV21_BE_Rec709,
                "YUV10_420_NV21_BE_Rec709", "10-bit YCbCr 4:2:0 NV21 BE, Rec.709, limited range",
                PixelFormat::SP_420_NV21_10_BE, ycbcrSem10);
}

static PixelDesc::Data makeYUV12_420_NV21_LE() {
        return makeYCbCrDesc(PixelDesc::YUV12_420_NV21_LE_Rec709,
                "YUV12_420_NV21_LE_Rec709", "12-bit YCbCr 4:2:0 NV21 LE, Rec.709, limited range",
                PixelFormat::SP_420_NV21_12_LE, ycbcrSem12);
}

static PixelDesc::Data makeYUV12_420_NV21_BE() {
        return makeYCbCrDesc(PixelDesc::YUV12_420_NV21_BE_Rec709,
                "YUV12_420_NV21_BE_Rec709", "12-bit YCbCr 4:2:0 NV21 BE, Rec.709, limited range",
                PixelFormat::SP_420_NV21_12_BE, ycbcrSem12);
}

// ---------------------------------------------------------------------------
// NV16 semi-planar 4:2:2 Rec.709 PixelDesc factory functions
// ---------------------------------------------------------------------------

static PixelDesc::Data makeYUV8_422_SemiPlanar() {
        auto d = makeYCbCrDesc(PixelDesc::YUV8_422_SemiPlanar_Rec709,
                "YUV8_422_SemiPlanar_Rec709", "8-bit YCbCr 4:2:2 NV16, Rec.709, limited range",
                PixelFormat::SP_422_8, ycbcrSem8);
        d.fourccList = { "NV16" };
        return d;
}

static PixelDesc::Data makeYUV10_422_SemiPlanar_LE() {
        return makeYCbCrDesc(PixelDesc::YUV10_422_SemiPlanar_LE_Rec709,
                "YUV10_422_SemiPlanar_LE_Rec709", "10-bit YCbCr 4:2:2 NV16 LE, Rec.709, limited range",
                PixelFormat::SP_422_10_LE, ycbcrSem10);
}

static PixelDesc::Data makeYUV10_422_SemiPlanar_BE() {
        return makeYCbCrDesc(PixelDesc::YUV10_422_SemiPlanar_BE_Rec709,
                "YUV10_422_SemiPlanar_BE_Rec709", "10-bit YCbCr 4:2:2 NV16 BE, Rec.709, limited range",
                PixelFormat::SP_422_10_BE, ycbcrSem10);
}

static PixelDesc::Data makeYUV12_422_SemiPlanar_LE() {
        return makeYCbCrDesc(PixelDesc::YUV12_422_SemiPlanar_LE_Rec709,
                "YUV12_422_SemiPlanar_LE_Rec709", "12-bit YCbCr 4:2:2 NV16 LE, Rec.709, limited range",
                PixelFormat::SP_422_12_LE, ycbcrSem12);
}

static PixelDesc::Data makeYUV12_422_SemiPlanar_BE() {
        return makeYCbCrDesc(PixelDesc::YUV12_422_SemiPlanar_BE_Rec709,
                "YUV12_422_SemiPlanar_BE_Rec709", "12-bit YCbCr 4:2:2 NV16 BE, Rec.709, limited range",
                PixelFormat::SP_422_12_BE, ycbcrSem12);
}

// ---------------------------------------------------------------------------
// 4:1:1 Rec.709 PixelDesc factory function
// ---------------------------------------------------------------------------

static PixelDesc::Data makeYUV8_411_Planar() {
        return makeYCbCrDesc(PixelDesc::YUV8_411_Planar_Rec709,
                "YUV8_411_Planar_Rec709", "8-bit YCbCr 4:1:1 planar, Rec.709, limited range",
                PixelFormat::P_411_3x8, ycbcrSem8);
}

// ---------------------------------------------------------------------------
// 16-bit YCbCr Rec.709 PixelDesc factory functions
// ---------------------------------------------------------------------------

static PixelDesc::Data makeYUV16_422_UYVY_LE() {
        return makeYCbCrDesc(PixelDesc::YUV16_422_UYVY_LE_Rec709,
                "YUV16_422_UYVY_LE_Rec709", "16-bit YCbCr 4:2:2 UYVY LE, Rec.709, limited range",
                PixelFormat::I_422_UYVY_3x16_LE, ycbcrSem16);
}

static PixelDesc::Data makeYUV16_422_UYVY_BE() {
        return makeYCbCrDesc(PixelDesc::YUV16_422_UYVY_BE_Rec709,
                "YUV16_422_UYVY_BE_Rec709", "16-bit YCbCr 4:2:2 UYVY BE, Rec.709, limited range",
                PixelFormat::I_422_UYVY_3x16_BE, ycbcrSem16);
}

static PixelDesc::Data makeYUV16_422_Planar_LE() {
        return makeYCbCrDesc(PixelDesc::YUV16_422_Planar_LE_Rec709,
                "YUV16_422_Planar_LE_Rec709", "16-bit YCbCr 4:2:2 planar LE, Rec.709, limited range",
                PixelFormat::P_422_3x16_LE, ycbcrSem16);
}

static PixelDesc::Data makeYUV16_422_Planar_BE() {
        return makeYCbCrDesc(PixelDesc::YUV16_422_Planar_BE_Rec709,
                "YUV16_422_Planar_BE_Rec709", "16-bit YCbCr 4:2:2 planar BE, Rec.709, limited range",
                PixelFormat::P_422_3x16_BE, ycbcrSem16);
}

static PixelDesc::Data makeYUV16_420_Planar_LE() {
        return makeYCbCrDesc(PixelDesc::YUV16_420_Planar_LE_Rec709,
                "YUV16_420_Planar_LE_Rec709", "16-bit YCbCr 4:2:0 planar LE, Rec.709, limited range",
                PixelFormat::P_420_3x16_LE, ycbcrSem16);
}

static PixelDesc::Data makeYUV16_420_Planar_BE() {
        return makeYCbCrDesc(PixelDesc::YUV16_420_Planar_BE_Rec709,
                "YUV16_420_Planar_BE_Rec709", "16-bit YCbCr 4:2:0 planar BE, Rec.709, limited range",
                PixelFormat::P_420_3x16_BE, ycbcrSem16);
}

static PixelDesc::Data makeYUV16_420_SemiPlanar_LE() {
        return makeYCbCrDesc(PixelDesc::YUV16_420_SemiPlanar_LE_Rec709,
                "YUV16_420_SemiPlanar_LE_Rec709", "16-bit YCbCr 4:2:0 NV12 LE, Rec.709, limited range",
                PixelFormat::SP_420_16_LE, ycbcrSem16);
}

static PixelDesc::Data makeYUV16_420_SemiPlanar_BE() {
        return makeYCbCrDesc(PixelDesc::YUV16_420_SemiPlanar_BE_Rec709,
                "YUV16_420_SemiPlanar_BE_Rec709", "16-bit YCbCr 4:2:0 NV12 BE, Rec.709, limited range",
                PixelFormat::SP_420_16_BE, ycbcrSem16);
}

// ---------------------------------------------------------------------------
// Video codec compressed formats (QuickTime / MP4 / ISO-BMFF)
//
// These entries describe compressed bitstreams by codec. The pixelFormat
// field is a semantic hint at the "natural" decoded layout for the codec;
// actual decoded output formats belong in decodeTargets (added when a
// real decoder is wired up). The fourccList carries every FourCC code
// that identifies this codec across containers — the first entry is the
// preferred code used by writers.
// ---------------------------------------------------------------------------

static PixelDesc::Data makeH264() {
        PixelDesc::Data d;
        d.id               = PixelDesc::H264;
        d.name             = "H264";
        d.desc             = "H.264 / AVC compressed video";
        d.pixelFormat      = PixelFormat(PixelFormat::P_420_3x8);
        d.colorModel       = ColorModel(ColorModel::YCbCr_Rec709);
        d.compressed       = true;
        d.codecName        = "h264";
        d.fourccList       = { "avc1", "avc3" };
        d.compSemantics[0] = ycbcrSem8[0];
        d.compSemantics[1] = ycbcrSem8[1];
        d.compSemantics[2] = ycbcrSem8[2];
        return d;
}

static PixelDesc::Data makeHEVC() {
        PixelDesc::Data d;
        d.id               = PixelDesc::HEVC;
        d.name             = "HEVC";
        d.desc             = "H.265 / HEVC compressed video";
        d.pixelFormat      = PixelFormat(PixelFormat::P_420_3x10_LE);
        d.colorModel       = ColorModel(ColorModel::YCbCr_Rec709);
        d.compressed       = true;
        d.codecName        = "hevc";
        d.fourccList       = { "hvc1", "hev1" };
        d.compSemantics[0] = ycbcrSem10[0];
        d.compSemantics[1] = ycbcrSem10[1];
        d.compSemantics[2] = ycbcrSem10[2];
        return d;
}

static PixelDesc::Data makeProRes422Desc(PixelDesc::ID id, const char *name, const char *desc,
                                         FourCC fourcc) {
        PixelDesc::Data d;
        d.id               = id;
        d.name             = name;
        d.desc             = desc;
        // ProRes 422 family decodes to 10-bit YCbCr 4:2:2.
        d.pixelFormat      = PixelFormat(PixelFormat::P_422_3x10_LE);
        d.colorModel       = ColorModel(ColorModel::YCbCr_Rec709);
        d.compressed       = true;
        d.codecName        = "prores";
        d.fourccList       = { fourcc };
        d.compSemantics[0] = ycbcrSem10[0];
        d.compSemantics[1] = ycbcrSem10[1];
        d.compSemantics[2] = ycbcrSem10[2];
        return d;
}

static PixelDesc::Data makeProRes_422_Proxy() {
        return makeProRes422Desc(PixelDesc::ProRes_422_Proxy,
                                 "ProRes_422_Proxy", "Apple ProRes 422 Proxy", FourCC("apco"));
}

static PixelDesc::Data makeProRes_422_LT() {
        return makeProRes422Desc(PixelDesc::ProRes_422_LT,
                                 "ProRes_422_LT", "Apple ProRes 422 LT", FourCC("apcs"));
}

static PixelDesc::Data makeProRes_422() {
        return makeProRes422Desc(PixelDesc::ProRes_422,
                                 "ProRes_422", "Apple ProRes 422", FourCC("apcn"));
}

static PixelDesc::Data makeProRes_422_HQ() {
        return makeProRes422Desc(PixelDesc::ProRes_422_HQ,
                                 "ProRes_422_HQ", "Apple ProRes 422 HQ", FourCC("apch"));
}

static PixelDesc::Data makeProRes_4444() {
        PixelDesc::Data d;
        d.id               = PixelDesc::ProRes_4444;
        d.name             = "ProRes_4444";
        d.desc             = "Apple ProRes 4444";
        // ProRes 4444 decodes to 10-bit 4:4:4 with optional alpha.
        d.pixelFormat      = PixelFormat(PixelFormat::I_4x10_LE);
        d.colorModel       = ColorModel(ColorModel::YCbCr_Rec709);
        d.compressed       = true;
        d.hasAlpha         = true;
        d.alphaCompIndex   = 3;
        d.codecName        = "prores";
        d.fourccList       = { "ap4h" };
        d.compSemantics[0] = ycbcrSem10[0];
        d.compSemantics[1] = ycbcrSem10[1];
        d.compSemantics[2] = ycbcrSem10[2];
        d.compSemantics[3] = { "Alpha", "A", 0, 1023 };
        return d;
}

static PixelDesc::Data makeProRes_4444_XQ() {
        PixelDesc::Data d;
        d.id               = PixelDesc::ProRes_4444_XQ;
        d.name             = "ProRes_4444_XQ";
        d.desc             = "Apple ProRes 4444 XQ";
        // ProRes 4444 XQ decodes to 12-bit 4:4:4 with optional alpha.
        d.pixelFormat      = PixelFormat(PixelFormat::I_4x12_LE);
        d.colorModel       = ColorModel(ColorModel::YCbCr_Rec709);
        d.compressed       = true;
        d.hasAlpha         = true;
        d.alphaCompIndex   = 3;
        d.codecName        = "prores";
        d.fourccList       = { "ap4x" };
        d.compSemantics[0] = ycbcrSem12[0];
        d.compSemantics[1] = ycbcrSem12[1];
        d.compSemantics[2] = ycbcrSem12[2];
        d.compSemantics[3] = { "Alpha", "A", 0, 4095 };
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
                add(makeRGBA10_LE());
                add(makeRGBA10_BE());
                add(makeRGB10_LE());
                add(makeRGB10_BE());
                add(makeRGBA12_LE());
                add(makeRGBA12_BE());
                add(makeRGB12_LE());
                add(makeRGB12_BE());
                add(makeRGBA16_LE());
                add(makeRGBA16_BE());
                add(makeRGB16_LE());
                add(makeRGB16_BE());
                add(makeYUV10_DPX());
                add(makeRGB10_DPX_LE());
                add(makeYUV10_DPX_B());

                // BGRA/BGR
                add(makeBGRA8());
                add(makeBGR8());
                add(makeBGRA10_LE());
                add(makeBGRA10_BE());
                add(makeBGR10_LE());
                add(makeBGR10_BE());
                add(makeBGRA12_LE());
                add(makeBGRA12_BE());
                add(makeBGR12_LE());
                add(makeBGR12_BE());
                add(makeBGRA16_LE());
                add(makeBGRA16_BE());
                add(makeBGR16_LE());
                add(makeBGR16_BE());

                // ARGB
                add(makeARGB8());
                add(makeARGB10_LE());
                add(makeARGB10_BE());
                add(makeARGB12_LE());
                add(makeARGB12_BE());
                add(makeARGB16_LE());
                add(makeARGB16_BE());

                // ABGR
                add(makeABGR8());
                add(makeABGR10_LE());
                add(makeABGR10_BE());
                add(makeABGR12_LE());
                add(makeABGR12_BE());
                add(makeABGR16_LE());
                add(makeABGR16_BE());

                // Monochrome sRGB
                add(makeMono8_sRGB());
                add(makeMono10_LE_sRGB());
                add(makeMono10_BE_sRGB());
                add(makeMono12_LE_sRGB());
                add(makeMono12_BE_sRGB());
                add(makeMono16_LE_sRGB());
                add(makeMono16_BE_sRGB());

                // Float RGBA/RGB/Mono LinearRec709
                add(makeRGBAF16_LE_LinearRec709());
                add(makeRGBAF16_BE_LinearRec709());
                add(makeRGBF16_LE_LinearRec709());
                add(makeRGBF16_BE_LinearRec709());
                add(makeMonoF16_LE_LinearRec709());
                add(makeMonoF16_BE_LinearRec709());
                add(makeRGBAF32_LE_LinearRec709());
                add(makeRGBAF32_BE_LinearRec709());
                add(makeRGBF32_LE_LinearRec709());
                add(makeRGBF32_BE_LinearRec709());
                add(makeMonoF32_LE_LinearRec709());
                add(makeMonoF32_BE_LinearRec709());

                // 10:10:10:2 packed sRGB
                add(makeRGB10A2_LE());
                add(makeRGB10A2_BE());
                add(makeBGR10A2_LE());
                add(makeBGR10A2_BE());

                // 4:4:4 YCbCr Rec.709
                add(makeYUV8_444());
                add(makeYUV10_LE_444());
                add(makeYUV10_BE_444());
                add(makeYUV12_LE_444());
                add(makeYUV12_BE_444());
                add(makeYUV16_LE_444());
                add(makeYUV16_BE_444());

                // Rec.2020 YCbCr
                add(makeYUV10_422_UYVY_LE_Rec2020());
                add(makeYUV10_422_UYVY_BE_Rec2020());
                add(makeYUV12_422_UYVY_LE_Rec2020());
                add(makeYUV12_422_UYVY_BE_Rec2020());
                add(makeYUV10_420_Planar_LE_Rec2020());
                add(makeYUV10_420_Planar_BE_Rec2020());
                add(makeYUV12_420_Planar_LE_Rec2020());
                add(makeYUV12_420_Planar_BE_Rec2020());

                // Rec.601 YCbCr
                add(makeYUV8_422_Rec601());
                add(makeYUV8_422_UYVY_Rec601());
                add(makeYUV8_420_Planar_Rec601());
                add(makeYUV8_420_SemiPlanar_Rec601());

                // NV21 Rec.709
                add(makeYUV8_420_NV21());
                add(makeYUV10_420_NV21_LE());
                add(makeYUV10_420_NV21_BE());
                add(makeYUV12_420_NV21_LE());
                add(makeYUV12_420_NV21_BE());

                // NV16 semi-planar 4:2:2 Rec.709
                add(makeYUV8_422_SemiPlanar());
                add(makeYUV10_422_SemiPlanar_LE());
                add(makeYUV10_422_SemiPlanar_BE());
                add(makeYUV12_422_SemiPlanar_LE());
                add(makeYUV12_422_SemiPlanar_BE());

                // 4:1:1 Rec.709
                add(makeYUV8_411_Planar());

                // 16-bit YCbCr Rec.709
                add(makeYUV16_422_UYVY_LE());
                add(makeYUV16_422_UYVY_BE());
                add(makeYUV16_422_Planar_LE());
                add(makeYUV16_422_Planar_BE());
                add(makeYUV16_420_Planar_LE());
                add(makeYUV16_420_Planar_BE());
                add(makeYUV16_420_SemiPlanar_LE());
                add(makeYUV16_420_SemiPlanar_BE());

                // Video codec compressed formats (QuickTime / MP4 / ISO-BMFF)
                add(makeH264());
                add(makeHEVC());
                add(makeProRes_422_Proxy());
                add(makeProRes_422_LT());
                add(makeProRes_422());
                add(makeProRes_422_HQ());
                add(makeProRes_4444());
                add(makeProRes_4444_XQ());

                // Full-range uncompressed YCbCr intermediates (used by
                // the full-range JPEG encode path and any other
                // pipeline that needs full-range YCbCr).
                add(makeYUV8_422_Rec709_Full());
                add(makeYUV8_422_Rec601_Full());
                add(makeYUV8_420_Planar_Rec709_Full());
                add(makeYUV8_420_Planar_Rec601_Full());

                // Full complement of JPEG YCbCr variants.  The two
                // Rec.709 limited-range entries are already registered
                // earlier in this block via the legacy
                // makeJPEG_YUV8_422 / makeJPEG_YUV8_420 wrappers; these
                // are the six additional (matrix × range) cells.
                add(makeJPEG_YUV8_422_Rec601());
                add(makeJPEG_YUV8_420_Rec601());
                add(makeJPEG_YUV8_422_Rec709_Full());
                add(makeJPEG_YUV8_420_Rec709_Full());
                add(makeJPEG_YUV8_422_Rec601_Full());
                add(makeJPEG_YUV8_420_Rec601_Full());

                // JPEG XS entries.  Unlike JPEG these only distinguish
                // bit depth and subsampling (matrix / range live in the
                // container metadata) so the grid is smaller: 8/10/12-bit
                // × 4:2:2/4:2:0 for YCbCr, plus an 8-bit sRGB entry.
                add(makeJPEG_XS_YUV8_422_Rec709());
                add(makeJPEG_XS_YUV10_422_Rec709());
                add(makeJPEG_XS_YUV12_422_Rec709());
                add(makeJPEG_XS_YUV8_420_Rec709());
                add(makeJPEG_XS_YUV10_420_Rec709());
                add(makeJPEG_XS_YUV12_420_Rec709());
                add(makeJPEG_XS_RGB8_sRGB());
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
PaintEngine createPaintEngine_RGBA10_LE(const PixelDesc::Data *d, const Image &img);
PaintEngine createPaintEngine_RGB10_LE(const PixelDesc::Data *d, const Image &img);
PaintEngine createPaintEngine_RGBA12_LE(const PixelDesc::Data *d, const Image &img);
PaintEngine createPaintEngine_RGB12_LE(const PixelDesc::Data *d, const Image &img);
PaintEngine createPaintEngine_RGBA16_LE(const PixelDesc::Data *d, const Image &img);
PaintEngine createPaintEngine_RGB16_LE(const PixelDesc::Data *d, const Image &img);
PaintEngine createPaintEngine_BGRA8(const PixelDesc::Data *d, const Image &img);
PaintEngine createPaintEngine_BGR8(const PixelDesc::Data *d, const Image &img);
PaintEngine createPaintEngine_ARGB8(const PixelDesc::Data *d, const Image &img);
PaintEngine createPaintEngine_ABGR8(const PixelDesc::Data *d, const Image &img);

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

                // 8-bit
                patch(PixelDesc::RGBA8_sRGB, createPaintEngine_RGBA8);
                patch(PixelDesc::RGB8_sRGB,  createPaintEngine_RGB8);

                // 10-bit LE
                patch(PixelDesc::RGBA10_LE_sRGB, createPaintEngine_RGBA10_LE);
                patch(PixelDesc::RGB10_LE_sRGB,  createPaintEngine_RGB10_LE);

                // 12-bit LE
                patch(PixelDesc::RGBA12_LE_sRGB, createPaintEngine_RGBA12_LE);
                patch(PixelDesc::RGB12_LE_sRGB,  createPaintEngine_RGB12_LE);

                // 16-bit LE
                patch(PixelDesc::RGBA16_LE_sRGB, createPaintEngine_RGBA16_LE);
                patch(PixelDesc::RGB16_LE_sRGB,  createPaintEngine_RGB16_LE);

                // Component-reordered 8-bit
                patch(PixelDesc::BGRA8_sRGB, createPaintEngine_BGRA8);
                patch(PixelDesc::BGR8_sRGB,  createPaintEngine_BGR8);
                patch(PixelDesc::ARGB8_sRGB, createPaintEngine_ARGB8);
                patch(PixelDesc::ABGR8_sRGB, createPaintEngine_ABGR8);
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
