/**
 * @file      memspace.h
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Abstraction for memory allocation in different address spaces.
 *
 * Provides a uniform interface for allocating, releasing, copying, and setting
 * memory that may reside in different memory spaces (e.g. system RAM, GPU memory).
 * Each memory space is identified by an ID and provides its own set of operations.
 */
class MemSpace {
        public:
                /** @brief Identifies a memory space. */
                enum ID {
                        System = 0,     ///< System (CPU) memory.
                        Default = System ///< Alias for System memory.
                };

                /** @brief Function table for memory space operations. */
                struct Ops {
                        ID id;                                                          ///< The memory space identifier.
                        String name;                                                    ///< Human-readable name of the memory space.
                        void *(*alloc)(size_t bytes, size_t align);                     ///< Allocate memory with alignment.
                        void (*release)(void *ptr);                                     ///< Release previously allocated memory.
                        bool (*copy)(ID toSpace, void *to, const void *from, size_t bytes); ///< Copy bytes between memory spaces.
                        bool (*set)(void *to, size_t bytes, char value);                ///< Fill memory with a byte value.
                };

                /**
                 * @brief Constructs a MemSpace for the given memory space ID.
                 * @param id The memory space to use (default: Default).
                 */
                MemSpace(ID id = Default) : d(lookup(id)) { }

                /**
                 * @brief Allocates memory in this memory space.
                 * @param bytes Number of bytes to allocate.
                 * @param align Required alignment in bytes.
                 * @return Pointer to the allocated memory, or nullptr on failure.
                 */
                void *alloc(size_t bytes, size_t align) const {
                        return d->alloc(bytes, align);
                }

                /**
                 * @brief Releases memory previously allocated from this memory space.
                 * @param ptr Pointer to free. Does nothing if nullptr.
                 */
                void release(void *ptr) const {
                        if(ptr == nullptr) return;
                        d->release(ptr);
                        return;
                }

                /**
                 * @brief Copies bytes from this memory space to another.
                 * @param toSpace The destination memory space ID.
                 * @param to      Destination pointer.
                 * @param from    Source pointer.
                 * @param bytes   Number of bytes to copy.
                 * @return True on success, false if either pointer is nullptr.
                 */
                bool copy(ID toSpace, void *to, const void *from, size_t bytes) const {
                        if(to == nullptr || from == nullptr) return false;
                        return d->copy(toSpace, to, from, bytes);
                }

                /**
                 * @brief Fills memory with a byte value.
                 * @param to    Destination pointer.
                 * @param bytes Number of bytes to set.
                 * @param value The byte value to fill with.
                 * @return True on success, false if @p to is nullptr.
                 */
                bool set(void *to, size_t bytes, char value) const {
                        if(to == nullptr) return false;
                        return d->set(to, bytes, value);
                }

        private:
                const Ops *d = nullptr;
                static const Ops *lookup(ID id);
};

PROMEKI_NAMESPACE_END


