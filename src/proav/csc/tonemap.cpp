/**
 * @file      tonemap.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "src/proav/csc/tonemap-inl.h"
#include "hwy/foreach_target.h" // IWYU pragma: keep
#include "hwy/highway.h"
#include "src/proav/csc/tonemap-inl.h"

#if HWY_ONCE

#include "csc_kernels.h"

namespace promeki {
        namespace csc {

                HWY_EXPORT(ApplyBt2390EETFImpl);

                void applyBt2390EETF(float *buffer, size_t width, float srcMaxPq, float dstMaxPq, bool useSimd) {
                        HWY_DYNAMIC_DISPATCH(ApplyBt2390EETFImpl)(buffer, width, srcMaxPq, dstMaxPq, useSimd);
                }

        } // namespace csc
} // namespace promeki

#endif // HWY_ONCE
