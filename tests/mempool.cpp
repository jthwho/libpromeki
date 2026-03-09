/**
 * @file      mempool.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/mempool.h>
#include <promeki/string.h>
#include <promeki/logger.h>

using namespace promeki;

void outputMemPoolStats(const MemPool::Stats &stats) {
        promekiInfo("Total Free  : %s", String("%1").arg(stats.totalFree).cstr());
        promekiInfo("Total Used  : %s", String("%1").arg(stats.totalUsed).cstr());
        promekiInfo("Free Blocks : %s", String("%1").arg(stats.numFreeBlocks).cstr());
        promekiInfo("Alloc Blocks: %s", String("%1").arg(stats.numAllocatedBlocks).cstr());
        promekiInfo("Largest Free: %s", String("%1").arg(stats.largestFreeBlock).cstr());
        return;
}

TEST_CASE("MemPool") {

    size_t memorySize = 1024;
    MemPool memoryPool;
    memoryPool.addRegion(0x12345, memorySize);

    // Check initial statistics
    auto stats = memoryPool.stats();
    outputMemPoolStats(stats);
    CHECK(stats.totalFree == memorySize);
    CHECK(stats.totalUsed == 0);
    CHECK(stats.numFreeBlocks == 1);
    CHECK(stats.numAllocatedBlocks == 0);
    CHECK(stats.largestFreeBlock == memorySize);

    // Allocate memory blocks from the memory pool
    void* block1 = memoryPool.allocate(128, 16); // 128 bytes, 16-byte alignment
    void* block2 = memoryPool.allocate(256, 32); // 256 bytes, 32-byte alignment
    REQUIRE(block1 != nullptr);
    REQUIRE(block2 != nullptr);

    // Check statistics after allocation
    memoryPool.dump();
    stats = memoryPool.stats();
    outputMemPoolStats(stats);
    //CHECK(stats.totalFree == memorySize - (128 + 256));
    //CHECK(stats.totalUsed == 128 + 256);
    CHECK(stats.numFreeBlocks == 1);
    CHECK(stats.numAllocatedBlocks == 2);
    //CHECK(stats.largestFreeBlock == memorySize - (128 + 256));

    // Free the memory blocks back to the memory pool
    memoryPool.free(block1);
    memoryPool.free(block2);

    // Check statistics after deallocation
    memoryPool.dump();
    stats = memoryPool.stats();
    outputMemPoolStats(stats);
    CHECK(stats.totalFree == memorySize);
    CHECK(stats.totalUsed == 0);
    CHECK(stats.numFreeBlocks == 1);
    CHECK(stats.numAllocatedBlocks == 0);
    CHECK(stats.largestFreeBlock == memorySize);

}
