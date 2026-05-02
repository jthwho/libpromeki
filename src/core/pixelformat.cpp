/**
 * @file      pixelformat.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/pixelformat.h>
#include <promeki/atomic.h>
#include <promeki/map.h>
#include <promeki/paintengine.h>
#include <promeki/imagedesc.h>
#include <promeki/metadata.h>
#include <promeki/uncompressedvideopayload.h>

PROMEKI_NAMESPACE_BEGIN

// ---------------------------------------------------------------------------
// Atomic ID counter for user-registered types
// ---------------------------------------------------------------------------

static Atomic<int> _nextType{PixelFormat::UserDefined};

PixelFormat::ID PixelFormat::registerType() {
        return static_cast<ID>(_nextType.fetchAndAdd(1));
}

// ---------------------------------------------------------------------------
// Factory functions for well-known pixel descriptions
// ---------------------------------------------------------------------------

static PixelFormat::Data makeInvalid() {
        PixelFormat::Data d;
        d.id = PixelFormat::Invalid;
        d.name = "Invalid";
        d.desc = "Invalid pixel description";
        return d;
}

static PixelFormat::Data makeRGBA8() {
        PixelFormat::Data d;
        d.id = PixelFormat::RGBA8_sRGB;
        d.name = "RGBA8_sRGB";
        d.desc = "8-bit RGBA, sRGB, full range";
        d.memLayout = PixelMemLayout(PixelMemLayout::I_4x8);
        d.colorModel = ColorModel(ColorModel::sRGB);
        d.hasAlpha = true;
        d.alphaCompIndex = 3;
        d.fourccList = {"RGBA"};
        d.compSemantics[0] = {"Red", "R", 0, 255};
        d.compSemantics[1] = {"Green", "G", 0, 255};
        d.compSemantics[2] = {"Blue", "B", 0, 255};
        d.compSemantics[3] = {"Alpha", "A", 0, 255};
        return d;
}

static PixelFormat::Data makeRGB8() {
        PixelFormat::Data d;
        d.id = PixelFormat::RGB8_sRGB;
        d.name = "RGB8_sRGB";
        d.desc = "8-bit RGB, sRGB, full range";
        d.memLayout = PixelMemLayout(PixelMemLayout::I_3x8);
        d.colorModel = ColorModel(ColorModel::sRGB);
        // "raw " — QuickTime canonical FourCC for packed 24-bit.
        //          FIXME: players disagree on byte order. The historical
        //          QuickTime spec says 'raw ' + depth 24 is B,G,R, but
        //          ffmpeg/VLC/our reader treat it as R,G,B (matching what
        //          ffmpeg's rawvideo encoder emits). mplayer follows the
        //          spec and swaps red/blue on our files. See
        //          devplan/fixme.md → "QuickTime: 'raw ' Codec Byte Order".
        // "RGB2" — historical/AVI alias for the same byte layout.
        d.fourccList = {"raw ", "RGB2"};
        d.compSemantics[0] = {"Red", "R", 0, 255};
        d.compSemantics[1] = {"Green", "G", 0, 255};
        d.compSemantics[2] = {"Blue", "B", 0, 255};
        return d;
}

static PixelFormat::Data makeRGB10() {
        PixelFormat::Data d;
        d.id = PixelFormat::RGB10_DPX_sRGB;
        d.name = "RGB10_DPX_sRGB";
        d.desc = "10-bit RGB, sRGB, full range";
        d.memLayout = PixelMemLayout(PixelMemLayout::I_3x10_DPX);
        d.colorModel = ColorModel(ColorModel::sRGB);
        d.compSemantics[0] = {"Red", "R", 0, 1023};
        d.compSemantics[1] = {"Green", "G", 0, 1023};
        d.compSemantics[2] = {"Blue", "B", 0, 1023};
        return d;
}

static PixelFormat::Data makeYUV8_422() {
        PixelFormat::Data d;
        d.id = PixelFormat::YUV8_422_Rec709;
        d.name = "YUV8_422_Rec709";
        d.desc = "8-bit YCbCr 4:2:2 YUYV, Rec.709, limited range";
        d.memLayout = PixelMemLayout(PixelMemLayout::I_422_3x8);
        d.colorModel = ColorModel(ColorModel::YCbCr_Rec709);
        d.fourccList = {"YUY2", "YUYV"};
        d.compSemantics[0] = {"Luma", "Y", 16, 235};
        d.compSemantics[1] = {"Chroma Blue", "Cb", 16, 240};
        d.compSemantics[2] = {"Chroma Red", "Cr", 16, 240};
        return d;
}

static PixelFormat::Data makeYUV10_422() {
        PixelFormat::Data d;
        d.id = PixelFormat::YUV10_422_Rec709;
        d.name = "YUV10_422_Rec709";
        d.desc = "10-bit YCbCr 4:2:2, Rec.709, limited range";
        d.memLayout = PixelMemLayout(PixelMemLayout::I_422_3x10);
        d.colorModel = ColorModel(ColorModel::YCbCr_Rec709);
        d.compSemantics[0] = {"Luma", "Y", 64, 940};
        d.compSemantics[1] = {"Chroma Blue", "Cb", 64, 960};
        d.compSemantics[2] = {"Chroma Red", "Cr", 64, 960};
        return d;
}

static PixelFormat::Data makeJPEG_RGBA8() {
        PixelFormat::Data d;
        d.id = PixelFormat::JPEG_RGBA8_sRGB;
        d.name = "JPEG_RGBA8_sRGB";
        d.desc = "JPEG-compressed 8-bit RGBA";
        d.memLayout = PixelMemLayout(PixelMemLayout::I_4x8);
        d.colorModel = ColorModel(ColorModel::sRGB);
        d.hasAlpha = true;
        d.alphaCompIndex = 3;
        d.compressed = true;
        d.videoCodec = VideoCodec(VideoCodec::JPEG);
        d.encodeSources = {PixelFormat::RGBA8_sRGB};
        d.decodeTargets = {PixelFormat::RGBA8_sRGB};
        d.fourccList = {"JPEG", "mjpa", "mjpb", "mjpg", "AVRn", "AVDJ", "ADJV"};
        d.compSemantics[0] = {"Red", "R", 0, 255};
        d.compSemantics[1] = {"Green", "G", 0, 255};
        d.compSemantics[2] = {"Blue", "B", 0, 255};
        d.compSemantics[3] = {"Alpha", "A", 0, 255};
        return d;
}

static PixelFormat::Data makeJPEG_RGB8() {
        PixelFormat::Data d;
        d.id = PixelFormat::JPEG_RGB8_sRGB;
        d.name = "JPEG_RGB8_sRGB";
        d.desc = "JPEG-compressed 8-bit RGB";
        d.memLayout = PixelMemLayout(PixelMemLayout::I_3x8);
        d.colorModel = ColorModel(ColorModel::sRGB);
        d.compressed = true;
        d.videoCodec = VideoCodec(VideoCodec::JPEG);
        // Only formats from the natural RGB family belong here — see
        // JpegVideoEncoder, which tags the output based on the
        // input component order.  A different family (e.g. RGBA or YUV)
        // as input would produce a different JPEG sub-format and
        // contradict this PixelFormat's identity.
        d.encodeSources = {PixelFormat::RGB8_sRGB};
        d.decodeTargets = {PixelFormat::RGB8_sRGB};
        d.fourccList = {"JPEG", "mjpa", "mjpb", "mjpg", "AVRn", "AVDJ", "ADJV"};
        d.compSemantics[0] = {"Red", "R", 0, 255};
        d.compSemantics[1] = {"Green", "G", 0, 255};
        d.compSemantics[2] = {"Blue", "B", 0, 255};
        return d;
}

// ---------------------------------------------------------------------------
// JPEG YCbCr factory helper
// ---------------------------------------------------------------------------
//
// Builds one entry from the full complement of JPEG YCbCr variants
// (matrix × range × subsampling).  Every variant has the same
// general shape — a compressed PixelFormat with the "JPEG" codec, a
// matching colour model, and encodeSources / decodeTargets lists
// drawn from the uncompressed YCbCr family that matches its own
// (matrix, range) pair — so a single helper keeps the eight
// definitions (2 subsampling × 2 matrix × 2 range) short and
// consistent.  The helper sets compSemantics and a default
// descriptor string automatically so all variants stay in sync.
struct JpegYuvEntry {
                PixelFormat::ID       id;
                const char           *name;
                PixelMemLayout::ID    memLayout;  // I_422_3x8 or P_420_3x8
                ColorModel::ID        colorModel; // YCbCr_Rec709 or YCbCr_Rec601
                bool                  limited;    // true = [16..235]/[16..240], false = [0..255]
                bool                  is420;      // true = 4:2:0, false = 4:2:2
                List<PixelFormat::ID> encodeSources;
                List<PixelFormat::ID> decodeTargets;
};

static PixelFormat::Data makeJPEG_YUV(const JpegYuvEntry &e) {
        PixelFormat::Data d;
        d.id = e.id;
        d.name = e.name;
        // Build a short, consistent description string.
        const char *matrixName = (e.colorModel == ColorModel::YCbCr_Rec709) ? "Rec.709" : "Rec.601";
        const char *rangeName = e.limited ? "limited range" : "full range";
        const char *subsampling = e.is420 ? "4:2:0" : "4:2:2";
        d.desc = String("JPEG-compressed 8-bit YCbCr ") + String(subsampling) + String(" (") + String(matrixName) +
                 String(" matrix, ") + String(rangeName) + String(")");
        d.memLayout = PixelMemLayout(e.memLayout);
        d.colorModel = ColorModel(e.colorModel);
        d.compressed = true;
        d.videoCodec = VideoCodec(VideoCodec::JPEG);
        d.encodeSources = e.encodeSources;
        d.decodeTargets = e.decodeTargets;
        d.fourccList = e.is420 ? List<FourCC>{"JPEG", "mjpg"}
                               : List<FourCC>{"JPEG", "mjpa", "mjpb", "mjpg", "AVRn", "AVDJ", "ADJV"};

        if (e.limited) {
                d.compSemantics[0] = {"Luma", "Y", 16, 235};
                d.compSemantics[1] = {"Chroma Blue", "Cb", 16, 240};
                d.compSemantics[2] = {"Chroma Red", "Cr", 16, 240};
        } else {
                // Full range per JFIF convention.  libjpeg writes the
                // input bytes verbatim into the JPEG bitstream, so the
                // comp semantics must match the byte range the CSC
                // pipeline produces on the input side.  A JFIF-assuming
                // decoder (ffplay, browsers, libjpeg-turbo) interprets
                // the decoded bytes as full range regardless — using
                // limited range here would make black (Y=16) display as
                // dark grey on the receiver.
                d.compSemantics[0] = {"Luma", "Y", 0, 255};
                d.compSemantics[1] = {"Chroma Blue", "Cb", 0, 255};
                d.compSemantics[2] = {"Chroma Red", "Cr", 0, 255};
        }
        return d;
}

// -- Rec.709 limited (the default YCbCr convention — existing IDs) --

static PixelFormat::Data makeJPEG_YUV8_422_Rec709() {
        return makeJPEG_YUV(
                {PixelFormat::JPEG_YUV8_422_Rec709,
                 "JPEG_YUV8_422_Rec709",
                 PixelMemLayout::I_422_3x8,
                 ColorModel::YCbCr_Rec709,
                 /*limited*/ true,
                 /*is420*/ false,
                 {PixelFormat::YUV8_422_Rec709, PixelFormat::YUV8_422_UYVY_Rec709, PixelFormat::YUV8_422_Planar_Rec709},
                 {PixelFormat::YUV8_422_Rec709, PixelFormat::YUV8_422_UYVY_Rec709, PixelFormat::YUV8_422_Planar_Rec709,
                  PixelFormat::RGB8_sRGB, PixelFormat::RGBA8_sRGB}});
}

static PixelFormat::Data makeJPEG_YUV8_420_Rec709() {
        return makeJPEG_YUV({PixelFormat::JPEG_YUV8_420_Rec709,
                             "JPEG_YUV8_420_Rec709",
                             PixelMemLayout::P_420_3x8,
                             ColorModel::YCbCr_Rec709,
                             /*limited*/ true,
                             /*is420*/ true,
                             {PixelFormat::YUV8_420_Planar_Rec709, PixelFormat::YUV8_420_SemiPlanar_Rec709},
                             {PixelFormat::YUV8_420_Planar_Rec709, PixelFormat::YUV8_420_SemiPlanar_Rec709,
                              PixelFormat::YUV8_422_UYVY_Rec709, PixelFormat::YUV8_422_Rec709, PixelFormat::RGB8_sRGB,
                              PixelFormat::RGBA8_sRGB}});
}

// -- Rec.601 limited --

static PixelFormat::Data makeJPEG_YUV8_422_Rec601() {
        return makeJPEG_YUV({PixelFormat::JPEG_YUV8_422_Rec601,
                             "JPEG_YUV8_422_Rec601",
                             PixelMemLayout::I_422_3x8,
                             ColorModel::YCbCr_Rec601,
                             /*limited*/ true,
                             /*is420*/ false,
                             {PixelFormat::YUV8_422_Rec601, PixelFormat::YUV8_422_UYVY_Rec601},
                             {PixelFormat::YUV8_422_Rec601, PixelFormat::YUV8_422_UYVY_Rec601, PixelFormat::RGB8_sRGB,
                              PixelFormat::RGBA8_sRGB}});
}

static PixelFormat::Data makeJPEG_YUV8_420_Rec601() {
        return makeJPEG_YUV({PixelFormat::JPEG_YUV8_420_Rec601,
                             "JPEG_YUV8_420_Rec601",
                             PixelMemLayout::P_420_3x8,
                             ColorModel::YCbCr_Rec601,
                             /*limited*/ true,
                             /*is420*/ true,
                             {PixelFormat::YUV8_420_Planar_Rec601, PixelFormat::YUV8_420_SemiPlanar_Rec601},
                             {PixelFormat::YUV8_420_Planar_Rec601, PixelFormat::YUV8_420_SemiPlanar_Rec601,
                              PixelFormat::RGB8_sRGB, PixelFormat::RGBA8_sRGB}});
}

// -- Rec.709 full (encode sources are the new full-range uncompressed intermediates) --

static PixelFormat::Data makeJPEG_YUV8_422_Rec709_Full() {
        return makeJPEG_YUV({PixelFormat::JPEG_YUV8_422_Rec709_Full,
                             "JPEG_YUV8_422_Rec709_Full",
                             PixelMemLayout::I_422_3x8,
                             ColorModel::YCbCr_Rec709,
                             /*limited*/ false,
                             /*is420*/ false,
                             {PixelFormat::YUV8_422_Rec709_Full},
                             {PixelFormat::YUV8_422_Rec709_Full, PixelFormat::RGB8_sRGB, PixelFormat::RGBA8_sRGB}});
}

