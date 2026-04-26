/**
 * @file      pixelformat.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/list.h>
#include <promeki/error.h>
#include <promeki/fourcc.h>
#include <promeki/colormodel.h>
#include <promeki/pixelmemlayout.h>
#include <promeki/videocodec.h>
#include <promeki/enums.h>

PROMEKI_NAMESPACE_BEGIN

class Image;
class ImageDesc;
class UncompressedVideoPayload;
class PaintEngine;

/**
 * @brief Complete pixel description combining format, color model, and semantics.
 * @ingroup proav
 *
 * Uses the @ref typeregistry "TypeRegistry pattern": a lightweight inline
 * wrapper around an immutable Data record, identified by an integer ID.
 *
 * PixelFormat combines a PixelMemLayout (memory layout) with a ColorModel (color
 * semantics), per-component value ranges, compression information, and
 * paint engine creation.  It fully describes everything needed to interpret
 * and manipulate pixel data.
 *
 * @par Naming Convention
 *
 * PixelFormat names (both C++ identifiers and string names) follow the form:
 * @code
 * [<codec>_]<comps><bits>[_<subsampling>][_<layout>][_<endian>]_<colormodel>[_<range>]
 * @endcode
 *
 * Where:
 * - @b codec — Compression codec prefix, e.g. "JPEG".  Omitted for
 *   uncompressed formats.
 * - @b comps — Component abbreviation indicating the component order
 *   in memory:
 *   - "RGBA", "RGB" — standard red-green-blue order.
 *   - "BGR", "BGRA" — reversed component order (blue first).
 *   - "ARGB" — alpha-first, then red-green-blue.
 *   - "ABGR" — alpha-first, then blue-green-red.
 *   - "Mono" — single-component grayscale / monochrome.
 *   - "YUV" — Y'CbCr (luma + chroma).
 *   .
 *   These represent different component orderings in memory; the
 *   underlying PixelMemLayout is the same for all orderings with the
 *   same component count and bit depth.
 * - @b bits — Bit depth per component: 8, 10, 12, 16, or float
 *   designators "F16" (half-precision IEEE 754) or "F32"
 *   (single-precision IEEE 754).  For example, "RGBAF16" means
 *   half-float RGBA and "MonoF32" means single-float monochrome.
 *   For mixed bit-depth packed formats, "RGB10A2" and "BGR10A2"
 *   indicate 10 bits per color channel and 2 bits for alpha
 *   (3x10 + 1x2 packed in 32 bits).
 * - @b subsampling — Chroma subsampling ratio, e.g. "422", "420", or
 *   "411".  Omitted for 4:4:4 (no subsampling).
 * - @b layout — Memory layout or packing variant: "DPX" (DPX/Cineon
 *   packed), "UYVY", "v210", "Planar", "SemiPlanar", "NV21".
 *   Omitted for default interleaved ordering (interleaved is
 *   assumed).  "NV21" indicates CrCb chroma ordering (as opposed
 *   to the default CbCr order used by NV12/SemiPlanar).
 *   "SemiPlanar" is also used for NV16 (4:2:2 semi-planar).
 * - @b endian — Byte order: "LE" or "BE" for components stored in
 *   16-bit or 32-bit words.  Omitted for byte-aligned formats
 *   (8-bit) where endianness is irrelevant.
 * - @b colormodel — Color model identifier:
 *   - "sRGB" — standard sRGB with gamma encoding.
 *   - "Rec709" — ITU-R BT.709 for HD Y'CbCr.
 *   - "Rec601" — ITU-R BT.601 for SD Y'CbCr.
 *   - "Rec2020" — ITU-R BT.2020 for UHD/HDR Y'CbCr.
 *   - "LinearRec709" — linear-light Rec.709 primaries (no gamma
 *     encoding).  Used for float formats (F16/F32) because gamma
 *     encoding does not apply in linear space.
 * - @b range — Value range qualifier.  For RGB, full range (0 to
 *   2^N-1) is the default and omitted; only "Limited" is explicit.
 *   For Y'CbCr, limited (broadcast-legal) range is the default and
 *   omitted; only "Full" is explicit.
 *
 * @par Examples
 * | Name                         | Description                                       |
 * |------------------------------|---------------------------------------------------|
 * | RGBA8_sRGB                   | 8-bit RGBA, sRGB, full range (default)            |
 * | BGRA8_sRGB                   | 8-bit BGRA, sRGB                                 |
 * | ARGB8_sRGB                   | 8-bit ARGB (alpha-first), sRGB                   |
 * | Mono8_sRGB                   | 8-bit grayscale, sRGB                             |
 * | RGB10_DPX_sRGB               | 10-bit RGB, DPX packed, sRGB, full range          |
 * | RGBA12_LE_sRGB               | 12-bit RGBA in 16-bit LE words, sRGB              |
 * | RGBAF16_LE_LinearRec709      | Half-float RGBA, linear Rec.709                   |
 * | MonoF32_LE_LinearRec709      | Float mono, linear Rec.709                        |
 * | RGB10A2_LE_sRGB              | 10+10+10+2 packed, sRGB                           |
 * | YUV8_Rec709                  | 8-bit Y'CbCr 4:4:4, Rec.709                      |
 * | YUV8_422_Rec709              | 8-bit Y'CbCr 4:2:2, Rec.709, limited (default)   |
 * | YUV8_422_Rec601              | 8-bit Y'CbCr 4:2:2, Rec.601                      |
 * | YUV10_422_UYVY_LE_Rec709     | 10-bit Y'CbCr 4:2:2 UYVY LE, Rec.709             |
 * | YUV10_422_UYVY_LE_Rec2020    | 10-bit Y'CbCr 4:2:2 UYVY, Rec.2020               |
 * | YUV8_420_SemiPlanar_Rec709   | 8-bit Y'CbCr 4:2:0 NV12, Rec.709                 |
 * | YUV8_420_NV21_Rec709         | 8-bit Y'CbCr 4:2:0 NV21 (CrCb order)             |
 * | YUV8_411_Planar_Rec709       | 8-bit Y'CbCr 4:1:1 planar, Rec.709               |
 * | JPEG_RGB8_sRGB               | JPEG-compressed 8-bit RGB, sRGB                   |
 * | JPEG_XS_YUV10_422_Rec709     | JPEG XS-compressed 10-bit Y'CbCr 4:2:2, Rec.709   |
 * | JPEG_XS_RGB8_sRGB            | JPEG XS-compressed 8-bit RGB, sRGB                 |
 * | RGB8_Planar_sRGB             | 8-bit planar RGB (3 planes: R, G, B), sRGB         |
 *
 * @par Example
 * @code
 * PixelFormat pd(PixelFormat::RGBA8_sRGB);
 * assert(pd.memLayout().id() == PixelMemLayout::I_4x8);
 * assert(pd.colorModel().id() == ColorModel::sRGB);
 * assert(pd.hasAlpha());
 * assert(!pd.isCompressed());
 *
 * PaintEngine pe = pd.createPaintEngine(image);
 * @endcode
 *
 * @see PixelMemLayout, ColorModel, @ref typeregistry "TypeRegistry Pattern"
 */
