/**
 * @file      hdrtransfer-inl.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Highway SIMD direct-compute implementations of the SMPTE ST 2084
 * (PQ) and ITU-R BT.2100 (HLG) transfer functions, used by CSC
 * pipelines that operate on float buffers in linear or normalized
 * scene-referred space.  The LUT-based @ref applyLUT path clamps
 * its index to [0,1], which is correct for SDR (display-referred
 * 0..1) but silently collapses any scene-referred float input above
 * 1.0 to the LUT's last sample — destroying HDR highlight data.
 * The direct-compute kernels here keep the PQ / HLG curves analytic
 * across the entire non-negative real range so scene-referred
 * float HDR inputs survive the transfer stage intact.
 *
 * Re-included per Highway target via @c foreach_target.h.
 */

#if defined(PROMEKI_CSC_HDRTRANSFER_INL_H_) == defined(HWY_TARGET_TOGGLE)
#ifdef PROMEKI_CSC_HDRTRANSFER_INL_H_
#undef PROMEKI_CSC_HDRTRANSFER_INL_H_
#else
#define PROMEKI_CSC_HDRTRANSFER_INL_H_
#endif

#include "hwy/contrib/math/math-inl.h"
#include "hwy/highway.h"

#include <cmath>

HWY_BEFORE_NAMESPACE();
namespace promeki {
        namespace csc {
                namespace HWY_NAMESPACE {
                        namespace hn = hwy::HWY_NAMESPACE;

                        // SMPTE ST 2084 PQ constants — the perceptual quantizer
                        // curve as defined in BT.2100 Table 4.  Values are
                        // exact rationals expressed as decimal fractions; the
                        // analytic curve is well-defined for any non-negative
                        // normalized linear input where 1.0 == 10000 nits.
                        static constexpr float kPq_M1 = 0.1593017578125f;
                        static constexpr float kPq_M2 = 78.84375f;
                        static constexpr float kPq_C1 = 0.8359375f;
                        static constexpr float kPq_C2 = 18.8515625f;
                        static constexpr float kPq_C3 = 18.6875f;

                        // ITU-R BT.2100 HLG (Hybrid Log-Gamma) constants.
                        // The HLG OETF is piecewise: square-root for the lower
                        // half of the input range, log for the upper half.
                        // We follow the BT.2100 normalized form where input
                        // 1.0 corresponds to peak display white at the system
                        // gamma's reference brightness.
                        static constexpr float kHlg_A    = 0.17883277f;
                        static constexpr float kHlg_B    = 0.28466892f; // 1 - 4 * a
                        static constexpr float kHlg_C    = 0.55991073f; // 0.5 - a * ln(4 * a)

                        // Vector pow(x, n) via Exp(n * Log(x)).  Highway has
                        // no direct Pow primitive on every target; this
                        // composition is what hwy's own implementations
                        // collapse to for non-special exponents.  Caller is
                        // responsible for masking x <= 0 since Log(0) is
                        // undefined.
                        template <class D, class V>
                        HWY_INLINE V VecPow(D d, V x, V n) {
                                return hn::Exp(d, hn::Mul(n, hn::Log(d, x)));
                        }

                        // PQ OETF (linear scene-referred normalized → encoded).
                        // Vectorized form of @c ColorModel::transferPQ.  In-place
                        // on @p buffer of length @p width.  Inputs below zero
                        // round up to zero (PQ is undefined there); inputs above
                        // 1.0 are honored — they encode > 10000 nits, which the
                        // analytic curve still represents.  Caller must clamp
                        // to a legal range afterward if a bounded encoded
                        // result is required.
                        void ApplyPqOETFImpl(float *buffer, size_t width, bool useSimd) {
                                const hn::ScalableTag<float> df;
                                const auto                   vzero = hn::Zero(df);
                                const auto                   vone  = hn::Set(df, 1.0f);
                                const auto                   vm1   = hn::Set(df, kPq_M1);
                                const auto                   vm2   = hn::Set(df, kPq_M2);
                                const auto                   vc1   = hn::Set(df, kPq_C1);
                                const auto                   vc2   = hn::Set(df, kPq_C2);
                                const auto                   vc3   = hn::Set(df, kPq_C3);
                                const size_t                 N     = hn::Lanes(df);
                                size_t                       x     = 0;
#if HWY_TARGET != HWY_SCALAR && HWY_TARGET != HWY_EMU128
                                if (useSimd) {
                                        for (; x + N <= width; x += N) {
                                                auto       v       = hn::LoadU(df, buffer + x);
                                                const auto safe    = hn::Max(v, vzero);
                                                // y_m1 = pow(safe, m1), guarding
                                                // log(0) with a one-substitution
                                                // mask so the math is finite
                                                // even when safe == 0.
                                                const auto guard   = hn::IfThenElse(hn::Eq(safe, vzero), vone, safe);
                                                const auto y_m1g   = VecPow(df, guard, vm1);
                                                const auto y_m1    = hn::IfThenElse(hn::Eq(safe, vzero), vzero, y_m1g);
                                                const auto num     = hn::MulAdd(y_m1, vc2, vc1);
                                                const auto den     = hn::MulAdd(y_m1, vc3, vone);
                                                const auto ratio   = hn::Div(num, den);
                                                const auto out     = VecPow(df, ratio, vm2);
                                                hn::StoreU(out, df, buffer + x);
                                        }
                                }
#else
                                (void)N;
                                (void)useSimd;
                                (void)vzero;
                                (void)vone;
                                (void)vc1;
                                (void)vc2;
                                (void)vc3;
#endif
                                for (; x < width; ++x) {
                                        float v = buffer[x];
                                        if (v <= 0.0f) {
                                                buffer[x] = 0.0f;
                                                continue;
                                        }
                                        const float ym1 = std::pow(v, kPq_M1);
                                        buffer[x] = std::pow((kPq_C1 + kPq_C2 * ym1) / (1.0f + kPq_C3 * ym1), kPq_M2);
                                }
                        }

