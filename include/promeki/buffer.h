/**
 * @file      buffer.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstring>
#include <promeki/namespace.h>
#include <promeki/list.h>
#include <promeki/sharedptr.h>
#include <promeki/memspace.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Managed memory buffer with alignment and memory space support.
 *
 * Provides a contiguous block of memory that can be allocated with a
 * specific alignment and memory space. Supports both owned and
 * non-owned (externally managed) memory. When shared ownership is
 * needed, use Buffer::Ptr.
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
                        _ms(ms), _size(sz), _align(an)
                {
                        _odata = _data = _ms.alloc(_size, _align);
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
                        _ms(ms), _data(p), _odata(p), _size(sz), _align(an), _owned(own) { }

                /**
                 * @brief Copy constructor.
                 *
                 * Performs a deep copy of the buffer data, including any data shift offset.
                 */
                Buffer(const Buffer &o) : _ms(o._ms), _size(o._size), _align(o._align), _owned(true) {
                        if(o._data != nullptr) {
                                _odata = _data = _ms.alloc(_size, _align);
                                size_t shift = static_cast<uint8_t *>(o._data) - static_cast<uint8_t *>(o._odata);
                                std::memcpy(_odata, o._odata, _size + shift);
                                _data = static_cast<uint8_t *>(_odata) + shift;
                        }
                }

                /** @brief Copy assignment operator. Performs a deep copy. */
                Buffer &operator=(const Buffer &o) {
                        if(this == &o) return *this;
                        if(_owned) _ms.release(_odata);
                        _ms = o._ms;
                        _size = o._size;
                        _align = o._align;
                        _owned = true;
                        _data = nullptr;
                        _odata = nullptr;
                        if(o._data != nullptr) {
                                _odata = _data = _ms.alloc(_size, _align);
                                size_t shift = static_cast<uint8_t *>(o._data) - static_cast<uint8_t *>(o._odata);
                                std::memcpy(_odata, o._odata, _size + shift);
                                _data = static_cast<uint8_t *>(_odata) + shift;
                        }
                        return *this;
                }

                /** @brief Move constructor. Transfers ownership from the source buffer. */
                Buffer(Buffer &&o) noexcept :
                        _ms(o._ms), _data(o._data), _odata(o._odata),
                        _size(o._size), _align(o._align), _owned(o._owned)
                {
                        o._data = nullptr;
                        o._odata = nullptr;
                        o._size = 0;
                        o._owned = false;
                }

                /** @brief Move assignment operator. Transfers ownership from the source buffer. */
                Buffer &operator=(Buffer &&o) noexcept {
                        if(this == &o) return *this;
                        if(_owned) _ms.release(_odata);
                        _ms = o._ms;
                        _data = o._data;
                        _odata = o._odata;
                        _size = o._size;
                        _align = o._align;
                        _owned = o._owned;
                        o._data = nullptr;
                        o._odata = nullptr;
                        o._size = 0;
                        o._owned = false;
                        return *this;
                }

                /** @brief Destructor. Releases owned memory. */
                ~Buffer() {
                        if(_owned) _ms.release(_odata);
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
                 * @brief Returns the size of the buffer in bytes.
                 * @return The buffer size.
                 */
                size_t size() const { return _size; }

                /**
                 * @brief Returns the alignment of the buffer in bytes.
                 * @return The alignment value.
                 */
                size_t align() const { return _align; }

                /**
                 * @brief Returns the memory space this buffer was allocated from.
                 * @return A const reference to the MemSpace.
                 */
                const MemSpace &memSpace() const { return _ms; }

                /**
                 * @brief Shifts the data pointer forward by the given number of bytes.
                 *
                 * This advances the data pointer without changing the original
                 * allocation pointer, effectively creating a sub-buffer view.
                 * @param bytes Number of bytes to shift forward.
                 */
                void shiftData(size_t bytes) {
                        _data = static_cast<void *>(static_cast<uint8_t *>(_data) + bytes);
                        return;
                }

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
                 * @return true on success, false on failure.
                 */
                bool fill(char value) const {
                        return _ms.set(_data, _size, value);
                }

        private:
                MemSpace        _ms;
                void            *_data          = nullptr;
                void            *_odata         = nullptr;
                size_t          _size           = 0;
                size_t          _align          = 0;
                bool            _owned          = true;
};

PROMEKI_NAMESPACE_END

