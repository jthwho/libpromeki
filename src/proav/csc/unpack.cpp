/**
 * @file      unpack.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "src/proav/csc/unpack-inl.h"
#include "hwy/foreach_target.h"  // IWYU pragma: keep
#include "hwy/highway.h"
#include "src/proav/csc/unpack-inl.h"

#if HWY_ONCE

#include "csc_kernels.h"
#include <cstdint>
#include <cstring>

namespace promeki {
namespace csc {

HWY_EXPORT(UnpackInterleavedImpl);
HWY_EXPORT(UnpackPlanarImpl);
HWY_EXPORT(UnpackSemiPlanarImpl);

void unpackInterleaved(const void *src, float *const *buffers,
                       size_t width, int compCount, int bitsPerComp,
                       int bytesPerBlock, int pixelsPerBlock,
                       const int *compByteOffset, const int *compBits,
                       bool hasAlpha, int alphaCompIndex, bool useSimd) {
        HWY_DYNAMIC_DISPATCH(UnpackInterleavedImpl)(src, buffers, width,
                compCount, bitsPerComp, bytesPerBlock, pixelsPerBlock,
                compByteOffset, compBits, hasAlpha, alphaCompIndex, useSimd);
        return;
}

void unpackPlanar(const void *const *planes, const size_t *strides,
                  float *const *buffers, size_t width,
                  int compCount, int bitsPerComp,
                  const int *compPlane, const int *planeHSub,
                  const int *planeBytesPerSample,
                  bool hasAlpha, int alphaCompIndex, bool useSimd) {
        HWY_DYNAMIC_DISPATCH(UnpackPlanarImpl)(planes, strides, buffers, width,
                compCount, bitsPerComp, compPlane, planeHSub, planeBytesPerSample,
                hasAlpha, alphaCompIndex, useSimd);
        return;
}

void unpackSemiPlanar(const void *const *planes, const size_t *strides,
                      float *const *buffers, size_t width,
                      int bitsPerComp, const int *planeHSub,
                      const int *planeBytesPerSample,
                      bool hasAlpha, int alphaCompIndex,
                      const int *compByteOffset, bool useSimd) {
        // Default byte offsets: CbCr order (NV12)
        static const int defaultOffsets[3] = {0, 0, 1};
        const int *offsets = compByteOffset ? compByteOffset : defaultOffsets;
        HWY_DYNAMIC_DISPATCH(UnpackSemiPlanarImpl)(planes, strides, buffers, width,
                bitsPerComp, planeHSub, planeBytesPerSample,
                hasAlpha, alphaCompIndex, offsets, useSimd);
        return;
}

}  // namespace csc
}  // namespace promeki

#endif  // HWY_ONCE
