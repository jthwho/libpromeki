/**
 * @file      buffer.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstring>
#include <doctest/doctest.h>
#include <promeki/buffer.h>
#include <promeki/string.h>

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
        Buffer b = Buffer::wrapHost(mem, sizeof(mem), 1);
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
// Copy semantics — refcount-shared by default, ensureExclusive detaches
// ============================================================================

TEST_CASE("Buffer_CopyIsRefcountShared") {
        Buffer b1(256);
        b1.setSize(256);
        CHECK(b1.fill(0x42).isOk());

        Buffer b2 = b1;
        // Both handles point at the same backing storage.
        CHECK(b2.isValid());
        CHECK(b2.size() == b1.size());
        CHECK(b2.availSize() == b1.availSize());
        CHECK(b2.data() == b1.data());
        CHECK(b1.impl().referenceCount() == 2);

        // After ensureExclusive on b2, b1 keeps the original; b2 has a private clone.
        b2.ensureExclusive();
        CHECK(b1.impl().referenceCount() == 1);
        CHECK(b2.impl().referenceCount() == 1);
        CHECK(b2.data() != b1.data());

        // Contents were carried across the clone.
        const uint8_t *p1 = static_cast<const uint8_t *>(b1.data());
        const uint8_t *p2 = static_cast<const uint8_t *>(b2.data());
        CHECK(p2[0] == 0x42);
        CHECK(p2[255] == 0x42);

        // Mutating the now-private b2 does not affect b1.
        b2.fill(0x00);
        CHECK(p1[0] == 0x42);
        CHECK(p2[0] == 0x00);
}

// ============================================================================
// Shared ownership via Buffer
// ============================================================================

TEST_CASE("Buffer_SharedPtr") {
        auto b1 = Buffer(256);
        CHECK(b1.fill(0x42).isOk());
        CHECK(b1.impl().referenceCount() == 1);

        Buffer b2 = b1;
        CHECK(b1.impl().referenceCount() == 2);
        CHECK(b2.impl().referenceCount() == 2);
        // Both point to the same buffer
        CHECK(b1.data() == b2.data());
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
        Buffer b = Buffer::wrapHost(mem, sizeof(mem), 1);
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
// PtrList type (List<Buffer>)
// ============================================================================

TEST_CASE("Buffer_List") {
        Buffer::List list;
        list.pushToBack(Buffer(64));
        list.pushToBack(Buffer(128));
        list.pushToBack(Buffer(256));
        CHECK(list.size() == 3);
        CHECK(list[0].availSize() == 64);
        CHECK(list[1].availSize() == 128);
        CHECK(list[2].availSize() == 256);
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

// ============================================================================
// Generic seal / residentBytes / isCowBacked / isShared (Phase B.1)
// ============================================================================

TEST_CASE("Buffer_seal_default_isOk") {
        // The default BufferImpl::seal() returns Ok unconditionally, so
        // call sites can issue seal() without knowing the backend.
        Buffer b(1024);
        REQUIRE(b.isValid());
        CHECK(b.seal() == Error::Ok);
        CHECK(b.seal() == Error::Ok); // idempotent default
}

TEST_CASE("Buffer_seal_invalid_returnsInvalid") {
        Buffer b;
        CHECK(b.seal() == Error::Invalid);
}

TEST_CASE("Buffer_isCowBacked_defaultBackend_false") {
        Buffer b(1024);
        REQUIRE(b.isValid());
        CHECK_FALSE(b.isCowBacked());

        Buffer empty;
        CHECK_FALSE(empty.isCowBacked());
}

TEST_CASE("Buffer_residentBytes_defaultBackend_equalsAllocSize") {
        // For HostBufferImpl, the entire allocation is resident.
        Buffer b(4096);
        REQUIRE(b.isValid());
        CHECK(b.residentBytes() == b.allocSize());

        Buffer empty;
        CHECK(empty.residentBytes() == 0);
}

TEST_CASE("Buffer_isShared_singleHandle_false") {
        Buffer b(1024);
        REQUIRE(b.isValid());
        CHECK_FALSE(b.isShared());
        CHECK(b.isExclusive());
}

TEST_CASE("Buffer_isShared_twoHandles_true") {
        Buffer a(1024);
        Buffer b = a; // copy = refcount bump
        CHECK(a.isShared());
        CHECK(b.isShared());
        CHECK_FALSE(a.isExclusive());
        CHECK_FALSE(b.isExclusive());
}

TEST_CASE("Buffer_isShared_afterHandleDrops_false") {
        Buffer a(1024);
        {
                Buffer b = a;
                CHECK(a.isShared());
        }
        CHECK_FALSE(a.isShared());
        CHECK(a.isExclusive());
}

TEST_CASE("Buffer_isShared_invalid_false") {
        Buffer b;
        CHECK_FALSE(b.isShared());
        CHECK_FALSE(b.isExclusive());
}

// ============================================================================
// Hex string round-trip
// ============================================================================

namespace {

        Buffer fromBytesLiteral(std::initializer_list<uint8_t> bytes) {
                Buffer b(bytes.size() == 0 ? 1 : bytes.size());
                b.setSize(bytes.size());
                if (bytes.size() > 0) {
                        std::vector<uint8_t> v(bytes);
                        b.copyFrom(v.data(), v.size(), 0);
                }
                return b;
        }

}

TEST_CASE("Buffer::toHex emits a lowercase space-separated dump by default") {
        Buffer b = fromBytesLiteral({0x00, 0x10, 0xAB, 0xFF});
        CHECK(b.toHex() == "00 10 ab ff");
}

TEST_CASE("Buffer::toHex with empty separator emits a contiguous run of nibbles") {
        Buffer b = fromBytesLiteral({0xDE, 0xAD, 0xBE, 0xEF});
        CHECK(b.toHex(String("")) == "deadbeef");
}

TEST_CASE("Buffer::toHex with custom separator inserts it between every byte pair") {
        Buffer b = fromBytesLiteral({0x12, 0x34, 0x56});
        CHECK(b.toHex(String("-")) == "12-34-56");
}

TEST_CASE("Buffer::toHex on an empty buffer returns the empty string") {
        Buffer b;
        CHECK(b.toHex() == "");
        Buffer b2(8);
        b2.setSize(0);
        CHECK(b2.toHex() == "");
}

TEST_CASE("Buffer::fromHex parses lowercase + uppercase + space-separated hex") {
        auto [out, err] = Buffer::fromHex(String("DE ad BE ef"));
        REQUIRE(err.isOk());
        REQUIRE(out.size() == 4);
        const auto *p = static_cast<const uint8_t *>(out.data());
        CHECK(p[0] == 0xDE);
        CHECK(p[1] == 0xAD);
        CHECK(p[2] == 0xBE);
        CHECK(p[3] == 0xEF);
}

TEST_CASE("Buffer::fromHex tolerates dash and tab separators between bytes") {
        auto [out, err] = Buffer::fromHex(String("01-02\t03 04"));
        REQUIRE(err.isOk());
        REQUIRE(out.size() == 4);
        const auto *p = static_cast<const uint8_t *>(out.data());
        CHECK(p[0] == 0x01);
        CHECK(p[1] == 0x02);
        CHECK(p[2] == 0x03);
        CHECK(p[3] == 0x04);
}

TEST_CASE("Buffer::fromHex parses contiguous nibbles with no separator") {
        auto [out, err] = Buffer::fromHex(String("deadbeef"));
        REQUIRE(err.isOk());
        REQUIRE(out.size() == 4);
}

TEST_CASE("Buffer::fromHex returns an empty buffer for an empty / whitespace-only string") {
        auto [out, err] = Buffer::fromHex(String(""));
        REQUIRE(err.isOk());
        CHECK(out.size() == 0);
        auto [out2, err2] = Buffer::fromHex(String(" \t\n - "));
        REQUIRE(err2.isOk());
        CHECK(out2.size() == 0);
}

TEST_CASE("Buffer::fromHex fails on odd nibble count") {
        auto [out, err] = Buffer::fromHex(String("abc"));
        CHECK(err.isError());
        CHECK(err == Error::ParseFailed);
}

TEST_CASE("Buffer::fromHex fails on non-hex / non-skip characters") {
        auto [out, err] = Buffer::fromHex(String("ab xx 12"));
        CHECK(err.isError());
        CHECK(err == Error::ParseFailed);
}

TEST_CASE("Buffer::toHex / Buffer::fromHex round-trip is identity") {
        Buffer original = fromBytesLiteral({0x00, 0x7F, 0x80, 0xFF, 0x55, 0xAA, 0x10, 0x20, 0x30});
        String hex = original.toHex();
        auto [restored, err] = Buffer::fromHex(hex);
        REQUIRE(err.isOk());
        REQUIRE(restored.size() == original.size());
        const auto *o = static_cast<const uint8_t *>(original.data());
        const auto *r = static_cast<const uint8_t *>(restored.data());
        for (size_t i = 0; i < original.size(); ++i) CHECK(r[i] == o[i]);
}
