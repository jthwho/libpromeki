/**
 * @file      cea608encoder.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <chrono>
#include <doctest/doctest.h>
#include <promeki/cea608.h>
#include <promeki/cea608encoder.h>
#include <promeki/cea708cdp.h>
#include <promeki/framenumber.h>
#include <promeki/framerate.h>
#include <promeki/subtitle.h>
#include <promeki/timestamp.h>

using namespace promeki;

namespace {

        /// @brief Constructs a media-relative TimeStamp from a
        ///        millisecond offset (epoch = media t=0).
        TimeStamp tsFromMs(int64_t ms) {
                using ClockDur = TimeStamp::Value::duration;
                return TimeStamp(TimeStamp::Value(std::chrono::duration_cast<ClockDur>(std::chrono::milliseconds(ms))));
        }

        /// @brief Pulls the single CcData triple from a one-frame
        ///        @ref nextFrame call.  All tests below feed exactly
        ///        one frame at a time.
        Cea708Cdp::CcData oneTriple(const Cea608Encoder &enc, int64_t frame) {
                Cea708Cdp::CcDataList list = enc.nextFrame(FrameNumber(frame));
                REQUIRE(list.size() == 1);
                return list[0];
        }

        /// @brief Convenience: returns @c true if the triple's bytes
        ///        equal the expected @c (b1, b2) pair after parity
        ///        stripping.  Avoids hand-stamping parity in every
        ///        assertion.
        bool tripleHasBytes(const Cea708Cdp::CcData &t, uint8_t expB1, uint8_t expB2) {
                return Cea608::stripParity(t.b1) == expB1 && Cea608::stripParity(t.b2) == expB2;
        }

} // namespace

// ============================================================================
// Odd parity helpers
// ============================================================================

TEST_CASE("Cea608::withOddParity stamps bit 7 to make the byte odd-parity") {
        // 'A' = 0x41 = 0100_0001 — 2 ones in low 7 bits (even) → parity-set.
        CHECK(Cea608::withOddParity(0x41) == 0xC1);
        // 'B' = 0x42 = 0100_0010 — 2 ones (even) → parity-set.
        CHECK(Cea608::withOddParity(0x42) == 0xC2);
        // 0x14 (RCL b1) = 0001_0100 — 2 ones (even) → parity-set → 0x94.
        CHECK(Cea608::withOddParity(0x14) == 0x94);
        // 0x20 (RCL b2 / space) = 0010_0000 — 1 one (odd) → no change → 0x20.
        CHECK(Cea608::withOddParity(0x20) == 0x20);
        // 0x00 (null) = 0 ones (even) → parity-set → 0x80.
        CHECK(Cea608::withOddParity(0x00) == 0x80);
}

TEST_CASE("Cea608::checkOddParity / stripParity round-trip the parity bit") {
        for (uint8_t v : {uint8_t(0x00), uint8_t(0x14), uint8_t(0x20), uint8_t(0x41), uint8_t(0x7F)}) {
                uint8_t stamped = Cea608::withOddParity(v);
                CHECK(Cea608::checkOddParity(stamped));
                CHECK(Cea608::stripParity(stamped) == v);
        }
}

// ============================================================================
// Construction / config
// ============================================================================

TEST_CASE("Cea608Encoder: empty subtitle list emits null pairs on every frame") {
        Cea608Encoder::Config cfg;
        cfg.frameRate = FrameRate(FrameRate::FPS_30);
        Cea608Encoder enc(cfg);
        SubtitleList  subs;
        REQUIRE(enc.setSubtitles(subs).isOk());

        for (int64_t f = 0; f < 100; ++f) {
                Cea708Cdp::CcData t = oneTriple(enc, f);
                CHECK(t.valid);
                CHECK(t.type == 0); // CC1 → field 1.
                CHECK(tripleHasBytes(t, Cea608::NullB1, Cea608::NullB2));
                // Wire bytes must carry odd parity even on null pairs.
                CHECK(Cea608::checkOddParity(t.b1));
                CHECK(Cea608::checkOddParity(t.b2));
        }
}

TEST_CASE("Cea608Encoder: invalid frame rate -> setSubtitles fails Error::Invalid") {
        Cea608Encoder::Config cfg; // Default-constructed FrameRate is invalid.
        Cea608Encoder         enc(cfg);
        SubtitleList          subs;
        subs.append(Subtitle(tsFromMs(1000), tsFromMs(2000), "X"));
        CHECK(enc.setSubtitles(subs).code() == Error::Invalid);
}

TEST_CASE("Cea608Encoder: paint-on + roll-up modes are accepted") {
        // Both modes were added in Phase 3.5d.  Mode-specific byte
        // streams are exercised in dedicated test cases below.
        for (auto m : {Cea608Encoder::Mode::PaintOn, Cea608Encoder::Mode::RollUp}) {
                Cea608Encoder::Config cfg;
                cfg.frameRate = FrameRate(FrameRate::FPS_30);
                cfg.mode = m;
                Cea608Encoder enc(cfg);
                SubtitleList  subs;
                subs.append(Subtitle(tsFromMs(1000), tsFromMs(2000), "X"));
                CHECK(enc.setSubtitles(subs).isOk());
        }
}

TEST_CASE("Cea608Encoder: non-CC1 channel -> setSubtitles fails Error::NotImplemented") {
        Cea608Encoder::Config cfg;
        cfg.frameRate = FrameRate(FrameRate::FPS_30);
        cfg.channel = Cea608Encoder::Channel::CC3;
        Cea608Encoder enc(cfg);
        SubtitleList  subs;
        subs.append(Subtitle(tsFromMs(1000), tsFromMs(2000), "X"));
        CHECK(enc.setSubtitles(subs).code() == Error::NotImplemented);
}

// ============================================================================
// Single-cue scheduling: RCL → PAC → chars → EOC then EDM at end
// ============================================================================

TEST_CASE("Cea608Encoder: single 2-char cue lays out the pop-on byte stream") {
        // At 30fps, cue starts at frame 30 (= 1000 ms).
        //   "AB" → 1 char pair.
        //   firstFrame = startFrame - (2 RCL + 2 PAC + N chars) = 30 - 5 = 25.
        //   First EOC lands at frame 30 (== startFrame) so the receiver
        //   swaps memory exactly at the cue's logical start.
        // Layout:
        //   frame 25, 26: RCL doubled
        //   frame 27, 28: PAC doubled
        //   frame 29:     "AB"
        //   frame 30, 31: EOC doubled (swap at 30; duplicate at 31)
        //   frame 32..endFrame-1: null (cue is on screen)
        //   frame 60, 61: EDM doubled  (endFrame = 60 for end=2000ms)
        Cea608Encoder::Config cfg;
        cfg.frameRate = FrameRate(FrameRate::FPS_30);
        Cea608Encoder enc(cfg);
        SubtitleList  subs;
        subs.append(Subtitle(tsFromMs(1000), tsFromMs(2000), "AB"));
        REQUIRE(enc.setSubtitles(subs).isOk());

        // Frame 24: still empty (pre-RCL).
        CHECK(tripleHasBytes(oneTriple(enc, 24), Cea608::NullB1, Cea608::NullB2));

        // Frames 25, 26: RCL doubled.
        CHECK(tripleHasBytes(oneTriple(enc, 25), Cea608::RclB1, Cea608::RclB2));
        CHECK(tripleHasBytes(oneTriple(enc, 26), Cea608::RclB1, Cea608::RclB2));

        // Frames 27, 28: PAC doubled.
        CHECK(tripleHasBytes(oneTriple(enc, 27), Cea608::PacRow15Col0WhiteB1, Cea608::PacRow15Col0WhiteB2));
        CHECK(tripleHasBytes(oneTriple(enc, 28), Cea608::PacRow15Col0WhiteB1, Cea608::PacRow15Col0WhiteB2));

        // Frame 29: "AB".
        CHECK(tripleHasBytes(oneTriple(enc, 29), 0x41, 0x42));

        // Frames 30, 31: EOC doubled (swaps memory at 30; duplicate at 31).
        CHECK(tripleHasBytes(oneTriple(enc, 30), Cea608::EocB1, Cea608::EocB2));
        CHECK(tripleHasBytes(oneTriple(enc, 31), Cea608::EocB1, Cea608::EocB2));

        // Frames 32..59: null (cue is on screen — no further bytes needed).
        for (int64_t f = 32; f < 60; ++f) {
                CHECK(tripleHasBytes(oneTriple(enc, f), Cea608::NullB1, Cea608::NullB2));
        }

        // Frames 60, 61: EDM doubled.
        CHECK(tripleHasBytes(oneTriple(enc, 60), Cea608::EdmB1, Cea608::EdmB2));
        CHECK(tripleHasBytes(oneTriple(enc, 61), Cea608::EdmB1, Cea608::EdmB2));

        // Frame 62+: null.
        CHECK(tripleHasBytes(oneTriple(enc, 62), Cea608::NullB1, Cea608::NullB2));
}

TEST_CASE("Cea608Encoder: emitted bytes carry odd parity") {
        Cea608Encoder::Config cfg;
        cfg.frameRate = FrameRate(FrameRate::FPS_30);
        Cea608Encoder enc(cfg);
        SubtitleList  subs;
        subs.append(Subtitle(tsFromMs(1000), tsFromMs(2000), "AB"));
        REQUIRE(enc.setSubtitles(subs).isOk());
        for (int64_t f = 25; f <= 62; ++f) {
                Cea708Cdp::CcData t = oneTriple(enc, f);
                CHECK(Cea608::checkOddParity(t.b1));
                CHECK(Cea608::checkOddParity(t.b2));
        }
}

TEST_CASE("Cea608Encoder: odd-length text pads the final char pair with space") {
        // "ABC" is 3 chars → 2 char pairs (AB then C+space).
        // Pre-roll: 2 (RCL) + 2 (PAC) + 2 (chars) = 6 frames.
        // firstFrame = 30 - 6 = 24.  First EOC at frame 30.
        Cea608Encoder::Config cfg;
        cfg.frameRate = FrameRate(FrameRate::FPS_30);
        Cea608Encoder enc(cfg);
        SubtitleList  subs;
        subs.append(Subtitle(tsFromMs(1000), tsFromMs(2000), "ABC"));
        REQUIRE(enc.setSubtitles(subs).isOk());

        // Frame 24..25: RCL doubled.
        CHECK(tripleHasBytes(oneTriple(enc, 24), Cea608::RclB1, Cea608::RclB2));
        CHECK(tripleHasBytes(oneTriple(enc, 25), Cea608::RclB1, Cea608::RclB2));
        // Frame 26..27: PAC doubled.
        CHECK(tripleHasBytes(oneTriple(enc, 26), Cea608::PacRow15Col0WhiteB1, Cea608::PacRow15Col0WhiteB2));
        CHECK(tripleHasBytes(oneTriple(enc, 27), Cea608::PacRow15Col0WhiteB1, Cea608::PacRow15Col0WhiteB2));
        // Frame 28: "AB".
        CHECK(tripleHasBytes(oneTriple(enc, 28), 0x41, 0x42));
        // Frame 29: "C" + space.
        CHECK(tripleHasBytes(oneTriple(enc, 29), 0x43, 0x20));
        // Frame 30..31: EOC doubled (first at startFrame).
        CHECK(tripleHasBytes(oneTriple(enc, 30), Cea608::EocB1, Cea608::EocB2));
        CHECK(tripleHasBytes(oneTriple(enc, 31), Cea608::EocB1, Cea608::EocB2));
}

// ============================================================================
// Pre-roll errors
// ============================================================================

TEST_CASE("Cea608Encoder: cue start too close to t=0 -> Error::OutOfRange") {
        // "AB" needs 5 frames of pre-roll.  A cue starting at frame
        // 3 fails: firstFrame would be -2.
        Cea608Encoder::Config cfg;
        cfg.frameRate = FrameRate(FrameRate::FPS_30);
        Cea608Encoder enc(cfg);
        SubtitleList  subs;
        subs.append(Subtitle(tsFromMs(100), tsFromMs(1000), "AB")); // ~frame 3 at 30fps
        CHECK(enc.setSubtitles(subs).code() == Error::OutOfRange);
}

TEST_CASE("Cea608Encoder: back-to-back cues colliding with prior EDM elide that EDM") {
        // Cue 1: ends at frame 60.  EDM would land at frames 60, 61.
        // Cue 2: starts at frame 65.  "CD" needs firstFrame = 65 - 5 = 60.
        // Pre-roll collides with cue 1's EDM at frames 60, 61 — the
        // encoder elides cue 1's EDM (cue 1 persists until cue 2's
        // EOC at frame 65) instead of dropping cue 2 entirely.
        Cea608Encoder::Config cfg;
        cfg.frameRate = FrameRate(FrameRate::FPS_30);
        Cea608Encoder enc(cfg);
        SubtitleList  subs;
        subs.append(Subtitle(tsFromMs(1000), tsFromMs(2000), "AB"));
        subs.append(Subtitle(tsFromMs(2167), tsFromMs(3000), "CD"));
        REQUIRE(enc.setSubtitles(subs).isOk());

        // Frames 60, 61: cue 2's RCL doubled — *not* cue 1's EDM.
        CHECK(tripleHasBytes(oneTriple(enc, 60), Cea608::RclB1, Cea608::RclB2));
        CHECK(tripleHasBytes(oneTriple(enc, 61), Cea608::RclB1, Cea608::RclB2));
        // Frames 62, 63: cue 2's PAC doubled.
        CHECK(tripleHasBytes(oneTriple(enc, 62), Cea608::PacRow15Col0WhiteB1,
                             Cea608::PacRow15Col0WhiteB2));
        CHECK(tripleHasBytes(oneTriple(enc, 63), Cea608::PacRow15Col0WhiteB1,
                             Cea608::PacRow15Col0WhiteB2));
        // Frame 64: cue 2's "CD".
        CHECK(tripleHasBytes(oneTriple(enc, 64), 0x43, 0x44));
        // Frames 65, 66: cue 2's EOC doubled (swap at 65 — cue 1
        // visually replaced by cue 2 here, a 5-frame extension of
        // cue 1 past its authored end at frame 60).
        CHECK(tripleHasBytes(oneTriple(enc, 65), Cea608::EocB1, Cea608::EocB2));
        CHECK(tripleHasBytes(oneTriple(enc, 66), Cea608::EocB1, Cea608::EocB2));
        // Frames 90, 91: cue 2's EDM (cue 2 is the last cue — its EDM
        // is always committed since no later cue can collide with it).
        CHECK(tripleHasBytes(oneTriple(enc, 90), Cea608::EdmB1, Cea608::EdmB2));
        CHECK(tripleHasBytes(oneTriple(enc, 91), Cea608::EdmB1, Cea608::EdmB2));
}

TEST_CASE("Cea608Encoder: pre-roll overlapping prior cue's wire stream -> OutOfRange") {
        // Cue 1: 1000..2000ms = frames 30..60.  "AB" → wire stream ends
        // at frame 31 (second EOC).
        // Cue 2: 1100..3000ms = frame 33 start.  "CD" pre-roll = 5
        // frames → firstFrame = 28.  Frame 28 is inside cue 1's PAC
        // doubled at frames 27, 28 — no amount of EDM elision can
        // rescue this collision.
        Cea608Encoder::Config cfg;
        cfg.frameRate = FrameRate(FrameRate::FPS_30);
        Cea608Encoder enc(cfg);
        SubtitleList  subs;
        subs.append(Subtitle(tsFromMs(1000), tsFromMs(2000), "AB"));
        subs.append(Subtitle(tsFromMs(1100), tsFromMs(3000), "CD"));
        CHECK(enc.setSubtitles(subs).code() == Error::OutOfRange);
}

TEST_CASE("Cea608Encoder::encodableSubset drops cues that fail pre-roll") {
        Cea608Encoder::Config cfg;
        cfg.frameRate = FrameRate(FrameRate::FPS_30);
        Cea608Encoder enc(cfg);

        SubtitleList subs;
        // Cue 0: starts at frame ~3 — pre-roll for "AB" is 5 frames so
        // firstFrame would be -2.  Dropped (before t=0).
        subs.append(Subtitle(tsFromMs(100), tsFromMs(1000), "AB"));
        // Cue 1: starts at frame 30 — pre-roll 5 → firstFrame 25.
        // Comfortable; kept.  lastEocFrame after kept = 31.
        subs.append(Subtitle(tsFromMs(1000), tsFromMs(2000), "CD"));
        // Cue 2: starts at frame 33 — firstFrame 28.  Overlaps cue 1's
        // PAC at frame 27/28 (lastEocFrame=31).  Dropped (wire-stream
        // collision — EDM elision cannot rescue).
        subs.append(Subtitle(tsFromMs(1100), tsFromMs(3000), "EF"));
        // Cue 3: starts at frame 65 — firstFrame 60.  Overlaps cue 1's
        // EDM at frame 60/61, but EDM elision rescues it.  Kept.
        subs.append(Subtitle(tsFromMs(2167), tsFromMs(3000), "GH"));
        // Cue 4: starts at frame 120 — firstFrame 115.  Kept.
        subs.append(Subtitle(tsFromMs(4000), tsFromMs(5000), "IJ"));

        SubtitleList dropped;
        SubtitleList kept = enc.encodableSubset(subs, &dropped);
        REQUIRE(kept.size() == 3);
        CHECK(kept[0].text() == "CD");
        CHECK(kept[1].text() == "GH");
        CHECK(kept[2].text() == "IJ");
        REQUIRE(dropped.size() == 2);
        CHECK(dropped[0].text() == "AB");
        CHECK(dropped[1].text() == "EF");

        // Encoder accepts the filtered list without error.
        CHECK(enc.setSubtitles(kept).isOk());
}

TEST_CASE("Cea608Encoder: comfortably spaced cues schedule successfully") {
        // Cue 1 ends at frame 60.  Cue 2 starts at frame 120.  Cue 2
        // firstFrame = 120 - 5 = 115, well after cue 1's EDM at 61.
        Cea608Encoder::Config cfg;
        cfg.frameRate = FrameRate(FrameRate::FPS_30);
        Cea608Encoder enc(cfg);
        SubtitleList  subs;
        subs.append(Subtitle(tsFromMs(1000), tsFromMs(2000), "AB"));
        subs.append(Subtitle(tsFromMs(4000), tsFromMs(5000), "CD"));
        REQUIRE(enc.setSubtitles(subs).isOk());
        // Cue 2: RCL at frame 115.
        CHECK(tripleHasBytes(oneTriple(enc, 115), Cea608::RclB1, Cea608::RclB2));
}

// ============================================================================
// earliestStartFor diagnostic
// ============================================================================

TEST_CASE("Cea608Encoder::earliestStartFor returns the RCL frame for a cue") {
        Cea608Encoder::Config cfg;
        cfg.frameRate = FrameRate(FrameRate::FPS_30);
        Cea608Encoder enc(cfg);
        // 2 chars at 30fps → 5 frames of pre-roll → first RCL at frame 25.
        Subtitle    cue(tsFromMs(1000), tsFromMs(2000), "AB");
        FrameNumber first = enc.earliestStartFor(cue);
        REQUIRE(first.isValid());
        CHECK(first.value() == 25);
}

TEST_CASE("Cea608Encoder::earliestStartFor: pre-roll < 0 returns Unknown") {
        Cea608Encoder::Config cfg;
        cfg.frameRate = FrameRate(FrameRate::FPS_30);
        Cea608Encoder enc(cfg);
        Subtitle      cue(tsFromMs(50), tsFromMs(1000), "ABCDEF");
        FrameNumber   first = enc.earliestStartFor(cue);
        CHECK(first.isUnknown());
}

// ============================================================================
// Deterministic output (same input -> same byte stream)
// ============================================================================

TEST_CASE("Cea608Encoder: deterministic output for identical input") {
        Cea608Encoder::Config cfg;
        cfg.frameRate = FrameRate(FrameRate::FPS_30);
        Cea608Encoder enc(cfg);
        SubtitleList  subs;
        subs.append(Subtitle(tsFromMs(1000), tsFromMs(2000), "Hello, world."));
        REQUIRE(enc.setSubtitles(subs).isOk());

        std::vector<Cea708Cdp::CcData> first;
        for (int64_t f = 0; f < 100; ++f) first.push_back(oneTriple(enc, f));

        // Re-load same input and compare.
        REQUIRE(enc.setSubtitles(subs).isOk());
        for (int64_t f = 0; f < 100; ++f) {
                Cea708Cdp::CcData t = oneTriple(enc, f);
                CHECK(t.valid == first[f].valid);
                CHECK(t.type == first[f].type);
                CHECK(t.b1 == first[f].b1);
                CHECK(t.b2 == first[f].b2);
        }
}

// ============================================================================
// Paint-on mode (Phase 3.5d)
// ============================================================================

TEST_CASE("Cea608Encoder[paint-on]: single 2-char cue lays out the byte stream") {
        // At 30fps, cue starts at frame 30 (= 1000 ms), ends at frame 60.
        //   "AB" → 1 char pair.
        //   Pre-roll = 4 frames (2 RDC + 2 PAC).
        //   firstFrame = startFrame - 4 = 26.
        // Layout:
        //   26..27: RDC doubled
        //   28..29: PAC doubled
        //   30:     "AB" (chars stream live at startFrame onwards)
        //   31..59: null (cue is on-screen, painted)
        //   60..61: EDM doubled (cue end at frame 60)
        Cea608Encoder::Config cfg;
        cfg.frameRate = FrameRate(FrameRate::FPS_30);
        cfg.mode = Cea608Encoder::Mode::PaintOn;
        Cea608Encoder enc(cfg);
        SubtitleList  subs;
        subs.append(Subtitle(tsFromMs(1000), tsFromMs(2000), "AB"));
        REQUIRE(enc.setSubtitles(subs).isOk());

        // Frame 25: still null.
        CHECK(tripleHasBytes(oneTriple(enc, 25), Cea608::NullB1, Cea608::NullB2));
        // Frames 26, 27: RDC doubled.
        CHECK(tripleHasBytes(oneTriple(enc, 26), Cea608::Cc1MiscFirstByte, Cea608::MiscRDC));
        CHECK(tripleHasBytes(oneTriple(enc, 27), Cea608::Cc1MiscFirstByte, Cea608::MiscRDC));
        // Frames 28, 29: PAC doubled.
        CHECK(tripleHasBytes(oneTriple(enc, 28), Cea608::PacRow15Col0WhiteB1, Cea608::PacRow15Col0WhiteB2));
        CHECK(tripleHasBytes(oneTriple(enc, 29), Cea608::PacRow15Col0WhiteB1, Cea608::PacRow15Col0WhiteB2));
        // Frame 30: live "AB".
        CHECK(tripleHasBytes(oneTriple(enc, 30), 0x41, 0x42));
        // Frame 31..59: null (cue visible).
        for (int64_t f = 31; f < 60; ++f) {
                CHECK(tripleHasBytes(oneTriple(enc, f), Cea608::NullB1, Cea608::NullB2));
        }
        // Frames 60, 61: EDM doubled.
        CHECK(tripleHasBytes(oneTriple(enc, 60), Cea608::EdmB1, Cea608::EdmB2));
        CHECK(tripleHasBytes(oneTriple(enc, 61), Cea608::EdmB1, Cea608::EdmB2));
}

TEST_CASE("Cea608Encoder[paint-on]: chars overrunning cue end -> OutOfRange") {
        // Cue 1000..1100ms = frame 30..33 (3 frame display window).
        // "ABCDEFGH" → 4 char pairs.  lastCharFrame = 30 + 4 - 1 = 33,
        // which is == endFrame.  The overrun check fires (lastCharFrame
        // must be < endFrame).
        Cea608Encoder::Config cfg;
        cfg.frameRate = FrameRate(FrameRate::FPS_30);
        cfg.mode = Cea608Encoder::Mode::PaintOn;
        Cea608Encoder enc(cfg);
        SubtitleList  subs;
        subs.append(Subtitle(tsFromMs(1000), tsFromMs(1100), "ABCDEFGH"));
        CHECK(enc.setSubtitles(subs).code() == Error::OutOfRange);
}

TEST_CASE("Cea608Encoder[paint-on]: cue start too close to t=0 -> OutOfRange") {
        // Pre-roll = 4 frames.  Cue at frame 3 needs firstFrame = -1.
        Cea608Encoder::Config cfg;
        cfg.frameRate = FrameRate(FrameRate::FPS_30);
        cfg.mode = Cea608Encoder::Mode::PaintOn;
        Cea608Encoder enc(cfg);
        SubtitleList  subs;
        subs.append(Subtitle(tsFromMs(100), tsFromMs(1000), "AB"));
        CHECK(enc.setSubtitles(subs).code() == Error::OutOfRange);
}

// ============================================================================
// Roll-up mode (Phase 3.5d)
// ============================================================================

TEST_CASE("Cea608Encoder[roll-up]: first cue lays out RUx + CR + PAC + chars") {
        // At 30fps, cue starts at frame 30.
        //   Pre-roll = 6 frames (2 RUx + 2 CR + 2 PAC).
        //   firstFrame = 30 - 6 = 24.
        // Layout:
        //   24, 25: RU2 doubled
        //   26, 27: CR doubled
        //   28, 29: PAC doubled (row 15)
        //   30:     "AB" (live)
        //   31+ :   null (cue painted; no EDM in roll-up)
        Cea608Encoder::Config cfg;
        cfg.frameRate = FrameRate(FrameRate::FPS_30);
        cfg.mode = Cea608Encoder::Mode::RollUp;
        cfg.rollUpRows = 2;
        Cea608Encoder enc(cfg);
        SubtitleList  subs;
        subs.append(Subtitle(tsFromMs(1000), tsFromMs(2000), "AB"));
        REQUIRE(enc.setSubtitles(subs).isOk());

        // 24, 25: RU2 doubled.
        CHECK(tripleHasBytes(oneTriple(enc, 24), Cea608::Cc1MiscFirstByte, Cea608::MiscRU2));
        CHECK(tripleHasBytes(oneTriple(enc, 25), Cea608::Cc1MiscFirstByte, Cea608::MiscRU2));
        // 26, 27: CR doubled.
        CHECK(tripleHasBytes(oneTriple(enc, 26), Cea608::Cc1MiscFirstByte, Cea608::MiscCR));
        CHECK(tripleHasBytes(oneTriple(enc, 27), Cea608::Cc1MiscFirstByte, Cea608::MiscCR));
        // 28, 29: PAC doubled (row 15, white).
        CHECK(tripleHasBytes(oneTriple(enc, 28), Cea608::PacRow15Col0WhiteB1, Cea608::PacRow15Col0WhiteB2));
        CHECK(tripleHasBytes(oneTriple(enc, 29), Cea608::PacRow15Col0WhiteB1, Cea608::PacRow15Col0WhiteB2));
        // 30: "AB" live.
        CHECK(tripleHasBytes(oneTriple(enc, 30), 0x41, 0x42));
        // 31+: null (no EDM).
        for (int64_t f = 31; f < 90; ++f) {
                CHECK(tripleHasBytes(oneTriple(enc, f), Cea608::NullB1, Cea608::NullB2));
        }
}

TEST_CASE("Cea608Encoder[roll-up]: subsequent cue skips RUx (just CR + PAC + chars)") {
        // Cue 1: frame 30, "AB".  firstFrame = 24.
        // Cue 2: frame 120, "CD".  Pre-roll = 4 frames (no RUx
        // re-emission).  firstFrame = 116.
        Cea608Encoder::Config cfg;
        cfg.frameRate = FrameRate(FrameRate::FPS_30);
        cfg.mode = Cea608Encoder::Mode::RollUp;
        cfg.rollUpRows = 3;
        Cea608Encoder enc(cfg);
        SubtitleList  subs;
        subs.append(Subtitle(tsFromMs(1000), tsFromMs(2000), "AB"));
        subs.append(Subtitle(tsFromMs(4000), tsFromMs(5000), "CD"));
        REQUIRE(enc.setSubtitles(subs).isOk());

        // 24, 25: RU3 doubled (set on first cue).
        CHECK(tripleHasBytes(oneTriple(enc, 24), Cea608::Cc1MiscFirstByte, Cea608::MiscRU3));
        CHECK(tripleHasBytes(oneTriple(enc, 25), Cea608::Cc1MiscFirstByte, Cea608::MiscRU3));

        // 116, 117: CR doubled (cue 2 starts here; no RUx).
        CHECK(tripleHasBytes(oneTriple(enc, 116), Cea608::Cc1MiscFirstByte, Cea608::MiscCR));
        CHECK(tripleHasBytes(oneTriple(enc, 117), Cea608::Cc1MiscFirstByte, Cea608::MiscCR));
        // 118, 119: PAC doubled.
        CHECK(tripleHasBytes(oneTriple(enc, 118), Cea608::PacRow15Col0WhiteB1, Cea608::PacRow15Col0WhiteB2));
        CHECK(tripleHasBytes(oneTriple(enc, 119), Cea608::PacRow15Col0WhiteB1, Cea608::PacRow15Col0WhiteB2));
        // 120: "CD" live.
        CHECK(tripleHasBytes(oneTriple(enc, 120), 0x43, 0x44));
}

TEST_CASE("Cea608Encoder[roll-up]: chars overrunning cue end -> OutOfRange") {
        // Cue 1000..1100ms = 30..33 (3 frame display).
        // "ABCDEFGH" → 4 char pairs.  lastCharFrame = 33 == endFrame
        // → overrun.
        Cea608Encoder::Config cfg;
        cfg.frameRate = FrameRate(FrameRate::FPS_30);
        cfg.mode = Cea608Encoder::Mode::RollUp;
        Cea608Encoder enc(cfg);
        SubtitleList  subs;
        subs.append(Subtitle(tsFromMs(1000), tsFromMs(1100), "ABCDEFGH"));
        CHECK(enc.setSubtitles(subs).code() == Error::OutOfRange);
}

// ============================================================================
// Multi-row word-wrap (smart layout)
// ============================================================================

namespace {

        /// @brief Helper: pulls the row index out of an arbitrary
        ///        PAC byte pair.  Falls back to -1 when the bytes
        ///        do not decode as a PAC.
        int rowFromPac(uint8_t b1, uint8_t b2) {
                Cea608::PacAttr pac;
                if (!Cea608::decodePac(b1, b2, pac)) return -1;
                return pac.row;
        }

        /// @brief Helper: counts how many *distinct* PAC frames the
        ///        encoder schedules between @p lo and @p hi
        ///        (inclusive).  Doubled PACs at consecutive frames
        ///        register as one logical "row PAC".
        size_t countDistinctPacs(const Cea608Encoder &enc, int64_t lo, int64_t hi) {
                size_t  count = 0;
                int     prevRow = -2; // not -1 since rowFromPac returns -1 on miss
                int64_t prevFrame = -2;
                for (int64_t f = lo; f <= hi; ++f) {
                        auto    list = enc.nextFrame(FrameNumber(f));
                        if (list.size() != 1) continue;
                        uint8_t b1 = Cea608::stripParity(list[0].b1);
                        uint8_t b2 = Cea608::stripParity(list[0].b2);
                        int     r = rowFromPac(b1, b2);
                        if (r >= 1) {
                                // Distinct row PAC fires once per (row, isolated)
                                // — collapse the doubled emission on the next
                                // consecutive frame.
                                if (!(r == prevRow && f == prevFrame + 1)) {
                                        ++count;
                                }
                                prevRow = r;
                                prevFrame = f;
                        } else {
                                prevRow = -2;
                        }
                }
                return count;
        }

} // namespace

TEST_CASE("Cea608Encoder: multi-row layout (\\n in SRT honoured when both rows fit)") {
        // SubRip-style cue with an explicit line break and both rows
        // fitting 32 cols.  Expect TWO doubled-PAC blocks (rows 14
        // and 15) in the schedule.
        Cea608Encoder::Config cfg;
        cfg.frameRate = FrameRate(FrameRate::FPS_30);
        Cea608Encoder enc(cfg);
        SubtitleList  subs;
        // Long display window so the multi-row pre-roll fits.
        subs.append(Subtitle(tsFromMs(3000), tsFromMs(6000), "First line\nSecond line"));
        REQUIRE(enc.setSubtitles(subs).isOk());

        // At 30 fps the cue starts at frame 90 (= 3000 ms).
        // Pre-roll = 2 (RCL) + 2 (PAC row 14) + N0 chars + 2 (PAC row 15) + N1 chars.
        // "First line" → 5 char pairs; "Second line" → 6 char pairs.
        // body bytes = 4 (PACs) + 5 + 6 = 15 → pre-roll = 17 frames.
        // firstFrame = 90 - 17 = 73.

        // The first row PAC should target row 14, the second row 15.
        // PAC bytes start with 0x10..0x17 (b1 high nibble 0x1).
        size_t pacCount = countDistinctPacs(enc, 73, 89);
        CHECK(pacCount == 2);

        // Verify the two PAC rows are 14 then 15 by sampling the
        // first byte of each PAC block.
        // First row PAC is at frame 75, 76.
        int row1 = rowFromPac(Cea608::stripParity(oneTriple(enc, 75).b1),
                              Cea608::stripParity(oneTriple(enc, 75).b2));
        CHECK(row1 == 14);
        // Second row PAC sits between the chars of row 1 and the chars
        // of row 2.  Locate it by scanning the body span.
        bool foundRow15Pac = false;
        for (int64_t f = 77; f < 90; ++f) {
                int r = rowFromPac(Cea608::stripParity(oneTriple(enc, f).b1),
                                   Cea608::stripParity(oneTriple(enc, f).b2));
                if (r == 15) {
                        foundRow15Pac = true;
                        break;
                }
        }
        CHECK(foundRow15Pac);
}

TEST_CASE("Cea608Encoder: long single-line cue word-wraps with balanced rows") {
        // Cue text exceeds maxCols=32 → re-flow with balanced wrap.
        // "Welcome to the libpromeki SubRip test file" — 42 chars,
        // 7 words ([7,2,3,10,6,4,5]).  Balanced minimax at maxCols=32
        // picks 2 rows: ["Welcome to the libpromeki" (25) +
        //                "SubRip test file" (16)] — both within cap.
        Cea608Encoder::Config cfg;
        cfg.frameRate = FrameRate(FrameRate::FPS_30);
        Cea608Encoder enc(cfg);
        SubtitleList  subs;
        subs.append(Subtitle(tsFromMs(3000), tsFromMs(6000),
                             "Welcome to the libpromeki SubRip test file"));
        REQUIRE(enc.setSubtitles(subs).isOk());

        // Exactly two distinct PACs (one per row) in the pre-roll window.
        size_t pacCount = countDistinctPacs(enc, 60, 89);
        CHECK(pacCount == 2);
}

TEST_CASE("Cea608Encoder: \\n is ignored when an explicit row overflows maxCols") {
        // Author put a hard break, but the second line overflows
        // maxCols=32 → the encoder ignores the '\n' entirely and
        // re-flows the whole cue.  Expectation: every emitted PAC
        // row's chars (concatenated through the next style boundary)
        // fits within maxCols.
        Cea608Encoder::Config cfg;
        cfg.frameRate = FrameRate(FrameRate::FPS_30);
        Cea608Encoder enc(cfg);
        SubtitleList  subs;
        subs.append(Subtitle(tsFromMs(3000), tsFromMs(7000),
                             "Short line\n"
                             "This second line is far too long for the cap of thirty two cols"));
        REQUIRE(enc.setSubtitles(subs).isOk());

        // Joined text: "Short line This second line is far too long for the cap of thirty two cols"
        // ≈ 75 chars.  At maxCols=32 the balanced wrap will produce 3
        // rows (would never produce 2 rows since the joined text > 64
        // chars).  3 rows → 3 distinct PACs.  The pre-roll spans more
        // than 30 frames so scan the entire span before cue start.
        size_t pacCount = countDistinctPacs(enc, 0, 119);
        CHECK(pacCount == 3);
}

TEST_CASE("Cea608Encoder: cue exceeding maxRows auto-splits into time-displaced sub-cues") {
        // Build a deliberately-long cue that needs > 3 rows at the
        // 32-col cap.  Auto-split should produce ≥ 2 sub-cues.
        // Detection: count the EOC pairs in the schedule — each
        // sub-cue contributes one EOC pair.
        Cea608Encoder::Config cfg;
        cfg.frameRate = FrameRate(FrameRate::FPS_30);
        Cea608Encoder enc(cfg);
        SubtitleList  subs;
        // ~150-char cue → 5 rows at minimax over maxCols=32 → 2 sub-cues.
        subs.append(Subtitle(tsFromMs(4000), tsFromMs(12000),
                             "alpha bravo charlie delta echo foxtrot golf hotel india juliet "
                             "kilo lima mike november oscar papa quebec romeo sierra tango"));
        REQUIRE(enc.setSubtitles(subs).isOk());

        // Walk the whole span counting EOC pairs (doubled at sub-cue
        // start).  At minimum we expect 2 EOC blocks for an auto-split
        // cue.
        size_t eocBlocks = 0;
        bool   prevEoc = false;
        for (int64_t f = 0; f < 380; ++f) {
                Cea708Cdp::CcData t = oneTriple(enc, f);
                bool isEoc = (Cea608::stripParity(t.b1) == Cea608::EocB1
                              && Cea608::stripParity(t.b2) == Cea608::EocB2);
                if (isEoc && !prevEoc) ++eocBlocks;
                prevEoc = isEoc;
        }
        CHECK(eocBlocks >= 2);
}

TEST_CASE("Cea608Encoder: 2-row layout placed at correct anchor rows") {
        // Top-anchored cue → rows 1, 2.
        // Middle-anchored cue → rows 7, 8.
        // Bottom-anchored cue → rows 14, 15.
        struct Case {
                        SubtitleAnchor anchor;
                        int            expectedTop;
                        int            expectedBottom;
        };
        Case cases[] = {
                {SubtitleAnchor::TopCenter, 1, 2},
                {SubtitleAnchor::MiddleCenter, 7, 8},
                {SubtitleAnchor::BottomCenter, 14, 15},
        };
        for (const auto &c : cases) {
                Cea608Encoder::Config cfg;
                cfg.frameRate = FrameRate(FrameRate::FPS_30);
                Cea608Encoder enc(cfg);
                SubtitleList  subs;
                subs.append(Subtitle(tsFromMs(3000), tsFromMs(6000),
                                     "Line one\nLine two", c.anchor));
                REQUIRE(enc.setSubtitles(subs).isOk());

                // Sample the schedule: the first PAC in the pre-roll
                // window addresses @c expectedTop; somewhere before
                // startFrame the row-@c expectedBottom PAC fires.
                bool foundTop = false;
                bool foundBot = false;
                for (int64_t f = 0; f < 90; ++f) {
                        Cea708Cdp::CcData t = oneTriple(enc, f);
                        int r = rowFromPac(Cea608::stripParity(t.b1), Cea608::stripParity(t.b2));
                        if (r == c.expectedTop) foundTop = true;
                        if (r == c.expectedBottom) foundBot = true;
                }
                CHECK(foundTop);
                CHECK(foundBot);
        }
}

TEST_CASE("Cea608Encoder: maxRows = 2 caps wrap rows even when 3 would balance better") {
        // Same long-cue input as the "long single-line" test but with
        // maxRows lowered to 2.  Auto-split fires.
        Cea608Encoder::Config cfg;
        cfg.frameRate = FrameRate(FrameRate::FPS_30);
        cfg.maxRows = 2;
        Cea608Encoder enc(cfg);
        SubtitleList  subs;
        // 7-word, 42-char cue that minimaxes nicely into 2 rows at
        // maxCols=32; with maxRows=2 it stays one sub-cue.
        subs.append(Subtitle(tsFromMs(3000), tsFromMs(6000),
                             "Welcome to the libpromeki SubRip test file"));
        REQUIRE(enc.setSubtitles(subs).isOk());
        CHECK(countDistinctPacs(enc, 0, 89) == 2);
}

TEST_CASE("Cea608Encoder: single-row cue identical to legacy single-PAC byte stream") {
        // Regression: even with the multi-row code path in place, a
        // single-row cue must produce the exact byte sequence the
        // legacy encoder emitted — verified by the existing tests for
        // "AB" / "ABC" / etc.  This case adds an explicit guard
        // against pre-roll length regressions for a longer plain
        // single-row cue.
        Cea608Encoder::Config cfg;
        cfg.frameRate = FrameRate(FrameRate::FPS_30);
        Cea608Encoder enc(cfg);
        SubtitleList  subs;
        // 13-char cue, fits one row → pre-roll = 2 (RCL) + 2 (PAC) +
        // 7 char pairs = 11 frames.
        subs.append(Subtitle(tsFromMs(3000), tsFromMs(6000), "Hello, world!"));
        REQUIRE(enc.setSubtitles(subs).isOk());
        // Cue starts at frame 90; pre-roll begins at 90 - 11 = 79.
        CHECK(tripleHasBytes(oneTriple(enc, 79), Cea608::RclB1, Cea608::RclB2));
        CHECK(tripleHasBytes(oneTriple(enc, 80), Cea608::RclB1, Cea608::RclB2));
        // Exactly one distinct PAC row in the pre-roll window.
        CHECK(countDistinctPacs(enc, 79, 89) == 1);
}

TEST_CASE("Cea608Encoder[roll-up]: multi-row wrap falls back to single row with warning") {
        // Roll-up is single-row by spec; a cue that would wrap to >1
        // row must collapse to a single line at row 15.
        Cea608Encoder::Config cfg;
        cfg.frameRate = FrameRate(FrameRate::FPS_30);
        cfg.mode = Cea608Encoder::Mode::RollUp;
        Cea608Encoder enc(cfg);
        SubtitleList  subs;
        // 50-char cue that would normally wrap into 2 rows at maxCols 32.
        subs.append(Subtitle(tsFromMs(3000), tsFromMs(6000),
                             "Long roll up cue that exceeds the 32-col cap easily"));
        REQUIRE(enc.setSubtitles(subs).isOk());
        // Exactly one distinct PAC (forced row 15 by roll-up).
        CHECK(countDistinctPacs(enc, 0, 89) == 1);
}

TEST_CASE("Cea608Encoder[roll-up]: rollUpRows clamped to [2,4]") {
        // rollUpRows out of range should be clamped at scheduling time
        // (RU2 for <2, RU4 for >4) and still produce a valid schedule.
        Cea608Encoder::Config cfg;
        cfg.frameRate = FrameRate(FrameRate::FPS_30);
        cfg.mode = Cea608Encoder::Mode::RollUp;
        cfg.rollUpRows = 1; // out of range
        Cea608Encoder enc(cfg);
        SubtitleList  subs;
        subs.append(Subtitle(tsFromMs(1000), tsFromMs(2000), "AB"));
        REQUIRE(enc.setSubtitles(subs).isOk());
        // First bytes: RU2 (clamped from 1).
        CHECK(tripleHasBytes(oneTriple(enc, 24), Cea608::Cc1MiscFirstByte, Cea608::MiscRU2));

        Cea608Encoder::Config cfg2 = cfg;
        cfg2.rollUpRows = 10;
        Cea608Encoder enc2(cfg2);
        REQUIRE(enc2.setSubtitles(subs).isOk());
        // First bytes: RU4 (clamped from 10).
        CHECK(tripleHasBytes(oneTriple(enc2, 24), Cea608::Cc1MiscFirstByte, Cea608::MiscRU4));
}

// ============================================================================
// Per-cue CaptionMode override
// ============================================================================

TEST_CASE("Cea608Encoder: cue.mode=RollUp overrides Config.mode=PopOn") {
        // Cue stamps an explicit CaptionMode::RollUp; the encoder
        // ignores Config.mode (PopOn default) and emits the roll-up
        // initialiser (RU2) at the matching pre-roll frame.
        Cea608Encoder::Config cfg;
        cfg.frameRate = FrameRate(FrameRate::FPS_30);
        // Leave Config.mode at PopOn default.
        Cea608Encoder enc(cfg);
        SubtitleList  subs;
        Subtitle      cue(tsFromMs(1000), tsFromMs(2000), "X");
        cue.setMode(CaptionMode::RollUp);
        subs.append(cue);
        REQUIRE(enc.setSubtitles(subs).isOk());
        // RU2 (Cc1MiscFirstByte + MiscRU2) at the roll-up first-cue
        // pre-roll frame (startFrame=30, pre-roll=6 → frame 24).
        CHECK(tripleHasBytes(oneTriple(enc, 24), Cea608::Cc1MiscFirstByte, Cea608::MiscRU2));
}

TEST_CASE("Cea608Encoder: cue.mode=Default falls back to Config.mode") {
        // No explicit cue mode → encoder uses Config.mode (PaintOn here).
        Cea608Encoder::Config cfg;
        cfg.frameRate = FrameRate(FrameRate::FPS_30);
        cfg.mode = Cea608Encoder::Mode::PaintOn;
        Cea608Encoder enc(cfg);
        SubtitleList  subs;
        subs.append(Subtitle(tsFromMs(1000), tsFromMs(2000), "X"));
        REQUIRE(enc.setSubtitles(subs).isOk());
        // Paint-on uses RDC (Resume Direct Captioning, MiscRDC) as
        // pre-roll, landing 4 frames before the cue's start.
        CHECK(tripleHasBytes(oneTriple(enc, 26), Cea608::Cc1MiscFirstByte, Cea608::MiscRDC));
}

// ============================================================================
// Background-attribute wire codes (EIA-608-B §7.6)
// ============================================================================

TEST_CASE("Cea608::encodeBgAttribute / decodeBgAttribute round-trip") {
        for (int ci = 0; ci < static_cast<int>(Cea608::CaptionColorCount); ++ci) {
                for (bool semi : {false, true}) {
                        const auto c = static_cast<Cea608::CaptionColor>(ci);
                        uint8_t    b1 = 0, b2 = 0;
                        Cea608::encodeBgAttribute(c, semi, b1, b2);
                        CHECK(b1 == 0x10);
                        CHECK(Cea608::isBgAttribute(b1, b2));
                        Cea608::CaptionColor outColor;
                        bool                 outSemi = false;
                        REQUIRE(Cea608::decodeBgAttribute(b1, b2, outColor, outSemi));
                        CHECK(outColor == c);
                        CHECK(outSemi == semi);
                }
        }
}

TEST_CASE("Cea608::isBgAttribute rejects PAC and mid-row bytes") {
        // PAC for row 11 also uses b1=0x10, but b2 is in [0x40, 0x7F].
        // Make sure isBgAttribute doesn't fire on that.
        CHECK_FALSE(Cea608::isBgAttribute(0x10, 0x40));
        CHECK_FALSE(Cea608::isBgAttribute(0x10, 0x60));
        // Mid-row codes are b1=0x11 — different first byte entirely.
        CHECK_FALSE(Cea608::isBgAttribute(0x11, 0x20));
}

TEST_CASE("Cea608Encoder: mixed cue modes warn and fall back to Config.mode") {
        // Two cues with different explicit modes → encoder warns and
        // dispatches the whole batch under Config.mode (PopOn default).
        Cea608Encoder::Config cfg;
        cfg.frameRate = FrameRate(FrameRate::FPS_30);
        Cea608Encoder enc(cfg);
        SubtitleList  subs;
        Subtitle      cue1(tsFromMs(1000), tsFromMs(2000), "A");
        cue1.setMode(CaptionMode::RollUp);
        Subtitle cue2(tsFromMs(3000), tsFromMs(4000), "B");
        cue2.setMode(CaptionMode::PaintOn);
        subs.append(cue1);
        subs.append(cue2);
        REQUIRE(enc.setSubtitles(subs).isOk());
        // Fallback to Config.mode (PopOn) → some RCL (MiscRCL)
        // pre-roll appears in the [20..30) window before the first
        // cue's start.  Exact frame depends on layout; just confirm
        // RCL is present anywhere in the pre-roll range.
        bool sawRcl = false;
        for (int64_t f = 20; f < 30; ++f) {
                if (tripleHasBytes(oneTriple(enc, f), Cea608::Cc1MiscFirstByte, Cea608::MiscRCL)) {
                        sawRcl = true;
                        break;
                }
        }
        CHECK(sawRcl);
}
