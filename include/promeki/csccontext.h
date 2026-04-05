/**
 * @file      csccontext.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/sharedptr.h>
#include <promeki/buffer.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Per-invocation scratch space for CSC pipeline execution.
 * @ingroup proav
 *
 * Holds Highway-aligned SoA (Structure-of-Arrays) float buffers used as
 * intermediate storage by CSCPipeline stages. Each buffer holds one
 * component's worth of float data for a single scanline.
 *
 * CSCContext is not thread-safe. Each thread executing a conversion
 * should have its own CSCContext instance.
 *
 * @par Example
 * @code
 * CSCContext ctx(1920);
 * pipeline.processLine(srcPlanes, srcStrides, dstPlanes, dstStrides,
 *                      1920, y, ctx);
 * @endcode
 *
 * @see CSCPipeline
 */
class CSCContext {
        PROMEKI_SHARED_FINAL(CSCContext)
        public:
                /** @brief Shared pointer type for CSCContext. */
                using Ptr = SharedPtr<CSCContext>;

                /** @brief Number of internal float buffers (one per component). */
                static constexpr int BufferCount = 8;

                /** @brief Alignment for internal buffers (matches Highway max lane width). */
                static constexpr size_t BufferAlign = 128;

                /** @brief Constructs an invalid context. */
                CSCContext() = default;

                /**
                 * @brief Constructs a context sized for the given maximum line width.
                 * @param maxWidth Maximum number of pixels per scanline.
                 */
                explicit CSCContext(size_t maxWidth);

                /**
                 * @brief Returns true if the context has been allocated.
                 * @return true if buffers are valid.
                 */
                bool isValid() const { return _maxWidth > 0; }

                /**
                 * @brief Returns the maximum width this context supports.
                 * @return The maximum pixel width.
                 */
                size_t maxWidth() const { return _maxWidth; }

                /**
                 * @brief Returns a pointer to the specified float buffer.
                 * @param index Buffer index (0 to BufferCount-1).
                 * @return Aligned float pointer, or nullptr if invalid.
                 */
                float *buffer(int index) const;

        private:
                size_t          _maxWidth = 0;
                Buffer::Ptr     _buffers[BufferCount];
};

PROMEKI_NAMESPACE_END
