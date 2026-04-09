/**
 * @file      bufferpool.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/bufferpool.h>

PROMEKI_NAMESPACE_BEGIN

BufferPool::BufferPool(size_t bufferSize, size_t alignment, const MemSpace &ms) :
        _bufferSize(bufferSize),
        _alignment(alignment == 0 ? Buffer::DefaultAlign : alignment),
        _memSpace(ms) {}

void BufferPool::reserve(size_t count) {
        while(_free.size() < count) {
                Buffer buf(_bufferSize, _alignment, _memSpace);
                if(!buf.isValid()) return;
                _free.pushToBack(std::move(buf));
        }
}

Buffer BufferPool::acquire() {
        if(!_free.isEmpty()) {
                Buffer buf = std::move(_free[_free.size() - 1]);
                _free.popFromBack();
                // Reset user-visible state: size() back to 0 and shiftData
                // back to the base. The caller treats the returned buffer
                // as freshly allocated.
                buf.shiftData(0);
                return buf;
        }
        return Buffer(_bufferSize, _alignment, _memSpace);
}

void BufferPool::release(Buffer &&buf) {
        if(!buf.isValid()) return;
        // Reject shape mismatches — it's almost certainly a bug to pass
        // a different-geometry buffer into a fixed-geometry pool.
        if(buf.allocSize() != _bufferSize || buf.align() != _alignment) return;
        // Reset user-visible state so next acquire gets a fresh view.
        buf.shiftData(0);
        buf.setSize(0);
        _free.pushToBack(std::move(buf));
}

void BufferPool::clear() {
        _free.clear();
}

PROMEKI_NAMESPACE_END
