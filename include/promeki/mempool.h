/*****************************************************************************
 * mempool.h
 * April 27, 2023
 *
 * Copyright 2023 - Howard Logic
 * https://howardlogic.com
 * All Rights Reserved
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>
 *
 *****************************************************************************/

#pragma once

#include <cstddef>
#include <map>
#include <set>
#include <mutex>
#include <algorithm>
#include <promeki/namespace.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

// Creates a memory pool that can be allocated from.  It does not use the
// memory buffer that's given for any metadata, so you can use this to
// manage a pool of memory that this class wouldn't be able to access.
// Examples of this might be memory on a remote device, hardware device,
// video memory, etc.  It's also thread safe.
class MemPool {
        public:
                struct Stats {
                        size_t totalFree;
                        size_t totalUsed;
                        size_t numFreeBlocks;
                        size_t numAllocatedBlocks;
                        size_t largestFreeBlock;
                };

                class Block {
                        public:
                                bool            allocated = false;
                                intptr_t        address = 0;
                                size_t          size = 0;
                                size_t          alignment = 1;

                                bool operator<(const Block &other) const {
                                        return address < other.address;
                                }

                                uintptr_t alignedAddress(size_t align) const {
                                        uintptr_t _align = align - 1;
                                        return (address + _align) & ~(_align);
                                }

                                uintptr_t alignedAddress() const {
                                        return alignedAddress(alignment);
                                }

                                size_t padding(size_t align) const {
                                        return align > 1 ? alignedAddress(align) - address : 0;
                                }

                                size_t padding() const {
                                        return padding(alignment);
                                }

                                bool follows(const Block &block) const {
                                        return block.address + block.size == address;
                                }

                        };

                static bool isValidAlignment(size_t val) {
                        return (val > 0) && !(val & (val - 1));
                }

                using BlockSet = std::set<Block>;
                using BlockMap = std::map<uintptr_t, Block>;

                MemPool();

                void addRegion(uintptr_t startingAddress, size_t size);
                void addRegion(void *startingAddress, size_t size) {
                        addRegion(reinterpret_cast<uintptr_t>(startingAddress), size);
                        return;
                }

                const String &name() const { return _name; }
                void setName(const String &val) {
                        _name = val;
                        return;
                }
                Stats stats() const;
                BlockSet memoryMap() const;
                void dump() const;
                void *allocate(size_t size, size_t alignment = 1);
                void free(void* ptr);

       private:
                String                  _name;
                mutable std::mutex      _mutex;
                BlockSet                _freeBlocks;
                BlockMap                _allocatedBlocks;
};

PROMEKI_NAMESPACE_END

