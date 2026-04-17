/**
 * @file      timestamp.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/timestamp.h>
#include <promeki/duration.h>

using namespace promeki;

TEST_CASE("TimeStamp: default construction") {
        TimeStamp ts;
        // Default is epoch, so seconds should be 0
        CHECK(ts.seconds() == doctest::Approx(0.0));
}

TEST_CASE("TimeStamp: now returns non-zero") {
        TimeStamp ts = TimeStamp::now();
        CHECK(ts.seconds() > 0.0);
}

TEST_CASE("TimeStamp: update changes value") {
        TimeStamp ts;
        CHECK(ts.seconds() == doctest::Approx(0.0));
        ts.update();
        CHECK(ts.seconds() > 0.0);
}

TEST_CASE("TimeStamp: elapsed time") {
        TimeStamp ts = TimeStamp::now();
        // Elapsed should be very small (just created)
        CHECK(ts.elapsedSeconds() >= 0.0);
        CHECK(ts.elapsedSeconds() < 1.0);
}

TEST_CASE("TimeStamp: milliseconds") {
        TimeStamp ts = TimeStamp::now();
        CHECK(ts.milliseconds() > 0);
}

TEST_CASE("TimeStamp: microseconds") {
        TimeStamp ts = TimeStamp::now();
        CHECK(ts.microseconds() > 0);
}

TEST_CASE("TimeStamp: nanoseconds") {
        TimeStamp ts = TimeStamp::now();
        CHECK(ts.nanoseconds() > 0);
}

TEST_CASE("TimeStamp: operator+= duration") {
        TimeStamp ts = TimeStamp::now();
        double before = ts.seconds();
        ts += TimeStamp::secondsToDuration(1.0);
        CHECK(ts.seconds() > before);
        CHECK(ts.seconds() == doctest::Approx(before + 1.0).epsilon(0.01));
}

TEST_CASE("TimeStamp: operator-= duration") {
        TimeStamp ts = TimeStamp::now();
        double before = ts.seconds();
        ts -= TimeStamp::secondsToDuration(1.0);
        CHECK(ts.seconds() < before);
}

TEST_CASE("TimeStamp: operator+ duration") {
        TimeStamp ts = TimeStamp::now();
        TimeStamp later = ts + TimeStamp::secondsToDuration(2.0);
        CHECK(later.seconds() > ts.seconds());
}

TEST_CASE("TimeStamp: operator- duration") {
        TimeStamp ts = TimeStamp::now();
        TimeStamp earlier = ts - TimeStamp::secondsToDuration(2.0);
        CHECK(earlier.seconds() < ts.seconds());
}

TEST_CASE("TimeStamp: secondsToDuration") {
        auto d = TimeStamp::secondsToDuration(1.0);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(d);
        CHECK(ms.count() == doctest::Approx(1000).epsilon(10));
}

TEST_CASE("TimeStamp: toString") {
        TimeStamp ts = TimeStamp::now();
        String s = ts.toString();
        CHECK_FALSE(s.isEmpty());
}

TEST_CASE("TimeStamp: String conversion") {
        TimeStamp ts = TimeStamp::now();
        String s = ts;
        CHECK_FALSE(s.isEmpty());
}

TEST_CASE("TimeStamp: setValue") {
        TimeStamp a = TimeStamp::now();
        TimeStamp b;
        b.setValue(a.value());
        CHECK(b.seconds() == doctest::Approx(a.seconds()));
}

TEST_CASE("TimeStamp: Value conversion operator") {
        TimeStamp ts = TimeStamp::now();
        TimeStamp::Value v = ts;
        TimeStamp ts2(v);
        CHECK(ts2.seconds() == doctest::Approx(ts.seconds()));
}

// ============================================================================
// Equality operators
// ============================================================================

TEST_CASE("TimeStamp: equality same value") {
        TimeStamp ts = TimeStamp::now();
        TimeStamp ts2(ts.value());
        CHECK(ts == ts2);
        CHECK_FALSE(ts != ts2);
}

TEST_CASE("TimeStamp: equality different values") {
        TimeStamp a = TimeStamp::now();
        TimeStamp b = a + TimeStamp::secondsToDuration(1.0);
        CHECK_FALSE(a == b);
        CHECK(a != b);
}

TEST_CASE("TimeStamp: equality default constructed") {
        TimeStamp a;
        TimeStamp b;
        CHECK(a == b);
}

// ============================================================================
// promeki::Duration interop (new free functions)
// ============================================================================

TEST_CASE("TimeStamp: toClockDuration converts Duration to clock duration") {
        Duration d = Duration::fromMicroseconds(500);
        auto cd = toClockDuration(d);
        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(cd);
        CHECK(ns.count() == 500000);
}

TEST_CASE("TimeStamp: toClockDuration zero Duration") {
        Duration d = Duration::fromNanoseconds(0);
        auto cd = toClockDuration(d);
        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(cd);
        CHECK(ns.count() == 0);
}

TEST_CASE("TimeStamp: operator+= with Duration") {
        TimeStamp ts = TimeStamp::now();
        double before = ts.seconds();
        Duration d = Duration::fromMilliseconds(500);
        ts += d;
        double after = ts.seconds();
        CHECK(after > before);
        CHECK(after == doctest::Approx(before + 0.5).epsilon(0.01));
}

TEST_CASE("TimeStamp: operator-= with Duration") {
        TimeStamp ts = TimeStamp::now();
        double before = ts.seconds();
        Duration d = Duration::fromMilliseconds(500);
        ts -= d;
        double after = ts.seconds();
        CHECK(after < before);
        CHECK(after == doctest::Approx(before - 0.5).epsilon(0.01));
}

TEST_CASE("TimeStamp: operator+ with Duration") {
        TimeStamp ts = TimeStamp::now();
        Duration d = Duration::fromMilliseconds(1000);
        TimeStamp later = ts + d;
        CHECK(later.seconds() > ts.seconds());
        CHECK(later.seconds() == doctest::Approx(ts.seconds() + 1.0).epsilon(0.01));
}

TEST_CASE("TimeStamp: operator- with Duration") {
        TimeStamp ts = TimeStamp::now();
        Duration d = Duration::fromMilliseconds(1000);
        TimeStamp earlier = ts - d;
        CHECK(earlier.seconds() < ts.seconds());
        CHECK(earlier.seconds() == doctest::Approx(ts.seconds() - 1.0).epsilon(0.01));
}

TEST_CASE("TimeStamp: operator- returns Duration between two timestamps") {
        TimeStamp a = TimeStamp::now();
        TimeStamp b = a + Duration::fromMicroseconds(250000); // 250ms
        Duration diff = b - a;
        // The difference should be approximately 250ms = 250000us
        CHECK(diff.microseconds() == doctest::Approx(250000).epsilon(1000));
}

TEST_CASE("TimeStamp: Duration subtraction negative result") {
        TimeStamp a = TimeStamp::now();
        TimeStamp b = a + Duration::fromMilliseconds(100);
        Duration diff = a - b;  // a is earlier so diff is negative
        CHECK(diff.nanoseconds() < 0);
}

TEST_CASE("TimeStamp: Duration subtraction same timestamp is zero") {
        TimeStamp ts = TimeStamp::now();
        Duration diff = ts - ts;
        CHECK(diff.nanoseconds() == 0);
}

TEST_CASE("TimeStamp: Duration interop does not modify original") {
        TimeStamp ts = TimeStamp::now();
        TimeStamp original = ts;
        Duration d = Duration::fromMilliseconds(100);
        TimeStamp result = ts + d;
        CHECK(ts == original); // original unchanged
        CHECK(result != original);
}
