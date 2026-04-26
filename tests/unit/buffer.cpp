/**
 * @file      buffer.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
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
}

// ============================================================================
// Allocated buffer
// ============================================================================

TEST_CASE("Buffer_Allocate") {
        Buffer b(1024);
        CHECK(b.isValid());
        CHECK(b.data() != nullptr);
        CHECK(b.size() == 0);
        CHECK(b.availSize() == 1024);
        CHECK(b.allocSize() == 1024);
}

TEST_CASE("Buffer_AllocateWithAlign") {
        Buffer b(4096, 64);
        CHECK(b.isValid());
        CHECK(b.size() == 0);
        CHECK(b.availSize() == 4096);
        CHECK(b.align() == 64);
        // Verify alignment
        uintptr_t addr = reinterpret_cast<uintptr_t>(b.data());
        CHECK(addr % 64 == 0);
}

// ============================================================================
// External memory (non-owned)
// ============================================================================

TEST_CASE("Buffer_External") {
        char   mem[256];
        Buffer b = Buffer::wrap(mem, sizeof(mem), 1);
        CHECK(b.isValid());
        CHECK(b.data() == mem);
        CHECK(b.size() == 0);
        CHECK(b.availSize() == sizeof(mem));
}

// ============================================================================
// Fill
// ============================================================================

TEST_CASE("Buffer_Fill") {
        Buffer b(128);
        REQUIRE(b.isValid());
        CHECK(b.fill(0xAB).isOk());
        const uint8_t *ptr = static_cast<const uint8_t *>(b.data());
        CHECK(ptr[0] == 0xAB);
        CHECK(ptr[63] == 0xAB);
        CHECK(ptr[127] == 0xAB);
}

TEST_CASE("Buffer_FillZero") {
        Buffer b(64);
        CHECK(b.fill(0).isOk());
        const uint8_t *ptr = static_cast<const uint8_t *>(b.data());
        for (size_t i = 0; i < 64; i++) {
                CHECK(ptr[i] == 0);
        }
}

// ============================================================================
// Copy semantics (deep copy, independent buffers)
// ============================================================================

TEST_CASE("Buffer_CopyIsIndependent") {
        Buffer b1(256);
        b1.setSize(256);
        CHECK(b1.fill(0x42).isOk());

        Buffer b2 = b1;
        // Deep copy — different memory, same content and size
        CHECK(b2.isValid());
        CHECK(b2.size() == b1.size());
        CHECK(b2.availSize() == b1.availSize());
        CHECK(b2.data() != b1.data());

        const uint8_t *p1 = static_cast<const uint8_t *>(b1.data());
        const uint8_t *p2 = static_cast<const uint8_t *>(b2.data());
        CHECK(p2[0] == 0x42);
        CHECK(p2[255] == 0x42);

        // Mutating b2 does not affect b1
        b2.fill(0x00);
        CHECK(p1[0] == 0x42);
        CHECK(p2[0] == 0x00);
}

// ============================================================================
// Shared ownership via Buffer::Ptr
// ============================================================================

TEST_CASE("Buffer_SharedPtr") {
        auto b1 = Buffer::Ptr::create(256);
        CHECK(b1->fill(0x42).isOk());
        CHECK(b1.referenceCount() == 1);

        Buffer::Ptr b2 = b1;
        CHECK(b1.referenceCount() == 2);
        CHECK(b2.referenceCount() == 2);
        // Both point to the same buffer
        CHECK(b1->data() == b2->data());
}

// ============================================================================
// ShiftData
// ============================================================================

TEST_CASE("Buffer_ShiftData") {
        Buffer b(1024);
        CHECK(b.fill(0).isOk());
        void *orig = b.data();

        b.shiftData(64);
        uintptr_t shifted = reinterpret_cast<uintptr_t>(b.data());
        uintptr_t original = reinterpret_cast<uintptr_t>(orig);
        CHECK(shifted == original + 64);
        CHECK(b.size() == 0);
        CHECK(b.availSize() == 1024 - 64);
        CHECK(b.allocSize() == 1024);
}

TEST_CASE("Buffer_ShiftDataFromBase") {
        Buffer b(1024);
        void  *base = b.odata();

        // First shift
        b.shiftData(64);
        CHECK(b.data() == static_cast<uint8_t *>(base) + 64);
        CHECK(b.availSize() == 1024 - 64);
        CHECK(b.size() == 0);

        // Second shift replaces the first (not accumulating)
        b.shiftData(128);
        CHECK(b.data() == static_cast<uint8_t *>(base) + 128);
        CHECK(b.availSize() == 1024 - 128);

        // Shift back to zero
        b.shiftData(0);
        CHECK(b.data() == base);
        CHECK(b.availSize() == 1024);
}

TEST_CASE("Buffer_SetSize") {
        Buffer b(1024);
        CHECK(b.size() == 0);
        CHECK(b.availSize() == 1024);

        b.setSize(500);
        CHECK(b.size() == 500);

        b.setSize(1024);
        CHECK(b.size() == 1024);

        b.setSize(0);
        CHECK(b.size() == 0);
}

TEST_CASE("Buffer_SetSizeAfterShift") {
        Buffer b(1024);
        b.shiftData(100);
        CHECK(b.availSize() == 924);
        CHECK(b.size() == 0);

        b.setSize(924);
        CHECK(b.size() == 924);

        // Shifting again resets size to 0
        b.shiftData(200);
        CHECK(b.size() == 0);
        CHECK(b.availSize() == 824);
}

TEST_CASE("Buffer_SizePreservedOnCopy") {
        Buffer b1(256);
        b1.setSize(100);

        Buffer b2 = b1;
        CHECK(b2.size() == 100);
        CHECK(b2.availSize() == 256);
}

TEST_CASE("Buffer_SizePreservedOnMove") {
        Buffer b1(256);
        b1.setSize(100);

        Buffer b2 = std::move(b1);
        CHECK(b2.size() == 100);
        CHECK(b1.size() == 0);
}

TEST_CASE("Buffer_IsHostAccessible") {
        Buffer b(64);
        CHECK(b.isHostAccessible());
}

// ============================================================================
// Buffer::wrap (non-owning view of external memory)
// ============================================================================

TEST_CASE("Buffer_Wrap") {
        char   mem[64];
        Buffer b = Buffer::wrap(mem, sizeof(mem), 1);
        CHECK(b.isValid());
        CHECK(b.data() == mem);
        CHECK(b.allocSize() == sizeof(mem));
        // Buffer destructor must not try to free stack memory.
}

// ============================================================================
// Write and read back
// ============================================================================

TEST_CASE("Buffer_WriteRead") {
        Buffer b(sizeof(int) * 10);
        int   *p = static_cast<int *>(b.data());
        for (int i = 0; i < 10; i++) {
                p[i] = i * 100;
        }
        const int *cp = static_cast<const int *>(b.data());
        for (int i = 0; i < 10; i++) {
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
// PtrList type (List<Buffer::Ptr>)
// ============================================================================

TEST_CASE("Buffer_PtrList") {
        Buffer::PtrList list;
        list.pushToBack(Buffer::Ptr::create(64));
        list.pushToBack(Buffer::Ptr::create(128));
        list.pushToBack(Buffer::Ptr::create(256));
        CHECK(list.size() == 3);
        CHECK(list[0]->availSize() == 64);
        CHECK(list[1]->availSize() == 128);
        CHECK(list[2]->availSize() == 256);
}

// ============================================================================
// copyFrom
// ============================================================================

TEST_CASE("Buffer_CopyFrom") {
        SUBCASE("Basic copy") {
                Buffer      b(64);
                const char *src = "Hello, Buffer!";
                size_t      len = std::strlen(src);
                Error       err = b.copyFrom(src, len);
                CHECK(err.isOk());
                CHECK(std::memcmp(b.data(), src, len) == 0);
        }

        SUBCASE("Copy with offset") {
                Buffer b(64);
                b.fill(0);
                const char *src = "test";
                Error       err = b.copyFrom(src, 4, 10);
                CHECK(err.isOk());
                CHECK(std::memcmp(static_cast<uint8_t *>(b.data()) + 10, src, 4) == 0);
        }

        SUBCASE("Copy exceeding availSize returns BufferTooSmall") {
                Buffer b(16);
                char   src[32] = {};
                Error  err = b.copyFrom(src, 32);
                CHECK(err == Error::BufferTooSmall);
        }

        SUBCASE("Copy with offset exceeding availSize returns BufferTooSmall") {
                Buffer b(16);
                char   src[4] = {};
                Error  err = b.copyFrom(src, 4, 14);
                CHECK(err == Error::BufferTooSmall);
        }

        SUBCASE("Copy into invalid buffer returns Invalid") {
                Buffer b;
                char   src[4] = {};
                Error  err = b.copyFrom(src, 4);
                CHECK(err == Error::Invalid);
        }
}
