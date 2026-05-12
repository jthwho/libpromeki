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

TEST_CASE("Cea608Encoder: non-PopOn mode -> setSubtitles fails Error::NotImplemented") {
        Cea608Encoder::Config cfg;
        cfg.frameRate = FrameRate(FrameRate::FPS_30);
        cfg.mode = Cea608Encoder::Mode::PaintOn;
        Cea608Encoder enc(cfg);
        SubtitleList  subs;
        subs.append(Subtitle(tsFromMs(1000), tsFromMs(2000), "X"));
        CHECK(enc.setSubtitles(subs).code() == Error::NotImplemented);
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
