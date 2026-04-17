/**
 * @file      fnv1a.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <cstring>
#include <promeki/fnv1a.h>

using namespace promeki;

TEST_CASE("FNV-1a: empty string") {
        // FNV-1a of "" is the offset basis itself.
        constexpr uint64_t h = fnv1a("");
        CHECK(h == 0xcbf29ce484222325ULL);
}

TEST_CASE("FNV-1a: known test vectors") {
        // Well-known FNV-1a 64-bit test vectors.
        CHECK(fnv1a("a")           == 0xaf63dc4c8601ec8cULL);
        CHECK(fnv1a("foobar")      == 0x85944171f73967e8ULL);
}

TEST_CASE("FNV-1a: deterministic") {
        constexpr uint64_t h1 = fnv1a("hello world");
        constexpr uint64_t h2 = fnv1a("hello world");
        CHECK(h1 == h2);
}

TEST_CASE("FNV-1a: different strings differ") {
        constexpr uint64_t h1 = fnv1a("abc");
        constexpr uint64_t h2 = fnv1a("abd");
        CHECK(h1 != h2);
}

TEST_CASE("FNV-1a: compile-time evaluation") {
        // Verify the function works in a constexpr context.
        static_assert(fnv1a("") == 0xcbf29ce484222325ULL);
        static_assert(fnv1a("a") != fnv1a("b"));
        CHECK(true);
}

TEST_CASE("FNV-1a: fnv1aData") {
        const char *msg = "hello";
        size_t len = std::strlen(msg);
        // fnv1a stops before the null byte, so fnv1aData with the
        // same length should produce the same hash.
        uint64_t h1 = fnv1a(msg);
        uint64_t h2 = fnv1aData(msg, len);
        CHECK(h1 == h2);
}

TEST_CASE("FNV-1a: custom seed") {
        uint64_t h1 = fnv1a("test");
        uint64_t h2 = fnv1a("test", 42ULL);
        CHECK(h1 != h2);
}
