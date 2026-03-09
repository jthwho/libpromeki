/**
 * @file      mempool.cpp
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/unittest.h>
#include <promeki/mempool.h>

using namespace promeki;

void outputMemPoolStats(const UnitTest &unit, const MemPool::Stats &stats) {
        PROMEKI_TEST_MSG(String("Total Free  : %1").arg(stats.totalFree));
        PROMEKI_TEST_MSG(String("Total Used  : %1").arg(stats.totalUsed));
        PROMEKI_TEST_MSG(String("Free Blocks : %1").arg(stats.numFreeBlocks));
        PROMEKI_TEST_MSG(String("Alloc Blocks: %1").arg(stats.numAllocatedBlocks));
        PROMEKI_TEST_MSG(String("Largest Free: %1").arg(stats.largestFreeBlock));
        return;
}

PROMEKI_TEST_BEGIN(MemPool)

    size_t memorySize = 1024;
    MemPool memoryPool;
    memoryPool.addRegion(0x12345, memorySize);

    // Check initial statistics
    auto stats = memoryPool.stats();
    outputMemPoolStats(unit, stats);
    PROMEKI_TEST(stats.totalFree == memorySize);
    PROMEKI_TEST(stats.totalUsed == 0);
    PROMEKI_TEST(stats.numFreeBlocks == 1);
    PROMEKI_TEST(stats.numAllocatedBlocks == 0);
    PROMEKI_TEST(stats.largestFreeBlock == memorySize);

    // Allocate memory blocks from the memory pool
    void* block1 = memoryPool.allocate(128, 16); // 128 bytes, 16-byte alignment
    void* block2 = memoryPool.allocate(256, 32); // 256 bytes, 32-byte alignment
    PROMEKI_TEST(block1 != nullptr);
    PROMEKI_TEST(block2 != nullptr);

    // Check statistics after allocation
    memoryPool.dump();
    stats = memoryPool.stats();
    outputMemPoolStats(unit, stats);
    //PROMEKI_TEST(stats.totalFree == memorySize - (128 + 256));
    //PROMEKI_TEST(stats.totalUsed == 128 + 256);
    PROMEKI_TEST(stats.numFreeBlocks == 1);
    PROMEKI_TEST(stats.numAllocatedBlocks == 2);
    //PROMEKI_TEST(stats.largestFreeBlock == memorySize - (128 + 256));

    // Free the memory blocks back to the memory pool
    memoryPool.free(block1);
    memoryPool.free(block2);

    // Check statistics after deallocation
    memoryPool.dump();
    stats = memoryPool.stats();
    outputMemPoolStats(unit, stats);
    PROMEKI_TEST(stats.totalFree == memorySize);
    PROMEKI_TEST(stats.totalUsed == 0);
    PROMEKI_TEST(stats.numFreeBlocks == 1);
    PROMEKI_TEST(stats.numAllocatedBlocks == 0);
    PROMEKI_TEST(stats.largestFreeBlock == memorySize);

PROMEKI_TEST_END()

