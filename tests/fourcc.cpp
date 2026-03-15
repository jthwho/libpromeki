/**
 * @file      fourcc.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/core/fourcc.h>

using namespace promeki;

TEST_CASE("FourCC: construction from characters") {
        FourCC f('A', 'B', 'C', 'D');
        CHECK(f.value() != 0);
}

TEST_CASE("FourCC: construction from string literal") {
        FourCC f("ABCD");
        CHECK(f.value() != 0);
}

TEST_CASE("FourCC: string and char construction are equivalent") {
        FourCC a('A', 'B', 'C', 'D');
        FourCC b("ABCD");
        CHECK(a == b);
        CHECK(a.value() == b.value());
}

TEST_CASE("FourCC: equality") {
        FourCC a("ABCD");
        FourCC b("ABCD");
        FourCC c("EFGH");
        CHECK(a == b);
        CHECK(a != c);
}

TEST_CASE("FourCC: ordering") {
        FourCC a("AAAA");
        FourCC b("ZZZZ");
        CHECK(a < b);
        CHECK(a <= b);
        CHECK(b > a);
        CHECK(b >= a);
}

TEST_CASE("FourCC: constexpr construction") {
        constexpr FourCC f("TEST");
        constexpr uint32_t v = f.value();
        CHECK(v != 0);
}

TEST_CASE("FourCC: different codes have different values") {
        FourCC a("RGB8");
        FourCC b("YUV8");
        FourCC c("RGBA");
        CHECK(a != b);
        CHECK(b != c);
        CHECK(a != c);
}

TEST_CASE("FourCC: byte order is big-endian") {
        FourCC f('A', 'B', 'C', 'D');
        uint32_t expected = (uint32_t('A') << 24) | (uint32_t('B') << 16) | (uint32_t('C') << 8) | uint32_t('D');
        CHECK(f.value() == expected);
}

TEST_CASE("FourCCList: basic usage") {
        FourCCList list;
        list.pushToBack(FourCC("ABCD"));
        list.pushToBack(FourCC("EFGH"));
        CHECK(list.size() == 2);
}