static PixelFormat::Data makeJPEG_YUV8_420_Rec709_Full() {
        return makeJPEG_YUV(
                {PixelFormat::JPEG_YUV8_420_Rec709_Full,
                 "JPEG_YUV8_420_Rec709_Full",
                 PixelMemLayout::P_420_3x8,
                 ColorModel::YCbCr_Rec709,
                 /*limited*/ false,
                 /*is420*/ true,
                 {PixelFormat::YUV8_420_Planar_Rec709_Full},
                 {PixelFormat::YUV8_420_Planar_Rec709_Full, PixelFormat::RGB8_sRGB, PixelFormat::RGBA8_sRGB}});
}

// -- Rec.601 full (the strict JFIF-compatible variants) --

static PixelFormat::Data makeJPEG_YUV8_422_Rec601_Full() {
        return makeJPEG_YUV({PixelFormat::JPEG_YUV8_422_Rec601_Full,
                             "JPEG_YUV8_422_Rec601_Full",
                             PixelMemLayout::I_422_3x8,
                             ColorModel::YCbCr_Rec601,
                             /*limited*/ false,
                             /*is420*/ false,
                             {PixelFormat::YUV8_422_Rec601_Full},
                             {PixelFormat::YUV8_422_Rec601_Full, PixelFormat::RGB8_sRGB, PixelFormat::RGBA8_sRGB}});
}

