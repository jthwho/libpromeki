/**
 * @file      duration.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/duration.h>
#include <promeki/error.h>
#include <promeki/string.h>

using namespace promeki;

TEST_CASE("Duration: default is zero") {
        Duration d;
        CHECK(d.isZero());
        CHECK(d.nanoseconds() == 0);
}

TEST_CASE("Duration: fromHours") {
        Duration d = Duration::fromHours(2);
        CHECK(d.hours() == 2);
        CHECK(d.minutes() == 120);
        CHECK(d.seconds() == 7200);
}

TEST_CASE("Duration: fromMinutes") {
        Duration d = Duration::fromMinutes(90);
        CHECK(d.hours() == 1);
        CHECK(d.minutes() == 90);
        CHECK(d.seconds() == 5400);
}

TEST_CASE("Duration: fromSeconds") {
        Duration d = Duration::fromSeconds(3661);
        CHECK(d.hours() == 1);
        CHECK(d.minutes() == 61);
        CHECK(d.seconds() == 3661);
}

TEST_CASE("Duration: fromMilliseconds") {
        Duration d = Duration::fromMilliseconds(1500);
        CHECK(d.seconds() == 1);
        CHECK(d.milliseconds() == 1500);
}

TEST_CASE("Duration: fromMicroseconds") {
        Duration d = Duration::fromMicroseconds(1500000);
        CHECK(d.milliseconds() == 1500);
        CHECK(d.microseconds() == 1500000);
}

TEST_CASE("Duration: fromNanoseconds") {
        Duration d = Duration::fromNanoseconds(123456789);
        CHECK(d.nanoseconds() == 123456789);
        CHECK(d.milliseconds() == 123);
}

TEST_CASE("Duration: toSecondsDouble") {
        Duration d = Duration::fromMilliseconds(1500);
        CHECK(d.toSecondsDouble() == doctest::Approx(1.5));
}

TEST_CASE("Duration: isNegative") {
        Duration pos = Duration::fromSeconds(1);
        Duration neg = Duration::fromSeconds(-1);
        Duration zero;
        CHECK_FALSE(pos.isNegative());
        CHECK(neg.isNegative());
        CHECK_FALSE(zero.isNegative());
}

TEST_CASE("Duration: arithmetic") {
        Duration a = Duration::fromSeconds(10);
        Duration b = Duration::fromSeconds(3);

        CHECK((a + b).seconds() == 13);
        CHECK((a - b).seconds() == 7);
        CHECK((a * 2).seconds() == 20);
        CHECK((a / 2).seconds() == 5);
}

TEST_CASE("Duration: comparison") {
        Duration a = Duration::fromSeconds(10);
        Duration b = Duration::fromSeconds(20);
        Duration c = Duration::fromSeconds(10);

        CHECK(a == c);
        CHECK(a != b);
        CHECK(a < b);
        CHECK(b > a);
        CHECK(a <= c);
        CHECK(a <= b);
        CHECK(b >= a);
        CHECK(a >= c);
}

TEST_CASE("Duration: toString basic") {
        Duration d = Duration::fromSeconds(3661);
        String   s = d.toString();
        CHECK(s.contains("1h"));
        CHECK(s.contains("1m"));
        CHECK(s.contains("1s"));
}

TEST_CASE("Duration: toString zero") {
        Duration d;
        String   s = d.toString();
        CHECK(s.contains("0s"));
}

TEST_CASE("Duration: toString with milliseconds") {
        Duration d = Duration::fromMilliseconds(1500);
        String   s = d.toString();
        CHECK(s.contains("500"));
}

TEST_CASE("Duration: toString negative") {
        Duration d = Duration::fromSeconds(-5);
        String   s = d.toString();
        CHECK(s.contains("-"));
}

TEST_CASE("Duration: copy semantics") {
        Duration a = Duration::fromSeconds(42);
        Duration b = a;
        CHECK(b.seconds() == 42);
        CHECK(a == b);

        Duration c;
        c = a;
        CHECK(c.seconds() == 42);
}

TEST_CASE("Duration: isZero on non-zero") {
        Duration d = Duration::fromNanoseconds(1);
        CHECK_FALSE(d.isZero());

        Duration neg = Duration::fromNanoseconds(-1);
        CHECK_FALSE(neg.isZero());
}

TEST_CASE("Duration: negative factories") {
        Duration d = Duration::fromHours(-2);
        CHECK(d.isNegative());
        CHECK(d.hours() == -2);

        Duration m = Duration::fromMinutes(-30);
        CHECK(m.isNegative());
        CHECK(m.minutes() == -30);
}

TEST_CASE("Duration: subtraction producing negative") {
        Duration a = Duration::fromSeconds(3);
        Duration b = Duration::fromSeconds(10);
        Duration result = a - b;
        CHECK(result.isNegative());
        CHECK(result.seconds() == -7);
}

TEST_CASE("Duration: microseconds and nanoseconds accessors") {
        Duration d = Duration::fromNanoseconds(1234567890LL);
        CHECK(d.nanoseconds() == 1234567890LL);
        CHECK(d.microseconds() == 1234567);
        CHECK(d.milliseconds() == 1234);
        CHECK(d.seconds() == 1);
}

TEST_CASE("Duration: fromHours zero") {
        Duration d = Duration::fromHours(0);
        CHECK(d.isZero());
}

TEST_CASE("Duration: toScaledString") {
        Duration d = Duration::fromMilliseconds(1500);
        CHECK(d.toScaledString() == "1.5 s");

        Duration us = Duration::fromMicroseconds(42);
        CHECK(us.toScaledString() == "42 us");

        Duration h = Duration::fromHours(2);
        CHECK(h.toScaledString() == "2 h");
}

TEST_CASE("Duration: format default is HMS") {
        Duration d = Duration::fromSeconds(3661);
        String   s = String::format("{}", d);
        CHECK(s.contains("1h"));
        CHECK(s.contains("1m"));
        CHECK(s.contains("1s"));
}

TEST_CASE("Duration: format hms spec") {
        Duration d = Duration::fromSeconds(90);
        String   s = String::format("{:hms}", d);
        CHECK(s.contains("1m"));
        CHECK(s.contains("30s"));
}

TEST_CASE("Duration: format scaled spec") {
        Duration d = Duration::fromMilliseconds(1500);
        String   s = String::format("{:scaled}", d);
        CHECK(s == "1.5 s");
}

TEST_CASE("Duration: format scaled with width") {
        Duration d = Duration::fromMicroseconds(42);
        String   s = String::format("{:scaled:>12}", d);
        CHECK(s.size() == 12);
        CHECK(s.contains("42 us"));
}

TEST_CASE("Duration: fromString unit suffixes") {
        Error    e;
        Duration d;

        d = Duration::fromString("3s", &e);
        CHECK(!e.isError());
        CHECK(d == Duration::fromSeconds(3));

        d = Duration::fromString("500ms", &e);
        CHECK(!e.isError());
        CHECK(d == Duration::fromMilliseconds(500));

        d = Duration::fromString("100us", &e);
        CHECK(!e.isError());
        CHECK(d == Duration::fromMicroseconds(100));

        d = Duration::fromString("250ns", &e);
        CHECK(!e.isError());
        CHECK(d == Duration::fromNanoseconds(250));

        d = Duration::fromString("2m", &e);
        CHECK(!e.isError());
        CHECK(d == Duration::fromMinutes(2));

        d = Duration::fromString("1h", &e);
        CHECK(!e.isError());
        CHECK(d == Duration::fromHours(1));
}

TEST_CASE("Duration: fromString bare number is seconds") {
        Error    e;
        Duration d = Duration::fromString("5", &e);
        CHECK(!e.isError());
        CHECK(d == Duration::fromSeconds(5));
}

TEST_CASE("Duration: fromString decimals and whitespace") {
        Error    e;
        Duration d;

        d = Duration::fromString("1.5s", &e);
        CHECK(!e.isError());
        CHECK(d == Duration::fromMilliseconds(1500));

        d = Duration::fromString("0.25ms", &e);
        CHECK(!e.isError());
        CHECK(d == Duration::fromMicroseconds(250));

        d = Duration::fromString("  3 s ", &e);
        CHECK(!e.isError());
        CHECK(d == Duration::fromSeconds(3));
}

TEST_CASE("Duration: fromString rejects garbage") {
        Error e;
        Duration::fromString("", &e);
        CHECK(e.isError());

        e = Error();
        Duration::fromString("abc", &e);
        CHECK(e.isError());

        e = Error();
        Duration::fromString("3 weeks", &e);
        CHECK(e.isError());

        e = Error();
        Duration::fromString("ms", &e);  // unit only, no magnitude
        CHECK(e.isError());
}

TEST_CASE("Duration: fromString negative values") {
        Error    e;
        Duration d = Duration::fromString("-500ms", &e);
        CHECK(!e.isError());
        CHECK(d == Duration::fromMilliseconds(-500));
        CHECK(d.isNegative());
}