                        // PQ EOTF (encoded → linear scene-referred normalized).
                        // Vectorized form of @c ColorModel::invTransferPQ.
                        // Numerator can go negative when the input is just
                        // below zero — clamp to zero before the final pow so
                        // the result stays in the analytic domain.
                        void ApplyPqEOTFImpl(float *buffer, size_t width, bool useSimd) {
                                const hn::ScalableTag<float> df;
                                const auto                   vzero    = hn::Zero(df);
                                const auto                   vone     = hn::Set(df, 1.0f);
                                const auto                   vinvm1   = hn::Set(df, 1.0f / kPq_M1);
                                const auto                   vinvm2   = hn::Set(df, 1.0f / kPq_M2);
                                const auto                   vc1      = hn::Set(df, kPq_C1);
                                const auto                   vc2      = hn::Set(df, kPq_C2);
                                const auto                   vc3      = hn::Set(df, kPq_C3);
                                const size_t                 N        = hn::Lanes(df);
                                size_t                       x        = 0;
#if HWY_TARGET != HWY_SCALAR && HWY_TARGET != HWY_EMU128
                                if (useSimd) {
                                        for (; x + N <= width; x += N) {
                                                auto       v       = hn::LoadU(df, buffer + x);
                                                const auto safe    = hn::Max(v, vzero);
                                                const auto guard   = hn::IfThenElse(hn::Eq(safe, vzero), vone, safe);
                                                const auto e_inv2  = VecPow(df, guard, vinvm2);
                                                const auto numRaw  = hn::Sub(e_inv2, vc1);
                                                const auto num     = hn::Max(numRaw, vzero);
                                                const auto denRaw  = hn::NegMulAdd(vc3, e_inv2, vc2);
                                                // Avoid division by zero / negative when den ≤ 0; the EOTF
                                                // analytically converges to +∞ as the input approaches the
                                                // singular point — saturate to a finite vone so downstream
                                                // SoA math sees a sane value.
                                                const auto denOk   = hn::Gt(denRaw, vzero);
                                                const auto den     = hn::IfThenElse(denOk, denRaw, vone);
                                                const auto ratio   = hn::Div(num, den);
                                                const auto outRaw  = VecPow(df, ratio, vinvm1);
                                                const auto out     = hn::IfThenElse(hn::Eq(safe, vzero), vzero, outRaw);
                                                hn::StoreU(out, df, buffer + x);
                                        }
                                }
#else
                                (void)N;
                                (void)useSimd;
                                (void)vzero;
                                (void)vone;
                                (void)vc1;
                                (void)vc2;
                                (void)vc3;
#endif
                                for (; x < width; ++x) {
                                        float v = buffer[x];
                                        if (v <= 0.0f) {
                                                buffer[x] = 0.0f;
                                                continue;
                                        }
                                        const float eInvm2 = std::pow(v, 1.0f / kPq_M2);
                                        const float num    = std::max(eInvm2 - kPq_C1, 0.0f);
                                        const float den    = kPq_C2 - kPq_C3 * eInvm2;
                                        if (den <= 0.0f) {
                                                buffer[x] = 1.0f;
                                                continue;
                                        }
                                        buffer[x] = std::pow(num / den, 1.0f / kPq_M1);
                                }
                        }

