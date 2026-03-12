/**
 * @file      buffer.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cassert>
#include <promeki/namespace.h>
#include <promeki/list.h>
#include <promeki/sharedptr.h>
#include <promeki/memspace.h>
#include <promeki/error.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Generic memory buffer descriptor with alignment and memory space support.
 *
 * Buffer is a uniform tracking object for contiguous memory regions that may
 * reside in different address spaces: local system RAM, GPU device memory,
 * or even remote memory on another machine (e.g. RDMA). It does not assume
 * the memory is host-accessible — use isHostAccessible() before dereferencing
 * data(). All memory operations (allocation, release, copy, fill) are
 * delegated through the buffer's MemSpace, allowing each memory space to
 * provide appropriate implementations.
 *
 * @par Allocation and ownership
 * Buffers can be allocated from any MemSpace, or can wrap existing memory
 * pointers without taking ownership. Owned buffers are released through
 * their MemSpace on destruction. The MemSpace's release implementation
 * handles any space-specific cleanup (e.g. SystemSecure performs secure
 * zeroing and page unlocking before freeing).
 *
 * @par Data shifting
 * The shiftData() method offsets the user-visible data pointer from the
 * allocation base. This is primarily used for O_DIRECT I/O, where the
 * underlying allocation must be page-aligned but the actual content starts
 * at some offset within that allocation. The shift is always relative to
 * the allocation base (not accumulating). After a shift, size() returns
 * the remaining usable bytes, while allocSize() always returns the full
 * allocation size. Calling shiftData(0) resets the view to the base.
 *
 * @par Copy semantics
 * Copying a buffer performs a deep copy through the MemSpace's copy
 * operation, preserving any data shift offset. The copy inherits the
 * source's MemSpace, so copies of a SystemSecure buffer remain secure.
 * For memory spaces where deep copy is expensive or impossible (large GPU
 * framebuffers, remote RDMA regions), use Buffer::Ptr for shared ownership
 * instead.
 *
 * @par Shared ownership
 * When shared ownership is needed, use Buffer::Ptr (SharedPtr\<Buffer\>).
 * This is the recommended approach for large buffers or buffers in
 * non-host-accessible memory spaces, as it avoids deep copies.
 */
class Buffer {
        PROMEKI_SHARED_FINAL(Buffer)
        public:
                /** @brief Shared pointer type for Buffer. */
                using Ptr = SharedPtr<Buffer>;

                /**
                 * @brief Returns the system memory page size in bytes.
                 * @return The page size.
                 */
                static size_t getPageSize();

                /** @brief Default memory alignment in bytes. */
                static const size_t DefaultAlign;

                /** @brief List of Buffer values. */
                using List = promeki::List<Buffer>;

                /** @brief List of shared Buffer pointers. */
                using PtrList = promeki::List<Ptr>;

                /** @brief Constructs an invalid (empty) buffer. */
                Buffer() = default;

                /**
                 * @brief Allocates a buffer of the given size.
                 * @param sz Size in bytes to allocate.
                 * @param an Alignment in bytes (defaults to DefaultAlign).
                 * @param ms Memory space to allocate from.
                 */
                Buffer(size_t sz, size_t an = DefaultAlign, const MemSpace &ms = MemSpace::Default) :
                        _alloc(ms.alloc(sz, an))
                {
                        _data = _alloc.ptr;
                }

                /**
                 * @brief Wraps an existing memory pointer as a buffer.
                 * @param p   Pointer to existing memory.
                 * @param sz  Size of the memory region in bytes.
                 * @param an  Alignment of the pointer (0 if unknown).
                 * @param own If true, the buffer takes ownership and will free the memory on destruction.
                 * @param ms  Memory space the pointer belongs to.
                 */
                Buffer(void *p, size_t sz, size_t an = 0, bool own = false, const MemSpace &ms = MemSpace::Default) :
                        _data(p), _owned(own)
                {
                        _alloc.ptr = p;
                        _alloc.size = sz;
                        _alloc.align = an;
                        _alloc.ms = ms;
                }

                /**
                 * @brief Copy constructor.
                 *
                 * Performs a deep copy of the buffer data, including any data shift offset.
                 */
                Buffer(const Buffer &o) : _owned(true) {
                        if(o._data != nullptr) {
                                _alloc = o._alloc.ms.alloc(o._alloc.size, o._alloc.align);
                                if(_alloc.ptr == nullptr) return;
                                o._alloc.ms.copy(o._alloc, _alloc, _alloc.size);
                                size_t shift = static_cast<uint8_t *>(o._data) - static_cast<uint8_t *>(o._alloc.ptr);
                                _data = static_cast<uint8_t *>(_alloc.ptr) + shift;
                        }
                }

