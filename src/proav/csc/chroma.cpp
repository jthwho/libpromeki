/**
 * @file      chroma.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "src/proav/csc/chroma-inl.h"
#include "hwy/foreach_target.h"  // IWYU pragma: keep
#include "hwy/highway.h"
#include "src/proav/csc/chroma-inl.h"

#if HWY_ONCE

#include "csc_kernels.h"

namespace promeki {
namespace csc {

HWY_EXPORT(ChromaUpsampleHImpl);
HWY_EXPORT(ChromaDownsampleHImpl);

void chromaUpsampleH(const float *src, float *dst, size_t srcWidth, int ratio) {
        HWY_DYNAMIC_DISPATCH(ChromaUpsampleHImpl)(src, dst, srcWidth, ratio);
        return;
}

void chromaDownsampleH(const float *src, float *dst, size_t srcWidth, int ratio) {
        HWY_DYNAMIC_DISPATCH(ChromaDownsampleHImpl)(src, dst, srcWidth, ratio);
        return;
}

}  // namespace csc
}  // namespace promeki

#endif  // HWY_ONCE
