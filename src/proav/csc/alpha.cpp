/**
 * @file      alpha.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "src/proav/csc/alpha-inl.h"
#include "hwy/foreach_target.h"  // IWYU pragma: keep
#include "hwy/highway.h"
#include "src/proav/csc/alpha-inl.h"

#if HWY_ONCE

#include "csc_kernels.h"

namespace promeki {
namespace csc {

HWY_EXPORT(AlphaFillImpl);
HWY_EXPORT(RangeMapImpl);

void alphaFill(float *buffer, size_t width, float value, bool useSimd) {
        HWY_DYNAMIC_DISPATCH(AlphaFillImpl)(buffer, width, value, useSimd);
        return;
}

void rangeMap(float *const *buffers, size_t width, int compCount,
              const float *scale, const float *bias, bool useSimd) {
        HWY_DYNAMIC_DISPATCH(RangeMapImpl)(buffers, width, compCount, scale, bias, useSimd);
        return;
}

}  // namespace csc
}  // namespace promeki

#endif  // HWY_ONCE
