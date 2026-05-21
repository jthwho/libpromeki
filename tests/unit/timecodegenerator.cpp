/**
 * @file      timecodegenerator.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/timecodegenerator.h>
#include <promeki/framerate.h>

using namespace promeki;

// ============================================================================
// Default construction
// ============================================================================

TEST_CASE("TimecodeGenerator_Default") {
        TimecodeGenerator gen;
        CHECK(gen.frameRate().numerator() == 0);
        CHECK(!gen.dropFrame());
        CHECK(gen.runMode() == TimecodeGenerator::Forward);
        CHECK(gen.frameCount() == 0);
}

// ============================================================================
// Construction with frame rate
// ============================================================================

TEST_CASE("TimecodeGenerator_ConstructWithRate") {
        TimecodeGenerator gen(FrameRate(FrameRate::FPS_29_97));
        CHECK(gen.frameRate().numerator() == 30000);
        CHECK(gen.frameRate().denominator() == 1001);
        CHECK(!gen.dropFrame());
        CHECK(gen.timecodeMode() == Timecode::Mode(Timecode::NDF30));
}

TEST_CASE("TimecodeGenerator_ConstructWithRateAndDF") {
        TimecodeGenerator gen(FrameRate(FrameRate::FPS_29_97), true);
        CHECK(gen.dropFrame());
        CHECK(gen.timecodeMode() == Timecode::Mode(Timecode::DF30));
}

// ============================================================================
// Mode derivation for standard rates
// ============================================================================

TEST_CASE("TimecodeGenerator_Mode24") {
        TimecodeGenerator gen;
        gen.setFrameRate(FrameRate(FrameRate::FPS_24));
        CHECK(gen.timecodeMode() == Timecode::Mode(Timecode::NDF24));
        CHECK(!gen.dropFrame());
}

TEST_CASE("TimecodeGenerator_Mode23976") {
        TimecodeGenerator gen;
        gen.setFrameRate(FrameRate(FrameRate::FPS_23_98));
        CHECK(gen.timecodeMode() == Timecode::Mode(Timecode::NDF24));
        CHECK(!gen.dropFrame());
}

TEST_CASE("TimecodeGenerator_Mode25") {
        TimecodeGenerator gen;
        gen.setFrameRate(FrameRate(FrameRate::FPS_25));
        CHECK(gen.timecodeMode() == Timecode::Mode(Timecode::NDF25));
        CHECK(!gen.dropFrame());
}

TEST_CASE("TimecodeGenerator_Mode2997NDF") {
        TimecodeGenerator gen;
        gen.setFrameRate(FrameRate(FrameRate::FPS_29_97));
        CHECK(gen.timecodeMode() == Timecode::Mode(Timecode::NDF30));
        CHECK(!gen.dropFrame());
}

TEST_CASE("TimecodeGenerator_Mode2997DF") {
        TimecodeGenerator gen;
        gen.setFrameRate(FrameRate(FrameRate::FPS_29_97));
        gen.setDropFrame(true);
        CHECK(gen.timecodeMode() == Timecode::Mode(Timecode::DF30));
        CHECK(gen.dropFrame());
}

TEST_CASE("TimecodeGenerator_Mode30") {
        TimecodeGenerator gen;
        gen.setFrameRate(FrameRate(FrameRate::FPS_30));
        CHECK(gen.timecodeMode() == Timecode::Mode(Timecode::NDF30));
        CHECK(!gen.dropFrame());
}

// ============================================================================
// Drop-frame forced off at non-29.97 rates
// ============================================================================

TEST_CASE("TimecodeGenerator_DFIgnoredAt24") {
        TimecodeGenerator gen;
        gen.setFrameRate(FrameRate(FrameRate::FPS_24));
        gen.setDropFrame(true);
        CHECK(!gen.dropFrame());
        CHECK(gen.timecodeMode() == Timecode::Mode(Timecode::NDF24));
}

TEST_CASE("TimecodeGenerator_DFIgnoredAt25") {
        TimecodeGenerator gen;
        gen.setFrameRate(FrameRate(FrameRate::FPS_25));
        gen.setDropFrame(true);
        CHECK(!gen.dropFrame());
        CHECK(gen.timecodeMode() == Timecode::Mode(Timecode::NDF25));
}

TEST_CASE("TimecodeGenerator_DFIgnoredAt30") {
        TimecodeGenerator gen;
        gen.setFrameRate(FrameRate(FrameRate::FPS_30));
        gen.setDropFrame(true);
        CHECK(!gen.dropFrame());
}

// ============================================================================
// Forward mode
// ============================================================================

TEST_CASE("TimecodeGenerator_Forward") {
        TimecodeGenerator gen(FrameRate(FrameRate::FPS_24));
        gen.setTimecode(Timecode(Timecode::NDF24, 1, 0, 0, 0));

        Timecode tc0 = gen.advance();
        CHECK(tc0.hour() == 1);
        CHECK(tc0.min() == 0);
        CHECK(tc0.sec() == 0);
        CHECK(tc0.frame() == 0);

        Timecode tc1 = gen.advance();
        CHECK(tc1.frame() == 1);

        Timecode tc2 = gen.advance();
        CHECK(tc2.frame() == 2);

        CHECK(gen.frameCount() == 3);
}

// ============================================================================
// Reverse mode
// ============================================================================

TEST_CASE("TimecodeGenerator_Reverse") {
        TimecodeGenerator gen(FrameRate(FrameRate::FPS_24));
        gen.setTimecode(Timecode(Timecode::NDF24, 1, 0, 0, 5));
        gen.setRunMode(TimecodeGenerator::Reverse);

        Timecode tc0 = gen.advance();
        CHECK(tc0.frame() == 5);

        Timecode tc1 = gen.advance();
        CHECK(tc1.frame() == 4);

        Timecode tc2 = gen.advance();
        CHECK(tc2.frame() == 3);

        CHECK(gen.frameCount() == 3);
}

// ============================================================================
// Still mode
// ============================================================================

TEST_CASE("TimecodeGenerator_Still") {
        TimecodeGenerator gen(FrameRate(FrameRate::FPS_24));
        gen.setTimecode(Timecode(Timecode::NDF24, 1, 0, 0, 0));
        gen.setRunMode(TimecodeGenerator::Still);

        Timecode tc0 = gen.advance();
        Timecode tc1 = gen.advance();
        Timecode tc2 = gen.advance();

        CHECK(tc0 == tc1);
        CHECK(tc1 == tc2);
        CHECK(gen.frameCount() == 3);
}

// ============================================================================
// Jam
// ============================================================================

TEST_CASE("TimecodeGenerator_Jam") {
        TimecodeGenerator gen(FrameRate(FrameRate::FPS_24));
        gen.setTimecode(Timecode(Timecode::NDF24, 1, 0, 0, 0));

        gen.advance();
        gen.advance();
        CHECK(gen.frameCount() == 2);

        // Jam to a new value
        gen.jam(Timecode(Timecode::NDF24, 2, 0, 0, 0));
        Timecode tc = gen.advance();
        CHECK(tc.hour() == 2);
        CHECK(tc.min() == 0);
        CHECK(tc.sec() == 0);
        CHECK(tc.frame() == 0);

        // Should continue from jammed value
        Timecode tc2 = gen.advance();
        CHECK(tc2.hour() == 2);
        CHECK(tc2.frame() == 1);
}

// ============================================================================
// Reset
// ============================================================================

TEST_CASE("TimecodeGenerator_Reset") {
        TimecodeGenerator gen(FrameRate(FrameRate::FPS_24));
        gen.setTimecode(Timecode(Timecode::NDF24, 1, 0, 0, 0));

        gen.advance();
        gen.advance();
        gen.advance();
        CHECK(gen.frameCount() == 3);

        gen.reset();
        CHECK(gen.frameCount() == 0);
        CHECK(gen.timecode() == Timecode(Timecode::NDF24, 1, 0, 0, 0));
}

TEST_CASE("TimecodeGenerator_ResetAfterJam") {
        TimecodeGenerator gen(FrameRate(FrameRate::FPS_24));
        gen.setTimecode(Timecode(Timecode::NDF24, 1, 0, 0, 0));
        gen.jam(Timecode(Timecode::NDF24, 5, 0, 0, 0));

        gen.reset();
        // Should reset to the setTimecode value, not the jammed value
        CHECK(gen.timecode() == Timecode(Timecode::NDF24, 1, 0, 0, 0));
}

// ============================================================================
// Drop-frame skip at minute boundaries (29.97 DF)
// ============================================================================

TEST_CASE("TimecodeGenerator_DFSkip") {
        TimecodeGenerator gen(FrameRate(FrameRate::FPS_29_97), true);
        CHECK(gen.dropFrame());

        // Set TC just before a minute boundary
        gen.setTimecode(Timecode(Timecode::DF30, 0, 0, 59, 28));

        Timecode tc28 = gen.advance(); // 00:00:59:28
        CHECK(tc28.sec() == 59);
        CHECK(tc28.frame() == 28);

        Timecode tc29 = gen.advance(); // 00:00:59:29
        CHECK(tc29.sec() == 59);
        CHECK(tc29.frame() == 29);

        // Next frame should skip 00:01:00:00 and 00:01:00:01 → land on 00:01:00:02
        Timecode tc_skip = gen.advance();
        CHECK(tc_skip.min() == 1);
        CHECK(tc_skip.sec() == 0);
        CHECK(tc_skip.frame() == 2);
}

// ============================================================================
// Phase 3: HFR rate derivation (ST 12-1 §12 + ST 12-3) — every rate above 30
// must lock onto the matching libvtc HFR format and walk the correct digit
// range when advanced.
// ============================================================================

TEST_CASE("TimecodeGenerator_Mode48") {
        TimecodeGenerator gen;
        gen.setFrameRate(FrameRate(FrameRate::FPS_48));
        CHECK(gen.timecodeMode() == Timecode::Mode(Timecode::NDF48));
        CHECK_FALSE(gen.dropFrame());
}

TEST_CASE("TimecodeGenerator_Mode50") {
        TimecodeGenerator gen;
        gen.setFrameRate(FrameRate(FrameRate::FPS_50));
        CHECK(gen.timecodeMode() == Timecode::Mode(Timecode::NDF50));
        CHECK_FALSE(gen.dropFrame());
}

TEST_CASE("TimecodeGenerator_Mode60NDF") {
        TimecodeGenerator gen;
        gen.setFrameRate(FrameRate(FrameRate::FPS_60));
        CHECK(gen.timecodeMode() == Timecode::Mode(Timecode::NDF60));
        CHECK_FALSE(gen.dropFrame());
}

TEST_CASE("TimecodeGenerator_Mode5994NDF") {
        TimecodeGenerator gen;
        gen.setFrameRate(FrameRate(FrameRate::FPS_59_94));
        CHECK(gen.timecodeMode() == Timecode::Mode(Timecode::NDF60));
        CHECK_FALSE(gen.dropFrame());
}

TEST_CASE("TimecodeGenerator_Mode5994DF") {
        TimecodeGenerator gen;
        gen.setFrameRate(FrameRate(FrameRate::FPS_59_94));
        gen.setDropFrame(true);
        CHECK(gen.timecodeMode() == Timecode::Mode(Timecode::DF60));
        CHECK(gen.dropFrame());
}

TEST_CASE("TimecodeGenerator_Mode120NDF") {
        TimecodeGenerator gen;
        gen.setFrameRate(FrameRate(FrameRate::FPS_120));
        CHECK(gen.timecodeMode() == Timecode::Mode(Timecode::NDF120));
        CHECK_FALSE(gen.dropFrame());
}

TEST_CASE("TimecodeGenerator_Mode11988DF") {
        TimecodeGenerator gen;
        gen.setFrameRate(FrameRate(FrameRate::FPS_119_88));
        gen.setDropFrame(true);
        CHECK(gen.timecodeMode() == Timecode::Mode(Timecode::DF120));
        CHECK(gen.dropFrame());
}

TEST_CASE("TimecodeGenerator_Forward60p_walks_0_to_59") {
        TimecodeGenerator gen(FrameRate(FrameRate::FPS_60));
        gen.setTimecode(Timecode(Timecode::NDF60, 0, 0, 0, 0));
        for (uint32_t f = 0; f < 60; ++f) {
                Timecode tc = gen.advance();
                CHECK(tc.frame() == f);
                CHECK(tc.sec() == 0);
        }
        // After 60 advances we should be at 00:00:01:00
        Timecode tcSecond = gen.advance();
        CHECK(tcSecond.sec() == 1);
        CHECK(tcSecond.frame() == 0);
}

TEST_CASE("TimecodeGenerator_5994DF_drops_4_frames_at_minute_boundary") {
        // 59.94 DF skips the first 4 frames at every minute except minutes
        // divisible by 10 (per ST 12-3 §6.4.3 applied to 30×2 HFR).
        TimecodeGenerator gen(FrameRate(FrameRate::FPS_59_94), true);
        REQUIRE(gen.dropFrame());

        gen.setTimecode(Timecode(Timecode::DF60, 0, 0, 59, 59));
        gen.advance(); // consume 00:00:59:59
        Timecode after = gen.advance();
        CHECK(after.min() == 1);
        CHECK(after.sec() == 0);
        CHECK(after.frame() == 4);
}

TEST_CASE("TimecodeGenerator_11988DF_drops_8_frames_at_minute_boundary") {
        TimecodeGenerator gen(FrameRate(FrameRate::FPS_119_88), true);
        REQUIRE(gen.dropFrame());

        gen.setTimecode(Timecode(Timecode::DF120, 0, 0, 59, 119));
        gen.advance();
        Timecode after = gen.advance();
        CHECK(after.min() == 1);
        CHECK(after.sec() == 0);
        CHECK(after.frame() == 8);
}

TEST_CASE("TimecodeGenerator_5994DF_keeps_frame_0_at_minute_10") {
        // Drop-frame rule excludes minutes divisible by 10 — frames 0..3 are
        // not dropped at 00:10:00:00.  Walk the full minute boundary to
        // confirm.
        TimecodeGenerator gen(FrameRate(FrameRate::FPS_59_94), true);
        gen.setTimecode(Timecode(Timecode::DF60, 0, 9, 59, 59));
        gen.advance();
        Timecode after = gen.advance();
        CHECK(after.min() == 10);
        CHECK(after.sec() == 0);
        CHECK(after.frame() == 0);
}

TEST_CASE("TimecodeGenerator_DFNoSkipAt10Min") {
        TimecodeGenerator gen(FrameRate(FrameRate::FPS_29_97), true);

        // Set TC just before a 10-minute boundary — NO skip
        gen.setTimecode(Timecode(Timecode::DF30, 0, 9, 59, 28));

        gen.advance(); // 00:09:59:28
        gen.advance(); // 00:09:59:29

        Timecode tc = gen.advance(); // Should be 00:10:00:00 (no skip at 10-min mark)
        CHECK(tc.min() == 10);
        CHECK(tc.sec() == 0);
        CHECK(tc.frame() == 0);
}

// ============================================================================
// Default at 29.97 without setDropFrame
// ============================================================================

TEST_CASE("TimecodeGenerator_Default2997IsNDF") {
        TimecodeGenerator gen(FrameRate(FrameRate::FPS_29_97));
        CHECK(!gen.dropFrame());
        CHECK(gen.timecodeMode() == Timecode::Mode(Timecode::NDF30));
}

// ============================================================================
// Non-standard frame rate (custom mode via libvtc)
// ============================================================================

TEST_CASE("TimecodeGenerator_NonStandardRate") {
        TimecodeGenerator gen;
        gen.setFrameRate(FrameRate(FrameRate::FPS_60));
        CHECK(gen.timecodeMode().isValid());
        CHECK(gen.timecodeMode().fps() == 60);
        CHECK(!gen.dropFrame());
}

// ============================================================================
// Forward across second boundary
// ============================================================================

TEST_CASE("TimecodeGenerator_ForwardAcrossSecond") {
        TimecodeGenerator gen(FrameRate(FrameRate::FPS_24));
        gen.setTimecode(Timecode(Timecode::NDF24, 0, 0, 0, 22));

        for (int i = 0; i < 3; i++) gen.advance(); // frames 22, 23, 0 (next sec)

        // After 3 advances, current TC should be at 00:00:01:01
        Timecode tc = gen.timecode();
        CHECK(tc.sec() == 1);
        CHECK(tc.frame() == 1);
}
