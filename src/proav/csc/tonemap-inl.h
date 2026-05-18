/**
 * @file      tonemap-inl.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Highway SIMD implementation of the ITU-R BT.2390-9 Annex B.2.5
 * Electro-Electrical Transfer Function (EETF), the recommended
 * perceptual tone-mapping operator for HDR PQ content displayed on
 * an SDR or lower-peak-luminance HDR target.  Operates entirely in
 * PQ-encoded space — values arrive after range-in (PQ-encoded in
 * [0,1]) and before EOTF, so the linear domain following EOTF is
 * already scaled to the target's peak luminance and the rest of
 * the pipeline (gamut, sRGB OETF) is free of HDR-range clipping.
 *
 * The curve is piecewise:
 *   - For input below the knee start KS: linear pass-through.
 *   - For input above KS: Hermite spline interpolation from
 *     (KS, KS) to (1, target_max) with zero derivative at the
 *     target.
 *
 * Per-channel application — simpler than BT.2390's maxRGB-preserving
 * variant but adequate for non-saturated content.  Hue shifts on
 * heavily-saturated highlights are the recognised tradeoff; the
 * maxRGB-preserving form can be layered on top later.
 *
 * Re-included per Highway target via @c foreach_target.h.
 */

#if defined(PROMEKI_CSC_TONEMAP_INL_H_) == defined(HWY_TARGET_TOGGLE)
#ifdef PROMEKI_CSC_TONEMAP_INL_H_
#undef PROMEKI_CSC_TONEMAP_INL_H_
#else
#define PROMEKI_CSC_TONEMAP_INL_H_
#endif

#include "hwy/highway.h"

HWY_BEFORE_NAMESPACE();
namespace promeki {
        namespace csc {
                namespace HWY_NAMESPACE {
                        namespace hn = hwy::HWY_NAMESPACE;

