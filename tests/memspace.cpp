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
        void *ptr = ms.alloc(256, 16);
        CHECK(ptr != nullptr);
        ms.release(ptr);
}

TEST_CASE("MemSpace: alloc alignment") {
        MemSpace ms;
        void *ptr = ms.alloc(1024, 64);
        CHECK(ptr != nullptr);
        CHECK((reinterpret_cast<uintptr_t>(ptr) % 64) == 0);
        ms.release(ptr);
}

TEST_CASE("MemSpace: release nullptr is safe") {
        MemSpace ms;
        ms.release(nullptr);
        CHECK(true);
}

TEST_CASE("MemSpace: set memory") {
        MemSpace ms;
        void *ptr = ms.alloc(128, 16);
        REQUIRE(ptr != nullptr);
        CHECK(ms.set(ptr, 128, 0xAA));
        unsigned char *bytes = static_cast<unsigned char *>(ptr);
        CHECK(bytes[0] == 0xAA);
        CHECK(bytes[127] == 0xAA);
        ms.release(ptr);
}

TEST_CASE("MemSpace: set with nullptr returns false") {
        MemSpace ms;
        CHECK_FALSE(ms.set(nullptr, 128, 0));
}

TEST_CASE("MemSpace: copy") {
        MemSpace ms;
        void *src = ms.alloc(128, 16);
        void *dst = ms.alloc(128, 16);
        REQUIRE(src != nullptr);
        REQUIRE(dst != nullptr);
        ms.set(src, 128, 0x42);
        CHECK(ms.copy(MemSpace::System, dst, src, 128));
        unsigned char *d = static_cast<unsigned char *>(dst);
        CHECK(d[0] == 0x42);
        CHECK(d[127] == 0x42);
        ms.release(src);
        ms.release(dst);
}

TEST_CASE("MemSpace: copy with nullptr returns false") {
        MemSpace ms;
        void *buf = ms.alloc(64, 16);
        REQUIRE(buf != nullptr);
        CHECK_FALSE(ms.copy(MemSpace::System, nullptr, buf, 64));
        CHECK_FALSE(ms.copy(MemSpace::System, buf, nullptr, 64));
        ms.release(buf);
}