                /** @brief Copy assignment operator. Performs a deep copy. */
                Buffer &operator=(const Buffer &o) {
                        if(this == &o) return *this;
                        if(_owned) _alloc.ms.release(_alloc);
                        _data = nullptr;
                        _owned = true;
                        _alloc = {};
                        if(o._data != nullptr) {
                                _alloc = o._alloc.ms.alloc(o._alloc.size, o._alloc.align);
                                if(_alloc.ptr != nullptr) {
                                        o._alloc.ms.copy(o._alloc, _alloc, _alloc.size);
                                        size_t shift = static_cast<uint8_t *>(o._data) - static_cast<uint8_t *>(o._alloc.ptr);
                                        _data = static_cast<uint8_t *>(_alloc.ptr) + shift;
                                }
                        }
                        return *this;
                }

                /** @brief Move constructor. Transfers ownership from the source buffer. */
                Buffer(Buffer &&o) noexcept :
                        _alloc(o._alloc), _data(o._data), _owned(o._owned)
                {
                        o._alloc = {};
                        o._data = nullptr;
                        o._owned = false;
                }

                /** @brief Move assignment operator. Transfers ownership from the source buffer. */
                Buffer &operator=(Buffer &&o) noexcept {
                        if(this == &o) return *this;
                        if(_owned) _alloc.ms.release(_alloc);
                        _alloc = o._alloc;
                        _data = o._data;
                        _owned = o._owned;
                        o._alloc = {};
                        o._data = nullptr;
                        o._owned = false;
                        return *this;
                }

                /** @brief Destructor. Releases owned memory. */
                ~Buffer() {
                        if(_owned) _alloc.ms.release(_alloc);
                }

                /**
                 * @brief Returns true if the buffer has been allocated or assigned memory.
                 * @return true if the internal data pointer is non-null.
                 */
                bool isValid() const { return _data != nullptr; }

                /**
                 * @brief Returns a pointer to the buffer data.
                 * @return The current data pointer (may be shifted from the original allocation).
                 */
                void *data() const { return _data; }

                /**
                 * @brief Returns the original allocation pointer.
                 *
                 * Unlike data(), this always points to the start of the allocated region
                 * regardless of any shiftData() calls. This is the pointer that was
                 * returned by the MemSpace allocator.
                 *
                 * @return The original allocation pointer.
                 */
                void *odata() const { return _alloc.ptr; }

                /**
                 * @brief Returns the usable size of the buffer in bytes.
                 *
                 * If shiftData() has been called, this returns the remaining
                 * size from the current data pointer to the end of the allocation.
                 * @return The usable buffer size.
                 */
                size_t size() const {
                        if(_data == nullptr) return 0;
                        return _alloc.size - static_cast<size_t>(
                                static_cast<uint8_t *>(_data) -
                                static_cast<uint8_t *>(_alloc.ptr)
                        );
                }

                /**
                 * @brief Returns the total allocation size in bytes.
                 *
                 * Unlike size(), this always returns the full allocation size
                 * regardless of any shiftData() calls.
                 * @return The allocation size.
                 */
                size_t allocSize() const { return _alloc.size; }

                /**
                 * @brief Returns the alignment of the buffer in bytes.
                 * @return The alignment value.
                 */
                size_t align() const { return _alloc.align; }

                /**
                 * @brief Returns the memory space this buffer was allocated from.
                 * @return A const reference to the MemSpace.
                 */
                const MemSpace &memSpace() const { return _alloc.ms; }

                /**
                 * @brief Returns the underlying allocation descriptor.
                 * @return A const reference to the MemAllocation.
                 */
                const MemAllocation &allocation() const { return _alloc; }

                /**
                 * @brief Shifts the data pointer forward from the allocation base.
                 *
                 * Sets the data pointer to allocation base + bytes, creating a
                 * sub-buffer view. The shift is always relative to the original
                 * allocation pointer, not the current data pointer.
                 * @param bytes Number of bytes from the allocation base.
                 */
                void shiftData(size_t bytes) {
                        assert(bytes <= _alloc.size);
                        _data = static_cast<uint8_t *>(_alloc.ptr) + bytes;
                        return;
                }

                /**
                 * @brief Returns true if the buffer memory is directly accessible from the host CPU.
                 * @return True for system memory, false for device or remote memory.
                 */
                bool isHostAccessible() const { return _alloc.ms.isHostAccessible(_alloc); }

                /**
                 * @brief Enables or disables ownership of the underlying memory.
                 * @param val If true, the buffer will free memory on destruction; if false, it will not.
                 */
                void setOwnershipEnabled(bool val) {
                        _owned = val;
                        return;
                }

                /**
                 * @brief Fills the entire buffer with the given byte value.
                 * @param value The byte value to fill with.
                 * @return Error::Ok on success, or an error on failure.
                 */
                Error fill(char value) const {
                        return _alloc.ms.fill(_data, size(), value);
                }

        private:
                MemAllocation   _alloc;
                void            *_data          = nullptr;
                bool            _owned          = true;
};

PROMEKI_NAMESPACE_END