class PixelFormat {
        public:
                static constexpr size_t MaxComps = PixelMemLayout::MaxComps; ///< Maximum number of components.

                /**
                 * @brief Identifies a pixel description.
                 *
                 * Well-known descriptions have named enumerators.  User-defined
                 * descriptions obtain IDs from registerType().
                 */
                enum ID {
                        Invalid                            = 0,    ///< Invalid or uninitialized pixel description.

                        // -- RGB/RGBA uncompressed (8-bit, byte-aligned) --
                        RGBA8_sRGB                         = 1,    ///< 8-bit RGBA, sRGB, full range.
                        RGB8_sRGB                          = 2,    ///< 8-bit RGB, sRGB, full range.

                        // -- RGB DPX packed --
                        RGB10_DPX_sRGB                     = 3,    ///< 10-bit RGB, DPX packed (30 bits in 4 bytes), sRGB.

                        // -- YCbCr 4:2:2 interleaved YUYV --
                        YUV8_422_Rec709                    = 4,    ///< 8-bit YCbCr 4:2:2, Rec.709, limited range.
                        YUV10_422_Rec709                   = 5,    ///< 10-bit YCbCr 4:2:2, Rec.709, limited range.

                        // -- JPEG compressed --
                        JPEG_RGBA8_sRGB                    = 6,    ///< JPEG-compressed 8-bit RGBA, sRGB, full range.
                        JPEG_RGB8_sRGB                     = 7,    ///< JPEG-compressed 8-bit RGB, sRGB, full range.
                        JPEG_YUV8_422_Rec709               = 8,    ///< JPEG-compressed 8-bit YCbCr 4:2:2, Rec.709 matrix, limited range (broadcast).
                        JPEG_YUV8_420_Rec709               = 9,    ///< JPEG-compressed 8-bit YCbCr 4:2:0, Rec.709 matrix, limited range (broadcast).

                        // -- YCbCr 4:2:2 UYVY --
                        YUV8_422_UYVY_Rec709               = 10,   ///< 8-bit YCbCr 4:2:2 UYVY, Rec.709, limited range.
                        YUV10_422_UYVY_LE_Rec709           = 11,   ///< 10-bit YCbCr 4:2:2 UYVY LE, Rec.709, limited range.
                        YUV10_422_UYVY_BE_Rec709           = 12,   ///< 10-bit YCbCr 4:2:2 UYVY BE, Rec.709, limited range.
                        YUV12_422_UYVY_LE_Rec709           = 13,   ///< 12-bit YCbCr 4:2:2 UYVY LE, Rec.709, limited range.
                        YUV12_422_UYVY_BE_Rec709           = 14,   ///< 12-bit YCbCr 4:2:2 UYVY BE, Rec.709, limited range.

                        // -- YCbCr 4:2:2 v210 --
                        YUV10_422_v210_Rec709              = 15,   ///< 10-bit YCbCr 4:2:2 v210 packed, Rec.709.

                        // -- YCbCr 4:2:2 planar --
                        YUV8_422_Planar_Rec709             = 16,   ///< 8-bit YCbCr 4:2:2 planar, Rec.709, limited range.
                        YUV10_422_Planar_LE_Rec709         = 17,   ///< 10-bit YCbCr 4:2:2 planar LE, Rec.709.
                        YUV10_422_Planar_BE_Rec709         = 18,   ///< 10-bit YCbCr 4:2:2 planar BE, Rec.709.
                        YUV12_422_Planar_LE_Rec709         = 19,   ///< 12-bit YCbCr 4:2:2 planar LE, Rec.709.
                        YUV12_422_Planar_BE_Rec709         = 20,   ///< 12-bit YCbCr 4:2:2 planar BE, Rec.709.

