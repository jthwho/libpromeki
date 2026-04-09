/**
 * @file      bufferpool.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstddef>
#include <promeki/namespace.h>
#include <promeki/buffer.h>
#include <promeki/list.h>
#include <promeki/memspace.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Simple fixed-geometry buffer pool for hot-path allocations.
 * @ingroup util
 *
 * Maintains a small list of pre-allocated @c Buffer objects of a fixed
 * size + alignment, handing them out via @c acquire() and receiving
 * them back via @c release(). Designed for realtime read/write loops
 * that repeatedly need a scratch buffer of the same shape — e.g. a
 * video reader pulling one uncompressed frame per vsync.
 *
 * The pool is single-threaded — callers are responsible for ensuring
 * acquire/release happen from the same thread (or are externally
 * synchronized).
 *
 * @par Design notes
 *
 * This is a simple explicit-release pool, not a smart-pointer-backed
 * one. That's deliberate: @c Buffer::Ptr (SharedPtr<Buffer>) doesn't
 * have a user-defined deleter hook, so wrapping a pool-backed buffer
 * in a shared pointer would require a separate layer. For the
 * expected hot-path use case (read one sample, use it, pop the next),
 * explicit release is clearer.
 *
 * @par Growth
 *
 * When @c acquire() is called and the pool is empty, a new buffer is
 * allocated. The pool never shrinks — returned buffers stay in the
 * free list until the pool is destroyed. If you're worried about
 * memory footprint, call @c clear() periodically.
 *
 * @par Example
 * @code
 * BufferPool pool(imageBytes, 4096);  // page-aligned, 4K tall stack
 * pool.reserve(4);
 *
 * for(uint64_t i = 0; i < frameCount; ++i) {
 *     Buffer buf = pool.acquire();       // O(1), no malloc if warmed
 *     file.readBulk(buf, imageBytes);
 *     // ... use buf ...
 *     pool.release(std::move(buf));
 * }
 * @endcode
 */
class BufferPool {
        public:
                /** @brief Default-constructs an invalid pool with no geometry. */
                BufferPool() = default;

                /**
                 * @brief Constructs a pool that hands out @p bufferSize-byte buffers.
                 * @param bufferSize Size of each buffer in bytes.
                 * @param alignment  Alignment in bytes (defaults to @c Buffer::DefaultAlign).
                 * @param ms         Memory space to allocate from.
                 */
                BufferPool(size_t bufferSize, size_t alignment = 0,
                           const MemSpace &ms = MemSpace::Default);

                /** @brief Destructor — releases all pooled buffers. */
                ~BufferPool() = default;

                BufferPool(const BufferPool &) = delete;
                BufferPool &operator=(const BufferPool &) = delete;
                BufferPool(BufferPool &&) noexcept = default;
                BufferPool &operator=(BufferPool &&) noexcept = default;

                /** @brief Returns the size of each buffer in bytes. */
                size_t bufferSize() const { return _bufferSize; }

                /** @brief Returns the alignment of each buffer in bytes. */
                size_t alignment() const { return _alignment; }

                /** @brief Returns the number of buffers currently in the free list. */
                size_t available() const { return _free.size(); }

                /**
                 * @brief Pre-allocates @p count buffers into the free list.
                 *
                 * Does nothing if the free list already has at least
                 * @p count buffers.
                 */
                void reserve(size_t count);

                /**
                 * @brief Acquires a buffer from the pool.
                 *
                 * Returns a free buffer if one is available, otherwise
                 * allocates a fresh one. The returned @c Buffer is
                 * owned by the caller until @c release() is called.
                 *
                 * @return A valid @c Buffer, or an invalid Buffer if
                 *         allocation fails.
                 */
                Buffer acquire();

                /**
                 * @brief Returns a previously-acquired buffer to the pool.
                 *
                 * The buffer must have been originally returned by
                 * @c acquire() on the same pool. Its @c allocSize() and
                 * @c align() are checked; a mismatch drops the buffer
                 * instead of returning it to the free list.
                 *
                 * @param buf The buffer to release (moved in).
                 */
                void release(Buffer &&buf);

                /** @brief Drops all pooled buffers. */
                void clear();

        private:
                size_t       _bufferSize = 0;
                size_t       _alignment  = 0;
                MemSpace     _memSpace;
                List<Buffer> _free;
};

PROMEKI_NAMESPACE_END
