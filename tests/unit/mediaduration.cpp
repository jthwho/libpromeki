/**
 * @file      mediaduration.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <unordered_set>
#include <promeki/mediaduration.h>
#include <promeki/variant.h>

using namespace promeki;

TEST_CASE("MediaDuration construction") {
        SUBCASE("Default is fully Unknown") {
                MediaDuration d;
                CHECK_FALSE(d.isValid());
                CHECK(d.isUnknown());
                CHECK(d.start().isUnknown());
                CHECK(d.length().isUnknown());
        }
        SUBCASE("Explicit start + length") {
                MediaDuration d(FrameNumber(0), FrameCount(50));
                CHECK(d.isValid());
                CHECK_FALSE(d.isUnknown());
                CHECK(d.start().value() == 0);
                CHECK(d.length().value() == 50);
        }
        SUBCASE("Infinite length flag") {
                MediaDuration d(FrameNumber(0), FrameCount::infinity());
                CHECK(d.isValid());
                CHECK(d.isInfinite());
        }
        SUBCASE("Empty length flag") {
                MediaDuration d(FrameNumber(0), FrameCount::empty());
                CHECK(d.isValid());
                CHECK(d.isEmpty());
        }
}

TEST_CASE("MediaDuration::FrameRange basics") {
        using FrameRange = MediaDuration::FrameRange;
        SUBCASE("Default invalid") {
                FrameRange r;
                CHECK_FALSE(r.isValid());
                CHECK(r.count().isUnknown());
        }
        SUBCASE("Inclusive count: 0..9 = 10 frames") {
                FrameRange r(FrameNumber(0), FrameNumber(9));
                CHECK(r.isValid());
                CHECK(r.count().value() == 10);
        }
        SUBCASE("Single-frame range: 5..5 = 1 frame") {
                FrameRange r(FrameNumber(5), FrameNumber(5));
                CHECK(r.isValid());
                CHECK(r.count().value() == 1);
        }
        SUBCASE("End < start is invalid") {
                FrameRange r(FrameNumber(10), FrameNumber(5));
                CHECK_FALSE(r.isValid());
                CHECK(r.count().isUnknown());
        }
}

TEST_CASE("MediaDuration::end and toFrameRange") {
        SUBCASE("end = start + length - 1") {
                MediaDuration d(FrameNumber(10), FrameCount(5));
                FrameNumber   e = d.end();
                CHECK(e.isValid());
                CHECK(e.value() == 14);
        }
        SUBCASE("Empty length has no end") {
                MediaDuration d(FrameNumber(10), FrameCount::empty());
                CHECK(d.end().isUnknown());
        }
        SUBCASE("Infinite length has no end") {
                MediaDuration d(FrameNumber(10), FrameCount::infinity());
                CHECK(d.end().isUnknown());
        }
        SUBCASE("toFrameRange success") {
                MediaDuration d(FrameNumber(0), FrameCount(50));
                auto [r, err] = d.toFrameRange();
                CHECK(err.isOk());
                CHECK(r.start.value() == 0);
                CHECK(r.end.value() == 49);
        }
        SUBCASE("Unknown duration returns DurationUnknown") {
                MediaDuration d;
                auto [r, err] = d.toFrameRange();
                CHECK(err == Error::DurationUnknown);
        }
        SUBCASE("Infinite duration returns FrameRangeInfinite") {
                MediaDuration d(FrameNumber(0), FrameCount::infinity());
                auto [r, err] = d.toFrameRange();
                CHECK(err == Error::FrameRangeInfinite);
        }
        SUBCASE("Empty duration returns Invalid") {
                MediaDuration d(FrameNumber(0), FrameCount::empty());
                auto [r, err] = d.toFrameRange();
                CHECK(err == Error::Invalid);
        }
        SUBCASE("Round-trip via FrameRange") {
                MediaDuration::FrameRange r(FrameNumber(10), FrameNumber(19));
                MediaDuration             d = MediaDuration::fromFrameRange(r);
                CHECK(d.start().value() == 10);
                CHECK(d.length().value() == 10);
                auto [r2, err] = d.toFrameRange();
                CHECK(err.isOk());
                CHECK(r2.start.value() == 10);
                CHECK(r2.end.value() == 19);
        }
}

TEST_CASE("MediaDuration mutation helpers") {
        SUBCASE("addToStart shifts start, length unchanged") {
                MediaDuration d(FrameNumber(0), FrameCount(10));
                d.addToStart(5);
                CHECK(d.start().value() == 5);
                CHECK(d.length().value() == 10);
        }
        SUBCASE("addToEnd extends length") {
                MediaDuration d(FrameNumber(0), FrameCount(10));
                d.addToEnd(7);
                CHECK(d.length().value() == 17);
        }
        SUBCASE("setEnd derives length from inclusive end") {
                MediaDuration d(FrameNumber(10), FrameCount(5));
                d.setEnd(FrameNumber(19));
                CHECK(d.length().value() == 10); // 10..19 inclusive = 10 frames
        }
        SUBCASE("setEnd before start poisons length to Unknown") {
                MediaDuration d(FrameNumber(10), FrameCount(5));
                d.setEnd(FrameNumber(5));
                CHECK(d.length().isUnknown());
        }
        SUBCASE("setEnd with Unknown end poisons length") {
                MediaDuration d(FrameNumber(10), FrameCount(5));
                d.setEnd(FrameNumber::unknown());
                CHECK(d.length().isUnknown());
        }
        SUBCASE("operator+= extends length, start unchanged") {
                MediaDuration d(FrameNumber(0), FrameCount(10));
                d += FrameCount(5);
                CHECK(d.start().value() == 0);
                CHECK(d.length().value() == 15);
        }
        SUBCASE("operator-= shrinks length") {
                MediaDuration d(FrameNumber(0), FrameCount(10));
                d -= FrameCount(3);
                CHECK(d.length().value() == 7);
        }
        SUBCASE("operator+ returns new MediaDuration without mutating") {
                MediaDuration d(FrameNumber(0), FrameCount(10));
                MediaDuration d2 = d + FrameCount(5);
                CHECK(d.length().value() == 10);
                CHECK(d2.length().value() == 15);
        }
}

TEST_CASE("MediaDuration::contains") {
        MediaDuration d(FrameNumber(10), FrameCount(5)); // covers 10..14
        CHECK(d.contains(FrameNumber(10)));
        CHECK(d.contains(FrameNumber(12)));
        CHECK(d.contains(FrameNumber(14)));
        CHECK_FALSE(d.contains(FrameNumber(9)));
        CHECK_FALSE(d.contains(FrameNumber(15)));
        CHECK_FALSE(d.contains(FrameNumber::unknown()));

        SUBCASE("Unknown duration contains nothing") {
                MediaDuration u;
                CHECK_FALSE(u.contains(FrameNumber(0)));
        }
        SUBCASE("Empty duration contains nothing") {
                MediaDuration e(FrameNumber(0), FrameCount::empty());
                CHECK_FALSE(e.contains(FrameNumber(0)));
        }
        SUBCASE("Infinite duration contains anything >= start") {
                MediaDuration inf(FrameNumber(10), FrameCount::infinity());
                CHECK_FALSE(inf.contains(FrameNumber(9)));
                CHECK(inf.contains(FrameNumber(10)));
                CHECK(inf.contains(FrameNumber(1000000)));
        }
}

TEST_CASE("MediaDuration::FrameRange::contains and iteration") {
        using FrameRange = MediaDuration::FrameRange;

        SUBCASE("contains: inclusive bounds") {
                FrameRange r(FrameNumber(5), FrameNumber(8));
                CHECK_FALSE(r.contains(FrameNumber(4)));
                CHECK(r.contains(FrameNumber(5)));
                CHECK(r.contains(FrameNumber(7)));
                CHECK(r.contains(FrameNumber(8)));
                CHECK_FALSE(r.contains(FrameNumber(9)));
                CHECK_FALSE(r.contains(FrameNumber::unknown()));
        }
        SUBCASE("contains: invalid range contains nothing") {
                FrameRange r;
                CHECK_FALSE(r.contains(FrameNumber(0)));
                FrameRange r2(FrameNumber(10), FrameNumber(5));
                CHECK_FALSE(r2.contains(FrameNumber(7)));
        }
        SUBCASE("Range-for over four frames") {
                FrameRange r(FrameNumber(2), FrameNumber(5)); // 2,3,4,5
                int64_t    sum = 0;
                int64_t    count = 0;
                for (FrameNumber n : r) {
                        sum += n.value();
                        ++count;
                }
                CHECK(count == 4);
                CHECK(sum == 14); // 2+3+4+5
        }
        SUBCASE("Range-for over single frame") {
                FrameRange r(FrameNumber(7), FrameNumber(7));
                int64_t    count = 0;
                for (FrameNumber n : r) {
                        CHECK(n.value() == 7);
                        ++count;
                }
                CHECK(count == 1);
        }
        SUBCASE("Range-for over invalid range yields nothing") {
                FrameRange r;
                int64_t    count = 0;
                for (FrameNumber n : r) {
                        (void)n;
                        ++count;
                }
                CHECK(count == 0);
        }
}

TEST_CASE("MediaDuration toString and fromString") {
        SUBCASE("Canonical form '<start>+<length>'") {
                MediaDuration d(FrameNumber(0), FrameCount(50));
                CHECK(d.toString() == String("0+50f"));
                MediaDuration d2(FrameNumber(100), FrameCount(10));
                CHECK(d2.toString() == String("100+10f"));
        }
        SUBCASE("Unknown components render empty") {
                MediaDuration d;
                CHECK(d.toString() == String("+"));
        }
        SUBCASE("Round-trip canonical form") {
                Error         err;
                MediaDuration d = MediaDuration::fromString(String("0+50f"), &err);
                CHECK(err.isOk());
                CHECK(d.start().value() == 0);
                CHECK(d.length().value() == 50);
        }
        SUBCASE("Round-trip with whitespace") {
                Error         err;
                MediaDuration d = MediaDuration::fromString(String("  0 + 50f  "), &err);
                CHECK(err.isOk());
                CHECK(d.start().value() == 0);
                CHECK(d.length().value() == 50);
        }
        SUBCASE("Range form '<start>-<end>' is inclusive") {
                Error         err;
                MediaDuration d = MediaDuration::fromString(String("0-9"), &err);
                CHECK(err.isOk());
                CHECK(d.start().value() == 0);
                CHECK(d.length().value() == 10);
        }
        SUBCASE("Range form with whitespace") {
                Error         err;
                MediaDuration d = MediaDuration::fromString(String("10 - 19"), &err);
                CHECK(err.isOk());
                CHECK(d.start().value() == 10);
                CHECK(d.length().value() == 10);
        }
        SUBCASE("Empty input is default Unknown") {
                Error         err;
                MediaDuration d = MediaDuration::fromString(String(), &err);
                CHECK(err.isOk());
                CHECK(d.isUnknown());
        }
        SUBCASE("Garbage parses as ParseFailed") {
                Error         err;
                MediaDuration d = MediaDuration::fromString(String("not a duration"), &err);
                CHECK(err == Error::ParseFailed);
        }
        SUBCASE("Infinite length round-trips through string") {
                MediaDuration d(FrameNumber(5), FrameCount::infinity());
                String        s = d.toString();
                CHECK(s == String("5+inf"));
                Error         err;
                MediaDuration d2 = MediaDuration::fromString(s, &err);
                CHECK(err.isOk());
                CHECK(d2.start().value() == 5);
                CHECK(d2.length().isInfinite());
        }
}

TEST_CASE("MediaDuration Variant integration") {
        SUBCASE("Round-trip via Variant") {
                Variant v(MediaDuration(FrameNumber(0), FrameCount(50)));
                CHECK(v.type() == Variant::TypeMediaDuration);
                MediaDuration d = v.get<MediaDuration>();
                CHECK(d.start().value() == 0);
                CHECK(d.length().value() == 50);
        }
        SUBCASE("Convertible to/from String via Variant") {
                Variant v(MediaDuration(FrameNumber(10), FrameCount(20)));
                CHECK(v.get<String>() == String("10+20f"));
                Variant       s(String("10+20f"));
                MediaDuration d = s.get<MediaDuration>();
                CHECK(d.start().value() == 10);
                CHECK(d.length().value() == 20);
        }
}

TEST_CASE("MediaDuration equality") {
        CHECK(MediaDuration() == MediaDuration());
        CHECK(MediaDuration(FrameNumber(0), FrameCount(50)) == MediaDuration(FrameNumber(0), FrameCount(50)));
        CHECK(MediaDuration(FrameNumber(0), FrameCount(50)) != MediaDuration(FrameNumber(0), FrameCount(51)));
        CHECK(MediaDuration(FrameNumber(0), FrameCount(50)) != MediaDuration(FrameNumber(1), FrameCount(50)));
}

TEST_CASE("MediaDuration ordering (lexicographic)") {
        // Sort by start, then by length.
        CHECK(MediaDuration(FrameNumber(0), FrameCount(10)) < MediaDuration(FrameNumber(1), FrameCount(5)));
        CHECK(MediaDuration(FrameNumber(5), FrameCount(10)) < MediaDuration(FrameNumber(5), FrameCount(20)));
        CHECK_FALSE(MediaDuration(FrameNumber(5), FrameCount(10)) < MediaDuration(FrameNumber(5), FrameCount(10)));
}

TEST_CASE("MediaDuration ordering is NaN-like when Unknown is involved") {
        const MediaDuration u; // both fields Unknown
        const MediaDuration finite(FrameNumber(0), FrameCount(10));
        // Equality on Unknown fields still holds (storage compare).
        CHECK(u == MediaDuration());
        // But every ordering relation against an Unknown operand is false.
        CHECK_FALSE(u < finite);
        CHECK_FALSE(finite < u);
        CHECK_FALSE(u <= u);
        CHECK_FALSE(u >= u);
        CHECK_FALSE(u <= finite);
        CHECK_FALSE(finite <= u);
        CHECK_FALSE(u >= finite);
        CHECK_FALSE(finite >= u);
}

TEST_CASE("MediaDuration::contains(MediaDuration)") {
        MediaDuration outer(FrameNumber(10), FrameCount(20)); // 10..29
        SUBCASE("Strict containment") {
                CHECK(outer.contains(MediaDuration(FrameNumber(15), FrameCount(5))));
                CHECK(outer.contains(MediaDuration(FrameNumber(10), FrameCount(20)))); // exact
        }
        SUBCASE("Boundary cases") {
                CHECK_FALSE(outer.contains(MediaDuration(FrameNumber(9), FrameCount(5))));   // start before
                CHECK_FALSE(outer.contains(MediaDuration(FrameNumber(20), FrameCount(15)))); // end after
        }
        SUBCASE("Empty other is contained in any valid duration") {
                CHECK(outer.contains(MediaDuration(FrameNumber(0), FrameCount::empty())));
        }
        SUBCASE("Empty this contains nothing non-empty") {
                MediaDuration empty(FrameNumber(0), FrameCount::empty());
                CHECK_FALSE(empty.contains(MediaDuration(FrameNumber(0), FrameCount(1))));
        }
        SUBCASE("Infinite this contains anything starting >= start") {
                MediaDuration inf(FrameNumber(10), FrameCount::infinity());
                CHECK(inf.contains(MediaDuration(FrameNumber(10), FrameCount(1))));
                CHECK(inf.contains(MediaDuration(FrameNumber(1000000), FrameCount(50))));
                CHECK_FALSE(inf.contains(MediaDuration(FrameNumber(9), FrameCount(1))));
        }
        SUBCASE("Finite this can't contain infinite other") {
                CHECK_FALSE(outer.contains(MediaDuration(FrameNumber(10), FrameCount::infinity())));
        }
        SUBCASE("Unknown propagates to false") {
                CHECK_FALSE(outer.contains(MediaDuration()));
                CHECK_FALSE(MediaDuration().contains(outer));
        }
}

TEST_CASE("MediaDuration::overlaps") {
        MediaDuration a(FrameNumber(10), FrameCount(10));                  // 10..19
        CHECK(a.overlaps(MediaDuration(FrameNumber(15), FrameCount(10)))); // 15..24
        CHECK(a.overlaps(MediaDuration(FrameNumber(0), FrameCount(15))));  // 0..14
        CHECK(a.overlaps(a));
        CHECK_FALSE(a.overlaps(MediaDuration(FrameNumber(20), FrameCount(5)))); // 20..24 (adjacent, not overlapping)
        CHECK_FALSE(a.overlaps(MediaDuration()));
}

TEST_CASE("MediaDuration::intersect") {
        MediaDuration a(FrameNumber(10), FrameCount(10)); // 10..19
        SUBCASE("Partial overlap") {
                MediaDuration b(FrameNumber(15), FrameCount(10)); // 15..24
                MediaDuration r = a.intersect(b);
                CHECK(r.start().value() == 15);
                CHECK(r.length().value() == 5); // 15..19
        }
        SUBCASE("Full containment returns the inner") {
                MediaDuration b(FrameNumber(12), FrameCount(3)); // 12..14
                MediaDuration r = a.intersect(b);
                CHECK(r == b);
        }
        SUBCASE("No overlap returns Unknown") {
                MediaDuration b(FrameNumber(30), FrameCount(5));
                MediaDuration r = a.intersect(b);
                CHECK(r.isUnknown());
        }
        SUBCASE("Intersect with infinite returns the finite tail from later start") {
                MediaDuration inf(FrameNumber(15), FrameCount::infinity());
                MediaDuration r = a.intersect(inf);
                CHECK(r.start().value() == 15);
                CHECK(r.length().value() == 5); // 15..19
        }
        SUBCASE("Intersect of two infinites is infinite") {
                MediaDuration inf1(FrameNumber(10), FrameCount::infinity());
                MediaDuration inf2(FrameNumber(20), FrameCount::infinity());
                MediaDuration r = inf1.intersect(inf2);
                CHECK(r.start().value() == 20);
                CHECK(r.isInfinite());
        }
}

TEST_CASE("MediaDuration::canAppend / append") {
        MediaDuration base(FrameNumber(10), FrameCount(5)); // 10..14
        SUBCASE("Adjacent finite append") {
                MediaDuration tail(FrameNumber(15), FrameCount(3));
                CHECK(base.canAppend(tail));
                CHECK(base.append(tail) == Error::Ok);
                CHECK(base.start().value() == 10);
                CHECK(base.length().value() == 8); // 10..17
        }
        SUBCASE("Non-adjacent fails") {
                MediaDuration gap(FrameNumber(20), FrameCount(3));
                CHECK_FALSE(base.canAppend(gap));
                CHECK(base.append(gap) == Error::NotAdjacent);
                CHECK(base.length().value() == 5); // unchanged
        }
        SUBCASE("Overlapping fails") {
                MediaDuration over(FrameNumber(12), FrameCount(3));
                CHECK_FALSE(base.canAppend(over));
                CHECK(base.append(over) == Error::NotAdjacent);
        }
        SUBCASE("Append infinite tail extends to infinity") {
                MediaDuration inf(FrameNumber(15), FrameCount::infinity());
                CHECK(base.canAppend(inf));
                CHECK(base.append(inf) == Error::Ok);
                CHECK(base.isInfinite());
                CHECK(base.start().value() == 10);
        }
        SUBCASE("Append empty fails (force caller to handle)") {
                MediaDuration empty(FrameNumber(15), FrameCount::empty());
                CHECK_FALSE(base.canAppend(empty));
        }
        SUBCASE("Append onto infinite fails (no defined end)") {
                MediaDuration inf(FrameNumber(0), FrameCount::infinity());
                MediaDuration tail(FrameNumber(10), FrameCount(5));
                CHECK_FALSE(inf.canAppend(tail));
                CHECK(inf.append(tail) == Error::NotAdjacent);
        }
        SUBCASE("Append onto unknown fails") {
                MediaDuration u;
                MediaDuration tail(FrameNumber(0), FrameCount(5));
                CHECK_FALSE(u.canAppend(tail));
        }
}

TEST_CASE("MediaDuration::canPrepend / prepend") {
        MediaDuration base(FrameNumber(10), FrameCount(5)); // 10..14
        SUBCASE("Adjacent finite prepend") {
                MediaDuration head(FrameNumber(5), FrameCount(5)); // 5..9
                CHECK(base.canPrepend(head));
                CHECK(base.prepend(head) == Error::Ok);
                CHECK(base.start().value() == 5);
                CHECK(base.length().value() == 10); // 5..14
        }
        SUBCASE("Non-adjacent fails") {
                MediaDuration gap(FrameNumber(0), FrameCount(3));
                CHECK_FALSE(base.canPrepend(gap));
                CHECK(base.prepend(gap) == Error::NotAdjacent);
                CHECK(base.start().value() == 10); // unchanged
        }
        SUBCASE("Prepend before infinite tail works") {
                MediaDuration inf(FrameNumber(10), FrameCount::infinity());
                MediaDuration head(FrameNumber(0), FrameCount(10));
                CHECK(inf.canPrepend(head));
                CHECK(inf.prepend(head) == Error::Ok);
                CHECK(inf.start().value() == 0);
                CHECK(inf.isInfinite());
        }
        SUBCASE("Prepend with infinite head fails (no defined end on head)") {
                MediaDuration head(FrameNumber(0), FrameCount::infinity());
                CHECK_FALSE(base.canPrepend(head));
        }
}

TEST_CASE("FrameRange::shift") {
        using FrameRange = MediaDuration::FrameRange;
        FrameRange r(FrameNumber(5), FrameNumber(9));
        SUBCASE("shift forward") {
                r.shift(10);
                CHECK(r.start.value() == 15);
                CHECK(r.end.value() == 19);
                CHECK(r.count().value() == 5); // count preserved
        }
        SUBCASE("shift backward") {
                r.shift(-3);
                CHECK(r.start.value() == 2);
                CHECK(r.end.value() == 6);
        }
        SUBCASE("operator+= and operator-=") {
                r += 4;
                CHECK(r.start.value() == 9);
                CHECK(r.end.value() == 13);
                r -= 2;
                CHECK(r.start.value() == 7);
                CHECK(r.end.value() == 11);
        }
        SUBCASE("Free + and - return new ranges") {
                FrameRange r2 = r + int64_t(5);
                CHECK(r.start.value() == 5); // original unchanged
                CHECK(r2.start.value() == 10);
        }
}

TEST_CASE("Hash support for FrameNumber/FrameCount/MediaDuration") {
        std::unordered_set<FrameNumber> fns;
        fns.insert(FrameNumber(1));
        fns.insert(FrameNumber(2));
        fns.insert(FrameNumber(1)); // duplicate
        CHECK(fns.size() == 2);
        CHECK(fns.contains(FrameNumber(1)));
        CHECK_FALSE(fns.contains(FrameNumber(3)));

        std::unordered_set<FrameCount> fcs;
        fcs.insert(FrameCount(50));
        fcs.insert(FrameCount::infinity());
        fcs.insert(FrameCount::unknown());
        CHECK(fcs.size() == 3);

        std::unordered_set<MediaDuration> mds;
        mds.insert(MediaDuration(FrameNumber(0), FrameCount(50)));
        mds.insert(MediaDuration(FrameNumber(0), FrameCount(50))); // duplicate
        mds.insert(MediaDuration(FrameNumber(0), FrameCount(51)));
        CHECK(mds.size() == 2);

        std::unordered_set<MediaDuration::FrameRange> rs;
        rs.insert(MediaDuration::FrameRange(FrameNumber(0), FrameNumber(9)));
        rs.insert(MediaDuration::FrameRange(FrameNumber(0), FrameNumber(9)));
        rs.insert(MediaDuration::FrameRange(FrameNumber(10), FrameNumber(19)));
        CHECK(rs.size() == 2);
}
