/**
 * @file      chroma-inl.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Chroma resampling kernels. Currently scalar; SIMD optimization is
 * deferred to Phase 3 since chroma resampling is not a hot path for
 * most interleaved format conversions.
 * Re-included per target via foreach_target.h.
 */

#if defined(PROMEKI_CSC_CHROMA_INL_H_) == defined(HWY_TARGET_TOGGLE)
#ifdef PROMEKI_CSC_CHROMA_INL_H_
#undef PROMEKI_CSC_CHROMA_INL_H_
#else
#define PROMEKI_CSC_CHROMA_INL_H_
#endif

#include "hwy/highway.h"

HWY_BEFORE_NAMESPACE();
namespace promeki {
        namespace csc {
                namespace HWY_NAMESPACE {

                        void ChromaUpsampleHImpl(const float *src, float *dst, size_t srcWidth, int ratio) {
                                if (ratio == 1) {
                                        for (size_t x = 0; x < srcWidth; x++) dst[x] = src[x];
                                        return;
                                }
                                size_t dstWidth = srcWidth * ratio;
                                for (size_t dx = 0; dx < dstWidth; dx++) {
                                        float srcPos = (static_cast<float>(dx) + 0.5f) / ratio - 0.5f;
                                        int   sx0 = static_cast<int>(srcPos);
                                        if (sx0 < 0) sx0 = 0;
                                        int sx1 = sx0 + 1;
                                        if (static_cast<size_t>(sx1) >= srcWidth) sx1 = static_cast<int>(srcWidth - 1);
                                        float frac = srcPos - sx0;
                                        if (frac < 0.0f) frac = 0.0f;
                                        dst[dx] = src[sx0] * (1.0f - frac) + src[sx1] * frac;
                                }
                                return;
                        }

                        void ChromaDownsampleHImpl(const float *src, float *dst, size_t srcWidth, int ratio) {
                                if (ratio == 1) {
                                        for (size_t x = 0; x < srcWidth; x++) dst[x] = src[x];
                                        return;
                                }
                                size_t dstWidth = (srcWidth + ratio - 1) / ratio;
                                for (size_t dx = 0; dx < dstWidth; dx++) {
                                        float sum = 0.0f;
                                        int   count = 0;
                                        for (int i = 0; i < ratio; i++) {
                                                size_t sx = dx * ratio + i;
                                                if (sx < srcWidth) {
                                                        sum += src[sx];
                                                        count++;
                                                }
                                        }
                                        dst[dx] = (count > 0) ? sum / count : 0.0f;
                                }
                                return;
                        }

                } // namespace HWY_NAMESPACE
        } // namespace csc
} // namespace promeki
HWY_AFTER_NAMESPACE();

#endif
