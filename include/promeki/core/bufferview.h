/**
 * @file      core/bufferview.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstdint>
#include <promeki/core/buffer.h>
#include <promeki/core/list.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Lightweight view into a region of a shared Buffer.
 * @ingroup core_media
 *
 * BufferView is a simple value type that references a contiguous
 * region (offset + size) of a shared Buffer. Multiple BufferViews
 * can reference different regions of the same underlying Buffer::Ptr,
 * avoiding per-view allocation when slicing large buffers into
 * smaller pieces.
 *
 * Common uses include RTP packet fragmentation, scatter/gather I/O,
 * and any scenario where a large buffer is logically divided into
 * sub-regions without copying data.
 *
 * @par Example
 * @code
 * auto buf = Buffer::Ptr::create(65536);
 * BufferView view1(buf, 0, 1400);
 * BufferView view2(buf, 1400, 1400);
 *
 * // Both views share the same buffer
 * CHECK(view1.buffer().ptr() == view2.buffer().ptr());
 * CHECK(view1.data() != view2.data());
 * @endcode
 */
class BufferView {
        public:
                /** @brief List of BufferView values. */
                using List = promeki::List<BufferView>;

                /** @brief Default constructor. Creates an empty view with no buffer. */
                BufferView() = default;

                /**
                 * @brief Constructs a BufferView referencing a region of a shared buffer.
                 * @param buf The shared backing buffer.
                 * @param offset Byte offset into the buffer where this view begins.
                 * @param size Byte size of this view.
                 *
                 * @note The caller must ensure that offset + size does not exceed
                 *       the buffer's allocated size. No bounds checking is performed.
                 */
                BufferView(Buffer::Ptr buf, size_t offset, size_t size)
                        : _buffer(std::move(buf)), _offset(offset), _size(size) { }

                /** @brief Returns the shared backing buffer. */
                const Buffer::Ptr &buffer() const { return _buffer; }

                /** @brief Returns the byte offset into the buffer. */
                size_t offset() const { return _offset; }

                /** @brief Returns the byte size of this view. */
                size_t size() const { return _size; }

                /**
                 * @brief Returns a const pointer to this view's data.
                 * @return Pointer to the data, or nullptr if no buffer is set.
                 */
                const uint8_t *data() const {
                        if(!_buffer) return nullptr;
                        return static_cast<const uint8_t *>(_buffer->data()) + _offset;
                }

                /**
                 * @brief Returns a mutable pointer to this view's data.
                 * @return Pointer to the data, or nullptr if no buffer is set.
                 */
                uint8_t *data() {
                        if(!_buffer) return nullptr;
                        return static_cast<uint8_t *>(_buffer->data()) + _offset;
                }

                /** @brief Returns true if no buffer is set. */
                bool isNull() const { return !_buffer; }

                /** @brief Returns true if a buffer is set. */
                bool isValid() const { return _buffer != nullptr; }

        private:
                Buffer::Ptr     _buffer;
                size_t          _offset = 0;
                size_t          _size = 0;
};

PROMEKI_NAMESPACE_END