                        // BT.2390-9 §B.2.5: hermite-spline tone-map.
                        // @p srcMaxPq is the source's peak PQ value (e.g.
                        // 0.7518 for 1000 nits / 10000 nominal); @p dstMaxPq
                        // is the target's peak (e.g. 0.5081 for 100 nits).
                        // Input @p buffer values must be PQ-encoded in [0,1].
                        // The curve preserves black, leaves dark values
                        // linear up to the knee start KS, then smoothly
                        // attenuates to @p dstMaxPq at @p srcMaxPq.
                        //
                        // @c dstMaxPq >= @c srcMaxPq is treated as identity
                        // (target peak >= source peak means no compression
                        // needed); same when either value is non-positive.
                        void ApplyBt2390EETFImpl(float *buffer, size_t width, float srcMaxPq, float dstMaxPq,
                                                 bool useSimd) {
                                if (srcMaxPq <= 0.0f || dstMaxPq <= 0.0f || dstMaxPq >= srcMaxPq) {
                                        // Identity: HDR target >= source means
                                        // no compression; leave the encoded
                                        // PQ values alone.
                                        return;
                                }

                                // Normalize the target peak into the source's
                                // [0,1] space — the BT.2390 curve operates
                                // on E1 = input / srcMaxPq and produces
                                // output in [0, dstMaxPq / srcMaxPq], which
                                // we then rescale back to PQ-encoded space
                                // by multiplying by srcMaxPq.  All maths
                                // below stays in the normalized [0,1]
                                // domain so the constants (KS in particular)
                                // are independent of the absolute peak.
                                const float maxLum = dstMaxPq / srcMaxPq;
                                const float ks     = 1.5f * maxLum - 0.5f;
                                const float kKsRange = 1.0f - ks; // denominator in the spline

                                const hn::ScalableTag<float> df;
                                const auto                   vzero      = hn::Zero(df);
                                const auto                   vone       = hn::Set(df, 1.0f);
                                const auto                   vtwo       = hn::Set(df, 2.0f);
                                const auto                   vthree     = hn::Set(df, 3.0f);
                                const auto                   vmaxLum    = hn::Set(df, maxLum);
                                const auto                   vsrcMax    = hn::Set(df, srcMaxPq);
                                const auto                   vsrcMaxInv = hn::Set(df, 1.0f / srcMaxPq);
                                const auto                   vks        = hn::Set(df, ks);
                                const auto                   vksRangeInv= hn::Set(df, 1.0f / kKsRange);
                                const size_t                 N          = hn::Lanes(df);
                                size_t                       x          = 0;
#if HWY_TARGET != HWY_SCALAR && HWY_TARGET != HWY_EMU128
                                if (useSimd) {
                                        for (; x + N <= width; x += N) {
                                                auto       pq    = hn::LoadU(df, buffer + x);
                                                const auto safe  = hn::Max(pq, vzero);
                                                // Normalize PQ value into the
                                                // source's [0, 1] space.
                                                const auto e1    = hn::Mul(safe, vsrcMaxInv);
                                                // Low branch: pass-through.
                                                const auto lowBr = e1;
                                                // High branch: hermite spline.
                                                //   T = (E1 - KS) / (1 - KS)
                                                //   P(T) = (2T^3 - 3T^2 + 1) * KS
                                                //        + (T^3 - 2T^2 + T) * (1 - KS)
                                                //        + (-2T^3 + 3T^2) * maxLum
                                                const auto t    = hn::Mul(hn::Sub(e1, vks), vksRangeInv);
                                                const auto t2   = hn::Mul(t, t);
                                                const auto t3   = hn::Mul(t2, t);
                                                const auto h00  = hn::Add(hn::Sub(hn::Mul(vtwo, t3), hn::Mul(vthree, t2)), vone);
                                                const auto h10  = hn::Add(hn::Sub(t3, hn::Mul(vtwo, t2)), t);
                                                const auto h01  = hn::Add(hn::Mul(hn::Set(df, -2.0f), t3), hn::Mul(vthree, t2));
                                                const auto term1 = hn::Mul(h00, vks);
                                                const auto term2 = hn::Mul(h10, hn::Sub(vone, vks));
                                                const auto term3 = hn::Mul(h01, vmaxLum);
                                                const auto highBr = hn::Add(hn::Add(term1, term2), term3);
                                                // Pick branch by E1 vs KS.
                                                const auto useLow  = hn::Le(e1, vks);
                                                const auto e2      = hn::IfThenElse(useLow, lowBr, highBr);
                                                // Rescale back into PQ-encoded
                                                // domain and store.
                                                const auto outPq   = hn::Mul(e2, vsrcMax);
                                                hn::StoreU(outPq, df, buffer + x);
                                        }
                                }
#else
                                (void)N;
                                (void)useSimd;
                                (void)vzero;
                                (void)vone;
                                (void)vtwo;
                                (void)vthree;
                                (void)vmaxLum;
                                (void)vsrcMax;
                                (void)vsrcMaxInv;
                                (void)vks;
                                (void)vksRangeInv;
#endif
                                for (; x < width; ++x) {
                                        float pq = buffer[x];
                                        if (pq < 0.0f) pq = 0.0f;
                                        const float e1 = pq / srcMaxPq;
                                        float       e2;
                                        if (e1 <= ks) {
                                                e2 = e1;
                                        } else {
                                                const float t   = (e1 - ks) / kKsRange;
                                                const float t2  = t * t;
                                                const float t3  = t2 * t;
                                                const float h00 = 2.0f * t3 - 3.0f * t2 + 1.0f;
                                                const float h10 = t3 - 2.0f * t2 + t;
                                                const float h01 = -2.0f * t3 + 3.0f * t2;
                                                e2 = h00 * ks + h10 * (1.0f - ks) + h01 * maxLum;
                                        }
                                        buffer[x] = e2 * srcMaxPq;
                                }
                        }

                } // namespace HWY_NAMESPACE
        } // namespace csc
} // namespace promeki
HWY_AFTER_NAMESPACE();

#endif
