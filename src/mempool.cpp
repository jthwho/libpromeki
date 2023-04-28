/*****************************************************************************
 * mempool.cpp
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

#include <iostream>
#include <promeki/mempool.h>
#include <promeki/string.h>

namespace promeki {

MemPool::MemPool() {

}

void MemPool::addRegion(uintptr_t startingAddress, size_t size) {
        Block block;
        block.address = startingAddress;
        block.allocated = false;
        block.size = size;
        _freeBlocks.insert(block);
        return;
}

MemPool::Stats MemPool::stats() const {
        std::lock_guard<std::mutex> lock(_mutex);
        Stats stats = {0, 0, 0, 0, 0};

        // Calculate statistics for free blocks
        for(const auto &block : _freeBlocks) {
                stats.totalFree += block.size;
                stats.numFreeBlocks++;
                stats.largestFreeBlock = std::max(stats.largestFreeBlock, block.size);
        }

        // Calculate statistics for allocated blocks
        for(const auto &val : _allocatedBlocks) {
                const Block &block = val.second;
                stats.totalUsed += block.size;
        }
        stats.numAllocatedBlocks = _allocatedBlocks.size();

        return stats;

}

MemPool::BlockSet MemPool::memoryMap() const {
        std::lock_guard<std::mutex> lock(_mutex);
        // Start with all the free blocks then insert all the allocated ones.
        BlockSet ret = _freeBlocks;
        for(const auto &val : _allocatedBlocks) ret.insert(val.second);
        return ret;
}

void MemPool::dump() const {
        BlockSet map = memoryMap();
        for(const auto& block : map) {
                if(block.allocated) {
                        std::cout << String("ALLOC [%1] %2 bytes, %3 align")
                        .arg(block.address, 16, -18, ' ', true)
                        .arg(block.size)
                        .arg(block.alignment)
                        << std::endl;
                } else {
                        std::cout << String("FREE  [%1] %2 bytes")
                        .arg(block.address, 16, -18, ' ', true)
                        .arg(block.size)
                        << std::endl;
                }
        }
        return;
}

void *MemPool::allocate(size_t size, size_t alignment) {
        if(!isValidAlignment(alignment)) {
                std::cout << "Allocate Failed: alignment invalid. Size " << size << ", Alignment " << alignment << std::endl;
                return nullptr;
        }
        std::lock_guard<std::mutex> lock(_mutex);

        // Walk through the blocks until we find one that's big enough
        for(auto it = _freeBlocks.begin(); it != _freeBlocks.end(); ++it) {
                // The amount of padding a block needs depends on the block's starting address
                // and the alignment.  We need to compute that size for each block we look at
                // and use that size to determine if 
                size_t padding = it->padding(alignment);
                size_t totalSize = size + padding;
                if(totalSize >= size) {
                        // Copy the free block and modify it to represent
                        // the allocated block.
                        Block block = *it;
                        block.allocated = true;
                        block.size = totalSize;
                        block.alignment = alignment;
                        uintptr_t alignedAddress = block.alignedAddress();
                        _allocatedBlocks[alignedAddress] = block;

                        // If the original free block had more than totalAllocationSize bytes
                        // we need to create a new free block and add it.
                        if(it->size > block.size) {
                                Block freeBlock = *it;
                                freeBlock.allocated = false;
                                freeBlock.size = it->size - block.size;
                                freeBlock.address = block.address + block.size;
                                _freeBlocks.insert(freeBlock);
                        }
                        _freeBlocks.erase(it);
                        return reinterpret_cast<void*>(alignedAddress);
                }
        }
        std::cout << "Allocate Failed: unable to find free block large enough. Size " << size << ", Alignment " << alignment << std::endl;
        return nullptr; // No suitable block found
}

void MemPool::free(void* ptr) {
        if(!ptr) return;
        std::lock_guard<std::mutex> lock(_mutex);
        auto it = _allocatedBlocks.find(reinterpret_cast<uintptr_t>(ptr));
        if(it == _allocatedBlocks.end()) return; // Invalid pointer, not found in allocations
        
        // Copy the block here and remove it from the allocated blocks
        Block block = it->second;
        _allocatedBlocks.erase(it);

        // Make sure we mark the block as not allocated so it'll report correctly if we
        // ask for a complete block list later.
        block.allocated = false;

        // We initially add the block to the free blocks so it'll get sorted to the right
        // position.  That'll allow us to check the block before and after to see if it
        // should be coalesced
        auto insret = _freeBlocks.insert(block);
        if(!insret.second) {
                // FIXME: Add a proper warning when logging gets enabled.
                std::cout << "FAILED TO INSERT BLOCK" << std::endl;
                return;
        }

        auto itf = insret.first;
        auto itfPrev = std::prev(itf);
        auto itfNext = std::next(itf);
        bool update = false;

        if(itfPrev != _freeBlocks.end() && block.follows(*itfPrev)) {
                block.address = itfPrev->address;
                block.size += itfPrev->size;
                _freeBlocks.erase(itfPrev);
                update = true;
        }
        if(itfNext != _freeBlocks.end() && itfNext->follows(block)) {
                block.size += itfNext->size;
                _freeBlocks.erase(itfNext);
                update = true;
        }
        // If update, we need to remove the block we just added and add it again
        // since it got bigger and might have changed address.
        if(update) {
                _freeBlocks.erase(itf);
                insret = _freeBlocks.insert(block);
                if(!insret.second) {
                        std::cout << "FAILED TO INSERT UPDATED BLOCK" << std::endl;
                        return;
                }
        }
        return;
}


} // namespace promeki

