/**
 * @file      timestamp.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/timestamp.h>

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
