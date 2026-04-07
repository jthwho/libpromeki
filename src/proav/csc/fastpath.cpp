/**
 * @file      fastpath.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "src/proav/csc/fastpath-inl.h"
#include "hwy/foreach_target.h"  // IWYU pragma: keep
#include "hwy/highway.h"
#include "src/proav/csc/fastpath-inl.h"

#if HWY_ONCE

#include <promeki/cscregistry.h>
#include <promeki/csccontext.h>
#include <promeki/pixeldesc.h>

namespace promeki {
namespace csc {

HWY_EXPORT(FastPathBGRA8toRGBA8);
HWY_EXPORT(FastPathRGBA8toBGRA8);
HWY_EXPORT(FastPathRGBA8toRGB8);
HWY_EXPORT(FastPathRGB8toRGBA8);
HWY_EXPORT(FastPathYUYV8toRGBA8);
HWY_EXPORT(FastPathRGBA8toYUYV8);
HWY_EXPORT(FastPathNV12toRGBA8);
HWY_EXPORT(FastPathRGBA8toNV12);
HWY_EXPORT(FastPathUYVY8toRGBA8);
HWY_EXPORT(FastPathRGBA8toUYVY8);
HWY_EXPORT(FastPathNV21toRGBA8);
HWY_EXPORT(FastPathRGBA8toNV21);
HWY_EXPORT(FastPathNV16toRGBA8);
HWY_EXPORT(FastPathRGBA8toNV16);
HWY_EXPORT(FastPathPlanar422toRGBA8);
HWY_EXPORT(FastPathRGBA8toPlanar422);
HWY_EXPORT(FastPathPlanar420toRGBA8);
HWY_EXPORT(FastPathRGBA8toPlanar420);
HWY_EXPORT(FastPathYUYV601toRGBA8);
HWY_EXPORT(FastPathRGBA8toYUYV601);
HWY_EXPORT(FastPathUYVY601toRGBA8);
HWY_EXPORT(FastPathRGBA8toUYVY601);
HWY_EXPORT(FastPathNV12_601toRGBA8);
HWY_EXPORT(FastPathRGBA8toNV12_601);
HWY_EXPORT(FastPathPlanar601_420toRGBA8);
HWY_EXPORT(FastPathRGBA8toPlanar601_420);
HWY_EXPORT(FastPathUYVY10LE_2020toRGBA10LE);
HWY_EXPORT(FastPathRGBA10LEtoUYVY10LE_2020);
HWY_EXPORT(FastPathPlanar10LE420_2020toRGBA10LE);
HWY_EXPORT(FastPathRGBA10LEtoPlanar10LE420_2020);
HWY_EXPORT(FastPathUYVY10LEtoRGBA10LE);
HWY_EXPORT(FastPathRGBA10LEtoUYVY10LE);
HWY_EXPORT(FastPathPlanar10LE422toRGBA10LE);
HWY_EXPORT(FastPathRGBA10LEtoPlanar10LE422);
HWY_EXPORT(FastPathPlanar10LE420toRGBA10LE);
HWY_EXPORT(FastPathRGBA10LEtoPlanar10LE420);
HWY_EXPORT(FastPathNV12_10LEtoRGBA10LE);
HWY_EXPORT(FastPathRGBA10LEtoNV12_10LE);
HWY_EXPORT(FastPathV210toRGBA10LE);
HWY_EXPORT(FastPathRGBA10LEtoV210);
HWY_EXPORT(FastPathV210toRGBA8);
HWY_EXPORT(FastPathRGBA8toV210);
HWY_EXPORT(FastPathUYVY12LE_709toRGBA12LE);
HWY_EXPORT(FastPathRGBA12LEtoUYVY12LE_709);
HWY_EXPORT(FastPathPlanar12LE422_709toRGBA12LE);
HWY_EXPORT(FastPathRGBA12LEtoPlanar12LE422_709);
HWY_EXPORT(FastPathPlanar12LE420_709toRGBA12LE);
HWY_EXPORT(FastPathRGBA12LEtoPlanar12LE420_709);
HWY_EXPORT(FastPathNV12_12LE_709toRGBA12LE);
HWY_EXPORT(FastPathRGBA12LEtoNV12_12LE_709);
HWY_EXPORT(FastPathUYVY12LE_2020toRGBA12LE);
HWY_EXPORT(FastPathRGBA12LEtoUYVY12LE_2020);
HWY_EXPORT(FastPathPlanar12LE420_2020toRGBA12LE);
HWY_EXPORT(FastPathRGBA12LEtoPlanar12LE420_2020);

// Generate dispatch wrappers via macro to avoid repetitive boilerplate
#define CSC_DISPATCH(Name) \
static void dispatch_##Name(const void *const *s, const size_t *ss, \
                            void *const *d, const size_t *ds, \
                            size_t w, CSCContext &c) { \
        HWY_DYNAMIC_DISPATCH(Name)(s, ss, d, ds, w, c); \
}

CSC_DISPATCH(FastPathBGRA8toRGBA8)
CSC_DISPATCH(FastPathRGBA8toBGRA8)
CSC_DISPATCH(FastPathRGBA8toRGB8)
CSC_DISPATCH(FastPathRGB8toRGBA8)
CSC_DISPATCH(FastPathYUYV8toRGBA8)
CSC_DISPATCH(FastPathRGBA8toYUYV8)
CSC_DISPATCH(FastPathNV12toRGBA8)
CSC_DISPATCH(FastPathRGBA8toNV12)
CSC_DISPATCH(FastPathUYVY8toRGBA8)
CSC_DISPATCH(FastPathRGBA8toUYVY8)
CSC_DISPATCH(FastPathNV21toRGBA8)
CSC_DISPATCH(FastPathRGBA8toNV21)
CSC_DISPATCH(FastPathNV16toRGBA8)
CSC_DISPATCH(FastPathRGBA8toNV16)
CSC_DISPATCH(FastPathPlanar422toRGBA8)
CSC_DISPATCH(FastPathRGBA8toPlanar422)
CSC_DISPATCH(FastPathPlanar420toRGBA8)
CSC_DISPATCH(FastPathRGBA8toPlanar420)
CSC_DISPATCH(FastPathYUYV601toRGBA8)
CSC_DISPATCH(FastPathRGBA8toYUYV601)
CSC_DISPATCH(FastPathUYVY601toRGBA8)
CSC_DISPATCH(FastPathRGBA8toUYVY601)
CSC_DISPATCH(FastPathNV12_601toRGBA8)
CSC_DISPATCH(FastPathRGBA8toNV12_601)
CSC_DISPATCH(FastPathPlanar601_420toRGBA8)
CSC_DISPATCH(FastPathRGBA8toPlanar601_420)
CSC_DISPATCH(FastPathUYVY10LE_2020toRGBA10LE)
CSC_DISPATCH(FastPathRGBA10LEtoUYVY10LE_2020)
CSC_DISPATCH(FastPathPlanar10LE420_2020toRGBA10LE)
CSC_DISPATCH(FastPathRGBA10LEtoPlanar10LE420_2020)
CSC_DISPATCH(FastPathUYVY10LEtoRGBA10LE)
CSC_DISPATCH(FastPathRGBA10LEtoUYVY10LE)
CSC_DISPATCH(FastPathPlanar10LE422toRGBA10LE)
CSC_DISPATCH(FastPathRGBA10LEtoPlanar10LE422)
CSC_DISPATCH(FastPathPlanar10LE420toRGBA10LE)
CSC_DISPATCH(FastPathRGBA10LEtoPlanar10LE420)
CSC_DISPATCH(FastPathNV12_10LEtoRGBA10LE)
CSC_DISPATCH(FastPathRGBA10LEtoNV12_10LE)
CSC_DISPATCH(FastPathV210toRGBA10LE)
CSC_DISPATCH(FastPathRGBA10LEtoV210)
CSC_DISPATCH(FastPathV210toRGBA8)
CSC_DISPATCH(FastPathRGBA8toV210)
CSC_DISPATCH(FastPathUYVY12LE_709toRGBA12LE)
CSC_DISPATCH(FastPathRGBA12LEtoUYVY12LE_709)
CSC_DISPATCH(FastPathPlanar12LE422_709toRGBA12LE)
CSC_DISPATCH(FastPathRGBA12LEtoPlanar12LE422_709)
CSC_DISPATCH(FastPathPlanar12LE420_709toRGBA12LE)
CSC_DISPATCH(FastPathRGBA12LEtoPlanar12LE420_709)
CSC_DISPATCH(FastPathNV12_12LE_709toRGBA12LE)
CSC_DISPATCH(FastPathRGBA12LEtoNV12_12LE_709)
CSC_DISPATCH(FastPathUYVY12LE_2020toRGBA12LE)
CSC_DISPATCH(FastPathRGBA12LEtoUYVY12LE_2020)
CSC_DISPATCH(FastPathPlanar12LE420_2020toRGBA12LE)
CSC_DISPATCH(FastPathRGBA12LEtoPlanar12LE420_2020)

#undef CSC_DISPATCH

// Static registration at init time
static struct FastPathRegistrar {
        FastPathRegistrar() {
                auto reg = CSCRegistry::registerFastPath;

                // Same-model format changes
                reg(PixelDesc::BGRA8_sRGB, PixelDesc::RGBA8_sRGB, dispatch_FastPathBGRA8toRGBA8);
                reg(PixelDesc::RGBA8_sRGB, PixelDesc::BGRA8_sRGB, dispatch_FastPathRGBA8toBGRA8);
                reg(PixelDesc::RGBA8_sRGB, PixelDesc::RGB8_sRGB,  dispatch_FastPathRGBA8toRGB8);
                reg(PixelDesc::RGB8_sRGB,  PixelDesc::RGBA8_sRGB, dispatch_FastPathRGB8toRGBA8);

                // YUYV interleaved
                reg(PixelDesc::YUV8_422_Rec709,          PixelDesc::RGBA8_sRGB, dispatch_FastPathYUYV8toRGBA8);
                reg(PixelDesc::RGBA8_sRGB,               PixelDesc::YUV8_422_Rec709, dispatch_FastPathRGBA8toYUYV8);

                // UYVY interleaved
                reg(PixelDesc::YUV8_422_UYVY_Rec709,     PixelDesc::RGBA8_sRGB, dispatch_FastPathUYVY8toRGBA8);
                reg(PixelDesc::RGBA8_sRGB,               PixelDesc::YUV8_422_UYVY_Rec709, dispatch_FastPathRGBA8toUYVY8);

                // NV12 (420 semi-planar, CbCr)
                reg(PixelDesc::YUV8_420_SemiPlanar_Rec709, PixelDesc::RGBA8_sRGB, dispatch_FastPathNV12toRGBA8);
                reg(PixelDesc::RGBA8_sRGB,               PixelDesc::YUV8_420_SemiPlanar_Rec709, dispatch_FastPathRGBA8toNV12);

                // NV21 (420 semi-planar, CrCb)
                reg(PixelDesc::YUV8_420_NV21_Rec709,     PixelDesc::RGBA8_sRGB, dispatch_FastPathNV21toRGBA8);
                reg(PixelDesc::RGBA8_sRGB,               PixelDesc::YUV8_420_NV21_Rec709, dispatch_FastPathRGBA8toNV21);

                // NV16 (422 semi-planar, CbCr)
                reg(PixelDesc::YUV8_422_SemiPlanar_Rec709, PixelDesc::RGBA8_sRGB, dispatch_FastPathNV16toRGBA8);
                reg(PixelDesc::RGBA8_sRGB,               PixelDesc::YUV8_422_SemiPlanar_Rec709, dispatch_FastPathRGBA8toNV16);

                // Planar 422
                reg(PixelDesc::YUV8_422_Planar_Rec709,   PixelDesc::RGBA8_sRGB, dispatch_FastPathPlanar422toRGBA8);
                reg(PixelDesc::RGBA8_sRGB,               PixelDesc::YUV8_422_Planar_Rec709, dispatch_FastPathRGBA8toPlanar422);

                // Planar 420
                reg(PixelDesc::YUV8_420_Planar_Rec709,   PixelDesc::RGBA8_sRGB, dispatch_FastPathPlanar420toRGBA8);
                reg(PixelDesc::RGBA8_sRGB,               PixelDesc::YUV8_420_Planar_Rec709, dispatch_FastPathRGBA8toPlanar420);

                // --- Rec.601 8-bit fast paths ---

                // YUYV Rec.601
                reg(PixelDesc::YUV8_422_Rec601,          PixelDesc::RGBA8_sRGB, dispatch_FastPathYUYV601toRGBA8);
                reg(PixelDesc::RGBA8_sRGB,               PixelDesc::YUV8_422_Rec601, dispatch_FastPathRGBA8toYUYV601);

                // UYVY Rec.601
                reg(PixelDesc::YUV8_422_UYVY_Rec601,     PixelDesc::RGBA8_sRGB, dispatch_FastPathUYVY601toRGBA8);
                reg(PixelDesc::RGBA8_sRGB,               PixelDesc::YUV8_422_UYVY_Rec601, dispatch_FastPathRGBA8toUYVY601);

                // NV12 Rec.601
                reg(PixelDesc::YUV8_420_SemiPlanar_Rec601, PixelDesc::RGBA8_sRGB, dispatch_FastPathNV12_601toRGBA8);
                reg(PixelDesc::RGBA8_sRGB,               PixelDesc::YUV8_420_SemiPlanar_Rec601, dispatch_FastPathRGBA8toNV12_601);

                // Planar 420 Rec.601
                reg(PixelDesc::YUV8_420_Planar_Rec601,   PixelDesc::RGBA8_sRGB, dispatch_FastPathPlanar601_420toRGBA8);
                reg(PixelDesc::RGBA8_sRGB,               PixelDesc::YUV8_420_Planar_Rec601, dispatch_FastPathRGBA8toPlanar601_420);

                // --- 10-bit LE fast paths ---

                // 10-bit UYVY LE
                reg(PixelDesc::YUV10_422_UYVY_LE_Rec709, PixelDesc::RGBA10_LE_sRGB, dispatch_FastPathUYVY10LEtoRGBA10LE);
                reg(PixelDesc::RGBA10_LE_sRGB,           PixelDesc::YUV10_422_UYVY_LE_Rec709, dispatch_FastPathRGBA10LEtoUYVY10LE);

                // 10-bit Planar 422 LE
                reg(PixelDesc::YUV10_422_Planar_LE_Rec709, PixelDesc::RGBA10_LE_sRGB, dispatch_FastPathPlanar10LE422toRGBA10LE);
                reg(PixelDesc::RGBA10_LE_sRGB,             PixelDesc::YUV10_422_Planar_LE_Rec709, dispatch_FastPathRGBA10LEtoPlanar10LE422);

                // 10-bit Planar 420 LE
                reg(PixelDesc::YUV10_420_Planar_LE_Rec709, PixelDesc::RGBA10_LE_sRGB, dispatch_FastPathPlanar10LE420toRGBA10LE);
                reg(PixelDesc::RGBA10_LE_sRGB,             PixelDesc::YUV10_420_Planar_LE_Rec709, dispatch_FastPathRGBA10LEtoPlanar10LE420);

                // 10-bit NV12 LE (420 semi-planar)
                reg(PixelDesc::YUV10_420_SemiPlanar_LE_Rec709, PixelDesc::RGBA10_LE_sRGB, dispatch_FastPathNV12_10LEtoRGBA10LE);
                reg(PixelDesc::RGBA10_LE_sRGB,                 PixelDesc::YUV10_420_SemiPlanar_LE_Rec709, dispatch_FastPathRGBA10LEtoNV12_10LE);

                // v210 <-> RGBA10_LE
                reg(PixelDesc::YUV10_422_v210_Rec709, PixelDesc::RGBA10_LE_sRGB, dispatch_FastPathV210toRGBA10LE);
                reg(PixelDesc::RGBA10_LE_sRGB,        PixelDesc::YUV10_422_v210_Rec709, dispatch_FastPathRGBA10LEtoV210);

                // v210 <-> RGBA8 (wraps the 10-bit path with an
                // inline bit-depth conversion — the generic pipeline
                // can't handle v210's bit packing).
                reg(PixelDesc::YUV10_422_v210_Rec709, PixelDesc::RGBA8_sRGB, dispatch_FastPathV210toRGBA8);
                reg(PixelDesc::RGBA8_sRGB,            PixelDesc::YUV10_422_v210_Rec709, dispatch_FastPathRGBA8toV210);

                // --- Rec.2020 10-bit LE fast paths ---

                // UYVY 10-bit LE Rec.2020
                reg(PixelDesc::YUV10_422_UYVY_LE_Rec2020, PixelDesc::RGBA10_LE_sRGB, dispatch_FastPathUYVY10LE_2020toRGBA10LE);
                reg(PixelDesc::RGBA10_LE_sRGB,             PixelDesc::YUV10_422_UYVY_LE_Rec2020, dispatch_FastPathRGBA10LEtoUYVY10LE_2020);

                // Planar 420 10-bit LE Rec.2020
                reg(PixelDesc::YUV10_420_Planar_LE_Rec2020, PixelDesc::RGBA10_LE_sRGB, dispatch_FastPathPlanar10LE420_2020toRGBA10LE);
                reg(PixelDesc::RGBA10_LE_sRGB,              PixelDesc::YUV10_420_Planar_LE_Rec2020, dispatch_FastPathRGBA10LEtoPlanar10LE420_2020);

                // --- 12-bit LE BT.709 fast paths ---

                // UYVY 12-bit LE Rec.709
                reg(PixelDesc::YUV12_422_UYVY_LE_Rec709, PixelDesc::RGBA12_LE_sRGB, dispatch_FastPathUYVY12LE_709toRGBA12LE);
                reg(PixelDesc::RGBA12_LE_sRGB,           PixelDesc::YUV12_422_UYVY_LE_Rec709, dispatch_FastPathRGBA12LEtoUYVY12LE_709);

                // Planar 422 12-bit LE Rec.709
                reg(PixelDesc::YUV12_422_Planar_LE_Rec709, PixelDesc::RGBA12_LE_sRGB, dispatch_FastPathPlanar12LE422_709toRGBA12LE);
                reg(PixelDesc::RGBA12_LE_sRGB,             PixelDesc::YUV12_422_Planar_LE_Rec709, dispatch_FastPathRGBA12LEtoPlanar12LE422_709);

                // Planar 420 12-bit LE Rec.709
                reg(PixelDesc::YUV12_420_Planar_LE_Rec709, PixelDesc::RGBA12_LE_sRGB, dispatch_FastPathPlanar12LE420_709toRGBA12LE);
                reg(PixelDesc::RGBA12_LE_sRGB,             PixelDesc::YUV12_420_Planar_LE_Rec709, dispatch_FastPathRGBA12LEtoPlanar12LE420_709);

                // NV12 12-bit LE Rec.709
                reg(PixelDesc::YUV12_420_SemiPlanar_LE_Rec709, PixelDesc::RGBA12_LE_sRGB, dispatch_FastPathNV12_12LE_709toRGBA12LE);
                reg(PixelDesc::RGBA12_LE_sRGB,                 PixelDesc::YUV12_420_SemiPlanar_LE_Rec709, dispatch_FastPathRGBA12LEtoNV12_12LE_709);

                // --- 12-bit LE BT.2020 fast paths ---

                // UYVY 12-bit LE Rec.2020
                reg(PixelDesc::YUV12_422_UYVY_LE_Rec2020, PixelDesc::RGBA12_LE_sRGB, dispatch_FastPathUYVY12LE_2020toRGBA12LE);
                reg(PixelDesc::RGBA12_LE_sRGB,             PixelDesc::YUV12_422_UYVY_LE_Rec2020, dispatch_FastPathRGBA12LEtoUYVY12LE_2020);

                // Planar 420 12-bit LE Rec.2020
                reg(PixelDesc::YUV12_420_Planar_LE_Rec2020, PixelDesc::RGBA12_LE_sRGB, dispatch_FastPathPlanar12LE420_2020toRGBA12LE);
                reg(PixelDesc::RGBA12_LE_sRGB,              PixelDesc::YUV12_420_Planar_LE_Rec2020, dispatch_FastPathRGBA12LEtoPlanar12LE420_2020);
        }
} __fastPathRegistrar;

}  // namespace csc
}  // namespace promeki

#endif  // HWY_ONCE
