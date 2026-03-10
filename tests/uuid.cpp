/**
 * @file      uuid.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/uuid.h>

using namespace promeki;

TEST_CASE("UUID: default is invalid") {
        UUID v;
        CHECK_FALSE(v.isValid());
}

TEST_CASE("UUID: generate is valid") {
        UUID v = UUID::generate();
        CHECK(v.isValid());
}

TEST_CASE("UUID: generate creates unique UUIDs") {
        UUID a = UUID::generate();
        UUID b = UUID::generate();
        CHECK(a != b);
}

TEST_CASE("UUID: construct from string") {
        UUID v("94eb2454-5116-4814-889f-7eb9bcb58bf1");
        CHECK(v.isValid());
}

TEST_CASE("UUID: case insensitive parsing") {
        UUID a("94eb2454-5116-4814-889f-7eb9bcb58bf1");
        UUID b("94EB2454-5116-4814-889F-7EB9BCB58BF1");
        CHECK(a == b);
}

TEST_CASE("UUID: invalid string") {
        UUID v("94EB2454-X116-4814-889F-7EB9BCB58BF1");
        CHECK_FALSE(v.isValid());
}

TEST_CASE("UUID: construct from String type") {
        String s("91809c4d-3682-4868-800c-05b871b84c0b");
        UUID v(s);
        CHECK(v.isValid());
        CHECK(v.toString() == s);
}

TEST_CASE("UUID: toString roundtrip") {
        UUID v = UUID::generate();
        String s = v.toString();
        UUID v2(s);
        CHECK(v == v2);
}

TEST_CASE("UUID: comparison operators") {
        UUID a = UUID::generate();
        UUID b = UUID::generate();
        UUID a2 = a;
        CHECK(a == a2);
        CHECK(a != b);
        // One must be less than the other
        CHECK((a < b) != (a > b));
        CHECK(a <= a2);
        CHECK(a >= a2);
}

TEST_CASE("UUID: less than is consistent with greater than") {
        UUID a = UUID::generate();
        UUID b = UUID::generate();
        if(a < b) {
                CHECK(b > a);
                CHECK(a <= b);
                CHECK(b >= a);
        } else {
                CHECK(a > b);
                CHECK(b <= a);
                CHECK(a >= b);
        }
}

TEST_CASE("UUID: String conversion operator") {
        UUID v("91809c4d-3682-4868-800c-05b871b84c0b");
        String s = v;
        CHECK_FALSE(s.isEmpty());
}

TEST_CASE("UUID: raw data access") {
        UUID v = UUID::generate();
        const uint8_t *raw = v.raw();
        CHECK(raw != nullptr);
}

TEST_CASE("UUID: data accessor") {
        UUID v = UUID::generate();
        const auto &d = v.data();
        CHECK(d.size() == 16);
}

TEST_CASE("UUID: fromString with error") {
        Error err;
        UUID v = UUID::fromString("invalid-uuid", &err);
        CHECK_FALSE(v.isValid());
}

TEST_CASE("UUID: fromString valid") {
        Error err;
        UUID v = UUID::fromString("94eb2454-5116-4814-889f-7eb9bcb58bf1", &err);
        CHECK(v.isValid());
}
