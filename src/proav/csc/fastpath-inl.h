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
// BT.709 8-bit YCbCr <-> RGBA8 fast paths (limited range)
// =========================================================================
//
// BT.709 limited-range (Kr=0.2126, Kg=0.7152, Kb=0.0722), 8-bit fixed point:
//
// RGB -> YCbCr:
//   Y  = (( 47*R + 157*G +  16*B + 128) >> 8) + 16
//   Cb = ((-26*R -  87*G + 112*B + 128) >> 8) + 128
//   Cr = ((112*R - 102*G -  10*B + 128) >> 8) + 128
//
// YCbCr -> RGB:
//   C = Y - 16,  D = Cb - 128,  E = Cr - 128
//   R = clip((298*C + 459*E + 128) >> 8)
//   G = clip((298*C -  55*D - 136*E + 128) >> 8)
//   B = clip((298*C + 541*D + 128) >> 8)
//
// An earlier revision of these kernels accidentally used the BT.601
// coefficients (66/129/25 for Y, 409/100/208/516 for the decode
// matrix, etc.).  Because both the encode and decode sides used the
// same (wrong) matrix, round-trip YUV<->RGB conversions through
// libpromeki looked correct — but YUV data produced through the
// "Rec.709" kernels was actually BT.601-encoded, so anything that
// decoded the data with a real BT.709 matrix (e.g. SDL's GPU shader
// when the texture is tagged SDL_COLORSPACE_BT709_LIMITED) got a
// subtle hue shift on saturated primaries.

static inline uint8_t clipU8(int v) {
        return static_cast<uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v));
}

static inline void yuv709ToRgba8(int y, int cb, int cr, uint8_t *dst) {
        int c = y - 16, d = cb - 128, e = cr - 128;
        dst[0] = clipU8((298*c + 459*e + 128) >> 8);
        dst[1] = clipU8((298*c -  55*d - 136*e + 128) >> 8);
        dst[2] = clipU8((298*c + 541*d + 128) >> 8);
        dst[3] = 255;
}

static inline void rgba8ToYCbCr709(int r, int g, int b, int *y, int *cb, int *cr) {
        *y  = (( 47*r + 157*g +  16*b + 128) >> 8) + 16;
        *cb = ((-26*r -  87*g + 112*b + 128) >> 8) + 128;
        *cr = ((112*r - 102*g -  10*b + 128) >> 8) + 128;
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
                yuv709ToRgba8(y0, cb, cr, dst + i * 8);
                yuv709ToRgba8(y1, cb, cr, dst + i * 8 + 4);
        }
        // Handle odd trailing pixel
        if(width & 1) {
                size_t i = pairs;
                int y0 = src[i * 4 + 0];
                int cb = src[i * 4 + 1];
                int cr = src[i * 4 + 3];
                yuv709ToRgba8(y0, cb, cr, dst + i * 8);
        }
        return;
}

