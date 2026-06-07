/**
 * @file      cea608encoder.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <chrono>
#include <doctest/doctest.h>
#include <promeki/cea608.h>
#include <promeki/cea608decoder.h>
#include <promeki/cea608encoder.h>
#include <promeki/cea708cdp.h>
#include <promeki/framenumber.h>
#include <promeki/framerate.h>
#include <promeki/metadata.h>
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

        /// @brief Returns the TimeStamp at frame @p frame for 30 fps.
        ///        Uses the same rounded-ms mapping the encoder applies,
        ///        so a TS computed via @c tsAt30fps(N) round-trips to
        ///        frame @c N through @c timeStampToFrame.
        TimeStamp tsAt30fps(int64_t frame) {
                return tsFromMs((frame * 1000 + 15) / 30);
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

TEST_CASE("Cea608Encoder: every channel (CC1..CC4) accepts setSubtitles") {
        // CC1/CC2/CC3/CC4 all schedule successfully; per-channel
        // wire-byte verification lives in the dedicated CC2 round-
        // trip tests further down.
        for (Cea608Encoder::Channel ch : {Cea608Encoder::Channel::CC1,
                                          Cea608Encoder::Channel::CC2,
                                          Cea608Encoder::Channel::CC3,
                                          Cea608Encoder::Channel::CC4}) {
                Cea608Encoder::Config cfg;
                cfg.frameRate = FrameRate(FrameRate::FPS_30);
                cfg.channel = ch;
                Cea608Encoder enc(cfg);
                SubtitleList  subs;
                subs.append(Subtitle(tsFromMs(1000), tsFromMs(2000), "X"));
                CHECK(enc.setSubtitles(subs).isOk());
        }
}

// ============================================================================
// Single-cue scheduling: RCL → PAC → chars → EOC then EDM at end
// ============================================================================

TEST_CASE("Cea608Encoder: single 2-char cue lays out the pop-on byte stream") {
        // At 30fps, cue starts at frame 30 (= 1000 ms).
        //   "AB" → 1 char pair.
        //   firstFrame = startFrame - (2 RCL + 2 ENM + 2 PAC + N chars)
        //              = 30 - 7 = 23.  Per §B.8.3 ENM rides between RCL
        //              and PAC so the upcoming PAC + chars land on a
        //              clean non-displayed memory canvas (E1).
        //   First EOC lands at frame 30 (== startFrame) so the receiver
        //   swaps memory exactly at the cue's logical start.
        // Layout:
        //   frame 23, 24: RCL doubled
        //   frame 25, 26: ENM doubled (§B.8.3 — wipe loading memory)
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

        // Frame 22: still empty (pre-RCL).
        CHECK(tripleHasBytes(oneTriple(enc, 22), Cea608::NullB1, Cea608::NullB2));

        // Frames 23, 24: RCL doubled.
        CHECK(tripleHasBytes(oneTriple(enc, 23), Cea608::RclB1, Cea608::RclB2));
        CHECK(tripleHasBytes(oneTriple(enc, 24), Cea608::RclB1, Cea608::RclB2));

        // Frames 25, 26: ENM doubled (§B.8.3).
        CHECK(tripleHasBytes(oneTriple(enc, 25), Cea608::EnmB1, Cea608::EnmB2));
        CHECK(tripleHasBytes(oneTriple(enc, 26), Cea608::EnmB1, Cea608::EnmB2));

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
        // Pre-roll now begins at frame 23 (2 RCL + 2 ENM + 2 PAC + 1
        // char = 7 frames before startFrame=30; E1 added the ENM pair).
        for (int64_t f = 23; f <= 62; ++f) {
                Cea708Cdp::CcData t = oneTriple(enc, f);
                CHECK(Cea608::checkOddParity(t.b1));
                CHECK(Cea608::checkOddParity(t.b2));
        }
}

TEST_CASE("Cea608Encoder: odd-length text pads the final char pair with NUL") {
        // "ABC" is 3 chars → 2 char pairs (AB then C+NUL).
        // Pre-roll: 2 (RCL) + 2 (ENM) + 2 (PAC) + 2 (chars) = 8 frames.
        // firstFrame = 30 - 8 = 22.  First EOC at frame 30.
        //
        // The pad byte is NUL (0x00), not SPACE (0x20): the decoder's
        // char-pair handler treats b2 < 0x20 as filler and skips it,
        // so NUL keeps the wire's pair cadence without writing an
        // extra visible cell after the last text char.  Padding with
        // SPACE would land that space in the next cell and — at the
        // §C.13 col-32 boundary — overwrite the last text character
        // in place (cursor clamps at col 31, further chars overwrite
        // that cell).
        Cea608Encoder::Config cfg;
        cfg.frameRate = FrameRate(FrameRate::FPS_30);
        Cea608Encoder enc(cfg);
        SubtitleList  subs;
        subs.append(Subtitle(tsFromMs(1000), tsFromMs(2000), "ABC"));
        REQUIRE(enc.setSubtitles(subs).isOk());

        // Frame 22..23: RCL doubled.
        CHECK(tripleHasBytes(oneTriple(enc, 22), Cea608::RclB1, Cea608::RclB2));
        CHECK(tripleHasBytes(oneTriple(enc, 23), Cea608::RclB1, Cea608::RclB2));
        // Frame 24..25: ENM doubled.
        CHECK(tripleHasBytes(oneTriple(enc, 24), Cea608::EnmB1, Cea608::EnmB2));
        CHECK(tripleHasBytes(oneTriple(enc, 25), Cea608::EnmB1, Cea608::EnmB2));
        // Frame 26..27: PAC doubled.
        CHECK(tripleHasBytes(oneTriple(enc, 26), Cea608::PacRow15Col0WhiteB1, Cea608::PacRow15Col0WhiteB2));
        CHECK(tripleHasBytes(oneTriple(enc, 27), Cea608::PacRow15Col0WhiteB1, Cea608::PacRow15Col0WhiteB2));
        // Frame 28: "AB".
        CHECK(tripleHasBytes(oneTriple(enc, 28), 0x41, 0x42));
        // Frame 29: "C" + NUL.
        CHECK(tripleHasBytes(oneTriple(enc, 29), 0x43, Cea608::NullB2));
        // Frame 30..31: EOC doubled (first at startFrame).
        CHECK(tripleHasBytes(oneTriple(enc, 30), Cea608::EocB1, Cea608::EocB2));
        CHECK(tripleHasBytes(oneTriple(enc, 31), Cea608::EocB1, Cea608::EocB2));
}

// ============================================================================
// Pre-roll errors
// ============================================================================

TEST_CASE("Cea608Encoder: cue start too close to t=0 -> Error::OutOfRange") {
        // "AB" needs 7 frames of pre-roll (RCL + ENM + PAC + 1 char,
        // each control pair doubled — E1 added the ENM pair).  A cue
        // starting at frame 3 fails: firstFrame would be -4.
        Cea608Encoder::Config cfg;
        cfg.frameRate = FrameRate(FrameRate::FPS_30);
        Cea608Encoder enc(cfg);
        SubtitleList  subs;
        subs.append(Subtitle(tsFromMs(100), tsFromMs(1000), "AB")); // ~frame 3 at 30fps
        CHECK(enc.setSubtitles(subs).code() == Error::OutOfRange);
}

TEST_CASE("Cea608Encoder: centered cue too close to t=0 falls back to flush-left") {
        // A 28-char BottomCenter cue centers at col 2 (PAC indent 0 +
        // Tab Offset T2 doubled = +2 frames over the flush-left
        // shape).  Flush-left pre-roll = 2 RCL + 2 ENM + 2 PAC + 14
        // char pairs = 20 frames; centered pre-roll = 22 frames.  A
        // cue starting at frame 20 fits flush-left exactly (firstFrame
        // = 0) but the centered layout's firstFrame would land at -2.
        //
        // The encoder's flush-left fallback re-places the chunk at
        // col 0 so the Tab Offset pair drops off the body and the
        // cue survives.  We assert:
        //   - setSubtitles returns Ok (no OutOfRange drop).
        //   - encodableSubset keeps the cue (same fallback at the
        //     filter gate, so TpgMediaIO's drop-warn path stays
        //     quiet for centered cues that would otherwise be lost).
        //   - The decoder round-trips it as BottomLeft (flush-left
        //     col 0 + bottom row + 28-char width gives leftGap=0,
        //     rightGap=4, so the symmetric-gap rule resolves Left).
        Cea608Encoder::Config cfg;
        cfg.frameRate = FrameRate(FrameRate::FPS_30);
        Cea608Encoder enc(cfg);

        const String wide28 = "Centered text twenty-eight..";
        REQUIRE(wide28.length() == 28);
        SubtitleList subs;
        subs.append(Subtitle(tsAt30fps(20), tsAt30fps(80), wide28,
                             SubtitleAnchor::BottomCenter));

        // encodableSubset must NOT drop the cue — the filter mirrors
        // the in-encoder fallback so the cue survives the headroom
        // constraint via flush-left placement.
        SubtitleList dropped;
        SubtitleList kept = enc.encodableSubset(subs, &dropped);
        CHECK(kept.size() == 1);
        CHECK(dropped.size() == 0);

        // setSubtitles must also accept the cue without OutOfRange —
        // the same fallback runs inside encodePopOnCue.
        REQUIRE(enc.setSubtitles(kept).isOk());

        // Round-trip through the decoder; the recovered anchor is
        // BottomLeft because the fallback emitted the cue at col 0.
        Cea608Decoder dec;
        for (int64_t f = 0; f < 100; ++f) {
                dec.pushFrame(FrameNumber(f), tsAt30fps(f),
                              enc.nextFrame(FrameNumber(f)));
        }
        SubtitleList out = dec.finalize();
        REQUIRE(out.size() == 1);
        CHECK(out[0].anchor() == SubtitleAnchor::BottomLeft);
}

TEST_CASE("Cea608Encoder: back-to-back cues colliding with prior EDM elide that EDM") {
        // Cue 1: ends at frame 60.  EDM would land at frames 60, 61.
        // Cue 2: starts at frame 65.  "CD" needs firstFrame = 65 - 7 = 58
        // (E1 added the doubled ENM pair to the pop-on pre-roll).
        // Cue 2's pre-roll overlaps cue 1's EDM slot at frames 60, 61
        // (via the ENM pair) — the encoder elides cue 1's EDM (cue 1
        // persists until cue 2's EOC at frame 65) instead of dropping
        // cue 2 entirely.
        Cea608Encoder::Config cfg;
        cfg.frameRate = FrameRate(FrameRate::FPS_30);
        Cea608Encoder enc(cfg);
        SubtitleList  subs;
        subs.append(Subtitle(tsFromMs(1000), tsFromMs(2000), "AB"));
        subs.append(Subtitle(tsFromMs(2167), tsFromMs(3000), "CD"));
        REQUIRE(enc.setSubtitles(subs).isOk());

        // Frames 58, 59: cue 2's RCL doubled.
        CHECK(tripleHasBytes(oneTriple(enc, 58), Cea608::RclB1, Cea608::RclB2));
        CHECK(tripleHasBytes(oneTriple(enc, 59), Cea608::RclB1, Cea608::RclB2));
        // Frames 60, 61: cue 2's ENM doubled — *not* cue 1's EDM.
        CHECK(tripleHasBytes(oneTriple(enc, 60), Cea608::EnmB1, Cea608::EnmB2));
        CHECK(tripleHasBytes(oneTriple(enc, 61), Cea608::EnmB1, Cea608::EnmB2));
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
        // Cue 2: 1100..3000ms = frame 33 start.  "CD" pre-roll = 7
        // frames (E1 added the doubled ENM pair) → firstFrame = 26.
        // Frame 26 is inside cue 1's ENM doubled at frames 25-26 — no
        // amount of EDM elision can rescue this collision.
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
        // Pre-roll for a 2-char pop-on cue is 7 frames (E1: 2 RCL + 2
        // ENM + 2 PAC + 1 char).  Cue 0: starts at frame ~3 → firstFrame
        // would be -4.  Dropped (before t=0).
        subs.append(Subtitle(tsFromMs(100), tsFromMs(1000), "AB"));
        // Cue 1: starts at frame 30 — firstFrame 23.  Comfortable; kept.
        // lastEocFrame after kept = 31.
        subs.append(Subtitle(tsFromMs(1000), tsFromMs(2000), "CD"));
        // Cue 2: starts at frame 33 — firstFrame 26.  Overlaps cue 1's
        // ENM at frame 25/26 (lastEocFrame=31).  Dropped (wire-stream
        // collision — EDM elision cannot rescue).
        subs.append(Subtitle(tsFromMs(1100), tsFromMs(3000), "EF"));
        // Cue 3: starts at frame 65 — firstFrame 58.  Overlaps cue 1's
        // EDM at frame 60/61, but EDM elision rescues it.  Kept.
        subs.append(Subtitle(tsFromMs(2167), tsFromMs(3000), "GH"));
        // Cue 4: starts at frame 120 — firstFrame 113.  Kept.
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
        // firstFrame = 120 - 7 = 113, well after cue 1's EDM at 61.
        // Pre-roll grew from 5 to 7 frames in E1 (added doubled ENM).
        Cea608Encoder::Config cfg;
        cfg.frameRate = FrameRate(FrameRate::FPS_30);
        Cea608Encoder enc(cfg);
        SubtitleList  subs;
        subs.append(Subtitle(tsFromMs(1000), tsFromMs(2000), "AB"));
        subs.append(Subtitle(tsFromMs(4000), tsFromMs(5000), "CD"));
        REQUIRE(enc.setSubtitles(subs).isOk());
        // Cue 2: RCL at frame 113.
        CHECK(tripleHasBytes(oneTriple(enc, 113), Cea608::RclB1, Cea608::RclB2));
}

// ============================================================================
// earliestStartFor diagnostic
// ============================================================================

TEST_CASE("Cea608Encoder::earliestStartFor returns the RCL frame for a cue") {
        Cea608Encoder::Config cfg;
        cfg.frameRate = FrameRate(FrameRate::FPS_30);
        Cea608Encoder enc(cfg);
        // 2-char pop-on pre-roll = 7 frames (2 RCL + 2 ENM + 2 PAC + 1
        // char); the doubled ENM was added by E1.  First RCL at frame
        // 30 - 7 = 23.
        Subtitle    cue(tsFromMs(1000), tsFromMs(2000), "AB");
        FrameNumber first = enc.earliestStartFor(cue);
        REQUIRE(first.isValid());
        CHECK(first.value() == 23);
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

TEST_CASE("Cea608Encoder[roll-up]: TopCenter anchor places base row at the top region (§B.5)") {
        // §B.5: roll-up base rows are 1..4 (top) or 12..15 (bottom).
        // A top-anchored cue with rollUpRows=2 must emit a PAC for
        // row 2 (visible rows 1, 2) — NOT the bottom default row 15.
        Cea608Encoder::Config cfg;
        cfg.frameRate = FrameRate(FrameRate::FPS_30);
        cfg.mode = Cea608Encoder::Mode::RollUp;
        cfg.rollUpRows = 2;
        Cea608Encoder enc(cfg);
        SubtitleList  subs;
        Subtitle s(tsFromMs(1000), tsFromMs(2000), "AB");
        s.setAnchor(SubtitleAnchor::TopCenter);
        subs.append(s);
        REQUIRE(enc.setSubtitles(subs).isOk());

        // PAC at frame 28-29 — row 2.  Decode it back to confirm.
        Cea708Cdp::CcData pacTriple = oneTriple(enc, 28);
        const uint8_t pb1 = Cea608::stripParity(pacTriple.b1);
        const uint8_t pb2 = Cea608::stripParity(pacTriple.b2);
        Cea608::PacAttr pac;
        REQUIRE(Cea608::decodePac(pb1, pb2, pac));
        CHECK(pac.row == 2);

        // Round-trip through the decoder to confirm.
        Cea608Decoder dec;
        for (int64_t f = 0; f < 90; ++f) {
                dec.pushFrame(FrameNumber(f), tsFromMs(f * 1000 / 30), enc.nextFrame(FrameNumber(f)));
        }
        SubtitleList out = dec.finalize();
        REQUIRE(out.size() == 1);
        CHECK(out[0].text() == "AB");
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
        // 13-char cue, fits one row → pre-roll = 2 (RCL) + 2 (ENM) +
        // 2 (PAC) + 7 char pairs = 13 frames (E1 added the ENM pair).
        subs.append(Subtitle(tsFromMs(3000), tsFromMs(6000), "Hello, world!"));
        REQUIRE(enc.setSubtitles(subs).isOk());
        // Cue starts at frame 90; pre-roll begins at 90 - 13 = 77.
        CHECK(tripleHasBytes(oneTriple(enc, 77), Cea608::RclB1, Cea608::RclB2));
        CHECK(tripleHasBytes(oneTriple(enc, 78), Cea608::RclB1, Cea608::RclB2));
        // Exactly one distinct PAC row in the pre-roll window.
        CHECK(countDistinctPacs(enc, 77, 89) == 1);
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

TEST_CASE("Cea608Encoder: mixed cue modes encode per-cue mode-establishing control codes") {
        // Three cues with three explicit modes — each cue should
        // dispatch under its own mode and emit the matching mode-
        // establishing control code (RUx / RDC / RCL) in its
        // pre-roll window.  Cues are spaced far apart so cross-mode
        // EDM flushes have room to fire.
        Cea608Encoder::Config cfg;
        cfg.frameRate = FrameRate(FrameRate::FPS_30);
        Cea608Encoder enc(cfg);
        SubtitleList  subs;
        Subtitle      cue1(tsFromMs(2000), tsFromMs(3000), "A");
        cue1.setMode(CaptionMode::RollUp);
        Subtitle cue2(tsFromMs(5000), tsFromMs(6000), "B");
        cue2.setMode(CaptionMode::PaintOn);
        Subtitle cue3(tsFromMs(8000), tsFromMs(9000), "C");
        cue3.setMode(CaptionMode::PopOn);
        subs.append(cue1);
        subs.append(cue2);
        subs.append(cue3);
        REQUIRE(enc.setSubtitles(subs).isOk());

        auto saw = [&](int64_t lo, int64_t hi, uint8_t miscByte) {
                for (int64_t f = lo; f < hi; ++f) {
                        if (tripleHasBytes(oneTriple(enc, f),
                                           Cea608::Cc1MiscFirstByte, miscByte)) {
                                return true;
                        }
                }
                return false;
        };
        // RollUp cue starts at frame 60 (2s @ 30fps) — RUx pre-roll
        // lands somewhere in [50, 60).
        CHECK(saw(50, 60, Cea608::MiscRU2));
        // PaintOn cue starts at frame 150 (5s) — RDC pre-roll lands
        // somewhere in [140, 150).
        CHECK(saw(140, 150, Cea608::MiscRDC));
        // PopOn cue starts at frame 240 (8s) — RCL pre-roll lands
        // somewhere in [200, 240).
        CHECK(saw(200, 240, Cea608::MiscRCL));
}

// ============================================================================
// CC2 / CC3 / CC4 channel support — channel-bit OR-mask in the wire bytes
// ============================================================================

TEST_CASE("Cea608Encoder: CC2 RCL pre-roll carries the channel-bit-set first byte (0x1C)") {
        // CC1 RCL is (0x14, 0x20); CC2 RCL is (0x1C, 0x20) — the channel
        // selector lives in bit 3 of the first byte.
        Cea608Encoder::Config cfg;
        cfg.frameRate = FrameRate(FrameRate::FPS_30);
        cfg.channel = Cea608Encoder::Channel::CC2;
        Cea608Encoder enc(cfg);
        SubtitleList  subs;
        subs.append(Subtitle(tsFromMs(2000), tsFromMs(3000), "AB"));
        REQUIRE(enc.setSubtitles(subs).isOk());

        bool sawCc2Rcl = false;
        for (int64_t f = 0; f < 60; ++f) {
                Cea708Cdp::CcData t = oneTriple(enc, f);
                if (Cea608::stripParity(t.b1) == 0x1C
                    && Cea608::stripParity(t.b2) == Cea608::MiscRCL) {
                        sawCc2Rcl = true;
                        break;
                }
        }
        CHECK(sawCc2Rcl);
}

TEST_CASE("Cea608Encoder: CC2 PAC carries the channel-bit OR'd into bit 3") {
        // CC1 row-15 white PAC is (0x14, 0x70); CC2 is (0x1C, 0x70).
        // Default anchor leaves the row at flush-left (column 0) so
        // the PAC second byte stays the canonical 0x70.
        Cea608Encoder::Config cfg;
        cfg.frameRate = FrameRate(FrameRate::FPS_30);
        cfg.channel = Cea608Encoder::Channel::CC2;
        Cea608Encoder enc(cfg);
        SubtitleList  subs;
        subs.append(Subtitle(tsFromMs(2000), tsFromMs(3000), "AB"));
        REQUIRE(enc.setSubtitles(subs).isOk());

        bool sawCc2Pac = false;
        for (int64_t f = 0; f < 60; ++f) {
                Cea708Cdp::CcData t = oneTriple(enc, f);
                const uint8_t b1 = Cea608::stripParity(t.b1);
                const uint8_t b2 = Cea608::stripParity(t.b2);
                if (b1 == 0x1C && b2 == 0x70) {
                        sawCc2Pac = true;
                        break;
                }
        }
        CHECK(sawCc2Pac);
}

TEST_CASE("Cea608Encoder: CC3 emits cc_type=1 (field 2)") {
        // CC3 lives in field 2, so its CDP cc_type is 1.  The
        // intra-field channel byte is the same as CC1 (bit 3 = 0).
        Cea608Encoder::Config cfg;
        cfg.frameRate = FrameRate(FrameRate::FPS_30);
        cfg.channel = Cea608Encoder::Channel::CC3;
        Cea608Encoder enc(cfg);
        SubtitleList  subs;
        subs.append(Subtitle(tsFromMs(2000), tsFromMs(3000), "X"));
        REQUIRE(enc.setSubtitles(subs).isOk());

        // Pre-roll RCL at some frame — verify cc_type=1 (field 2) and
        // the §8.4(a) F2 misc-control b1 = 0x15 (not 0x14: that's the
        // F1 form; F2 remap is mandatory per §8.4(a) so a spec-strict
        // receiver can route the RCL).
        bool sawCc3Rcl = false;
        for (int64_t f = 0; f < 60; ++f) {
                Cea708Cdp::CcData t = oneTriple(enc, f);
                if (t.type == 1
                    && Cea608::stripParity(t.b1) == 0x15
                    && Cea608::stripParity(t.b2) == Cea608::MiscRCL) {
                        sawCc3Rcl = true;
                        break;
                }
        }
        CHECK(sawCc3Rcl);
}

TEST_CASE("Cea608Encoder + Decoder: CC3 Row-15 PAC round-trips (no §8.4 over-remap)") {
        // Regression for the §8.4 over-remap bug: the F2 misc-control
        // remap (b1=0x14 → 0x15 / b1=0x1C → 0x1D) applies ONLY when b2
        // is in 0x20..0x2F (the misc-control range).  The PAC for
        // Row 15 (default Bottom anchor) uses b1=0x14, b2=0x70 — its
        // b2 sits OUTSIDE the misc range, so the encoder must leave
        // b1 alone.  Before the fix, b1 was remapped unconditionally,
        // corrupting every CC3/CC4 cue anchored on rows 14/15.
        Cea608Encoder::Config encCfg;
        encCfg.frameRate = FrameRate(FrameRate::FPS_30);
        encCfg.channel = Cea608Encoder::Channel::CC3;
        Cea608Encoder enc(encCfg);
        SubtitleList  in;
        // Even-length text avoids the encoder's odd-pair pad appending
        // a trailing space (round-trip alignment, not a §8.4 concern).
        in.append(Subtitle(tsAt30fps(60), tsAt30fps(120), "Row 15 CC3"));
        REQUIRE(enc.setSubtitles(in).isOk());

        // Walk the encoded byte stream and assert that for every PAC
        // we emit (b1=0x14 / b1=0x1C with b2 in 0x40..0x7F), the b1
        // stays at the F1 form — the §8.4 remap MUST NOT touch it.
        // Misc-control pairs (b2 in 0x20..0x2F) still get the remap.
        bool sawPacRow15 = false;
        bool sawMiscRemap = false;
        for (int64_t f = 0; f < 140; ++f) {
                Cea708Cdp::CcData t = oneTriple(enc, f);
                const uint8_t b1 = Cea608::stripParity(t.b1);
                const uint8_t b2 = Cea608::stripParity(t.b2);
                if (b2 >= 0x40 && b2 <= 0x7F) {
                        // Looks like a PAC b2.  If b1 is 0x14 / 0x1C the
                        // PAC was correctly NOT remapped.  Catch the
                        // corrupted form too: 0x15 / 0x1D + b2 in the
                        // PAC range is the bug signature.
                        if (b1 == 0x14 || b1 == 0x1C) {
                                if (b2 == 0x70) sawPacRow15 = true;
                        } else if (b1 == 0x15 || b1 == 0x1D) {
                                // bug signature — must not occur
                                INFO("Spurious F2 remap on PAC: b1=", static_cast<int>(b1),
                                     " b2=", static_cast<int>(b2));
                                CHECK(false);
                        }
                }
                if ((b1 == 0x15 || b1 == 0x1D) && b2 >= 0x20 && b2 <= 0x2F) {
                        sawMiscRemap = true;
                }
        }
        CHECK(sawPacRow15);     // Row 15 PAC must appear in F1 form on F2.
        CHECK(sawMiscRemap);    // Misc-control bytes (RCL, EDM, EOC) still remap.

        // End-to-end: decode the same stream through a CC3 decoder.
        Cea608Decoder::Config decCfg;
        decCfg.channel = Cea608Decoder::Channel::CC3;
        Cea608Decoder dec(decCfg);
        for (int64_t f = 0; f < 140; ++f) {
                dec.pushFrame(FrameNumber(f), tsAt30fps(f), enc.nextFrame(FrameNumber(f)));
        }
        SubtitleList out = dec.finalize();
        REQUIRE(out.size() == 1);
        CHECK(out[0].text() == "Row 15 CC3");
}

TEST_CASE("Cea608Encoder + Decoder: CC2 round-trips end-to-end") {
        Cea608Encoder::Config encCfg;
        encCfg.frameRate = FrameRate(FrameRate::FPS_30);
        encCfg.channel = Cea608Encoder::Channel::CC2;
        Cea608Encoder enc(encCfg);
        SubtitleList  in;
        // Even-length text avoids the encoder's odd-pair pad
        // appending a trailing space.
        in.append(Subtitle(tsAt30fps(60), tsAt30fps(120), "Hello CC2!"));
        REQUIRE(enc.setSubtitles(in).isOk());

        Cea608Decoder::Config decCfg;
        decCfg.channel = Cea608Decoder::Channel::CC2;
        Cea608Decoder dec(decCfg);
        for (int64_t f = 0; f < 140; ++f) {
                dec.pushFrame(FrameNumber(f), tsAt30fps(f), enc.nextFrame(FrameNumber(f)));
        }
        SubtitleList out = dec.finalize();
        REQUIRE(out.size() == 1);
        CHECK(out[0].text() == "Hello CC2!");
}

TEST_CASE("Cea608Encoder + Decoder: CC2 stream isn't picked up by a CC1 decoder") {
        // Verify channel isolation: the CC2 wire bytes have bit 3 set
        // in their control-byte first bytes, so a decoder configured
        // for CC1 (which filters by `(b1 & 0x08) == 0`) drops them all.
        Cea608Encoder::Config encCfg;
        encCfg.frameRate = FrameRate(FrameRate::FPS_30);
        encCfg.channel = Cea608Encoder::Channel::CC2;
        Cea608Encoder enc(encCfg);
        SubtitleList  in;
        in.append(Subtitle(tsAt30fps(60), tsAt30fps(120), "CC2 only"));
        REQUIRE(enc.setSubtitles(in).isOk());

        Cea608Decoder dec; // default CC1
        for (int64_t f = 0; f < 140; ++f) {
                dec.pushFrame(FrameNumber(f), tsAt30fps(f), enc.nextFrame(FrameNumber(f)));
        }
        SubtitleList out = dec.finalize();
        CHECK(out.size() == 0);
}

TEST_CASE("Cea608Encoder + Decoder: time-shared CC1 + CC2 stream both decode independently") {
        // Real broadcast usage: English on CC1, Spanish on CC2 sharing
        // the same field-1 wire timeline.  CEA-608 only carries one
        // channel's byte-pair per frame in a given field — so two
        // encoders' outputs are *time-multiplexed* (CC1 cue lands in
        // one window, CC2 cue in another, no overlap).  At the merge
        // point each frame still has at most one byte pair from each
        // channel, but in practice they occupy disjoint frame ranges.
        // Each decoder filters for its own channel via the
        // @c (b1 & 0x08) == 0/1 channel-bit check.
        Cea608Encoder::Config encCfg1;
        encCfg1.frameRate = FrameRate(FrameRate::FPS_30);
        encCfg1.channel = Cea608Encoder::Channel::CC1;
        Cea608Encoder enc1(encCfg1);

        Cea608Encoder::Config encCfg2;
        encCfg2.frameRate = FrameRate(FrameRate::FPS_30);
        encCfg2.channel = Cea608Encoder::Channel::CC2;
        Cea608Encoder enc2(encCfg2);

        SubtitleList in1;
        in1.append(Subtitle(tsAt30fps(60), tsAt30fps(120), "English!"));
        REQUIRE(enc1.setSubtitles(in1).isOk());

        SubtitleList in2;
        // CC2 cue is time-shifted past CC1's pre-roll + display +
        // post-EDM window so the two channels' wire bytes don't
        // overlap on the field-1 timeline.
        in2.append(Subtitle(tsAt30fps(180), tsAt30fps(240), "Espanola"));
        REQUIRE(enc2.setSubtitles(in2).isOk());

        Cea608Decoder dec1; // CC1
        Cea608Decoder::Config dec2Cfg;
        dec2Cfg.channel = Cea608Decoder::Channel::CC2;
        Cea608Decoder dec2(dec2Cfg);

        for (int64_t f = 0; f < 280; ++f) {
                Cea708Cdp::CcDataList merged;
                Cea708Cdp::CcDataList l1 = enc1.nextFrame(FrameNumber(f));
                Cea708Cdp::CcDataList l2 = enc2.nextFrame(FrameNumber(f));
                for (size_t i = 0; i < l1.size(); ++i) merged.pushToBack(l1[i]);
                for (size_t i = 0; i < l2.size(); ++i) merged.pushToBack(l2[i]);
                dec1.pushFrame(FrameNumber(f), tsAt30fps(f), merged);
                dec2.pushFrame(FrameNumber(f), tsAt30fps(f), merged);
        }
        SubtitleList out1 = dec1.finalize();
        SubtitleList out2 = dec2.finalize();
        REQUIRE(out1.size() == 1);
        REQUIRE(out2.size() == 1);
        CHECK(out1[0].text() == "English!");
        CHECK(out2[0].text() == "Espanola");
}

TEST_CASE("Cea608Encoder + Decoder: CC3 special character (™) — §8.4 F2 misc remap NOT applied (E11)") {
        // §8.4(a)(b) F2 remap (b1=0x14 → 0x15, b1=0x1C → 0x1D) is
        // gated by @c b2 in @c 0x20..0x2F (the misc-control range).
        // The Special Character control pair sits at @c (0x11, 0x30..0x3F)
        // on CC1 — @c b2 is OUTSIDE the misc range, so a CC3 encoder
        // MUST NOT remap the @c b1.  CC3 is field-2, channel-1: bit 3
        // of the first byte stays clear (no channel OR), so the Special
        // pair lands as @c (0x11, 0x34) on the wire (cc_type=1).
        //
        // This regression check protects against a future widening of
        // the F2 remap to non-misc control families.
        Cea608Encoder::Config encCfg;
        encCfg.frameRate = FrameRate(FrameRate::FPS_30);
        encCfg.channel = Cea608Encoder::Channel::CC3;
        Cea608Encoder enc(encCfg);
        SubtitleList  in;
        // Even-length text keeps the encoder's odd-pair pad from
        // affecting the trailing fill.
        in.append(Subtitle(tsAt30fps(60), tsAt30fps(120), "TM\xE2\x84\xA2"));
        REQUIRE(enc.setSubtitles(in).isOk());

        // Walk the wire and find the Special-character pair @c (0x11, 0x34).
        // It MUST appear (no bit-3 OR, no §8.4 remap on b2=0x34) and the
        // bug signature @c (0x19, *) MUST NOT.
        bool sawSpecialPair = false;
        bool sawBugSignature = false;
        bool cc3OnFieldTwo = true;
        for (int64_t f = 0; f < 140; ++f) {
                Cea708Cdp::CcData t = oneTriple(enc, f);
                const uint8_t b1 = Cea608::stripParity(t.b1);
                const uint8_t b2 = Cea608::stripParity(t.b2);
                if (b1 == 0x11 && b2 == 0x34) {
                        sawSpecialPair = true;
                        if (t.type != 1) cc3OnFieldTwo = false;
                }
                if (b1 == 0x19 && b2 >= 0x30 && b2 <= 0x3F) {
                        sawBugSignature = true;
                }
        }
        CHECK(sawSpecialPair);
        CHECK_FALSE(sawBugSignature);
        CHECK(cc3OnFieldTwo);

        // End-to-end: the same stream decodes back through a CC3 decoder.
        Cea608Decoder::Config decCfg;
        decCfg.channel = Cea608Decoder::Channel::CC3;
        Cea608Decoder dec(decCfg);
        for (int64_t f = 0; f < 140; ++f) {
                dec.pushFrame(FrameNumber(f), tsAt30fps(f), enc.nextFrame(FrameNumber(f)));
        }
        SubtitleList out = dec.finalize();
        REQUIRE(out.size() == 1);
        CHECK(out[0].text() == String("TM\xE2\x84\xA2"));
}

TEST_CASE("Cea608Encoder + Decoder: CC2 special character (™) round-trips") {
        // Special-character control code for CC1 is (0x11, 0x34); for
        // CC2 it's (0x19, 0x34) — verifies the channel-bit OR-mask
        // flows through encodeCharPairs' control emissions too.
        Cea608Encoder::Config encCfg;
        encCfg.frameRate = FrameRate(FrameRate::FPS_30);
        encCfg.channel = Cea608Encoder::Channel::CC2;
        Cea608Encoder enc(encCfg);
        SubtitleList  in;
        in.append(Subtitle(tsAt30fps(60), tsAt30fps(120), "TM\xE2\x84\xA2"));
        REQUIRE(enc.setSubtitles(in).isOk());

        Cea608Decoder::Config decCfg;
        decCfg.channel = Cea608Decoder::Channel::CC2;
        Cea608Decoder dec(decCfg);
        for (int64_t f = 0; f < 140; ++f) {
                dec.pushFrame(FrameNumber(f), tsAt30fps(f), enc.nextFrame(FrameNumber(f)));
        }
        SubtitleList out = dec.finalize();
        REQUIRE(out.size() == 1);
        CHECK(out[0].text() == String("TM\xE2\x84\xA2"));
}

// ============================================================================
// BT (Background Transparent) round-trip — SubtitleOpacity::Transparent
// ============================================================================

TEST_CASE("Cea608Encoder + Decoder: SubtitleOpacity::Transparent background round-trips via doubled BT") {
        // §6.2 Table 3 BT (0x17, 0x2D): removes the background box.
        // A span with backgroundOpacity == Transparent must emit a
        // doubled BT pair and decode back with the same opacity.
        Cea608Encoder::Config encCfg;
        encCfg.frameRate = FrameRate(FrameRate::FPS_30);
        Cea608Encoder enc(encCfg);

        SubtitleSpan span("AB", false, false, false, Color());
        span.setBackgroundOpacity(SubtitleOpacity::Transparent);
        SubtitleSpan::List spans;
        spans.pushToBack(span);
        Subtitle s(tsAt30fps(60), tsAt30fps(120), spans,
                   SubtitleAnchor::Default, Rect2Di32(), String(), Metadata());
        SubtitleList in;
        in.append(s);
        REQUIRE(enc.setSubtitles(in).isOk());

        // Walk the encoded wire — confirm a doubled BT pair exists.
        int btCount = 0;
        for (int64_t f = 0; f < 140; ++f) {
                Cea708Cdp::CcData t = oneTriple(enc, f);
                if (Cea608::stripParity(t.b1) == Cea608::Cc1ExtAttrB1
                    && Cea608::stripParity(t.b2) == Cea608::BtB2) {
                        ++btCount;
                }
        }
        CHECK(btCount >= 2); // doubled per §B.14

        Cea608Decoder dec;
        for (int64_t f = 0; f < 140; ++f) {
                dec.pushFrame(FrameNumber(f), tsAt30fps(f), enc.nextFrame(FrameNumber(f)));
        }
        SubtitleList out = dec.finalize();
        REQUIRE(out.size() == 1);
        REQUIRE(out[0].spans().size() >= 1);
        // Find a span carrying Transparent bg opacity.
        bool sawTransparent = false;
        for (size_t i = 0; i < out[0].spans().size(); ++i) {
                if (out[0].spans()[i].backgroundOpacity() == SubtitleOpacity::Transparent) {
                        sawTransparent = true;
                        break;
                }
        }
        CHECK(sawTransparent);
}

// ============================================================================
// FON (Flash On) round-trip — SubtitleOpacity::Flash through the encoder
// ============================================================================

TEST_CASE("Cea608Encoder + Decoder: SubtitleOpacity::Flash round-trips via doubled FON") {
        // §6.2 FON (0x14, 0x28): sets the flash attribute for all
        // subsequent characters until the next PAC clears it.  The
        // encoder must emit a doubled FON pair when a span carries
        // SubtitleOpacity::Flash; the decoder must recover the
        // attribute on the recovered span.
        Cea608Encoder::Config encCfg;
        encCfg.frameRate = FrameRate(FrameRate::FPS_30);
        Cea608Encoder enc(encCfg);

        SubtitleSpan span("AB", false, false, false, Color());
        span.setForegroundOpacity(SubtitleOpacity::Flash);
        SubtitleSpan::List spans;
        spans.pushToBack(span);
        Subtitle s(tsAt30fps(60), tsAt30fps(120), spans,
                   SubtitleAnchor::Default, Rect2Di32(), String(), Metadata());
        SubtitleList in;
        in.append(s);
        REQUIRE(enc.setSubtitles(in).isOk());

        // Walk the encoded wire and confirm a doubled FON pair lands
        // on the schedule.  FON is 0x14 (Cc1MiscFirstByte) + 0x28
        // (MiscFON).
        int fonCount = 0;
        for (int64_t f = 0; f < 140; ++f) {
                Cea708Cdp::CcData t = oneTriple(enc, f);
                if (Cea608::stripParity(t.b1) == Cea608::Cc1MiscFirstByte
                    && Cea608::stripParity(t.b2) == Cea608::MiscFON) {
                        ++fonCount;
                }
        }
        CHECK(fonCount >= 2); // doubled per §B.14

        Cea608Decoder dec;
        for (int64_t f = 0; f < 140; ++f) {
                dec.pushFrame(FrameNumber(f), tsAt30fps(f), enc.nextFrame(FrameNumber(f)));
        }
        SubtitleList out = dec.finalize();
        REQUIRE(out.size() == 1);
        REQUIRE(out[0].spans().size() >= 1);
        // The recovered span carries the flash attribute.
        bool sawFlash = false;
        for (size_t i = 0; i < out[0].spans().size(); ++i) {
                if (out[0].spans()[i].foregroundOpacity() == SubtitleOpacity::Flash) {
                        sawFlash = true;
                        break;
                }
        }
        CHECK(sawFlash);
}

// ============================================================================
// Per-cue mode mixing — pop-on / paint-on / roll-up in one cue list
// ============================================================================

TEST_CASE("Cea608Encoder + Decoder: pop-on then paint-on cues round-trip with their own modes") {
        // Even-length text keeps the encoder's odd-pair pad from
        // appending a trailing space.
        Cea608Encoder::Config encCfg;
        encCfg.frameRate = FrameRate(FrameRate::FPS_30);
        Cea608Encoder enc(encCfg);
        SubtitleList  in;
        Subtitle      cue1(tsAt30fps(60), tsAt30fps(120), "POPS");
        cue1.setMode(CaptionMode::PopOn);
        Subtitle cue2(tsAt30fps(180), tsAt30fps(240), "PAIN");
        cue2.setMode(CaptionMode::PaintOn);
        in.append(cue1);
        in.append(cue2);
        REQUIRE(enc.setSubtitles(in).isOk());

        Cea608Decoder dec;
        for (int64_t f = 0; f < 280; ++f) {
                dec.pushFrame(FrameNumber(f), tsAt30fps(f), enc.nextFrame(FrameNumber(f)));
        }
        SubtitleList out = dec.finalize();
        REQUIRE(out.size() == 2);
        CHECK(out[0].text() == "POPS");
        CHECK(out[0].mode() == CaptionMode::PopOn);
        CHECK(out[1].text() == "PAIN");
        CHECK(out[1].mode() == CaptionMode::PaintOn);
}

TEST_CASE("Cea608Encoder + Decoder: paint-on then roll-up round-trip with their own modes") {
        Cea608Encoder::Config encCfg;
        encCfg.frameRate = FrameRate(FrameRate::FPS_30);
        Cea608Encoder enc(encCfg);
        SubtitleList  in;
        Subtitle      cue1(tsAt30fps(60), tsAt30fps(120), "PAIN");
        cue1.setMode(CaptionMode::PaintOn);
        Subtitle cue2(tsAt30fps(180), tsAt30fps(240), "ROLL");
        cue2.setMode(CaptionMode::RollUp);
        in.append(cue1);
        in.append(cue2);
        REQUIRE(enc.setSubtitles(in).isOk());

        Cea608Decoder dec;
        for (int64_t f = 0; f < 280; ++f) {
                dec.pushFrame(FrameNumber(f), tsAt30fps(f), enc.nextFrame(FrameNumber(f)));
        }
        SubtitleList out = dec.finalize();
        REQUIRE(out.size() == 2);
        CHECK(out[0].text() == "PAIN");
        CHECK(out[0].mode() == CaptionMode::PaintOn);
        CHECK(out[1].text() == "ROLL");
        CHECK(out[1].mode() == CaptionMode::RollUp);
}

TEST_CASE("Cea608Encoder + Decoder: three-mode cue list round-trips per-cue modes") {
        Cea608Encoder::Config encCfg;
        encCfg.frameRate = FrameRate(FrameRate::FPS_30);
        Cea608Encoder enc(encCfg);
        SubtitleList  in;
        Subtitle      cue1(tsAt30fps(60), tsAt30fps(120), "ONEX");
        cue1.setMode(CaptionMode::PopOn);
        Subtitle cue2(tsAt30fps(180), tsAt30fps(240), "TWOY");
        cue2.setMode(CaptionMode::RollUp);
        Subtitle cue3(tsAt30fps(300), tsAt30fps(360), "TREZ");
        cue3.setMode(CaptionMode::PaintOn);
        in.append(cue1);
        in.append(cue2);
        in.append(cue3);
        REQUIRE(enc.setSubtitles(in).isOk());

        Cea608Decoder dec;
        for (int64_t f = 0; f < 400; ++f) {
                dec.pushFrame(FrameNumber(f), tsAt30fps(f), enc.nextFrame(FrameNumber(f)));
        }
        SubtitleList out = dec.finalize();
        REQUIRE(out.size() == 3);
        CHECK(out[0].text() == "ONEX");
        CHECK(out[0].mode() == CaptionMode::PopOn);
        CHECK(out[1].text() == "TWOY");
        CHECK(out[1].mode() == CaptionMode::RollUp);
        CHECK(out[2].text() == "TREZ");
        CHECK(out[2].mode() == CaptionMode::PaintOn);
}

TEST_CASE("Cea608Encoder: re-entering RollUp after another mode re-emits RUx") {
        // First RollUp cue establishes the receiver's roll-up window
        // with RUx.  An intervening paint-on cue ends roll-up state;
        // the next RollUp cue must re-emit RUx to re-establish the
        // window (the receiver doesn't auto-restore from another mode).
        Cea608Encoder::Config encCfg;
        encCfg.frameRate = FrameRate(FrameRate::FPS_30);
        Cea608Encoder enc(encCfg);
        SubtitleList  in;
        Subtitle      cue1(tsAt30fps(60), tsAt30fps(120), "R1");
        cue1.setMode(CaptionMode::RollUp);
        Subtitle cue2(tsAt30fps(180), tsAt30fps(240), "P");
        cue2.setMode(CaptionMode::PaintOn);
        Subtitle cue3(tsAt30fps(300), tsAt30fps(360), "R2");
        cue3.setMode(CaptionMode::RollUp);
        in.append(cue1);
        in.append(cue2);
        in.append(cue3);
        REQUIRE(enc.setSubtitles(in).isOk());

        // Count RU2 control codes across the schedule — should appear
        // in two distinct pre-roll windows (before frame 60 and before
        // frame 300), not just once.
        int ruxCount = 0;
        int64_t lastRuxFrame = -100;
        for (int64_t f = 0; f < 400; ++f) {
                if (tripleHasBytes(oneTriple(enc, f), Cea608::Cc1MiscFirstByte,
                                   Cea608::MiscRU2)
                    && f - lastRuxFrame > 5) {
                        ++ruxCount;
                        lastRuxFrame = f;
                }
        }
        // Two distinct RUx pairs (one per roll-up segment); each
        // segment doubles the RUx so the counter sees both bytes.
        CHECK(ruxCount >= 2);
}

TEST_CASE("Cea608Encoder + Decoder: per-cue rollUpRows changes RUx between adjacent cues") {
        // Two consecutive roll-up cues with different rollUpRows.
        // The encoder must re-emit RUx with the new row count
        // (RU2 → RU3) when the count changes between cues.
        Cea608Encoder::Config encCfg;
        encCfg.frameRate = FrameRate(FrameRate::FPS_30);
        Cea608Encoder enc(encCfg);
        SubtitleList  in;
        Subtitle      cue1(tsAt30fps(60), tsAt30fps(120), "X");
        cue1.setMode(CaptionMode::RollUp);
        cue1.setRollUpRows(2);
        Subtitle cue2(tsAt30fps(180), tsAt30fps(240), "Y");
        cue2.setMode(CaptionMode::RollUp);
        cue2.setRollUpRows(3);
        in.append(cue1);
        in.append(cue2);
        REQUIRE(enc.setSubtitles(in).isOk());

        bool sawRu2 = false;
        bool sawRu3 = false;
        for (int64_t f = 0; f < 240; ++f) {
                Cea708Cdp::CcData t = oneTriple(enc, f);
                if (tripleHasBytes(t, Cea608::Cc1MiscFirstByte, Cea608::MiscRU2)) sawRu2 = true;
                if (tripleHasBytes(t, Cea608::Cc1MiscFirstByte, Cea608::MiscRU3)) sawRu3 = true;
        }
        CHECK(sawRu2);
        CHECK(sawRu3);
}

TEST_CASE("Cea608Encoder + Decoder: Default cue mode falls back to Config::mode") {
        // A cue with CaptionMode::Default should pick up the encoder's
        // Config::mode (PaintOn here).
        Cea608Encoder::Config encCfg;
        encCfg.frameRate = FrameRate(FrameRate::FPS_30);
        encCfg.mode = Cea608Encoder::Mode::PaintOn;
        Cea608Encoder enc(encCfg);
        SubtitleList  in;
        // Cue keeps Default mode → encoder picks PaintOn.
        in.append(Subtitle(tsAt30fps(60), tsAt30fps(120), "DEFX"));
        REQUIRE(enc.setSubtitles(in).isOk());

        Cea608Decoder dec;
        for (int64_t f = 0; f < 140; ++f) {
                dec.pushFrame(FrameNumber(f), tsAt30fps(f), enc.nextFrame(FrameNumber(f)));
        }
        SubtitleList out = dec.finalize();
        REQUIRE(out.size() == 1);
        CHECK(out[0].text() == "DEFX");
        CHECK(out[0].mode() == CaptionMode::PaintOn);
}

// ============================================================================
// Tab Offset constants + helpers
// ============================================================================

TEST_CASE("Cea608::encodeTabOffset round-trips T1 / T2 / T3 codes") {
        for (int n : {1, 2, 3}) {
                uint8_t b1 = 0, b2 = 0;
                Cea608::encodeTabOffset(n, b1, b2);
                CHECK(b1 == Cea608::Cc1ExtAttrB1);
                CHECK(b2 == static_cast<uint8_t>(0x20 + n));
                CHECK(Cea608::isTabOffset(b1, b2));
                int got = 0;
                CHECK(Cea608::decodeTabOffset(b1, b2, got));
                CHECK(got == n);
        }
}

TEST_CASE("Cea608::encodeTabOffset clamps out-of-range values to [1,3]") {
        uint8_t b1 = 0, b2 = 0;
        Cea608::encodeTabOffset(0, b1, b2);
        CHECK(b2 == Cea608::TabOffsetT1);
        Cea608::encodeTabOffset(99, b1, b2);
        CHECK(b2 == Cea608::TabOffsetT3);
}

TEST_CASE("Cea608::isTabOffset rejects non-Tab-Offset pairs") {
        CHECK_FALSE(Cea608::isTabOffset(0x14, Cea608::MiscRCL));
        CHECK_FALSE(Cea608::isTabOffset(Cea608::Cc1ExtAttrB1, 0x40));
        CHECK_FALSE(Cea608::isTabOffset(Cea608::Cc1ExtAttrB1, 0x20));
        CHECK_FALSE(Cea608::isTabOffset(Cea608::Cc1ExtAttrB1, 0x24));
}

// ============================================================================
// Horizontal positioning — PAC indent + Tab Offset
// ============================================================================

TEST_CASE("Cea608Encoder: BottomLeft anchor lands chars at column 0 (no Tab Offset)") {
        Cea608Encoder::Config cfg;
        cfg.frameRate = FrameRate(FrameRate::FPS_30);
        Cea608Encoder enc(cfg);
        SubtitleList  subs;
        subs.append(Subtitle(tsFromMs(2000), tsFromMs(3000), "AB",
                             SubtitleAnchor::BottomLeft));
        REQUIRE(enc.setSubtitles(subs).isOk());
        // No Tab Offset anywhere in the schedule.
        bool sawTabOffset = false;
        for (int64_t f = 0; f < 90; ++f) {
                Cea708Cdp::CcData t = oneTriple(enc, f);
                const uint8_t b1 = Cea608::stripParity(t.b1);
                const uint8_t b2 = Cea608::stripParity(t.b2);
                if (Cea608::isTabOffset(b1, b2)) {
                        sawTabOffset = true;
                        break;
                }
        }
        CHECK_FALSE(sawTabOffset);
}

TEST_CASE("Cea608Encoder: BottomCenter anchor with 4-char cue uses PAC indent + Tab Offset") {
        // "WORD" is 4 chars; centered → column = (32-4)/2 = 14.
        // PAC indent = 12, Tab Offset = T2 (+2 columns).
        Cea608Encoder::Config cfg;
        cfg.frameRate = FrameRate(FrameRate::FPS_30);
        Cea608Encoder enc(cfg);
        SubtitleList  subs;
        subs.append(Subtitle(tsFromMs(2000), tsFromMs(3000), "WORD",
                             SubtitleAnchor::BottomCenter));
        REQUIRE(enc.setSubtitles(subs).isOk());

        bool sawIndentPac = false;
        bool sawT2 = false;
        for (int64_t f = 0; f < 90; ++f) {
                Cea708Cdp::CcData t = oneTriple(enc, f);
                const uint8_t b1 = Cea608::stripParity(t.b1);
                const uint8_t b2 = Cea608::stripParity(t.b2);
                if (Cea608::isPac(b1, b2)) {
                        Cea608::PacAttr pac;
                        if (Cea608::decodePac(b1, b2, pac) && pac.row == 15
                            && pac.indentCol == 12) {
                                sawIndentPac = true;
                        }
                }
                if (Cea608::isTabOffset(b1, b2) && b2 == Cea608::TabOffsetT2) {
                        sawT2 = true;
                }
        }
        CHECK(sawIndentPac);
        CHECK(sawT2);
}

TEST_CASE("Cea608Encoder: BottomRight anchor with 6-char cue lands flush-right") {
        // "ABCDEF" → 6 chars; right-aligned → column 32-6 = 26.
        // PAC indent = 24, Tab Offset = T2.
        Cea608Encoder::Config cfg;
        cfg.frameRate = FrameRate(FrameRate::FPS_30);
        Cea608Encoder enc(cfg);
        SubtitleList  subs;
        subs.append(Subtitle(tsFromMs(2000), tsFromMs(3000), "ABCDEF",
                             SubtitleAnchor::BottomRight));
        REQUIRE(enc.setSubtitles(subs).isOk());

        bool sawIndentPac = false;
        bool sawT2 = false;
        for (int64_t f = 0; f < 90; ++f) {
                Cea708Cdp::CcData t = oneTriple(enc, f);
                const uint8_t b1 = Cea608::stripParity(t.b1);
                const uint8_t b2 = Cea608::stripParity(t.b2);
                if (Cea608::isPac(b1, b2)) {
                        Cea608::PacAttr pac;
                        if (Cea608::decodePac(b1, b2, pac) && pac.row == 15
                            && pac.indentCol == 24) {
                                sawIndentPac = true;
                        }
                }
                if (Cea608::isTabOffset(b1, b2) && b2 == Cea608::TabOffsetT2) {
                        sawT2 = true;
                }
        }
        CHECK(sawIndentPac);
        CHECK(sawT2);
}

TEST_CASE("Cea608Encoder + Decoder: BottomCenter anchor round-trips horizontal half") {
        Cea608Encoder::Config encCfg;
        encCfg.frameRate = FrameRate(FrameRate::FPS_30);
        Cea608Encoder enc(encCfg);
        SubtitleList  in;
        in.append(Subtitle(tsAt30fps(60), tsAt30fps(120), "WORD",
                           SubtitleAnchor::BottomCenter));
        REQUIRE(enc.setSubtitles(in).isOk());

        Cea608Decoder dec;
        for (int64_t f = 0; f < 140; ++f) {
                dec.pushFrame(FrameNumber(f), tsAt30fps(f), enc.nextFrame(FrameNumber(f)));
        }
        SubtitleList out = dec.finalize();
        REQUIRE(out.size() == 1);
        CHECK(out[0].text() == "WORD");
        CHECK(out[0].anchor() == SubtitleAnchor::BottomCenter);
}

TEST_CASE("Cea608Encoder + Decoder: BottomRight anchor round-trips horizontal half") {
        Cea608Encoder::Config encCfg;
        encCfg.frameRate = FrameRate(FrameRate::FPS_30);
        Cea608Encoder enc(encCfg);
        SubtitleList  in;
        in.append(Subtitle(tsAt30fps(60), tsAt30fps(120), "ABCDEF",
                           SubtitleAnchor::BottomRight));
        REQUIRE(enc.setSubtitles(in).isOk());

        Cea608Decoder dec;
        for (int64_t f = 0; f < 140; ++f) {
                dec.pushFrame(FrameNumber(f), tsAt30fps(f), enc.nextFrame(FrameNumber(f)));
        }
        SubtitleList out = dec.finalize();
        REQUIRE(out.size() == 1);
        CHECK(out[0].text() == "ABCDEF");
        CHECK(out[0].anchor() == SubtitleAnchor::BottomRight);
}

TEST_CASE("Cea608Encoder + Decoder: BottomLeft anchor round-trips horizontal half") {
        Cea608Encoder::Config encCfg;
        encCfg.frameRate = FrameRate(FrameRate::FPS_30);
        Cea608Encoder enc(encCfg);
        SubtitleList  in;
        in.append(Subtitle(tsAt30fps(60), tsAt30fps(120), "WORD",
                           SubtitleAnchor::BottomLeft));
        REQUIRE(enc.setSubtitles(in).isOk());

        Cea608Decoder dec;
        for (int64_t f = 0; f < 140; ++f) {
                dec.pushFrame(FrameNumber(f), tsAt30fps(f), enc.nextFrame(FrameNumber(f)));
        }
        SubtitleList out = dec.finalize();
        REQUIRE(out.size() == 1);
        CHECK(out[0].text() == "WORD");
        CHECK(out[0].anchor() == SubtitleAnchor::BottomLeft);
}

TEST_CASE("Cea608Encoder + Decoder: TopCenter and MiddleCenter anchors round-trip") {
        // Two cues spaced apart so each gets its own pre-roll window.
        // Each tests both vertical group (Top/Middle) + horizontal Center.
        Cea608Encoder::Config encCfg;
        encCfg.frameRate = FrameRate(FrameRate::FPS_30);
        Cea608Encoder enc(encCfg);
        SubtitleList  in;
        in.append(Subtitle(tsAt30fps(60), tsAt30fps(120), "TOPX", SubtitleAnchor::TopCenter));
        in.append(Subtitle(tsAt30fps(180), tsAt30fps(240), "MIDX", SubtitleAnchor::MiddleCenter));
        REQUIRE(enc.setSubtitles(in).isOk());

        Cea608Decoder dec;
        for (int64_t f = 0; f < 280; ++f) {
                dec.pushFrame(FrameNumber(f), tsAt30fps(f), enc.nextFrame(FrameNumber(f)));
        }
        SubtitleList out = dec.finalize();
        REQUIRE(out.size() == 2);
        CHECK(out[0].text() == "TOPX");
        CHECK(out[0].anchor() == SubtitleAnchor::TopCenter);
        CHECK(out[1].text() == "MIDX");
        CHECK(out[1].anchor() == SubtitleAnchor::MiddleCenter);
}

TEST_CASE("Cea608Encoder: coloured BottomCenter cue uses §B.4 PAC-indent + MR split (E4)") {
        // §B.4: when a row needs BOTH a non-zero indent AND a coloured
        // first span, the encoder splits across two control codes —
        // PAC carries the indent (white, no italic) and a MR carries
        // the colour.  Tab Offset spans (residual - 1) since the MR
        // cell itself consumes one display column.  Previously the
        // encoder collapsed this case to flush-left column 0 to
        // preserve colour; the audit (E4) restores §B.4 conformance.
        //
        // "REDX" is 4 chars centred → targetCol = (32-4)/2 = 14.
        // pacIndent = 12, residual = 2 → PAC(row 15, indent 12, white)
        // + TabOffset(residual - 1 = 1) + MidRow(Red, italic=false,
        // underline=false) + chars.
        Cea608Encoder::Config encCfg;
        encCfg.frameRate = FrameRate(FrameRate::FPS_30);
        Cea608Encoder enc(encCfg);

        SubtitleSpan::List spans;
        spans.pushToBack(SubtitleSpan("REDX", false, false, false, Color::Red));
        SubtitleList in;
        in.append(Subtitle(tsAt30fps(60), tsAt30fps(120), spans, SubtitleAnchor::BottomCenter,
                           Rect2Di32(), String(), Metadata()));
        REQUIRE(enc.setSubtitles(in).isOk());

        bool sawIndentPac = false;
        bool sawColorPac = false;
        bool sawT1 = false;
        bool sawRedMidRow = false;
        for (int64_t f = 0; f < 90; ++f) {
                Cea708Cdp::CcData t = oneTriple(enc, f);
                const uint8_t b1 = Cea608::stripParity(t.b1);
                const uint8_t b2 = Cea608::stripParity(t.b2);
                if (Cea608::isPac(b1, b2)) {
                        Cea608::PacAttr pac;
                        if (Cea608::decodePac(b1, b2, pac)) {
                                if (pac.color == Cea608::CaptionColor::Red) sawColorPac = true;
                                if (pac.indentCol == 12
                                    && pac.color == Cea608::CaptionColor::White) {
                                        sawIndentPac = true;
                                }
                        }
                }
                if (Cea608::isTabOffset(b1, b2) && b2 == Cea608::TabOffsetT1) sawT1 = true;
                if (Cea608::isMidRow(b1, b2)) {
                        Cea608::CaptionColor c = Cea608::CaptionColor::White;
                        bool italic = false;
                        bool underline = false;
                        if (Cea608::decodeMidRow(b1, b2, c, italic, underline)
                            && c == Cea608::CaptionColor::Red) {
                                sawRedMidRow = true;
                        }
                }
        }
        // The PAC carries indent + White (NOT the coloured PAC the
        // old encoder emitted).  Colour rides on the subsequent MR.
        CHECK(sawIndentPac);
        CHECK_FALSE(sawColorPac);
        CHECK(sawT1);
        CHECK(sawRedMidRow);
}