                        // -- YCbCr 4:2:0 planar --
                        YUV8_420_Planar_Rec709             = 21,   ///< 8-bit YCbCr 4:2:0 planar, Rec.709, limited range.
                        YUV10_420_Planar_LE_Rec709         = 22,   ///< 10-bit YCbCr 4:2:0 planar LE, Rec.709.
                        YUV10_420_Planar_BE_Rec709         = 23,   ///< 10-bit YCbCr 4:2:0 planar BE, Rec.709.
                        YUV12_420_Planar_LE_Rec709         = 24,   ///< 12-bit YCbCr 4:2:0 planar LE, Rec.709.
                        YUV12_420_Planar_BE_Rec709         = 25,   ///< 12-bit YCbCr 4:2:0 planar BE, Rec.709.

                        // -- YCbCr 4:2:0 semi-planar (NV12) --
                        YUV8_420_SemiPlanar_Rec709         = 26,   ///< 8-bit YCbCr 4:2:0 NV12, Rec.709, limited range.
                        YUV10_420_SemiPlanar_LE_Rec709     = 27,   ///< 10-bit YCbCr 4:2:0 NV12 LE, Rec.709.
                        YUV10_420_SemiPlanar_BE_Rec709     = 28,   ///< 10-bit YCbCr 4:2:0 NV12 BE, Rec.709.
                        YUV12_420_SemiPlanar_LE_Rec709     = 29,   ///< 12-bit YCbCr 4:2:0 NV12 LE, Rec.709.
                        YUV12_420_SemiPlanar_BE_Rec709     = 30,   ///< 12-bit YCbCr 4:2:0 NV12 BE, Rec.709.

                        // -- RGB/RGBA 10/12/16-bit in 16-bit words --
                        RGBA10_LE_sRGB                     = 31,   ///< 10-bit RGBA in 16-bit LE words, sRGB, full range.
                        RGBA10_BE_sRGB                     = 32,   ///< 10-bit RGBA in 16-bit BE words, sRGB, full range.
                        RGB10_LE_sRGB                      = 33,   ///< 10-bit RGB in 16-bit LE words, sRGB, full range.
                        RGB10_BE_sRGB                      = 34,   ///< 10-bit RGB in 16-bit BE words, sRGB, full range.
                        RGBA12_LE_sRGB                     = 35,   ///< 12-bit RGBA in 16-bit LE words, sRGB, full range.
                        RGBA12_BE_sRGB                     = 36,   ///< 12-bit RGBA in 16-bit BE words, sRGB, full range.
                        RGB12_LE_sRGB                      = 37,   ///< 12-bit RGB in 16-bit LE words, sRGB, full range.
                        RGB12_BE_sRGB                      = 38,   ///< 12-bit RGB in 16-bit BE words, sRGB, full range.
                        RGBA16_LE_sRGB                     = 39,   ///< 16-bit RGBA LE, sRGB, full range.
                        RGBA16_BE_sRGB                     = 40,   ///< 16-bit RGBA BE, sRGB, full range.
                        RGB16_LE_sRGB                      = 41,   ///< 16-bit RGB LE, sRGB, full range.
                        RGB16_BE_sRGB                      = 42,   ///< 16-bit RGB BE, sRGB, full range.

                        // -- YCbCr 4:4:4 DPX packed --
                        YUV10_DPX_Rec709                   = 43,   ///< 10-bit YCbCr 4:4:4 DPX packed, Rec.709.

                        // -- BGRA/BGR (reversed component order) --
                        BGRA8_sRGB                         = 44,   ///< 8-bit BGRA, sRGB, full range.
                        BGR8_sRGB                          = 45,   ///< 8-bit BGR, sRGB, full range.
                        BGRA10_LE_sRGB                     = 46,   ///< 10-bit BGRA in 16-bit LE words, sRGB.
                        BGRA10_BE_sRGB                     = 47,   ///< 10-bit BGRA in 16-bit BE words, sRGB.
                        BGR10_LE_sRGB                      = 48,   ///< 10-bit BGR in 16-bit LE words, sRGB.
                        BGR10_BE_sRGB                      = 49,   ///< 10-bit BGR in 16-bit BE words, sRGB.
                        BGRA12_LE_sRGB                     = 50,   ///< 12-bit BGRA in 16-bit LE words, sRGB.
                        BGRA12_BE_sRGB                     = 51,   ///< 12-bit BGRA in 16-bit BE words, sRGB.
                        BGR12_LE_sRGB                      = 52,   ///< 12-bit BGR in 16-bit LE words, sRGB.
                        BGR12_BE_sRGB                      = 53,   ///< 12-bit BGR in 16-bit BE words, sRGB.
                        BGRA16_LE_sRGB                     = 54,   ///< 16-bit BGRA LE, sRGB.
                        BGRA16_BE_sRGB                     = 55,   ///< 16-bit BGRA BE, sRGB.
                        BGR16_LE_sRGB                      = 56,   ///< 16-bit BGR LE, sRGB.
                        BGR16_BE_sRGB                      = 57,   ///< 16-bit BGR BE, sRGB.

                        // -- ARGB (alpha-first) --
                        ARGB8_sRGB                         = 58,   ///< 8-bit ARGB, sRGB, full range.
                        ARGB10_LE_sRGB                     = 59,   ///< 10-bit ARGB in 16-bit LE words, sRGB.
                        ARGB10_BE_sRGB                     = 60,   ///< 10-bit ARGB in 16-bit BE words, sRGB.
                        ARGB12_LE_sRGB                     = 61,   ///< 12-bit ARGB in 16-bit LE words, sRGB.
                        ARGB12_BE_sRGB                     = 62,   ///< 12-bit ARGB in 16-bit BE words, sRGB.
                        ARGB16_LE_sRGB                     = 63,   ///< 16-bit ARGB LE, sRGB.
                        ARGB16_BE_sRGB                     = 64,   ///< 16-bit ARGB BE, sRGB.

