/**
 * @file      matrix-inl.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Highway SIMD implementation of 3x3 matrix multiply for CSC pipeline.
 * Re-included per target via foreach_target.h.
 */

// Per-target include guard (Highway pattern)
#if defined(PROMEKI_CSC_MATRIX_INL_H_) == defined(HWY_TARGET_TOGGLE)
#ifdef PROMEKI_CSC_MATRIX_INL_H_
#undef PROMEKI_CSC_MATRIX_INL_H_
#else
#define PROMEKI_CSC_MATRIX_INL_H_
#endif

#include "hwy/highway.h"

HWY_BEFORE_NAMESPACE();
namespace promeki {
namespace csc {
namespace HWY_NAMESPACE {
namespace hn = hwy::HWY_NAMESPACE;

void MatrixMultiply3x3Impl(float *buf0, float *buf1, float *buf2,
                           size_t width,
                           const float matrix[3][3],
                           const float preOffset[3],
                           const float postOffset[3], bool useSimd) {
        const hn::ScalableTag<float> df;
        const size_t N = useSimd ? hn::Lanes(df) : width + 1;

        // Broadcast matrix coefficients and offsets
        const auto m00 = hn::Set(df, matrix[0][0]);
        const auto m01 = hn::Set(df, matrix[0][1]);
        const auto m02 = hn::Set(df, matrix[0][2]);
        const auto m10 = hn::Set(df, matrix[1][0]);
        const auto m11 = hn::Set(df, matrix[1][1]);
        const auto m12 = hn::Set(df, matrix[1][2]);
        const auto m20 = hn::Set(df, matrix[2][0]);
        const auto m21 = hn::Set(df, matrix[2][1]);
        const auto m22 = hn::Set(df, matrix[2][2]);

        const auto pre0 = hn::Set(df, preOffset[0]);
        const auto pre1 = hn::Set(df, preOffset[1]);
        const auto pre2 = hn::Set(df, preOffset[2]);
        const auto post0 = hn::Set(df, postOffset[0]);
        const auto post1 = hn::Set(df, postOffset[1]);
        const auto post2 = hn::Set(df, postOffset[2]);

        size_t x = 0;
        for(; x + N <= width; x += N) {
                auto s0 = hn::Add(hn::LoadU(df, buf0 + x), pre0);
                auto s1 = hn::Add(hn::LoadU(df, buf1 + x), pre1);
                auto s2 = hn::Add(hn::LoadU(df, buf2 + x), pre2);

                // d0 = m00*s0 + m01*s1 + m02*s2 + post0
                auto d0 = hn::MulAdd(m00, s0, hn::MulAdd(m01, s1, hn::MulAdd(m02, s2, post0)));
                auto d1 = hn::MulAdd(m10, s0, hn::MulAdd(m11, s1, hn::MulAdd(m12, s2, post1)));
                auto d2 = hn::MulAdd(m20, s0, hn::MulAdd(m21, s1, hn::MulAdd(m22, s2, post2)));

                hn::StoreU(d0, df, buf0 + x);
                hn::StoreU(d1, df, buf1 + x);
                hn::StoreU(d2, df, buf2 + x);
        }

        // Scalar tail
        for(; x < width; x++) {
                float s0 = buf0[x] + preOffset[0];
                float s1 = buf1[x] + preOffset[1];
                float s2 = buf2[x] + preOffset[2];

                buf0[x] = matrix[0][0]*s0 + matrix[0][1]*s1 + matrix[0][2]*s2 + postOffset[0];
                buf1[x] = matrix[1][0]*s0 + matrix[1][1]*s1 + matrix[1][2]*s2 + postOffset[1];
                buf2[x] = matrix[2][0]*s0 + matrix[2][1]*s1 + matrix[2][2]*s2 + postOffset[2];
        }
        return;
}

}  // namespace HWY_NAMESPACE
}  // namespace csc
}  // namespace promeki
HWY_AFTER_NAMESPACE();

#endif  // include guard
