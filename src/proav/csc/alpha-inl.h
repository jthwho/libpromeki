/**
 * @file      alpha-inl.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Highway SIMD implementation of alpha fill and range mapping.
 * Re-included per target via foreach_target.h.
 */

#if defined(PROMEKI_CSC_ALPHA_INL_H_) == defined(HWY_TARGET_TOGGLE)
#ifdef PROMEKI_CSC_ALPHA_INL_H_
#undef PROMEKI_CSC_ALPHA_INL_H_
#else
#define PROMEKI_CSC_ALPHA_INL_H_
#endif

#include "hwy/highway.h"

HWY_BEFORE_NAMESPACE();
namespace promeki {
        namespace csc {
                namespace HWY_NAMESPACE {
                        namespace hn = hwy::HWY_NAMESPACE;

                        void AlphaFillImpl(float *buffer, size_t width, float value, bool useSimd) {
                                const hn::ScalableTag<float> df;
                                const size_t                 N = useSimd ? hn::Lanes(df) : width + 1;
                                const auto                   vval = hn::Set(df, value);

                                size_t x = 0;
                                for (; x + N <= width; x += N) {
                                        hn::StoreU(vval, df, buffer + x);
                                }
                                for (; x < width; x++) {
                                        buffer[x] = value;
                                }
                                return;
                        }

                        void RangeMapImpl(float *const *buffers, size_t width, int compCount, const float *scale,
                                          const float *bias, bool useSimd) {
                                const hn::ScalableTag<float> df;
                                const size_t                 N = useSimd ? hn::Lanes(df) : width + 1;

                                for (int c = 0; c < compCount; c++) {
                                        const auto vs = hn::Set(df, scale[c]);
                                        const auto vb = hn::Set(df, bias[c]);
                                        float     *buf = buffers[c];

                                        size_t x = 0;
                                        for (; x + N <= width; x += N) {
                                                auto v = hn::LoadU(df, buf + x);
                                                v = hn::MulAdd(v, vs, vb);
                                                hn::StoreU(v, df, buf + x);
                                        }
                                        for (; x < width; x++) {
                                                buf[x] = buf[x] * scale[c] + bias[c];
                                        }
                                }
                                return;
                        }

                } // namespace HWY_NAMESPACE
        } // namespace csc
} // namespace promeki
HWY_AFTER_NAMESPACE();

#endif
