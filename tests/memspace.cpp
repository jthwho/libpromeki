/**
 * @file      memspace.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstring>
#include <doctest/doctest.h>
#include <promeki/memspace.h>

using namespace promeki;

TEST_CASE("MemSpace: default construction") {
        MemSpace ms;
        // Default should be System/Default
        CHECK(true); // Construction doesn't crash
}

TEST_CASE("MemSpace: construction with System ID") {
        MemSpace ms(MemSpace::System);
        CHECK(true);
}

TEST_CASE("MemSpace: alloc and release") {
        MemSpace ms;
        MemAllocation a = ms.alloc(256, 16);
        CHECK(a.isValid());
        CHECK(a.size == 256);
        CHECK(a.align == 16);
        ms.release(a);
        CHECK(!a.isValid());
}

TEST_CASE("MemSpace: alloc alignment") {
        MemSpace ms;
        MemAllocation a = ms.alloc(1024, 64);
        CHECK(a.isValid());
        CHECK((reinterpret_cast<uintptr_t>(a.ptr) % 64) == 0);
        ms.release(a);
}

TEST_CASE("MemSpace: release nullptr is safe") {
        MemSpace ms;
        MemAllocation a;
        ms.release(a);
        CHECK(true);
}

TEST_CASE("MemSpace: fill memory") {
        MemSpace ms;
        MemAllocation a = ms.alloc(128, 16);
        REQUIRE(a.isValid());
        CHECK(ms.fill(a.ptr, 128, 0xAA).isOk());
        unsigned char *bytes = static_cast<unsigned char *>(a.ptr);
        CHECK(bytes[0] == 0xAA);
        CHECK(bytes[127] == 0xAA);
        ms.release(a);
}

TEST_CASE("MemSpace: fill with nullptr returns error") {
        MemSpace ms;
        CHECK(ms.fill(nullptr, 128, 0).isError());
}

TEST_CASE("MemSpace: copy") {
        MemSpace ms;
        MemAllocation src = ms.alloc(128, 16);
        MemAllocation dst = ms.alloc(128, 16);
        REQUIRE(src.isValid());
        REQUIRE(dst.isValid());
        CHECK(ms.fill(src.ptr, 128, 0x42).isOk());
        CHECK(ms.copy(src, dst, 128));
        unsigned char *d = static_cast<unsigned char *>(dst.ptr);
        CHECK(d[0] == 0x42);
        CHECK(d[127] == 0x42);
        ms.release(src);
        ms.release(dst);
}

TEST_CASE("MemSpace: copy with nullptr returns false") {
        MemSpace ms;
        MemAllocation a = ms.alloc(64, 16);
        MemAllocation empty;
        REQUIRE(a.isValid());
        CHECK_FALSE(ms.copy(empty, a, 64));
        CHECK_FALSE(ms.copy(a, empty, 64));
        ms.release(a);
}

TEST_CASE("MemSpace: alloc preserves MemSpace") {
        MemSpace ms(MemSpace::SystemSecure);
        MemAllocation a = ms.alloc(256, 16);
        REQUIRE(a.isValid());
        CHECK(a.ms.id() == MemSpace::SystemSecure);
        ms.release(a);
}

TEST_CASE("MemSpace: isHostAccessible") {
        MemSpace ms;
        MemAllocation a = ms.alloc(64, 16);
        REQUIRE(a.isValid());
        CHECK(ms.isHostAccessible(a));
        ms.release(a);
}