                        // -- ABGR (alpha-first, blue-first) --
                        ABGR8_sRGB                         = 65,   ///< 8-bit ABGR, sRGB, full range.
                        ABGR10_LE_sRGB                     = 66,   ///< 10-bit ABGR in 16-bit LE words, sRGB.
                        ABGR10_BE_sRGB                     = 67,   ///< 10-bit ABGR in 16-bit BE words, sRGB.
                        ABGR12_LE_sRGB                     = 68,   ///< 12-bit ABGR in 16-bit LE words, sRGB.
                        ABGR12_BE_sRGB                     = 69,   ///< 12-bit ABGR in 16-bit BE words, sRGB.
                        ABGR16_LE_sRGB                     = 70,   ///< 16-bit ABGR LE, sRGB.
                        ABGR16_BE_sRGB                     = 71,   ///< 16-bit ABGR BE, sRGB.

                        // -- Monochrome (sRGB luminance) --
                        Mono8_sRGB                         = 72,   ///< 8-bit grayscale, sRGB.
                        Mono10_LE_sRGB                     = 73,   ///< 10-bit grayscale in 16-bit LE word, sRGB.
                        Mono10_BE_sRGB                     = 74,   ///< 10-bit grayscale in 16-bit BE word, sRGB.
                        Mono12_LE_sRGB                     = 75,   ///< 12-bit grayscale in 16-bit LE word, sRGB.
                        Mono12_BE_sRGB                     = 76,   ///< 12-bit grayscale in 16-bit BE word, sRGB.
                        Mono16_LE_sRGB                     = 77,   ///< 16-bit grayscale LE, sRGB.
                        Mono16_BE_sRGB                     = 78,   ///< 16-bit grayscale BE, sRGB.

                        // -- Float RGBA/RGB/Mono (linear Rec.709 primaries) --
                        RGBAF16_LE_LinearRec709            = 79,   ///< Half-float RGBA LE, linear Rec.709.
                        RGBAF16_BE_LinearRec709            = 80,   ///< Half-float RGBA BE, linear Rec.709.
                        RGBF16_LE_LinearRec709             = 81,   ///< Half-float RGB LE, linear Rec.709.
                        RGBF16_BE_LinearRec709             = 82,   ///< Half-float RGB BE, linear Rec.709.
                        MonoF16_LE_LinearRec709            = 83,   ///< Half-float mono LE, linear Rec.709.
                        MonoF16_BE_LinearRec709            = 84,   ///< Half-float mono BE, linear Rec.709.
                        RGBAF32_LE_LinearRec709            = 85,   ///< Float RGBA LE, linear Rec.709.
                        RGBAF32_BE_LinearRec709            = 86,   ///< Float RGBA BE, linear Rec.709.
                        RGBF32_LE_LinearRec709             = 87,   ///< Float RGB LE, linear Rec.709.
                        RGBF32_BE_LinearRec709             = 88,   ///< Float RGB BE, linear Rec.709.
                        MonoF32_LE_LinearRec709            = 89,   ///< Float mono LE, linear Rec.709.
                        MonoF32_BE_LinearRec709            = 90,   ///< Float mono BE, linear Rec.709.

                        // -- 10:10:10:2 packed (3x10 + 1x2 in 32 bits) --
                        RGB10A2_LE_sRGB                    = 91,   ///< RGB 10-bit + Alpha 2-bit in 32-bit LE, sRGB.
                        RGB10A2_BE_sRGB                    = 92,   ///< RGB 10-bit + Alpha 2-bit in 32-bit BE, sRGB.
                        BGR10A2_LE_sRGB                    = 93,   ///< BGR 10-bit + Alpha 2-bit in 32-bit LE, sRGB.
                        BGR10A2_BE_sRGB                    = 94,   ///< BGR 10-bit + Alpha 2-bit in 32-bit BE, sRGB.

                        // -- YCbCr 4:4:4 (non-DPX, Rec.709) --
                        YUV8_Rec709                        = 95,   ///< 8-bit YCbCr 4:4:4, Rec.709, limited range.
                        YUV10_LE_Rec709                    = 96,   ///< 10-bit YCbCr 4:4:4 LE, Rec.709.
                        YUV10_BE_Rec709                    = 97,   ///< 10-bit YCbCr 4:4:4 BE, Rec.709.
                        YUV12_LE_Rec709                    = 98,   ///< 12-bit YCbCr 4:4:4 LE, Rec.709.
                        YUV12_BE_Rec709                    = 99,   ///< 12-bit YCbCr 4:4:4 BE, Rec.709.
                        YUV16_LE_Rec709                    = 100,  ///< 16-bit YCbCr 4:4:4 LE, Rec.709.
                        YUV16_BE_Rec709                    = 101,  ///< 16-bit YCbCr 4:4:4 BE, Rec.709.

