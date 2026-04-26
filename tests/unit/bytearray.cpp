/**
 * @file      bytearray.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/bytearray.h>

using namespace promeki;

TEST_CASE("ByteArray: default is zero") {
        ByteArray<4> b;
        CHECK(b.isZero());
        CHECK(b[0] == 0);
        CHECK(b[3] == 0);
}

TEST_CASE("ByteArray: size") {
        ByteArray<16> b;
        CHECK(b.size() == 16);
}

TEST_CASE("ByteArray: construct from raw pointer") {
        uint8_t      raw[] = {0xDE, 0xAD, 0xBE, 0xEF};
        ByteArray<4> b(raw);
        CHECK(b[0] == 0xDE);
        CHECK(b[1] == 0xAD);
        CHECK(b[2] == 0xBE);
        CHECK(b[3] == 0xEF);
}

TEST_CASE("ByteArray: toHexString") {
        uint8_t      raw[] = {0xDE, 0xAD, 0xBE, 0xEF};
        ByteArray<4> b(raw);
        CHECK(b.toHexString() == "deadbeef");
}

TEST_CASE("ByteArray: toHexString all zeros") {
        ByteArray<4> b;
        CHECK(b.toHexString() == "00000000");
}

TEST_CASE("ByteArray: fromHexString const char *") {
        ByteArray<4> b = ByteArray<4>::fromHexString("deadbeef");
        CHECK(b[0] == 0xDE);
        CHECK(b[1] == 0xAD);
        CHECK(b[2] == 0xBE);
        CHECK(b[3] == 0xEF);
}

TEST_CASE("ByteArray: fromHexString String") {
        String       s("cafebabe");
        ByteArray<4> b = ByteArray<4>::fromHexString(s);
        CHECK(b[0] == 0xCA);
        CHECK(b[1] == 0xFE);
        CHECK(b[2] == 0xBA);
        CHECK(b[3] == 0xBE);
}

TEST_CASE("ByteArray: fromHexString case insensitive") {
        ByteArray<4> a = ByteArray<4>::fromHexString("DEADBEEF");
        ByteArray<4> b = ByteArray<4>::fromHexString("deadbeef");
        CHECK(a == b);
}

TEST_CASE("ByteArray: fromHexString invalid") {
        Error        err;
        ByteArray<4> b = ByteArray<4>::fromHexString("ZZZZZZZZ", &err);
        CHECK(b.isZero());
        CHECK(err != Error::Ok);
}

TEST_CASE("ByteArray: fromHexString null") {
        Error        err;
        ByteArray<4> b = ByteArray<4>::fromHexString(static_cast<const char *>(nullptr), &err);
        CHECK(b.isZero());
        CHECK(err != Error::Ok);
}

TEST_CASE("ByteArray: roundtrip hex") {
        ByteArray<8> original = ByteArray<8>::fromHexString("0123456789abcdef");
        String       hex = original.toHexString();
        ByteArray<8> roundtripped = ByteArray<8>::fromHexString(hex);
        CHECK(original == roundtripped);
        CHECK(hex == "0123456789abcdef");
}

TEST_CASE("ByteArray: equality and inequality") {
        uint8_t      raw1[] = {1, 2, 3, 4};
        uint8_t      raw2[] = {1, 2, 3, 5};
        ByteArray<4> a(raw1);
        ByteArray<4> b(raw1);
        ByteArray<4> c(raw2);
        CHECK(a == b);
        CHECK(a != c);
}

TEST_CASE("ByteArray: data access") {
        ByteArray<4>   b = ByteArray<4>::fromHexString("01020304");
        const uint8_t *p = b.data();
        CHECK(p[0] == 1);
        CHECK(p[1] == 2);
        CHECK(p[2] == 3);
        CHECK(p[3] == 4);
}
