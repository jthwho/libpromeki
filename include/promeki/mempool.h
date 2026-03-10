/**
 * @file      mempool.h
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstddef>
#include <mutex>
#include <algorithm>
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/map.h>
#include <promeki/set.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Thread-safe memory pool allocator with external metadata.
 *
 * Manages a pool of memory that can be allocated from with alignment support.
 * It does not use the managed memory buffer for any metadata, so it can
 * manage memory that this class cannot directly access — for example,
 * memory on a remote device, hardware device, or video memory.
 * Free blocks are automatically coalesced on deallocation.
 */
class MemPool {
        public:
                /**
                 * @brief Statistics about the current state of the memory pool.
                 */
                struct Stats {
                        size_t totalFree;           ///< Total bytes available for allocation.
                        size_t totalUsed;            ///< Total bytes currently allocated.
                        size_t numFreeBlocks;        ///< Number of free block regions.
                        size_t numAllocatedBlocks;   ///< Number of allocated block regions.
                        size_t largestFreeBlock;     ///< Size in bytes of the largest free block.
                };

                /**
                 * @brief Represents a contiguous region of memory in the pool.
                 */
                class Block {
                        public:
                                bool            allocated = false;   ///< true if this block is allocated.
                                intptr_t        address = 0;         ///< Starting address of the block.
                                size_t          size = 0;            ///< Size in bytes.
                                size_t          alignment = 1;       ///< Alignment requirement.

                                /** @brief Orders blocks by address for sorted storage. */
                                bool operator<(const Block &other) const {
                                        return address < other.address;
                                }

                                /**
                                 * @brief Returns the address aligned to the given alignment.
                                 * @param align The alignment boundary (must be a power of two).
                                 * @return The aligned address.
                                 */
                                uintptr_t alignedAddress(size_t align) const {
                                        uintptr_t _align = align - 1;
                                        return (address + _align) & ~(_align);
                                }

                                /**
                                 * @brief Returns the address aligned to this block's alignment.
                                 * @return The aligned address.
                                 */
                                uintptr_t alignedAddress() const {
                                        return alignedAddress(alignment);
                                }

                                /**
                                 * @brief Returns padding bytes needed to reach the given alignment.
                                 * @param align The alignment boundary.
                                 * @return Number of padding bytes, or 0 if no padding needed.
                                 */
                                size_t padding(size_t align) const {
                                        return align > 1 ? alignedAddress(align) - address : 0;
                                }

                                /**
                                 * @brief Returns padding bytes needed for this block's alignment.
                                 * @return Number of padding bytes.
                                 */
                                size_t padding() const {
                                        return padding(alignment);
                                }

                                /**
                                 * @brief Returns true if this block immediately follows another.
                                 * @param block The preceding block to check adjacency against.
                                 * @return true if this block starts where the other ends.
                                 */
                                bool follows(const Block &block) const {
                                        return block.address + block.size == address;
                                }

                        };

                /**
                 * @brief Returns true if the given value is a valid alignment (power of two).
                 * @param val The alignment value to test.
                 * @return true if val is a non-zero power of two.
                 */
                static bool isValidAlignment(size_t val) {
                        return (val > 0) && !(val & (val - 1));
                }

                using BlockSet = Set<Block>;        ///< Sorted set of blocks ordered by address.
                using BlockMap = Map<uintptr_t, Block>; ///< Map from aligned address to allocated block.

                /** @brief Constructs an empty memory pool with a default hex name. */
                MemPool();

                /**
                 * @brief Adds a contiguous memory region to the pool.
                 * @param startingAddress The starting address of the region.
                 * @param size            The size of the region in bytes.
                 */
                void addRegion(uintptr_t startingAddress, size_t size);

                /**
                 * @brief Adds a contiguous memory region to the pool.
                 * @param startingAddress Pointer to the start of the region.
                 * @param size            The size of the region in bytes.
                 */
                void addRegion(void *startingAddress, size_t size) {
                        addRegion(reinterpret_cast<uintptr_t>(startingAddress), size);
                        return;
                }

                /** @brief Returns the name of this memory pool. */
                const String &name() const { return _name; }

                /**
                 * @brief Sets the name of this memory pool.
                 * @param val The new name.
                 */
                void setName(const String &val) {
                        _name = val;
                        return;
                }

                /**
                 * @brief Returns current pool statistics.
                 * @return A Stats struct with usage information.
                 */
                Stats stats() const;

                /**
                 * @brief Returns a combined set of all free and allocated blocks.
                 * @return A BlockSet containing every block in the pool.
                 */
                BlockSet memoryMap() const;

                /** @brief Dumps the memory map to the logger for debugging. */
                void dump() const;

                /**
                 * @brief Allocates a block from the pool.
                 * @param size      The number of bytes to allocate.
                 * @param alignment The alignment requirement (must be a power of two, default 1).
                 * @return A pointer to the aligned allocation, or nullptr on failure.
                 */
                void *allocate(size_t size, size_t alignment = 1);

                /**
                 * @brief Frees a previously allocated block back to the pool.
                 *
                 * Adjacent free blocks are automatically coalesced.
                 *
                 * @param ptr The pointer returned by allocate(). Passing nullptr is a no-op.
                 */
                void free(void* ptr);

       private:
                String                  _name;
                mutable std::mutex      _mutex;
                BlockSet                _freeBlocks;
                BlockMap                _allocatedBlocks;
};

PROMEKI_NAMESPACE_END

