/**
 * @file      uuid.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/uuid.h>
#include <promeki/application.h>

using namespace promeki;

TEST_CASE("UUID: default is invalid") {
        UUID v;
        CHECK_FALSE(v.isValid());
}

TEST_CASE("UUID: default version is 0") {
        UUID v;
        CHECK(v.version() == 0);
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

TEST_CASE("UUID: parse from string") {
        UUID v = UUID::fromString("94eb2454-5116-4814-889f-7eb9bcb58bf1");
        CHECK(v.isValid());
}

TEST_CASE("UUID: case insensitive parsing") {
        UUID a = UUID::fromString("94eb2454-5116-4814-889f-7eb9bcb58bf1");
        UUID b = UUID::fromString("94EB2454-5116-4814-889F-7EB9BCB58BF1");
        CHECK(a == b);
}

TEST_CASE("UUID: invalid string") {
        Error err;
        UUID v = UUID::fromString("94EB2454-X116-4814-889F-7EB9BCB58BF1", &err);
        CHECK_FALSE(v.isValid());
        CHECK(err != Error::Ok);
}

TEST_CASE("UUID: construct from String type") {
        String s("91809c4d-3682-4868-800c-05b871b84c0b");
        UUID v = UUID::fromString(s);
        CHECK(v.isValid());
        CHECK(v.toString() == s);
}

TEST_CASE("UUID: toString roundtrip") {
        UUID v = UUID::generate();
        String s = v.toString();
        UUID v2 = UUID::fromString(s);
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
        UUID v = UUID::fromString("91809c4d-3682-4868-800c-05b871b84c0b");
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

TEST_CASE("UUID: generateV4 is valid and version 4") {
        UUID v = UUID::generateV4();
        CHECK(v.isValid());
        CHECK(v.version() == 4);
}

TEST_CASE("UUID: generate default is version 4") {
        UUID v = UUID::generate();
        CHECK(v.version() == 4);
}

TEST_CASE("UUID: generateV7 is valid and version 7") {
        UUID v = UUID::generateV7();
        CHECK(v.isValid());
        CHECK(v.version() == 7);
}

TEST_CASE("UUID: generateV7 embeds timestamp") {
        UUID a = UUID::generateV7();
        UUID b = UUID::generateV7();
        CHECK(a.isValid());
        CHECK(b.isValid());
        CHECK(a != b);
}

TEST_CASE("UUID: generateV7 with explicit timestamp") {
        int64_t ts = 1700000000000LL; // 2023-11-14T22:13:20Z
        UUID a = UUID::generateV7(ts);
        UUID b = UUID::generateV7(ts);
        CHECK(a.isValid());
        CHECK(b.isValid());
        CHECK(a.version() == 7);
        // Same timestamp but different random bits
        CHECK(a != b);
        // First 6 bytes (48-bit timestamp) should be identical
        const uint8_t *ra = a.raw();
        const uint8_t *rb = b.raw();
        for(int i = 0; i < 6; i++) {
                CHECK(ra[i] == rb[i]);
        }
}

TEST_CASE("UUID: generateV7 explicit timestamps are sortable") {
        int64_t ts1 = 1700000000000LL;
        int64_t ts2 = 1700000001000LL; // 1 second later
        UUID a = UUID::generateV7(ts1);
        UUID b = UUID::generateV7(ts2);
        CHECK(b > a);
}

TEST_CASE("UUID: generateV3 is deterministic") {
        UUID ns = UUID::fromString("6ba7b810-9dad-11d1-80b4-00c04fd430c8"); // DNS namespace
        String name("example.com");
        UUID a = UUID::generateV3(ns, name);
        UUID b = UUID::generateV3(ns, name);
        CHECK(a == b);
        CHECK(a.version() == 3);
}

TEST_CASE("UUID: generateV3 different names produce different UUIDs") {
        UUID ns = UUID::fromString("6ba7b810-9dad-11d1-80b4-00c04fd430c8");
        UUID a = UUID::generateV3(ns, "foo");
        UUID b = UUID::generateV3(ns, "bar");
        CHECK(a != b);
}

TEST_CASE("UUID: generateV5 is deterministic") {
        UUID ns = UUID::fromString("6ba7b810-9dad-11d1-80b4-00c04fd430c8");
        String name("example.com");
        UUID a = UUID::generateV5(ns, name);
        UUID b = UUID::generateV5(ns, name);
        CHECK(a == b);
        CHECK(a.version() == 5);
}

TEST_CASE("UUID: generateV5 different names produce different UUIDs") {
        UUID ns = UUID::fromString("6ba7b810-9dad-11d1-80b4-00c04fd430c8");
        UUID a = UUID::generateV5(ns, "foo");
        UUID b = UUID::generateV5(ns, "bar");
        CHECK(a != b);
}

TEST_CASE("UUID: generateV3 and generateV5 produce different results for same input") {
        UUID ns = UUID::fromString("6ba7b810-9dad-11d1-80b4-00c04fd430c8");
        UUID v3 = UUID::generateV3(ns, "example.com");
        UUID v5 = UUID::generateV5(ns, "example.com");
        CHECK(v3 != v5);
}

TEST_CASE("UUID: generate convenience with v3 uses Application") {
        Application::setAppUUID(UUID::fromString("6ba7b810-9dad-11d1-80b4-00c04fd430c8"));
        Application::setAppName("test-app");
        UUID v = UUID::generate(3);
        CHECK(v.isValid());
        CHECK(v.version() == 3);
        // Should match explicit call with same params
        UUID expected = UUID::generateV3(Application::appUUID(), Application::appName());
        CHECK(v == expected);
}

TEST_CASE("UUID: generate convenience with v5 uses Application") {
        Application::setAppUUID(UUID::fromString("6ba7b810-9dad-11d1-80b4-00c04fd430c8"));
        Application::setAppName("test-app");
        UUID v = UUID::generate(5);
        CHECK(v.isValid());
        CHECK(v.version() == 5);
        UUID expected = UUID::generateV5(Application::appUUID(), Application::appName());
        CHECK(v == expected);
}

TEST_CASE("UUID: version from parsed string") {
        // This is a known v4 UUID
        UUID v = UUID::fromString("94eb2454-5116-4814-889f-7eb9bcb58bf1");
        CHECK(v.version() == 4);
}

TEST_CASE("UUID: generateV3 with long name exceeding stack buffer") {
        // Names longer than 256 bytes previously triggered a heap allocation
        // via raw new[]/delete[].  This test exercises that path (now using
        // List<uint8_t>) to verify no leaks or crashes.
        UUID ns = UUID::fromString("6ba7b810-9dad-11d1-80b4-00c04fd430c8");
        String longName(512, 'x');
        UUID a = UUID::generateV3(ns, longName);
        CHECK(a.isValid());
        CHECK(a.version() == 3);
        // Must be deterministic
        UUID b = UUID::generateV3(ns, longName);
        CHECK(a == b);
}

TEST_CASE("UUID: generateV5 with long name exceeding stack buffer") {
        UUID ns = UUID::fromString("6ba7b810-9dad-11d1-80b4-00c04fd430c8");
        String longName(512, 'y');
        UUID a = UUID::generateV5(ns, longName);
        CHECK(a.isValid());
        CHECK(a.version() == 5);
        UUID b = UUID::generateV5(ns, longName);
        CHECK(a == b);
}

TEST_CASE("UUID: generateV3 short vs long name produce different results") {
        UUID ns = UUID::fromString("6ba7b810-9dad-11d1-80b4-00c04fd430c8");
        UUID shortResult = UUID::generateV3(ns, "short");
        UUID longResult = UUID::generateV3(ns, String(512, 'z'));
        CHECK(shortResult != longResult);
}

TEST_CASE("UUID: generateV5 short vs long name produce different results") {
        UUID ns = UUID::fromString("6ba7b810-9dad-11d1-80b4-00c04fd430c8");
        UUID shortResult = UUID::generateV5(ns, "short");
        UUID longResult = UUID::generateV5(ns, String(512, 'z'));
        CHECK(shortResult != longResult);
}
