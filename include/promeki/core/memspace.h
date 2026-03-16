/**
 * @file      core/memspace.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/core/namespace.h>
#include <promeki/core/string.h>
#include <promeki/core/error.h>

PROMEKI_NAMESPACE_BEGIN

struct MemAllocation;

/**
 * @brief Abstraction for memory allocation in different address spaces.
 * @ingroup core_util
 *
 * Provides a uniform interface for allocating, releasing, copying, and setting
 * memory that may reside in different memory spaces (e.g. system RAM, GPU memory).
 * Each memory space is identified by an ID and provides its own set of operations.
 *
 * Memory spaces handle their own lifecycle concerns internally. For example,
 * SystemSecure performs page locking on allocation and secure zeroing on release
 * without exposing those details through the MemSpace API.
 */
class MemSpace {
        public:
                /** @brief Identifies a memory space. */
                enum ID {
                        System = 0,     ///< System (CPU) memory.
                        SystemSecure,   ///< System memory with secure zeroing on free and page locking.
                        Default = System ///< Alias for System memory.
                };

                /** @brief Function table for memory space operations. */
                struct Ops {
                        ID id;                                                              ///< The memory space identifier.
                        String name;                                                        ///< Human-readable name of the memory space.
                        bool (*isHostAccessible)(const MemAllocation &alloc);                ///< Returns true if the allocation is directly accessible from the host CPU.
                        void (*alloc)(MemAllocation &alloc);                                             ///< Allocate memory. Size and align are pre-filled.
                        void (*release)(MemAllocation &alloc);                                          ///< Release previously allocated memory.
                        bool (*copy)(const MemAllocation &src, const MemAllocation &dst, size_t bytes); ///< Copy bytes from this space to another.
                        Error (*fill)(void *ptr, size_t bytes, char value);                              ///< Fill memory with a byte value.
                };

                /**
                 * @brief Constructs a MemSpace for the given memory space ID.
                 * @param id The memory space to use (default: Default).
                 */
                MemSpace(ID id = Default) : d(lookup(id)) { }

                /**
                 * @brief Returns the human-readable name of this memory space.
                 * @return The name string.
                 */
                const String &name() const { return d->name; }

                /**
                 * @brief Returns the memory space identifier.
                 * @return The ID.
                 */
                ID id() const { return d->id; }

                /**
                 * @brief Returns true if the given allocation is directly accessible from the host CPU.
                 * @param alloc The allocation to check.
                 * @return True if the memory can be directly read/written by the CPU.
                 */
                bool isHostAccessible(const MemAllocation &alloc) const {
                        return d->isHostAccessible(alloc);
                }

                /**
                 * @brief Allocates memory in this memory space.
                 * @param bytes Number of bytes to allocate.
                 * @param align Required alignment in bytes.
                 * @return A MemAllocation describing the allocated region.
                 */
                inline MemAllocation alloc(size_t bytes, size_t align) const;

                /**
                 * @brief Releases a previously allocated memory region.
                 * @param alloc The allocation to release. Cleared on return.
                 */
                inline void release(MemAllocation &alloc) const;

                /**
                 * @brief Copies bytes from a source allocation to a destination allocation.
                 *
                 * Called on the source's MemSpace. The source and destination may
                 * reside in different memory spaces.
                 * @param src   The source allocation.
                 * @param dst   The destination allocation.
                 * @param bytes Number of bytes to copy.
                 * @return True on success, false if either pointer is nullptr.
                 */
                inline bool copy(const MemAllocation &src, const MemAllocation &dst, size_t bytes) const;

                /**
                 * @brief Fills memory with a byte value.
                 * @param ptr   Destination pointer.
                 * @param bytes Number of bytes to fill.
                 * @param value The byte value to fill with.
                 * @return Error::Ok on success, or an error if @p ptr is nullptr.
                 */
                Error fill(void *ptr, size_t bytes, char value) const {
                        if(ptr == nullptr) return Error::Invalid;
                        return d->fill(ptr, bytes, value);
                }

        private:
                const Ops *d = nullptr;
                static const Ops *lookup(ID id);
};

/**
 * @brief Describes a memory allocation from a MemSpace.
 *
 * Returned by MemSpace::alloc() and passed to MemSpace::release().
 * Contains the allocation pointer, size, alignment, the originating
 * MemSpace, and an opaque private pointer for allocator-specific data.
 */
struct MemAllocation {
        void    *ptr   = nullptr;       ///< Pointer to the allocated memory.
        size_t  size   = 0;             ///< Size of the allocation in bytes.
        size_t  align  = 0;             ///< Alignment of the allocation in bytes.
        MemSpace ms;                    ///< The memory space this was allocated from.
        void    *priv  = nullptr;       ///< Private data for the allocator implementation.

        /** @brief Returns true if this allocation is valid. */
        bool isValid() const { return ptr != nullptr; }
};

inline bool MemSpace::copy(const MemAllocation &src, const MemAllocation &dst, size_t bytes) const {
        if(src.ptr == nullptr || dst.ptr == nullptr) return false;
        return d->copy(src, dst, bytes);
}

inline MemAllocation MemSpace::alloc(size_t bytes, size_t align) const {
        MemAllocation a;
        a.size = bytes;
        a.align = align;
        a.ms = *this;
        d->alloc(a);
        return a;
}

inline void MemSpace::release(MemAllocation &alloc) const {
        if(alloc.ptr == nullptr) return;
        d->release(alloc);
        alloc.ptr = nullptr;
        alloc.priv = nullptr;
}

PROMEKI_NAMESPACE_END
