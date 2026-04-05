/**
 * @file      pack.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "src/proav/csc/pack-inl.h"
#include "hwy/foreach_target.h"  // IWYU pragma: keep
#include "hwy/highway.h"
#include "src/proav/csc/pack-inl.h"

#if HWY_ONCE

#include "csc_kernels.h"
#include <cstdint>
#include <cstring>
#include <algorithm>

namespace promeki {
namespace csc {

HWY_EXPORT(PackInterleavedImpl);
HWY_EXPORT(PackPlanarImpl);
HWY_EXPORT(PackSemiPlanarImpl);

void packInterleaved(const float *const *buffers, void *dst,
                     size_t width, int compCount, int bitsPerComp,
                     int bytesPerBlock, int pixelsPerBlock,
                     const int *compByteOffset, const int *compBits,
                     bool hasAlpha, int alphaCompIndex, bool useSimd) {
        HWY_DYNAMIC_DISPATCH(PackInterleavedImpl)(buffers, dst, width,
                compCount, bitsPerComp, bytesPerBlock, pixelsPerBlock,
                compByteOffset, compBits, hasAlpha, alphaCompIndex, useSimd);
        return;
}

void packPlanar(const float *const *buffers, void *const *planes,
                const size_t *strides, size_t width,
                int compCount, int bitsPerComp,
                const int *compPlane, const int *planeHSub,
                const int *planeBytesPerSample,
                bool hasAlpha, int alphaCompIndex, bool useSimd) {
        HWY_DYNAMIC_DISPATCH(PackPlanarImpl)(buffers, planes, strides, width,
                compCount, bitsPerComp, compPlane, planeHSub, planeBytesPerSample,
                hasAlpha, alphaCompIndex, useSimd);
        return;
}

void packSemiPlanar(const float *const *buffers, void *const *planes,
                    const size_t *strides, size_t width,
                    int bitsPerComp, const int *planeHSub,
                    const int *planeBytesPerSample,
                    bool hasAlpha, int alphaCompIndex,
                    const int *compByteOffset, bool useSimd) {
        static const int defaultOffsets[3] = {0, 0, 1};
        const int *offsets = compByteOffset ? compByteOffset : defaultOffsets;
        HWY_DYNAMIC_DISPATCH(PackSemiPlanarImpl)(buffers, planes, strides, width,
                bitsPerComp, planeHSub, planeBytesPerSample,
                hasAlpha, alphaCompIndex, offsets, useSimd);
        return;
}

}  // namespace csc
}  // namespace promeki

#endif  // HWY_ONCE
