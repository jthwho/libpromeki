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

// ============================================================================
// Construction and initial state
// ============================================================================

TEST_CASE("MemPool_DefaultConstruction") {
        MemPool pool;
        auto    stats = pool.stats();
        CHECK(stats.totalFree == 0);
        CHECK(stats.totalUsed == 0);
        CHECK(stats.numFreeBlocks == 0);
        CHECK(stats.numAllocatedBlocks == 0);
        CHECK(stats.largestFreeBlock == 0);
}

TEST_CASE("MemPool_Name") {
        MemPool pool;
        // Default name is hex address, should not be empty
        CHECK(pool.name().size() > 0);
        pool.setName("TestPool");
        CHECK(pool.name() == "TestPool");
}

// ============================================================================
// addRegion
// ============================================================================

TEST_CASE("MemPool_AddRegion") {
        MemPool pool;
        pool.addRegion(0x10000, 4096);
        auto stats = pool.stats();
        CHECK(stats.totalFree == 4096);
        CHECK(stats.totalUsed == 0);
        CHECK(stats.numFreeBlocks == 1);
        CHECK(stats.numAllocatedBlocks == 0);
        CHECK(stats.largestFreeBlock == 4096);
}

TEST_CASE("MemPool_AddMultipleRegions") {
        MemPool pool;
        pool.addRegion(0x10000, 1024);
        pool.addRegion(0x20000, 2048);
        auto stats = pool.stats();
        CHECK(stats.totalFree == 1024 + 2048);
        CHECK(stats.numFreeBlocks == 2);
        CHECK(stats.largestFreeBlock == 2048);
}

// ============================================================================
// isValidAlignment
// ============================================================================

TEST_CASE("MemPool_IsValidAlignment") {
        CHECK(MemPool::isValidAlignment(1));
        CHECK(MemPool::isValidAlignment(2));
        CHECK(MemPool::isValidAlignment(4));
        CHECK(MemPool::isValidAlignment(8));
        CHECK(MemPool::isValidAlignment(16));
        CHECK(MemPool::isValidAlignment(32));
        CHECK(MemPool::isValidAlignment(64));
        CHECK(MemPool::isValidAlignment(128));
        CHECK(MemPool::isValidAlignment(256));
        CHECK(MemPool::isValidAlignment(4096));
        CHECK_FALSE(MemPool::isValidAlignment(0));
        CHECK_FALSE(MemPool::isValidAlignment(3));
        CHECK_FALSE(MemPool::isValidAlignment(5));
        CHECK_FALSE(MemPool::isValidAlignment(6));
        CHECK_FALSE(MemPool::isValidAlignment(7));
        CHECK_FALSE(MemPool::isValidAlignment(9));
        CHECK_FALSE(MemPool::isValidAlignment(100));
}

// ============================================================================
// Basic allocation and free
// ============================================================================

TEST_CASE("MemPool_AllocateBasic") {
        MemPool pool;
        pool.addRegion(0x10000, 1024);

        void *block = pool.allocate(128);
        REQUIRE(block != nullptr);

        auto stats = pool.stats();
        CHECK(stats.numAllocatedBlocks == 1);
        CHECK(stats.totalUsed == 128);
        CHECK(stats.totalFree == 1024 - 128);
}

TEST_CASE("MemPool_AllocateAndFree") {
        MemPool pool;
        pool.addRegion(0x10000, 1024);

        void *block = pool.allocate(256);
        REQUIRE(block != nullptr);

        pool.free(block);

        auto stats = pool.stats();
        CHECK(stats.totalFree == 1024);
        CHECK(stats.totalUsed == 0);
        CHECK(stats.numFreeBlocks == 1);
        CHECK(stats.numAllocatedBlocks == 0);
}

TEST_CASE("MemPool_AllocateMultiple") {
        MemPool pool;
        pool.addRegion(0x10000, 4096);

        void *b1 = pool.allocate(128);
        void *b2 = pool.allocate(256);
        void *b3 = pool.allocate(512);
        REQUIRE(b1 != nullptr);
        REQUIRE(b2 != nullptr);
        REQUIRE(b3 != nullptr);

        auto stats = pool.stats();
        CHECK(stats.numAllocatedBlocks == 3);
}

// ============================================================================
// Aligned allocation
// ============================================================================

TEST_CASE("MemPool_AllocateAligned") {
        MemPool pool;
        pool.addRegion(0x10000, 4096);

        void *block = pool.allocate(128, 16);
        REQUIRE(block != nullptr);
        CHECK((reinterpret_cast<uintptr_t>(block) % 16) == 0);

        void *block2 = pool.allocate(64, 32);
        REQUIRE(block2 != nullptr);
        CHECK((reinterpret_cast<uintptr_t>(block2) % 32) == 0);

        void *block3 = pool.allocate(32, 64);
        REQUIRE(block3 != nullptr);
        CHECK((reinterpret_cast<uintptr_t>(block3) % 64) == 0);
}

TEST_CASE("MemPool_InvalidAlignment") {
        MemPool pool;
        pool.addRegion(0x10000, 4096);
        void *block = pool.allocate(128, 3); // 3 is not a power of 2
        CHECK(block == nullptr);
}

// ============================================================================
// Allocation failure
// ============================================================================

TEST_CASE("MemPool_AllocateTooLarge") {
        MemPool pool;
        pool.addRegion(0x10000, 256);
        void *block = pool.allocate(512);
        CHECK(block == nullptr);
}