                        // -- Rec.2020 YCbCr --
                        YUV10_422_UYVY_LE_Rec2020          = 102,  ///< 10-bit YCbCr 4:2:2 UYVY LE, Rec.2020.
                        YUV10_422_UYVY_BE_Rec2020          = 103,  ///< 10-bit YCbCr 4:2:2 UYVY BE, Rec.2020.
                        YUV12_422_UYVY_LE_Rec2020          = 104,  ///< 12-bit YCbCr 4:2:2 UYVY LE, Rec.2020.
                        YUV12_422_UYVY_BE_Rec2020          = 105,  ///< 12-bit YCbCr 4:2:2 UYVY BE, Rec.2020.
                        YUV10_420_Planar_LE_Rec2020        = 106,  ///< 10-bit YCbCr 4:2:0 planar LE, Rec.2020.
                        YUV10_420_Planar_BE_Rec2020        = 107,  ///< 10-bit YCbCr 4:2:0 planar BE, Rec.2020.
                        YUV12_420_Planar_LE_Rec2020        = 108,  ///< 12-bit YCbCr 4:2:0 planar LE, Rec.2020.
                        YUV12_420_Planar_BE_Rec2020        = 109,  ///< 12-bit YCbCr 4:2:0 planar BE, Rec.2020.

                        // -- Rec.601 YCbCr --
                        YUV8_422_Rec601                    = 110,  ///< 8-bit YCbCr 4:2:2, Rec.601, limited range.
                        YUV8_422_UYVY_Rec601               = 111,  ///< 8-bit YCbCr 4:2:2 UYVY, Rec.601, limited range.
                        YUV8_420_Planar_Rec601             = 112,  ///< 8-bit YCbCr 4:2:0 planar, Rec.601, limited range.
                        YUV8_420_SemiPlanar_Rec601         = 113,  ///< 8-bit YCbCr 4:2:0 NV12, Rec.601, limited range.

                        // -- NV21 (semi-planar 4:2:0, CrCb order, Rec.709) --
                        YUV8_420_NV21_Rec709               = 114,  ///< 8-bit YCbCr 4:2:0 NV21, Rec.709.
                        YUV10_420_NV21_LE_Rec709           = 115,  ///< 10-bit YCbCr 4:2:0 NV21 LE, Rec.709.
                        YUV10_420_NV21_BE_Rec709           = 116,  ///< 10-bit YCbCr 4:2:0 NV21 BE, Rec.709.
                        YUV12_420_NV21_LE_Rec709           = 117,  ///< 12-bit YCbCr 4:2:0 NV21 LE, Rec.709.
                        YUV12_420_NV21_BE_Rec709           = 118,  ///< 12-bit YCbCr 4:2:0 NV21 BE, Rec.709.

                        // -- Semi-planar 4:2:2 (NV16, Rec.709) --
                        YUV8_422_SemiPlanar_Rec709         = 119,  ///< 8-bit YCbCr 4:2:2 NV16, Rec.709.
                        YUV10_422_SemiPlanar_LE_Rec709     = 120,  ///< 10-bit YCbCr 4:2:2 NV16 LE, Rec.709.
                        YUV10_422_SemiPlanar_BE_Rec709     = 121,  ///< 10-bit YCbCr 4:2:2 NV16 BE, Rec.709.
                        YUV12_422_SemiPlanar_LE_Rec709     = 122,  ///< 12-bit YCbCr 4:2:2 NV16 LE, Rec.709.
                        YUV12_422_SemiPlanar_BE_Rec709     = 123,  ///< 12-bit YCbCr 4:2:2 NV16 BE, Rec.709.

                        // -- Planar 4:1:1 (Rec.709) --
                        YUV8_411_Planar_Rec709             = 124,  ///< 8-bit YCbCr 4:1:1 planar, Rec.709.

                        // -- 16-bit YCbCr (Rec.709) --
                        YUV16_422_UYVY_LE_Rec709           = 125,  ///< 16-bit YCbCr 4:2:2 UYVY LE, Rec.709.
                        YUV16_422_UYVY_BE_Rec709           = 126,  ///< 16-bit YCbCr 4:2:2 UYVY BE, Rec.709.
                        YUV16_422_Planar_LE_Rec709         = 127,  ///< 16-bit YCbCr 4:2:2 planar LE, Rec.709.
                        YUV16_422_Planar_BE_Rec709         = 128,  ///< 16-bit YCbCr 4:2:2 planar BE, Rec.709.
                        YUV16_420_Planar_LE_Rec709         = 129,  ///< 16-bit YCbCr 4:2:0 planar LE, Rec.709.
                        YUV16_420_Planar_BE_Rec709         = 130,  ///< 16-bit YCbCr 4:2:0 planar BE, Rec.709.
                        YUV16_420_SemiPlanar_LE_Rec709     = 131,  ///< 16-bit YCbCr 4:2:0 NV12 LE, Rec.709.
                        YUV16_420_SemiPlanar_BE_Rec709     = 132,  ///< 16-bit YCbCr 4:2:0 NV12 BE, Rec.709.

                        // -- DPX additional packed formats --
                        RGB10_DPX_LE_sRGB                  = 133,  ///< 10-bit RGB, DPX packed LE, sRGB, full range.
                        YUV10_DPX_B_Rec709                 = 134,  ///< 10-bit YCbCr 4:4:4 DPX packed method B, Rec.709.

                        // ----- Video codec compressed formats (QuickTime/MP4 family) ------------
                        H264                               = 135,  ///< H.264 / AVC compressed video (avc1/avc3).
                        HEVC                               = 136,  ///< H.265 / HEVC compressed video (hvc1/hev1).
                        AV1                                = 161,  ///< AV1 (AOMedia Video 1) compressed video (av01).
                        ProRes_422_Proxy                   = 137,  ///< Apple ProRes 422 Proxy (apco).
                        ProRes_422_LT                      = 138,  ///< Apple ProRes 422 LT (apcs).
                        ProRes_422                         = 139,  ///< Apple ProRes 422 (apcn).
                        ProRes_422_HQ                      = 140,  ///< Apple ProRes 422 HQ (apch).
                        ProRes_4444                        = 141,  ///< Apple ProRes 4444 (ap4h).
                        ProRes_4444_XQ                     = 142,  ///< Apple ProRes 4444 XQ (ap4x).

