/**
 * @file      datetime.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/datetime.h>

using namespace promeki;

TEST_CASE("DateTime: default construction") {
        DateTime dt;
        // Default-constructed chrono time_point is epoch
        CHECK(dt.toTimeT() == 0);
}

TEST_CASE("DateTime: now returns non-zero") {
        DateTime dt = DateTime::now();
        CHECK(dt.toTimeT() > 0);
}

TEST_CASE("DateTime: construction from time_t") {
        time_t t = 1000000;
        DateTime dt(t);
        CHECK(dt.toTimeT() == t);
}

TEST_CASE("DateTime: toDouble") {
        time_t t = 1000000;
        DateTime dt(t);
        CHECK(dt.toDouble() == doctest::Approx(1000000.0).epsilon(1.0));
}

TEST_CASE("DateTime: equality") {
        time_t t = 500000;
        DateTime a(t);
        DateTime b(t);
        CHECK(a == b);
}

TEST_CASE("DateTime: inequality") {
        DateTime a((time_t)100);
        DateTime b((time_t)200);
        CHECK(a != b);
}

TEST_CASE("DateTime: ordering") {
        DateTime a((time_t)100);
        DateTime b((time_t)200);
        CHECK(a < b);
        CHECK(a <= b);
        CHECK(b > a);
        CHECK(b >= a);
}

TEST_CASE("DateTime: add seconds") {
        DateTime dt((time_t)1000);
        DateTime result = dt + 500.0;
        CHECK(result.toTimeT() == doctest::Approx(1500).epsilon(1));
}

TEST_CASE("DateTime: subtract seconds") {
        DateTime dt((time_t)1000);
        DateTime result = dt - 500.0;
        CHECK(result.toTimeT() == doctest::Approx(500).epsilon(1));
}

TEST_CASE("DateTime: operator+= seconds") {
        DateTime dt((time_t)1000);
        dt += 100.0;
        CHECK(dt.toTimeT() == doctest::Approx(1100).epsilon(1));
}

TEST_CASE("DateTime: operator-= seconds") {
        DateTime dt((time_t)1000);
        dt -= 100.0;
        CHECK(dt.toTimeT() == doctest::Approx(900).epsilon(1));
}

TEST_CASE("DateTime: toString") {
        DateTime dt((time_t)0);
        String s = dt.toString();
        CHECK_FALSE(s.isEmpty());
}

TEST_CASE("DateTime: String conversion operator") {
        DateTime dt((time_t)0);
        String s = dt;
        CHECK_FALSE(s.isEmpty());
}

TEST_CASE("DateTime: fromString with known format") {
        Error err;
        DateTime parsed = DateTime::fromString("2023-11-14 22:13:20", "%Y-%m-%d %H:%M:%S", &err);
        CHECK(err.isOk());
        // Should parse successfully to a valid time
        CHECK(parsed.toTimeT() > 0);
}

TEST_CASE("DateTime: fromString with bad input") {
        Error err;
        DateTime dt = DateTime::fromString("not-a-date", DateTime::DefaultFormat, &err);
        CHECK(err.isError());
}