                        // HLG OETF (linear scene-referred normalized → encoded).
                        // ITU-R BT.2100 Table 5: piecewise
                        //   y = sqrt(3 * v)              for 0 <= v <= 1/12
                        //   y = a * ln(12 * v - b) + c   for v > 1/12
                        // where (a, b, c) = (0.17883277, 0.28466892,
                        // 0.55991073).  The threshold v = 1/12 maps to
                        // encoded y = 0.5, providing a smooth join.
                        // Both branches are SIMD-friendly via blend mask.
                        void ApplyHlgOETFImpl(float *buffer, size_t width, bool useSimd) {
                                const hn::ScalableTag<float> df;
                                const auto                   vzero    = hn::Zero(df);
                                const auto                   vone     = hn::Set(df, 1.0f);
                                const auto                   vthresh  = hn::Set(df, 1.0f / 12.0f);
                                const auto                   vthree   = hn::Set(df, 3.0f);
                                const auto                   vtwelve  = hn::Set(df, 12.0f);
                                const auto                   va       = hn::Set(df, kHlg_A);
                                const auto                   vb       = hn::Set(df, kHlg_B);
                                const auto                   vc       = hn::Set(df, kHlg_C);
                                const size_t                 N        = hn::Lanes(df);
                                size_t                       x        = 0;
#if HWY_TARGET != HWY_SCALAR && HWY_TARGET != HWY_EMU128
                                if (useSimd) {
                                        for (; x + N <= width; x += N) {
                                                auto       v       = hn::LoadU(df, buffer + x);
                                                const auto safe    = hn::Max(v, vzero);
                                                const auto lowBr   = hn::Sqrt(hn::Mul(vthree, safe));
                                                // High branch: a * Log(12*v - b) + c.
                                                // Clamp the log argument to >= 1 so
                                                // the SIMD branch is well-defined
                                                // on lanes the blend later discards
                                                // (low-branch lanes).
                                                const auto logArg  = hn::Max(hn::Sub(hn::Mul(vtwelve, safe), vb), vone);
                                                const auto highBr  = hn::MulAdd(va, hn::Log(df, logArg), vc);
                                                const auto useLow  = hn::Le(safe, vthresh);
                                                const auto out     = hn::IfThenElse(useLow, lowBr, highBr);
                                                hn::StoreU(out, df, buffer + x);
                                        }
                                }
#else
                                (void)N;
                                (void)useSimd;
                                (void)vone;
                                (void)vthresh;
                                (void)vthree;
                                (void)vtwelve;
                                (void)va;
                                (void)vb;
                                (void)vc;
#endif
                                for (; x < width; ++x) {
                                        float v = buffer[x];
                                        if (v <= 0.0f) {
                                                buffer[x] = 0.0f;
                                                continue;
                                        }
                                        if (v <= 1.0f / 12.0f) {
                                                buffer[x] = std::sqrt(3.0f * v);
                                        } else {
                                                buffer[x] = kHlg_A * std::log(12.0f * v - kHlg_B) + kHlg_C;
                                        }
                                }
                        }

                        // HLG EOTF (encoded → linear scene-referred normalized).
                        // Inverse of the OETF piecewise:
                        //   v = y^2 / 3                      for 0 <= y <= 0.5
                        //   v = (exp((y - c) / a) + b) / 12  for y > 0.5
                        // The encoded threshold 0.5 is the image of the OETF
                        // threshold v = 1/12.
                        void ApplyHlgEOTFImpl(float *buffer, size_t width, bool useSimd) {
                                const hn::ScalableTag<float> df;
                                const auto                   vzero    = hn::Zero(df);
                                const auto                   vthreeInv= hn::Set(df, 1.0f / 3.0f);
                                const auto                   vtwelveInv = hn::Set(df, 1.0f / 12.0f);
                                const auto                   vhalf    = hn::Set(df, 0.5f);
                                const auto                   vb       = hn::Set(df, kHlg_B);
                                const auto                   vc       = hn::Set(df, kHlg_C);
                                const auto                   vaInv    = hn::Set(df, 1.0f / kHlg_A);
                                const size_t                 N        = hn::Lanes(df);
                                size_t                       x        = 0;
#if HWY_TARGET != HWY_SCALAR && HWY_TARGET != HWY_EMU128
                                if (useSimd) {
                                        for (; x + N <= width; x += N) {
                                                auto       v       = hn::LoadU(df, buffer + x);
                                                const auto safe    = hn::Max(v, vzero);
                                                // Low branch: v^2 / 3
                                                const auto v2      = hn::Mul(safe, safe);
                                                const auto lowBr   = hn::Mul(v2, vthreeInv);
                                                // High branch: (Exp((v - c) / a) + b) / 12
                                                const auto t       = hn::Mul(hn::Sub(safe, vc), vaInv);
                                                const auto highBr  = hn::Mul(hn::Add(hn::Exp(df, t), vb), vtwelveInv);
                                                const auto useLow  = hn::Le(safe, vhalf);
                                                const auto out     = hn::IfThenElse(useLow, lowBr, highBr);
                                                hn::StoreU(out, df, buffer + x);
                                        }
                                }
#else
                                (void)N;
                                (void)useSimd;
                                (void)vthreeInv;
                                (void)vtwelveInv;
                                (void)vhalf;
                                (void)vb;
                                (void)vc;
                                (void)vaInv;
#endif
                                for (; x < width; ++x) {
                                        float v = buffer[x];
                                        if (v <= 0.0f) {
                                                buffer[x] = 0.0f;
                                                continue;
                                        }
                                        if (v <= 0.5f) {
                                                buffer[x] = v * v / 3.0f;
                                        } else {
                                                buffer[x] = (std::exp((v - kHlg_C) / kHlg_A) + kHlg_B) / 12.0f;
                                        }
                                }
                        }

                } // namespace HWY_NAMESPACE
        } // namespace csc
} // namespace promeki
HWY_AFTER_NAMESPACE();

#endif
