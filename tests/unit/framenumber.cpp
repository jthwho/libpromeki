/**
 * @file      framenumber.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/framenumber.h>
#include <promeki/framecount.h>
#include <promeki/variant.h>

using namespace promeki;

TEST_CASE("FrameNumber construction and state") {
        SUBCASE("Default is Unknown") {
                FrameNumber fn;
                CHECK(fn.isUnknown());
                CHECK_FALSE(fn.isValid());
                CHECK(fn.value() == FrameNumber::UnknownValue);
        }
        SUBCASE("From non-negative int64 is valid") {
                FrameNumber fn(0);
                CHECK(fn.isValid());
                CHECK(fn.value() == 0);
                FrameNumber fn2(123456789);
                CHECK(fn2.isValid());
                CHECK(fn2.value() == 123456789);
        }
        SUBCASE("Negative int64 is canonicalised to Unknown") {
                FrameNumber fn(-1);
                CHECK(fn.isUnknown());
                FrameNumber fn2(-999);
                CHECK(fn2.isUnknown());
                CHECK(fn2.value() == FrameNumber::UnknownValue);
        }
        SUBCASE("unknown() factory") {
                CHECK(FrameNumber::unknown().isUnknown());
        }
}

TEST_CASE("FrameNumber arithmetic with int64_t") {
        SUBCASE("operator+= advances when valid") {
                FrameNumber fn(10);
                fn += 5;
                CHECK(fn.value() == 15);
        }
        SUBCASE("operator-= retreats when valid") {
                FrameNumber fn(10);
                fn -= 5;
                CHECK(fn.value() == 5);
        }
        SUBCASE("operator-= below zero poisons to Unknown") {
                FrameNumber fn(3);
                fn -= 5;
                CHECK(fn.isUnknown());
        }
        SUBCASE("Unknown stays Unknown under arithmetic") {
                FrameNumber fn = FrameNumber::unknown();
                fn += 5;
                CHECK(fn.isUnknown());
                fn -= 1;
                CHECK(fn.isUnknown());
        }
        SUBCASE("Pre/post increment & decrement") {
                FrameNumber fn(0);
                ++fn;
                CHECK(fn.value() == 1);
                fn++;
                CHECK(fn.value() == 2);
                --fn;
                CHECK(fn.value() == 1);
                fn--;
                CHECK(fn.value() == 0);
                // Decrement at zero → Unknown.
                fn--;
                CHECK(fn.isUnknown());
        }
        SUBCASE("Free-function operators produce new values") {
                FrameNumber a(5);
                FrameNumber b = a + int64_t(3);
                CHECK(a.value() == 5);
                CHECK(b.value() == 8);
                FrameNumber c = int64_t(2) + a;
                CHECK(c.value() == 7);
        }
}

TEST_CASE("FrameNumber arithmetic with FrameCount") {
        SUBCASE("FrameNumber + finite FrameCount") {
                FrameNumber sum = FrameNumber(10) + FrameCount(5);
                CHECK(sum.value() == 15);
        }
        SUBCASE("FrameNumber - FrameNumber yields FrameCount distance") {
                FrameCount d = FrameNumber(20) - FrameNumber(5);
                CHECK(d.isValid());
                CHECK(d.isFinite());
                CHECK(d.value() == 15);
        }
        SUBCASE("FrameNumber - FrameNumber backwards is Unknown") {
                FrameCount d = FrameNumber(5) - FrameNumber(20);
                CHECK(d.isUnknown());
        }
        SUBCASE("Unknown poisons cross-type arithmetic") {
                CHECK((FrameNumber::unknown() + FrameCount(5)).isUnknown());
                CHECK((FrameNumber(5) + FrameCount::unknown()).isUnknown());
        }
        SUBCASE("Infinite FrameCount poisons FrameNumber") {
                CHECK((FrameNumber(5) + FrameCount::infinity()).isUnknown());
                CHECK((FrameNumber(5) - FrameCount::infinity()).isUnknown());
        }
}

TEST_CASE("FrameNumber comparisons") {
        SUBCASE("Equality on values and on Unknown") {
                CHECK(FrameNumber(7) == FrameNumber(7));
                CHECK(FrameNumber(7) != FrameNumber(8));
                CHECK(FrameNumber::unknown() == FrameNumber::unknown());
                CHECK(FrameNumber(7) != FrameNumber::unknown());
        }
        SUBCASE("Ordering uses raw int64; Unknown sorts below valid") {
                CHECK(FrameNumber(5) < FrameNumber(6));
                CHECK(FrameNumber(5) <= FrameNumber(5));
                CHECK(FrameNumber(7) > FrameNumber(5));
                CHECK(FrameNumber::unknown() < FrameNumber(0));
        }
}

TEST_CASE("FrameNumber toString and fromString") {
        SUBCASE("Unknown round-trips through empty string") {
                CHECK(FrameNumber::unknown().toString() == String());
                Error err;
                FrameNumber fn = FrameNumber::fromString(String(), &err);
                CHECK(err.isOk());
                CHECK(fn.isUnknown());
        }
        SUBCASE("Valid round-trip") {
                CHECK(FrameNumber(42).toString() == String("42"));
                Error err;
                FrameNumber fn = FrameNumber::fromString(String("42"), &err);
                CHECK(err.isOk());
                CHECK(fn.value() == 42);
        }
        SUBCASE("Lenient parse: surrounding whitespace") {
                Error err;
                FrameNumber fn = FrameNumber::fromString(String("   100  "), &err);
                CHECK(err.isOk());
                CHECK(fn.value() == 100);
        }
        SUBCASE("Lenient parse: 'unknown' / 'unk' / '?' (case insensitive)") {
                CHECK(FrameNumber::fromString(String("unknown")).isUnknown());
                CHECK(FrameNumber::fromString(String("UNK")).isUnknown());
                CHECK(FrameNumber::fromString(String("?")).isUnknown());
        }
        SUBCASE("Negative numeric input is OutOfRange") {
                Error err;
                FrameNumber fn = FrameNumber::fromString(String("-5"), &err);
                CHECK(err == Error::OutOfRange);
                CHECK(fn.isUnknown());
        }
        SUBCASE("Garbage parses as ParseFailed") {
                Error err;
                FrameNumber fn = FrameNumber::fromString(String("abc"), &err);
                CHECK(err == Error::ParseFailed);
                CHECK(fn.isUnknown());
        }
}

TEST_CASE("FrameNumber Variant integration") {
        SUBCASE("Round-trip via Variant") {
                Variant v(FrameNumber(99));
                CHECK(v.type() == Variant::TypeFrameNumber);
                FrameNumber fn = v.get<FrameNumber>();
                CHECK(fn.value() == 99);
        }
        SUBCASE("Convertible to/from String via Variant") {
                Variant v(FrameNumber(42));
                CHECK(v.get<String>() == String("42"));
                Variant s(String("17"));
                FrameNumber fn = s.get<FrameNumber>();
                CHECK(fn.value() == 17);
        }
        SUBCASE("Convertible to int64_t for cross-type queries") {
                Variant v(FrameNumber(7));
                CHECK(v.get<int64_t>() == 7);
        }
        SUBCASE("Cross-type equality with int") {
                Variant a(FrameNumber(42));
                Variant b(int64_t(42));
                CHECK(a == b);
        }
}

