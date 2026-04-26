/**
 * @file      cscregistry.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/pixelformat.h>

PROMEKI_NAMESPACE_BEGIN

class CSCContext;

/**
 * @brief Static registry for fast-path CSC conversion kernels.
 * @ingroup proav
 *
 * Maps (source PixelFormat, target PixelFormat) pairs to hand-tuned
 * conversion functions that bypass the general pipeline. Register
 * fast paths at static-init time; they are discovered automatically
 * by CSCPipeline during compilation.
 *
 * @par Thread Safety
 * Fully thread-safe.  Registrations are expected at static-init time
 * and the registry is internally synchronized; thereafter @c lookup is
 * lock-free.
 *
 * @see CSCPipeline
 */
class CSCRegistry {
        public:
                /**
                 * @brief Function signature for a fast-path line conversion.
                 *
                 * Converts a single scanline from source to target format.
                 *
                 * @param srcPlanes  Array of source plane pointers for this line.
                 * @param srcStrides Array of source line strides in bytes.
                 * @param dstPlanes  Array of target plane pointers for this line.
                 * @param dstStrides Array of target line strides in bytes.
                 * @param width      Pixel width of the line.
                 * @param ctx        Scratch context for temporary buffers.
                 */
                using LineFuncPtr = void (*)(const void *const *srcPlanes,
                                             const size_t *srcStrides,
                                             void *const *dstPlanes,
                                             const size_t *dstStrides,
                                             size_t width,
                                             CSCContext &ctx);

                /**
                 * @brief Registers a fast-path line kernel for a source/target pair.
                 * @param src  Source pixel description.
                 * @param dst  Target pixel description.
                 * @param func The line conversion function.
                 */
                static void registerFastPath(const PixelFormat &src, const PixelFormat &dst, LineFuncPtr func);

                /**
                 * @brief Looks up a registered fast-path for the given pair.
                 * @param src Source pixel description.
                 * @param dst Target pixel description.
                 * @return The registered function, or nullptr if none registered.
                 */
                static LineFuncPtr lookupFastPath(const PixelFormat &src, const PixelFormat &dst);

        private:
                CSCRegistry() = delete;
};

PROMEKI_NAMESPACE_END