static PixelFormat::Data makeJPEG_YUV8_420_Rec601_Full() {
        return makeJPEG_YUV(
                {PixelFormat::JPEG_YUV8_420_Rec601_Full,
                 "JPEG_YUV8_420_Rec601_Full",
                 PixelMemLayout::P_420_3x8,
                 ColorModel::YCbCr_Rec601,
                 /*limited*/ false,
                 /*is420*/ true,
                 {PixelFormat::YUV8_420_Planar_Rec601_Full},
                 {PixelFormat::YUV8_420_Planar_Rec601_Full, PixelFormat::RGB8_sRGB, PixelFormat::RGBA8_sRGB}});
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
// carriage expects.  Bit depth drives the memLayout choice
// (P_XXX_3x8 for 8-bit, P_XXX_3x10_LE for 10-bit, etc.) and the
// CompSemantic ranges.
struct JpegXsYuvEntry {
                PixelFormat::ID       id;
                const char           *name;
                PixelMemLayout::ID    memLayout; // P_422_3x8 / P_422_3x10_LE / ... / P_420_*
                int                   bitDepth;  // 8, 10, or 12
                bool                  is420;
                List<PixelFormat::ID> encodeSources;
                List<PixelFormat::ID> decodeTargets;
};

static PixelFormat::Data makeJPEG_XS_YUV(const JpegXsYuvEntry &e) {
        PixelFormat::Data d;
        d.id = e.id;
        d.name = e.name;
        const char *subsampling = e.is420 ? "4:2:0" : "4:2:2";
        d.desc = String("JPEG XS-compressed ") + String::number(e.bitDepth) + String("-bit YCbCr ") +
                 String(subsampling) + String(" (Rec.709, limited range)");
        d.memLayout = PixelMemLayout(e.memLayout);
        d.colorModel = ColorModel(ColorModel::YCbCr_Rec709);
        d.compressed = true;
        d.videoCodec = VideoCodec(VideoCodec::JPEG_XS);
        d.encodeSources = e.encodeSources;
        d.decodeTargets = e.decodeTargets;
        // JPEG XS ISOBMFF sample entry is "jxsm" (ISO/IEC 21122-3).
        d.fourccList = {"jxsm"};
        // Component ranges scale with bit depth, matching the other
        // limited-range YCbCr descriptors in this file.
        switch (e.bitDepth) {
                case 8:
                        d.compSemantics[0] = {"Luma", "Y", 16, 235};
                        d.compSemantics[1] = {"Chroma Blue", "Cb", 16, 240};
                        d.compSemantics[2] = {"Chroma Red", "Cr", 16, 240};
                        break;
                case 10:
                        d.compSemantics[0] = {"Luma", "Y", 64, 940};
                        d.compSemantics[1] = {"Chroma Blue", "Cb", 64, 960};
                        d.compSemantics[2] = {"Chroma Red", "Cr", 64, 960};
                        break;
                case 12:
                        d.compSemantics[0] = {"Luma", "Y", 256, 3760};
                        d.compSemantics[1] = {"Chroma Blue", "Cb", 256, 3840};
                        d.compSemantics[2] = {"Chroma Red", "Cr", 256, 3840};
                        break;
        }
        return d;
}

static PixelFormat::Data makeJPEG_XS_YUV8_422_Rec709() {
        return makeJPEG_XS_YUV({PixelFormat::JPEG_XS_YUV8_422_Rec709,
                                "JPEG_XS_YUV8_422_Rec709",
                                PixelMemLayout::P_422_3x8,
                                8,
                                /*is420*/ false,
                                {PixelFormat::YUV8_422_Planar_Rec709},
                                {PixelFormat::YUV8_422_Planar_Rec709}});
}

static PixelFormat::Data makeJPEG_XS_YUV10_422_Rec709() {
        return makeJPEG_XS_YUV({PixelFormat::JPEG_XS_YUV10_422_Rec709,
                                "JPEG_XS_YUV10_422_Rec709",
                                PixelMemLayout::P_422_3x10_LE,
                                10,
                                /*is420*/ false,
                                {PixelFormat::YUV10_422_Planar_LE_Rec709},
                                {PixelFormat::YUV10_422_Planar_LE_Rec709}});
}

static PixelFormat::Data makeJPEG_XS_YUV12_422_Rec709() {
        return makeJPEG_XS_YUV({PixelFormat::JPEG_XS_YUV12_422_Rec709,
                                "JPEG_XS_YUV12_422_Rec709",
                                PixelMemLayout::P_422_3x12_LE,
                                12,
                                /*is420*/ false,
                                {PixelFormat::YUV12_422_Planar_LE_Rec709},
                                {PixelFormat::YUV12_422_Planar_LE_Rec709}});
}

static PixelFormat::Data makeJPEG_XS_YUV8_420_Rec709() {
        return makeJPEG_XS_YUV({PixelFormat::JPEG_XS_YUV8_420_Rec709,
                                "JPEG_XS_YUV8_420_Rec709",
                                PixelMemLayout::P_420_3x8,
                                8,
                                /*is420*/ true,
                                {PixelFormat::YUV8_420_Planar_Rec709},
                                {PixelFormat::YUV8_420_Planar_Rec709}});
}

static PixelFormat::Data makeJPEG_XS_YUV10_420_Rec709() {
        return makeJPEG_XS_YUV({PixelFormat::JPEG_XS_YUV10_420_Rec709,
                                "JPEG_XS_YUV10_420_Rec709",
                                PixelMemLayout::P_420_3x10_LE,
                                10,
                                /*is420*/ true,
                                {PixelFormat::YUV10_420_Planar_LE_Rec709},
                                {PixelFormat::YUV10_420_Planar_LE_Rec709}});
}

static PixelFormat::Data makeJPEG_XS_YUV12_420_Rec709() {
        return makeJPEG_XS_YUV({PixelFormat::JPEG_XS_YUV12_420_Rec709,
                                "JPEG_XS_YUV12_420_Rec709",
                                PixelMemLayout::P_420_3x12_LE,
                                12,
                                /*is420*/ true,
                                {PixelFormat::YUV12_420_Planar_LE_Rec709},
                                {PixelFormat::YUV12_420_Planar_LE_Rec709}});
}

static PixelFormat::Data makeJPEG_XS_RGB8_sRGB() {
        PixelFormat::Data d;
        d.id = PixelFormat::JPEG_XS_RGB8_sRGB;
        d.name = "JPEG_XS_RGB8_sRGB";
        d.desc = "JPEG XS-compressed 8-bit RGB, sRGB, full range";
        d.memLayout = PixelMemLayout(PixelMemLayout::I_3x8);
        d.colorModel = ColorModel(ColorModel::sRGB);
        d.compressed = true;
        d.videoCodec = VideoCodec(VideoCodec::JPEG_XS);
        d.encodeSources = {PixelFormat::RGB8_Planar_sRGB};
        d.decodeTargets = {PixelFormat::RGB8_Planar_sRGB};
        d.fourccList = {"jxsm"};
        d.compSemantics[0] = {"Red", "R", 0, 255};
        d.compSemantics[1] = {"Green", "G", 0, 255};
        d.compSemantics[2] = {"Blue", "B", 0, 255};
        return d;
}

static PixelFormat::Data makeRGB8_Planar_sRGB() {
        PixelFormat::Data d;
        d.id = PixelFormat::RGB8_Planar_sRGB;
        d.name = "RGB8_Planar_sRGB";
        d.desc = "8-bit planar RGB (3 equal-sized planes: R, G, B), sRGB, full range";
        d.memLayout = PixelMemLayout(PixelMemLayout::P_444_3x8);
        d.colorModel = ColorModel(ColorModel::sRGB);
        d.compSemantics[0] = {"Red", "R", 0, 255};
        d.compSemantics[1] = {"Green", "G", 0, 255};
        d.compSemantics[2] = {"Blue", "B", 0, 255};
        return d;
}

// ---------------------------------------------------------------------------
// Legacy single-entry wrappers used by the registry init block
// ---------------------------------------------------------------------------

static PixelFormat::Data makeJPEG_YUV8_422() {
        return makeJPEG_YUV8_422_Rec709();
}

static PixelFormat::Data makeJPEG_YUV8_420() {
        return makeJPEG_YUV8_420_Rec709();
}

static PixelFormat::Data makeYUV8_422_UYVY() {
        PixelFormat::Data d;
        d.id = PixelFormat::YUV8_422_UYVY_Rec709;
        d.name = "YUV8_422_UYVY_Rec709";
        d.desc = "8-bit YCbCr 4:2:2 UYVY, Rec.709, limited range";
        d.memLayout = PixelMemLayout(PixelMemLayout::I_422_UYVY_3x8);
        d.colorModel = ColorModel(ColorModel::YCbCr_Rec709);
        // "2vuy" — QuickTime sample-entry FourCC for 8-bit 4:2:2 UYVY (canonical).
        // "UYVY" — generic / AVI / FFmpeg name for the same byte layout.
        // The first entry is the writer-preferred FourCC; QuickTime files
        // written by us emit "2vuy".
        d.fourccList = {"2vuy", "UYVY"};
        d.compSemantics[0] = {"Luma", "Y", 16, 235};
        d.compSemantics[1] = {"Chroma Blue", "Cb", 16, 240};
        d.compSemantics[2] = {"Chroma Red", "Cr", 16, 240};
        return d;
}

static PixelFormat::Data makeYUV10_422_UYVY_LE() {
        PixelFormat::Data d;
        d.id = PixelFormat::YUV10_422_UYVY_LE_Rec709;
        d.name = "YUV10_422_UYVY_LE_Rec709";
        d.desc = "10-bit YCbCr 4:2:2 UYVY LE, Rec.709, limited range";
        d.memLayout = PixelMemLayout(PixelMemLayout::I_422_UYVY_3x10_LE);
        d.colorModel = ColorModel(ColorModel::YCbCr_Rec709);
        d.compSemantics[0] = {"Luma", "Y", 64, 940};
        d.compSemantics[1] = {"Chroma Blue", "Cb", 64, 960};
        d.compSemantics[2] = {"Chroma Red", "Cr", 64, 960};
        return d;
}

static PixelFormat::Data makeYUV10_422_UYVY_BE() {
        PixelFormat::Data d;
        d.id = PixelFormat::YUV10_422_UYVY_BE_Rec709;
        d.name = "YUV10_422_UYVY_BE_Rec709";
        d.desc = "10-bit YCbCr 4:2:2 UYVY BE, Rec.709, limited range";
        d.memLayout = PixelMemLayout(PixelMemLayout::I_422_UYVY_3x10_BE);
        d.colorModel = ColorModel(ColorModel::YCbCr_Rec709);
        d.compSemantics[0] = {"Luma", "Y", 64, 940};
        d.compSemantics[1] = {"Chroma Blue", "Cb", 64, 960};
        d.compSemantics[2] = {"Chroma Red", "Cr", 64, 960};
        return d;
}

static PixelFormat::Data makeYUV12_422_UYVY_LE() {
        PixelFormat::Data d;
        d.id = PixelFormat::YUV12_422_UYVY_LE_Rec709;
        d.name = "YUV12_422_UYVY_LE_Rec709";
        d.desc = "12-bit YCbCr 4:2:2 UYVY LE, Rec.709, limited range";
        d.memLayout = PixelMemLayout(PixelMemLayout::I_422_UYVY_3x12_LE);
        d.colorModel = ColorModel(ColorModel::YCbCr_Rec709);
        d.compSemantics[0] = {"Luma", "Y", 256, 3760};
        d.compSemantics[1] = {"Chroma Blue", "Cb", 256, 3840};
        d.compSemantics[2] = {"Chroma Red", "Cr", 256, 3840};
        return d;
}

static PixelFormat::Data makeYUV12_422_UYVY_BE() {
        PixelFormat::Data d;
        d.id = PixelFormat::YUV12_422_UYVY_BE_Rec709;
        d.name = "YUV12_422_UYVY_BE_Rec709";
        d.desc = "12-bit YCbCr 4:2:2 UYVY BE, Rec.709, limited range";
        d.memLayout = PixelMemLayout(PixelMemLayout::I_422_UYVY_3x12_BE);
        d.colorModel = ColorModel(ColorModel::YCbCr_Rec709);
        d.compSemantics[0] = {"Luma", "Y", 256, 3760};
        d.compSemantics[1] = {"Chroma Blue", "Cb", 256, 3840};
        d.compSemantics[2] = {"Chroma Red", "Cr", 256, 3840};
        return d;
}

static PixelFormat::Data makeYUV10_422_v210() {
        PixelFormat::Data d;
        d.id = PixelFormat::YUV10_422_v210_Rec709;
        d.name = "YUV10_422_v210_Rec709";
        d.desc = "10-bit YCbCr 4:2:2 v210 packed, Rec.709, limited range";
        d.memLayout = PixelMemLayout(PixelMemLayout::I_422_v210);
        d.colorModel = ColorModel(ColorModel::YCbCr_Rec709);
        d.fourccList = {"v210"};
        d.compSemantics[0] = {"Luma", "Y", 64, 940};
        d.compSemantics[1] = {"Chroma Blue", "Cb", 64, 960};
        d.compSemantics[2] = {"Chroma Red", "Cr", 64, 960};
        return d;
}

// ---------------------------------------------------------------------------
// Planar 4:2:2 PixelFormat factory functions
// ---------------------------------------------------------------------------

static const PixelFormat::CompSemantic ycbcrSem8[] = {
        {"Luma", "Y", 16, 235}, {"Chroma Blue", "Cb", 16, 240}, {"Chroma Red", "Cr", 16, 240}};
static const PixelFormat::CompSemantic ycbcrSem10[] = {
        {"Luma", "Y", 64, 940}, {"Chroma Blue", "Cb", 64, 960}, {"Chroma Red", "Cr", 64, 960}};
static const PixelFormat::CompSemantic ycbcrSem12[] = {
        {"Luma", "Y", 256, 3760}, {"Chroma Blue", "Cb", 256, 3840}, {"Chroma Red", "Cr", 256, 3840}};
static const PixelFormat::CompSemantic ycbcrSem16[] = {
        {"Luma", "Y", 4096, 60160}, {"Chroma Blue", "Cb", 4096, 61440}, {"Chroma Red", "Cr", 4096, 61440}};

// Full-range (0..255 / 0..1023 / 0..4095 / 0..65535) YCbCr comp
// semantics.  The library-wide YCbCr default is limited range (the
// unsuffixed sem arrays above) because broadcast / SDI / ST 2110
// pipelines are overwhelmingly limited-range.  These "_Full"
// variants are the explicit full-range opt-in, used by JPEG / JFIF
// interop paths and by any future pipeline that needs a full-range
// YCbCr intermediate (e.g. a CSC fast-path into a JFIF-compatible
// JPEG encode).
static const PixelFormat::CompSemantic ycbcrSem8Full[] = {
        {"Luma", "Y", 0, 255}, {"Chroma Blue", "Cb", 0, 255}, {"Chroma Red", "Cr", 0, 255}};

static PixelFormat::Data makeYCbCrDesc(PixelFormat::ID id, const char *name, const char *desc, PixelMemLayout::ID pfId,
                                       const PixelFormat::CompSemantic *sem) {
        PixelFormat::Data d;
        d.id = id;
        d.name = name;
        d.desc = desc;
        d.memLayout = PixelMemLayout(pfId);
        d.colorModel = ColorModel(ColorModel::YCbCr_Rec709);
        d.compSemantics[0] = sem[0];
        d.compSemantics[1] = sem[1];
        d.compSemantics[2] = sem[2];
        return d;
}

static PixelFormat::Data makeYCbCrDescWithModel(PixelFormat::ID id, const char *name, const char *desc,
                                                PixelMemLayout::ID pfId, const PixelFormat::CompSemantic *sem,
                                                ColorModel::ID colorModelId) {
        PixelFormat::Data d;
        d.id = id;
        d.name = name;
        d.desc = desc;
        d.memLayout = PixelMemLayout(pfId);
        d.colorModel = ColorModel(colorModelId);
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
// for the full-range JPEG PixelFormats so the CSC pipeline can land
// in a byte range that matches what the JPEG codec will write
// verbatim into the JFIF bitstream.  General pipeline code is
// also free to use them any time a full-range YCbCr
// representation is more appropriate than the broadcast default.

static PixelFormat::Data makeYUV8_422_Rec709_Full() {
        return makeYCbCrDescWithModel(PixelFormat::YUV8_422_Rec709_Full, "YUV8_422_Rec709_Full",
                                      "8-bit YCbCr 4:2:2 YUYV, Rec.709, full range", PixelMemLayout::I_422_3x8,
                                      ycbcrSem8Full, ColorModel::YCbCr_Rec709);
}

static PixelFormat::Data makeYUV8_422_Rec601_Full() {
        return makeYCbCrDescWithModel(PixelFormat::YUV8_422_Rec601_Full, "YUV8_422_Rec601_Full",
                                      "8-bit YCbCr 4:2:2 YUYV, Rec.601, full range", PixelMemLayout::I_422_3x8,
                                      ycbcrSem8Full, ColorModel::YCbCr_Rec601);
}

static PixelFormat::Data makeYUV8_420_Planar_Rec709_Full() {
        return makeYCbCrDescWithModel(PixelFormat::YUV8_420_Planar_Rec709_Full, "YUV8_420_Planar_Rec709_Full",
                                      "8-bit YCbCr 4:2:0 planar, Rec.709, full range", PixelMemLayout::P_420_3x8,
                                      ycbcrSem8Full, ColorModel::YCbCr_Rec709);
}

static PixelFormat::Data makeYUV8_420_Planar_Rec601_Full() {
        return makeYCbCrDescWithModel(PixelFormat::YUV8_420_Planar_Rec601_Full, "YUV8_420_Planar_Rec601_Full",
                                      "8-bit YCbCr 4:2:0 planar, Rec.601, full range", PixelMemLayout::P_420_3x8,
                                      ycbcrSem8Full, ColorModel::YCbCr_Rec601);
}

static PixelFormat::Data makeBGRDesc(PixelFormat::ID id, const char *name, const char *desc, PixelMemLayout::ID pfId,
                                     bool alpha, float rangeMax) {
        PixelFormat::Data d;
        d.id = id;
        d.name = name;
        d.desc = desc;
        d.memLayout = PixelMemLayout(pfId);
        d.colorModel = ColorModel(ColorModel::sRGB);
        d.hasAlpha = alpha;
        d.alphaCompIndex = alpha ? 3 : -1;
        d.compSemantics[0] = {"Blue", "B", 0, rangeMax};
        d.compSemantics[1] = {"Green", "G", 0, rangeMax};
        d.compSemantics[2] = {"Red", "R", 0, rangeMax};
        if (alpha) d.compSemantics[3] = {"Alpha", "A", 0, rangeMax};
        return d;
}

static PixelFormat::Data makeARGBDesc(PixelFormat::ID id, const char *name, const char *desc, PixelMemLayout::ID pfId,
                                      float rangeMax) {
        PixelFormat::Data d;
        d.id = id;
        d.name = name;
        d.desc = desc;
        d.memLayout = PixelMemLayout(pfId);
        d.colorModel = ColorModel(ColorModel::sRGB);
        d.hasAlpha = true;
        d.alphaCompIndex = 0;
        d.compSemantics[0] = {"Alpha", "A", 0, rangeMax};
        d.compSemantics[1] = {"Red", "R", 0, rangeMax};
        d.compSemantics[2] = {"Green", "G", 0, rangeMax};
        d.compSemantics[3] = {"Blue", "B", 0, rangeMax};
        return d;
}

static PixelFormat::Data makeABGRDesc(PixelFormat::ID id, const char *name, const char *desc, PixelMemLayout::ID pfId,
                                      float rangeMax) {
        PixelFormat::Data d;
        d.id = id;
        d.name = name;
        d.desc = desc;
        d.memLayout = PixelMemLayout(pfId);
        d.colorModel = ColorModel(ColorModel::sRGB);
        d.hasAlpha = true;
        d.alphaCompIndex = 0;
        d.compSemantics[0] = {"Alpha", "A", 0, rangeMax};
        d.compSemantics[1] = {"Blue", "B", 0, rangeMax};
        d.compSemantics[2] = {"Green", "G", 0, rangeMax};
        d.compSemantics[3] = {"Red", "R", 0, rangeMax};
        return d;
}

static PixelFormat::Data makeMonoDesc(PixelFormat::ID id, const char *name, const char *desc, PixelMemLayout::ID pfId,
                                      ColorModel::ID colorModelId, float rangeMax) {
        PixelFormat::Data d;
        d.id = id;
        d.name = name;
        d.desc = desc;
        d.memLayout = PixelMemLayout(pfId);
        d.colorModel = ColorModel(colorModelId);
        d.compSemantics[0] = {"Luminance", "L", 0, rangeMax};
        return d;
}

static PixelFormat::Data makeFloatRGBDesc(PixelFormat::ID id, const char *name, const char *desc,
                                          PixelMemLayout::ID pfId, bool alpha, ColorModel::ID colorModelId) {
        PixelFormat::Data d;
        d.id = id;
        d.name = name;
        d.desc = desc;
        d.memLayout = PixelMemLayout(pfId);
        d.colorModel = ColorModel(colorModelId);
        d.hasAlpha = alpha;
        d.alphaCompIndex = alpha ? 3 : -1;
        d.compSemantics[0] = {"Red", "R", 0.0, 1.0};
        d.compSemantics[1] = {"Green", "G", 0.0, 1.0};
        d.compSemantics[2] = {"Blue", "B", 0.0, 1.0};
        if (alpha) d.compSemantics[3] = {"Alpha", "A", 0.0, 1.0};
        return d;
}

static PixelFormat::Data makeYUV8_422_Planar() {
        auto d =
                makeYCbCrDesc(PixelFormat::YUV8_422_Planar_Rec709, "YUV8_422_Planar_Rec709",
                              "8-bit YCbCr 4:2:2 planar, Rec.709, limited range", PixelMemLayout::P_422_3x8, ycbcrSem8);
        d.fourccList = {"I422"};
        return d;
}

static PixelFormat::Data makeYUV10_422_Planar_LE() {
        return makeYCbCrDesc(PixelFormat::YUV10_422_Planar_LE_Rec709, "YUV10_422_Planar_LE_Rec709",
                             "10-bit YCbCr 4:2:2 planar LE, Rec.709, limited range", PixelMemLayout::P_422_3x10_LE,
                             ycbcrSem10);
}

static PixelFormat::Data makeYUV10_422_Planar_BE() {
        return makeYCbCrDesc(PixelFormat::YUV10_422_Planar_BE_Rec709, "YUV10_422_Planar_BE_Rec709",
                             "10-bit YCbCr 4:2:2 planar BE, Rec.709, limited range", PixelMemLayout::P_422_3x10_BE,
                             ycbcrSem10);
}

static PixelFormat::Data makeYUV12_422_Planar_LE() {
        return makeYCbCrDesc(PixelFormat::YUV12_422_Planar_LE_Rec709, "YUV12_422_Planar_LE_Rec709",
                             "12-bit YCbCr 4:2:2 planar LE, Rec.709, limited range", PixelMemLayout::P_422_3x12_LE,
                             ycbcrSem12);
}

static PixelFormat::Data makeYUV12_422_Planar_BE() {
        return makeYCbCrDesc(PixelFormat::YUV12_422_Planar_BE_Rec709, "YUV12_422_Planar_BE_Rec709",
                             "12-bit YCbCr 4:2:2 planar BE, Rec.709, limited range", PixelMemLayout::P_422_3x12_BE,
                             ycbcrSem12);
}

// ---------------------------------------------------------------------------
// Planar 4:2:0 PixelFormat factory functions
// ---------------------------------------------------------------------------

static PixelFormat::Data makeYUV8_420_Planar() {
        auto d =
                makeYCbCrDesc(PixelFormat::YUV8_420_Planar_Rec709, "YUV8_420_Planar_Rec709",
                              "8-bit YCbCr 4:2:0 planar, Rec.709, limited range", PixelMemLayout::P_420_3x8, ycbcrSem8);
        d.fourccList = {"I420"};
        return d;
}

static PixelFormat::Data makeYUV10_420_Planar_LE() {
        return makeYCbCrDesc(PixelFormat::YUV10_420_Planar_LE_Rec709, "YUV10_420_Planar_LE_Rec709",
                             "10-bit YCbCr 4:2:0 planar LE, Rec.709, limited range", PixelMemLayout::P_420_3x10_LE,
                             ycbcrSem10);
}

static PixelFormat::Data makeYUV10_420_Planar_BE() {
        return makeYCbCrDesc(PixelFormat::YUV10_420_Planar_BE_Rec709, "YUV10_420_Planar_BE_Rec709",
                             "10-bit YCbCr 4:2:0 planar BE, Rec.709, limited range", PixelMemLayout::P_420_3x10_BE,
                             ycbcrSem10);
}

static PixelFormat::Data makeYUV12_420_Planar_LE() {
        return makeYCbCrDesc(PixelFormat::YUV12_420_Planar_LE_Rec709, "YUV12_420_Planar_LE_Rec709",
                             "12-bit YCbCr 4:2:0 planar LE, Rec.709, limited range", PixelMemLayout::P_420_3x12_LE,
                             ycbcrSem12);
}

static PixelFormat::Data makeYUV12_420_Planar_BE() {
        return makeYCbCrDesc(PixelFormat::YUV12_420_Planar_BE_Rec709, "YUV12_420_Planar_BE_Rec709",
                             "12-bit YCbCr 4:2:0 planar BE, Rec.709, limited range", PixelMemLayout::P_420_3x12_BE,
                             ycbcrSem12);
}

// ---------------------------------------------------------------------------
// Semi-planar 4:2:0 (NV12) PixelFormat factory functions
// ---------------------------------------------------------------------------

static PixelFormat::Data makeYUV8_420_SemiPlanar() {
        auto d = makeYCbCrDesc(PixelFormat::YUV8_420_SemiPlanar_Rec709, "YUV8_420_SemiPlanar_Rec709",
                               "8-bit YCbCr 4:2:0 NV12, Rec.709, limited range", PixelMemLayout::SP_420_8, ycbcrSem8);
        d.fourccList = {"NV12"};
        return d;
}

static PixelFormat::Data makeYUV10_420_SemiPlanar_LE() {
        return makeYCbCrDesc(PixelFormat::YUV10_420_SemiPlanar_LE_Rec709, "YUV10_420_SemiPlanar_LE_Rec709",
                             "10-bit YCbCr 4:2:0 NV12 LE, Rec.709, limited range", PixelMemLayout::SP_420_10_LE,
                             ycbcrSem10);
}

static PixelFormat::Data makeYUV10_420_SemiPlanar_BE() {
        return makeYCbCrDesc(PixelFormat::YUV10_420_SemiPlanar_BE_Rec709, "YUV10_420_SemiPlanar_BE_Rec709",
                             "10-bit YCbCr 4:2:0 NV12 BE, Rec.709, limited range", PixelMemLayout::SP_420_10_BE,
                             ycbcrSem10);
}

static PixelFormat::Data makeYUV12_420_SemiPlanar_LE() {
        return makeYCbCrDesc(PixelFormat::YUV12_420_SemiPlanar_LE_Rec709, "YUV12_420_SemiPlanar_LE_Rec709",
                             "12-bit YCbCr 4:2:0 NV12 LE, Rec.709, limited range", PixelMemLayout::SP_420_12_LE,
                             ycbcrSem12);
}

static PixelFormat::Data makeYUV12_420_SemiPlanar_BE() {
        return makeYCbCrDesc(PixelFormat::YUV12_420_SemiPlanar_BE_Rec709, "YUV12_420_SemiPlanar_BE_Rec709",
                             "12-bit YCbCr 4:2:0 NV12 BE, Rec.709, limited range", PixelMemLayout::SP_420_12_BE,
                             ycbcrSem12);
}

// ---------------------------------------------------------------------------
// RGB/RGBA 10/12/16-bit PixelFormat factory functions
// ---------------------------------------------------------------------------

static PixelFormat::Data makeRGBDesc(PixelFormat::ID id, const char *name, const char *desc, PixelMemLayout::ID pfId,
                                     bool alpha, float rangeMax) {
        PixelFormat::Data d;
        d.id = id;
        d.name = name;
        d.desc = desc;
        d.memLayout = PixelMemLayout(pfId);
        d.colorModel = ColorModel(ColorModel::sRGB);
        d.hasAlpha = alpha;
        d.alphaCompIndex = alpha ? 3 : -1;
        d.compSemantics[0] = {"Red", "R", 0, rangeMax};
        d.compSemantics[1] = {"Green", "G", 0, rangeMax};
        d.compSemantics[2] = {"Blue", "B", 0, rangeMax};
        if (alpha) d.compSemantics[3] = {"Alpha", "A", 0, rangeMax};
        return d;
}

static PixelFormat::Data makeRGBA10_LE() {
        return makeRGBDesc(PixelFormat::RGBA10_LE_sRGB, "RGBA10_LE_sRGB",
                           "10-bit RGBA in 16-bit LE words, sRGB, full range", PixelMemLayout::I_4x10_LE, true, 1023);
}

static PixelFormat::Data makeRGBA10_BE() {
        return makeRGBDesc(PixelFormat::RGBA10_BE_sRGB, "RGBA10_BE_sRGB",
                           "10-bit RGBA in 16-bit BE words, sRGB, full range", PixelMemLayout::I_4x10_BE, true, 1023);
}

static PixelFormat::Data makeRGB10_LE() {
        return makeRGBDesc(PixelFormat::RGB10_LE_sRGB, "RGB10_LE_sRGB",
                           "10-bit RGB in 16-bit LE words, sRGB, full range", PixelMemLayout::I_3x10_LE, false, 1023);
}

static PixelFormat::Data makeRGB10_BE() {
        return makeRGBDesc(PixelFormat::RGB10_BE_sRGB, "RGB10_BE_sRGB",
                           "10-bit RGB in 16-bit BE words, sRGB, full range", PixelMemLayout::I_3x10_BE, false, 1023);
}

static PixelFormat::Data makeRGBA12_LE() {
        return makeRGBDesc(PixelFormat::RGBA12_LE_sRGB, "RGBA12_LE_sRGB",
                           "12-bit RGBA in 16-bit LE words, sRGB, full range", PixelMemLayout::I_4x12_LE, true, 4095);
}

static PixelFormat::Data makeRGBA12_BE() {
        return makeRGBDesc(PixelFormat::RGBA12_BE_sRGB, "RGBA12_BE_sRGB",
                           "12-bit RGBA in 16-bit BE words, sRGB, full range", PixelMemLayout::I_4x12_BE, true, 4095);
}

static PixelFormat::Data makeRGB12_LE() {
        return makeRGBDesc(PixelFormat::RGB12_LE_sRGB, "RGB12_LE_sRGB",
                           "12-bit RGB in 16-bit LE words, sRGB, full range", PixelMemLayout::I_3x12_LE, false, 4095);
}

static PixelFormat::Data makeRGB12_BE() {
        return makeRGBDesc(PixelFormat::RGB12_BE_sRGB, "RGB12_BE_sRGB",
                           "12-bit RGB in 16-bit BE words, sRGB, full range", PixelMemLayout::I_3x12_BE, false, 4095);
}

static PixelFormat::Data makeRGBA16_LE() {
        return makeRGBDesc(PixelFormat::RGBA16_LE_sRGB, "RGBA16_LE_sRGB", "16-bit RGBA LE, sRGB, full range",
                           PixelMemLayout::I_4x16_LE, true, 65535);
}

static PixelFormat::Data makeRGBA16_BE() {
        return makeRGBDesc(PixelFormat::RGBA16_BE_sRGB, "RGBA16_BE_sRGB", "16-bit RGBA BE, sRGB, full range",
                           PixelMemLayout::I_4x16_BE, true, 65535);
}

static PixelFormat::Data makeRGB16_LE() {
        return makeRGBDesc(PixelFormat::RGB16_LE_sRGB, "RGB16_LE_sRGB", "16-bit RGB LE, sRGB, full range",
                           PixelMemLayout::I_3x16_LE, false, 65535);
}

static PixelFormat::Data makeRGB16_BE() {
        return makeRGBDesc(PixelFormat::RGB16_BE_sRGB, "RGB16_BE_sRGB", "16-bit RGB BE, sRGB, full range",
                           PixelMemLayout::I_3x16_BE, false, 65535);
}

// ---------------------------------------------------------------------------
// YCbCr 4:4:4 DPX packed PixelFormat factory function
// ---------------------------------------------------------------------------

static PixelFormat::Data makeYUV10_DPX() {
        return makeYCbCrDesc(PixelFormat::YUV10_DPX_Rec709, "YUV10_DPX_Rec709",
                             "10-bit YCbCr 4:4:4 DPX packed, Rec.709, limited range", PixelMemLayout::I_3x10_DPX,
                             ycbcrSem10);
}

// ---------------------------------------------------------------------------
// DPX additional packed PixelFormat factory functions
// ---------------------------------------------------------------------------

static PixelFormat::Data makeRGB10_DPX_LE() {
        PixelFormat::Data d;
        d.id = PixelFormat::RGB10_DPX_LE_sRGB;
        d.name = "RGB10_DPX_LE_sRGB";
        d.desc = "10-bit RGB, DPX packed LE, sRGB, full range";
        d.memLayout = PixelMemLayout(PixelMemLayout::I_3x10_DPX);
        d.colorModel = ColorModel(ColorModel::sRGB);
        d.compSemantics[0] = {"Red", "R", 0, 1023};
        d.compSemantics[1] = {"Green", "G", 0, 1023};
        d.compSemantics[2] = {"Blue", "B", 0, 1023};
        return d;
}

static PixelFormat::Data makeYUV10_DPX_B() {
        return makeYCbCrDesc(PixelFormat::YUV10_DPX_B_Rec709, "YUV10_DPX_B_Rec709",
                             "10-bit YCbCr 4:4:4 DPX packed method B, Rec.709, limited range",
                             PixelMemLayout::I_3x10_DPX_B, ycbcrSem10);
}

// ---------------------------------------------------------------------------
// BGRA/BGR PixelFormat factory functions
// ---------------------------------------------------------------------------

static PixelFormat::Data makeBGRA8() {
        return makeBGRDesc(PixelFormat::BGRA8_sRGB, "BGRA8_sRGB", "8-bit BGRA, sRGB, full range", PixelMemLayout::I_4x8,
                           true, 255);
}

static PixelFormat::Data makeBGR8() {
        return makeBGRDesc(PixelFormat::BGR8_sRGB, "BGR8_sRGB", "8-bit BGR, sRGB, full range", PixelMemLayout::I_3x8,
                           false, 255);
}

static PixelFormat::Data makeBGRA10_LE() {
        return makeBGRDesc(PixelFormat::BGRA10_LE_sRGB, "BGRA10_LE_sRGB",
                           "10-bit BGRA in 16-bit LE words, sRGB, full range", PixelMemLayout::I_4x10_LE, true, 1023);
}

static PixelFormat::Data makeBGRA10_BE() {
        return makeBGRDesc(PixelFormat::BGRA10_BE_sRGB, "BGRA10_BE_sRGB",
                           "10-bit BGRA in 16-bit BE words, sRGB, full range", PixelMemLayout::I_4x10_BE, true, 1023);
}

static PixelFormat::Data makeBGR10_LE() {
        return makeBGRDesc(PixelFormat::BGR10_LE_sRGB, "BGR10_LE_sRGB",
                           "10-bit BGR in 16-bit LE words, sRGB, full range", PixelMemLayout::I_3x10_LE, false, 1023);
}

static PixelFormat::Data makeBGR10_BE() {
        return makeBGRDesc(PixelFormat::BGR10_BE_sRGB, "BGR10_BE_sRGB",
                           "10-bit BGR in 16-bit BE words, sRGB, full range", PixelMemLayout::I_3x10_BE, false, 1023);
}

static PixelFormat::Data makeBGRA12_LE() {
        return makeBGRDesc(PixelFormat::BGRA12_LE_sRGB, "BGRA12_LE_sRGB",
                           "12-bit BGRA in 16-bit LE words, sRGB, full range", PixelMemLayout::I_4x12_LE, true, 4095);
}

static PixelFormat::Data makeBGRA12_BE() {
        return makeBGRDesc(PixelFormat::BGRA12_BE_sRGB, "BGRA12_BE_sRGB",
                           "12-bit BGRA in 16-bit BE words, sRGB, full range", PixelMemLayout::I_4x12_BE, true, 4095);
}

static PixelFormat::Data makeBGR12_LE() {
        return makeBGRDesc(PixelFormat::BGR12_LE_sRGB, "BGR12_LE_sRGB",
                           "12-bit BGR in 16-bit LE words, sRGB, full range", PixelMemLayout::I_3x12_LE, false, 4095);
}

static PixelFormat::Data makeBGR12_BE() {
        return makeBGRDesc(PixelFormat::BGR12_BE_sRGB, "BGR12_BE_sRGB",
                           "12-bit BGR in 16-bit BE words, sRGB, full range", PixelMemLayout::I_3x12_BE, false, 4095);
}

static PixelFormat::Data makeBGRA16_LE() {
        return makeBGRDesc(PixelFormat::BGRA16_LE_sRGB, "BGRA16_LE_sRGB", "16-bit BGRA LE, sRGB, full range",
                           PixelMemLayout::I_4x16_LE, true, 65535);
}

static PixelFormat::Data makeBGRA16_BE() {
        return makeBGRDesc(PixelFormat::BGRA16_BE_sRGB, "BGRA16_BE_sRGB", "16-bit BGRA BE, sRGB, full range",
                           PixelMemLayout::I_4x16_BE, true, 65535);
}

static PixelFormat::Data makeBGR16_LE() {
        return makeBGRDesc(PixelFormat::BGR16_LE_sRGB, "BGR16_LE_sRGB", "16-bit BGR LE, sRGB, full range",
                           PixelMemLayout::I_3x16_LE, false, 65535);
}

static PixelFormat::Data makeBGR16_BE() {
        return makeBGRDesc(PixelFormat::BGR16_BE_sRGB, "BGR16_BE_sRGB", "16-bit BGR BE, sRGB, full range",
                           PixelMemLayout::I_3x16_BE, false, 65535);
}

// ---------------------------------------------------------------------------
// ARGB PixelFormat factory functions
// ---------------------------------------------------------------------------

static PixelFormat::Data makeARGB8() {
        return makeARGBDesc(PixelFormat::ARGB8_sRGB, "ARGB8_sRGB", "8-bit ARGB, sRGB, full range",
                            PixelMemLayout::I_4x8, 255);
}

static PixelFormat::Data makeARGB10_LE() {
        return makeARGBDesc(PixelFormat::ARGB10_LE_sRGB, "ARGB10_LE_sRGB",
                            "10-bit ARGB in 16-bit LE words, sRGB, full range", PixelMemLayout::I_4x10_LE, 1023);
}

static PixelFormat::Data makeARGB10_BE() {
        return makeARGBDesc(PixelFormat::ARGB10_BE_sRGB, "ARGB10_BE_sRGB",
                            "10-bit ARGB in 16-bit BE words, sRGB, full range", PixelMemLayout::I_4x10_BE, 1023);
}

static PixelFormat::Data makeARGB12_LE() {
        return makeARGBDesc(PixelFormat::ARGB12_LE_sRGB, "ARGB12_LE_sRGB",
                            "12-bit ARGB in 16-bit LE words, sRGB, full range", PixelMemLayout::I_4x12_LE, 4095);
}

static PixelFormat::Data makeARGB12_BE() {
        return makeARGBDesc(PixelFormat::ARGB12_BE_sRGB, "ARGB12_BE_sRGB",
                            "12-bit ARGB in 16-bit BE words, sRGB, full range", PixelMemLayout::I_4x12_BE, 4095);
}

static PixelFormat::Data makeARGB16_LE() {
        return makeARGBDesc(PixelFormat::ARGB16_LE_sRGB, "ARGB16_LE_sRGB", "16-bit ARGB LE, sRGB, full range",
                            PixelMemLayout::I_4x16_LE, 65535);
}

static PixelFormat::Data makeARGB16_BE() {
        return makeARGBDesc(PixelFormat::ARGB16_BE_sRGB, "ARGB16_BE_sRGB", "16-bit ARGB BE, sRGB, full range",
                            PixelMemLayout::I_4x16_BE, 65535);
}

// ---------------------------------------------------------------------------
// ABGR PixelFormat factory functions
// ---------------------------------------------------------------------------

static PixelFormat::Data makeABGR8() {
        return makeABGRDesc(PixelFormat::ABGR8_sRGB, "ABGR8_sRGB", "8-bit ABGR, sRGB, full range",
                            PixelMemLayout::I_4x8, 255);
}

static PixelFormat::Data makeABGR10_LE() {
        return makeABGRDesc(PixelFormat::ABGR10_LE_sRGB, "ABGR10_LE_sRGB",
                            "10-bit ABGR in 16-bit LE words, sRGB, full range", PixelMemLayout::I_4x10_LE, 1023);
}

static PixelFormat::Data makeABGR10_BE() {
        return makeABGRDesc(PixelFormat::ABGR10_BE_sRGB, "ABGR10_BE_sRGB",
                            "10-bit ABGR in 16-bit BE words, sRGB, full range", PixelMemLayout::I_4x10_BE, 1023);
}

static PixelFormat::Data makeABGR12_LE() {
        return makeABGRDesc(PixelFormat::ABGR12_LE_sRGB, "ABGR12_LE_sRGB",
                            "12-bit ABGR in 16-bit LE words, sRGB, full range", PixelMemLayout::I_4x12_LE, 4095);
}

static PixelFormat::Data makeABGR12_BE() {
        return makeABGRDesc(PixelFormat::ABGR12_BE_sRGB, "ABGR12_BE_sRGB",
                            "12-bit ABGR in 16-bit BE words, sRGB, full range", PixelMemLayout::I_4x12_BE, 4095);
}

static PixelFormat::Data makeABGR16_LE() {
        return makeABGRDesc(PixelFormat::ABGR16_LE_sRGB, "ABGR16_LE_sRGB", "16-bit ABGR LE, sRGB, full range",
                            PixelMemLayout::I_4x16_LE, 65535);
}

static PixelFormat::Data makeABGR16_BE() {
        return makeABGRDesc(PixelFormat::ABGR16_BE_sRGB, "ABGR16_BE_sRGB", "16-bit ABGR BE, sRGB, full range",
                            PixelMemLayout::I_4x16_BE, 65535);
}

// ---------------------------------------------------------------------------
// Monochrome sRGB PixelFormat factory functions
// ---------------------------------------------------------------------------

static PixelFormat::Data makeMono8_sRGB() {
        return makeMonoDesc(PixelFormat::Mono8_sRGB, "Mono8_sRGB", "8-bit grayscale, sRGB", PixelMemLayout::I_1x8,
                            ColorModel::sRGB, 255);
}

static PixelFormat::Data makeMono10_LE_sRGB() {
        return makeMonoDesc(PixelFormat::Mono10_LE_sRGB, "Mono10_LE_sRGB", "10-bit grayscale in 16-bit LE word, sRGB",
                            PixelMemLayout::I_1x10_LE, ColorModel::sRGB, 1023);
}

static PixelFormat::Data makeMono10_BE_sRGB() {
        return makeMonoDesc(PixelFormat::Mono10_BE_sRGB, "Mono10_BE_sRGB", "10-bit grayscale in 16-bit BE word, sRGB",
                            PixelMemLayout::I_1x10_BE, ColorModel::sRGB, 1023);
}

static PixelFormat::Data makeMono12_LE_sRGB() {
        return makeMonoDesc(PixelFormat::Mono12_LE_sRGB, "Mono12_LE_sRGB", "12-bit grayscale in 16-bit LE word, sRGB",
                            PixelMemLayout::I_1x12_LE, ColorModel::sRGB, 4095);
}

static PixelFormat::Data makeMono12_BE_sRGB() {
        return makeMonoDesc(PixelFormat::Mono12_BE_sRGB, "Mono12_BE_sRGB", "12-bit grayscale in 16-bit BE word, sRGB",
                            PixelMemLayout::I_1x12_BE, ColorModel::sRGB, 4095);
}

static PixelFormat::Data makeMono16_LE_sRGB() {
        return makeMonoDesc(PixelFormat::Mono16_LE_sRGB, "Mono16_LE_sRGB", "16-bit grayscale LE, sRGB",
                            PixelMemLayout::I_1x16_LE, ColorModel::sRGB, 65535);
}

static PixelFormat::Data makeMono16_BE_sRGB() {
        return makeMonoDesc(PixelFormat::Mono16_BE_sRGB, "Mono16_BE_sRGB", "16-bit grayscale BE, sRGB",
                            PixelMemLayout::I_1x16_BE, ColorModel::sRGB, 65535);
}

// ---------------------------------------------------------------------------
// Float RGBA/RGB/Mono LinearRec709 PixelFormat factory functions
// ---------------------------------------------------------------------------

static PixelFormat::Data makeRGBAF16_LE_LinearRec709() {
        return makeFloatRGBDesc(PixelFormat::RGBAF16_LE_LinearRec709, "RGBAF16_LE_LinearRec709",
                                "Half-float RGBA LE, linear Rec.709", PixelMemLayout::I_4xF16_LE, true,
                                ColorModel::LinearRec709);
}

static PixelFormat::Data makeRGBAF16_BE_LinearRec709() {
        return makeFloatRGBDesc(PixelFormat::RGBAF16_BE_LinearRec709, "RGBAF16_BE_LinearRec709",
                                "Half-float RGBA BE, linear Rec.709", PixelMemLayout::I_4xF16_BE, true,
                                ColorModel::LinearRec709);
}

static PixelFormat::Data makeRGBF16_LE_LinearRec709() {
        return makeFloatRGBDesc(PixelFormat::RGBF16_LE_LinearRec709, "RGBF16_LE_LinearRec709",
                                "Half-float RGB LE, linear Rec.709", PixelMemLayout::I_3xF16_LE, false,
                                ColorModel::LinearRec709);
}

static PixelFormat::Data makeRGBF16_BE_LinearRec709() {
        return makeFloatRGBDesc(PixelFormat::RGBF16_BE_LinearRec709, "RGBF16_BE_LinearRec709",
                                "Half-float RGB BE, linear Rec.709", PixelMemLayout::I_3xF16_BE, false,
                                ColorModel::LinearRec709);
}

static PixelFormat::Data makeMonoF16_LE_LinearRec709() {
        return makeMonoDesc(PixelFormat::MonoF16_LE_LinearRec709, "MonoF16_LE_LinearRec709",
                            "Half-float mono LE, linear Rec.709", PixelMemLayout::I_1xF16_LE, ColorModel::LinearRec709,
                            1.0);
}

static PixelFormat::Data makeMonoF16_BE_LinearRec709() {
        return makeMonoDesc(PixelFormat::MonoF16_BE_LinearRec709, "MonoF16_BE_LinearRec709",
                            "Half-float mono BE, linear Rec.709", PixelMemLayout::I_1xF16_BE, ColorModel::LinearRec709,
                            1.0);
}

static PixelFormat::Data makeRGBAF32_LE_LinearRec709() {
        return makeFloatRGBDesc(PixelFormat::RGBAF32_LE_LinearRec709, "RGBAF32_LE_LinearRec709",
                                "Float RGBA LE, linear Rec.709", PixelMemLayout::I_4xF32_LE, true,
                                ColorModel::LinearRec709);
}

static PixelFormat::Data makeRGBAF32_BE_LinearRec709() {
        return makeFloatRGBDesc(PixelFormat::RGBAF32_BE_LinearRec709, "RGBAF32_BE_LinearRec709",
                                "Float RGBA BE, linear Rec.709", PixelMemLayout::I_4xF32_BE, true,
                                ColorModel::LinearRec709);
}

static PixelFormat::Data makeRGBF32_LE_LinearRec709() {
        return makeFloatRGBDesc(PixelFormat::RGBF32_LE_LinearRec709, "RGBF32_LE_LinearRec709",
                                "Float RGB LE, linear Rec.709", PixelMemLayout::I_3xF32_LE, false,
                                ColorModel::LinearRec709);
}

static PixelFormat::Data makeRGBF32_BE_LinearRec709() {
        return makeFloatRGBDesc(PixelFormat::RGBF32_BE_LinearRec709, "RGBF32_BE_LinearRec709",
                                "Float RGB BE, linear Rec.709", PixelMemLayout::I_3xF32_BE, false,
                                ColorModel::LinearRec709);
}

static PixelFormat::Data makeMonoF32_LE_LinearRec709() {
        return makeMonoDesc(PixelFormat::MonoF32_LE_LinearRec709, "MonoF32_LE_LinearRec709",
                            "Float mono LE, linear Rec.709", PixelMemLayout::I_1xF32_LE, ColorModel::LinearRec709, 1.0);
}

static PixelFormat::Data makeMonoF32_BE_LinearRec709() {
        return makeMonoDesc(PixelFormat::MonoF32_BE_LinearRec709, "MonoF32_BE_LinearRec709",
                            "Float mono BE, linear Rec.709", PixelMemLayout::I_1xF32_BE, ColorModel::LinearRec709, 1.0);
}

// ---------------------------------------------------------------------------
// 10:10:10:2 packed sRGB PixelFormat factory functions
// ---------------------------------------------------------------------------

static PixelFormat::Data makeRGB10A2_LE() {
        PixelFormat::Data d;
        d.id = PixelFormat::RGB10A2_LE_sRGB;
        d.name = "RGB10A2_LE_sRGB";
        d.desc = "RGB 10-bit + Alpha 2-bit in 32-bit LE, sRGB, full range";
        d.memLayout = PixelMemLayout(PixelMemLayout::I_10_10_10_2_LE);
        d.colorModel = ColorModel(ColorModel::sRGB);
        d.hasAlpha = true;
        d.alphaCompIndex = 3;
        d.compSemantics[0] = {"Red", "R", 0, 1023};
        d.compSemantics[1] = {"Green", "G", 0, 1023};
        d.compSemantics[2] = {"Blue", "B", 0, 1023};
        d.compSemantics[3] = {"Alpha", "A", 0, 3};
        return d;
}

static PixelFormat::Data makeRGB10A2_BE() {
        PixelFormat::Data d;
        d.id = PixelFormat::RGB10A2_BE_sRGB;
        d.name = "RGB10A2_BE_sRGB";
        d.desc = "RGB 10-bit + Alpha 2-bit in 32-bit BE, sRGB, full range";
        d.memLayout = PixelMemLayout(PixelMemLayout::I_10_10_10_2_BE);
        d.colorModel = ColorModel(ColorModel::sRGB);
        d.hasAlpha = true;
        d.alphaCompIndex = 3;
        d.compSemantics[0] = {"Red", "R", 0, 1023};
        d.compSemantics[1] = {"Green", "G", 0, 1023};
        d.compSemantics[2] = {"Blue", "B", 0, 1023};
        d.compSemantics[3] = {"Alpha", "A", 0, 3};
        return d;
}

static PixelFormat::Data makeBGR10A2_LE() {
        PixelFormat::Data d;
        d.id = PixelFormat::BGR10A2_LE_sRGB;
        d.name = "BGR10A2_LE_sRGB";
        d.desc = "BGR 10-bit + Alpha 2-bit in 32-bit LE, sRGB, full range";
        d.memLayout = PixelMemLayout(PixelMemLayout::I_10_10_10_2_LE);
        d.colorModel = ColorModel(ColorModel::sRGB);
        d.hasAlpha = true;
        d.alphaCompIndex = 3;
        d.compSemantics[0] = {"Blue", "B", 0, 1023};
        d.compSemantics[1] = {"Green", "G", 0, 1023};
        d.compSemantics[2] = {"Red", "R", 0, 1023};
        d.compSemantics[3] = {"Alpha", "A", 0, 3};
        return d;
}

static PixelFormat::Data makeBGR10A2_BE() {
        PixelFormat::Data d;
        d.id = PixelFormat::BGR10A2_BE_sRGB;
        d.name = "BGR10A2_BE_sRGB";
        d.desc = "BGR 10-bit + Alpha 2-bit in 32-bit BE, sRGB, full range";
        d.memLayout = PixelMemLayout(PixelMemLayout::I_10_10_10_2_BE);
        d.colorModel = ColorModel(ColorModel::sRGB);
        d.hasAlpha = true;
        d.alphaCompIndex = 3;
        d.compSemantics[0] = {"Blue", "B", 0, 1023};
        d.compSemantics[1] = {"Green", "G", 0, 1023};
        d.compSemantics[2] = {"Red", "R", 0, 1023};
        d.compSemantics[3] = {"Alpha", "A", 0, 3};
        return d;
}

// ---------------------------------------------------------------------------
// 4:4:4 YCbCr Rec.709 PixelFormat factory functions
// ---------------------------------------------------------------------------

static PixelFormat::Data makeYUV8_444() {
        return makeYCbCrDesc(PixelFormat::YUV8_Rec709, "YUV8_Rec709", "8-bit YCbCr 4:4:4, Rec.709, limited range",
                             PixelMemLayout::I_3x8, ycbcrSem8);
}

static PixelFormat::Data makeYUV10_LE_444() {
        return makeYCbCrDesc(PixelFormat::YUV10_LE_Rec709, "YUV10_LE_Rec709",
                             "10-bit YCbCr 4:4:4 LE, Rec.709, limited range", PixelMemLayout::I_3x10_LE, ycbcrSem10);
}

static PixelFormat::Data makeYUV10_BE_444() {
        return makeYCbCrDesc(PixelFormat::YUV10_BE_Rec709, "YUV10_BE_Rec709",
                             "10-bit YCbCr 4:4:4 BE, Rec.709, limited range", PixelMemLayout::I_3x10_BE, ycbcrSem10);
}

static PixelFormat::Data makeYUV12_LE_444() {
        return makeYCbCrDesc(PixelFormat::YUV12_LE_Rec709, "YUV12_LE_Rec709",
                             "12-bit YCbCr 4:4:4 LE, Rec.709, limited range", PixelMemLayout::I_3x12_LE, ycbcrSem12);
}

static PixelFormat::Data makeYUV12_BE_444() {
        return makeYCbCrDesc(PixelFormat::YUV12_BE_Rec709, "YUV12_BE_Rec709",
                             "12-bit YCbCr 4:4:4 BE, Rec.709, limited range", PixelMemLayout::I_3x12_BE, ycbcrSem12);
}

static PixelFormat::Data makeYUV16_LE_444() {
        return makeYCbCrDesc(PixelFormat::YUV16_LE_Rec709, "YUV16_LE_Rec709",
                             "16-bit YCbCr 4:4:4 LE, Rec.709, limited range", PixelMemLayout::I_3x16_LE, ycbcrSem16);
}

static PixelFormat::Data makeYUV16_BE_444() {
        return makeYCbCrDesc(PixelFormat::YUV16_BE_Rec709, "YUV16_BE_Rec709",
                             "16-bit YCbCr 4:4:4 BE, Rec.709, limited range", PixelMemLayout::I_3x16_BE, ycbcrSem16);
}

// ---------------------------------------------------------------------------
// Rec.2020 YCbCr PixelFormat factory functions
// ---------------------------------------------------------------------------

static PixelFormat::Data makeYUV10_422_UYVY_LE_Rec2020() {
        return makeYCbCrDescWithModel(PixelFormat::YUV10_422_UYVY_LE_Rec2020, "YUV10_422_UYVY_LE_Rec2020",
                                      "10-bit YCbCr 4:2:2 UYVY LE, Rec.2020, limited range",
                                      PixelMemLayout::I_422_UYVY_3x10_LE, ycbcrSem10, ColorModel::YCbCr_Rec2020);
}

static PixelFormat::Data makeYUV10_422_UYVY_BE_Rec2020() {
        return makeYCbCrDescWithModel(PixelFormat::YUV10_422_UYVY_BE_Rec2020, "YUV10_422_UYVY_BE_Rec2020",
                                      "10-bit YCbCr 4:2:2 UYVY BE, Rec.2020, limited range",
                                      PixelMemLayout::I_422_UYVY_3x10_BE, ycbcrSem10, ColorModel::YCbCr_Rec2020);
}

static PixelFormat::Data makeYUV12_422_UYVY_LE_Rec2020() {
        return makeYCbCrDescWithModel(PixelFormat::YUV12_422_UYVY_LE_Rec2020, "YUV12_422_UYVY_LE_Rec2020",
                                      "12-bit YCbCr 4:2:2 UYVY LE, Rec.2020, limited range",
                                      PixelMemLayout::I_422_UYVY_3x12_LE, ycbcrSem12, ColorModel::YCbCr_Rec2020);
}

static PixelFormat::Data makeYUV12_422_UYVY_BE_Rec2020() {
        return makeYCbCrDescWithModel(PixelFormat::YUV12_422_UYVY_BE_Rec2020, "YUV12_422_UYVY_BE_Rec2020",
                                      "12-bit YCbCr 4:2:2 UYVY BE, Rec.2020, limited range",
                                      PixelMemLayout::I_422_UYVY_3x12_BE, ycbcrSem12, ColorModel::YCbCr_Rec2020);
}

static PixelFormat::Data makeYUV10_420_Planar_LE_Rec2020() {
        return makeYCbCrDescWithModel(PixelFormat::YUV10_420_Planar_LE_Rec2020, "YUV10_420_Planar_LE_Rec2020",
                                      "10-bit YCbCr 4:2:0 planar LE, Rec.2020, limited range",
                                      PixelMemLayout::P_420_3x10_LE, ycbcrSem10, ColorModel::YCbCr_Rec2020);
}

static PixelFormat::Data makeYUV10_420_Planar_BE_Rec2020() {
        return makeYCbCrDescWithModel(PixelFormat::YUV10_420_Planar_BE_Rec2020, "YUV10_420_Planar_BE_Rec2020",
                                      "10-bit YCbCr 4:2:0 planar BE, Rec.2020, limited range",
                                      PixelMemLayout::P_420_3x10_BE, ycbcrSem10, ColorModel::YCbCr_Rec2020);
}

static PixelFormat::Data makeYUV12_420_Planar_LE_Rec2020() {
        return makeYCbCrDescWithModel(PixelFormat::YUV12_420_Planar_LE_Rec2020, "YUV12_420_Planar_LE_Rec2020",
                                      "12-bit YCbCr 4:2:0 planar LE, Rec.2020, limited range",
                                      PixelMemLayout::P_420_3x12_LE, ycbcrSem12, ColorModel::YCbCr_Rec2020);
}

static PixelFormat::Data makeYUV12_420_Planar_BE_Rec2020() {
        return makeYCbCrDescWithModel(PixelFormat::YUV12_420_Planar_BE_Rec2020, "YUV12_420_Planar_BE_Rec2020",
                                      "12-bit YCbCr 4:2:0 planar BE, Rec.2020, limited range",
                                      PixelMemLayout::P_420_3x12_BE, ycbcrSem12, ColorModel::YCbCr_Rec2020);
}

// ---------------------------------------------------------------------------
// Rec.601 YCbCr PixelFormat factory functions
// ---------------------------------------------------------------------------

// Rec.601 variants share on-disk FourCCs with their Rec.709
// counterparts — the FourCC identifies the byte layout, and the
// colour-matrix / range information is carried separately in the
// container (e.g. QuickTime's @c colr atom).  Omitting the FourCC
// here would make these descs undetectable through FourCC-based
// writers (QuickTime's @c stsd, FFmpeg's mov demuxer, ...), so we
// reuse the Rec.709 FourCC list verbatim.

static PixelFormat::Data makeYUV8_422_Rec601() {
        auto d = makeYCbCrDescWithModel(PixelFormat::YUV8_422_Rec601, "YUV8_422_Rec601",
                                        "8-bit YCbCr 4:2:2, Rec.601, limited range", PixelMemLayout::I_422_3x8,
                                        ycbcrSem8, ColorModel::YCbCr_Rec601);
        d.fourccList = {"YUY2", "YUYV"};
        return d;
}

static PixelFormat::Data makeYUV8_422_UYVY_Rec601() {
        auto d = makeYCbCrDescWithModel(PixelFormat::YUV8_422_UYVY_Rec601, "YUV8_422_UYVY_Rec601",
                                        "8-bit YCbCr 4:2:2 UYVY, Rec.601, limited range",
                                        PixelMemLayout::I_422_UYVY_3x8, ycbcrSem8, ColorModel::YCbCr_Rec601);
        d.fourccList = {"2vuy", "UYVY"};
        return d;
}

static PixelFormat::Data makeYUV8_420_Planar_Rec601() {
        auto d = makeYCbCrDescWithModel(PixelFormat::YUV8_420_Planar_Rec601, "YUV8_420_Planar_Rec601",
                                        "8-bit YCbCr 4:2:0 planar, Rec.601, limited range", PixelMemLayout::P_420_3x8,
                                        ycbcrSem8, ColorModel::YCbCr_Rec601);
        d.fourccList = {"I420"};
        return d;
}

static PixelFormat::Data makeYUV8_420_SemiPlanar_Rec601() {
        auto d = makeYCbCrDescWithModel(PixelFormat::YUV8_420_SemiPlanar_Rec601, "YUV8_420_SemiPlanar_Rec601",
                                        "8-bit YCbCr 4:2:0 NV12, Rec.601, limited range", PixelMemLayout::SP_420_8,
                                        ycbcrSem8, ColorModel::YCbCr_Rec601);
        d.fourccList = {"NV12"};
        return d;
}

// ---------------------------------------------------------------------------
// NV21 Rec.709 PixelFormat factory functions
// ---------------------------------------------------------------------------

static PixelFormat::Data makeYUV8_420_NV21() {
        auto d = makeYCbCrDesc(PixelFormat::YUV8_420_NV21_Rec709, "YUV8_420_NV21_Rec709",
                               "8-bit YCbCr 4:2:0 NV21, Rec.709, limited range", PixelMemLayout::SP_420_NV21_8,
                               ycbcrSem8);
        d.fourccList = {"NV21"};
        return d;
}

static PixelFormat::Data makeYUV10_420_NV21_LE() {
        return makeYCbCrDesc(PixelFormat::YUV10_420_NV21_LE_Rec709, "YUV10_420_NV21_LE_Rec709",
                             "10-bit YCbCr 4:2:0 NV21 LE, Rec.709, limited range", PixelMemLayout::SP_420_NV21_10_LE,
                             ycbcrSem10);
}

static PixelFormat::Data makeYUV10_420_NV21_BE() {
        return makeYCbCrDesc(PixelFormat::YUV10_420_NV21_BE_Rec709, "YUV10_420_NV21_BE_Rec709",
                             "10-bit YCbCr 4:2:0 NV21 BE, Rec.709, limited range", PixelMemLayout::SP_420_NV21_10_BE,
                             ycbcrSem10);
}

static PixelFormat::Data makeYUV12_420_NV21_LE() {
        return makeYCbCrDesc(PixelFormat::YUV12_420_NV21_LE_Rec709, "YUV12_420_NV21_LE_Rec709",
                             "12-bit YCbCr 4:2:0 NV21 LE, Rec.709, limited range", PixelMemLayout::SP_420_NV21_12_LE,
                             ycbcrSem12);
}

static PixelFormat::Data makeYUV12_420_NV21_BE() {
        return makeYCbCrDesc(PixelFormat::YUV12_420_NV21_BE_Rec709, "YUV12_420_NV21_BE_Rec709",
                             "12-bit YCbCr 4:2:0 NV21 BE, Rec.709, limited range", PixelMemLayout::SP_420_NV21_12_BE,
                             ycbcrSem12);
}

// ---------------------------------------------------------------------------
// NV16 semi-planar 4:2:2 Rec.709 PixelFormat factory functions
// ---------------------------------------------------------------------------

static PixelFormat::Data makeYUV8_422_SemiPlanar() {
        auto d = makeYCbCrDesc(PixelFormat::YUV8_422_SemiPlanar_Rec709, "YUV8_422_SemiPlanar_Rec709",
                               "8-bit YCbCr 4:2:2 NV16, Rec.709, limited range", PixelMemLayout::SP_422_8, ycbcrSem8);
        d.fourccList = {"NV16"};
        return d;
}

static PixelFormat::Data makeYUV10_422_SemiPlanar_LE() {
        return makeYCbCrDesc(PixelFormat::YUV10_422_SemiPlanar_LE_Rec709, "YUV10_422_SemiPlanar_LE_Rec709",
                             "10-bit YCbCr 4:2:2 NV16 LE, Rec.709, limited range", PixelMemLayout::SP_422_10_LE,
                             ycbcrSem10);
}

static PixelFormat::Data makeYUV10_422_SemiPlanar_BE() {
        return makeYCbCrDesc(PixelFormat::YUV10_422_SemiPlanar_BE_Rec709, "YUV10_422_SemiPlanar_BE_Rec709",
                             "10-bit YCbCr 4:2:2 NV16 BE, Rec.709, limited range", PixelMemLayout::SP_422_10_BE,
                             ycbcrSem10);
}

static PixelFormat::Data makeYUV12_422_SemiPlanar_LE() {
        return makeYCbCrDesc(PixelFormat::YUV12_422_SemiPlanar_LE_Rec709, "YUV12_422_SemiPlanar_LE_Rec709",
                             "12-bit YCbCr 4:2:2 NV16 LE, Rec.709, limited range", PixelMemLayout::SP_422_12_LE,
                             ycbcrSem12);
}

static PixelFormat::Data makeYUV12_422_SemiPlanar_BE() {
        return makeYCbCrDesc(PixelFormat::YUV12_422_SemiPlanar_BE_Rec709, "YUV12_422_SemiPlanar_BE_Rec709",
                             "12-bit YCbCr 4:2:2 NV16 BE, Rec.709, limited range", PixelMemLayout::SP_422_12_BE,
                             ycbcrSem12);
}

// ---------------------------------------------------------------------------
// 4:1:1 Rec.709 PixelFormat factory function
// ---------------------------------------------------------------------------

static PixelFormat::Data makeYUV8_411_Planar() {
        return makeYCbCrDesc(PixelFormat::YUV8_411_Planar_Rec709, "YUV8_411_Planar_Rec709",
                             "8-bit YCbCr 4:1:1 planar, Rec.709, limited range", PixelMemLayout::P_411_3x8, ycbcrSem8);
}

// ---------------------------------------------------------------------------
// 16-bit YCbCr Rec.709 PixelFormat factory functions
// ---------------------------------------------------------------------------

static PixelFormat::Data makeYUV16_422_UYVY_LE() {
        return makeYCbCrDesc(PixelFormat::YUV16_422_UYVY_LE_Rec709, "YUV16_422_UYVY_LE_Rec709",
                             "16-bit YCbCr 4:2:2 UYVY LE, Rec.709, limited range", PixelMemLayout::I_422_UYVY_3x16_LE,
                             ycbcrSem16);
}

static PixelFormat::Data makeYUV16_422_UYVY_BE() {
        return makeYCbCrDesc(PixelFormat::YUV16_422_UYVY_BE_Rec709, "YUV16_422_UYVY_BE_Rec709",
                             "16-bit YCbCr 4:2:2 UYVY BE, Rec.709, limited range", PixelMemLayout::I_422_UYVY_3x16_BE,
                             ycbcrSem16);
}

static PixelFormat::Data makeYUV16_422_Planar_LE() {
        return makeYCbCrDesc(PixelFormat::YUV16_422_Planar_LE_Rec709, "YUV16_422_Planar_LE_Rec709",
                             "16-bit YCbCr 4:2:2 planar LE, Rec.709, limited range", PixelMemLayout::P_422_3x16_LE,
                             ycbcrSem16);
}

static PixelFormat::Data makeYUV16_422_Planar_BE() {
        return makeYCbCrDesc(PixelFormat::YUV16_422_Planar_BE_Rec709, "YUV16_422_Planar_BE_Rec709",
                             "16-bit YCbCr 4:2:2 planar BE, Rec.709, limited range", PixelMemLayout::P_422_3x16_BE,
                             ycbcrSem16);
}

static PixelFormat::Data makeYUV16_420_Planar_LE() {
        return makeYCbCrDesc(PixelFormat::YUV16_420_Planar_LE_Rec709, "YUV16_420_Planar_LE_Rec709",
                             "16-bit YCbCr 4:2:0 planar LE, Rec.709, limited range", PixelMemLayout::P_420_3x16_LE,
                             ycbcrSem16);
}

static PixelFormat::Data makeYUV16_420_Planar_BE() {
        return makeYCbCrDesc(PixelFormat::YUV16_420_Planar_BE_Rec709, "YUV16_420_Planar_BE_Rec709",
                             "16-bit YCbCr 4:2:0 planar BE, Rec.709, limited range", PixelMemLayout::P_420_3x16_BE,
                             ycbcrSem16);
}

static PixelFormat::Data makeYUV16_420_SemiPlanar_LE() {
        return makeYCbCrDesc(PixelFormat::YUV16_420_SemiPlanar_LE_Rec709, "YUV16_420_SemiPlanar_LE_Rec709",
                             "16-bit YCbCr 4:2:0 NV12 LE, Rec.709, limited range", PixelMemLayout::SP_420_16_LE,
                             ycbcrSem16);
}

static PixelFormat::Data makeYUV16_420_SemiPlanar_BE() {
        return makeYCbCrDesc(PixelFormat::YUV16_420_SemiPlanar_BE_Rec709, "YUV16_420_SemiPlanar_BE_Rec709",
                             "16-bit YCbCr 4:2:0 NV12 BE, Rec.709, limited range", PixelMemLayout::SP_420_16_BE,
                             ycbcrSem16);
}

static PixelFormat::Data makeYUV16_422_SemiPlanar_LE() {
        // NDI's P216 wire format: 16-bit container, 16-bit precision, 4:2:2
        // semi-planar.  Senders carrying 10/12-bit content over NDI use the
        // narrower YUV10_/YUV12_422_SemiPlanar_LE_Rec709 entries instead, which
        // share the same wire layout (sample bits in the high bits of each
        // 16-bit container).  See docs/ndi.md for the bit-depth convention.
        auto d = makeYCbCrDesc(PixelFormat::YUV16_422_SemiPlanar_LE_Rec709, "YUV16_422_SemiPlanar_LE_Rec709",
                               "16-bit YCbCr 4:2:2 NV16 LE, Rec.709, limited range",
                               PixelMemLayout::SP_422_16_LE, ycbcrSem16);
        d.fourccList = {"P216"};
        return d;
}

static PixelFormat::Data makeYUV16_422_SemiPlanar_BE() {
        return makeYCbCrDesc(PixelFormat::YUV16_422_SemiPlanar_BE_Rec709, "YUV16_422_SemiPlanar_BE_Rec709",
                             "16-bit YCbCr 4:2:2 NV16 BE, Rec.709, limited range", PixelMemLayout::SP_422_16_BE,
                             ycbcrSem16);
}

// ---------------------------------------------------------------------------
// Video codec compressed formats (QuickTime / MP4 / ISO-BMFF)
//
// These entries describe compressed bitstreams by codec. The memLayout
// field is a semantic hint at the "natural" decoded layout for the codec;
// actual decoded output formats belong in decodeTargets (added when a
// real decoder is wired up). The fourccList carries every FourCC code
// that identifies this codec across containers — the first entry is the
// preferred code used by writers.
// ---------------------------------------------------------------------------

static PixelFormat::Data makeH264() {
        PixelFormat::Data d;
        d.id = PixelFormat::H264;
        d.name = "H264";
        d.desc = "H.264 / AVC compressed video";
        d.memLayout = PixelMemLayout(PixelMemLayout::P_420_3x8);
        d.colorModel = ColorModel(ColorModel::YCbCr_Rec709);
        d.compressed = true;
        d.videoCodec = VideoCodec(VideoCodec::H264);
        d.fourccList = {"avc1", "avc3"};
        d.compSemantics[0] = ycbcrSem8[0];
        d.compSemantics[1] = ycbcrSem8[1];
        d.compSemantics[2] = ycbcrSem8[2];
        return d;
}

static PixelFormat::Data makeHEVC() {
        PixelFormat::Data d;
        d.id = PixelFormat::HEVC;
        d.name = "HEVC";
        d.desc = "H.265 / HEVC compressed video";
        d.memLayout = PixelMemLayout(PixelMemLayout::P_420_3x10_LE);
        d.colorModel = ColorModel(ColorModel::YCbCr_Rec709);
        d.compressed = true;
        d.videoCodec = VideoCodec(VideoCodec::HEVC);
        d.fourccList = {"hvc1", "hev1"};
        d.compSemantics[0] = ycbcrSem10[0];
        d.compSemantics[1] = ycbcrSem10[1];
        d.compSemantics[2] = ycbcrSem10[2];
        return d;
}

static PixelFormat::Data makeAV1() {
        PixelFormat::Data d;
        d.id = PixelFormat::AV1;
        d.name = "AV1";
        d.desc = "AV1 (AOMedia Video 1) compressed video";
        d.memLayout = PixelMemLayout(PixelMemLayout::P_420_3x10_LE);
        d.colorModel = ColorModel(ColorModel::YCbCr_Rec709);
        d.compressed = true;
        d.videoCodec = VideoCodec(VideoCodec::AV1);
        d.fourccList = {"av01"};
        d.compSemantics[0] = ycbcrSem10[0];
        d.compSemantics[1] = ycbcrSem10[1];
        d.compSemantics[2] = ycbcrSem10[2];
        return d;
}

static PixelFormat::Data makeYUV8_444_Planar() {
        return makeYCbCrDesc(PixelFormat::YUV8_444_Planar_Rec709, "YUV8_444_Planar_Rec709",
                             "8-bit YCbCr 4:4:4 planar, Rec.709, limited range", PixelMemLayout::P_444_3x8, ycbcrSem8);
}

static PixelFormat::Data makeYUV10_444_Planar_LE() {
        return makeYCbCrDesc(PixelFormat::YUV10_444_Planar_LE_Rec709, "YUV10_444_Planar_LE_Rec709",
                             "10-bit YCbCr 4:4:4 planar LE, Rec.709, limited range", PixelMemLayout::P_444_3x10_LE,
                             ycbcrSem10);
}

static PixelFormat::Data makeProRes422Desc(PixelFormat::ID id, const char *name, const char *desc, FourCC fourcc,
                                           VideoCodec::ID codec) {
        PixelFormat::Data d;
        d.id = id;
        d.name = name;
        d.desc = desc;
        // ProRes 422 family decodes to 10-bit YCbCr 4:2:2.
        d.memLayout = PixelMemLayout(PixelMemLayout::P_422_3x10_LE);
        d.colorModel = ColorModel(ColorModel::YCbCr_Rec709);
        d.compressed = true;
        d.videoCodec = VideoCodec(codec);
        d.fourccList = {fourcc};
        d.compSemantics[0] = ycbcrSem10[0];
        d.compSemantics[1] = ycbcrSem10[1];
        d.compSemantics[2] = ycbcrSem10[2];
        return d;
}

static PixelFormat::Data makeProRes_422_Proxy() {
        return makeProRes422Desc(PixelFormat::ProRes_422_Proxy, "ProRes_422_Proxy", "Apple ProRes 422 Proxy",
                                 FourCC("apco"), VideoCodec::ProRes_422_Proxy);
}

static PixelFormat::Data makeProRes_422_LT() {
        return makeProRes422Desc(PixelFormat::ProRes_422_LT, "ProRes_422_LT", "Apple ProRes 422 LT", FourCC("apcs"),
                                 VideoCodec::ProRes_422_LT);
}

static PixelFormat::Data makeProRes_422() {
        return makeProRes422Desc(PixelFormat::ProRes_422, "ProRes_422", "Apple ProRes 422", FourCC("apcn"),
                                 VideoCodec::ProRes_422);
}

static PixelFormat::Data makeProRes_422_HQ() {
        return makeProRes422Desc(PixelFormat::ProRes_422_HQ, "ProRes_422_HQ", "Apple ProRes 422 HQ", FourCC("apch"),
                                 VideoCodec::ProRes_422_HQ);
}

static PixelFormat::Data makeProRes_4444() {
        PixelFormat::Data d;
        d.id = PixelFormat::ProRes_4444;
        d.name = "ProRes_4444";
        d.desc = "Apple ProRes 4444";
        // ProRes 4444 decodes to 10-bit 4:4:4 with optional alpha.
        d.memLayout = PixelMemLayout(PixelMemLayout::I_4x10_LE);
        d.colorModel = ColorModel(ColorModel::YCbCr_Rec709);
        d.compressed = true;
        d.hasAlpha = true;
        d.alphaCompIndex = 3;
        d.videoCodec = VideoCodec(VideoCodec::ProRes_4444);
        d.fourccList = {"ap4h"};
        d.compSemantics[0] = ycbcrSem10[0];
        d.compSemantics[1] = ycbcrSem10[1];
        d.compSemantics[2] = ycbcrSem10[2];
        d.compSemantics[3] = {"Alpha", "A", 0, 1023};
        return d;
}

static PixelFormat::Data makeProRes_4444_XQ() {
        PixelFormat::Data d;
        d.id = PixelFormat::ProRes_4444_XQ;
        d.name = "ProRes_4444_XQ";
        d.desc = "Apple ProRes 4444 XQ";
        // ProRes 4444 XQ decodes to 12-bit 4:4:4 with optional alpha.
        d.memLayout = PixelMemLayout(PixelMemLayout::I_4x12_LE);
        d.colorModel = ColorModel(ColorModel::YCbCr_Rec709);
        d.compressed = true;
        d.hasAlpha = true;
        d.alphaCompIndex = 3;
        d.videoCodec = VideoCodec(VideoCodec::ProRes_4444_XQ);
        d.fourccList = {"ap4x"};
        d.compSemantics[0] = ycbcrSem12[0];
        d.compSemantics[1] = ycbcrSem12[1];
        d.compSemantics[2] = ycbcrSem12[2];
        d.compSemantics[3] = {"Alpha", "A", 0, 4095};
        return d;
}

// ---------------------------------------------------------------------------
// Construct-on-first-use registry
// ---------------------------------------------------------------------------

struct PixelFormatRegistry {
                Map<PixelFormat::ID, PixelFormat::Data> entries;
                Map<String, PixelFormat::ID>            nameMap;

                PixelFormatRegistry() {
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
                        add(makeYUV16_422_SemiPlanar_LE());
                        add(makeYUV16_422_SemiPlanar_BE());

                        // Video codec compressed formats (QuickTime / MP4 / ISO-BMFF)
                        add(makeH264());
                        add(makeHEVC());
                        add(makeAV1());
                        add(makeYUV8_444_Planar());
                        add(makeYUV10_444_Planar_LE());
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
                        add(makeRGB8_Planar_sRGB());
                }

                void add(PixelFormat::Data d) {
                        PixelFormat::ID id = d.id;
                        // Register every name including the "Invalid" sentinel
                        // so a Variant String round-trip is lossless — see
                        // PixelFormat::registerData for the rationale.
                        nameMap[d.name] = id;
                        entries[id] = std::move(d);
                }
};

static PixelFormatRegistry &registry() {
        static PixelFormatRegistry reg;
        return reg;
}

const PixelFormat::Data *PixelFormat::lookupData(ID id) {
        auto &reg = registry();
        auto  it = reg.entries.find(id);
        if (it != reg.entries.end()) return &it->second;
        return &reg.entries[Invalid];
}

// Auto-derive VideoRange from the Luma (YCbCr) or Red (RGB) component
// range in compSemantics[0].  The library convention across the
// well-known PixelFormat factories is:
//
//   Full range:    min == 0 and max is a power-of-two-minus-one
//                  (255, 1023, 4095, 65535) — the full digital code
//                  range for a given bit depth.
//   Limited range: min > 0 and max < the corresponding full-range
//                  maximum (e.g. 16..235 for 8-bit Y'CbCr luma,
//                  64..940 for 10-bit).
//
// Anything that doesn't match either shape is left as Unknown.
// Callers can always set @c Data::videoRange explicitly to override.
static VideoRange autoDeriveVideoRange(const PixelFormat::Data &d) {
        if (d.videoRange != VideoRange::Unknown) return d.videoRange;
        const auto &c0 = d.compSemantics[0];
        if (c0.rangeMax <= 0.0f) return VideoRange::Unknown;
        if (c0.rangeMin > 0.0f) return VideoRange::Limited;
        const int imax = static_cast<int>(c0.rangeMax);
        // Full-range bit-depth maxima: 2^N - 1 for N in {8, 10, 12, 14, 16}.
        // (14-bit is uncommon in practice but included for completeness.)
        if (imax == 255 || imax == 1023 || imax == 4095 || imax == 16383 || imax == 65535) {
                return VideoRange::Full;
        }
        return VideoRange::Unknown;
}

void PixelFormat::registerData(Data &&data) {
        auto &reg = registry();
        // The "Invalid" sentinel name is registered too so a Variant
        // String round-trip (PixelFormat() → "Invalid" → PixelFormat())
        // is lossless — defaults that intentionally use Invalid as a
        // pass-through marker (e.g. CSC's OutputPixelFormat) need to
        // survive a JSON serialize-then-parse without the parse
        // failing.  The error-aware @ref lookup overload still
        // distinguishes a successful sentinel hit from a genuine miss
        // for callers (like @ref VariantSpec::parseString) that need
        // that distinction.
        reg.nameMap[data.name] = data.id;
        // Fill in a reasonable videoRange default when the factory
        // didn't set one explicitly — the library ships a lot of
        // pre-existing factories that predate the field, and they all
        // carry component ranges consistent with their names.
        data.videoRange = autoDeriveVideoRange(data);
        reg.entries[data.id] = std::move(data);
}

PixelFormat PixelFormat::lookup(const String &name) {
        return lookup(name, nullptr);
}

PixelFormat PixelFormat::lookup(const String &name, Error *err) {
        auto &reg = registry();
        auto  it = reg.nameMap.find(name);
        if (it == reg.nameMap.end()) {
                if (err != nullptr) *err = Error::IdNotFound;
                return PixelFormat(Invalid);
        }
        if (err != nullptr) *err = Error::Ok;
        return PixelFormat(it->second);
}

PixelFormat::IDList PixelFormat::registeredIDs() {
        auto  &reg = registry();
        IDList ret;
        for (const auto &[id, data] : reg.entries) {
                if (id != Invalid) ret.pushToBack(id);
        }
        return ret;
}

// ---------------------------------------------------------------------------
// Paint engine factory declarations from implementation files
// ---------------------------------------------------------------------------

// paintengine_interleaved.cpp factories
PaintEngine createPaintEngine_RGBA8(const PixelFormat::Data *d, const UncompressedVideoPayload &payload);
PaintEngine createPaintEngine_RGB8(const PixelFormat::Data *d, const UncompressedVideoPayload &payload);
PaintEngine createPaintEngine_BGRA8(const PixelFormat::Data *d, const UncompressedVideoPayload &payload);
PaintEngine createPaintEngine_BGR8(const PixelFormat::Data *d, const UncompressedVideoPayload &payload);
PaintEngine createPaintEngine_ARGB8(const PixelFormat::Data *d, const UncompressedVideoPayload &payload);
PaintEngine createPaintEngine_ABGR8(const PixelFormat::Data *d, const UncompressedVideoPayload &payload);
PaintEngine createPaintEngine_RGBA10_LE(const PixelFormat::Data *d, const UncompressedVideoPayload &payload);
PaintEngine createPaintEngine_RGB10_LE(const PixelFormat::Data *d, const UncompressedVideoPayload &payload);
PaintEngine createPaintEngine_BGRA10_LE(const PixelFormat::Data *d, const UncompressedVideoPayload &payload);
PaintEngine createPaintEngine_BGR10_LE(const PixelFormat::Data *d, const UncompressedVideoPayload &payload);
PaintEngine createPaintEngine_ARGB10_LE(const PixelFormat::Data *d, const UncompressedVideoPayload &payload);
PaintEngine createPaintEngine_ABGR10_LE(const PixelFormat::Data *d, const UncompressedVideoPayload &payload);
PaintEngine createPaintEngine_RGBA12_LE(const PixelFormat::Data *d, const UncompressedVideoPayload &payload);
PaintEngine createPaintEngine_RGB12_LE(const PixelFormat::Data *d, const UncompressedVideoPayload &payload);
PaintEngine createPaintEngine_BGRA12_LE(const PixelFormat::Data *d, const UncompressedVideoPayload &payload);
PaintEngine createPaintEngine_BGR12_LE(const PixelFormat::Data *d, const UncompressedVideoPayload &payload);
PaintEngine createPaintEngine_ARGB12_LE(const PixelFormat::Data *d, const UncompressedVideoPayload &payload);
PaintEngine createPaintEngine_ABGR12_LE(const PixelFormat::Data *d, const UncompressedVideoPayload &payload);
PaintEngine createPaintEngine_RGBA16_LE(const PixelFormat::Data *d, const UncompressedVideoPayload &payload);
PaintEngine createPaintEngine_RGB16_LE(const PixelFormat::Data *d, const UncompressedVideoPayload &payload);
PaintEngine createPaintEngine_BGRA16_LE(const PixelFormat::Data *d, const UncompressedVideoPayload &payload);
PaintEngine createPaintEngine_BGR16_LE(const PixelFormat::Data *d, const UncompressedVideoPayload &payload);
PaintEngine createPaintEngine_ARGB16_LE(const PixelFormat::Data *d, const UncompressedVideoPayload &payload);
PaintEngine createPaintEngine_ABGR16_LE(const PixelFormat::Data *d, const UncompressedVideoPayload &payload);
PaintEngine createPaintEngine_YUV8_444(const PixelFormat::Data *d, const UncompressedVideoPayload &payload);
PaintEngine createPaintEngine_YUV10_444_LE(const PixelFormat::Data *d, const UncompressedVideoPayload &payload);
PaintEngine createPaintEngine_YUV12_444_LE(const PixelFormat::Data *d, const UncompressedVideoPayload &payload);
PaintEngine createPaintEngine_YUV16_444_LE(const PixelFormat::Data *d, const UncompressedVideoPayload &payload);
PaintEngine createPaintEngine_Mono8(const PixelFormat::Data *d, const UncompressedVideoPayload &payload);
PaintEngine createPaintEngine_Mono10_LE(const PixelFormat::Data *d, const UncompressedVideoPayload &payload);
PaintEngine createPaintEngine_Mono12_LE(const PixelFormat::Data *d, const UncompressedVideoPayload &payload);
PaintEngine createPaintEngine_Mono16_LE(const PixelFormat::Data *d, const UncompressedVideoPayload &payload);

// paintengine_422.cpp factories
PaintEngine createPaintEngine_YUYV8(const PixelFormat::Data *d, const UncompressedVideoPayload &payload);
PaintEngine createPaintEngine_YUYV10_LE(const PixelFormat::Data *d, const UncompressedVideoPayload &payload);
PaintEngine createPaintEngine_UYVY8(const PixelFormat::Data *d, const UncompressedVideoPayload &payload);
PaintEngine createPaintEngine_UYVY10_LE(const PixelFormat::Data *d, const UncompressedVideoPayload &payload);
PaintEngine createPaintEngine_UYVY12_LE(const PixelFormat::Data *d, const UncompressedVideoPayload &payload);
PaintEngine createPaintEngine_UYVY16_LE(const PixelFormat::Data *d, const UncompressedVideoPayload &payload);

// paintengine_multiplane.cpp factories
PaintEngine createPaintEngine_MultiPlane8(const PixelFormat::Data *d, const UncompressedVideoPayload &payload);
PaintEngine createPaintEngine_MultiPlane10_LE(const PixelFormat::Data *d, const UncompressedVideoPayload &payload);
PaintEngine createPaintEngine_MultiPlane12_LE(const PixelFormat::Data *d, const UncompressedVideoPayload &payload);
PaintEngine createPaintEngine_MultiPlane16_LE(const PixelFormat::Data *d, const UncompressedVideoPayload &payload);

// paintengine_packed.cpp factories
PaintEngine createPaintEngine_DPX_A(const PixelFormat::Data *d, const UncompressedVideoPayload &payload);
PaintEngine createPaintEngine_DPX_B(const PixelFormat::Data *d, const UncompressedVideoPayload &payload);
PaintEngine createPaintEngine_v210(const PixelFormat::Data *d, const UncompressedVideoPayload &payload);

// ---------------------------------------------------------------------------
// Register paint engine factories with PixelFormat entries.
// ---------------------------------------------------------------------------

static struct PixelFormatPaintEngineInit {
                PixelFormatPaintEngineInit() {
                        auto patch = [](PixelFormat::ID id, PaintEngine (*func)(const PixelFormat::Data *,
                                                                                const UncompressedVideoPayload &)) {
                                PixelFormat pd(id);
                                if (!pd.isValid()) return;
                                PixelFormat::Data d = *pd.data();
                                d.createPaintEngineFunc = func;
                                PixelFormat::registerData(std::move(d));
                        };

                        // RGBA/RGB 8-bit
                        patch(PixelFormat::RGBA8_sRGB, createPaintEngine_RGBA8);
                        patch(PixelFormat::RGB8_sRGB, createPaintEngine_RGB8);

                        // BGRA/BGR 8-bit
                        patch(PixelFormat::BGRA8_sRGB, createPaintEngine_BGRA8);
                        patch(PixelFormat::BGR8_sRGB, createPaintEngine_BGR8);

                        // ARGB/ABGR 8-bit
                        patch(PixelFormat::ARGB8_sRGB, createPaintEngine_ARGB8);
                        patch(PixelFormat::ABGR8_sRGB, createPaintEngine_ABGR8);

                        // RGBA/RGB 10-bit LE
                        patch(PixelFormat::RGBA10_LE_sRGB, createPaintEngine_RGBA10_LE);
                        patch(PixelFormat::RGB10_LE_sRGB, createPaintEngine_RGB10_LE);

                        // BGRA/BGR 10-bit LE
                        patch(PixelFormat::BGRA10_LE_sRGB, createPaintEngine_BGRA10_LE);
                        patch(PixelFormat::BGR10_LE_sRGB, createPaintEngine_BGR10_LE);

                        // ARGB/ABGR 10-bit LE
                        patch(PixelFormat::ARGB10_LE_sRGB, createPaintEngine_ARGB10_LE);
                        patch(PixelFormat::ABGR10_LE_sRGB, createPaintEngine_ABGR10_LE);

                        // RGBA/RGB 12-bit LE
                        patch(PixelFormat::RGBA12_LE_sRGB, createPaintEngine_RGBA12_LE);
                        patch(PixelFormat::RGB12_LE_sRGB, createPaintEngine_RGB12_LE);

                        // BGRA/BGR 12-bit LE
                        patch(PixelFormat::BGRA12_LE_sRGB, createPaintEngine_BGRA12_LE);
                        patch(PixelFormat::BGR12_LE_sRGB, createPaintEngine_BGR12_LE);

                        // ARGB/ABGR 12-bit LE
                        patch(PixelFormat::ARGB12_LE_sRGB, createPaintEngine_ARGB12_LE);
                        patch(PixelFormat::ABGR12_LE_sRGB, createPaintEngine_ABGR12_LE);

                        // RGBA/RGB 16-bit LE
                        patch(PixelFormat::RGBA16_LE_sRGB, createPaintEngine_RGBA16_LE);
                        patch(PixelFormat::RGB16_LE_sRGB, createPaintEngine_RGB16_LE);

                        // BGRA/BGR 16-bit LE
                        patch(PixelFormat::BGRA16_LE_sRGB, createPaintEngine_BGRA16_LE);
                        patch(PixelFormat::BGR16_LE_sRGB, createPaintEngine_BGR16_LE);

                        // ARGB/ABGR 16-bit LE
                        patch(PixelFormat::ARGB16_LE_sRGB, createPaintEngine_ARGB16_LE);
                        patch(PixelFormat::ABGR16_LE_sRGB, createPaintEngine_ABGR16_LE);

                        // YCbCr 4:4:4 LE
                        patch(PixelFormat::YUV8_Rec709, createPaintEngine_YUV8_444);
                        patch(PixelFormat::YUV10_LE_Rec709, createPaintEngine_YUV10_444_LE);
                        patch(PixelFormat::YUV12_LE_Rec709, createPaintEngine_YUV12_444_LE);
                        patch(PixelFormat::YUV16_LE_Rec709, createPaintEngine_YUV16_444_LE);

                        // Monochrome LE
                        patch(PixelFormat::Mono8_sRGB, createPaintEngine_Mono8);
                        patch(PixelFormat::Mono10_LE_sRGB, createPaintEngine_Mono10_LE);
                        patch(PixelFormat::Mono12_LE_sRGB, createPaintEngine_Mono12_LE);
                        patch(PixelFormat::Mono16_LE_sRGB, createPaintEngine_Mono16_LE);

                        // --- 4:2:2 interleaved (YUYV) ---
                        patch(PixelFormat::YUV8_422_Rec709, createPaintEngine_YUYV8);
                        patch(PixelFormat::YUV10_422_Rec709, createPaintEngine_YUYV10_LE);
                        patch(PixelFormat::YUV8_422_Rec601, createPaintEngine_YUYV8);
                        patch(PixelFormat::YUV8_422_Rec709_Full, createPaintEngine_YUYV8);
                        patch(PixelFormat::YUV8_422_Rec601_Full, createPaintEngine_YUYV8);

                        // --- 4:2:2 interleaved (UYVY) ---
                        patch(PixelFormat::YUV8_422_UYVY_Rec709, createPaintEngine_UYVY8);
                        patch(PixelFormat::YUV8_422_UYVY_Rec601, createPaintEngine_UYVY8);
                        patch(PixelFormat::YUV10_422_UYVY_LE_Rec709, createPaintEngine_UYVY10_LE);
                        patch(PixelFormat::YUV12_422_UYVY_LE_Rec709, createPaintEngine_UYVY12_LE);
                        patch(PixelFormat::YUV16_422_UYVY_LE_Rec709, createPaintEngine_UYVY16_LE);
                        patch(PixelFormat::YUV10_422_UYVY_LE_Rec2020, createPaintEngine_UYVY10_LE);
                        patch(PixelFormat::YUV12_422_UYVY_LE_Rec2020, createPaintEngine_UYVY12_LE);

                        // --- Planar 4:2:2 LE ---
                        patch(PixelFormat::YUV8_422_Planar_Rec709, createPaintEngine_MultiPlane8);
                        patch(PixelFormat::YUV10_422_Planar_LE_Rec709, createPaintEngine_MultiPlane10_LE);
                        patch(PixelFormat::YUV12_422_Planar_LE_Rec709, createPaintEngine_MultiPlane12_LE);
                        patch(PixelFormat::YUV16_422_Planar_LE_Rec709, createPaintEngine_MultiPlane16_LE);

                        // --- Planar 4:2:0 LE ---
                        patch(PixelFormat::YUV8_420_Planar_Rec709, createPaintEngine_MultiPlane8);
                        patch(PixelFormat::YUV10_420_Planar_LE_Rec709, createPaintEngine_MultiPlane10_LE);
                        patch(PixelFormat::YUV12_420_Planar_LE_Rec709, createPaintEngine_MultiPlane12_LE);
                        patch(PixelFormat::YUV16_420_Planar_LE_Rec709, createPaintEngine_MultiPlane16_LE);

                        // --- Planar 4:2:0 LE (Rec.2020) ---
                        patch(PixelFormat::YUV10_420_Planar_LE_Rec2020, createPaintEngine_MultiPlane10_LE);
                        patch(PixelFormat::YUV12_420_Planar_LE_Rec2020, createPaintEngine_MultiPlane12_LE);

                        // --- Planar 4:2:0 LE (Rec.601 / full-range) ---
                        patch(PixelFormat::YUV8_420_Planar_Rec601, createPaintEngine_MultiPlane8);
                        patch(PixelFormat::YUV8_420_Planar_Rec709_Full, createPaintEngine_MultiPlane8);
                        patch(PixelFormat::YUV8_420_Planar_Rec601_Full, createPaintEngine_MultiPlane8);

                        // --- Planar 4:1:1 ---
                        patch(PixelFormat::YUV8_411_Planar_Rec709, createPaintEngine_MultiPlane8);

                        // --- Planar RGB ---
                        patch(PixelFormat::RGB8_Planar_sRGB, createPaintEngine_MultiPlane8);

                        // --- Semi-planar 4:2:0 (NV12) LE ---
                        patch(PixelFormat::YUV8_420_SemiPlanar_Rec709, createPaintEngine_MultiPlane8);
                        patch(PixelFormat::YUV10_420_SemiPlanar_LE_Rec709, createPaintEngine_MultiPlane10_LE);
                        patch(PixelFormat::YUV12_420_SemiPlanar_LE_Rec709, createPaintEngine_MultiPlane12_LE);
                        patch(PixelFormat::YUV16_420_SemiPlanar_LE_Rec709, createPaintEngine_MultiPlane16_LE);

                        // --- Semi-planar 4:2:0 (NV12) Rec.601 ---
                        patch(PixelFormat::YUV8_420_SemiPlanar_Rec601, createPaintEngine_MultiPlane8);

                        // --- Semi-planar 4:2:0 (NV21) LE ---
                        patch(PixelFormat::YUV8_420_NV21_Rec709, createPaintEngine_MultiPlane8);
                        patch(PixelFormat::YUV10_420_NV21_LE_Rec709, createPaintEngine_MultiPlane10_LE);
                        patch(PixelFormat::YUV12_420_NV21_LE_Rec709, createPaintEngine_MultiPlane12_LE);

                        // --- Semi-planar 4:2:2 (NV16) LE ---
                        patch(PixelFormat::YUV8_422_SemiPlanar_Rec709, createPaintEngine_MultiPlane8);
                        patch(PixelFormat::YUV10_422_SemiPlanar_LE_Rec709, createPaintEngine_MultiPlane10_LE);
                        patch(PixelFormat::YUV12_422_SemiPlanar_LE_Rec709, createPaintEngine_MultiPlane12_LE);

                        // --- DPX 3x10 packed ---
                        patch(PixelFormat::RGB10_DPX_sRGB, createPaintEngine_DPX_A);
                        patch(PixelFormat::RGB10_DPX_LE_sRGB, createPaintEngine_DPX_A);
                        patch(PixelFormat::YUV10_DPX_Rec709, createPaintEngine_DPX_A);
                        patch(PixelFormat::YUV10_DPX_B_Rec709, createPaintEngine_DPX_B);

                        // --- v210 packed 4:2:2 ---
                        patch(PixelFormat::YUV10_422_v210_Rec709, createPaintEngine_v210);
                }
} __pixelFormatPaintEngineInit;

// ---------------------------------------------------------------------------
// Methods that depend on ImageDesc and related types
// ---------------------------------------------------------------------------

size_t PixelFormat::lineStride(size_t planeIndex, const ImageDesc &desc) const {
        if (d->compressed) return 0;
        return d->memLayout.lineStride(planeIndex, desc.width(), desc.linePad(), desc.lineAlign());
}

size_t PixelFormat::planeSize(size_t planeIndex, const ImageDesc &desc) const {
        if (d->compressed) {
                if (!desc.metadata().contains(Metadata::CompressedSize)) return 0;
                return desc.metadata().get(Metadata::CompressedSize).get<size_t>();
        }
        return d->memLayout.planeSize(planeIndex, desc.width(), desc.height(), desc.linePad(), desc.lineAlign());
}

PaintEngine PixelFormat::createPaintEngine(const UncompressedVideoPayload &payload) const {
        if (d->createPaintEngineFunc == nullptr) return PaintEngine();
        return d->createPaintEngineFunc(d, payload);
}

PROMEKI_NAMESPACE_END