TEST_CASE("MemPool_AllocateExhaustion") {
        MemPool pool;
        pool.addRegion(0x10000, 256);

        void *b1 = pool.allocate(128);
        void *b2 = pool.allocate(128);
        REQUIRE(b1 != nullptr);
        REQUIRE(b2 != nullptr);

        // Pool should be exhausted
        void *b3 = pool.allocate(1);
        CHECK(b3 == nullptr);
}

// ============================================================================
// Free coalescing
// ============================================================================

TEST_CASE("MemPool_FreeCoalescing") {
        MemPool pool;
        pool.addRegion(0x10000, 1024);

        void *b1 = pool.allocate(256);
        void *b2 = pool.allocate(256);
        void *b3 = pool.allocate(256);
        REQUIRE(b1 != nullptr);
        REQUIRE(b2 != nullptr);
        REQUIRE(b3 != nullptr);

        // Free middle block
        pool.free(b2);
        auto stats = pool.stats();
        CHECK(stats.numAllocatedBlocks == 2);

        // Free first block — should coalesce with middle
        pool.free(b1);
        stats = pool.stats();
        CHECK(stats.numAllocatedBlocks == 1);

        // Free last allocated block — should coalesce everything
        pool.free(b3);
        stats = pool.stats();
        CHECK(stats.numAllocatedBlocks == 0);
        CHECK(stats.numFreeBlocks == 1);
        CHECK(stats.totalFree == 1024);
}

TEST_CASE("MemPool_FreeReverseOrder") {
        MemPool pool;
        pool.addRegion(0x10000, 1024);

        void *b1 = pool.allocate(128);
        void *b2 = pool.allocate(128);
        void *b3 = pool.allocate(128);
        REQUIRE(b1 != nullptr);
        REQUIRE(b2 != nullptr);
        REQUIRE(b3 != nullptr);

        pool.free(b3);
        pool.free(b2);
        pool.free(b1);

        auto stats = pool.stats();
        CHECK(stats.totalFree == 1024);
        CHECK(stats.numFreeBlocks == 1);
        CHECK(stats.numAllocatedBlocks == 0);
}

// ============================================================================
// Free nullptr (no-op)
// ============================================================================

TEST_CASE("MemPool_FreeNullptr") {
        MemPool pool;
        pool.addRegion(0x10000, 1024);
        // Should not crash
        pool.free(nullptr);
        auto stats = pool.stats();
        CHECK(stats.totalFree == 1024);
}

// ============================================================================
// memoryMap
// ============================================================================

TEST_CASE("MemPool_MemoryMap") {
        MemPool pool;
        pool.addRegion(0x10000, 1024);

        void *b1 = pool.allocate(256);
        REQUIRE(b1 != nullptr);

        auto map = pool.memoryMap();
        CHECK(map.size() == 2); // 1 allocated + 1 free
}

// ============================================================================
// Block helper methods
// ============================================================================

TEST_CASE("MemPool_Block_AlignedAddress") {
        MemPool::Block block;
        block.address = 0x10001;
        block.alignment = 16;

        CHECK(block.alignedAddress(16) == 0x10010);
        CHECK(block.alignedAddress(1) == 0x10001);
        CHECK(block.alignedAddress(4) == 0x10004);
}

TEST_CASE("MemPool_Block_Padding") {
        MemPool::Block block;
        block.address = 0x10001;

        CHECK(block.padding(1) == 0);
        CHECK(block.padding(16) == 15);
        CHECK(block.padding(4) == 3);
}

TEST_CASE("MemPool_Block_Follows") {
        MemPool::Block a;
        a.address = 0x10000;
        a.size = 256;

        MemPool::Block b;
        b.address = 0x10100; // 0x10000 + 256
        b.size = 128;

        CHECK(b.follows(a));
        CHECK_FALSE(a.follows(b));
}

TEST_CASE("MemPool_Block_Ordering") {
        MemPool::Block a;
        a.address = 0x10000;

        MemPool::Block b;
        b.address = 0x20000;

        CHECK(a < b);
        CHECK_FALSE(b < a);
}

// ============================================================================
// dump (should not crash)
// ============================================================================

TEST_CASE("MemPool_Dump") {
        MemPool pool;
        pool.setName("DumpTest");
        pool.addRegion(0x10000, 1024);
        void *b1 = pool.allocate(128, 16);
        REQUIRE(b1 != nullptr);
        // Just verify dump doesn't crash
        pool.dump();
        CHECK(true);
}

// ============================================================================
// Allocate exact pool size
// ============================================================================

TEST_CASE("MemPool_AllocateExactSize") {
        MemPool pool;
        pool.addRegion(0x10000, 512);
        void *block = pool.allocate(512);
        REQUIRE(block != nullptr);

        auto stats = pool.stats();
        CHECK(stats.totalUsed == 512);
        CHECK(stats.totalFree == 0);
        CHECK(stats.numFreeBlocks == 0);
        CHECK(stats.numAllocatedBlocks == 1);

        pool.free(block);
        stats = pool.stats();
        CHECK(stats.totalFree == 512);
        CHECK(stats.numFreeBlocks == 1);
}

// ============================================================================
// Reuse after free
// ============================================================================

TEST_CASE("MemPool_ReuseAfterFree") {
        MemPool pool;
        pool.addRegion(0x10000, 256);

        void *b1 = pool.allocate(256);
        REQUIRE(b1 != nullptr);

        // Can't allocate when full
        CHECK(pool.allocate(1) == nullptr);

        pool.free(b1);

        // Should be able to allocate again
        void *b2 = pool.allocate(256);
        REQUIRE(b2 != nullptr);

        pool.free(b2);
}
