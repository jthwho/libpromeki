/**
 * @file      framerate.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/framerate.h>

using namespace promeki;

TEST_CASE("FrameRate: default construction is invalid") {
        FrameRate fr;
        CHECK_FALSE(fr.isValid());
        CHECK_FALSE(fr.isWellKnownRate());
}

TEST_CASE("FrameRate: construction from well-known rate 24") {
        FrameRate fr(FrameRate::FPS_24);
        CHECK(fr.isValid());
        CHECK(fr.isWellKnownRate());
        CHECK(fr.wellKnownRate() == FrameRate::FPS_24);
        CHECK(fr.numerator() == 24);
        CHECK(fr.denominator() == 1);
        CHECK(fr.toDouble() == doctest::Approx(24.0));
}

TEST_CASE("FrameRate: construction from well-known rate 25") {
        FrameRate fr(FrameRate::FPS_25);
        CHECK(fr.isValid());
        CHECK(fr.numerator() == 25);
        CHECK(fr.denominator() == 1);
}

TEST_CASE("FrameRate: construction from well-known rate 29.97") {
        FrameRate fr(FrameRate::FPS_29_97);
        CHECK(fr.isValid());
        CHECK(fr.numerator() == 30000);
        CHECK(fr.denominator() == 1001);
        CHECK(fr.toDouble() == doctest::Approx(29.97).epsilon(0.01));
}

TEST_CASE("FrameRate: construction from well-known rate 30") {
        FrameRate fr(FrameRate::FPS_30);
        CHECK(fr.isValid());
        CHECK(fr.numerator() == 30);
        CHECK(fr.denominator() == 1);
}

TEST_CASE("FrameRate: construction from well-known rate 59.94") {
        FrameRate fr(FrameRate::FPS_59_94);
        CHECK(fr.isValid());
        CHECK(fr.numerator() == 60000);
        CHECK(fr.denominator() == 1001);
}

TEST_CASE("FrameRate: construction from well-known rate 60") {
        FrameRate fr(FrameRate::FPS_60);
        CHECK(fr.isValid());
        CHECK(fr.numerator() == 60);
        CHECK(fr.denominator() == 1);
}

TEST_CASE("FrameRate: construction from well-known rate 23.98") {
        FrameRate fr(FrameRate::FPS_23_98);
        CHECK(fr.isValid());
        CHECK(fr.numerator() == 24000);
        CHECK(fr.denominator() == 1001);
}

TEST_CASE("FrameRate: construction from well-known rate 120") {
        FrameRate fr(FrameRate::FPS_120);
        CHECK(fr.isValid());
        CHECK(fr.numerator() == 120);
        CHECK(fr.denominator() == 1);
}

TEST_CASE("FrameRate: construction from well-known rate 119.88") {
        FrameRate fr(FrameRate::FPS_119_88);
        CHECK(fr.isValid());
        CHECK(fr.numerator() == 120000);
        CHECK(fr.denominator() == 1001);
}

TEST_CASE("FrameRate: construction from well-known rate 100") {
        FrameRate fr(FrameRate::FPS_100);
        CHECK(fr.isValid());
        CHECK(fr.numerator() == 100);
        CHECK(fr.denominator() == 1);
}

TEST_CASE("FrameRate: construction from well-known rate 48") {
        FrameRate fr(FrameRate::FPS_48);
        CHECK(fr.isValid());
        CHECK(fr.numerator() == 48);
        CHECK(fr.denominator() == 1);
}

TEST_CASE("FrameRate: construction from well-known rate 47.95") {
        FrameRate fr(FrameRate::FPS_47_95);
        CHECK(fr.isValid());
        CHECK(fr.numerator() == 48000);
        CHECK(fr.denominator() == 1001);
}

TEST_CASE("FrameRate: construction from well-known rate 50") {
        FrameRate fr(FrameRate::FPS_50);
        CHECK(fr.isValid());
        CHECK(fr.numerator() == 50);
        CHECK(fr.denominator() == 1);
}

TEST_CASE("FrameRate: construction from rational") {
        FrameRate::RationalType r(90, 1);
        FrameRate fr(r);
        CHECK(fr.isValid());
        CHECK(fr.numerator() == 90);
        CHECK(fr.denominator() == 1);
        CHECK_FALSE(fr.isWellKnownRate());
}

TEST_CASE("FrameRate: invalid well-known rate") {
        FrameRate fr(FrameRate::FPS_Invalid);
        CHECK_FALSE(fr.isValid());
}

TEST_CASE("FrameRate: toString") {
        FrameRate fr(FrameRate::FPS_24);
        String s = fr.toString();
        CHECK_FALSE(s.isEmpty());
}

TEST_CASE("FrameRate: rational() accessor") {
        FrameRate fr(FrameRate::FPS_29_97);
        const FrameRate::RationalType &r = fr.rational();
        CHECK(r.numerator() == 30000);
        CHECK(r.denominator() == 1001);
}

TEST_CASE("FrameRate: operator== and operator!=") {
        FrameRate a(FrameRate::FPS_24);
        FrameRate b(FrameRate::FPS_24);
        FrameRate c(FrameRate::FPS_25);

        CHECK(a == b);
        CHECK_FALSE(a == c);
        CHECK(a != c);
        CHECK_FALSE(a != b);

        // Same rational from different construction paths
        FrameRate::RationalType r(24, 1);
        FrameRate d(r);
        CHECK(a == d);
}

TEST_CASE("FrameRate: fromString well-known names") {
        SUBCASE("24") {
                auto [fr, e] = FrameRate::fromString("24");
                CHECK_FALSE(e.isError());
                CHECK(fr == FrameRate(FrameRate::FPS_24));
        }
        SUBCASE("25") {
                auto [fr, e] = FrameRate::fromString("25");
                CHECK_FALSE(e.isError());
                CHECK(fr == FrameRate(FrameRate::FPS_25));
        }
        SUBCASE("29.97") {
                auto [fr, e] = FrameRate::fromString("29.97");
                CHECK_FALSE(e.isError());
                CHECK(fr == FrameRate(FrameRate::FPS_29_97));
        }
        SUBCASE("30") {
                auto [fr, e] = FrameRate::fromString("30");
                CHECK_FALSE(e.isError());
                CHECK(fr == FrameRate(FrameRate::FPS_30));
        }
        SUBCASE("50") {
                auto [fr, e] = FrameRate::fromString("50");
                CHECK_FALSE(e.isError());
                CHECK(fr == FrameRate(FrameRate::FPS_50));
        }
        SUBCASE("59.94") {
                auto [fr, e] = FrameRate::fromString("59.94");
                CHECK_FALSE(e.isError());
                CHECK(fr == FrameRate(FrameRate::FPS_59_94));
        }
        SUBCASE("60") {
                auto [fr, e] = FrameRate::fromString("60");
                CHECK_FALSE(e.isError());
                CHECK(fr == FrameRate(FrameRate::FPS_60));
        }
        SUBCASE("23.98") {
                auto [fr, e] = FrameRate::fromString("23.98");
                CHECK_FALSE(e.isError());
                CHECK(fr == FrameRate(FrameRate::FPS_23_98));
        }
        SUBCASE("23.976 alias") {
                auto [fr, e] = FrameRate::fromString("23.976");
                CHECK_FALSE(e.isError());
                CHECK(fr == FrameRate(FrameRate::FPS_23_98));
        }
}

TEST_CASE("FrameRate: fromString fraction form") {
        SUBCASE("30000/1001") {
                auto [fr, e] = FrameRate::fromString("30000/1001");
                CHECK_FALSE(e.isError());
                CHECK(fr == FrameRate(FrameRate::FPS_29_97));
        }
        SUBCASE("48/1") {
                auto [fr, e] = FrameRate::fromString("48/1");
                CHECK_FALSE(e.isError());
                CHECK(fr == FrameRate(FrameRate::FPS_48));
        }
        SUBCASE("24000/1001 (23.976)") {
                auto [fr, e] = FrameRate::fromString("24000/1001");
                CHECK_FALSE(e.isError());
                CHECK(fr == FrameRate(FrameRate::FPS_23_98));
        }
}

TEST_CASE("FrameRate: wellKnownRate matches via reduced rational") {
        // 30000/1000 reduces to 30/1, which should match FPS_30
        FrameRate fr(FrameRate::RationalType(30000, 1000));
        CHECK(fr.isWellKnownRate());
        CHECK(fr.wellKnownRate() == FrameRate::FPS_30);

        // 60000/1000 reduces to 60/1, which should match FPS_60
        FrameRate fr2(FrameRate::RationalType(60000, 1000));
        CHECK(fr2.wellKnownRate() == FrameRate::FPS_60);

        // Non-well-known rate
        FrameRate fr3(FrameRate::RationalType(90, 1));
        CHECK_FALSE(fr3.isWellKnownRate());
        CHECK(fr3.wellKnownRate() == FrameRate::FPS_NotWellKnown);
}

TEST_CASE("FrameRate: fromString invalid") {
        SUBCASE("empty string") {
                auto [fr, e] = FrameRate::fromString("");
                CHECK(e.isError());
        }
        SUBCASE("garbage string") {
                auto [fr, e] = FrameRate::fromString("not_a_rate");
                CHECK(e.isError());
        }
        SUBCASE("zero denominator") {
                auto [fr, e] = FrameRate::fromString("30/0");
                CHECK(e.isError());
        }
}

// =========================================================================
// Audio sample cadence (samplesPerFrame)
//
// Verifies that fractional NTSC rates produce a per-frame sample count
// whose cumulative total stays exactly aligned with wall-clock time.
// =========================================================================

TEST_CASE("FrameRate: samplesPerFrame integer cadences are constant") {
        struct Case { FrameRate::WellKnownRate rate; int64_t expected; };
        Case cases[] = {
                { FrameRate::FPS_24, 2000 },  // 48000 / 24
                { FrameRate::FPS_25, 1920 },  // 48000 / 25
                { FrameRate::FPS_30, 1600 },  // 48000 / 30
                { FrameRate::FPS_50,  960 },  // 48000 / 50
                { FrameRate::FPS_60,  800 },  // 48000 / 60
        };
        for(const auto &c : cases) {
                FrameRate fps(c.rate);
                for(int64_t f = 0; f < 32; f++) {
                        CHECK(fps.samplesPerFrame(48000, f) == (size_t)c.expected);
                }
        }
}

TEST_CASE("FrameRate: samplesPerFrame 23.976 @ 48k is constant 2002") {
        // 48000 * 1001 / 24000 = 2002 exactly — no cadence needed.
        FrameRate fps(FrameRate::FPS_23_98);
        for(int64_t f = 0; f < 32; f++) {
                CHECK(fps.samplesPerFrame(48000, f) == 2002u);
        }
}

TEST_CASE("FrameRate: samplesPerFrame 29.97 @ 48k is cadenced and exact") {
        FrameRate fps(FrameRate::FPS_29_97);
        // Five frames must sum to exactly 8008 (= 48000 * 5 * 1001 / 30000).
        int64_t sum = 0;
        for(int64_t f = 0; f < 5; f++) {
                size_t s = fps.samplesPerFrame(48000, f);
                CHECK((s == 1601u || s == 1602u));
                sum += s;
        }
        CHECK(sum == 8008);

        // The cadence must be cyclic with period 5: frame N and frame
        // N+5 emit the same count.
        for(int64_t f = 0; f < 25; f++) {
                CHECK(fps.samplesPerFrame(48000, f)
                      == fps.samplesPerFrame(48000, f + 5));
        }

        // Every five-frame cycle (the natural cadence period) must sum
        // to exactly 8008 samples, no matter how far into the run we
        // are.  Verifying this over 1000 cycles (~166 seconds at
        // 29.97 fps) demonstrates that the cumulative count never
        // drifts by even one sample.
        for(int64_t cycle = 0; cycle < 1000; cycle++) {
                int64_t cycleSum = 0;
                for(int i = 0; i < 5; i++) {
                        cycleSum += (int64_t)fps.samplesPerFrame(
                                48000, cycle * 5 + i);
                }
                CHECK(cycleSum == 8008);
        }

        // Spot-check the running cumulative against the closed-form
        // expression.  After N frames the total samples must equal
        // floor(N * sampleRate * den / num) — i.e. exactly the
        // wall-clock-aligned integer sample count.
        const int64_t N = 30000; // = 1000 * 30 = exactly 1001 seconds
        int64_t cum = 0;
        for(int64_t f = 0; f < N; f++) {
                cum += (int64_t)fps.samplesPerFrame(48000, f);
        }
        const int64_t expected = (N * INT64_C(48000) * INT64_C(1001)) / INT64_C(30000);
        CHECK(cum == expected);
        CHECK(cum == INT64_C(48048000));
}

TEST_CASE("FrameRate: samplesPerFrame 59.94 @ 48k is cadenced and exact") {
        FrameRate fps(FrameRate::FPS_59_94);
        // Five frames must sum to exactly 4004 (= 48000 * 5 * 1001 / 60000).
        int64_t sum = 0;
        for(int64_t f = 0; f < 5; f++) {
                size_t s = fps.samplesPerFrame(48000, f);
                CHECK((s == 800u || s == 801u));
                sum += s;
        }
        CHECK(sum == 4004);
}

TEST_CASE("FrameRate: samplesPerFrame 119.88 @ 48k is cadenced and exact") {
        FrameRate fps(FrameRate::FPS_119_88);
        int64_t sum = 0;
        for(int64_t f = 0; f < 5; f++) {
                sum += (int64_t)fps.samplesPerFrame(48000, f);
        }
        // 48000 * 5 * 1001 / 120000 = 2002
        CHECK(sum == 2002);
}

TEST_CASE("FrameRate: samplesPerFrame respects sample rate") {
        FrameRate fps(FrameRate::FPS_30);
        CHECK(fps.samplesPerFrame(44100, 0)  ==  1470u);
        CHECK(fps.samplesPerFrame(48000, 0)  ==  1600u);
        CHECK(fps.samplesPerFrame(96000, 0)  ==  3200u);
        CHECK(fps.samplesPerFrame(192000, 0) ==  6400u);
}

TEST_CASE("FrameRate: samplesPerFrame on invalid frame rate returns 0") {
        FrameRate fps;
        CHECK_FALSE(fps.isValid());
        CHECK(fps.samplesPerFrame(48000, 0) == 0u);
}

TEST_CASE("FrameRate: samplesPerFrame on bad inputs returns 0") {
        FrameRate fps(FrameRate::FPS_30);
        CHECK(fps.samplesPerFrame(0, 0) == 0u);
        CHECK(fps.samplesPerFrame(-1, 0) == 0u);
        CHECK(fps.samplesPerFrame(48000, -1) == 0u);
}

// ============================================================================
// cumulativeTicks (exact rational clock at frame N)
// ============================================================================

TEST_CASE("FrameRate: cumulativeTicks zero frame is always zero") {
        FrameRate fps(FrameRate::FPS_29_97);
        CHECK(fps.cumulativeTicks(90000, 0) == 0);
        CHECK(fps.cumulativeTicks(48000, 0) == 0);
}

TEST_CASE("FrameRate: cumulativeTicks integer rate stride is exact") {
        // 30 fps @ 90000 Hz → exactly 3000 ticks per frame.
        FrameRate fps(FrameRate::FPS_30);
        for(int64_t n = 0; n < 100; ++n) {
                CHECK(fps.cumulativeTicks(90000, n) == n * 3000);
        }
}

TEST_CASE("FrameRate: cumulativeTicks 29.97 @ 90000 is exact integer") {
        // 29.97 = 30000/1001.  Per-frame stride = 90000 * 1001 / 30000 = 3003
        // (integer, because 1001 divides evenly).
        FrameRate fps(FrameRate::FPS_29_97);
        for(int64_t n = 0; n < 100; ++n) {
                CHECK(fps.cumulativeTicks(90000, n) == n * 3003);
        }
}

TEST_CASE("FrameRate: cumulativeTicks 23.976 @ 90000 matches rational truncation") {
        // 23.976 = 24000/1001.  Per-frame stride at 90 kHz is
        // 90000 × 1001 / 24000 = 3753.75, so the cumulative count
        // is a truncation of the exact rational value — consecutive
        // stride values alternate between 3753 and 3754 (in a
        // pattern that sums correctly to 3753.75 × N over the full
        // period).  The assertion is just that cumulativeTicks
        // matches the same rational truncation the caller would do
        // by hand.
        FrameRate fps(FrameRate::FPS_23_98);
        for(int64_t n = 0; n <= 1001; ++n) {
                int64_t expected = (n * 90000 * 1001) / 24000;
                CHECK(fps.cumulativeTicks(90000, n) == expected);
        }
}

TEST_CASE("FrameRate: cumulativeTicks monotonic under fractional rates") {
        // The cumulative tick count must never go backwards.  Even
        // when the per-frame stride alternates, cumulative(n+1) >=
        // cumulative(n) holds for every frame.
        FrameRate fps(FrameRate::FPS_29_97);
        int64_t prev = 0;
        for(int64_t n = 1; n < 1000; ++n) {
                int64_t cur = fps.cumulativeTicks(48000, n);
                CHECK(cur >= prev);
                prev = cur;
        }
}

TEST_CASE("FrameRate: cumulativeTicks matches samplesPerFrame cumulative sum") {
        // The cumulative sum of samplesPerFrame(n) for n in [0..N-1]
        // must equal cumulativeTicks(N) since samplesPerFrame is
        // defined as the difference between consecutive cumulative
        // values.  Validates that the refactor kept the two in sync
        // for fractional NTSC audio cadences.
        FrameRate fps(FrameRate::FPS_29_97);
        int64_t sum = 0;
        for(int64_t n = 0; n < 500; ++n) {
                sum += static_cast<int64_t>(fps.samplesPerFrame(48000, n));
                CHECK(sum == fps.cumulativeTicks(48000, n + 1));
        }
}

TEST_CASE("FrameRate: cumulativeTicks on invalid frame rate returns 0") {
        FrameRate fps;
        CHECK_FALSE(fps.isValid());
        CHECK(fps.cumulativeTicks(90000, 100) == 0);
}

TEST_CASE("FrameRate: cumulativeTicks on bad inputs returns 0") {
        FrameRate fps(FrameRate::FPS_30);
        CHECK(fps.cumulativeTicks(0, 100) == 0);
        CHECK(fps.cumulativeTicks(-1, 100) == 0);
        CHECK(fps.cumulativeTicks(90000, -1) == 0);
}
