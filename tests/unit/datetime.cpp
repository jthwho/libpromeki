/**
 * @file      datetime.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/datetime.h>

using namespace promeki;

TEST_CASE("DateTime: default construction is invalid") {
        DateTime dt;
        CHECK_FALSE(dt.isValid());
        CHECK(dt.toTimeT() == 0);
        CHECK(dt.toDouble() == doctest::Approx(0.0));
}

TEST_CASE("DateTime: now returns non-zero") {
        DateTime dt = DateTime::now();
        CHECK(dt.toTimeT() > 0);
}

TEST_CASE("DateTime: construction from time_t") {
        time_t   t = 1000000;
        DateTime dt(t);
        CHECK(dt.toTimeT() == t);
}

TEST_CASE("DateTime: toDouble") {
        time_t   t = 1000000;
        DateTime dt(t);
        CHECK(dt.toDouble() == doctest::Approx(1000000.0).epsilon(1.0));
}

TEST_CASE("DateTime: equality") {
        time_t   t = 500000;
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

TEST_CASE("DateTime: operator-(DateTime) returns Duration") {
        DateTime later((time_t)1500);
        DateTime earlier((time_t)1000);
        Duration d = later - earlier;
        CHECK(d.seconds() == 500);
}

TEST_CASE("DateTime: operator+(Duration) shifts forward") {
        DateTime dt((time_t)1000);
        DateTime later = dt + Duration::fromSeconds(250);
        CHECK(later.toTimeT() == doctest::Approx(1250).epsilon(1));
}

TEST_CASE("DateTime: operator-(Duration) shifts backward") {
        DateTime dt((time_t)1000);
        DateTime earlier = dt - Duration::fromSeconds(250);
        CHECK(earlier.toTimeT() == doctest::Approx(750).epsilon(1));
}

TEST_CASE("DateTime: operator+= Duration") {
        DateTime dt((time_t)1000);
        dt += Duration::fromSeconds(50);
        CHECK(dt.toTimeT() == doctest::Approx(1050).epsilon(1));
}

TEST_CASE("DateTime: operator-= Duration") {
        DateTime dt((time_t)1000);
        dt -= Duration::fromSeconds(50);
        CHECK(dt.toTimeT() == doctest::Approx(950).epsilon(1));
}

TEST_CASE("DateTime: toString") {
        DateTime dt((time_t)0);
        String   s = dt.toString();
        CHECK_FALSE(s.isEmpty());
}

TEST_CASE("DateTime: String conversion operator") {
        DateTime dt((time_t)0);
        String   s = dt;
        CHECK_FALSE(s.isEmpty());
}

TEST_CASE("DateTime: fromString with known format") {
        auto [parsed, err] = DateTime::fromString("2023-11-14 22:13:20", "%Y-%m-%d %H:%M:%S");
        CHECK(err.isOk());
        // Should parse successfully to a valid time
        CHECK(parsed.toTimeT() > 0);
}

TEST_CASE("DateTime: fromString with bad input") {
        auto [parsed, err] = DateTime::fromString("not-a-date");
        CHECK(err.isError());
        (void)parsed;
}

TEST_CASE("DateTime: toString is thread-safe with known time_t") {
        // Exercises the localtime_r path (previously used non-thread-safe std::localtime)
        DateTime dt((time_t)0);
        String   s1 = dt.toString();
        String   s2 = dt.toString();
        CHECK(s1 == s2);
        CHECK_FALSE(s1.isEmpty());
}

TEST_CASE("DateTime: toString roundtrip with subsecond format") {
        // Exercises addSubsecondToFormat + localtime_r path
        DateTime dt = DateTime::now();
        String   s = dt.toString("%T.3");
        CHECK_FALSE(s.isEmpty());
        // Should contain a dot for subsecond digits
        CHECK(s.contains("."));
}

TEST_CASE("DateTime: fromNow returns a valid DateTime") {
        // Exercises the fromNow localtime_r path
        DateTime future = DateTime::fromNow("1 hour");
        // Should produce a non-epoch time
        CHECK(future.toTimeT() > 0);
}

// ============================================================================
// Validity sentinel
// ============================================================================

TEST_CASE("DateTime: Invalid sentinel is INT64_MIN") {
        CHECK(DateTime::Invalid == INT64_MIN);
}

TEST_CASE("DateTime: now() and time_t/tm ctors are valid") {
        CHECK(DateTime::now().isValid());
        CHECK(DateTime(time_t{1000000}).isValid());
        CHECK(DateTime(time_t{0}).isValid()); // Unix epoch is explicit and valid
}

TEST_CASE("DateTime: invalidate() resets to default") {
        DateTime dt = DateTime::now();
        CHECK(dt.isValid());
        dt.invalidate();
        CHECK_FALSE(dt.isValid());
        CHECK(dt == DateTime());
}

TEST_CASE("DateTime: arithmetic with invalid propagates") {
        DateTime invalid;
        Duration d = Duration::fromSeconds(10);
        CHECK_FALSE((invalid + d).isValid());
        CHECK_FALSE((invalid - d).isValid());
        CHECK_FALSE((invalid + 1.0).isValid());
        CHECK_FALSE((invalid - 1.0).isValid());

        DateTime valid(time_t{1000});
        Duration invalidD;
        CHECK_FALSE((valid + invalidD).isValid());
        CHECK_FALSE((valid - invalidD).isValid());
}

TEST_CASE("DateTime: subtraction with invalid operands yields invalid Duration") {
        DateTime invalid;
        DateTime valid(time_t{1000});
        CHECK_FALSE((valid - invalid).isValid());
        CHECK_FALSE((invalid - valid).isValid());
}

TEST_CASE("DateTime: invalid DateTimes compare equal") {
        DateTime a;
        DateTime b;
        CHECK(a == b);
        CHECK_FALSE(a != b);
}

TEST_CASE("DateTime: toString on invalid renders 'invalid'") {
        DateTime invalid;
        CHECK(invalid.toString() == "invalid");
}

TEST_CASE("DateTime: fromString('invalid') round-trips") {
        auto parsed = DateTime::fromString("invalid");
        CHECK(parsed.second().isOk());
        CHECK_FALSE(parsed.first().isValid());
}
