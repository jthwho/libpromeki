/**
 * @file      buffer.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information
 */

#include <cstring>
#include <promeki/unittest.h>
#include <promeki/buffer.h>

using namespace promeki;

// ============================================================================
// Default construction
// ============================================================================

PROMEKI_TEST_BEGIN(Buffer_Default)
    Buffer b;
    PROMEKI_TEST(!b.isValid());
    PROMEKI_TEST(b.data() == nullptr);
    PROMEKI_TEST(b.size() == 0);
    PROMEKI_TEST(b.referenceCount() == 1);
PROMEKI_TEST_END()

// ============================================================================
// Allocated buffer
// ============================================================================

PROMEKI_TEST_BEGIN(Buffer_Allocate)
    Buffer b(1024);
    PROMEKI_TEST(b.isValid());
    PROMEKI_TEST(b.data() != nullptr);
    PROMEKI_TEST(b.size() == 1024);
    PROMEKI_TEST(b.referenceCount() == 1);
PROMEKI_TEST_END()

PROMEKI_TEST_BEGIN(Buffer_AllocateWithAlign)
    Buffer b(4096, 64);
    PROMEKI_TEST(b.isValid());
    PROMEKI_TEST(b.size() == 4096);
    PROMEKI_TEST(b.align() == 64);
    // Verify alignment
    uintptr_t addr = reinterpret_cast<uintptr_t>(b.data());
    PROMEKI_TEST(addr % 64 == 0);
PROMEKI_TEST_END()

// ============================================================================
// External memory (non-owned)
// ============================================================================

PROMEKI_TEST_BEGIN(Buffer_External)
    char mem[256];
    Buffer b(mem, sizeof(mem), 1, false);
    PROMEKI_TEST(b.isValid());
    PROMEKI_TEST(b.data() == mem);
    PROMEKI_TEST(b.size() == sizeof(mem));
PROMEKI_TEST_END()

// ============================================================================
// Fill
// ============================================================================

PROMEKI_TEST_BEGIN(Buffer_Fill)
    Buffer b(128);
    PROMEKI_TEST(b.isValid());
    PROMEKI_TEST(b.fill(0xAB));
    const uint8_t *ptr = static_cast<const uint8_t *>(b.data());
    PROMEKI_TEST(ptr[0] == 0xAB);
    PROMEKI_TEST(ptr[63] == 0xAB);
    PROMEKI_TEST(ptr[127] == 0xAB);
PROMEKI_TEST_END()

PROMEKI_TEST_BEGIN(Buffer_FillZero)
    Buffer b(64);
    PROMEKI_TEST(b.fill(0));
    const uint8_t *ptr = static_cast<const uint8_t *>(b.data());
    for(size_t i = 0; i < 64; i++) {
        PROMEKI_TEST(ptr[i] == 0);
    }
PROMEKI_TEST_END()

// ============================================================================
// Shared (no COW)
// ============================================================================

PROMEKI_TEST_BEGIN(Buffer_SharedCopy)
    Buffer b1(256);
    PROMEKI_TEST(b1.fill(0x42));

    Buffer b2 = b1;
    PROMEKI_TEST(b1.referenceCount() == 2);
    PROMEKI_TEST(b2.referenceCount() == 2);
    // Both point to the same memory (no COW)
    PROMEKI_TEST(b1.data() == b2.data());
    PROMEKI_TEST(b1.size() == b2.size());
PROMEKI_TEST_END()

PROMEKI_TEST_BEGIN(Buffer_SharedMultiple)
    Buffer b1(128);
    Buffer b2 = b1;
    Buffer b3 = b1;
    PROMEKI_TEST(b1.referenceCount() == 3);
    PROMEKI_TEST(b2.referenceCount() == 3);
    PROMEKI_TEST(b3.referenceCount() == 3);
PROMEKI_TEST_END()

// ============================================================================
// ShiftData
// ============================================================================

PROMEKI_TEST_BEGIN(Buffer_ShiftData)
    Buffer b(1024);
    PROMEKI_TEST(b.fill(0));
    void *orig = b.data();

    b.shiftData(64);
    uintptr_t shifted = reinterpret_cast<uintptr_t>(b.data());
    uintptr_t original = reinterpret_cast<uintptr_t>(orig);
    PROMEKI_TEST(shifted == original + 64);
PROMEKI_TEST_END()

// ============================================================================
// SetOwnershipEnabled
// ============================================================================

PROMEKI_TEST_BEGIN(Buffer_SetOwnership)
    // Allocate a buffer, then disable ownership so it won't free
    // (We can't easily verify the free doesn't happen, but we can
    //  verify the call doesn't crash and the buffer remains valid)
    char mem[64];
    Buffer b(mem, sizeof(mem), 1, true);
    b.setOwnershipEnabled(false);
    // Buffer destructor should not try to free stack memory
PROMEKI_TEST_END()

// ============================================================================
// Write and read back
// ============================================================================

PROMEKI_TEST_BEGIN(Buffer_WriteRead)
    Buffer b(sizeof(int) * 10);
    int *p = static_cast<int *>(b.data());
    for(int i = 0; i < 10; i++) {
        p[i] = i * 100;
    }
    const int *cp = static_cast<const int *>(b.data());
    for(int i = 0; i < 10; i++) {
        PROMEKI_TEST(cp[i] == i * 100);
    }
PROMEKI_TEST_END()

// ============================================================================
// Page size
// ============================================================================

PROMEKI_TEST_BEGIN(Buffer_PageSize)
    size_t ps = Buffer::getPageSize();
    PROMEKI_TEST(ps > 0);
    // Page size should be a power of two
    PROMEKI_TEST((ps & (ps - 1)) == 0);
PROMEKI_TEST_END()

// ============================================================================
// DefaultAlign
// ============================================================================

PROMEKI_TEST_BEGIN(Buffer_DefaultAlign)
    PROMEKI_TEST(Buffer::DefaultAlign == Buffer::getPageSize());
PROMEKI_TEST_END()

// ============================================================================
// List type
// ============================================================================

PROMEKI_TEST_BEGIN(Buffer_List)
    Buffer::List list;
    list.push_back(Buffer(64));
    list.push_back(Buffer(128));
    list.push_back(Buffer(256));
    PROMEKI_TEST(list.size() == 3);
    PROMEKI_TEST(list[0].size() == 64);
    PROMEKI_TEST(list[1].size() == 128);
    PROMEKI_TEST(list[2].size() == 256);
PROMEKI_TEST_END()