                        // -- Full-range uncompressed YCbCr --
                        //
                        // The library-wide YCbCr naming convention is that
                        // the unsuffixed form (e.g. YUV8_422_Rec709) is
                        // limited-range (16..235 Y, 16..240 Cb/Cr) to match
                        // broadcast / SDI / ST 2110 defaults.  The explicit
                        // "_Full" suffix opts in to full-range 0..255 Y and
                        // 0..255 Cb/Cr, which is what JPEG / JFIF uses and
                        // what most consumer video decoders (ffplay,
                        // browsers, libjpeg-turbo) expect.  These
                        // PixelFormats exist both as general-purpose
                        // full-range YCbCr storage and as encode-source
                        // intermediates for the full-range JPEG variants
                        // below.
                        YUV8_422_Rec709_Full               = 143,  ///< 8-bit YCbCr 4:2:2 YUYV, Rec.709 matrix, full range.
                        YUV8_422_Rec601_Full               = 144,  ///< 8-bit YCbCr 4:2:2 YUYV, Rec.601 matrix, full range.
                        YUV8_420_Planar_Rec709_Full        = 145,  ///< 8-bit YCbCr 4:2:0 planar, Rec.709 matrix, full range.
                        YUV8_420_Planar_Rec601_Full        = 146,  ///< 8-bit YCbCr 4:2:0 planar, Rec.601 matrix, full range.

                        // -- Full complement of JPEG YCbCr variants (matrix × range) --
                        //
                        // Following the library-wide YCbCr convention: the
                        // unsuffixed JPEG names (existing
                        // JPEG_YUV8_422_Rec709 / JPEG_YUV8_420_Rec709) are
                        // limited-range to stay consistent with the
                        // uncompressed YCbCr defaults.  Full-range variants
                        // take the explicit "_Full" suffix.  All eight
                        // combinations (matrix × range × subsampling) are
                        // provided; pick the one that matches what the
                        // downstream decoder expects to interpret:
                        //
                        //   - Rec.601 + full range (strict JFIF) → ffplay,
                        //     browsers, libjpeg-turbo, most consumer apps
                        //   - Rec.709 + full range → modern cameras with
                        //     an ICC profile; good for high-quality still
                        //     storage that a colour-managed viewer honours
                        //   - Rec.709 + limited range (existing default) →
                        //     broadcast / SDI JPEG pipelines (ST 2110 JPEG XS
                        //     style)
                        //   - Rec.601 + limited range → legacy broadcast
                        JPEG_YUV8_422_Rec601               = 147,  ///< JPEG-compressed 8-bit YCbCr 4:2:2, Rec.601 matrix, limited range.
                        JPEG_YUV8_420_Rec601               = 148,  ///< JPEG-compressed 8-bit YCbCr 4:2:0, Rec.601 matrix, limited range.
                        JPEG_YUV8_422_Rec709_Full          = 149,  ///< JPEG-compressed 8-bit YCbCr 4:2:2, Rec.709 matrix, full range.
                        JPEG_YUV8_420_Rec709_Full          = 150,  ///< JPEG-compressed 8-bit YCbCr 4:2:0, Rec.709 matrix, full range.
                        JPEG_YUV8_422_Rec601_Full          = 151,  ///< JPEG-compressed 8-bit YCbCr 4:2:2, Rec.601 matrix, full range (strict JFIF).
                        JPEG_YUV8_420_Rec601_Full          = 152,  ///< JPEG-compressed 8-bit YCbCr 4:2:0, Rec.601 matrix, full range (strict JFIF).

                        // -- JPEG XS (ISO/IEC 21122) compressed variants --
                        //
                        // Modern low-complexity intra-only codec.  JPEG XS
                        // carries matrix / range out-of-band (in the MP4 sample
                        // entry or RTP SDP — see RFC 9134) rather than inside
                        // the bitstream, so the compressed PixelFormats only
                        // distinguish bit depth and subsampling; Rec.709 is the
                        // canonical broadcast default and the one wired up
                        // here.  10- and 12-bit variants are first-class
                        // citizens since JPEG XS targets high-bit-depth
                        // contribution workflows.
                        JPEG_XS_YUV8_422_Rec709            = 153,  ///< JPEG XS-compressed 8-bit YCbCr 4:2:2, Rec.709, limited range.
                        JPEG_XS_YUV10_422_Rec709           = 154,  ///< JPEG XS-compressed 10-bit YCbCr 4:2:2, Rec.709, limited range.
                        JPEG_XS_YUV12_422_Rec709           = 155,  ///< JPEG XS-compressed 12-bit YCbCr 4:2:2, Rec.709, limited range.
                        JPEG_XS_YUV8_420_Rec709            = 156,  ///< JPEG XS-compressed 8-bit YCbCr 4:2:0, Rec.709, limited range.
                        JPEG_XS_YUV10_420_Rec709           = 157,  ///< JPEG XS-compressed 10-bit YCbCr 4:2:0, Rec.709, limited range.
                        JPEG_XS_YUV12_420_Rec709           = 158,  ///< JPEG XS-compressed 12-bit YCbCr 4:2:0, Rec.709, limited range.
                        JPEG_XS_RGB8_sRGB                  = 159,  ///< JPEG XS-compressed 8-bit RGB, sRGB, full range.

