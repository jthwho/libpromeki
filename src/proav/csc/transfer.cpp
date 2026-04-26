/**
 * @file      transfer.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "src/proav/csc/transfer-inl.h"
#include "hwy/foreach_target.h" // IWYU pragma: keep
#include "hwy/highway.h"
#include "src/proav/csc/transfer-inl.h"

#if HWY_ONCE

#include "csc_kernels.h"
#include <cmath>

namespace promeki {
        namespace csc {

                HWY_EXPORT(ApplyLUTImpl);

                void applyLUT(float *buffer, size_t width, const float *lut, size_t lutSize, bool useSimd) {
                        HWY_DYNAMIC_DISPATCH(ApplyLUTImpl)(buffer, width, lut, lutSize, useSimd);
                        return;
                }

                void applyTransferFunc(float *buffer, size_t width, double (*func)(double)) {
                        for (size_t x = 0; x < width; x++) {
                                buffer[x] = static_cast<float>(func(static_cast<double>(buffer[x])));
                        }
                        return;
                }

        } // namespace csc
} // namespace promeki

#endif // HWY_ONCE
