/**
 * @file      mempool.cpp
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#include <iostream>
#include <promeki/core/mempool.h>
#include <promeki/core/logger.h>

PROMEKI_NAMESPACE_BEGIN

MemPool::MemPool() {
        _name = String::hex(reinterpret_cast<uintptr_t>(this));
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
        promekiInfo("MemPool '%s' Dump", _name.cstr());
        for(const auto& block : map) {
                if(block.allocated) {
                        promekiInfo("A [0x%16llX] %d bytes, %d align",
                                (unsigned long long)block.address, (int)block.size, (int)block.alignment);
                } else {
                        promekiInfo("F [0x%16llX] %d bytes",
                                (unsigned long long)block.address, (int)block.size);
                }
        }
        return;
}

void *MemPool::allocate(size_t size, size_t alignment) {
        if(!isValidAlignment(alignment)) {
                promekiErr("MemPool '%s': allocate failed, alignment invalid. Size %d, Align %d", 
                                _name.cstr(), (int)size, (int)alignment);
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
                if(it->size >= totalSize) {
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
                        _freeBlocks.remove(it);
                        return reinterpret_cast<void*>(alignedAddress);
                }
        }
        promekiErr("MemPool '%s': allocate failed, unable to find free block large enough. Size %d, Align %d", 
                        _name.cstr(), (int)size, (int)alignment);
        return nullptr; // No suitable block found
}

void MemPool::free(void* ptr) {
        if(!ptr) return;
        std::lock_guard<std::mutex> lock(_mutex);
        auto it = _allocatedBlocks.find(reinterpret_cast<uintptr_t>(ptr));
        if(it == _allocatedBlocks.end()) return; // Invalid pointer, not found in allocations
        
        // Copy the block here and remove it from the allocated blocks
        Block block = it->second;
        _allocatedBlocks.remove(it);

        // Make sure we mark the block as not allocated so it'll report correctly if we
        // ask for a complete block list later.
        block.allocated = false;

        // We initially add the block to the free blocks so it'll get sorted to the right
        // position.  That'll allow us to check the block before and after to see if it
        // should be coalesced
        auto insret = _freeBlocks.insert(block);
        if(!insret.second) {
                promekiErr("MemPool '%s': free %p (%p) failed to insert block", 
                        _name.cstr(), ptr, (void *)block.address);
                return;
        }

        auto itf = insret.first;
        auto itfPrev = std::prev(itf);
        auto itfNext = std::next(itf);
        bool update = false;

        if(itfPrev != _freeBlocks.end() && block.follows(*itfPrev)) {
                block.address = itfPrev->address;
                block.size += itfPrev->size;
                _freeBlocks.remove(itfPrev);
                update = true;
        }
        if(itfNext != _freeBlocks.end() && itfNext->follows(block)) {
                block.size += itfNext->size;
                _freeBlocks.remove(itfNext);
                update = true;
        }
        // If update, we need to remove the block we just added and add it again
        // since it got bigger and might have changed address.
        if(update) {
                _freeBlocks.remove(itf);
                insret = _freeBlocks.insert(block);
                if(!insret.second) {
                        promekiErr("MemPool '%s': free %p (%p) failed to insert updated block", 
                                _name.cstr(), ptr, (void *)block.address);
                        return;
                }
        }
        return;
}

PROMEKI_NAMESPACE_END

