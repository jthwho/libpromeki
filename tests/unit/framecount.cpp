/**
 * @file      framecount.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <cmath>
#include <promeki/framecount.h>
#include <promeki/variant.h>

using namespace promeki;

TEST_CASE("FrameCount construction and state") {
        SUBCASE("Default is Unknown") {
                FrameCount c;
                CHECK(c.isUnknown());
                CHECK_FALSE(c.isValid());
                CHECK_FALSE(c.isFinite());
                CHECK_FALSE(c.isInfinite());
                CHECK_FALSE(c.isEmpty());
        }
        SUBCASE("Zero is Empty") {
                FrameCount c(0);
                CHECK(c.isValid());
                CHECK(c.isFinite());
                CHECK(c.isEmpty());
                CHECK_FALSE(c.isUnknown());
                CHECK_FALSE(c.isInfinite());
                CHECK(c.value() == 0);
        }
        SUBCASE("Positive is finite valid") {
                FrameCount c(50);
                CHECK(c.isValid());
                CHECK(c.isFinite());
                CHECK_FALSE(c.isEmpty());
                CHECK(c.value() == 50);
        }
        SUBCASE("Negative is canonicalised to Unknown") {
                FrameCount c(-1);
                CHECK(c.isUnknown());
                FrameCount c2(-99);
                CHECK(c2.isUnknown());
        }
        SUBCASE("infinity() factory") {
                FrameCount c = FrameCount::infinity();
                CHECK(c.isInfinite());
                CHECK(c.isValid());
                CHECK_FALSE(c.isFinite());
                CHECK_FALSE(c.isEmpty());
        }
        SUBCASE("empty() factory") {
                FrameCount c = FrameCount::empty();
                CHECK(c.isEmpty());
                CHECK(c.isFinite());
                CHECK(c.value() == 0);
        }
}

TEST_CASE("FrameCount sentinel arithmetic") {
        SUBCASE("Unknown poisons + and -") {
                CHECK((FrameCount::unknown() + FrameCount(5)).isUnknown());
                CHECK((FrameCount(5) + FrameCount::unknown()).isUnknown());
                CHECK((FrameCount::unknown() - FrameCount::infinity()).isUnknown());
        }
        SUBCASE("Infinity absorbs finite") {
                CHECK((FrameCount::infinity() + FrameCount(7)).isInfinite());
                CHECK((FrameCount(7) + FrameCount::infinity()).isInfinite());
                CHECK((FrameCount::infinity() - FrameCount(7)).isInfinite());
        }
        SUBCASE("Infinity - Infinity is Unknown") {
                CHECK((FrameCount::infinity() - FrameCount::infinity()).isUnknown());
        }
        SUBCASE("Infinity + Infinity is Infinity") {
                CHECK((FrameCount::infinity() + FrameCount::infinity()).isInfinite());
        }
        SUBCASE("Finite - Infinity is Unknown (no negatives)") {
                CHECK((FrameCount(5) - FrameCount::infinity()).isUnknown());
        }
        SUBCASE("Finite arithmetic clamps to Unknown if negative") {
                FrameCount c(3);
                c -= 5;
                CHECK(c.isUnknown());
        }
        SUBCASE("int64_t addition") {
                FrameCount c(5);
                c += 3;
                CHECK(c.value() == 8);
                CHECK((FrameCount::infinity() + int64_t(10)).isInfinite());
                CHECK((FrameCount::unknown() + int64_t(10)).isUnknown());
        }
}

TEST_CASE("FrameCount comparisons") {
        SUBCASE("Equality matches storage values") {
                CHECK(FrameCount(5) == FrameCount(5));
                CHECK(FrameCount::unknown() == FrameCount::unknown());
                CHECK(FrameCount::infinity() == FrameCount::infinity());
                CHECK(FrameCount(5) != FrameCount(6));
                CHECK(FrameCount(5) != FrameCount::unknown());
                CHECK(FrameCount(5) != FrameCount::infinity());
        }
        SUBCASE("Ordering: empty < finite < infinity") {
                CHECK(FrameCount(0) < FrameCount(1));
                CHECK(FrameCount(1) < FrameCount::infinity());
                CHECK(FrameCount::infinity() > FrameCount(1000000));
                CHECK_FALSE(FrameCount::infinity() < FrameCount(1));
        }
        SUBCASE("Ordering with Unknown returns false (NaN-like)") {
                CHECK_FALSE(FrameCount::unknown() < FrameCount(5));
                CHECK_FALSE(FrameCount(5) < FrameCount::unknown());
                CHECK_FALSE(FrameCount::unknown() > FrameCount::unknown());
                CHECK_FALSE(FrameCount::unknown() < FrameCount::unknown());
        }
        SUBCASE("operator<= / >= are NaN-like for Unknown") {
                // Equality still holds for Unknown==Unknown via storage
                // compare, but the ordering relations must still report
                // false to preserve NaN semantics.
                CHECK(FrameCount::unknown() == FrameCount::unknown());
                CHECK_FALSE(FrameCount::unknown() <= FrameCount::unknown());
                CHECK_FALSE(FrameCount::unknown() >= FrameCount::unknown());
                CHECK_FALSE(FrameCount::unknown() <= FrameCount(5));
                CHECK_FALSE(FrameCount(5) <= FrameCount::unknown());
                CHECK_FALSE(FrameCount::unknown() >= FrameCount(5));
                CHECK_FALSE(FrameCount(5) >= FrameCount::unknown());
        }
        SUBCASE("operator<= / >= on finite values respects ordering") {
                CHECK(FrameCount(5) <= FrameCount(5));
                CHECK(FrameCount(5) <= FrameCount(6));
                CHECK_FALSE(FrameCount(6) <= FrameCount(5));
                CHECK(FrameCount(5) >= FrameCount(5));
                CHECK(FrameCount(6) >= FrameCount(5));
                CHECK_FALSE(FrameCount(5) >= FrameCount(6));
                // Infinity is the maximum for ordering purposes.
                CHECK(FrameCount(5) <= FrameCount::infinity());
                CHECK(FrameCount::infinity() >= FrameCount(5));
                CHECK(FrameCount::infinity() <= FrameCount::infinity());
                CHECK(FrameCount::infinity() >= FrameCount::infinity());
        }
}

TEST_CASE("FrameCount toString and fromString") {
        SUBCASE("Unknown round-trip via empty string") {
                CHECK(FrameCount::unknown().toString() == String());
                CHECK(FrameCount::fromString(String()).isUnknown());
        }
        SUBCASE("Finite round-trip uses 'f' suffix") {
                CHECK(FrameCount(0).toString() == String("0f"));
                CHECK(FrameCount(50).toString() == String("50f"));
                CHECK(FrameCount::fromString(String("50f")).value() == 50);
                CHECK(FrameCount::fromString(String("0f")).value() == 0);
        }
        SUBCASE("Bare integer parses as FrameCount") {
                CHECK(FrameCount::fromString(String("42")).value() == 42);
        }
        SUBCASE("Infinity round-trip") {
                CHECK(FrameCount::infinity().toString() == String("inf"));
                CHECK(FrameCount::fromString(String("inf")).isInfinite());
                CHECK(FrameCount::fromString(String("Infinity")).isInfinite());
                CHECK(FrameCount::fromString(String("INFINITE")).isInfinite());
        }
        SUBCASE("Lenient parse: whitespace + case") {
                CHECK(FrameCount::fromString(String("  50F  ")).value() == 50);
                CHECK(FrameCount::fromString(String("UNKNOWN")).isUnknown());
        }
        SUBCASE("Negative integer is OutOfRange") {
                Error err;
                FrameCount c = FrameCount::fromString(String("-3"), &err);
                CHECK(err == Error::OutOfRange);
                CHECK(c.isUnknown());
        }
        SUBCASE("Garbage parses as ParseFailed") {
                Error err;
                FrameCount c = FrameCount::fromString(String("abc"), &err);
                CHECK(err == Error::ParseFailed);
                CHECK(c.isUnknown());
        }
}

TEST_CASE("FrameCount Variant integration") {
        SUBCASE("Round-trip via Variant") {
                Variant v(FrameCount(123));
                CHECK(v.type() == Variant::TypeFrameCount);
                FrameCount c = v.get<FrameCount>();
                CHECK(c.value() == 123);
        }
        SUBCASE("Convertible to String via Variant") {
                Variant v(FrameCount(50));
                CHECK(v.get<String>() == String("50f"));
                Variant inf(FrameCount::infinity());
                CHECK(inf.get<String>() == String("inf"));
        }
        SUBCASE("get<double> handles sentinels") {
                Variant v(FrameCount(50));
                CHECK(v.get<double>() == 50.0);
                Variant inf(FrameCount::infinity());
                CHECK(std::isinf(inf.get<double>()));
                Variant unk(FrameCount::unknown());
                CHECK(std::isnan(unk.get<double>()));
        }
}

