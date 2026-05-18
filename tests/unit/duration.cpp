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

TEST_CASE("Duration: default construction is invalid") {
        Duration d;
        CHECK_FALSE(d.isValid());
        CHECK_FALSE(d.isZero()); // invalid is distinct from zero
        CHECK_FALSE(d.isNegative()); // invalid is not negative
        CHECK(d.nanoseconds() == Duration::Invalid);
}

TEST_CASE("Duration: zero() factory is the explicit zero value") {
        Duration z = Duration::zero();
        CHECK(z.isValid());
        CHECK(z.isZero());
        CHECK(z.nanoseconds() == 0);
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
        Duration zero = Duration::zero();
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
        Duration d = Duration::zero();
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
        auto check = [](const char *in, Duration expected) {
                auto [d, e] = Duration::fromString(in);
                CHECK(!e.isError());
                CHECK(d == expected);
        };
        check("3s", Duration::fromSeconds(3));
        check("500ms", Duration::fromMilliseconds(500));
        check("100us", Duration::fromMicroseconds(100));
        check("250ns", Duration::fromNanoseconds(250));
        check("2m", Duration::fromMinutes(2));
        check("1h", Duration::fromHours(1));
}

TEST_CASE("Duration: fromString bare number is seconds") {
        auto [d, e] = Duration::fromString("5");
        CHECK(!e.isError());
        CHECK(d == Duration::fromSeconds(5));
}

TEST_CASE("Duration: fromString decimals and whitespace") {
        auto check = [](const char *in, Duration expected) {
                auto [d, e] = Duration::fromString(in);
                CHECK(!e.isError());
                CHECK(d == expected);
        };
        check("1.5s", Duration::fromMilliseconds(1500));
        check("0.25ms", Duration::fromMicroseconds(250));
        check("  3 s ", Duration::fromSeconds(3));
}

TEST_CASE("Duration: fromString rejects garbage") {
        CHECK(Duration::fromString("").second().isError());
        CHECK(Duration::fromString("abc").second().isError());
        CHECK(Duration::fromString("3 weeks").second().isError());
        CHECK(Duration::fromString("ms").second().isError()); // unit only, no magnitude
}

TEST_CASE("Duration: fromString negative values") {
        auto [d, e] = Duration::fromString("-500ms");
        CHECK(!e.isError());
        CHECK(d == Duration::fromMilliseconds(-500));
        CHECK(d.isNegative());
}

// ============================================================================
// Validity sentinel
// ============================================================================

TEST_CASE("Duration: Invalid sentinel is INT64_MIN") {
        CHECK(Duration::Invalid == INT64_MIN);
}

TEST_CASE("Duration: -1ns and 0ns are distinguishable from invalid") {
        Duration negOne = Duration::fromNanoseconds(-1);
        Duration zero = Duration::zero();
        Duration invalid;
        CHECK(negOne.isValid());
        CHECK(negOne.isNegative());
        CHECK(zero.isValid());
        CHECK_FALSE(invalid.isValid());
        CHECK(negOne != zero);
        CHECK(zero != invalid);
}

TEST_CASE("Duration: arithmetic with invalid propagates") {
        Duration valid = Duration::fromSeconds(1);
        Duration invalid;
        CHECK_FALSE((valid + invalid).isValid());
        CHECK_FALSE((invalid + valid).isValid());
        CHECK_FALSE((valid - invalid).isValid());
        CHECK_FALSE((invalid * 2).isValid());
        CHECK_FALSE((invalid / 2).isValid());
}

TEST_CASE("Duration: accessors on invalid return safe zero") {
        Duration invalid;
        CHECK(invalid.hours() == 0);
        CHECK(invalid.minutes() == 0);
        CHECK(invalid.seconds() == 0);
        CHECK(invalid.milliseconds() == 0);
        CHECK(invalid.microseconds() == 0);
        CHECK(invalid.nanoseconds() == Duration::Invalid);
        CHECK(invalid.toSecondsDouble() == doctest::Approx(0.0));
}

TEST_CASE("Duration: invalid Durations compare equal") {
        Duration a;
        Duration b;
        CHECK(a == b);
        CHECK_FALSE(a != b);
}

TEST_CASE("Duration: toString invalid round-trips") {
        Duration invalid;
        CHECK(invalid.toString() == "invalid");
        CHECK(invalid.toScaledString() == "invalid");
        auto parsed = Duration::fromString("invalid");
        CHECK(parsed.second().isOk());
        CHECK_FALSE(parsed.first().isValid());
}

TEST_CASE("Duration: fromSamples with bad rate returns explicit zero") {
        // Bad rate is treated as zero rate — historically returned zero,
        // not invalid, and pipeline math relies on that behaviour.
        Duration d = Duration::fromSamples(int64_t(1000), int64_t(0));
        CHECK(d.isValid());
        CHECK(d.isZero());
}
