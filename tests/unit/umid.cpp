/**
 * @file      umid.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <cstring>
#include <promeki/umid.h>

using namespace promeki;

TEST_CASE("UMID: default is invalid") {
        UMID v;
        CHECK_FALSE(v.isValid());
        CHECK(v.length() == UMID::Invalid);
        CHECK(v.byteSize() == 0);
}

TEST_CASE("UMID: default toString is empty") {
        UMID v;
        CHECK(v.toString().isEmpty());
}

TEST_CASE("UMID: generate Extended is valid") {
        UMID v = UMID::generate();
        CHECK(v.isValid());
        CHECK(v.length() == UMID::Extended);
        CHECK(v.byteSize() == 64);
        CHECK(v.isExtended());
}

TEST_CASE("UMID: generate Basic is valid") {
        UMID v = UMID::generate(UMID::Basic);
        CHECK(v.isValid());
        CHECK(v.length() == UMID::Basic);
        CHECK(v.byteSize() == 32);
        CHECK_FALSE(v.isExtended());
}

TEST_CASE("UMID: Universal Label is present") {
        UMID           v = UMID::generate();
        const uint8_t *raw = v.raw();
        // SMPTE 330M label prefix.
        CHECK(raw[0] == 0x06);
        CHECK(raw[1] == 0x0A);
        CHECK(raw[2] == 0x2B);
        CHECK(raw[3] == 0x34);
        CHECK(raw[4] == 0x01);
        CHECK(raw[5] == 0x01);
        CHECK(raw[6] == 0x01);
        CHECK(raw[7] == 0x01);
        CHECK(raw[8] == 0x01);
        CHECK(raw[9] == 0x01);
        CHECK(raw[10] == 0x0F);
        CHECK(raw[11] == 0x20);
}

TEST_CASE("UMID: length byte matches form") {
        UMID basic = UMID::generate(UMID::Basic);
        UMID ext = UMID::generate(UMID::Extended);
        CHECK(basic.raw()[12] == 0x13);
        CHECK(ext.raw()[12] == 0x33);
}

TEST_CASE("UMID: instance number is zero for generated root UMIDs") {
        UMID           v = UMID::generate();
        const uint8_t *raw = v.raw();
        CHECK(raw[13] == 0);
        CHECK(raw[14] == 0);
        CHECK(raw[15] == 0);
}

TEST_CASE("UMID: Extended source pack has MEKI organization tag") {
        UMID           v = UMID::generate(UMID::Extended);
        const uint8_t *raw = v.raw();
        // Organization field lives at offset 56..59.
        CHECK(raw[56] == 'M');
        CHECK(raw[57] == 'E');
        CHECK(raw[58] == 'K');
        CHECK(raw[59] == 'I');
}

TEST_CASE("UMID: Extended time/date field is populated") {
        UMID           v = UMID::generate(UMID::Extended);
        const uint8_t *raw = v.raw();
        // Year (big-endian) should be at least 2026 — if it's not,
        // either the clock is wildly wrong or we populated nothing.
        int year = (static_cast<int>(raw[32]) << 8) | static_cast<int>(raw[33]);
        CHECK(year >= 2026);
        // Month in 1..12
        CHECK(raw[34] >= 1);
        CHECK(raw[34] <= 12);
        // Day in 1..31
        CHECK(raw[35] >= 1);
        CHECK(raw[35] <= 31);
        // Hour in 0..23
        CHECK(raw[36] <= 23);
        // Minute in 0..59
        CHECK(raw[37] <= 59);
        // Second in 0..59 (leap seconds allowed up to 60)
        CHECK(raw[38] <= 60);
}

TEST_CASE("UMID: Basic generated UMIDs are unique") {
        UMID a = UMID::generate(UMID::Basic);
        UMID b = UMID::generate(UMID::Basic);
        CHECK(a != b);
}

TEST_CASE("UMID: Extended generated UMIDs are unique") {
        UMID a = UMID::generate(UMID::Extended);
        UMID b = UMID::generate(UMID::Extended);
        CHECK(a != b);
}

TEST_CASE("UMID: toString length matches byte size") {
        UMID basic = UMID::generate(UMID::Basic);
        UMID ext = UMID::generate(UMID::Extended);
        CHECK(basic.toString().size() == 64); // 32 bytes * 2 hex chars
        CHECK(ext.toString().size() == 128);  // 64 bytes * 2 hex chars
}

TEST_CASE("UMID: toString/fromString round trip (Basic)") {
        UMID   a = UMID::generate(UMID::Basic);
        String s = a.toString();
        Error  err;
        UMID   b = UMID::fromString(s, &err);
        CHECK(err.isOk());
        CHECK(b.isValid());
        CHECK(b.length() == UMID::Basic);
        CHECK(a == b);
}

TEST_CASE("UMID: toString/fromString round trip (Extended)") {
        UMID   a = UMID::generate(UMID::Extended);
        String s = a.toString();
        Error  err;
        UMID   b = UMID::fromString(s, &err);
        CHECK(err.isOk());
        CHECK(b.isValid());
        CHECK(b.length() == UMID::Extended);
        CHECK(a == b);
}

TEST_CASE("UMID: fromString accepts uppercase hex") {
        UMID   a = UMID::generate(UMID::Basic);
        String s = a.toString();
        String upper = s.toUpper();
        UMID   b = UMID::fromString(upper);
        CHECK(b.isValid());
        CHECK(a == b);
}

TEST_CASE("UMID: fromString accepts dashes and whitespace") {
        // Build a valid Extended hex string and insert separators.
        UMID   a = UMID::generate(UMID::Extended);
        String s = a.toString();
        // Insert a dash every 8 hex chars.
        String decorated;
        for (size_t i = 0; i < s.size(); ++i) {
                if (i != 0 && (i % 8) == 0) decorated += '-';
                decorated += s[i];
        }
        UMID b = UMID::fromString(decorated);
        CHECK(b.isValid());
        CHECK(a == b);
}

TEST_CASE("UMID: fromString rejects wrong length") {
        Error err;
        UMID  v = UMID::fromString("deadbeef", &err);
        CHECK_FALSE(v.isValid());
        CHECK(err.isError());
}

TEST_CASE("UMID: fromString rejects invalid characters") {
        // 64 chars, but one is 'g' which isn't hex.
        String bad = String(5, '0') + String("g") + String(58, '0');
        Error  err;
        UMID   v = UMID::fromString(bad, &err);
        CHECK_FALSE(v.isValid());
        CHECK(err.isError());
}

TEST_CASE("UMID: fromString rejects odd number of hex digits") {
        String odd(63, '0');
        Error  err;
        UMID   v = UMID::fromString(odd, &err);
        CHECK_FALSE(v.isValid());
        CHECK(err.isError());
}

TEST_CASE("UMID: equality is sensitive to length and bytes") {
        UMID basic = UMID::generate(UMID::Basic);
        UMID ext = UMID::generate(UMID::Extended);
        CHECK(basic != ext);
}

TEST_CASE("UMID: comparison operators are consistent") {
        UMID a = UMID::generate();
        UMID b = UMID::generate();
        UMID aCopy = a;
        CHECK(a == aCopy);
        CHECK(a != b);
        CHECK((a < b) != (a > b));
        CHECK(a <= aCopy);
        CHECK(a >= aCopy);
}

TEST_CASE("UMID: String conversion operator") {
        UMID   v = UMID::generate();
        String s = v;
        CHECK_FALSE(s.isEmpty());
        CHECK(s == v.toString());
}

TEST_CASE("UMID: data() size is always Extended storage size") {
        UMID v = UMID::generate(UMID::Basic);
        CHECK(v.data().size() == UMID::ExtendedSize);
        // The tail should be zero for a Basic UMID.
        const auto &d = v.data();
        for (size_t i = UMID::BasicSize; i < UMID::ExtendedSize; ++i) {
                CHECK(d[i] == 0);
        }
}
