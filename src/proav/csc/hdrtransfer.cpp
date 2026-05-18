/**
 * @file      hdrtransfer.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "src/proav/csc/hdrtransfer-inl.h"
#include "hwy/foreach_target.h" // IWYU pragma: keep
#include "hwy/highway.h"
#include "src/proav/csc/hdrtransfer-inl.h"

#if HWY_ONCE

#include "csc_kernels.h"
#include <cmath>

namespace promeki {
        namespace csc {

                HWY_EXPORT(ApplyPqOETFImpl);
                HWY_EXPORT(ApplyPqEOTFImpl);
                HWY_EXPORT(ApplyHlgOETFImpl);
                HWY_EXPORT(ApplyHlgEOTFImpl);

                void applyPqOETF(float *buffer, size_t width, bool useSimd) {
                        HWY_DYNAMIC_DISPATCH(ApplyPqOETFImpl)(buffer, width, useSimd);
                }

                void applyPqEOTF(float *buffer, size_t width, bool useSimd) {
                        HWY_DYNAMIC_DISPATCH(ApplyPqEOTFImpl)(buffer, width, useSimd);
                }

                void applyHlgOETF(float *buffer, size_t width, bool useSimd) {
                        HWY_DYNAMIC_DISPATCH(ApplyHlgOETFImpl)(buffer, width, useSimd);
                }

                void applyHlgEOTF(float *buffer, size_t width, bool useSimd) {
                        HWY_DYNAMIC_DISPATCH(ApplyHlgEOTFImpl)(buffer, width, useSimd);
                }

        } // namespace csc
} // namespace promeki

#endif // HWY_ONCE
