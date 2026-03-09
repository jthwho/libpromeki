/**
 * @file      buffer.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information
 */

#include <cstring>
#include <doctest/doctest.h>
#include <promeki/buffer.h>

using namespace promeki;

// ============================================================================
// Default construction
// ============================================================================

TEST_CASE("Buffer_Default") {
    Buffer b;
    CHECK(!b.isValid());
    CHECK(b.data() == nullptr);
    CHECK(b.size() == 0);
    CHECK(b.referenceCount() == 1);
}

// ============================================================================
// Allocated buffer
// ============================================================================

TEST_CASE("Buffer_Allocate") {
    Buffer b(1024);
    CHECK(b.isValid());
    CHECK(b.data() != nullptr);
    CHECK(b.size() == 1024);
    CHECK(b.referenceCount() == 1);
}

TEST_CASE("Buffer_AllocateWithAlign") {
    Buffer b(4096, 64);
    CHECK(b.isValid());
    CHECK(b.size() == 4096);
    CHECK(b.align() == 64);
    // Verify alignment
    uintptr_t addr = reinterpret_cast<uintptr_t>(b.data());
    CHECK(addr % 64 == 0);
}

// ============================================================================
// External memory (non-owned)
// ============================================================================

TEST_CASE("Buffer_External") {
    char mem[256];
    Buffer b(mem, sizeof(mem), 1, false);
    CHECK(b.isValid());
    CHECK(b.data() == mem);
    CHECK(b.size() == sizeof(mem));
}

// ============================================================================
// Fill
// ============================================================================

TEST_CASE("Buffer_Fill") {
    Buffer b(128);
    REQUIRE(b.isValid());
    CHECK(b.fill(0xAB));
    const uint8_t *ptr = static_cast<const uint8_t *>(b.data());
    CHECK(ptr[0] == 0xAB);
    CHECK(ptr[63] == 0xAB);
    CHECK(ptr[127] == 0xAB);
}

TEST_CASE("Buffer_FillZero") {
    Buffer b(64);
    CHECK(b.fill(0));
    const uint8_t *ptr = static_cast<const uint8_t *>(b.data());
    for(size_t i = 0; i < 64; i++) {
        CHECK(ptr[i] == 0);
    }
}

// ============================================================================
// Shared (no COW)
// ============================================================================

TEST_CASE("Buffer_SharedCopy") {
    Buffer b1(256);
    CHECK(b1.fill(0x42));

    Buffer b2 = b1;
    CHECK(b1.referenceCount() == 2);
    CHECK(b2.referenceCount() == 2);
    // Both point to the same memory (no COW)
    CHECK(b1.data() == b2.data());
    CHECK(b1.size() == b2.size());
}

TEST_CASE("Buffer_SharedMultiple") {
    Buffer b1(128);
    Buffer b2 = b1;
    Buffer b3 = b1;
    CHECK(b1.referenceCount() == 3);
    CHECK(b2.referenceCount() == 3);
    CHECK(b3.referenceCount() == 3);
}

// ============================================================================
// ShiftData
// ============================================================================

TEST_CASE("Buffer_ShiftData") {
    Buffer b(1024);
    CHECK(b.fill(0));
    void *orig = b.data();

    b.shiftData(64);
    uintptr_t shifted = reinterpret_cast<uintptr_t>(b.data());
    uintptr_t original = reinterpret_cast<uintptr_t>(orig);
    CHECK(shifted == original + 64);
}

// ============================================================================
// SetOwnershipEnabled
// ============================================================================

TEST_CASE("Buffer_SetOwnership") {
    // Allocate a buffer, then disable ownership so it won't free
    // (We can't easily verify the free doesn't happen, but we can
    //  verify the call doesn't crash and the buffer remains valid)
    char mem[64];
    Buffer b(mem, sizeof(mem), 1, true);
    b.setOwnershipEnabled(false);
    // Buffer destructor should not try to free stack memory
}

// ============================================================================
// Write and read back
// ============================================================================

TEST_CASE("Buffer_WriteRead") {
    Buffer b(sizeof(int) * 10);
    int *p = static_cast<int *>(b.data());
    for(int i = 0; i < 10; i++) {
        p[i] = i * 100;
    }
    const int *cp = static_cast<const int *>(b.data());
    for(int i = 0; i < 10; i++) {
        CHECK(cp[i] == i * 100);
    }
}

// ============================================================================
// Page size
// ============================================================================

TEST_CASE("Buffer_PageSize") {
    size_t ps = Buffer::getPageSize();
    CHECK(ps > 0);
    // Page size should be a power of two
    CHECK((ps & (ps - 1)) == 0);
}

// ============================================================================
// DefaultAlign
// ============================================================================

TEST_CASE("Buffer_DefaultAlign") {
    CHECK(Buffer::DefaultAlign == Buffer::getPageSize());
}

// ============================================================================
// List type
// ============================================================================

TEST_CASE("Buffer_List") {
    Buffer::List list;
    list.push_back(Buffer(64));
    list.push_back(Buffer(128));
    list.push_back(Buffer(256));
    CHECK(list.size() == 3);
    CHECK(list[0].size() == 64);
    CHECK(list[1].size() == 128);
    CHECK(list[2].size() == 256);
}
