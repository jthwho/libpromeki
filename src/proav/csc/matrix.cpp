/**
 * @file      matrix.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "src/proav/csc/matrix-inl.h"
#include "hwy/foreach_target.h" // IWYU pragma: keep
#include "hwy/highway.h"
#include "src/proav/csc/matrix-inl.h"

#if HWY_ONCE

#include "csc_kernels.h"

namespace promeki {
        namespace csc {

                HWY_EXPORT(MatrixMultiply3x3Impl);

                void matrixMultiply3x3(float *buf0, float *buf1, float *buf2, size_t width, const float matrix[3][3],
                                       const float preOffset[3], const float postOffset[3], bool useSimd) {
                        HWY_DYNAMIC_DISPATCH(MatrixMultiply3x3Impl)(buf0, buf1, buf2, width, matrix, preOffset,
                                                                    postOffset, useSimd);
                        return;
                }

        } // namespace csc
} // namespace promeki

#endif // HWY_ONCE
