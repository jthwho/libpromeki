/**
 * @file      timecode.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/timecode.h>
#include <promeki/logger.h>

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
                Timecode lit(1, 0, 0, 5);     // no mode
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
                Timecode df30 (Timecode::DF30,  1, 0, 0, 0);
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
                auto [s, e] = tc.toString();
                CHECK(e.isOk());
                CHECK(s == String("01:02:03;04"));
        }

        SUBCASE("NDF30 default format uses colons") {
                Timecode tc(Timecode::NDF30, 1, 2, 3, 4);
                auto [s, e] = tc.toString();
                CHECK(e.isOk());
                CHECK(s == String("01:02:03:04"));
        }

        SUBCASE("NDF24") {
                Timecode tc(Timecode::NDF24, 0, 0, 0, 23);
                auto [s, e] = tc.toString();
                CHECK(e.isOk());
                CHECK(s == String("00:00:00:23"));
        }

        SUBCASE("NDF25") {
                Timecode tc(Timecode::NDF25, 12, 34, 56, 24);
                auto [s, e] = tc.toString();
                CHECK(e.isOk());
                CHECK(s == String("12:34:56:24"));
        }

        SUBCASE("SMPTE with fps suffix") {
                Timecode tc(Timecode::NDF30, 1, 2, 3, 4);
                auto [s, e] = tc.toString(&VTC_STR_FMT_SMPTE_WITH_FPS);
                CHECK(e.isOk());
                CHECK(s == String("01:02:03:04/30"));
        }

        SUBCASE("Invalid timecode renders as the canonical sentinel") {
                Timecode tc;
                auto [s, e] = tc.toString();
                CHECK(e.isOk());
                CHECK(s == String("--:--:--:--"));
        }

        SUBCASE("No format renders digits without a frame rate") {
                // Valid digits, no frame rate (Mode(0u, 0u)) — toString
                // lays out the digits ourselves rather than failing.
                Timecode tc(1, 2, 3, 4);
                auto [s, e] = tc.toString();
                CHECK(e.isOk());
                CHECK(s == String("01:02:03:04"));
        }
}

TEST_CASE("Timecode toFrameNumber") {
        SUBCASE("Zero is zero") {
                Timecode tc(Timecode::NDF30, 0, 0, 0, 0);
                auto [fn, err] = tc.toFrameNumber();
                CHECK(err.isOk());
                CHECK(fn == 0);
        }

        SUBCASE("One second at 30fps") {
                Timecode tc(Timecode::NDF30, 0, 0, 1, 0);
                auto [fn, err] = tc.toFrameNumber();
                CHECK(err.isOk());
                CHECK(fn == 30);
        }

        SUBCASE("One second at 24fps") {
                Timecode tc(Timecode::NDF24, 0, 0, 1, 0);
                auto [fn, err] = tc.toFrameNumber();
                CHECK(err.isOk());
                CHECK(fn == 24);
        }

        SUBCASE("One second at 25fps") {
                Timecode tc(Timecode::NDF25, 0, 0, 1, 0);
                auto [fn, err] = tc.toFrameNumber();
                CHECK(err.isOk());
                CHECK(fn == 25);
        }

        SUBCASE("One minute NDF30") {
                Timecode tc(Timecode::NDF30, 0, 1, 0, 0);
                auto [fn, err] = tc.toFrameNumber();
                CHECK(err.isOk());
                CHECK(fn == 1800);
        }

        SUBCASE("One hour NDF30 = 108000 frames") {
                Timecode tc(Timecode::NDF30, 1, 0, 0, 0);
                auto [fn, err] = tc.toFrameNumber();
                CHECK(err.isOk());
                CHECK(fn == 108000);
        }

        SUBCASE("One hour DF30 = 107892 frames") {
                // SMPTE: 30fps DF drops 2 frames/min except every 10th
                // = 108000 - (54 * 2) = 108000 - 108 = 107892
                Timecode tc(Timecode::DF30, 1, 0, 0, 0);
                auto [fn, err] = tc.toFrameNumber();
                CHECK(err.isOk());
                CHECK(fn == 107892);
        }

        SUBCASE("24 hours DF30") {
                // 24 * 107892 = 2589408
                Timecode tc(Timecode::DF30, 24, 0, 0, 0);
                auto [fn, err] = tc.toFrameNumber();
                CHECK(err.isOk());
                CHECK(fn == 24 * 107892);
        }

        SUBCASE("Invalid timecode returns error") {
                Timecode tc;
                auto [fn, err] = tc.toFrameNumber();
                CHECK_FALSE(err.isOk());
        }

        SUBCASE("No format returns NoFrameRate") {
                Timecode tc(1, 2, 3, 4);
                auto [fn, err] = tc.toFrameNumber();
                CHECK(err == Error::NoFrameRate);
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
                Timecode tc = Timecode::fromFrameNumber(invalid, 100);
                CHECK_FALSE(tc.isValid());
        }

        SUBCASE("Round trip for all modes") {
                Timecode::Mode modes[] = {
                        Timecode::NDF24, Timecode::NDF25, Timecode::NDF30, Timecode::DF30
                };
                for(auto &mode : modes) {
                        Timecode tc(mode, 1, 23, 45, 10);
                        auto [fn, err] = tc.toFrameNumber();
                        CHECK(err.isOk());
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
                for(int m = 0; m < 9; m++) {
                        Timecode tc(Timecode::DF30, 0, m, 59, 29);
                        tc++;
                        if((m + 1) % 10 == 0) {
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
        Timecode::TimecodeType modes[] = { Timecode::NDF24, Timecode::NDF25, Timecode::NDF30 };
        int fpsValues[] = { 24, 25, 30 };

        for(int mi = 0; mi < 3; mi++) {
                auto mode = modes[mi];
                int fps = fpsValues[mi];
                int framesPerHour = fps * 3600;

                CAPTURE(fps);

                Timecode tc(mode);
                CHECK(tc.toFrameNumber().first() == 0);

                bool correct = true;
                for(int i = 0; i < framesPerHour; i++) {
                        tc++;
                        auto [fn, err] = tc.toFrameNumber();
                        if(!err.isOk() || fn != (uint64_t)(i + 1)) {
                                correct = false;
                                break;
                        }
                        Timecode rt = Timecode::fromFrameNumber(tc.mode(), fn);
                        if(rt != tc) {
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
                CHECK(tc.toFrameNumber().first() == (uint64_t)framesPerHour);
        }
}

TEST_CASE("Timecode DF30 full 24h round trip") {
        const int framesToTest = 24 * 60 * 60 * 30;

        Timecode tc(Timecode::DF30);
        CHECK(tc.toFrameNumber().first() == 0);

        bool fnumCorrect = true;
        int last = 0;
        for(int i = 0; i < framesToTest; i++) {
                tc++;
                int fnum = tc.toFrameNumber().first();
                if(fnum != i + 1 || last + 1 != fnum) {
                        fnumCorrect = false;
                        break;
                }
                Timecode rt = Timecode::fromFrameNumber(tc.mode(), fnum);
                if(rt != tc) {
                        fnumCorrect = false;
                        break;
                }
                last = fnum;
        }
        CHECK(fnumCorrect);
        CHECK(tc.toFrameNumber().first() == (uint64_t)framesToTest);

        // Decrement all the way back
        for(int i = 0; i < framesToTest; i++) tc--;
        CHECK(tc.toFrameNumber().first() == 0);
        CHECK(tc.hour() == 0);
        CHECK(tc.min() == 0);
        CHECK(tc.sec() == 0);
        CHECK(tc.frame() == 0);
}

TEST_CASE("Timecode vtcFormat access") {
        Timecode tc(Timecode::DF30, 1, 0, 0, 0);
        const VtcFormat *fmt = tc.vtcFormat();
        CHECK(fmt != nullptr);
        CHECK(vtc_format_is_drop_frame(fmt));
        CHECK(fmt->tc_fps == 30);
}

// ============================================================================
// Invalid / format-less toString() and round-trip through fromString()
// ============================================================================

TEST_CASE("Timecode invalid renders as the canonical sentinel string") {
        Timecode tc;  // default-constructed; isValid() == false
        REQUIRE_FALSE(tc.isValid());
        auto r = tc.toString();
        REQUIRE(r.second().isOk());
        CHECK(r.first() == String("--:--:--:--"));
}

TEST_CASE("Timecode format-less renders as plain digits") {
        // 4-arg constructor binds Mode(0u, 0u): valid digits, no
        // frame rate.  toString must lay out the digits ourselves
        // since libvtc has no format pointer to consult.
        Timecode tc(1, 23, 45, 12);
        REQUIRE(tc.isValid());
        CHECK_FALSE(tc.mode().hasFormat());
        auto r = tc.toString();
        REQUIRE(r.second().isOk());
        CHECK(r.first() == String("01:23:45:12"));
}

TEST_CASE("Timecode fromString accepts the invalid sentinel") {
        auto r = Timecode::fromString(String("--:--:--:--"));
        REQUIRE(r.second().isOk());
        CHECK_FALSE(r.first().isValid());
        // And it round-trips: the invalid Timecode renders back to
        // the same sentinel string.
        auto r2 = r.first().toString();
        REQUIRE(r2.second().isOk());
        CHECK(r2.first() == String("--:--:--:--"));
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
        Timecode src(Timecode::NDF25, 1, 23, 45, 12);
        const uint64_t bcd = src.toBcd64(TimecodePackFormat::Vitc);

        auto r = Timecode::fromBcd64(bcd);  // no mode, no fmt — defaults
        REQUIRE(r.second().isOk());
        const Timecode &tc = r.first();
        CHECK(tc.isValid());
        CHECK_FALSE(tc.mode().hasFormat());
        CHECK(tc.hour() == 1);
        CHECK(tc.min() == 23);
        CHECK(tc.sec() == 45);
        CHECK(tc.frame() == 12);

        auto rs = tc.toString();
        REQUIRE(rs.second().isOk());
        CHECK(rs.first() == String("01:23:45:12"));

        auto rf = tc.toFrameNumber();
        CHECK(rf.second().isError());
        CHECK(rf.second().code() == Error::NoFrameRate);
}

// ============================================================================
// 64-bit BCD time-address packing
// ============================================================================

TEST_CASE("Timecode BCD64 round-trip Vitc 25fps") {
        // 25 fps does not support drop-frame so the DF bit must stay clear
        // throughout, which lets us round-trip cleanly without any rate
        // re-resolution complications.
        Timecode tc(Timecode::NDF25, 12, 34, 56, 18);
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
        Timecode tc(Timecode::DF30, 1, 23, 45, 12);
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
        Timecode tc(Timecode::NDF25, 5, 10, 15, 20);
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
        Timecode tc(Timecode::DF30, 1, 23, 45, 12);
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
        Timecode tc(Timecode::NDF25, 9, 8, 7, 6);
        const uint64_t bcd = tc.toBcd64(TimecodePackFormat::Vitc);

        // Frame units = 6 in bits 0..3
        CHECK(((bcd >>  0) & 0xfu) == 6u);
        // Frame tens = 0 in bits 8..9
        CHECK(((bcd >>  8) & 0x3u) == 0u);
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
        Timecode src(Timecode::DF30, 0, 1, 0, 0);
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
        Timecode src(Timecode::DF30, 0, 0, 1, 0);
        const uint64_t bcd = src.toBcd64(TimecodePackFormat::Vitc);
        CHECK(((bcd >> 10) & 1u) != 0u);

        // Caller supplies 29.97 NDF mode (which does not match the BCD's
        // DF flag) — the unpacker must upgrade to the DF sister.
        Timecode::Mode ndf(&VTC_FORMAT_29_97_NDF);
        auto rt = Timecode::fromBcd64(bcd, TimecodePackFormat::Vitc, ndf);
        REQUIRE(rt.second().isOk());
        const Timecode &back = rt.first();
        CHECK(back.isDropFrame());
        CHECK(back.fps() == 30);
}

TEST_CASE("Timecode BCD64 DF flag with non-DF rate is an error") {
        // Pack a 29.97 DF timecode, then try to unpack it as 24 fps.
        // 24 fps has no DF sister, so the inconsistency must surface.
        Timecode src(Timecode::DF30, 0, 0, 1, 0);
        const uint64_t bcd = src.toBcd64(TimecodePackFormat::Vitc);
        CHECK(((bcd >> 10) & 1u) != 0u);

        Timecode::Mode ndf24(&VTC_FORMAT_24);
        auto rt = Timecode::fromBcd64(bcd, TimecodePackFormat::Vitc, ndf24);
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
        auto rt = Timecode::fromBcd64(synth, TimecodePackFormat::Vitc, src.mode());
        REQUIRE(rt.second().isOk());
        CHECK(rt.first().isFirstField());
}
