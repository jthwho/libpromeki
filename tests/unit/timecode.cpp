/**
 * @file      timecode.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/buffer.h>
#include <promeki/bufferiodevice.h>
#include <promeki/datastream.h>
#include <promeki/framerate.h>
#include <promeki/logger.h>
#include <promeki/timecode.h>
#include <promeki/timecodeuserbits.h>

using namespace promeki;

TEST_CASE("Timecode constructors") {
        SUBCASE("Default constructor is invalid") {
                Timecode tc;
                CHECK_FALSE(tc.isValid());
                CHECK(tc.hour() == 0);
                CHECK(tc.min() == 0);
                CHECK(tc.sec() == 0);
                CHECK(tc.frame() == 0);
        }

        SUBCASE("Mode-only constructor") {
                Timecode tc(Timecode::NDF25);
                CHECK(tc.isValid());
                CHECK(tc.fps() == 25);
                CHECK(tc.hour() == 0);
                CHECK(tc.frame() == 0);
        }

        SUBCASE("Four-arg constructor (no mode)") {
                Timecode tc(1, 2, 3, 4);
                CHECK(tc.isValid());
                CHECK(tc.fps() == 0);
                CHECK(tc.hour() == 1);
                CHECK(tc.min() == 2);
                CHECK(tc.sec() == 3);
                CHECK(tc.frame() == 4);
        }

        SUBCASE("Mode + digits constructor") {
                Timecode tc(Timecode::DF30, 10, 20, 30, 15);
                CHECK(tc.isValid());
                CHECK(tc.isDropFrame());
                CHECK(tc.fps() == 30);
                CHECK(tc.hour() == 10);
                CHECK(tc.min() == 20);
                CHECK(tc.sec() == 30);
                CHECK(tc.frame() == 15);
        }

        SUBCASE("String constructor") {
                Timecode tc(String("01:02:03;04"));
                CHECK(tc.isValid());
                CHECK(tc.isDropFrame());
                CHECK(tc.hour() == 1);
                CHECK(tc.min() == 2);
                CHECK(tc.sec() == 3);
                CHECK(tc.frame() == 4);
        }

        SUBCASE("Invalid string constructor leaves default state") {
                Timecode tc(String("not a timecode"));
                CHECK_FALSE(tc.isValid());
        }
}

TEST_CASE("Timecode Mode") {
        SUBCASE("TimecodeType modes") {
                Timecode::Mode m24(Timecode::NDF24);
                CHECK(m24.isValid());
                CHECK(m24.fps() == 24);
                CHECK_FALSE(m24.isDropFrame());

                Timecode::Mode m25(Timecode::NDF25);
                CHECK(m25.fps() == 25);
                CHECK_FALSE(m25.isDropFrame());

                Timecode::Mode m30(Timecode::NDF30);
                CHECK(m30.fps() == 30);
                CHECK_FALSE(m30.isDropFrame());

                Timecode::Mode mdf(Timecode::DF30);
                CHECK(mdf.fps() == 30);
                CHECK(mdf.isDropFrame());
        }

        SUBCASE("Mode equality") {
                CHECK(Timecode::Mode(Timecode::NDF30) == Timecode::Mode(Timecode::NDF30));
                CHECK(Timecode::Mode(Timecode::DF30) == Timecode::Mode(Timecode::DF30));
                CHECK(Timecode::Mode(Timecode::NDF30) != Timecode::Mode(Timecode::DF30));
                CHECK(Timecode::Mode(Timecode::NDF24) != Timecode::Mode(Timecode::NDF25));
        }

        SUBCASE("Custom fps mode") {
                Timecode::Mode m(15u, 0u);
                CHECK(m.isValid());
                CHECK(m.fps() == 15);
                CHECK_FALSE(m.isDropFrame());
                CHECK(m.hasFormat());
        }

        SUBCASE("Zero fps mode is valid but has no format") {
                Timecode::Mode m(0u, 0u);
                CHECK(m.isValid());
                CHECK(m.fps() == 0);
                CHECK_FALSE(m.hasFormat());
        }

        SUBCASE("Default mode is invalid") {
                Timecode::Mode m;
                CHECK_FALSE(m.isValid());
        }
}

TEST_CASE("Timecode equality and comparison") {
        SUBCASE("Equal timecodes") {
                Timecode a(Timecode::NDF30, 1, 2, 3, 4);
                Timecode b(Timecode::NDF30, 1, 2, 3, 4);
                CHECK(a == b);
                CHECK_FALSE(a != b);
        }

        SUBCASE("Different digits") {
                Timecode a(Timecode::NDF30, 1, 2, 3, 4);
                Timecode b(Timecode::NDF30, 1, 2, 3, 5);
                CHECK(a != b);
                CHECK_FALSE(a == b);
        }

        SUBCASE("Different modes") {
                Timecode a(Timecode::NDF30, 1, 2, 3, 4);
                Timecode b(Timecode::NDF25, 1, 2, 3, 4);
                CHECK(a != b);
        }

        SUBCASE("Ordering operators") {
                Timecode a(Timecode::NDF30, 0, 0, 0, 0);
                Timecode b(Timecode::NDF30, 0, 0, 1, 0);
                CHECK(a < b);
                CHECK(b > a);
                CHECK(a <= b);
                CHECK(b >= a);
                CHECK(a <= a);
                CHECK(a >= a);
        }

        SUBCASE("Ordering: same mode compares digits") {
                // Ascending order, same mode — digit-tuple ordering
                // suffices without a frame-number conversion.
                Timecode a(Timecode::NDF30, 1, 0, 0, 5);
                Timecode b(Timecode::NDF30, 1, 0, 0, 6);
                Timecode c(Timecode::NDF30, 1, 0, 1, 0);
                Timecode d(Timecode::NDF30, 1, 1, 0, 0);
                Timecode e(Timecode::NDF30, 2, 0, 0, 0);
                CHECK(a < b);
                CHECK(b < c);
                CHECK(c < d);
                CHECK(d < e);
                CHECK_FALSE(e < a);
        }

        SUBCASE("Ordering: format-less (unknown mode) uses digits") {
                // Digits-only Timecodes (mode is valid-but-format-less)
                // can still be ordered by their digit tuple.  This is
                // the case pmdf-inspect's "Meta.Timecode > \"01:00:00:05\""
                // hits when the parsed literal has no mode attached.
                Timecode lit(1, 0, 0, 5);
                Timecode before(1, 0, 0, 4);
                Timecode after(1, 0, 0, 6);
                REQUIRE_FALSE(lit.mode().hasFormat());
                CHECK(before < lit);
                CHECK(lit < after);
                CHECK(lit > before);
                CHECK(lit <= lit);
                CHECK(lit >= lit);
        }

        SUBCASE("Ordering: unknown vs moded uses digits") {
                // Mixing a format-less side with a moded side must also
                // use digits — we have no rate with which to convert the
                // unknown side to a frame number.
                Timecode lit(1, 0, 0, 5); // no mode
                Timecode moded(Timecode::NDF30, 1, 0, 0, 10);
                REQUIRE_FALSE(lit.mode().hasFormat());
                REQUIRE(moded.mode().hasFormat());
                CHECK(lit < moded);
                CHECK(moded > lit);

                // Equal digits across unknown + moded → neither < nor >.
                Timecode litEqDigits(1, 0, 0, 10);
                CHECK_FALSE(litEqDigits < moded);
                CHECK_FALSE(litEqDigits > moded);
                CHECK(litEqDigits <= moded);
                CHECK(litEqDigits >= moded);
        }

        SUBCASE("Ordering: different valid modes use frame numbers") {
                // Same digits at different rates produce different
                // absolute frame counts — ordering must reflect that.
                // 01:00:00:00 at NDF30 = 108000; at NDF24 = 86400.
                Timecode tc30(Timecode::NDF30, 1, 0, 0, 0);
                Timecode tc24(Timecode::NDF24, 1, 0, 0, 0);
                CHECK(tc30 > tc24);
                CHECK(tc24 < tc30);

                // And sanity-check a case where frame-number ordering
                // disagrees with digit ordering: DF30 drops frames so
                // DF30 01:00:00:00 = 107892 frames < NDF30 108000.
                Timecode ndf30(Timecode::NDF30, 1, 0, 0, 0);
                Timecode df30(Timecode::DF30, 1, 0, 0, 0);
                CHECK(df30 < ndf30);
                CHECK(ndf30 > df30);
        }

        SUBCASE("Ordering: invalid timecodes fall back to digits") {
                // A default-constructed (invalid) Timecode has zero
                // digits and no format — comparing it to a moded
                // Timecode picks the digit path and gives the expected
                // "zero is less than one" result instead of all-false.
                Timecode invalid;
                Timecode moded(Timecode::NDF30, 1, 0, 0, 0);
                REQUIRE_FALSE(invalid.isValid());
                CHECK(invalid < moded);
                CHECK(moded > invalid);
        }
}

TEST_CASE("Timecode set") {
        Timecode tc(Timecode::NDF30);
        tc.set(10, 20, 30, 15);
        CHECK(tc.hour() == 10);
        CHECK(tc.min() == 20);
        CHECK(tc.sec() == 30);
        CHECK(tc.frame() == 15);
}

TEST_CASE("Timecode fromString") {
        SUBCASE("Basic NDF") {
                auto [tc, err] = Timecode::fromString("01:02:03:04");
                CHECK(err.isOk());
                CHECK(tc.isValid());
                CHECK(tc.hour() == 1);
                CHECK(tc.min() == 2);
                CHECK(tc.sec() == 3);
                CHECK(tc.frame() == 4);
                CHECK_FALSE(tc.isDropFrame());
        }

        SUBCASE("With fps suffix") {
                auto [tc, err] = Timecode::fromString("01:02:03:04/30");
                CHECK(err.isOk());
                CHECK(tc.fps() == 30);
        }

        SUBCASE("Drop frame semicolon delimiter") {
                auto [tc, err] = Timecode::fromString("01:02:03;04");
                CHECK(err.isOk());
                CHECK(tc.isDropFrame());
                CHECK(tc.fps() == 30);
        }

        SUBCASE("Zero timecode") {
                auto [tc, err] = Timecode::fromString("00:00:00:00/24");
                CHECK(err.isOk());
                CHECK(tc.fps() == 24);
                CHECK(tc.hour() == 0);
                CHECK(tc.min() == 0);
                CHECK(tc.sec() == 0);
                CHECK(tc.frame() == 0);
        }

        SUBCASE("Invalid string returns error") {
                auto [tc, err] = Timecode::fromString("not a timecode");
                CHECK_FALSE(err.isOk());
        }

        SUBCASE("Empty string parses to an invalid Timecode") {
                // Empty input is the round-trip partner of toString()
                // on a default-constructed Timecode (which renders as
                // the canonical "--:--:--:--" sentinel) and itself
                // produces an invalid Timecode rather than an error.
                auto [tc, err] = Timecode::fromString("");
                CHECK(err.isOk());
                CHECK_FALSE(tc.isValid());
        }
}

TEST_CASE("Timecode toString") {
        SUBCASE("DF30 default format uses semicolon") {
                Timecode tc(Timecode::DF30, 1, 2, 3, 4);
                auto [s, e] = tc.toFormatString();
                CHECK(e.isOk());
                CHECK(s == String("01:02:03;04"));
        }

        SUBCASE("NDF30 default format uses colons") {
                Timecode tc(Timecode::NDF30, 1, 2, 3, 4);
                auto [s, e] = tc.toFormatString();
                CHECK(e.isOk());
                CHECK(s == String("01:02:03:04"));
        }

        SUBCASE("NDF24") {
                Timecode tc(Timecode::NDF24, 0, 0, 0, 23);
                auto [s, e] = tc.toFormatString();
                CHECK(e.isOk());
                CHECK(s == String("00:00:00:23"));
        }

        SUBCASE("NDF25") {
                Timecode tc(Timecode::NDF25, 12, 34, 56, 24);
                auto [s, e] = tc.toFormatString();
                CHECK(e.isOk());
                CHECK(s == String("12:34:56:24"));
        }

        SUBCASE("SMPTE with fps suffix") {
                Timecode tc(Timecode::NDF30, 1, 2, 3, 4);
                auto [s, e] = tc.toFormatString(&VTC_STR_FMT_SMPTE_WITH_FPS);
                CHECK(e.isOk());
                CHECK(s == String("01:02:03:04/30"));
        }

        SUBCASE("Invalid timecode renders as the canonical sentinel") {
                Timecode tc;
                auto [s, e] = tc.toFormatString();
                CHECK(e.isOk());
                CHECK(s == String("--:--:--:--"));
        }

        SUBCASE("No format renders digits without a frame rate") {
                // Valid digits, no frame rate (Mode(0u, 0u)) — toString
                // lays out the digits ourselves rather than failing.
                Timecode tc(1, 2, 3, 4);
                auto [s, e] = tc.toFormatString();
                CHECK(e.isOk());
                CHECK(s == String("01:02:03:04"));
        }
}

TEST_CASE("Timecode toFrameNumber") {
        SUBCASE("Zero is zero") {
                Timecode    tc(Timecode::NDF30, 0, 0, 0, 0);
                FrameNumber fn = tc.toFrameNumber();
                CHECK(fn.isValid());
                CHECK(fn.value() == 0);
        }

        SUBCASE("One second at 30fps") {
                Timecode    tc(Timecode::NDF30, 0, 0, 1, 0);
                FrameNumber fn = tc.toFrameNumber();
                CHECK(fn.isValid());
                CHECK(fn.value() == 30);
        }

        SUBCASE("One second at 24fps") {
                Timecode    tc(Timecode::NDF24, 0, 0, 1, 0);
                FrameNumber fn = tc.toFrameNumber();
                CHECK(fn.isValid());
                CHECK(fn.value() == 24);
        }

        SUBCASE("One second at 25fps") {
                Timecode    tc(Timecode::NDF25, 0, 0, 1, 0);
                FrameNumber fn = tc.toFrameNumber();
                CHECK(fn.isValid());
                CHECK(fn.value() == 25);
        }

        SUBCASE("One minute NDF30") {
                Timecode    tc(Timecode::NDF30, 0, 1, 0, 0);
                FrameNumber fn = tc.toFrameNumber();
                CHECK(fn.isValid());
                CHECK(fn.value() == 1800);
        }

        SUBCASE("One hour NDF30 = 108000 frames") {
                Timecode    tc(Timecode::NDF30, 1, 0, 0, 0);
                FrameNumber fn = tc.toFrameNumber();
                CHECK(fn.isValid());
                CHECK(fn.value() == 108000);
        }

        SUBCASE("One hour DF30 = 107892 frames") {
                // SMPTE: 30fps DF drops 2 frames/min except every 10th
                // = 108000 - (54 * 2) = 108000 - 108 = 107892
                Timecode    tc(Timecode::DF30, 1, 0, 0, 0);
                FrameNumber fn = tc.toFrameNumber();
                CHECK(fn.isValid());
                CHECK(fn.value() == 107892);
        }

        SUBCASE("24 hours DF30") {
                // 24 * 107892 = 2589408
                Timecode    tc(Timecode::DF30, 24, 0, 0, 0);
                FrameNumber fn = tc.toFrameNumber();
                CHECK(fn.isValid());
                CHECK(fn.value() == 24 * 107892);
        }

        SUBCASE("Invalid timecode returns Unknown") {
                Timecode    tc;
                FrameNumber fn = tc.toFrameNumber();
                CHECK(fn.isUnknown());
        }

        SUBCASE("No format returns Unknown") {
                Timecode    tc(1, 2, 3, 4);
                FrameNumber fn = tc.toFrameNumber();
                CHECK(fn.isUnknown());
        }
}

TEST_CASE("Timecode fromFrameNumber") {
        SUBCASE("Frame 0") {
                Timecode tc = Timecode::fromFrameNumber(Timecode::NDF30, 0);
                CHECK(tc == Timecode(Timecode::NDF30, 0, 0, 0, 0));
        }

        SUBCASE("Frame 30 = 00:00:01:00 at 30fps") {
                Timecode tc = Timecode::fromFrameNumber(Timecode::NDF30, 30);
                CHECK(tc == Timecode(Timecode::NDF30, 0, 0, 1, 0));
        }

        SUBCASE("Frame 1800 = 00:01:00:00 at 30fps") {
                Timecode tc = Timecode::fromFrameNumber(Timecode::NDF30, 1800);
                CHECK(tc == Timecode(Timecode::NDF30, 0, 1, 0, 0));
        }

        SUBCASE("Frame 24 = 00:00:01:00 at 24fps") {
                Timecode tc = Timecode::fromFrameNumber(Timecode::NDF24, 24);
                CHECK(tc == Timecode(Timecode::NDF24, 0, 0, 1, 0));
        }

        SUBCASE("DF30 frame 107892 = 01:00:00:00") {
                Timecode tc = Timecode::fromFrameNumber(Timecode::DF30, 107892);
                CHECK(tc.hour() == 1);
                CHECK(tc.min() == 0);
                CHECK(tc.sec() == 0);
                CHECK(tc.frame() == 0);
        }

        SUBCASE("Invalid mode returns empty timecode") {
                Timecode::Mode invalid;
                Timecode       tc = Timecode::fromFrameNumber(invalid, 100);
                CHECK_FALSE(tc.isValid());
        }

        SUBCASE("Round trip for all modes") {
                Timecode::Mode modes[] = {Timecode::NDF24, Timecode::NDF25, Timecode::NDF30, Timecode::DF30};
                for (auto &mode : modes) {
                        Timecode    tc(mode, 1, 23, 45, 10);
                        FrameNumber fn = tc.toFrameNumber();
                        CHECK(fn.isValid());
                        Timecode tc2 = Timecode::fromFrameNumber(mode, fn);
                        CHECK(tc == tc2);
                }
        }
}

TEST_CASE("Timecode drop frame behavior") {
        SUBCASE("DF skips frames 0 and 1 at non-10th minutes") {
                // At 00:00:59:29, incrementing should go to 00:01:00:02 (skipping :00 and :01)
                Timecode tc(Timecode::DF30, 0, 0, 59, 29);
                tc++;
                CHECK(tc.hour() == 0);
                CHECK(tc.min() == 1);
                CHECK(tc.sec() == 0);
                CHECK(tc.frame() == 2);
        }

        SUBCASE("DF does NOT skip at 10th minutes") {
                // At 00:09:59:29, incrementing should go to 00:10:00:00 (no skip)
                Timecode tc(Timecode::DF30, 0, 9, 59, 29);
                tc++;
                CHECK(tc.hour() == 0);
                CHECK(tc.min() == 10);
                CHECK(tc.sec() == 0);
                CHECK(tc.frame() == 0);
        }

        SUBCASE("DF does NOT skip at minute 0") {
                // 00:59:59:29 -> 01:00:00:00 (no skip, it's the 0th minute of the hour)
                Timecode tc(Timecode::DF30, 0, 59, 59, 29);
                tc++;
                CHECK(tc.hour() == 1);
                CHECK(tc.min() == 0);
                CHECK(tc.sec() == 0);
                CHECK(tc.frame() == 0);
        }

        SUBCASE("DF does NOT skip at minute 20") {
                Timecode tc(Timecode::DF30, 0, 19, 59, 29);
                tc++;
                CHECK(tc.min() == 20);
                CHECK(tc.frame() == 0);
        }

        SUBCASE("DF skips at minutes 1-9 (except 0)") {
                for (int m = 0; m < 9; m++) {
                        Timecode tc(Timecode::DF30, 0, m, 59, 29);
                        tc++;
                        if ((m + 1) % 10 == 0) {
                                CHECK(tc.frame() == 0);
                        } else {
                                CHECK(tc.frame() == 2);
                        }
                }
        }

        SUBCASE("DF decrement reverses the skip") {
                // 00:01:00:02 decremented should go to 00:00:59:29
                Timecode tc(Timecode::DF30, 0, 1, 0, 2);
                tc--;
                CHECK(tc.hour() == 0);
                CHECK(tc.min() == 0);
                CHECK(tc.sec() == 59);
                CHECK(tc.frame() == 29);
        }

        SUBCASE("NDF does not skip frames") {
                Timecode tc(Timecode::NDF30, 0, 0, 59, 29);
                tc++;
                CHECK(tc.min() == 1);
                CHECK(tc.sec() == 0);
                CHECK(tc.frame() == 0);
        }
}

TEST_CASE("Timecode increment/decrement") {
        SUBCASE("Basic increment") {
                Timecode tc(Timecode::NDF30, 0, 0, 0, 0);
                tc++;
                CHECK(tc.frame() == 1);
        }

        SUBCASE("Frame rollover to next second") {
                Timecode tc(Timecode::NDF30, 0, 0, 0, 29);
                tc++;
                CHECK(tc.sec() == 1);
                CHECK(tc.frame() == 0);
        }

        SUBCASE("Second rollover to next minute") {
                Timecode tc(Timecode::NDF30, 0, 0, 59, 29);
                tc++;
                CHECK(tc.min() == 1);
                CHECK(tc.sec() == 0);
                CHECK(tc.frame() == 0);
        }

        SUBCASE("Minute rollover to next hour") {
                Timecode tc(Timecode::NDF30, 0, 59, 59, 29);
                tc++;
                CHECK(tc.hour() == 1);
                CHECK(tc.min() == 0);
                CHECK(tc.sec() == 0);
                CHECK(tc.frame() == 0);
        }

        SUBCASE("24fps frame rollover at 23") {
                Timecode tc(Timecode::NDF24, 0, 0, 0, 23);
                tc++;
                CHECK(tc.sec() == 1);
                CHECK(tc.frame() == 0);
        }

        SUBCASE("25fps frame rollover at 24") {
                Timecode tc(Timecode::NDF25, 0, 0, 0, 24);
                tc++;
                CHECK(tc.sec() == 1);
                CHECK(tc.frame() == 0);
        }

        SUBCASE("Basic decrement") {
                Timecode tc(Timecode::NDF30, 0, 0, 0, 5);
                tc--;
                CHECK(tc.frame() == 4);
        }

        SUBCASE("Decrement borrows from second") {
                Timecode tc(Timecode::NDF30, 0, 0, 1, 0);
                tc--;
                CHECK(tc.sec() == 0);
                CHECK(tc.frame() == 29);
        }

        SUBCASE("Decrement borrows from minute") {
                Timecode tc(Timecode::NDF30, 0, 1, 0, 0);
                tc--;
                CHECK(tc.min() == 0);
                CHECK(tc.sec() == 59);
                CHECK(tc.frame() == 29);
        }

        SUBCASE("Decrement borrows from hour") {
                Timecode tc(Timecode::NDF30, 1, 0, 0, 0);
                tc--;
                CHECK(tc.hour() == 0);
                CHECK(tc.min() == 59);
                CHECK(tc.sec() == 59);
                CHECK(tc.frame() == 29);
        }

        SUBCASE("Post-increment returns old value") {
                Timecode tc(Timecode::NDF30, 0, 0, 0, 5);
                Timecode old = tc++;
                CHECK(old.frame() == 5);
                CHECK(tc.frame() == 6);
        }

        SUBCASE("Post-decrement returns old value") {
                Timecode tc(Timecode::NDF30, 0, 0, 0, 5);
                Timecode old = tc--;
                CHECK(old.frame() == 5);
                CHECK(tc.frame() == 4);
        }
}

TEST_CASE("Timecode NDF round trips") {
        // Shorter round trips for NDF modes (1 hour each)
        Timecode::TimecodeType modes[] = {Timecode::NDF24, Timecode::NDF25, Timecode::NDF30};
        int                    fpsValues[] = {24, 25, 30};

        for (int mi = 0; mi < 3; mi++) {
                auto mode = modes[mi];
                int  fps = fpsValues[mi];
                int  framesPerHour = fps * 3600;

                CAPTURE(fps);

                Timecode tc(mode);
                CHECK(tc.toFrameNumber().value() == 0);

                bool correct = true;
                for (int i = 0; i < framesPerHour; i++) {
                        tc++;
                        FrameNumber fn = tc.toFrameNumber();
                        if (!fn.isValid() || fn.value() != int64_t(i + 1)) {
                                correct = false;
                                break;
                        }
                        Timecode rt = Timecode::fromFrameNumber(tc.mode(), fn);
                        if (rt != tc) {
                                correct = false;
                                break;
                        }
                }
                CHECK(correct);

                // Should be at exactly 01:00:00:00
                CHECK(tc.hour() == 1);
                CHECK(tc.min() == 0);
                CHECK(tc.sec() == 0);
                CHECK(tc.frame() == 0);
                CHECK(tc.toFrameNumber().value() == int64_t(framesPerHour));
        }
}

TEST_CASE("Timecode DF30 full 24h round trip") {
        const int framesToTest = 24 * 60 * 60 * 30;

        Timecode tc(Timecode::DF30);
        CHECK(tc.toFrameNumber().value() == 0);

        bool fnumCorrect = true;
        int  last = 0;
        for (int i = 0; i < framesToTest; i++) {
                tc++;
                int fnum = static_cast<int>(tc.toFrameNumber().value());
                if (fnum != i + 1 || last + 1 != fnum) {
                        fnumCorrect = false;
                        break;
                }
                Timecode rt = Timecode::fromFrameNumber(tc.mode(), FrameNumber(fnum));
                if (rt != tc) {
                        fnumCorrect = false;
                        break;
                }
                last = fnum;
        }
        CHECK(fnumCorrect);
        CHECK(tc.toFrameNumber().value() == int64_t(framesToTest));

        // Decrement all the way back
        for (int i = 0; i < framesToTest; i++) tc--;
        CHECK(tc.toFrameNumber().value() == 0);
        CHECK(tc.hour() == 0);
        CHECK(tc.min() == 0);
        CHECK(tc.sec() == 0);
        CHECK(tc.frame() == 0);
}

TEST_CASE("Timecode vtcFormat access") {
        Timecode         tc(Timecode::DF30, 1, 0, 0, 0);
        const VtcFormat *fmt = tc.vtcFormat();
        CHECK(fmt != nullptr);
        CHECK(vtc_format_is_drop_frame(fmt));
        CHECK(fmt->tc_fps == 30);
}

// ============================================================================
// Invalid / format-less toString() and round-trip through fromString()
// ============================================================================

TEST_CASE("Timecode invalid renders as the canonical sentinel string") {
        Timecode tc; // default-constructed; isValid() == false
        REQUIRE_FALSE(tc.isValid());
        CHECK(tc.toString() == String("--:--:--:--"));
}

TEST_CASE("Timecode format-less renders as plain digits") {
        // 4-arg constructor binds Mode(0u, 0u): valid digits, no
        // frame rate.  toString must lay out the digits ourselves
        // since libvtc has no format pointer to consult.
        Timecode tc(1, 23, 45, 12);
        REQUIRE(tc.isValid());
        CHECK_FALSE(tc.mode().hasFormat());
        CHECK(tc.toString() == String("01:23:45:12"));
}

TEST_CASE("Timecode fromString accepts the invalid sentinel") {
        auto r = Timecode::fromString(String("--:--:--:--"));
        REQUIRE(r.second().isOk());
        CHECK_FALSE(r.first().isValid());
        // And it round-trips: the invalid Timecode renders back to
        // the same sentinel string.
        CHECK(r.first().toString() == String("--:--:--:--"));
}

TEST_CASE("Timecode fromString accepts empty as invalid") {
        auto r = Timecode::fromString(String());
        REQUIRE(r.second().isOk());
        CHECK_FALSE(r.first().isValid());
}

TEST_CASE("Timecode fromBcd64 produces format-less digits when given no mode") {
        // Encode 01:23:45:12 NDF25, then decode without supplying a
        // mode hint.  The result should carry the original digits and
        // be valid (so it renders to "01:23:45:12") but should fail
        // toFrameNumber because no rate is known.
        Timecode       src(Timecode::NDF25, 1, 23, 45, 12);
        const uint64_t bcd = src.toBcd64(TimecodePackFormat::Vitc);

        auto r = Timecode::fromBcd64(bcd); // no mode, no fmt — defaults
        REQUIRE(r.second().isOk());
        const Timecode &tc = r.first();
        CHECK(tc.isValid());
        CHECK_FALSE(tc.mode().hasFormat());
        CHECK(tc.hour() == 1);
        CHECK(tc.min() == 23);
        CHECK(tc.sec() == 45);
        CHECK(tc.frame() == 12);

        CHECK(tc.toString() == String("01:23:45:12"));

        FrameNumber rf = tc.toFrameNumber();
        CHECK(rf.isUnknown());
}

// ============================================================================
// 64-bit BCD time-address packing
// ============================================================================

TEST_CASE("Timecode BCD64 round-trip Vitc 25fps") {
        // 25 fps does not support drop-frame so the DF bit must stay clear
        // throughout, which lets us round-trip cleanly without any rate
        // re-resolution complications.
        Timecode       tc(Timecode::NDF25, 12, 34, 56, 18);
        const uint64_t bcd = tc.toBcd64(TimecodePackFormat::Vitc);

        auto rt = Timecode::fromBcd64(bcd, TimecodePackFormat::Vitc, tc.mode());
        REQUIRE(rt.second().isOk());
        const Timecode &back = rt.first();
        CHECK(back.hour() == 12);
        CHECK(back.min() == 34);
        CHECK(back.sec() == 56);
        CHECK(back.frame() == 18);
        CHECK(back.fps() == 25);
        CHECK_FALSE(back.isDropFrame());
}

TEST_CASE("Timecode BCD64 round-trip Vitc 29.97 DF") {
        Timecode       tc(Timecode::DF30, 1, 23, 45, 12);
        const uint64_t bcd = tc.toBcd64(TimecodePackFormat::Vitc);
        // Drop-frame bit must be set on the wire.
        CHECK(((bcd >> 10) & 1u) != 0u);

        auto rt = Timecode::fromBcd64(bcd, TimecodePackFormat::Vitc, tc.mode());
        REQUIRE(rt.second().isOk());
        const Timecode &back = rt.first();
        CHECK(back.hour() == 1);
        CHECK(back.min() == 23);
        CHECK(back.sec() == 45);
        CHECK(back.frame() == 12);
        CHECK(back.isDropFrame());
}

TEST_CASE("Timecode BCD64 round-trip Ltc 25fps") {
        Timecode       tc(Timecode::NDF25, 5, 10, 15, 20);
        const uint64_t bcd = tc.toBcd64(TimecodePackFormat::Ltc);

        auto rt = Timecode::fromBcd64(bcd, TimecodePackFormat::Ltc, tc.mode());
        REQUIRE(rt.second().isOk());
        const Timecode &back = rt.first();
        CHECK(back.hour() == 5);
        CHECK(back.min() == 10);
        CHECK(back.sec() == 15);
        CHECK(back.frame() == 20);
        CHECK(back.fps() == 25);
}

TEST_CASE("Timecode BCD64 round-trip Ltc 29.97 DF") {
        Timecode       tc(Timecode::DF30, 1, 23, 45, 12);
        const uint64_t bcd = tc.toBcd64(TimecodePackFormat::Ltc);
        CHECK(((bcd >> 10) & 1u) != 0u);

        auto rt = Timecode::fromBcd64(bcd, TimecodePackFormat::Ltc, tc.mode());
        REQUIRE(rt.second().isOk());
        const Timecode &back = rt.first();
        CHECK(back.hour() == 1);
        CHECK(back.min() == 23);
        CHECK(back.sec() == 45);
        CHECK(back.frame() == 12);
        CHECK(back.isDropFrame());
}

TEST_CASE("Timecode BCD64 nibble layout") {
        // Verify that the BCD digits land at the documented nibble
        // positions in Vitc mode (which is the canonical layout).
        Timecode       tc(Timecode::NDF25, 9, 8, 7, 6);
        const uint64_t bcd = tc.toBcd64(TimecodePackFormat::Vitc);

        // Frame units = 6 in bits 0..3
        CHECK(((bcd >> 0) & 0xfu) == 6u);
        // Frame tens = 0 in bits 8..9
        CHECK(((bcd >> 8) & 0x3u) == 0u);
        // Seconds units = 7 in bits 16..19
        CHECK(((bcd >> 16) & 0xfu) == 7u);
        // Seconds tens = 0 in bits 24..26
        CHECK(((bcd >> 24) & 0x7u) == 0u);
        // Minute units = 8 in bits 32..35
        CHECK(((bcd >> 32) & 0xfu) == 8u);
        // Minute tens = 0 in bits 40..42
        CHECK(((bcd >> 40) & 0x7u) == 0u);
        // Hour units = 9 in bits 48..51
        CHECK(((bcd >> 48) & 0xfu) == 9u);
        // Hour tens = 0 in bits 56..57
        CHECK(((bcd >> 56) & 0x3u) == 0u);
}

TEST_CASE("Timecode BCD64 unknown mode + DF flag infers 29.97 DF") {
        // Pack at 29.97 DF, then unpack supplying an empty mode — the DF
        // flag in the BCD must drive inference of 29.97 DF.
        Timecode       src(Timecode::DF30, 0, 1, 0, 0);
        const uint64_t bcd = src.toBcd64(TimecodePackFormat::Vitc);

        auto rt = Timecode::fromBcd64(bcd, TimecodePackFormat::Vitc, Timecode::Mode());
        REQUIRE(rt.second().isOk());
        const Timecode &back = rt.first();
        CHECK(back.isValid());
        CHECK(back.isDropFrame());
        CHECK(back.fps() == 30);
}

TEST_CASE("Timecode BCD64 NDF mode + DF flag upgrades to DF sister") {
        // Build a BCD word with DF=1 by hand at NDF digits.
        Timecode       src(Timecode::DF30, 0, 0, 1, 0);
        const uint64_t bcd = src.toBcd64(TimecodePackFormat::Vitc);
        CHECK(((bcd >> 10) & 1u) != 0u);

        // Caller supplies 29.97 NDF mode (which does not match the BCD's
        // DF flag) — the unpacker must upgrade to the DF sister.
        Timecode::Mode ndf(&VTC_FORMAT_29_97_NDF);
        auto           rt = Timecode::fromBcd64(bcd, TimecodePackFormat::Vitc, ndf);
        REQUIRE(rt.second().isOk());
        const Timecode &back = rt.first();
        CHECK(back.isDropFrame());
        CHECK(back.fps() == 30);
}

TEST_CASE("Timecode BCD64 DF flag with non-DF rate is an error") {
        // Pack a 29.97 DF timecode, then try to unpack it as 24 fps.
        // 24 fps has no DF sister, so the inconsistency must surface.
        Timecode       src(Timecode::DF30, 0, 0, 1, 0);
        const uint64_t bcd = src.toBcd64(TimecodePackFormat::Vitc);
        CHECK(((bcd >> 10) & 1u) != 0u);

        Timecode::Mode ndf24(&VTC_FORMAT_24);
        auto           rt = Timecode::fromBcd64(bcd, TimecodePackFormat::Vitc, ndf24);
        CHECK(rt.second().isError());
        CHECK(rt.second().code() == Error::ConversionFailed);
}

TEST_CASE("Timecode BCD64 Vitc field marker carries first-field bit") {
        // The VITC variant's bit 27 doubles as the HFR frame-pair / field-1
        // marker.  Toggle the FirstField flag and verify the round-trip.
        // Field marker is only meaningful in Vitc mode.
        Timecode src(Timecode::NDF25, 0, 0, 0, 0);
        // No public setter for FirstField — round-trip via a known-good
        // first-field round-trip is sufficient: pack the cleared form and
        // verify bit 27 stays clear, since FirstField defaults to off.
        const uint64_t bcd = src.toBcd64(TimecodePackFormat::Vitc);
        CHECK(((bcd >> 27) & 1u) == 0u);

        // Build a synthetic word with bit 27 set and verify the unpacked
        // Timecode reports first-field on.
        const uint64_t synth = bcd | (uint64_t(1) << 27);
        auto           rt = Timecode::fromBcd64(synth, TimecodePackFormat::Vitc, src.mode());
        REQUIRE(rt.second().isOk());
        CHECK(rt.first().isFirstField());
}

// ============================================================================
// Phase 2 expansion: HFR TimecodeType enum, Mode(fps, flags) lookup,
// colorFrame + userbits fields, toRuntime, DataStream v2.
// ============================================================================

TEST_CASE("Timecode::Mode resolves every TimecodeType to the matching libvtc format") {
        struct Case {
                Timecode::TimecodeType type;
                uint32_t expectedFps;
                bool     expectDf;
        };
        const Case cases[] = {
                {Timecode::NDF24, 24, false},   {Timecode::NDF25, 25, false},
                {Timecode::NDF30, 30, false},   {Timecode::DF30, 30, true},
                {Timecode::NDF48, 48, false},   {Timecode::NDF50, 50, false},
                {Timecode::NDF60, 60, false},   {Timecode::DF60, 60, true},
                {Timecode::NDF72, 72, false},   {Timecode::NDF96, 96, false},
                {Timecode::NDF100, 100, false}, {Timecode::NDF120, 120, false},
                {Timecode::DF120, 120, true},   {Timecode::NDF120_24x5, 120, false},
        };
        for (const auto &c : cases) {
                Timecode::Mode m(c.type);
                CAPTURE(static_cast<int>(c.type));
                CHECK(m.isValid());
                CHECK(m.hasFormat());
                CHECK(m.fps() == c.expectedFps);
                CHECK(m.isDropFrame() == c.expectDf);
        }
}

TEST_CASE("Timecode::Mode(fps, flags) matches the standard HFR formats") {
        // Before Phase 2 this called vtc_format_find_or_create(fps, 1, fps, …)
        // which never matched an HFR format and silently created a malformed
        // custom one.  After the fix it walks VTC_STANDARD_FORMATS and locks
        // onto the correct ST 12-3 format pointer.
        Timecode::Mode m60(60u, 0u);
        CHECK(m60.fps() == 60);
        CHECK_FALSE(m60.isDropFrame());
        CHECK(m60.vtcFormat() == &VTC_FORMAT_60);

        Timecode::Mode m5994df(60u, Timecode::DropFrame);
        CHECK(m5994df.fps() == 60);
        CHECK(m5994df.isDropFrame());
        CHECK(m5994df.vtcFormat() == &VTC_FORMAT_59_94_DF);

        Timecode::Mode m120(120u, 0u);
        CHECK(m120.fps() == 120);
        // Either integer-rate 120 fps variant (24x5 or 30x4) is acceptable;
        // both produce identical digit math.  Use TimecodeType for an
        // unambiguous choice.
        CHECK((m120.vtcFormat() == &VTC_FORMAT_120_24X5 ||
               m120.vtcFormat() == &VTC_FORMAT_120_30X4));

        Timecode::Mode m11988df(120u, Timecode::DropFrame);
        CHECK(m11988df.fps() == 120);
        CHECK(m11988df.isDropFrame());
        CHECK(m11988df.vtcFormat() == &VTC_FORMAT_119_88_DF);
}

TEST_CASE("Timecode at HFR walks 0..fps-1 across one second") {
        // 60p: per-frame Timecode digits should walk 0..59 across one
        // wall-clock second.  Drop-frame variants land later.
        Timecode tc(Timecode::NDF60, 1, 0, 0, 0);
        for (uint32_t f = 0; f < 60; ++f) {
                CHECK(tc.frame() == f);
                CHECK(tc.sec() == 0);
                ++tc;
        }
        CHECK(tc.frame() == 0);
        CHECK(tc.sec() == 1);
}

TEST_CASE("Timecode at 120p walks 0..119 across one second") {
        Timecode tc(Timecode::NDF120, 0, 1, 2, 0);
        for (uint32_t f = 0; f < 120; ++f) {
                CHECK(tc.frame() == f);
                CHECK(tc.sec() == 2);
                ++tc;
        }
        CHECK(tc.frame() == 0);
        CHECK(tc.sec() == 3);
}

TEST_CASE("Timecode DF60 (59.94) drops frames at minute boundaries") {
        // 59.94 DF skips frames 0-3 at every minute except minutes 0,10,20…
        Timecode tc(Timecode::DF60, 0, 0, 59, 59);
        ++tc;
        // At minute 1 the first valid frame is 4 (frames 0..3 dropped at HFR).
        CHECK(tc.min() == 1);
        CHECK(tc.sec() == 0);
        CHECK(tc.frame() == 4);
}

TEST_CASE("Timecode DF120 (119.88) drops 8 frames at minute boundaries") {
        Timecode tc(Timecode::DF120, 0, 0, 59, 119);
        ++tc;
        CHECK(tc.min() == 1);
        CHECK(tc.sec() == 0);
        CHECK(tc.frame() == 8);
}

TEST_CASE("Timecode digit walk: every TimecodeType produces fps frames per second") {
        // For every standard TimecodeType, construct a fresh Timecode
        // and increment through one wall-clock second.  Verify the
        // digit sequence walks 0..fps-1 then rolls into the next
        // second.  NDF rates produce exactly fps frames per second;
        // the three DF rates produce exact wall-clock seconds at the
        // 1000/1001 fraction but still walk the full 0..fps-1 digit
        // sequence within a single second that isn't at a drop
        // boundary.
        struct Case {
                        Timecode::TimecodeType type;
                        uint32_t               fps;
        };
        const Case cases[] = {
                {Timecode::NDF24, 24},        {Timecode::NDF25, 25},
                {Timecode::NDF30, 30},        {Timecode::DF30, 30},
                {Timecode::NDF48, 48},        {Timecode::NDF50, 50},
                {Timecode::NDF60, 60},        {Timecode::DF60, 60},
                {Timecode::NDF72, 72},        {Timecode::NDF96, 96},
                {Timecode::NDF100, 100},      {Timecode::NDF120, 120},
                {Timecode::DF120, 120},       {Timecode::NDF120_24x5, 120},
        };
        for (const Case &c : cases) {
                CAPTURE(static_cast<int>(c.type));
                // Land at sec 30 so we're well clear of any minute-
                // boundary drop pattern for DF rates.
                Timecode tc(Timecode::Mode(c.type), 0, 0, 30, 0);
                CHECK(tc.fps() == c.fps);
                for (uint32_t f = 0; f < c.fps; ++f) {
                        CHECK(tc.frame() == f);
                        CHECK(tc.sec() == 30);
                        ++tc;
                }
                // After fps increments the second rolls over.
                CHECK(tc.sec() == 31);
                CHECK(tc.frame() == 0);
        }
}

TEST_CASE("Timecode DF30 drop pattern: walks 2 minutes, fires drop at 01..09, skips at 10") {
        // ST 12-1 §5.2.2: drop-frame format skips frames 00 and 01 at
        // every minute boundary EXCEPT every 10th minute.  Walk from
        // 00:00:59:29 forward and inspect every minute transition for
        // 11 minutes to cover the full 00..10 pattern.
        Timecode tc(Timecode::DF30, 0, 0, 59, 29);
        for (uint8_t m = 1; m <= 10; ++m) {
                ++tc;
                CHECK(tc.min() == m);
                CHECK(tc.sec() == 0);
                if (m == 10) {
                        // Minute 10 KEEPS frame 0 (no drop).
                        CHECK(tc.frame() == 0);
                } else {
                        // Minutes 1..9 drop frames 0 and 1 → first valid frame is 2.
                        CHECK(tc.frame() == 2);
                }
                // Advance to the end of this minute (one frame short of
                // the next minute boundary) without re-checking every
                // frame; libvtc's drop pattern is exercised exhaustively
                // elsewhere.  Walk until we land at xx:59:29.
                while (!(tc.sec() == 59 && tc.frame() == 29)) ++tc;
        }
}

TEST_CASE("Timecode DF60 drop pattern: 4 frames per minute except minute 10") {
        // ST 12-3 / Phase 1: HFR DF compensation scales the per-minute
        // drop by the HFR multiplier.  59.94 DF (N=2) drops 4 frames
        // per minute; the every-10th-minute exception is preserved.
        Timecode tc(Timecode::DF60, 0, 0, 59, 59);
        for (uint8_t m = 1; m <= 10; ++m) {
                ++tc;
                CHECK(tc.min() == m);
                CHECK(tc.sec() == 0);
                if (m == 10) {
                        CHECK(tc.frame() == 0);
                } else {
                        CHECK(tc.frame() == 4);
                }
                while (!(tc.sec() == 59 && tc.frame() == 59)) ++tc;
        }
}

TEST_CASE("Timecode DF120 drop pattern: 8 frames per minute except minute 10") {
        // 119.88 DF (N=4) drops 8 frames per minute via the same
        // multiplier rule as DF60.
        Timecode tc(Timecode::DF120, 0, 0, 59, 119);
        for (uint8_t m = 1; m <= 10; ++m) {
                ++tc;
                CHECK(tc.min() == m);
                CHECK(tc.sec() == 0);
                if (m == 10) {
                        CHECK(tc.frame() == 0);
                } else {
                        CHECK(tc.frame() == 8);
                }
                while (!(tc.sec() == 59 && tc.frame() == 119)) ++tc;
        }
}

TEST_CASE("Timecode colorFrame round-trips via setter / getter and ==") {
        Timecode a(Timecode::NDF25, 1, 2, 3, 4);
        Timecode b(Timecode::NDF25, 1, 2, 3, 4);
        CHECK(a == b);
        a.setColorFrame(true);
        CHECK(a.colorFrame());
        CHECK_FALSE(b.colorFrame());
        CHECK(a != b);
        b.setColorFrame(true);
        CHECK(a == b);
}

TEST_CASE("Timecode userbits round-trip and participate in equality") {
        Timecode a(Timecode::NDF30, 0, 0, 0, 0);
        Timecode b(Timecode::NDF30, 0, 0, 0, 0);
        CHECK(a == b);
        a.setUserbits(TimecodeUserbits::fromRawBits(0xDEADBEEFu, TimecodeUserbits::ClockTime));
        CHECK(a.userbits().toUint32() == 0xDEADBEEFu);
        CHECK(a.userbits().mode() == TimecodeUserbits::ClockTime);
        CHECK(a != b);
}

TEST_CASE("Timecode DataStream v2 round-trip preserves colorFrame + userbits") {
        Timecode src(Timecode::NDF30, 1, 2, 3, 4);
        src.setColorFrame(true);
        src.setUserbits(TimecodeUserbits::fromRawBits(0xCAFE1234u, TimecodeUserbits::DateTimeZone));

        Buffer         buf(4096);
        BufferIODevice dev(&buf);
        dev.open(IODevice::ReadWrite);
        DataStream ws = DataStream::createWriter(&dev);
        ws << src;
        REQUIRE(ws.status() == DataStream::Ok);

        dev.seek(0);
        DataStream rs = DataStream::createReader(&dev);
        Timecode dst;
        rs >> dst;
        REQUIRE(rs.status() == DataStream::Ok);
        CHECK(dst == src);
        CHECK(dst.colorFrame());
        CHECK(dst.userbits().toUint32() == 0xCAFE1234u);
        CHECK(dst.userbits().mode() == TimecodeUserbits::DateTimeZone);
}

TEST_CASE("Timecode DataStream v1 wire body reads cleanly into a v2 Timecode") {
        // v1 layout (pre-Phase 2): modeFps (u32), flags (u32),
        // hour/min/sec/frame (4 × u8).  No colorFrame, no userbits.
        // Hand-build a v1 body and feed it through readFromStream<1>
        // directly to verify the v1 reader path still works after
        // Phase 2's v2 bump.
        Buffer         buf(256);
        BufferIODevice dev(&buf);
        dev.open(IODevice::ReadWrite);
        {
                DataStream w = DataStream::createWriter(&dev);
                // modeFps = 30 (NDF30 digit family), flags = ModeValid
                // (high bit) | DropFrame (bit 0) for DF30.  The v1
                // reader splits the high bit off via the same
                // TimecodeFlagsModeValid sentinel the writer uses.
                w << uint32_t(30);
                w << uint32_t(0x80000001u);   // ModeValid + DropFrame
                w << uint8_t(1) << uint8_t(0) << uint8_t(0) << uint8_t(2);
                REQUIRE(w.status() == DataStream::Ok);
        }
        dev.seek(0);
        DataStream r = DataStream::createReader(&dev);
        auto rr = Timecode::readFromStream<1>(r);
        REQUIRE(rr.second().isOk());
        Timecode out = rr.first();
        CHECK(out.hour() == 1);
        CHECK(out.sec() == 0);
        CHECK(out.frame() == 2);
        CHECK(out.isDropFrame());
        // v1 wire body had no colorFrame / userbits → defaults.
        CHECK_FALSE(out.colorFrame());
        CHECK(out.userbits().toUint32() == 0u);
        CHECK(out.userbits().mode() == TimecodeUserbits::Unspecified);
}

TEST_CASE("Timecode::toRuntime at integer 30 fps equals seconds * 1s (within rounding)") {
        Timecode tc(Timecode::NDF30, 0, 1, 2, 0);
        FrameRate rate(FrameRate::RationalType(30, 1));
        auto r = tc.toRuntime(rate);
        REQUIRE(r.second().isOk());
        // 0:01:02:00 at 30 fps = 62 seconds.  The per-frame period is
        // computed by FrameRate::frameDuration() as nanoseconds rounded
        // to an integer, so a 30 fps period truncates to 33333333 ns;
        // 62 of those land ~20 microseconds short of the true value.
        // Bound the tolerance to one millisecond so the test catches
        // gross errors without depending on rounding-mode details.
        const int64_t expectedNs = 62LL * 1'000'000'000LL;
        const int64_t deltaNs    = r.first().nanoseconds() - expectedNs;
        CHECK(deltaNs > -1'000'000);
        CHECK(deltaNs < 1'000'000);
}

TEST_CASE("Timecode::toRuntime at NTSC 29.97 differs from integer 30 fps") {
        // Same digits → different wall-clock at fractional rate.  This is
        // exactly the property toRuntime exists to surface: the digit-family
        // alone can't predict wall-clock seconds.
        Timecode tc(Timecode::NDF30, 0, 1, 0, 0);
        auto int30 = tc.toRuntime(FrameRate(FrameRate::RationalType(30, 1)));
        auto ntsc  = tc.toRuntime(FrameRate(FrameRate::RationalType(30000, 1001)));
        REQUIRE(int30.second().isOk());
        REQUIRE(ntsc.second().isOk());
        CHECK(int30.first().nanoseconds() != ntsc.first().nanoseconds());
        // NTSC frames are slightly longer, so wall-clock is bigger.
        CHECK(ntsc.first().nanoseconds() > int30.first().nanoseconds());
}

TEST_CASE("Timecode::toRuntime fails on invalid rate or invalid timecode") {
        Timecode tc(Timecode::NDF30, 0, 0, 0, 0);
        FrameRate bad; // default = invalid
        auto r1 = tc.toRuntime(bad);
        CHECK(r1.second().isError());

        Timecode invalid;
        auto r2 = invalid.toRuntime(FrameRate(FrameRate::RationalType(30, 1)));
        CHECK(r2.second().isError());
}

// ============================================================================
// Super-frame / sub-frame accessor cluster (ST 12-3 §6.1 / §6.3).
// ============================================================================

TEST_CASE("Timecode::Mode::framesPerSuperFrame matches ST 12-3 N values") {
        CHECK(Timecode::Mode(Timecode::NDF24).framesPerSuperFrame() == 1);
        CHECK(Timecode::Mode(Timecode::NDF25).framesPerSuperFrame() == 1);
        CHECK(Timecode::Mode(Timecode::NDF30).framesPerSuperFrame() == 1);
        CHECK(Timecode::Mode(Timecode::DF30).framesPerSuperFrame() == 1);
        CHECK(Timecode::Mode(Timecode::NDF48).framesPerSuperFrame() == 2);
        CHECK(Timecode::Mode(Timecode::NDF50).framesPerSuperFrame() == 2);
        CHECK(Timecode::Mode(Timecode::NDF60).framesPerSuperFrame() == 2);
        CHECK(Timecode::Mode(Timecode::DF60).framesPerSuperFrame() == 2);
        CHECK(Timecode::Mode(Timecode::NDF72).framesPerSuperFrame() == 3);
        CHECK(Timecode::Mode(Timecode::NDF96).framesPerSuperFrame() == 4);
        CHECK(Timecode::Mode(Timecode::NDF100).framesPerSuperFrame() == 4);
        CHECK(Timecode::Mode(Timecode::NDF120).framesPerSuperFrame() == 4);
        CHECK(Timecode::Mode(Timecode::DF120).framesPerSuperFrame() == 4);
        CHECK(Timecode::Mode(Timecode::NDF120_24x5).framesPerSuperFrame() == 5);
        CHECK(Timecode::Mode().framesPerSuperFrame() == 1); // no format
}

TEST_CASE("Timecode::Mode::superFrameRate returns tc_fps for each family") {
        CHECK(Timecode::Mode(Timecode::NDF24).superFrameRate() == 24);
        CHECK(Timecode::Mode(Timecode::NDF25).superFrameRate() == 25);
        CHECK(Timecode::Mode(Timecode::NDF30).superFrameRate() == 30);
        CHECK(Timecode::Mode(Timecode::NDF48).superFrameRate() == 24);
        CHECK(Timecode::Mode(Timecode::NDF50).superFrameRate() == 25);
        CHECK(Timecode::Mode(Timecode::NDF60).superFrameRate() == 30);
        CHECK(Timecode::Mode(Timecode::DF60).superFrameRate() == 30);
        CHECK(Timecode::Mode(Timecode::NDF72).superFrameRate() == 24);
        CHECK(Timecode::Mode(Timecode::NDF100).superFrameRate() == 25);
        CHECK(Timecode::Mode(Timecode::NDF120).superFrameRate() == 30);
        CHECK(Timecode::Mode(Timecode::NDF120_24x5).superFrameRate() == 24);
        CHECK(Timecode::Mode().superFrameRate() == 0);
}

TEST_CASE("Timecode::superFrameIndex + subFrameIndex split physical frame at HFR") {
        // 120p (30×4): physical frame 87 → super-frame 21, sub-frame 3.
        Timecode tc(Timecode::NDF120, 0, 0, 0, 87);
        CHECK(tc.superFrameIndex() == 21);
        CHECK(tc.subFrameIndex() == 3);

        // 60p (30×2): physical frame 41 → super-frame 20, sub-frame 1.
        Timecode tc60(Timecode::NDF60, 0, 0, 0, 41);
        CHECK(tc60.superFrameIndex() == 20);
        CHECK(tc60.subFrameIndex() == 1);

        // 120p (24×5): physical frame 119 → super-frame 23, sub-frame 4.
        Timecode tc24x5(Timecode::NDF120_24x5, 0, 0, 0, 119);
        CHECK(tc24x5.superFrameIndex() == 23);
        CHECK(tc24x5.subFrameIndex() == 4);
}

TEST_CASE("Timecode::superFrameIndex degenerates to frame at non-HFR rates") {
        Timecode tc(Timecode::NDF30, 0, 0, 0, 17);
        CHECK(tc.superFrameIndex() == 17);
        CHECK(tc.subFrameIndex() == 0);
}

TEST_CASE("Timecode::isHfr classifies every TimecodeType") {
        CHECK_FALSE(Timecode(Timecode::NDF24, 0, 0, 0, 0).isHfr());
        CHECK_FALSE(Timecode(Timecode::NDF25, 0, 0, 0, 0).isHfr());
        CHECK_FALSE(Timecode(Timecode::NDF30, 0, 0, 0, 0).isHfr());
        CHECK_FALSE(Timecode(Timecode::DF30, 0, 0, 0, 0).isHfr());
        CHECK(Timecode(Timecode::NDF48, 0, 0, 0, 0).isHfr());
        CHECK(Timecode(Timecode::NDF60, 0, 0, 0, 0).isHfr());
        CHECK(Timecode(Timecode::DF60, 0, 0, 0, 0).isHfr());
        CHECK(Timecode(Timecode::NDF120, 0, 0, 0, 0).isHfr());
        CHECK(Timecode(Timecode::NDF120_24x5, 0, 0, 0, 0).isHfr());
}

TEST_CASE("Timecode::isSuperFrameBoundary fires every N physical frames at HFR") {
        // At 60p (N=2) every even physical frame is a super-frame boundary.
        Timecode tc(Timecode::NDF60, 0, 0, 0, 0);
        for (uint32_t f = 0; f < 60; ++f) {
                CAPTURE(f);
                CHECK(tc.isSuperFrameBoundary() == ((f % 2u) == 0u));
                ++tc;
        }
}

TEST_CASE("Timecode::isSuperFrameBoundary fires every 4 frames at 120p (30x4)") {
        Timecode tc(Timecode::NDF120, 0, 0, 0, 0);
        for (uint32_t f = 0; f < 120; ++f) {
                CAPTURE(f);
                CHECK(tc.isSuperFrameBoundary() == ((f % 4u) == 0u));
                ++tc;
        }
}

TEST_CASE("Timecode super-frame helpers handle invalid Timecode safely") {
        Timecode tc;
        CHECK(tc.superFrameIndex() == 0);
        CHECK(tc.subFrameIndex() == 0);
        CHECK_FALSE(tc.isHfr());
        CHECK(tc.isSuperFrameBoundary());
}
