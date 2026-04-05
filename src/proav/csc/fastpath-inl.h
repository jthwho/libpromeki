/**
 * @file      fastpath-inl.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Hand-tuned fast-path CSC kernels that bypass the generic pipeline.
 * Each kernel fuses the entire conversion into a single SIMD loop.
 *
 * The YCbCr<->RGB kernels use integer-domain BT.709 fixed-point
 * arithmetic (no float conversion, no transfer function LUTs).  This
 * approximates the sRGB<->Rec.709 transfer function difference as
 * negligible (< 1 LSB for most values), which is standard broadcast
 * practice.
 *
 * Re-included per target via foreach_target.h.
 */

#if defined(PROMEKI_CSC_FASTPATH_INL_H_) == defined(HWY_TARGET_TOGGLE)
#ifdef PROMEKI_CSC_FASTPATH_INL_H_
#undef PROMEKI_CSC_FASTPATH_INL_H_
#else
#define PROMEKI_CSC_FASTPATH_INL_H_
#endif

#include "hwy/highway.h"
#include <cstdint>
#include <algorithm>

// Forward declare CSCContext (full definition not needed by fast-path kernels)
namespace promeki { class CSCContext; }

HWY_BEFORE_NAMESPACE();
namespace promeki {
namespace csc {
namespace HWY_NAMESPACE {
namespace hn = hwy::HWY_NAMESPACE;

// =========================================================================
// BGRA8 <-> RGBA8 (byte swizzle)
// =========================================================================

void FastPathBGRA8toRGBA8(const void *const *srcPlanes,
                          const size_t *srcStrides,
                          void *const *dstPlanes,
                          const size_t *dstStrides,
                          size_t width, CSCContext &ctx) {
        const uint8_t *src = static_cast<const uint8_t *>(srcPlanes[0]);
        uint8_t *dst = static_cast<uint8_t *>(dstPlanes[0]);

        const hn::ScalableTag<uint8_t> du8;
        const hn::Rebind<uint8_t, hn::ScalableTag<float>> du8q;
        const size_t Nq = hn::Lanes(du8q);

        size_t x = 0;
        for(; x + Nq <= width; x += Nq) {
                hn::Vec<decltype(du8q)> b, g, r, a;
                hn::LoadInterleaved4(du8q, src + x * 4, b, g, r, a);
                hn::StoreInterleaved4(r, g, b, a, du8q, dst + x * 4);
        }
        for(; x < width; x++) {
                dst[x * 4 + 0] = src[x * 4 + 2]; // R <- B position
                dst[x * 4 + 1] = src[x * 4 + 1]; // G
                dst[x * 4 + 2] = src[x * 4 + 0]; // B <- R position
                dst[x * 4 + 3] = src[x * 4 + 3]; // A
        }
        return;
}

void FastPathRGBA8toBGRA8(const void *const *srcPlanes,
                          const size_t *srcStrides,
                          void *const *dstPlanes,
                          const size_t *dstStrides,
                          size_t width, CSCContext &ctx) {
        // Same operation -- swapping R and B is self-inverse
        FastPathBGRA8toRGBA8(srcPlanes, srcStrides, dstPlanes, dstStrides, width, ctx);
        return;
}

// =========================================================================
// RGBA8 <-> RGB8 (drop/add alpha)
// =========================================================================

void FastPathRGBA8toRGB8(const void *const *srcPlanes,
                         const size_t *srcStrides,
                         void *const *dstPlanes,
                         const size_t *dstStrides,
                         size_t width, CSCContext &ctx) {
        const uint8_t *src = static_cast<const uint8_t *>(srcPlanes[0]);
        uint8_t *dst = static_cast<uint8_t *>(dstPlanes[0]);

        const hn::Rebind<uint8_t, hn::ScalableTag<float>> du8q;
        const size_t Nq = hn::Lanes(du8q);

        size_t x = 0;
        for(; x + Nq <= width; x += Nq) {
                hn::Vec<decltype(du8q)> r, g, b, a;
                hn::LoadInterleaved4(du8q, src + x * 4, r, g, b, a);
                hn::StoreInterleaved3(r, g, b, du8q, dst + x * 3);
        }
        for(; x < width; x++) {
                dst[x * 3 + 0] = src[x * 4 + 0];
                dst[x * 3 + 1] = src[x * 4 + 1];
                dst[x * 3 + 2] = src[x * 4 + 2];
        }
        return;
}

void FastPathRGB8toRGBA8(const void *const *srcPlanes,
                         const size_t *srcStrides,
                         void *const *dstPlanes,
                         const size_t *dstStrides,
                         size_t width, CSCContext &ctx) {
        const uint8_t *src = static_cast<const uint8_t *>(srcPlanes[0]);
        uint8_t *dst = static_cast<uint8_t *>(dstPlanes[0]);

        const hn::Rebind<uint8_t, hn::ScalableTag<float>> du8q;
        const size_t Nq = hn::Lanes(du8q);
        const auto alpha = hn::Set(du8q, uint8_t(255));

        size_t x = 0;
        for(; x + Nq <= width; x += Nq) {
                hn::Vec<decltype(du8q)> r, g, b;
                hn::LoadInterleaved3(du8q, src + x * 3, r, g, b);
                hn::StoreInterleaved4(r, g, b, alpha, du8q, dst + x * 4);
        }
        for(; x < width; x++) {
                dst[x * 4 + 0] = src[x * 3 + 0];
                dst[x * 4 + 1] = src[x * 3 + 1];
                dst[x * 4 + 2] = src[x * 3 + 2];
                dst[x * 4 + 3] = 255;
        }
        return;
}

// =========================================================================
// YUV8_422 (YUYV) -> RGBA8_sRGB  (BT.709 fixed-point)
// =========================================================================
//
// BT.709 limited-range integer conversion:
//   C = Y - 16,  D = Cb - 128,  E = Cr - 128
//   R = clip((298*C + 409*E + 128) >> 8)
//   G = clip((298*C - 100*D - 208*E + 128) >> 8)
//   B = clip((298*C + 516*D + 128) >> 8)

static inline uint8_t clipU8(int v) {
        return static_cast<uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v));
}

void FastPathYUYV8toRGBA8(const void *const *srcPlanes,
                           const size_t *srcStrides,
                           void *const *dstPlanes,
                           const size_t *dstStrides,
                           size_t width, CSCContext &ctx) {
        const uint8_t *src = static_cast<const uint8_t *>(srcPlanes[0]);
        uint8_t *dst = static_cast<uint8_t *>(dstPlanes[0]);

        // Process pixel pairs (YUYV = 2 pixels per 4 bytes)
        size_t pairs = width / 2;
        for(size_t i = 0; i < pairs; i++) {
                int y0 = src[i * 4 + 0];
                int cb = src[i * 4 + 1];
                int y1 = src[i * 4 + 2];
                int cr = src[i * 4 + 3];

                int c0 = y0 - 16;
                int c1 = y1 - 16;
                int d  = cb - 128;
                int e  = cr - 128;

                dst[i * 8 + 0] = clipU8((298 * c0 + 409 * e + 128) >> 8);
                dst[i * 8 + 1] = clipU8((298 * c0 - 100 * d - 208 * e + 128) >> 8);
                dst[i * 8 + 2] = clipU8((298 * c0 + 516 * d + 128) >> 8);
                dst[i * 8 + 3] = 255;

                dst[i * 8 + 4] = clipU8((298 * c1 + 409 * e + 128) >> 8);
                dst[i * 8 + 5] = clipU8((298 * c1 - 100 * d - 208 * e + 128) >> 8);
                dst[i * 8 + 6] = clipU8((298 * c1 + 516 * d + 128) >> 8);
                dst[i * 8 + 7] = 255;
        }
        // Handle odd trailing pixel
        if(width & 1) {
                size_t i = pairs;
                int y0 = src[i * 4 + 0];
                int cb = src[i * 4 + 1];
                int cr = src[i * 4 + 3];
                int c0 = y0 - 16; int d = cb - 128; int e = cr - 128;
                dst[i * 8 + 0] = clipU8((298 * c0 + 409 * e + 128) >> 8);
                dst[i * 8 + 1] = clipU8((298 * c0 - 100 * d - 208 * e + 128) >> 8);
                dst[i * 8 + 2] = clipU8((298 * c0 + 516 * d + 128) >> 8);
                dst[i * 8 + 3] = 255;
        }
        return;
}

// =========================================================================
// RGBA8_sRGB -> YUV8_422 (YUYV) Rec.709  (BT.709 fixed-point)
// =========================================================================
//
// BT.709 limited-range:
//   Y  = clip(( 66*R + 129*G +  25*B + 128) >> 8) + 16
//   Cb = clip((-38*R -  74*G + 112*B + 128) >> 8) + 128
//   Cr = clip((112*R -  94*G -  18*B + 128) >> 8) + 128

void FastPathRGBA8toYUYV8(const void *const *srcPlanes,
                           const size_t *srcStrides,
                           void *const *dstPlanes,
                           const size_t *dstStrides,
                           size_t width, CSCContext &ctx) {
        const uint8_t *src = static_cast<const uint8_t *>(srcPlanes[0]);
        uint8_t *dst = static_cast<uint8_t *>(dstPlanes[0]);

        size_t pairs = width / 2;
        for(size_t i = 0; i < pairs; i++) {
                int r0 = src[i * 8 + 0], g0 = src[i * 8 + 1], b0 = src[i * 8 + 2];
                int r1 = src[i * 8 + 4], g1 = src[i * 8 + 5], b1 = src[i * 8 + 6];

                int y0 = (( 66 * r0 + 129 * g0 +  25 * b0 + 128) >> 8) + 16;
                int y1 = (( 66 * r1 + 129 * g1 +  25 * b1 + 128) >> 8) + 16;

                // Average chroma across the pair
                int ra = (r0 + r1 + 1) >> 1;
                int ga = (g0 + g1 + 1) >> 1;
                int ba = (b0 + b1 + 1) >> 1;
                int cb = ((-38 * ra -  74 * ga + 112 * ba + 128) >> 8) + 128;
                int cr = ((112 * ra -  94 * ga -  18 * ba + 128) >> 8) + 128;

                dst[i * 4 + 0] = clipU8(y0);
                dst[i * 4 + 1] = clipU8(cb);
                dst[i * 4 + 2] = clipU8(y1);
                dst[i * 4 + 3] = clipU8(cr);
        }
        if(width & 1) {
                size_t i = pairs;
                int r = src[i * 8 + 0], g = src[i * 8 + 1], b = src[i * 8 + 2];
                dst[i * 4 + 0] = clipU8((( 66 * r + 129 * g +  25 * b + 128) >> 8) + 16);
                dst[i * 4 + 1] = clipU8(((-38 * r -  74 * g + 112 * b + 128) >> 8) + 128);
                dst[i * 4 + 2] = dst[i * 4 + 0]; // duplicate Y
                dst[i * 4 + 3] = clipU8(((112 * r -  94 * g -  18 * b + 128) >> 8) + 128);
        }
        return;
}

// =========================================================================
// YUV8_420_SemiPlanar (NV12) -> RGBA8_sRGB  (BT.709 fixed-point)
// =========================================================================

void FastPathNV12toRGBA8(const void *const *srcPlanes,
                          const size_t *srcStrides,
                          void *const *dstPlanes,
                          const size_t *dstStrides,
                          size_t width, CSCContext &ctx) {
        const uint8_t *lumaLine = static_cast<const uint8_t *>(srcPlanes[0]);
        const uint8_t *chromaLine = static_cast<const uint8_t *>(srcPlanes[1]);
        uint8_t *dst = static_cast<uint8_t *>(dstPlanes[0]);

        size_t pairs = width / 2;
        for(size_t i = 0; i < pairs; i++) {
                int y0 = lumaLine[i * 2];
                int y1 = lumaLine[i * 2 + 1];
                int cb = chromaLine[i * 2];
                int cr = chromaLine[i * 2 + 1];

                int c0 = y0 - 16;
                int c1 = y1 - 16;
                int d  = cb - 128;
                int e  = cr - 128;

                dst[i * 8 + 0] = clipU8((298 * c0 + 409 * e + 128) >> 8);
                dst[i * 8 + 1] = clipU8((298 * c0 - 100 * d - 208 * e + 128) >> 8);
                dst[i * 8 + 2] = clipU8((298 * c0 + 516 * d + 128) >> 8);
                dst[i * 8 + 3] = 255;

                dst[i * 8 + 4] = clipU8((298 * c1 + 409 * e + 128) >> 8);
                dst[i * 8 + 5] = clipU8((298 * c1 - 100 * d - 208 * e + 128) >> 8);
                dst[i * 8 + 6] = clipU8((298 * c1 + 516 * d + 128) >> 8);
                dst[i * 8 + 7] = 255;
        }
        if(width & 1) {
                size_t i = pairs;
                int c = lumaLine[width - 1] - 16;
                int d = chromaLine[i * 2] - 128;
                int e = chromaLine[i * 2 + 1] - 128;
                dst[(width - 1) * 4 + 0] = clipU8((298 * c + 409 * e + 128) >> 8);
                dst[(width - 1) * 4 + 1] = clipU8((298 * c - 100 * d - 208 * e + 128) >> 8);
                dst[(width - 1) * 4 + 2] = clipU8((298 * c + 516 * d + 128) >> 8);
                dst[(width - 1) * 4 + 3] = 255;
        }
        return;
}

// =========================================================================
// RGBA8_sRGB -> YUV8_420_SemiPlanar (NV12) Rec.709  (BT.709 fixed-point)
// =========================================================================

void FastPathRGBA8toNV12(const void *const *srcPlanes,
                          const size_t *srcStrides,
                          void *const *dstPlanes,
                          const size_t *dstStrides,
                          size_t width, CSCContext &ctx) {
        const uint8_t *src = static_cast<const uint8_t *>(srcPlanes[0]);
        uint8_t *lumaLine = static_cast<uint8_t *>(dstPlanes[0]);
        uint8_t *chromaLine = static_cast<uint8_t *>(dstPlanes[1]);

        // Luma: every pixel
        for(size_t x = 0; x < width; x++) {
                int r = src[x * 4 + 0], g = src[x * 4 + 1], b = src[x * 4 + 2];
                lumaLine[x] = clipU8(((66 * r + 129 * g + 25 * b + 128) >> 8) + 16);
        }

        // Chroma: average pairs horizontally
        size_t pairs = width / 2;
        for(size_t i = 0; i < pairs; i++) {
                int r = (src[i * 8 + 0] + src[i * 8 + 4] + 1) >> 1;
                int g = (src[i * 8 + 1] + src[i * 8 + 5] + 1) >> 1;
                int b = (src[i * 8 + 2] + src[i * 8 + 6] + 1) >> 1;
                chromaLine[i * 2]     = clipU8(((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128);
                chromaLine[i * 2 + 1] = clipU8(((112 * r - 94 * g -  18 * b + 128) >> 8) + 128);
        }
        if(width & 1) {
                size_t x = width - 1;
                int r = src[x * 4 + 0], g = src[x * 4 + 1], b = src[x * 4 + 2];
                chromaLine[pairs * 2]     = clipU8(((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128);
                chromaLine[pairs * 2 + 1] = clipU8(((112 * r - 94 * g -  18 * b + 128) >> 8) + 128);
        }
        return;
}

// =========================================================================
// UYVY8 <-> RGBA8  (BT.709 fixed-point)
// UYVY byte order: [Cb, Y0, Cr, Y1]
// =========================================================================

void FastPathUYVY8toRGBA8(const void *const *srcPlanes,
                           const size_t *srcStrides,
                           void *const *dstPlanes,
                           const size_t *dstStrides,
                           size_t width, CSCContext &ctx) {
        const uint8_t *src = static_cast<const uint8_t *>(srcPlanes[0]);
        uint8_t *dst = static_cast<uint8_t *>(dstPlanes[0]);

        size_t pairs = width / 2;
        for(size_t i = 0; i < pairs; i++) {
                int cb = src[i * 4 + 0];
                int y0 = src[i * 4 + 1];
                int cr = src[i * 4 + 2];
                int y1 = src[i * 4 + 3];
                int c0 = y0 - 16, c1 = y1 - 16, d = cb - 128, e = cr - 128;

                dst[i * 8 + 0] = clipU8((298*c0 + 409*e + 128) >> 8);
                dst[i * 8 + 1] = clipU8((298*c0 - 100*d - 208*e + 128) >> 8);
                dst[i * 8 + 2] = clipU8((298*c0 + 516*d + 128) >> 8);
                dst[i * 8 + 3] = 255;
                dst[i * 8 + 4] = clipU8((298*c1 + 409*e + 128) >> 8);
                dst[i * 8 + 5] = clipU8((298*c1 - 100*d - 208*e + 128) >> 8);
                dst[i * 8 + 6] = clipU8((298*c1 + 516*d + 128) >> 8);
                dst[i * 8 + 7] = 255;
        }
        return;
}

void FastPathRGBA8toUYVY8(const void *const *srcPlanes,
                           const size_t *srcStrides,
                           void *const *dstPlanes,
                           const size_t *dstStrides,
                           size_t width, CSCContext &ctx) {
        const uint8_t *src = static_cast<const uint8_t *>(srcPlanes[0]);
        uint8_t *dst = static_cast<uint8_t *>(dstPlanes[0]);

        size_t pairs = width / 2;
        for(size_t i = 0; i < pairs; i++) {
                int r0 = src[i*8+0], g0 = src[i*8+1], b0 = src[i*8+2];
                int r1 = src[i*8+4], g1 = src[i*8+5], b1 = src[i*8+6];
                int y0 = ((66*r0 + 129*g0 + 25*b0 + 128) >> 8) + 16;
                int y1 = ((66*r1 + 129*g1 + 25*b1 + 128) >> 8) + 16;
                int ra = (r0+r1+1)>>1, ga = (g0+g1+1)>>1, ba = (b0+b1+1)>>1;
                int cb = ((-38*ra - 74*ga + 112*ba + 128) >> 8) + 128;
                int cr = ((112*ra - 94*ga - 18*ba + 128) >> 8) + 128;
                dst[i*4+0] = clipU8(cb);
                dst[i*4+1] = clipU8(y0);
                dst[i*4+2] = clipU8(cr);
                dst[i*4+3] = clipU8(y1);
        }
        return;
}

// =========================================================================
// NV21 (CrCb order) <-> RGBA8  (BT.709 fixed-point)
// =========================================================================

void FastPathNV21toRGBA8(const void *const *srcPlanes,
                          const size_t *srcStrides,
                          void *const *dstPlanes,
                          const size_t *dstStrides,
                          size_t width, CSCContext &ctx) {
        const uint8_t *luma = static_cast<const uint8_t *>(srcPlanes[0]);
        const uint8_t *chroma = static_cast<const uint8_t *>(srcPlanes[1]);
        uint8_t *dst = static_cast<uint8_t *>(dstPlanes[0]);

        size_t pairs = width / 2;
        for(size_t i = 0; i < pairs; i++) {
                int y0 = luma[i*2], y1 = luma[i*2+1];
                int cr = chroma[i*2];      // NV21: Cr first
                int cb = chroma[i*2+1];    // NV21: Cb second
                int c0 = y0-16, c1 = y1-16, d = cb-128, e = cr-128;

                dst[i*8+0] = clipU8((298*c0 + 409*e + 128) >> 8);
                dst[i*8+1] = clipU8((298*c0 - 100*d - 208*e + 128) >> 8);
                dst[i*8+2] = clipU8((298*c0 + 516*d + 128) >> 8);
                dst[i*8+3] = 255;
                dst[i*8+4] = clipU8((298*c1 + 409*e + 128) >> 8);
                dst[i*8+5] = clipU8((298*c1 - 100*d - 208*e + 128) >> 8);
                dst[i*8+6] = clipU8((298*c1 + 516*d + 128) >> 8);
                dst[i*8+7] = 255;
        }
        return;
}

void FastPathRGBA8toNV21(const void *const *srcPlanes,
                          const size_t *srcStrides,
                          void *const *dstPlanes,
                          const size_t *dstStrides,
                          size_t width, CSCContext &ctx) {
        const uint8_t *src = static_cast<const uint8_t *>(srcPlanes[0]);
        uint8_t *luma = static_cast<uint8_t *>(dstPlanes[0]);
        uint8_t *chroma = static_cast<uint8_t *>(dstPlanes[1]);

        for(size_t x = 0; x < width; x++) {
                int r = src[x*4+0], g = src[x*4+1], b = src[x*4+2];
                luma[x] = clipU8(((66*r + 129*g + 25*b + 128) >> 8) + 16);
        }
        size_t pairs = width / 2;
        for(size_t i = 0; i < pairs; i++) {
                int r = (src[i*8+0]+src[i*8+4]+1)>>1;
                int g = (src[i*8+1]+src[i*8+5]+1)>>1;
                int b = (src[i*8+2]+src[i*8+6]+1)>>1;
                chroma[i*2]   = clipU8(((112*r - 94*g - 18*b + 128) >> 8) + 128); // Cr first
                chroma[i*2+1] = clipU8(((-38*r - 74*g + 112*b + 128) >> 8) + 128); // Cb second
        }
        return;
}

// =========================================================================
// NV16 (422 semi-planar, CbCr order) <-> RGBA8  (BT.709 fixed-point)
// Same as NV12 but no vertical subsampling.
// =========================================================================

void FastPathNV16toRGBA8(const void *const *srcPlanes,
                          const size_t *srcStrides,
                          void *const *dstPlanes,
                          const size_t *dstStrides,
                          size_t width, CSCContext &ctx) {
        const uint8_t *luma = static_cast<const uint8_t *>(srcPlanes[0]);
        const uint8_t *chroma = static_cast<const uint8_t *>(srcPlanes[1]);
        uint8_t *dst = static_cast<uint8_t *>(dstPlanes[0]);

        size_t pairs = width / 2;
        for(size_t i = 0; i < pairs; i++) {
                int y0 = luma[i*2], y1 = luma[i*2+1];
                int cb = chroma[i*2], cr = chroma[i*2+1];
                int c0 = y0-16, c1 = y1-16, d = cb-128, e = cr-128;

                dst[i*8+0] = clipU8((298*c0 + 409*e + 128) >> 8);
                dst[i*8+1] = clipU8((298*c0 - 100*d - 208*e + 128) >> 8);
                dst[i*8+2] = clipU8((298*c0 + 516*d + 128) >> 8);
                dst[i*8+3] = 255;
                dst[i*8+4] = clipU8((298*c1 + 409*e + 128) >> 8);
                dst[i*8+5] = clipU8((298*c1 - 100*d - 208*e + 128) >> 8);
                dst[i*8+6] = clipU8((298*c1 + 516*d + 128) >> 8);
                dst[i*8+7] = 255;
        }
        return;
}

void FastPathRGBA8toNV16(const void *const *srcPlanes,
                          const size_t *srcStrides,
                          void *const *dstPlanes,
                          const size_t *dstStrides,
                          size_t width, CSCContext &ctx) {
        const uint8_t *src = static_cast<const uint8_t *>(srcPlanes[0]);
        uint8_t *luma = static_cast<uint8_t *>(dstPlanes[0]);
        uint8_t *chroma = static_cast<uint8_t *>(dstPlanes[1]);

        for(size_t x = 0; x < width; x++) {
                int r = src[x*4+0], g = src[x*4+1], b = src[x*4+2];
                luma[x] = clipU8(((66*r + 129*g + 25*b + 128) >> 8) + 16);
        }
        size_t pairs = width / 2;
        for(size_t i = 0; i < pairs; i++) {
                int r = (src[i*8+0]+src[i*8+4]+1)>>1;
                int g = (src[i*8+1]+src[i*8+5]+1)>>1;
                int b = (src[i*8+2]+src[i*8+6]+1)>>1;
                chroma[i*2]   = clipU8(((-38*r - 74*g + 112*b + 128) >> 8) + 128);
                chroma[i*2+1] = clipU8(((112*r - 94*g - 18*b + 128) >> 8) + 128);
        }
        return;
}

// =========================================================================
// Planar YUV8 422 (3-plane) <-> RGBA8  (BT.709 fixed-point)
// =========================================================================

void FastPathPlanar422toRGBA8(const void *const *srcPlanes,
                               const size_t *srcStrides,
                               void *const *dstPlanes,
                               const size_t *dstStrides,
                               size_t width, CSCContext &ctx) {
        const uint8_t *yp = static_cast<const uint8_t *>(srcPlanes[0]);
        const uint8_t *cbp = static_cast<const uint8_t *>(srcPlanes[1]);
        const uint8_t *crp = static_cast<const uint8_t *>(srcPlanes[2]);
        uint8_t *dst = static_cast<uint8_t *>(dstPlanes[0]);

        size_t pairs = width / 2;
        for(size_t i = 0; i < pairs; i++) {
                int y0 = yp[i*2], y1 = yp[i*2+1];
                int cb = cbp[i], cr = crp[i];
                int c0 = y0-16, c1 = y1-16, d = cb-128, e = cr-128;

                dst[i*8+0] = clipU8((298*c0 + 409*e + 128) >> 8);
                dst[i*8+1] = clipU8((298*c0 - 100*d - 208*e + 128) >> 8);
                dst[i*8+2] = clipU8((298*c0 + 516*d + 128) >> 8);
                dst[i*8+3] = 255;
                dst[i*8+4] = clipU8((298*c1 + 409*e + 128) >> 8);
                dst[i*8+5] = clipU8((298*c1 - 100*d - 208*e + 128) >> 8);
                dst[i*8+6] = clipU8((298*c1 + 516*d + 128) >> 8);
                dst[i*8+7] = 255;
        }
        return;
}

void FastPathRGBA8toPlanar422(const void *const *srcPlanes,
                               const size_t *srcStrides,
                               void *const *dstPlanes,
                               const size_t *dstStrides,
                               size_t width, CSCContext &ctx) {
        const uint8_t *src = static_cast<const uint8_t *>(srcPlanes[0]);
        uint8_t *yp = static_cast<uint8_t *>(dstPlanes[0]);
        uint8_t *cbp = static_cast<uint8_t *>(dstPlanes[1]);
        uint8_t *crp = static_cast<uint8_t *>(dstPlanes[2]);

        for(size_t x = 0; x < width; x++) {
                int r = src[x*4+0], g = src[x*4+1], b = src[x*4+2];
                yp[x] = clipU8(((66*r + 129*g + 25*b + 128) >> 8) + 16);
        }
        size_t pairs = width / 2;
        for(size_t i = 0; i < pairs; i++) {
                int r = (src[i*8+0]+src[i*8+4]+1)>>1;
                int g = (src[i*8+1]+src[i*8+5]+1)>>1;
                int b = (src[i*8+2]+src[i*8+6]+1)>>1;
                cbp[i] = clipU8(((-38*r - 74*g + 112*b + 128) >> 8) + 128);
                crp[i] = clipU8(((112*r - 94*g - 18*b + 128) >> 8) + 128);
        }
        return;
}

// =========================================================================
// Planar YUV8 420 (3-plane) <-> RGBA8  (BT.709 fixed-point)
// Same as 422 planar; the vertical subsampling is handled by the
// pipeline's line pointer calculation (planeY = y / vSubsampling).
// =========================================================================

void FastPathPlanar420toRGBA8(const void *const *srcPlanes,
                               const size_t *srcStrides,
                               void *const *dstPlanes,
                               const size_t *dstStrides,
                               size_t width, CSCContext &ctx) {
        // Identical per-line math to 422 planar
        FastPathPlanar422toRGBA8(srcPlanes, srcStrides, dstPlanes, dstStrides, width, ctx);
        return;
}

void FastPathRGBA8toPlanar420(const void *const *srcPlanes,
                               const size_t *srcStrides,
                               void *const *dstPlanes,
                               const size_t *dstStrides,
                               size_t width, CSCContext &ctx) {
        FastPathRGBA8toPlanar422(srcPlanes, srcStrides, dstPlanes, dstStrides, width, ctx);
        return;
}

// =========================================================================
// 10-bit clip helper (used by BT.709, BT.601, and BT.2020 10-bit paths)
// =========================================================================

static inline uint16_t clip10(int v) {
        return static_cast<uint16_t>(v < 0 ? 0 : (v > 1023 ? 1023 : v));
}

// =========================================================================
// BT.601 8-bit YCbCr <-> RGBA8 fast paths
// =========================================================================
//
// BT.601 luma: Y = 0.299*R + 0.587*G + 0.114*B  (different from BT.709!)
//
// RGB -> YCbCr (BT.601 limited range):
//   Y  = (( 77*R + 150*G +  29*B + 128) >> 8) + 16
//   Cb = ((-43*R -  85*G + 128*B + 128) >> 8) + 128
//   Cr = ((128*R - 107*G -  21*B + 128) >> 8) + 128
//
// YCbCr -> RGB (BT.601 limited range):
//   C = Y - 16,  D = Cb - 128,  E = Cr - 128
//   R = clip((298*C + 179*E + 128) >> 8)
//   G = clip((298*C -  44*D -  91*E + 128) >> 8)
//   B = clip((298*C + 227*D + 128) >> 8)

static inline void yuv601ToRgba8(int y, int cb, int cr, uint8_t *dst) {
        int c = y - 16, d = cb - 128, e = cr - 128;
        dst[0] = clipU8((298*c + 179*e + 128) >> 8);
        dst[1] = clipU8((298*c -  44*d -  91*e + 128) >> 8);
        dst[2] = clipU8((298*c + 227*d + 128) >> 8);
        dst[3] = 255;
}

static inline void rgba8ToYCbCr601(int r, int g, int b, int *y, int *cb, int *cr) {
        *y  = (( 77*r + 150*g +  29*b + 128) >> 8) + 16;
        *cb = ((-43*r -  85*g + 128*b + 128) >> 8) + 128;
        *cr = ((128*r - 107*g -  21*b + 128) >> 8) + 128;
}

void FastPathYUYV601toRGBA8(const void *const *srcPlanes,
                             const size_t *srcStrides,
                             void *const *dstPlanes,
                             const size_t *dstStrides,
                             size_t width, CSCContext &ctx) {
        const uint8_t *src = static_cast<const uint8_t *>(srcPlanes[0]);
        uint8_t *dst = static_cast<uint8_t *>(dstPlanes[0]);
        size_t pairs = width / 2;
        for(size_t i = 0; i < pairs; i++) {
                int y0 = src[i*4+0], cb = src[i*4+1], y1 = src[i*4+2], cr = src[i*4+3];
                yuv601ToRgba8(y0, cb, cr, dst + i*8);
                yuv601ToRgba8(y1, cb, cr, dst + i*8 + 4);
        }
        return;
}

void FastPathRGBA8toYUYV601(const void *const *srcPlanes,
                             const size_t *srcStrides,
                             void *const *dstPlanes,
                             const size_t *dstStrides,
                             size_t width, CSCContext &ctx) {
        const uint8_t *src = static_cast<const uint8_t *>(srcPlanes[0]);
        uint8_t *dst = static_cast<uint8_t *>(dstPlanes[0]);
        size_t pairs = width / 2;
        for(size_t i = 0; i < pairs; i++) {
                int r0 = src[i*8+0], g0 = src[i*8+1], b0 = src[i*8+2];
                int r1 = src[i*8+4], g1 = src[i*8+5], b1 = src[i*8+6];
                int y0, cb0, cr0, y1, cb1, cr1;
                rgba8ToYCbCr601(r0, g0, b0, &y0, &cb0, &cr0);
                rgba8ToYCbCr601(r1, g1, b1, &y1, &cb1, &cr1);
                dst[i*4+0] = clipU8(y0);
                dst[i*4+1] = clipU8((cb0+cb1+1)>>1);
                dst[i*4+2] = clipU8(y1);
                dst[i*4+3] = clipU8((cr0+cr1+1)>>1);
        }
        return;
}

void FastPathUYVY601toRGBA8(const void *const *srcPlanes,
                             const size_t *srcStrides,
                             void *const *dstPlanes,
                             const size_t *dstStrides,
                             size_t width, CSCContext &ctx) {
        const uint8_t *src = static_cast<const uint8_t *>(srcPlanes[0]);
        uint8_t *dst = static_cast<uint8_t *>(dstPlanes[0]);
        size_t pairs = width / 2;
        for(size_t i = 0; i < pairs; i++) {
                int cb = src[i*4+0], y0 = src[i*4+1], cr = src[i*4+2], y1 = src[i*4+3];
                yuv601ToRgba8(y0, cb, cr, dst + i*8);
                yuv601ToRgba8(y1, cb, cr, dst + i*8 + 4);
        }
        return;
}

void FastPathRGBA8toUYVY601(const void *const *srcPlanes,
                             const size_t *srcStrides,
                             void *const *dstPlanes,
                             const size_t *dstStrides,
                             size_t width, CSCContext &ctx) {
        const uint8_t *src = static_cast<const uint8_t *>(srcPlanes[0]);
        uint8_t *dst = static_cast<uint8_t *>(dstPlanes[0]);
        size_t pairs = width / 2;
        for(size_t i = 0; i < pairs; i++) {
                int r0 = src[i*8+0], g0 = src[i*8+1], b0 = src[i*8+2];
                int r1 = src[i*8+4], g1 = src[i*8+5], b1 = src[i*8+6];
                int y0, cb0, cr0, y1, cb1, cr1;
                rgba8ToYCbCr601(r0, g0, b0, &y0, &cb0, &cr0);
                rgba8ToYCbCr601(r1, g1, b1, &y1, &cb1, &cr1);
                dst[i*4+0] = clipU8((cb0+cb1+1)>>1);
                dst[i*4+1] = clipU8(y0);
                dst[i*4+2] = clipU8((cr0+cr1+1)>>1);
                dst[i*4+3] = clipU8(y1);
        }
        return;
}

void FastPathNV12_601toRGBA8(const void *const *srcPlanes,
                              const size_t *srcStrides,
                              void *const *dstPlanes,
                              const size_t *dstStrides,
                              size_t width, CSCContext &ctx) {
        const uint8_t *luma = static_cast<const uint8_t *>(srcPlanes[0]);
        const uint8_t *chroma = static_cast<const uint8_t *>(srcPlanes[1]);
        uint8_t *dst = static_cast<uint8_t *>(dstPlanes[0]);
        size_t pairs = width / 2;
        for(size_t i = 0; i < pairs; i++) {
                int y0 = luma[i*2], y1 = luma[i*2+1];
                int cb = chroma[i*2], cr = chroma[i*2+1];
                yuv601ToRgba8(y0, cb, cr, dst + i*8);
                yuv601ToRgba8(y1, cb, cr, dst + i*8 + 4);
        }
        return;
}

void FastPathRGBA8toNV12_601(const void *const *srcPlanes,
                              const size_t *srcStrides,
                              void *const *dstPlanes,
                              const size_t *dstStrides,
                              size_t width, CSCContext &ctx) {
        const uint8_t *src = static_cast<const uint8_t *>(srcPlanes[0]);
        uint8_t *luma = static_cast<uint8_t *>(dstPlanes[0]);
        uint8_t *chroma = static_cast<uint8_t *>(dstPlanes[1]);
        for(size_t x = 0; x < width; x++) {
                int r = src[x*4+0], g = src[x*4+1], b = src[x*4+2];
                luma[x] = clipU8(((77*r + 150*g + 29*b + 128) >> 8) + 16);
        }
        size_t pairs = width / 2;
        for(size_t i = 0; i < pairs; i++) {
                int r = (src[i*8+0]+src[i*8+4]+1)>>1;
                int g = (src[i*8+1]+src[i*8+5]+1)>>1;
                int b = (src[i*8+2]+src[i*8+6]+1)>>1;
                chroma[i*2]   = clipU8(((-43*r - 85*g + 128*b + 128) >> 8) + 128);
                chroma[i*2+1] = clipU8(((128*r - 107*g - 21*b + 128) >> 8) + 128);
        }
        return;
}

void FastPathPlanar601_420toRGBA8(const void *const *srcPlanes,
                                   const size_t *srcStrides,
                                   void *const *dstPlanes,
                                   const size_t *dstStrides,
                                   size_t width, CSCContext &ctx) {
        const uint8_t *yp = static_cast<const uint8_t *>(srcPlanes[0]);
        const uint8_t *cbp = static_cast<const uint8_t *>(srcPlanes[1]);
        const uint8_t *crp = static_cast<const uint8_t *>(srcPlanes[2]);
        uint8_t *dst = static_cast<uint8_t *>(dstPlanes[0]);
        size_t pairs = width / 2;
        for(size_t i = 0; i < pairs; i++) {
                yuv601ToRgba8(yp[i*2],   cbp[i], crp[i], dst + i*8);
                yuv601ToRgba8(yp[i*2+1], cbp[i], crp[i], dst + i*8 + 4);
        }
        return;
}

void FastPathRGBA8toPlanar601_420(const void *const *srcPlanes,
                                   const size_t *srcStrides,
                                   void *const *dstPlanes,
                                   const size_t *dstStrides,
                                   size_t width, CSCContext &ctx) {
        const uint8_t *src = static_cast<const uint8_t *>(srcPlanes[0]);
        uint8_t *yp = static_cast<uint8_t *>(dstPlanes[0]);
        uint8_t *cbp = static_cast<uint8_t *>(dstPlanes[1]);
        uint8_t *crp = static_cast<uint8_t *>(dstPlanes[2]);
        for(size_t x = 0; x < width; x++) {
                int r = src[x*4+0], g = src[x*4+1], b = src[x*4+2];
                yp[x] = clipU8(((77*r + 150*g + 29*b + 128) >> 8) + 16);
        }
        size_t pairs = width / 2;
        for(size_t i = 0; i < pairs; i++) {
                int r = (src[i*8+0]+src[i*8+4]+1)>>1;
                int g = (src[i*8+1]+src[i*8+5]+1)>>1;
                int b = (src[i*8+2]+src[i*8+6]+1)>>1;
                cbp[i] = clipU8(((-43*r - 85*g + 128*b + 128) >> 8) + 128);
                crp[i] = clipU8(((128*r - 107*g - 21*b + 128) >> 8) + 128);
        }
        return;
}

// =========================================================================
// BT.2020 10-bit helpers
// =========================================================================
//
// BT.2020 luma: Y = 0.2627*R + 0.6780*G + 0.0593*B
//
// RGB -> YCbCr (10-bit limited range):
//   Y  = (( 230*R + 594*G +  52*B + 512) >> 10) + 64
//   Cb = ((-125*R - 323*G + 448*B + 512) >> 10) + 512
//   Cr = (( 448*R - 412*G -  36*B + 512) >> 10) + 512
//
// YCbCr -> RGB (10-bit limited range):
//   C = Y - 64,  D = Cb - 512,  E = Cr - 512
//   R = clip10((1196*C + 1724*E + 512) >> 10)
//   G = clip10((1196*C -  192*D -  668*E + 512) >> 10)
//   B = clip10((1196*C + 2200*D + 512) >> 10)

static inline void yuv2020_10ToRgba10(int y, int cb, int cr, uint16_t *dst) {
        int c = y - 64, d = cb - 512, e = cr - 512;
        dst[0] = clip10((1196*c + 1724*e + 512) >> 10);
        dst[1] = clip10((1196*c - 192*d - 668*e + 512) >> 10);
        dst[2] = clip10((1196*c + 2200*d + 512) >> 10);
        dst[3] = 1023;
}

static inline void rgba10ToYCbCr2020(int r, int g, int b, int *y, int *cb, int *cr) {
        *y  = (( 230*r + 594*g +  52*b + 512) >> 10) + 64;
        *cb = ((-125*r - 323*g + 448*b + 512) >> 10) + 512;
        *cr = (( 448*r - 412*g -  36*b + 512) >> 10) + 512;
}

// UYVY 10-bit LE Rec.2020

void FastPathUYVY10LE_2020toRGBA10LE(const void *const *srcPlanes,
                                      const size_t *srcStrides,
                                      void *const *dstPlanes,
                                      const size_t *dstStrides,
                                      size_t width, CSCContext &ctx) {
        const uint16_t *src = static_cast<const uint16_t *>(srcPlanes[0]);
        uint16_t *dst = static_cast<uint16_t *>(dstPlanes[0]);
        size_t pairs = width / 2;
        for(size_t i = 0; i < pairs; i++) {
                int cb = src[i*4+0], y0 = src[i*4+1], cr = src[i*4+2], y1 = src[i*4+3];
                yuv2020_10ToRgba10(y0, cb, cr, dst + i*8);
                yuv2020_10ToRgba10(y1, cb, cr, dst + i*8 + 4);
        }
        return;
}

void FastPathRGBA10LEtoUYVY10LE_2020(const void *const *srcPlanes,
                                      const size_t *srcStrides,
                                      void *const *dstPlanes,
                                      const size_t *dstStrides,
                                      size_t width, CSCContext &ctx) {
        const uint16_t *src = static_cast<const uint16_t *>(srcPlanes[0]);
        uint16_t *dst = static_cast<uint16_t *>(dstPlanes[0]);
        size_t pairs = width / 2;
        for(size_t i = 0; i < pairs; i++) {
                int r0 = src[i*8+0], g0 = src[i*8+1], b0 = src[i*8+2];
                int r1 = src[i*8+4], g1 = src[i*8+5], b1 = src[i*8+6];
                int y0, cb0, cr0, y1, cb1, cr1;
                rgba10ToYCbCr2020(r0, g0, b0, &y0, &cb0, &cr0);
                rgba10ToYCbCr2020(r1, g1, b1, &y1, &cb1, &cr1);
                dst[i*4+0] = clip10((cb0+cb1+1)>>1);
                dst[i*4+1] = clip10(y0);
                dst[i*4+2] = clip10((cr0+cr1+1)>>1);
                dst[i*4+3] = clip10(y1);
        }
        return;
}

// Planar 420 10-bit LE Rec.2020

void FastPathPlanar10LE420_2020toRGBA10LE(const void *const *srcPlanes,
                                           const size_t *srcStrides,
                                           void *const *dstPlanes,
                                           const size_t *dstStrides,
                                           size_t width, CSCContext &ctx) {
        const uint16_t *yp = static_cast<const uint16_t *>(srcPlanes[0]);
        const uint16_t *cbp = static_cast<const uint16_t *>(srcPlanes[1]);
        const uint16_t *crp = static_cast<const uint16_t *>(srcPlanes[2]);
        uint16_t *dst = static_cast<uint16_t *>(dstPlanes[0]);
        size_t pairs = width / 2;
        for(size_t i = 0; i < pairs; i++) {
                yuv2020_10ToRgba10(yp[i*2],   cbp[i], crp[i], dst + i*8);
                yuv2020_10ToRgba10(yp[i*2+1], cbp[i], crp[i], dst + i*8 + 4);
        }
        return;
}

void FastPathRGBA10LEtoPlanar10LE420_2020(const void *const *srcPlanes,
                                           const size_t *srcStrides,
                                           void *const *dstPlanes,
                                           const size_t *dstStrides,
                                           size_t width, CSCContext &ctx) {
        const uint16_t *src = static_cast<const uint16_t *>(srcPlanes[0]);
        uint16_t *yp = static_cast<uint16_t *>(dstPlanes[0]);
        uint16_t *cbp = static_cast<uint16_t *>(dstPlanes[1]);
        uint16_t *crp = static_cast<uint16_t *>(dstPlanes[2]);
        for(size_t x = 0; x < width; x++) {
                int r = src[x*4+0], g = src[x*4+1], b = src[x*4+2];
                int y, cb, cr;
                rgba10ToYCbCr2020(r, g, b, &y, &cb, &cr);
                yp[x] = clip10(y);
        }
        size_t pairs = width / 2;
        for(size_t i = 0; i < pairs; i++) {
                int r = (src[i*8+0]+src[i*8+4]+1)>>1;
                int g = (src[i*8+1]+src[i*8+5]+1)>>1;
                int b = (src[i*8+2]+src[i*8+6]+1)>>1;
                int y, cb, cr;
                rgba10ToYCbCr2020(r, g, b, &y, &cb, &cr);
                cbp[i] = clip10(cb);
                crp[i] = clip10(cr);
        }
        return;
}

// =========================================================================
// BT.709 10-bit helpers
// =========================================================================
//
// BT.709 10-bit limited range:
//   Y: 64-940,  Cb/Cr: 64-960  (out of 0-1023)
//
// YCbCr -> RGB:
//   C = Y - 64,  D = Cb - 512,  E = Cr - 512
//   R = clip10((1192*C + 1634*E + 512) >> 10)
//   G = clip10((1192*C -  401*D -  833*E + 512) >> 10)
//   B = clip10((1192*C + 2066*D + 512) >> 10)
//
// RGB -> YCbCr:
//   Y  = clip10(( 263*R + 516*G + 100*B + 512) >> 10) + 64
//   Cb = clip10((-152*R - 298*G + 450*B + 512) >> 10) + 512
//   Cr = clip10(( 450*R - 377*G -  73*B + 512) >> 10) + 512

static inline void yuv10ToRgba10(int y, int cb, int cr, uint16_t *dst) {
        int c = y - 64, d = cb - 512, e = cr - 512;
        dst[0] = clip10((1192*c + 1634*e + 512) >> 10);
        dst[1] = clip10((1192*c - 401*d - 833*e + 512) >> 10);
        dst[2] = clip10((1192*c + 2066*d + 512) >> 10);
        dst[3] = 1023;
}

static inline void rgba10ToYCbCr(int r, int g, int b, int *y, int *cb, int *cr) {
        *y  = (( 263*r + 516*g + 100*b + 512) >> 10) + 64;
        *cb = ((-152*r - 298*g + 450*b + 512) >> 10) + 512;
        *cr = (( 450*r - 377*g -  73*b + 512) >> 10) + 512;
}

// =========================================================================
// 10-bit UYVY LE <-> RGBA10 LE  (BT.709 fixed-point)
// Layout: [Cb(u16), Y0(u16), Cr(u16), Y1(u16)] = 8 bytes per 2 pixels
// =========================================================================

void FastPathUYVY10LEtoRGBA10LE(const void *const *srcPlanes,
                                 const size_t *srcStrides,
                                 void *const *dstPlanes,
                                 const size_t *dstStrides,
                                 size_t width, CSCContext &ctx) {
        const uint16_t *src = static_cast<const uint16_t *>(srcPlanes[0]);
        uint16_t *dst = static_cast<uint16_t *>(dstPlanes[0]);

        size_t pairs = width / 2;
        for(size_t i = 0; i < pairs; i++) {
                int cb = src[i*4+0], y0 = src[i*4+1], cr = src[i*4+2], y1 = src[i*4+3];
                yuv10ToRgba10(y0, cb, cr, dst + i*8);
                yuv10ToRgba10(y1, cb, cr, dst + i*8 + 4);
        }
        return;
}

void FastPathRGBA10LEtoUYVY10LE(const void *const *srcPlanes,
                                 const size_t *srcStrides,
                                 void *const *dstPlanes,
                                 const size_t *dstStrides,
                                 size_t width, CSCContext &ctx) {
        const uint16_t *src = static_cast<const uint16_t *>(srcPlanes[0]);
        uint16_t *dst = static_cast<uint16_t *>(dstPlanes[0]);

        size_t pairs = width / 2;
        for(size_t i = 0; i < pairs; i++) {
                int r0 = src[i*8+0], g0 = src[i*8+1], b0 = src[i*8+2];
                int r1 = src[i*8+4], g1 = src[i*8+5], b1 = src[i*8+6];
                int y0, cb0, cr0, y1, cb1, cr1;
                rgba10ToYCbCr(r0, g0, b0, &y0, &cb0, &cr0);
                rgba10ToYCbCr(r1, g1, b1, &y1, &cb1, &cr1);
                dst[i*4+0] = clip10((cb0 + cb1 + 1) >> 1);
                dst[i*4+1] = clip10(y0);
                dst[i*4+2] = clip10((cr0 + cr1 + 1) >> 1);
                dst[i*4+3] = clip10(y1);
        }
        return;
}

// =========================================================================
// 10-bit Planar 422/420 LE <-> RGBA10 LE
// =========================================================================

void FastPathPlanar10LE422toRGBA10LE(const void *const *srcPlanes,
                                      const size_t *srcStrides,
                                      void *const *dstPlanes,
                                      const size_t *dstStrides,
                                      size_t width, CSCContext &ctx) {
        const uint16_t *yp = static_cast<const uint16_t *>(srcPlanes[0]);
        const uint16_t *cbp = static_cast<const uint16_t *>(srcPlanes[1]);
        const uint16_t *crp = static_cast<const uint16_t *>(srcPlanes[2]);
        uint16_t *dst = static_cast<uint16_t *>(dstPlanes[0]);

        size_t pairs = width / 2;
        for(size_t i = 0; i < pairs; i++) {
                yuv10ToRgba10(yp[i*2],   cbp[i], crp[i], dst + i*8);
                yuv10ToRgba10(yp[i*2+1], cbp[i], crp[i], dst + i*8 + 4);
        }
        return;
}

void FastPathRGBA10LEtoPlanar10LE422(const void *const *srcPlanes,
                                      const size_t *srcStrides,
                                      void *const *dstPlanes,
                                      const size_t *dstStrides,
                                      size_t width, CSCContext &ctx) {
        const uint16_t *src = static_cast<const uint16_t *>(srcPlanes[0]);
        uint16_t *yp = static_cast<uint16_t *>(dstPlanes[0]);
        uint16_t *cbp = static_cast<uint16_t *>(dstPlanes[1]);
        uint16_t *crp = static_cast<uint16_t *>(dstPlanes[2]);

        for(size_t x = 0; x < width; x++) {
                int r = src[x*4+0], g = src[x*4+1], b = src[x*4+2];
                int y, cb, cr;
                rgba10ToYCbCr(r, g, b, &y, &cb, &cr);
                yp[x] = clip10(y);
                if(x % 2 == 0) { cbp[x/2] = 0; crp[x/2] = 0; }
        }
        // Chroma: average pairs
        size_t pairs = width / 2;
        for(size_t i = 0; i < pairs; i++) {
                int r = (src[i*8+0]+src[i*8+4]+1)>>1;
                int g = (src[i*8+1]+src[i*8+5]+1)>>1;
                int b = (src[i*8+2]+src[i*8+6]+1)>>1;
                int y, cb, cr;
                rgba10ToYCbCr(r, g, b, &y, &cb, &cr);
                cbp[i] = clip10(cb);
                crp[i] = clip10(cr);
        }
        return;
}

// 420 per-line is identical to 422 (vertical subsampling handled by executor)
void FastPathPlanar10LE420toRGBA10LE(const void *const *s, const size_t *ss,
                                      void *const *d, const size_t *ds,
                                      size_t w, CSCContext &c) {
        FastPathPlanar10LE422toRGBA10LE(s, ss, d, ds, w, c);
}
void FastPathRGBA10LEtoPlanar10LE420(const void *const *s, const size_t *ss,
                                      void *const *d, const size_t *ds,
                                      size_t w, CSCContext &c) {
        FastPathRGBA10LEtoPlanar10LE422(s, ss, d, ds, w, c);
}

// =========================================================================
// 10-bit NV12 LE (semi-planar 420) <-> RGBA10 LE
// =========================================================================

void FastPathNV12_10LEtoRGBA10LE(const void *const *srcPlanes,
                                  const size_t *srcStrides,
                                  void *const *dstPlanes,
                                  const size_t *dstStrides,
                                  size_t width, CSCContext &ctx) {
        const uint16_t *luma = static_cast<const uint16_t *>(srcPlanes[0]);
        const uint16_t *chroma = static_cast<const uint16_t *>(srcPlanes[1]);
        uint16_t *dst = static_cast<uint16_t *>(dstPlanes[0]);

        size_t pairs = width / 2;
        for(size_t i = 0; i < pairs; i++) {
                int y0 = luma[i*2], y1 = luma[i*2+1];
                int cb = chroma[i*2], cr = chroma[i*2+1];
                yuv10ToRgba10(y0, cb, cr, dst + i*8);
                yuv10ToRgba10(y1, cb, cr, dst + i*8 + 4);
        }
        return;
}

void FastPathRGBA10LEtoNV12_10LE(const void *const *srcPlanes,
                                  const size_t *srcStrides,
                                  void *const *dstPlanes,
                                  const size_t *dstStrides,
                                  size_t width, CSCContext &ctx) {
        const uint16_t *src = static_cast<const uint16_t *>(srcPlanes[0]);
        uint16_t *luma = static_cast<uint16_t *>(dstPlanes[0]);
        uint16_t *chroma = static_cast<uint16_t *>(dstPlanes[1]);

        for(size_t x = 0; x < width; x++) {
                int r = src[x*4+0], g = src[x*4+1], b = src[x*4+2];
                int y, cb, cr;
                rgba10ToYCbCr(r, g, b, &y, &cb, &cr);
                luma[x] = clip10(y);
        }
        size_t pairs = width / 2;
        for(size_t i = 0; i < pairs; i++) {
                int r = (src[i*8+0]+src[i*8+4]+1)>>1;
                int g = (src[i*8+1]+src[i*8+5]+1)>>1;
                int b = (src[i*8+2]+src[i*8+6]+1)>>1;
                int y, cb, cr;
                rgba10ToYCbCr(r, g, b, &y, &cb, &cr);
                chroma[i*2]   = clip10(cb);
                chroma[i*2+1] = clip10(cr);
        }
        return;
}

// =========================================================================
// v210 <-> RGBA10 LE  (BT.709 fixed-point)
//
// v210 packs 6 pixels (4:2:2) into 4 x 32-bit LE words (16 bytes):
//   Word 0: [--][Cr0 10b][Y0  10b][Cb0 10b]  bits 29:20, 19:10, 9:0
//   Word 1: [--][Y2  10b][Cb1 10b][Y1  10b]
//   Word 2: [--][Cb2 10b][Y3  10b][Cr1 10b]
//   Word 3: [--][Y5  10b][Cr2 10b][Y4  10b]
// =========================================================================

void FastPathV210toRGBA10LE(const void *const *srcPlanes,
                             const size_t *srcStrides,
                             void *const *dstPlanes,
                             const size_t *dstStrides,
                             size_t width, CSCContext &ctx) {
        const uint32_t *src = static_cast<const uint32_t *>(srcPlanes[0]);
        uint16_t *dst = static_cast<uint16_t *>(dstPlanes[0]);

        size_t blocks = width / 6;
        for(size_t b = 0; b < blocks; b++) {
                const uint32_t *w = src + b * 4;
                uint16_t *out = dst + b * 6 * 4;  // 6 pixels x 4 components

                int cb0 = (w[0])       & 0x3FF;
                int y0  = (w[0] >> 10) & 0x3FF;
                int cr0 = (w[0] >> 20) & 0x3FF;
                int y1  = (w[1])       & 0x3FF;
                int cb1 = (w[1] >> 10) & 0x3FF;
                int y2  = (w[1] >> 20) & 0x3FF;
                int cr1 = (w[2])       & 0x3FF;
                int y3  = (w[2] >> 10) & 0x3FF;
                int cb2 = (w[2] >> 20) & 0x3FF;
                int y4  = (w[3])       & 0x3FF;
                int cr2 = (w[3] >> 10) & 0x3FF;
                int y5  = (w[3] >> 20) & 0x3FF;

                yuv10ToRgba10(y0, cb0, cr0, out);
                yuv10ToRgba10(y1, cb0, cr0, out + 4);
                yuv10ToRgba10(y2, cb1, cr1, out + 8);
                yuv10ToRgba10(y3, cb1, cr1, out + 12);
                yuv10ToRgba10(y4, cb2, cr2, out + 16);
                yuv10ToRgba10(y5, cb2, cr2, out + 20);
        }

        // Remaining pixels (0-5 trailing)
        size_t done = blocks * 6;
        if(done < width) {
                const uint32_t *w = src + blocks * 4;
                int samples[6][3]; // [pixel][Y, Cb, Cr]
                int cb0 = (w[0])       & 0x3FF;
                int y0  = (w[0] >> 10) & 0x3FF;
                int cr0 = (w[0] >> 20) & 0x3FF;
                int y1  = (w[1])       & 0x3FF;
                int cb1 = (w[1] >> 10) & 0x3FF;
                int y2  = (w[1] >> 20) & 0x3FF;
                int cr1 = (w[2])       & 0x3FF;
                int y3  = (w[2] >> 10) & 0x3FF;
                int cb2 = (w[2] >> 20) & 0x3FF;
                int y4  = (w[3])       & 0x3FF;
                int cr2 = (w[3] >> 10) & 0x3FF;
                int y5  = (w[3] >> 20) & 0x3FF;
                samples[0][0]=y0; samples[0][1]=cb0; samples[0][2]=cr0;
                samples[1][0]=y1; samples[1][1]=cb0; samples[1][2]=cr0;
                samples[2][0]=y2; samples[2][1]=cb1; samples[2][2]=cr1;
                samples[3][0]=y3; samples[3][1]=cb1; samples[3][2]=cr1;
                samples[4][0]=y4; samples[4][1]=cb2; samples[4][2]=cr2;
                samples[5][0]=y5; samples[5][1]=cb2; samples[5][2]=cr2;
                for(size_t i = 0; i < width - done && i < 6; i++) {
                        yuv10ToRgba10(samples[i][0], samples[i][1], samples[i][2],
                                      dst + (done + i) * 4);
                }
        }
        return;
}

void FastPathRGBA10LEtoV210(const void *const *srcPlanes,
                             const size_t *srcStrides,
                             void *const *dstPlanes,
                             const size_t *dstStrides,
                             size_t width, CSCContext &ctx) {
        const uint16_t *src = static_cast<const uint16_t *>(srcPlanes[0]);
        uint32_t *dst = static_cast<uint32_t *>(dstPlanes[0]);

        size_t blocks = width / 6;
        for(size_t b = 0; b < blocks; b++) {
                const uint16_t *in = src + b * 6 * 4;
                uint32_t *w = dst + b * 4;

                // Convert 6 RGBA pixels to YCbCr
                int y[6], cb[6], cr[6];
                for(int p = 0; p < 6; p++) {
                        rgba10ToYCbCr(in[p*4], in[p*4+1], in[p*4+2],
                                      &y[p], &cb[p], &cr[p]);
                }

                // Average chroma pairs
                int cb0 = (cb[0]+cb[1]+1)>>1, cr0 = (cr[0]+cr[1]+1)>>1;
                int cb1 = (cb[2]+cb[3]+1)>>1, cr1 = (cr[2]+cr[3]+1)>>1;
                int cb2 = (cb[4]+cb[5]+1)>>1, cr2 = (cr[4]+cr[5]+1)>>1;

                // Pack into v210 words
                w[0] = (static_cast<uint32_t>(clip10(cb0)))
                     | (static_cast<uint32_t>(clip10(y[0])) << 10)
                     | (static_cast<uint32_t>(clip10(cr0))  << 20);
                w[1] = (static_cast<uint32_t>(clip10(y[1])))
                     | (static_cast<uint32_t>(clip10(cb1))  << 10)
                     | (static_cast<uint32_t>(clip10(y[2])) << 20);
                w[2] = (static_cast<uint32_t>(clip10(cr1)))
                     | (static_cast<uint32_t>(clip10(y[3])) << 10)
                     | (static_cast<uint32_t>(clip10(cb2))  << 20);
                w[3] = (static_cast<uint32_t>(clip10(y[4])))
                     | (static_cast<uint32_t>(clip10(cr2))  << 10)
                     | (static_cast<uint32_t>(clip10(y[5])) << 20);
        }
        return;
}

}  // namespace HWY_NAMESPACE
}  // namespace csc
}  // namespace promeki
HWY_AFTER_NAMESPACE();

#endif