                        // -- Planar RGB --
                        RGB8_Planar_sRGB                   = 160,  ///< 8-bit planar RGB (3 equal-sized planes: R, G, B), sRGB, full range.

                        // -- Planar 4:4:4 YCbCr --
                        YUV8_444_Planar_Rec709             = 162,  ///< 8-bit YCbCr 4:4:4 planar, Rec.709, limited range.
                        YUV10_444_Planar_LE_Rec709         = 163,  ///< 10-bit YCbCr 4:4:4 planar LE, Rec.709.

                        UserDefined                        = 1024  ///< First ID available for user-registered types.
                };

                /** @brief List of PixelFormat IDs. */
                using IDList = List<ID>;

                /** @brief Describes the semantic meaning and value range of a single component. */
                struct CompSemantic {
                        String  name;           ///< Human-readable name (e.g. "Red", "Luma").
                        String  abbrev;         ///< Abbreviation (e.g. "R", "Y").
                        float   rangeMin = 0;   ///< Minimum valid value (e.g. 0, 16).
                        float   rangeMax = 0;   ///< Maximum valid value (e.g. 255, 235).
                };

                /** @brief Immutable data record for a pixel description. */
                struct Data {
                        ID              id = Invalid;                     ///< Unique description identifier.
                        String          name;                             ///< Short name (e.g. "RGBA8").
                        String          desc;                             ///< Human-readable description.
                        PixelMemLayout     memLayout;                      ///< Memory layout (by value).
                        ColorModel      colorModel;                      ///< Color semantics (by value).
                        bool            hasAlpha = false;                 ///< Whether this description includes alpha.
                        int             alphaCompIndex = -1;              ///< Component index for alpha (-1 = none).
                        bool            compressed = false;               ///< Whether this is a compressed format.
                        VideoCodec      videoCodec;                       ///< Codec identity for compressed formats (e.g. @c VideoCodec::H264).
                        List<ID>        encodeSources;                    ///< Uncompressed PixelFormats the codec can encode from.
                        List<ID>        decodeTargets;                    ///< Uncompressed PixelFormats the codec can decode to.
                        FourCCList      fourccList;                       ///< Associated FourCC codes.
                        CompSemantic    compSemantics[MaxComps] = {};     ///< Per-component semantics.
                        VideoRange      videoRange;                       ///< Y'CbCr / RGB quantization range.  Left at VideoRange::Unknown by default; @ref registerData auto-derives from @c compSemantics[0] when possible.

                        /**
                         * @brief Creates a PaintEngine for drawing on the given image.
                         * @param d   Pointer to this Data record.
                         * @param img The image to create a paint engine for.
                         * @return A PaintEngine, or an invalid PaintEngine if not supported.
                         */
                        PaintEngine (*createPaintEngineFunc)(const Data *d, const UncompressedVideoPayload &payload) = nullptr;
                };

                /**
                 * @brief Allocates and returns a unique ID for a user-defined pixel description.
                 * @return A unique ID value.
                 * @see registerData()
                 */
                static ID registerType();

                /**
                 * @brief Registers a Data record in the registry.
                 * @param data The populated Data struct.
                 * @see registerType()
                 */
                static void registerData(Data &&data);

                /**
                 * @brief Returns a list of all registered PixelFormat IDs.
                 *
                 * Excludes Invalid.  Includes both well-known and user-registered types.
                 *
                 * @return A list of ID values.
                 */
                static IDList registeredIDs();

                /**
                 * @brief Looks up a pixel description by name.
                 *
                 * Returns @c PixelFormat(Invalid) on miss.  Callers that
                 * need to distinguish a successful lookup of the @c "Invalid"
                 * sentinel from a genuine name miss should use the
                 * @ref lookup(const String &, Error *) overload — the
                 * sentinel name is registered so a String round-trip
                 * (toString → fromString → toString) is lossless.
                 *
                 * @param name The description name to search for.
                 * @return A PixelFormat wrapping the found description, or Invalid if not found.
                 */
                static PixelFormat lookup(const String &name);

                /**
                 * @brief Looks up a pixel description by name with explicit
                 *        error reporting.
                 *
                 * Differs from @ref lookup(const String &) in that the
                 * canonical sentinel name @c "Invalid" reports
                 * @c Error::Ok rather than being indistinguishable from a
                 * miss.  This is the round-trip-safe entry point used by
                 * the JSON / CLI parse paths so any value the library
                 * produces (including @c PixelFormat()) can be parsed
                 * back into the same Variant.
                 *
                 * @param name The description name to search for.
                 * @param err  Optional error output: @c Error::Ok on hit
                 *             (including the @c "Invalid" sentinel),
                 *             @c Error::IdNotFound on miss.
                 * @return A PixelFormat wrapping the found description.
                 *         @c PixelFormat(Invalid) on miss.
                 */
                static PixelFormat lookup(const String &name, Error *err);

                /**
                 * @brief Constructs a PixelFormat for the given ID.
                 * @param id The pixel description to use (default: Invalid).
                 */
                inline PixelFormat(ID id = Invalid);

                /** @brief Returns true if this pixel description is valid. */
                bool isValid() const { return d != nullptr && d->id != Invalid; }

                /** @brief Returns the unique ID. */
                ID id() const { return d->id; }

                /** @brief Returns the short name. */
                const String &name() const { return d->name; }

                /** @brief Returns a human-readable description. */
                const String &desc() const { return d->desc; }

                /** @brief Returns the pixel format (memory layout). */
                const PixelMemLayout &memLayout() const { return d->memLayout; }

                /** @brief Returns the color model. */
                const ColorModel &colorModel() const { return d->colorModel; }