// =========================================================================
// RGBA8_sRGB -> YUV8_422 (YUYV) Rec.709  (BT.709 fixed-point)
// =========================================================================

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
                int y0, cb0, cr0, y1, cb1, cr1;
                rgba8ToYCbCr709(r0, g0, b0, &y0, &cb0, &cr0);
                rgba8ToYCbCr709(r1, g1, b1, &y1, &cb1, &cr1);

                dst[i * 4 + 0] = clipU8(y0);
                dst[i * 4 + 1] = clipU8((cb0 + cb1 + 1) >> 1);
                dst[i * 4 + 2] = clipU8(y1);
                dst[i * 4 + 3] = clipU8((cr0 + cr1 + 1) >> 1);
        }
        if(width & 1) {
                size_t i = pairs;
                int r = src[i * 8 + 0], g = src[i * 8 + 1], b = src[i * 8 + 2];
                int y, cb, cr;
                rgba8ToYCbCr709(r, g, b, &y, &cb, &cr);
                dst[i * 4 + 0] = clipU8(y);
                dst[i * 4 + 1] = clipU8(cb);
                dst[i * 4 + 2] = dst[i * 4 + 0]; // duplicate Y
                dst[i * 4 + 3] = clipU8(cr);
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

        // SIMD decode using reformulated BT.709 coefficients so every
        // intermediate fits in i16:
        //   c = y - 16, d = cb - 128, e = cr - 128
        //   R = c + e + ((42*c + 203*e + 128) >> 8)
        //   G = c + ((42*c - 55*d - 136*e + 128) >> 8)
        //   B = c + 2*d + ((42*c + 29*d + 128) >> 8)
        // (Original coefficients 298/459/541 split as 256+42, 256+203, 512+29.)
        //
        // One iteration produces 2*Nq output pixels from Nq chroma pairs.
        const hn::Rebind<uint8_t,  hn::ScalableTag<float>>    du8q;     // Nq u8
        const hn::Rebind<uint16_t, hn::ScalableTag<float>>    du16q;    // Nq u16
        const hn::Rebind<int16_t,  hn::ScalableTag<float>>    di16q;    // Nq i16
        const hn::Rebind<uint8_t,  hn::ScalableTag<uint16_t>> du8h;     // 2*Nq u8
        const size_t Nq = hn::Lanes(du8q);

        const auto kC16   = hn::Set(di16q, int16_t(16));
        const auto kC128  = hn::Set(di16q, int16_t(128));
        const auto kBias  = hn::Set(di16q, int16_t(128));
        const auto k42    = hn::Set(di16q, int16_t(42));
        const auto k203   = hn::Set(di16q, int16_t(203));
        const auto km55   = hn::Set(di16q, int16_t(-55));
        const auto km136  = hn::Set(di16q, int16_t(-136));
        const auto k29    = hn::Set(di16q, int16_t(29));
        const auto kAlpha = hn::Set(du8h, uint8_t(255));

        size_t x = 0;
        for(; x + 2 * Nq <= width; x += 2 * Nq) {
                hn::Vec<decltype(du8q)> yE8, yO8, cb8, cr8;
                hn::LoadInterleaved2(du8q, lumaLine + x,  yE8, yO8);
                hn::LoadInterleaved2(du8q, chromaLine + x, cb8, cr8);

                auto yE = hn::BitCast(di16q, hn::PromoteTo(du16q, yE8));
                auto yO = hn::BitCast(di16q, hn::PromoteTo(du16q, yO8));
                auto cb = hn::BitCast(di16q, hn::PromoteTo(du16q, cb8));
                auto cr = hn::BitCast(di16q, hn::PromoteTo(du16q, cr8));
                auto cE = hn::Sub(yE, kC16);
                auto cO = hn::Sub(yO, kC16);
                auto d  = hn::Sub(cb, kC128);
                auto e  = hn::Sub(cr, kC128);

                auto k42e_common = hn::MulAdd(e, k203, kBias);
                auto rE = hn::Add(hn::Add(cE, e), hn::ShiftRight<8>(hn::MulAdd(cE, k42, k42e_common)));
                auto rO = hn::Add(hn::Add(cO, e), hn::ShiftRight<8>(hn::MulAdd(cO, k42, k42e_common)));

                auto gde_common = hn::MulAdd(d, km55, hn::MulAdd(e, km136, kBias));
                auto gE = hn::Add(cE, hn::ShiftRight<8>(hn::MulAdd(cE, k42, gde_common)));
                auto gO = hn::Add(cO, hn::ShiftRight<8>(hn::MulAdd(cO, k42, gde_common)));

                auto k29d_common = hn::MulAdd(d, k29, kBias);
                auto twoD = hn::Add(d, d);
                auto bE = hn::Add(hn::Add(cE, twoD), hn::ShiftRight<8>(hn::MulAdd(cE, k42, k29d_common)));
                auto bO = hn::Add(hn::Add(cO, twoD), hn::ShiftRight<8>(hn::MulAdd(cO, k42, k29d_common)));

                // Interleave even/odd pixel results and pack back to u8.
                // Use the Whole variants so lanes from both 128-bit blocks
                // of the source vectors are merged linearly on AVX-256/512
                // (plain InterleaveLower/Upper operates per-block and would
                // swap blocks in the combined output).
                auto rLo = hn::InterleaveWholeLower(di16q, rE, rO);
                auto rHi = hn::InterleaveWholeUpper(di16q, rE, rO);
                auto gLo = hn::InterleaveWholeLower(di16q, gE, gO);
                auto gHi = hn::InterleaveWholeUpper(di16q, gE, gO);
                auto bLo = hn::InterleaveWholeLower(di16q, bE, bO);
                auto bHi = hn::InterleaveWholeUpper(di16q, bE, bO);

                auto r = hn::Combine(du8h, hn::DemoteTo(du8q, rHi), hn::DemoteTo(du8q, rLo));
                auto g = hn::Combine(du8h, hn::DemoteTo(du8q, gHi), hn::DemoteTo(du8q, gLo));
                auto b = hn::Combine(du8h, hn::DemoteTo(du8q, bHi), hn::DemoteTo(du8q, bLo));

                hn::StoreInterleaved4(r, g, b, kAlpha, du8h, dst + x * 4);
        }

        // Scalar tail: whole pairs first
        size_t pairs = width / 2;
        for(size_t i = x / 2; i < pairs; i++) {
                int y0 = lumaLine[i * 2];
                int y1 = lumaLine[i * 2 + 1];
                int cb = chromaLine[i * 2];
                int cr = chromaLine[i * 2 + 1];
                yuv709ToRgba8(y0, cb, cr, dst + i * 8);
                yuv709ToRgba8(y1, cb, cr, dst + i * 8 + 4);
        }
        if(width & 1) {
                int y0 = lumaLine[width - 1];
                int cb = chromaLine[pairs * 2];
                int cr = chromaLine[pairs * 2 + 1];
                yuv709ToRgba8(y0, cb, cr, dst + (width - 1) * 4);
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

        // Luma (SIMD): y = ((47*r + 157*g + 16*b + 128) >> 8) + 16, u16-safe.
        // LoadInterleaved4 uses the quarter-width u8 tag (Nq lanes).
        const hn::Rebind<uint8_t,  hn::ScalableTag<float>>    du8q;
        const hn::Rebind<uint16_t, hn::ScalableTag<float>>    du16q;
        const hn::Rebind<int16_t,  hn::ScalableTag<float>>    di16q;
        const hn::Rebind<uint8_t,  hn::ScalableTag<uint16_t>> du8h;
        const size_t Nq = hn::Lanes(du8q);
        const auto c47   = hn::Set(du16q, uint16_t(47));
        const auto c157  = hn::Set(du16q, uint16_t(157));
        const auto c16m  = hn::Set(du16q, uint16_t(16));
        const auto c128  = hn::Set(du16q, uint16_t(128));
        const auto c16b  = hn::Set(du16q, uint16_t(16));

        size_t x = 0;
        for(; x + Nq <= width; x += Nq) {
                hn::Vec<decltype(du8q)> r, g, b, a;
                hn::LoadInterleaved4(du8q, src + x * 4, r, g, b, a);
                auto r16 = hn::PromoteTo(du16q, r);
                auto g16 = hn::PromoteTo(du16q, g);
                auto b16 = hn::PromoteTo(du16q, b);
                auto acc = hn::MulAdd(r16, c47,
                           hn::MulAdd(g16, c157,
                           hn::MulAdd(b16, c16m, c128)));
                auto y16 = hn::Add(hn::ShiftRight<8>(acc), c16b);
                auto y8  = hn::DemoteTo(du8q, y16);
                hn::StoreU(y8, du8q, lumaLine + x);
        }
        for(; x < width; x++) {
                int r = src[x * 4 + 0], g = src[x * 4 + 1], b = src[x * 4 + 2];
                int y, cb, cr;
                rgba8ToYCbCr709(r, g, b, &y, &cb, &cr);
                lumaLine[x] = clipU8(y);
        }

        // Chroma (SIMD): horizontally average adjacent pixel pairs,
        // then apply BT.709:
        //   Cb = ((-26*r -  87*g + 112*b + 128) >> 8) + 128
        //   Cr = (( 112*r - 102*g -  10*b + 128) >> 8) + 128
        // With r,g,b in [0,255], all intermediates fit in i16.
        // One iteration produces Nq (cb,cr) pairs from 2*Nq input pixels.
        //
        // LoadInterleaved4 uses the half-width u8 tag (2*Nq lanes);
        // we then BitCast to u16 to mask/shift even vs. odd pixel bytes
        // (little-endian: low byte = even, high byte = odd pixel).
        const auto cByteMask = hn::Set(du16q, uint16_t(0x00FF));
        const auto cOne      = hn::Set(du16q, uint16_t(1));
        const auto km26    = hn::Set(di16q, int16_t(-26));
        const auto km87    = hn::Set(di16q, int16_t(-87));
        const auto cK112   = hn::Set(di16q, int16_t(112));
        const auto km102   = hn::Set(di16q, int16_t(-102));
        const auto km10    = hn::Set(di16q, int16_t(-10));
        const auto cBias128 = hn::Set(di16q, int16_t(128));

        size_t pairs = width / 2;
        size_t i = 0;
        for(; i + Nq <= pairs; i += Nq) {
                hn::Vec<decltype(du8h)> r8, g8, b8, a8;
                hn::LoadInterleaved4(du8h, src + i * 8, r8, g8, b8, a8);
                auto rU = hn::BitCast(du16q, r8);
                auto gU = hn::BitCast(du16q, g8);
                auto bU = hn::BitCast(du16q, b8);
                auto rE = hn::And(rU, cByteMask);
                auto rO = hn::ShiftRight<8>(rU);
                auto gE = hn::And(gU, cByteMask);
                auto gO = hn::ShiftRight<8>(gU);
                auto bE = hn::And(bU, cByteMask);
                auto bO = hn::ShiftRight<8>(bU);
                auto rAvg = hn::BitCast(di16q,
                                hn::ShiftRight<1>(hn::Add(hn::Add(rE, rO), cOne)));
                auto gAvg = hn::BitCast(di16q,
                                hn::ShiftRight<1>(hn::Add(hn::Add(gE, gO), cOne)));
                auto bAvg = hn::BitCast(di16q,
                                hn::ShiftRight<1>(hn::Add(hn::Add(bE, bO), cOne)));

                auto cbAcc = hn::MulAdd(rAvg, km26,
                             hn::MulAdd(gAvg, km87,
                             hn::MulAdd(bAvg, cK112, cBias128)));
                auto crAcc = hn::MulAdd(rAvg, cK112,
                             hn::MulAdd(gAvg, km102,
                             hn::MulAdd(bAvg, km10, cBias128)));
                auto cb16 = hn::Add(hn::ShiftRight<8>(cbAcc), cBias128);
                auto cr16 = hn::Add(hn::ShiftRight<8>(crAcc), cBias128);
                auto cb8  = hn::DemoteTo(du8q, cb16);
                auto cr8  = hn::DemoteTo(du8q, cr16);
                hn::StoreInterleaved2(cb8, cr8, du8q, chromaLine + i * 2);
        }
        for(; i < pairs; i++) {
                int r = (src[i * 8 + 0] + src[i * 8 + 4] + 1) >> 1;
                int g = (src[i * 8 + 1] + src[i * 8 + 5] + 1) >> 1;
                int b = (src[i * 8 + 2] + src[i * 8 + 6] + 1) >> 1;
                int y, cb, cr;
                rgba8ToYCbCr709(r, g, b, &y, &cb, &cr);
                chromaLine[i * 2]     = clipU8(cb);
                chromaLine[i * 2 + 1] = clipU8(cr);
        }
        if(width & 1) {
                size_t x2 = width - 1;
                int r = src[x2 * 4 + 0], g = src[x2 * 4 + 1], b = src[x2 * 4 + 2];
                int y, cb, cr;
                rgba8ToYCbCr709(r, g, b, &y, &cb, &cr);
                chromaLine[pairs * 2]     = clipU8(cb);
                chromaLine[pairs * 2 + 1] = clipU8(cr);
        }
        return;
}

// =========================================================================
// YUV8_420_SemiPlanar (NV12) <-> RGB8_sRGB  (BT.709 fixed-point)
// Identical to the RGBA8 variants but with 3-byte-per-pixel RGB stride.
// =========================================================================

void FastPathNV12toRGB8(const void *const *srcPlanes,
                         const size_t *srcStrides,
                         void *const *dstPlanes,
                         const size_t *dstStrides,
                         size_t width, CSCContext &ctx) {
        const uint8_t *lumaLine = static_cast<const uint8_t *>(srcPlanes[0]);
        const uint8_t *chromaLine = static_cast<const uint8_t *>(srcPlanes[1]);
        uint8_t *dst = static_cast<uint8_t *>(dstPlanes[0]);

        // SIMD decode using reformulated BT.709 coefficients (see
        // FastPathNV12toRGBA8 for the derivation).  One iteration
        // produces 2*Nq output pixels from Nq chroma pairs.
        const hn::Rebind<uint8_t,  hn::ScalableTag<float>>    du8q;
        const hn::Rebind<uint16_t, hn::ScalableTag<float>>    du16q;
        const hn::Rebind<int16_t,  hn::ScalableTag<float>>    di16q;
        const hn::Rebind<uint8_t,  hn::ScalableTag<uint16_t>> du8h;
        const size_t Nq = hn::Lanes(du8q);

        const auto kC16  = hn::Set(di16q, int16_t(16));
        const auto kC128 = hn::Set(di16q, int16_t(128));
        const auto kBias = hn::Set(di16q, int16_t(128));
        const auto k42   = hn::Set(di16q, int16_t(42));
        const auto k203  = hn::Set(di16q, int16_t(203));
        const auto km55  = hn::Set(di16q, int16_t(-55));
        const auto km136 = hn::Set(di16q, int16_t(-136));
        const auto k29   = hn::Set(di16q, int16_t(29));

        size_t x = 0;
        for(; x + 2 * Nq <= width; x += 2 * Nq) {
                hn::Vec<decltype(du8q)> yE8, yO8, cb8, cr8;
                hn::LoadInterleaved2(du8q, lumaLine + x,  yE8, yO8);
                hn::LoadInterleaved2(du8q, chromaLine + x, cb8, cr8);

                auto yE = hn::BitCast(di16q, hn::PromoteTo(du16q, yE8));
                auto yO = hn::BitCast(di16q, hn::PromoteTo(du16q, yO8));
                auto cb = hn::BitCast(di16q, hn::PromoteTo(du16q, cb8));
                auto cr = hn::BitCast(di16q, hn::PromoteTo(du16q, cr8));
                auto cE = hn::Sub(yE, kC16);
                auto cO = hn::Sub(yO, kC16);
                auto d  = hn::Sub(cb, kC128);
                auto e  = hn::Sub(cr, kC128);

                auto k42e_common = hn::MulAdd(e, k203, kBias);
                auto rE = hn::Add(hn::Add(cE, e), hn::ShiftRight<8>(hn::MulAdd(cE, k42, k42e_common)));
                auto rO = hn::Add(hn::Add(cO, e), hn::ShiftRight<8>(hn::MulAdd(cO, k42, k42e_common)));

                auto gde_common = hn::MulAdd(d, km55, hn::MulAdd(e, km136, kBias));
                auto gE = hn::Add(cE, hn::ShiftRight<8>(hn::MulAdd(cE, k42, gde_common)));
                auto gO = hn::Add(cO, hn::ShiftRight<8>(hn::MulAdd(cO, k42, gde_common)));

                auto k29d_common = hn::MulAdd(d, k29, kBias);
                auto twoD = hn::Add(d, d);
                auto bE = hn::Add(hn::Add(cE, twoD), hn::ShiftRight<8>(hn::MulAdd(cE, k42, k29d_common)));
                auto bO = hn::Add(hn::Add(cO, twoD), hn::ShiftRight<8>(hn::MulAdd(cO, k42, k29d_common)));

                // See FastPathNV12toRGBA8 for why the Whole variants are
                // required (per-block InterleaveLower/Upper would swap
                // lane blocks on AVX-256/512).
                auto rLo = hn::InterleaveWholeLower(di16q, rE, rO);
                auto rHi = hn::InterleaveWholeUpper(di16q, rE, rO);
                auto gLo = hn::InterleaveWholeLower(di16q, gE, gO);
                auto gHi = hn::InterleaveWholeUpper(di16q, gE, gO);
                auto bLo = hn::InterleaveWholeLower(di16q, bE, bO);
                auto bHi = hn::InterleaveWholeUpper(di16q, bE, bO);

                auto r = hn::Combine(du8h, hn::DemoteTo(du8q, rHi), hn::DemoteTo(du8q, rLo));
                auto g = hn::Combine(du8h, hn::DemoteTo(du8q, gHi), hn::DemoteTo(du8q, gLo));
                auto b = hn::Combine(du8h, hn::DemoteTo(du8q, bHi), hn::DemoteTo(du8q, bLo));

                hn::StoreInterleaved3(r, g, b, du8h, dst + x * 3);
        }

        // Scalar tail
        size_t pairs = width / 2;
        for(size_t i = x / 2; i < pairs; i++) {
                int y0 = lumaLine[i * 2];
                int y1 = lumaLine[i * 2 + 1];
                int cb = chromaLine[i * 2];
                int cr = chromaLine[i * 2 + 1];
                uint8_t tmp[4];
                yuv709ToRgba8(y0, cb, cr, tmp);
                dst[i * 6 + 0] = tmp[0];
                dst[i * 6 + 1] = tmp[1];
                dst[i * 6 + 2] = tmp[2];
                yuv709ToRgba8(y1, cb, cr, tmp);
                dst[i * 6 + 3] = tmp[0];
                dst[i * 6 + 4] = tmp[1];
                dst[i * 6 + 5] = tmp[2];
        }
        if(width & 1) {
                int y0 = lumaLine[width - 1];
                int cb = chromaLine[pairs * 2];
                int cr = chromaLine[pairs * 2 + 1];
                uint8_t tmp[4];
                yuv709ToRgba8(y0, cb, cr, tmp);
                dst[(width - 1) * 3 + 0] = tmp[0];
                dst[(width - 1) * 3 + 1] = tmp[1];
                dst[(width - 1) * 3 + 2] = tmp[2];
        }
        return;
}

void FastPathRGB8toNV12(const void *const *srcPlanes,
                         const size_t *srcStrides,
                         void *const *dstPlanes,
                         const size_t *dstStrides,
                         size_t width, CSCContext &ctx) {
        const uint8_t *src = static_cast<const uint8_t *>(srcPlanes[0]);
        uint8_t *lumaLine = static_cast<uint8_t *>(dstPlanes[0]);
        uint8_t *chromaLine = static_cast<uint8_t *>(dstPlanes[1]);

        // Luma (SIMD): y = ((47*r + 157*g + 16*b + 128) >> 8) + 16, u16-safe.
        const hn::Rebind<uint8_t,  hn::ScalableTag<float>>    du8q;
        const hn::Rebind<uint16_t, hn::ScalableTag<float>>    du16q;
        const hn::Rebind<int16_t,  hn::ScalableTag<float>>    di16q;
        const hn::Rebind<uint8_t,  hn::ScalableTag<uint16_t>> du8h;
        const size_t Nq = hn::Lanes(du8q);
        const auto c47   = hn::Set(du16q, uint16_t(47));
        const auto c157  = hn::Set(du16q, uint16_t(157));
        const auto c16m  = hn::Set(du16q, uint16_t(16));
        const auto c128  = hn::Set(du16q, uint16_t(128));
        const auto c16b  = hn::Set(du16q, uint16_t(16));

        size_t x = 0;
        for(; x + Nq <= width; x += Nq) {
                hn::Vec<decltype(du8q)> r, g, b;
                hn::LoadInterleaved3(du8q, src + x * 3, r, g, b);
                auto r16 = hn::PromoteTo(du16q, r);
                auto g16 = hn::PromoteTo(du16q, g);
                auto b16 = hn::PromoteTo(du16q, b);
                auto acc = hn::MulAdd(r16, c47,
                           hn::MulAdd(g16, c157,
                           hn::MulAdd(b16, c16m, c128)));
                auto y16 = hn::Add(hn::ShiftRight<8>(acc), c16b);
                auto y8  = hn::DemoteTo(du8q, y16);
                hn::StoreU(y8, du8q, lumaLine + x);
        }
        for(; x < width; x++) {
                int r = src[x * 3 + 0], g = src[x * 3 + 1], b = src[x * 3 + 2];
                int y, cb, cr;
                rgba8ToYCbCr709(r, g, b, &y, &cb, &cr);
                lumaLine[x] = clipU8(y);
        }

        // Chroma (SIMD): same scheme as FastPathRGBA8toNV12, but
        // LoadInterleaved3 for the 3-byte RGB stride.
        const auto cByteMask = hn::Set(du16q, uint16_t(0x00FF));
        const auto cOne      = hn::Set(du16q, uint16_t(1));
        const auto km26    = hn::Set(di16q, int16_t(-26));
        const auto km87    = hn::Set(di16q, int16_t(-87));
        const auto cK112   = hn::Set(di16q, int16_t(112));
        const auto km102   = hn::Set(di16q, int16_t(-102));
        const auto km10    = hn::Set(di16q, int16_t(-10));
        const auto cBias128 = hn::Set(di16q, int16_t(128));

        size_t pairs = width / 2;
        size_t i = 0;
        for(; i + Nq <= pairs; i += Nq) {
                hn::Vec<decltype(du8h)> r8, g8, b8;
                hn::LoadInterleaved3(du8h, src + i * 6, r8, g8, b8);
                auto rU = hn::BitCast(du16q, r8);
                auto gU = hn::BitCast(du16q, g8);
                auto bU = hn::BitCast(du16q, b8);
                auto rE = hn::And(rU, cByteMask);
                auto rO = hn::ShiftRight<8>(rU);
                auto gE = hn::And(gU, cByteMask);
                auto gO = hn::ShiftRight<8>(gU);
                auto bE = hn::And(bU, cByteMask);
                auto bO = hn::ShiftRight<8>(bU);
                auto rAvg = hn::BitCast(di16q,
                                hn::ShiftRight<1>(hn::Add(hn::Add(rE, rO), cOne)));
                auto gAvg = hn::BitCast(di16q,
                                hn::ShiftRight<1>(hn::Add(hn::Add(gE, gO), cOne)));
                auto bAvg = hn::BitCast(di16q,
                                hn::ShiftRight<1>(hn::Add(hn::Add(bE, bO), cOne)));

                auto cbAcc = hn::MulAdd(rAvg, km26,
                             hn::MulAdd(gAvg, km87,
                             hn::MulAdd(bAvg, cK112, cBias128)));
                auto crAcc = hn::MulAdd(rAvg, cK112,
                             hn::MulAdd(gAvg, km102,
                             hn::MulAdd(bAvg, km10, cBias128)));
                auto cb16 = hn::Add(hn::ShiftRight<8>(cbAcc), cBias128);
                auto cr16 = hn::Add(hn::ShiftRight<8>(crAcc), cBias128);
                auto cb8  = hn::DemoteTo(du8q, cb16);
                auto cr8  = hn::DemoteTo(du8q, cr16);
                hn::StoreInterleaved2(cb8, cr8, du8q, chromaLine + i * 2);
        }
        for(; i < pairs; i++) {
                int r = (src[i * 6 + 0] + src[i * 6 + 3] + 1) >> 1;
                int g = (src[i * 6 + 1] + src[i * 6 + 4] + 1) >> 1;
                int b = (src[i * 6 + 2] + src[i * 6 + 5] + 1) >> 1;
                int y, cb, cr;
                rgba8ToYCbCr709(r, g, b, &y, &cb, &cr);
                chromaLine[i * 2]     = clipU8(cb);
                chromaLine[i * 2 + 1] = clipU8(cr);
        }
        if(width & 1) {
                size_t x2 = width - 1;
                int r = src[x2 * 3 + 0], g = src[x2 * 3 + 1], b = src[x2 * 3 + 2];
                int y, cb, cr;
                rgba8ToYCbCr709(r, g, b, &y, &cb, &cr);
                chromaLine[pairs * 2]     = clipU8(cb);
                chromaLine[pairs * 2 + 1] = clipU8(cr);
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
                yuv709ToRgba8(y0, cb, cr, dst + i * 8);
                yuv709ToRgba8(y1, cb, cr, dst + i * 8 + 4);
        }
        if(width & 1) {
                size_t i = pairs;
                int cb = src[i * 4 + 0];
                int y0 = src[i * 4 + 1];
                int cr = src[i * 4 + 2];
                yuv709ToRgba8(y0, cb, cr, dst + i * 8);
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
                int y0, cb0, cr0, y1, cb1, cr1;
                rgba8ToYCbCr709(r0, g0, b0, &y0, &cb0, &cr0);
                rgba8ToYCbCr709(r1, g1, b1, &y1, &cb1, &cr1);
                dst[i*4+0] = clipU8((cb0 + cb1 + 1) >> 1);
                dst[i*4+1] = clipU8(y0);
                dst[i*4+2] = clipU8((cr0 + cr1 + 1) >> 1);
                dst[i*4+3] = clipU8(y1);
        }
        if(width & 1) {
                size_t i = pairs;
                int r = src[i*8+0], g = src[i*8+1], b = src[i*8+2];
                int y, cb, cr;
                rgba8ToYCbCr709(r, g, b, &y, &cb, &cr);
                dst[i*4+0] = clipU8(cb);
                dst[i*4+1] = clipU8(y);
                dst[i*4+2] = clipU8(cr);
                dst[i*4+3] = dst[i*4+1]; // duplicate Y for the phantom odd-pair
        }
        return;
}

// =========================================================================
// YUYV8 <-> UYVY8  (byte-pair swap, no colour math)
// YUYV byte order: [Y0, Cb, Y1, Cr]
// UYVY byte order: [Cb, Y0, Cr, Y1]
// =========================================================================

void FastPathYUYV8toUYVY8(const void *const *srcPlanes,
                           const size_t *srcStrides,
                           void *const *dstPlanes,
                           const size_t *dstStrides,
                           size_t width, CSCContext &ctx) {
        const uint8_t *src = static_cast<const uint8_t *>(srcPlanes[0]);
        uint8_t *dst = static_cast<uint8_t *>(dstPlanes[0]);
        size_t pairs = width / 2;
        for(size_t i = 0; i < pairs; i++) {
                dst[i*4+0] = src[i*4+1]; // Cb
                dst[i*4+1] = src[i*4+0]; // Y0
                dst[i*4+2] = src[i*4+3]; // Cr
                dst[i*4+3] = src[i*4+2]; // Y1
        }
        if(width & 1) {
                size_t i = pairs;
                dst[i*4+0] = src[i*4+1]; // Cb
                dst[i*4+1] = src[i*4+0]; // Y0
                dst[i*4+2] = src[i*4+3]; // Cr
                dst[i*4+3] = src[i*4+0]; // Y1 absent; duplicate Y0
        }
}

void FastPathUYVY8toYUYV8(const void *const *srcPlanes,
                           const size_t *srcStrides,
                           void *const *dstPlanes,
                           const size_t *dstStrides,
                           size_t width, CSCContext &ctx) {
        const uint8_t *src = static_cast<const uint8_t *>(srcPlanes[0]);
        uint8_t *dst = static_cast<uint8_t *>(dstPlanes[0]);
        size_t pairs = width / 2;
        for(size_t i = 0; i < pairs; i++) {
                dst[i*4+0] = src[i*4+1]; // Y0
                dst[i*4+1] = src[i*4+0]; // Cb
                dst[i*4+2] = src[i*4+3]; // Y1
                dst[i*4+3] = src[i*4+2]; // Cr
        }
        if(width & 1) {
                size_t i = pairs;
                dst[i*4+0] = src[i*4+1]; // Y0
                dst[i*4+1] = src[i*4+0]; // Cb
                dst[i*4+2] = src[i*4+1]; // Y1 absent; duplicate Y0
                dst[i*4+3] = src[i*4+2]; // Cr
        }
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
                yuv709ToRgba8(y0, cb, cr, dst + i * 8);
                yuv709ToRgba8(y1, cb, cr, dst + i * 8 + 4);
        }
        if(width & 1) {
                int y0 = luma[width - 1];
                int cr = chroma[pairs * 2];
                int cb = chroma[pairs * 2 + 1];
                yuv709ToRgba8(y0, cb, cr, dst + (width - 1) * 4);
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
                int y, cb, cr;
                rgba8ToYCbCr709(r, g, b, &y, &cb, &cr);
                luma[x] = clipU8(y);
        }
        size_t pairs = width / 2;
        for(size_t i = 0; i < pairs; i++) {
                int r = (src[i*8+0]+src[i*8+4]+1)>>1;
                int g = (src[i*8+1]+src[i*8+5]+1)>>1;
                int b = (src[i*8+2]+src[i*8+6]+1)>>1;
                int y, cb, cr;
                rgba8ToYCbCr709(r, g, b, &y, &cb, &cr);
                chroma[i*2]   = clipU8(cr); // Cr first
                chroma[i*2+1] = clipU8(cb); // Cb second
        }
        if(width & 1) {
                size_t x = width - 1;
                int r = src[x*4+0], g = src[x*4+1], b = src[x*4+2];
                int y, cb, cr;
                rgba8ToYCbCr709(r, g, b, &y, &cb, &cr);
                chroma[pairs*2]     = clipU8(cr);
                chroma[pairs*2 + 1] = clipU8(cb);
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
                yuv709ToRgba8(y0, cb, cr, dst + i * 8);
                yuv709ToRgba8(y1, cb, cr, dst + i * 8 + 4);
        }
        if(width & 1) {
                int y0 = luma[width - 1];
                int cb = chroma[pairs * 2];
                int cr = chroma[pairs * 2 + 1];
                yuv709ToRgba8(y0, cb, cr, dst + (width - 1) * 4);
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
                int y, cb, cr;
                rgba8ToYCbCr709(r, g, b, &y, &cb, &cr);
                luma[x] = clipU8(y);
        }
        size_t pairs = width / 2;
        for(size_t i = 0; i < pairs; i++) {
                int r = (src[i*8+0]+src[i*8+4]+1)>>1;
                int g = (src[i*8+1]+src[i*8+5]+1)>>1;
                int b = (src[i*8+2]+src[i*8+6]+1)>>1;
                int y, cb, cr;
                rgba8ToYCbCr709(r, g, b, &y, &cb, &cr);
                chroma[i*2]   = clipU8(cb);
                chroma[i*2+1] = clipU8(cr);
        }
        if(width & 1) {
                size_t x = width - 1;
                int r = src[x*4+0], g = src[x*4+1], b = src[x*4+2];
                int y, cb, cr;
                rgba8ToYCbCr709(r, g, b, &y, &cb, &cr);
                chroma[pairs*2]     = clipU8(cb);
                chroma[pairs*2 + 1] = clipU8(cr);
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
                yuv709ToRgba8(y0, cb, cr, dst + i * 8);
                yuv709ToRgba8(y1, cb, cr, dst + i * 8 + 4);
        }
        if(width & 1) {
                int y0 = yp[width - 1];
                int cb = cbp[pairs], cr = crp[pairs];
                yuv709ToRgba8(y0, cb, cr, dst + (width - 1) * 4);
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
                int y, cb, cr;
                rgba8ToYCbCr709(r, g, b, &y, &cb, &cr);
                yp[x] = clipU8(y);
        }
        size_t pairs = width / 2;
        for(size_t i = 0; i < pairs; i++) {
                int r = (src[i*8+0]+src[i*8+4]+1)>>1;
                int g = (src[i*8+1]+src[i*8+5]+1)>>1;
                int b = (src[i*8+2]+src[i*8+6]+1)>>1;
                int y, cb, cr;
                rgba8ToYCbCr709(r, g, b, &y, &cb, &cr);
                cbp[i] = clipU8(cb);
                crp[i] = clipU8(cr);
        }
        if(width & 1) {
                size_t x = width - 1;
                int r = src[x*4+0], g = src[x*4+1], b = src[x*4+2];
                int y, cb, cr;
                rgba8ToYCbCr709(r, g, b, &y, &cb, &cr);
                cbp[pairs] = clipU8(cb);
                crp[pairs] = clipU8(cr);
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
// BT.601 limited-range (Kr=0.299, Kg=0.587, Kb=0.114), 8-bit fixed point:
//
// RGB -> YCbCr:
//   Y  = (( 66*R + 129*G +  25*B + 128) >> 8) + 16
//   Cb = ((-38*R -  74*G + 112*B + 128) >> 8) + 128
//   Cr = ((112*R -  94*G -  18*B + 128) >> 8) + 128
//
// YCbCr -> RGB:
//   C = Y - 16,  D = Cb - 128,  E = Cr - 128
//   R = clip((298*C + 409*E + 128) >> 8)
//   G = clip((298*C - 100*D - 208*E + 128) >> 8)
//   B = clip((298*C + 516*D + 128) >> 8)
//
// The previous version of this file mixed *full-range* 601 RGB->YCbCr
// coefficients with *limited-range* offsets (so +16 / +128 were added
// to values that were already full-range), and the decode side mixed
// <<8 luma scaling (298 for 1.164) with <<7 chroma scaling (179 for
// 1.402 * 128, etc.).  The result round-tripped very poorly: pure
// primaries decoded to roughly (178, 52, 52) style desaturated colors.
// These coefficients match the canonical BT.601 limited-range
// derivation and round-trip primaries to within a single LSB.

static inline void yuv601ToRgba8(int y, int cb, int cr, uint8_t *dst) {
        int c = y - 16, d = cb - 128, e = cr - 128;
        dst[0] = clipU8((298*c + 409*e + 128) >> 8);
        dst[1] = clipU8((298*c - 100*d - 208*e + 128) >> 8);
        dst[2] = clipU8((298*c + 516*d + 128) >> 8);
        dst[3] = 255;
}

static inline void rgba8ToYCbCr601(int r, int g, int b, int *y, int *cb, int *cr) {
        *y  = (( 66*r + 129*g +  25*b + 128) >> 8) + 16;
        *cb = ((-38*r -  74*g + 112*b + 128) >> 8) + 128;
        *cr = ((112*r -  94*g -  18*b + 128) >> 8) + 128;
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
        if(width & 1) {
                size_t i = pairs;
                int y0 = src[i*4+0], cb = src[i*4+1], cr = src[i*4+3];
                yuv601ToRgba8(y0, cb, cr, dst + i*8);
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
        if(width & 1) {
                size_t i = pairs;
                int r = src[i*8+0], g = src[i*8+1], b = src[i*8+2];
                int y, cb, cr;
                rgba8ToYCbCr601(r, g, b, &y, &cb, &cr);
                dst[i*4+0] = clipU8(y);
                dst[i*4+1] = clipU8(cb);
                dst[i*4+2] = dst[i*4+0];
                dst[i*4+3] = clipU8(cr);
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
        if(width & 1) {
                size_t i = pairs;
                int cb = src[i*4+0], y0 = src[i*4+1], cr = src[i*4+2];
                yuv601ToRgba8(y0, cb, cr, dst + i*8);
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
        if(width & 1) {
                size_t i = pairs;
                int r = src[i*8+0], g = src[i*8+1], b = src[i*8+2];
                int y, cb, cr;
                rgba8ToYCbCr601(r, g, b, &y, &cb, &cr);
                dst[i*4+0] = clipU8(cb);
                dst[i*4+1] = clipU8(y);
                dst[i*4+2] = clipU8(cr);
                dst[i*4+3] = dst[i*4+1];
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
        if(width & 1) {
                int y0 = luma[width - 1];
                int cb = chroma[pairs * 2];
                int cr = chroma[pairs * 2 + 1];
                yuv601ToRgba8(y0, cb, cr, dst + (width - 1) * 4);
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
                luma[x] = clipU8((( 66*r + 129*g +  25*b + 128) >> 8) + 16);
        }
        size_t pairs = width / 2;
        for(size_t i = 0; i < pairs; i++) {
                int r = (src[i*8+0]+src[i*8+4]+1)>>1;
                int g = (src[i*8+1]+src[i*8+5]+1)>>1;
                int b = (src[i*8+2]+src[i*8+6]+1)>>1;
                chroma[i*2]   = clipU8(((-38*r -  74*g + 112*b + 128) >> 8) + 128);
                chroma[i*2+1] = clipU8(((112*r -  94*g -  18*b + 128) >> 8) + 128);
        }
        if(width & 1) {
                size_t x = width - 1;
                int r = src[x*4+0], g = src[x*4+1], b = src[x*4+2];
                chroma[pairs*2]     = clipU8(((-38*r -  74*g + 112*b + 128) >> 8) + 128);
                chroma[pairs*2 + 1] = clipU8(((112*r -  94*g -  18*b + 128) >> 8) + 128);
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
        if(width & 1) {
                yuv601ToRgba8(yp[width - 1], cbp[pairs], crp[pairs],
                              dst + (width - 1) * 4);
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
                yp[x] = clipU8((( 66*r + 129*g +  25*b + 128) >> 8) + 16);
        }
        size_t pairs = width / 2;
        for(size_t i = 0; i < pairs; i++) {
                int r = (src[i*8+0]+src[i*8+4]+1)>>1;
                int g = (src[i*8+1]+src[i*8+5]+1)>>1;
                int b = (src[i*8+2]+src[i*8+6]+1)>>1;
                cbp[i] = clipU8(((-38*r -  74*g + 112*b + 128) >> 8) + 128);
                crp[i] = clipU8(((112*r -  94*g -  18*b + 128) >> 8) + 128);
        }
        if(width & 1) {
                size_t x = width - 1;
                int r = src[x*4+0], g = src[x*4+1], b = src[x*4+2];
                cbp[pairs] = clipU8(((-38*r -  74*g + 112*b + 128) >> 8) + 128);
                crp[pairs] = clipU8(((112*r -  94*g -  18*b + 128) >> 8) + 128);
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
// BT.709 10-bit helpers (limited range)
// =========================================================================
//
// BT.709 10-bit limited range (Kr=0.2126, Kg=0.7152, Kb=0.0722):
//   Y: 64-940,  Cb/Cr: 64-960  (out of 0-1023)
//
// YCbCr -> RGB:
//   C = Y - 64,  D = Cb - 512,  E = Cr - 512
//   R = clip10((1196*C + 1842*E + 512) >> 10)
//   G = clip10((1196*C -  219*D - 547*E + 512) >> 10)
//   B = clip10((1196*C + 2170*D + 512) >> 10)
//
// RGB -> YCbCr:
//   Y  = clip10(( 186*R + 627*G +  63*B + 512) >> 10) + 64
//   Cb = clip10((-103*R - 346*G + 448*B + 512) >> 10) + 512
//   Cr = clip10(( 448*R - 407*G -  41*B + 512) >> 10) + 512
//
// The previous coefficients here (263/516/100 for Y, 1634/401/833/2066
// for decode) were actually the BT.601 10-bit coefficients — same
// mislabeling bug as the 8-bit kernels.  Fixed to use the canonical
// BT.709 weights so that round-tripping RGBA10 <-> YUV10_422_*_Rec709
// produces true BT.709 data.

static inline void yuv10ToRgba10(int y, int cb, int cr, uint16_t *dst) {
        int c = y - 64, d = cb - 512, e = cr - 512;
        dst[0] = clip10((1196*c + 1842*e + 512) >> 10);
        dst[1] = clip10((1196*c -  219*d - 547*e + 512) >> 10);
        dst[2] = clip10((1196*c + 2170*d + 512) >> 10);
        dst[3] = 1023;
}

static inline void rgba10ToYCbCr(int r, int g, int b, int *y, int *cb, int *cr) {
        *y  = (( 186*r + 627*g +  63*b + 512) >> 10) + 64;
        *cb = ((-103*r - 346*g + 448*b + 512) >> 10) + 512;
        *cr = (( 448*r - 407*g -  41*b + 512) >> 10) + 512;
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

// =========================================================================
// v210 <-> RGBA8  (BT.709 fixed-point)
// =========================================================================
//
// v210 has a specialized bit-packing layout that the generic CSC
// pipeline's unpack/pack can't handle (6 pixels in 16 bytes with
// chroma subsampling and 128-byte line alignment).  There are fast
// paths for v210 <-> RGBA10_LE_sRGB, but UncompressedVideoPayload::convert()
// between RGBA8 and v210 has no direct entry, and the general pipeline
// silently produces garbage because UnpackInterleavedImpl treats v210
// as a generic 3x10 interleaved 4:2:2 format.  These wrapper kernels
// fill the 8-bit gap by inlining an RGBA8<->RGBA10 bit-depth step
// around the existing v210 <-> RGBA10 math.

// Bit-replication scaling: 8-bit -> 10-bit (v << 2) | (v >> 6)
// gives the full [0,1023] range (vs a plain <<2 which maxes at 1020).
static inline int rgb8to10(int v) {
        return (v << 2) | (v >> 6);
}

// 10-bit -> 8-bit with rounding: (v + 2) >> 2, clamped.
static inline uint8_t rgb10to8(int v) {
        int r = (v + 2) >> 2;
        return clipU8(r);
}

void FastPathV210toRGBA8(const void *const *srcPlanes,
                          const size_t *srcStrides,
                          void *const *dstPlanes,
                          const size_t *dstStrides,
                          size_t width, CSCContext &ctx) {
        const uint32_t *src = static_cast<const uint32_t *>(srcPlanes[0]);
        uint8_t *dst = static_cast<uint8_t *>(dstPlanes[0]);

        auto emit = [&](int y, int cb, int cr, uint8_t *out) {
                uint16_t tmp[4];
                yuv10ToRgba10(y, cb, cr, tmp);
                out[0] = rgb10to8(tmp[0]);
                out[1] = rgb10to8(tmp[1]);
                out[2] = rgb10to8(tmp[2]);
                out[3] = 255;
        };

        size_t blocks = width / 6;
        for(size_t b = 0; b < blocks; b++) {
                const uint32_t *w = src + b * 4;
                uint8_t *out = dst + b * 6 * 4;

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

                emit(y0, cb0, cr0, out);
                emit(y1, cb0, cr0, out + 4);
                emit(y2, cb1, cr1, out + 8);
                emit(y3, cb1, cr1, out + 12);
                emit(y4, cb2, cr2, out + 16);
                emit(y5, cb2, cr2, out + 20);
        }

        // Trailing 1-5 pixels
        size_t done = blocks * 6;
        if(done < width) {
                const uint32_t *w = src + blocks * 4;
                int samples[6][3];
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
                        emit(samples[i][0], samples[i][1], samples[i][2],
                             dst + (done + i) * 4);
                }
        }
        return;
}

void FastPathRGBA8toV210(const void *const *srcPlanes,
                          const size_t *srcStrides,
                          void *const *dstPlanes,
                          const size_t *dstStrides,
                          size_t width, CSCContext &ctx) {
        const uint8_t *src = static_cast<const uint8_t *>(srcPlanes[0]);
        uint32_t *dst = static_cast<uint32_t *>(dstPlanes[0]);

        size_t blocks = width / 6;
        for(size_t b = 0; b < blocks; b++) {
                const uint8_t *in = src + b * 6 * 4;
                uint32_t *w = dst + b * 4;

                int y[6], cb[6], cr[6];
                for(int p = 0; p < 6; p++) {
                        int r = rgb8to10(in[p*4 + 0]);
                        int g = rgb8to10(in[p*4 + 1]);
                        int bl= rgb8to10(in[p*4 + 2]);
                        rgba10ToYCbCr(r, g, bl, &y[p], &cb[p], &cr[p]);
                }

                int cb0 = (cb[0]+cb[1]+1)>>1, cr0 = (cr[0]+cr[1]+1)>>1;
                int cb1 = (cb[2]+cb[3]+1)>>1, cr1 = (cr[2]+cr[3]+1)>>1;
                int cb2 = (cb[4]+cb[5]+1)>>1, cr2 = (cr[4]+cr[5]+1)>>1;

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

        // Trailing 1-5 pixels (partial block)
        size_t done = blocks * 6;
        if(done < width) {
                const uint8_t *in = src + done * 4;
                uint32_t *w = dst + blocks * 4;
                size_t rem = width - done;

                // Compute YCbCr for the remaining pixels; pad missing
                // slots with the last valid pixel's values.
                int y[6] = {}, cb[6] = {}, cr[6] = {};
                for(size_t p = 0; p < rem; p++) {
                        int r = rgb8to10(in[p*4 + 0]);
                        int g = rgb8to10(in[p*4 + 1]);
                        int bl= rgb8to10(in[p*4 + 2]);
                        rgba10ToYCbCr(r, g, bl, &y[p], &cb[p], &cr[p]);
                }
                for(size_t p = rem; p < 6; p++) {
                        y[p] = y[rem-1]; cb[p] = cb[rem-1]; cr[p] = cr[rem-1];
                }

                int cb0t = (cb[0]+cb[1]+1)>>1, cr0t = (cr[0]+cr[1]+1)>>1;
                int cb1t = (cb[2]+cb[3]+1)>>1, cr1t = (cr[2]+cr[3]+1)>>1;
                int cb2t = (cb[4]+cb[5]+1)>>1, cr2t = (cr[4]+cr[5]+1)>>1;

                w[0] = (static_cast<uint32_t>(clip10(cb0t)))
                     | (static_cast<uint32_t>(clip10(y[0]))  << 10)
                     | (static_cast<uint32_t>(clip10(cr0t))  << 20);
                w[1] = (static_cast<uint32_t>(clip10(y[1])))
                     | (static_cast<uint32_t>(clip10(cb1t))  << 10)
                     | (static_cast<uint32_t>(clip10(y[2]))  << 20);
                w[2] = (static_cast<uint32_t>(clip10(cr1t)))
                     | (static_cast<uint32_t>(clip10(y[3]))  << 10)
                     | (static_cast<uint32_t>(clip10(cb2t))  << 20);
                w[3] = (static_cast<uint32_t>(clip10(y[4])))
                     | (static_cast<uint32_t>(clip10(cr2t))  << 10)
                     | (static_cast<uint32_t>(clip10(y[5]))  << 20);
        }
        return;
}

// =========================================================================
// 12-bit helpers
// =========================================================================
//
// 12-bit limited range: Y: 256-3760, Cb/Cr: 256-3840 (out of 0-4095)

static inline uint16_t clip12(int v) {
        return static_cast<uint16_t>(v < 0 ? 0 : (v > 4095 ? 4095 : v));
}

// BT.709 12-bit
static inline void yuv12_709ToRgba12(int y, int cb, int cr, uint16_t *dst) {
        int c = y - 256, d = cb - 2048, e = cr - 2048;
        dst[0] = clip12((4787*c + 7370*e + 2048) >> 12);
        dst[1] = clip12((4787*c - 877*d - 2191*e + 2048) >> 12);
        dst[2] = clip12((4787*c + 8684*d + 2048) >> 12);
        dst[3] = 4095;
}

static inline void rgba12ToYCbCr709_12(int r, int g, int b, int *y, int *cb, int *cr) {
        *y  = (( 745*r + 2506*g +  253*b + 2048) >> 12) + 256;
        *cb = ((-411*r - 1381*g + 1792*b + 2048) >> 12) + 2048;
        *cr = ((1792*r - 1628*g -  164*b + 2048) >> 12) + 2048;
}

// BT.2020 12-bit
static inline void yuv12_2020ToRgba12(int y, int cb, int cr, uint16_t *dst) {
        int c = y - 256, d = cb - 2048, e = cr - 2048;
        dst[0] = clip12((4787*c + 6901*e + 2048) >> 12);
        dst[1] = clip12((4787*c - 770*d - 2674*e + 2048) >> 12);
        dst[2] = clip12((4787*c + 8805*d + 2048) >> 12);
        dst[3] = 4095;
}

static inline void rgba12ToYCbCr2020_12(int r, int g, int b, int *y, int *cb, int *cr) {
        *y  = (( 921*r + 2376*g +  208*b + 2048) >> 12) + 256;
        *cb = ((-500*r - 1292*g + 1792*b + 2048) >> 12) + 2048;
        *cr = ((1792*r - 1648*g -  144*b + 2048) >> 12) + 2048;
}

// =========================================================================
// 12-bit BT.709 UYVY LE <-> RGBA12 LE
// =========================================================================

void FastPathUYVY12LE_709toRGBA12LE(const void *const *srcPlanes,
                                     const size_t *srcStrides,
                                     void *const *dstPlanes,
                                     const size_t *dstStrides,
                                     size_t width, CSCContext &ctx) {
        const uint16_t *src = static_cast<const uint16_t *>(srcPlanes[0]);
        uint16_t *dst = static_cast<uint16_t *>(dstPlanes[0]);
        size_t pairs = width / 2;
        for(size_t i = 0; i < pairs; i++) {
                int cb = src[i*4+0], y0 = src[i*4+1], cr = src[i*4+2], y1 = src[i*4+3];
                yuv12_709ToRgba12(y0, cb, cr, dst + i*8);
                yuv12_709ToRgba12(y1, cb, cr, dst + i*8 + 4);
        }
        return;
}

void FastPathRGBA12LEtoUYVY12LE_709(const void *const *srcPlanes,
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
                rgba12ToYCbCr709_12(r0, g0, b0, &y0, &cb0, &cr0);
                rgba12ToYCbCr709_12(r1, g1, b1, &y1, &cb1, &cr1);
                dst[i*4+0] = clip12((cb0+cb1+1)>>1);
                dst[i*4+1] = clip12(y0);
                dst[i*4+2] = clip12((cr0+cr1+1)>>1);
                dst[i*4+3] = clip12(y1);
        }
        return;
}

// =========================================================================
// 12-bit BT.709 Planar 422/420 LE <-> RGBA12 LE
// =========================================================================

void FastPathPlanar12LE422_709toRGBA12LE(const void *const *srcPlanes,
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
                yuv12_709ToRgba12(yp[i*2],   cbp[i], crp[i], dst + i*8);
                yuv12_709ToRgba12(yp[i*2+1], cbp[i], crp[i], dst + i*8 + 4);
        }
        return;
}

void FastPathRGBA12LEtoPlanar12LE422_709(const void *const *srcPlanes,
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
                rgba12ToYCbCr709_12(r, g, b, &y, &cb, &cr);
                yp[x] = clip12(y);
        }
        size_t pairs = width / 2;
        for(size_t i = 0; i < pairs; i++) {
                int r = (src[i*8+0]+src[i*8+4]+1)>>1;
                int g = (src[i*8+1]+src[i*8+5]+1)>>1;
                int b = (src[i*8+2]+src[i*8+6]+1)>>1;
                int y, cb, cr;
                rgba12ToYCbCr709_12(r, g, b, &y, &cb, &cr);
                cbp[i] = clip12(cb);
                crp[i] = clip12(cr);
        }
        return;
}

void FastPathPlanar12LE420_709toRGBA12LE(const void *const *s, const size_t *ss,
                                          void *const *d, const size_t *ds,
                                          size_t w, CSCContext &c) {
        FastPathPlanar12LE422_709toRGBA12LE(s, ss, d, ds, w, c);
}
void FastPathRGBA12LEtoPlanar12LE420_709(const void *const *s, const size_t *ss,
                                          void *const *d, const size_t *ds,
                                          size_t w, CSCContext &c) {
        FastPathRGBA12LEtoPlanar12LE422_709(s, ss, d, ds, w, c);
}

// =========================================================================
// 12-bit BT.709 NV12 LE <-> RGBA12 LE
// =========================================================================

void FastPathNV12_12LE_709toRGBA12LE(const void *const *srcPlanes,
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
                yuv12_709ToRgba12(y0, cb, cr, dst + i*8);
                yuv12_709ToRgba12(y1, cb, cr, dst + i*8 + 4);
        }
        return;
}

void FastPathRGBA12LEtoNV12_12LE_709(const void *const *srcPlanes,
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
                rgba12ToYCbCr709_12(r, g, b, &y, &cb, &cr);
                luma[x] = clip12(y);
        }
        size_t pairs = width / 2;
        for(size_t i = 0; i < pairs; i++) {
                int r = (src[i*8+0]+src[i*8+4]+1)>>1;
                int g = (src[i*8+1]+src[i*8+5]+1)>>1;
                int b = (src[i*8+2]+src[i*8+6]+1)>>1;
                int y, cb, cr;
                rgba12ToYCbCr709_12(r, g, b, &y, &cb, &cr);
                chroma[i*2]   = clip12(cb);
                chroma[i*2+1] = clip12(cr);
        }
        return;
}

// =========================================================================
// 12-bit BT.2020 UYVY LE <-> RGBA12 LE
// =========================================================================

void FastPathUYVY12LE_2020toRGBA12LE(const void *const *srcPlanes,
                                      const size_t *srcStrides,
                                      void *const *dstPlanes,
                                      const size_t *dstStrides,
                                      size_t width, CSCContext &ctx) {
        const uint16_t *src = static_cast<const uint16_t *>(srcPlanes[0]);
        uint16_t *dst = static_cast<uint16_t *>(dstPlanes[0]);
        size_t pairs = width / 2;
        for(size_t i = 0; i < pairs; i++) {
                int cb = src[i*4+0], y0 = src[i*4+1], cr = src[i*4+2], y1 = src[i*4+3];
                yuv12_2020ToRgba12(y0, cb, cr, dst + i*8);
                yuv12_2020ToRgba12(y1, cb, cr, dst + i*8 + 4);
        }
        return;
}

void FastPathRGBA12LEtoUYVY12LE_2020(const void *const *srcPlanes,
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
                rgba12ToYCbCr2020_12(r0, g0, b0, &y0, &cb0, &cr0);
                rgba12ToYCbCr2020_12(r1, g1, b1, &y1, &cb1, &cr1);
                dst[i*4+0] = clip12((cb0+cb1+1)>>1);
                dst[i*4+1] = clip12(y0);
                dst[i*4+2] = clip12((cr0+cr1+1)>>1);
                dst[i*4+3] = clip12(y1);
        }
        return;
}

// 12-bit BT.2020 Planar 420 LE <-> RGBA12 LE

void FastPathPlanar12LE420_2020toRGBA12LE(const void *const *srcPlanes,
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
                yuv12_2020ToRgba12(yp[i*2],   cbp[i], crp[i], dst + i*8);
                yuv12_2020ToRgba12(yp[i*2+1], cbp[i], crp[i], dst + i*8 + 4);
        }
        return;
}

void FastPathRGBA12LEtoPlanar12LE420_2020(const void *const *srcPlanes,
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
                rgba12ToYCbCr2020_12(r, g, b, &y, &cb, &cr);
                yp[x] = clip12(y);
        }
        size_t pairs = width / 2;
        for(size_t i = 0; i < pairs; i++) {
                int r = (src[i*8+0]+src[i*8+4]+1)>>1;
                int g = (src[i*8+1]+src[i*8+5]+1)>>1;
                int b = (src[i*8+2]+src[i*8+6]+1)>>1;
                int y, cb, cr;
                rgba12ToYCbCr2020_12(r, g, b, &y, &cb, &cr);
                cbp[i] = clip12(cb);
                crp[i] = clip12(cr);
        }
        return;
}

// =========================================================================
// Planar RGB8 <-> Interleaved RGB8 (pure byte shuffle, no colour math)
// =========================================================================

void FastPathPlanarRGB8toRGB8(const void *const *srcPlanes,
                              const size_t *srcStrides,
                              void *const *dstPlanes,
                              const size_t *dstStrides,
                              size_t width, CSCContext &ctx) {
        const uint8_t *rp = static_cast<const uint8_t *>(srcPlanes[0]);
        const uint8_t *gp = static_cast<const uint8_t *>(srcPlanes[1]);
        const uint8_t *bp = static_cast<const uint8_t *>(srcPlanes[2]);
        uint8_t *dst = static_cast<uint8_t *>(dstPlanes[0]);

        const hn::Rebind<uint8_t, hn::ScalableTag<float>> du8q;
        const size_t Nq = hn::Lanes(du8q);

        size_t x = 0;
        for(; x + Nq <= width; x += Nq) {
                auto r = hn::LoadU(du8q, rp + x);
                auto g = hn::LoadU(du8q, gp + x);
                auto b = hn::LoadU(du8q, bp + x);
                hn::StoreInterleaved3(r, g, b, du8q, dst + x * 3);
        }
        for(; x < width; x++) {
                dst[x * 3 + 0] = rp[x];
                dst[x * 3 + 1] = gp[x];
                dst[x * 3 + 2] = bp[x];
        }
        return;
}

void FastPathRGB8toPlanarRGB8(const void *const *srcPlanes,
                              const size_t *srcStrides,
                              void *const *dstPlanes,
                              const size_t *dstStrides,
                              size_t width, CSCContext &ctx) {
        const uint8_t *src = static_cast<const uint8_t *>(srcPlanes[0]);
        uint8_t *rp = static_cast<uint8_t *>(dstPlanes[0]);
        uint8_t *gp = static_cast<uint8_t *>(dstPlanes[1]);
        uint8_t *bp = static_cast<uint8_t *>(dstPlanes[2]);

        const hn::Rebind<uint8_t, hn::ScalableTag<float>> du8q;
        const size_t Nq = hn::Lanes(du8q);

        size_t x = 0;
        for(; x + Nq <= width; x += Nq) {
                hn::Vec<decltype(du8q)> r, g, b;
                hn::LoadInterleaved3(du8q, src + x * 3, r, g, b);
                hn::StoreU(r, du8q, rp + x);
                hn::StoreU(g, du8q, gp + x);
                hn::StoreU(b, du8q, bp + x);
        }
        for(; x < width; x++) {
                rp[x] = src[x * 3 + 0];
                gp[x] = src[x * 3 + 1];
                bp[x] = src[x * 3 + 2];
        }
        return;
}

void FastPathPlanarRGB8toRGBA8(const void *const *srcPlanes,
                               const size_t *srcStrides,
                               void *const *dstPlanes,
                               const size_t *dstStrides,
                               size_t width, CSCContext &ctx) {
        const uint8_t *rp = static_cast<const uint8_t *>(srcPlanes[0]);
        const uint8_t *gp = static_cast<const uint8_t *>(srcPlanes[1]);
        const uint8_t *bp = static_cast<const uint8_t *>(srcPlanes[2]);
        uint8_t *dst = static_cast<uint8_t *>(dstPlanes[0]);

        const hn::Rebind<uint8_t, hn::ScalableTag<float>> du8q;
        const size_t Nq = hn::Lanes(du8q);
        const auto alpha = hn::Set(du8q, uint8_t(255));

        size_t x = 0;
        for(; x + Nq <= width; x += Nq) {
                auto r = hn::LoadU(du8q, rp + x);
                auto g = hn::LoadU(du8q, gp + x);
                auto b = hn::LoadU(du8q, bp + x);
                hn::StoreInterleaved4(r, g, b, alpha, du8q, dst + x * 4);
        }
        for(; x < width; x++) {
                dst[x * 4 + 0] = rp[x];
                dst[x * 4 + 1] = gp[x];
                dst[x * 4 + 2] = bp[x];
                dst[x * 4 + 3] = 255;
        }
        return;
}

void FastPathRGBA8toPlanarRGB8(const void *const *srcPlanes,
                               const size_t *srcStrides,
                               void *const *dstPlanes,
                               const size_t *dstStrides,
                               size_t width, CSCContext &ctx) {
        const uint8_t *src = static_cast<const uint8_t *>(srcPlanes[0]);
        uint8_t *rp = static_cast<uint8_t *>(dstPlanes[0]);
        uint8_t *gp = static_cast<uint8_t *>(dstPlanes[1]);
        uint8_t *bp = static_cast<uint8_t *>(dstPlanes[2]);

        const hn::Rebind<uint8_t, hn::ScalableTag<float>> du8q;
        const size_t Nq = hn::Lanes(du8q);

        size_t x = 0;
        for(; x + Nq <= width; x += Nq) {
                hn::Vec<decltype(du8q)> r, g, b, a;
                hn::LoadInterleaved4(du8q, src + x * 4, r, g, b, a);
                hn::StoreU(r, du8q, rp + x);
                hn::StoreU(g, du8q, gp + x);
                hn::StoreU(b, du8q, bp + x);
        }
        for(; x < width; x++) {
                rp[x] = src[x * 4 + 0];
                gp[x] = src[x * 4 + 1];
                bp[x] = src[x * 4 + 2];
        }
        return;
}

}  // namespace HWY_NAMESPACE
}  // namespace csc
}  // namespace promeki
HWY_AFTER_NAMESPACE();

#endif