                /**
                 * @brief Returns the Y'CbCr / RGB value range.
                 *
                 * @return @c VideoRange::Full when the format uses the
                 *         whole digital code range (e.g. 0..255 on 8-bit
                 *         RGB or full-range Y'CbCr), @c VideoRange::Limited
                 *         for studio / "video" range Y'CbCr (16..235 on
                 *         8-bit luma, 16..240 chroma), or
                 *         @c VideoRange::Unknown when the description
                 *         doesn't declare a range (user-defined formats
                 *         registered without an explicit value).
                 *
                 * Well-known PixelFormats populate this field; user-registered
                 * descriptions that leave @c Data::videoRange at
                 * @c VideoRange::Unknown fall through to an auto-derivation
                 * at @ref registerData time based on @c compSemantics[0].
                 */
                VideoRange videoRange() const { return d->videoRange; }

                /** @brief Returns true if this description includes an alpha channel. */
                bool hasAlpha() const { return d->hasAlpha; }

                /** @brief Returns the component index for alpha, or -1 if none. */
                int alphaCompIndex() const { return d->alphaCompIndex; }

                /** @brief Returns true if this is a compressed format. */
                bool isCompressed() const { return d->compressed; }

                /**
                 * @brief Returns the codec identity for compressed formats.
                 *
                 * For uncompressed formats this returns an invalid
                 * @ref VideoCodec (whose @c id() is @c VideoCodec::Invalid).
                 * Use @ref isCompressed() to gate before checking the codec.
                 */
                const VideoCodec &videoCodec() const { return d->videoCodec; }

                /**
                 * @brief Returns the uncompressed PixelFormats the codec can encode from.
                 *
                 * For a compressed PixelFormat (e.g. JPEG_RGB8_sRGB), this lists
                 * the uncompressed pixel descriptions that the codec accepts as input
                 * for encoding.  Empty for uncompressed formats.
                 *
                 * @return A const reference to the list of source PixelFormat IDs.
                 */
                const List<ID> &encodeSources() const { return d->encodeSources; }

                /**
                 * @brief Returns the uncompressed PixelFormats the codec can decode to.
                 *
                 * For a compressed PixelFormat, this lists the uncompressed pixel
                 * descriptions that the codec can produce when decoding.  Empty for
                 * uncompressed formats.
                 *
                 * @return A const reference to the list of target PixelFormat IDs.
                 */
                const List<ID> &decodeTargets() const { return d->decodeTargets; }

                /** @brief Returns the list of associated FourCC codes. */
                const FourCCList &fourccList() const { return d->fourccList; }

                /** @brief Returns the number of components. */
                size_t compCount() const { return d->memLayout.compCount(); }

                /**
                 * @brief Returns the semantic descriptor for a component.
                 * @param index Zero-based component index.
                 * @return Reference to the CompSemantic for that component.
                 */
                const CompSemantic &compSemantic(size_t index) const { return d->compSemantics[index]; }

                /** @brief Returns the number of planes. */
                size_t planeCount() const { return d->memLayout.planeCount(); }

                // The following methods depend on proav types (ImageDesc, Image,
                // PaintEngine) — defined in pixelformat.cpp.

                /**
                 * @brief Returns the line stride for a given plane, using ImageDesc for dimensions.
                 * @param planeIndex Zero-based plane index.
                 * @param desc       Image descriptor providing width, linePad, lineAlign.
                 * @return Line stride in bytes.
                 */
                size_t lineStride(size_t planeIndex, const ImageDesc &desc) const;

                /**
                 * @brief Returns the total byte size of a given plane, using ImageDesc for dimensions.
                 *
                 * For compressed formats, reads Metadata::CompressedSize from the ImageDesc.
                 *
                 * @param planeIndex Zero-based plane index.
                 * @param desc       Image descriptor providing dimensions and metadata.
                 * @return Plane size in bytes.
                 */
                size_t planeSize(size_t planeIndex, const ImageDesc &desc) const;

                /**
                 * @brief Creates a PaintEngine for drawing on the given payload.
                 *
                 * Returns an invalid PaintEngine for compressed formats.
                 *
                 * @param payload The uncompressed video payload to create a paint engine for.
                 * @return A PaintEngine configured for this pixel description.
                 */
                PaintEngine createPaintEngine(const UncompressedVideoPayload &payload) const;

                /**
                 * @brief Returns whether this pixel format has a registered
                 *        paint engine.
                 *
                 * Formats without a paint engine (e.g. YUV/YCbCr, compressed)
                 * will return an invalid no-op engine from @c createPaintEngine
                 * rather than a working one.  Callers that need to draw
                 * directly can use this to decide whether to render via an
                 * RGB scratch payload followed by
                 * @c UncompressedVideoPayload::convert().
                 *
                 * @return @c true if @c createPaintEngine would return a
                 *         working engine, @c false otherwise.
                 */
                bool hasPaintEngine() const {
                        return d != nullptr && d->createPaintEngineFunc != nullptr;
                }

                /** @brief Equality comparison (identity-based). */
                bool operator==(const PixelFormat &o) const { return d == o.d; }

                /** @brief Inequality comparison. */
                bool operator!=(const PixelFormat &o) const { return d != o.d; }

                /** @brief Returns the underlying Data pointer. */
                const Data *data() const { return d; }

        private:
                const Data *d = nullptr;
                static const Data *lookupData(ID id);
};

inline PixelFormat::PixelFormat(ID id) : d(lookupData(id)) {}

PROMEKI_NAMESPACE_END
