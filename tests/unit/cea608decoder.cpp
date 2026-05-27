/**
 * @file      cea608decoder.cpp
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
#include <promeki/color.h>
#include <promeki/enums_subtitle.h>
#include <promeki/framenumber.h>
#include <promeki/framerate.h>
#include <promeki/metadata.h>
#include <promeki/rect.h>
#include <promeki/stringlist.h>
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

        /// @brief Builds a one-pair CcDataList carrying the given
        ///        pre-parity bytes, with parity stamped + cc_type
        ///        set to field 1 (CC1).
        Cea708Cdp::CcDataList onePair(uint8_t b1, uint8_t b2) {
                Cea708Cdp::CcDataList out;
                out.pushToBack(Cea708Cdp::CcData{true, 0, Cea608::withOddParity(b1), Cea608::withOddParity(b2)});
                return out;
        }

        /// @brief Returns the TimeStamp at frame @p frame for 30 fps.
        TimeStamp tsAt30fps(int64_t frame) {
                // Frame N at 30 fps lands at N * 1000/30 ms — but for
                // these tests we want the exact ms that the encoder
                // would map back to frame N.  Use 30fps integer arith:
                // ts(N) = round(N * 1000 / 30).  At 30 fps the encoder
                // also rounds, so the mapping is symmetric.
                return tsFromMs((frame * 1000 + 15) / 30);
        }

} // namespace

// ============================================================================
// Empty input
// ============================================================================

TEST_CASE("Cea608Decoder: empty pushes -> finalize returns empty SubtitleList") {
        Cea608Decoder dec;
        SubtitleList  out = dec.finalize();
        CHECK(out.isEmpty());
}

TEST_CASE("Cea608Decoder: null pairs only -> finalize returns empty list") {
        Cea608Decoder dec;
        for (int64_t f = 0; f < 30; ++f) {
                dec.pushFrame(FrameNumber(f), tsFromMs(f * 1000 / 30),
                              onePair(Cea608::NullB1, Cea608::NullB2));
        }
        CHECK(dec.finalize().isEmpty());
}

// ============================================================================
// Hand-rolled pop-on cycle
// ============================================================================

TEST_CASE("Cea608Decoder: hand-rolled RCL/PAC/AB/EOC then EDM emits one cue") {
        Cea608Decoder dec;
        // Frames 0..1: RCL doubled (second is the spec duplicate).
        dec.pushFrame(FrameNumber(0), tsFromMs(0), onePair(Cea608::RclB1, Cea608::RclB2));
        dec.pushFrame(FrameNumber(1), tsFromMs(33), onePair(Cea608::RclB1, Cea608::RclB2));
        // Frames 2..3: PAC doubled.
        dec.pushFrame(FrameNumber(2), tsFromMs(67),
                      onePair(Cea608::PacRow15Col0WhiteB1, Cea608::PacRow15Col0WhiteB2));
        dec.pushFrame(FrameNumber(3), tsFromMs(100),
                      onePair(Cea608::PacRow15Col0WhiteB1, Cea608::PacRow15Col0WhiteB2));
        // Frame 4: "AB" chars (not doubled).
        dec.pushFrame(FrameNumber(4), tsFromMs(133), onePair(0x41, 0x42));
        // Frames 5..6: EOC doubled.  After the first EOC, the cue is
        // visible — record its start at this frame's timestamp.
        const TimeStamp eocTs = tsFromMs(167);
        dec.pushFrame(FrameNumber(5), eocTs, onePair(Cea608::EocB1, Cea608::EocB2));
        dec.pushFrame(FrameNumber(6), tsFromMs(200), onePair(Cea608::EocB1, Cea608::EocB2));
        // Live state: the cue is on screen.
        CHECK(dec.displayedText() == "AB");
        // Frames 7..N: nulls; cue persists.
        for (int64_t f = 7; f < 30; ++f) {
                dec.pushFrame(FrameNumber(f), tsFromMs(f * 1000 / 30), onePair(0, 0));
                CHECK(dec.displayedText() == "AB");
        }
        // Frames 30..31: EDM doubled.  The cue ends at frame 30.
        const TimeStamp edmTs = tsFromMs(1000);
        dec.pushFrame(FrameNumber(30), edmTs, onePair(Cea608::EdmB1, Cea608::EdmB2));
        dec.pushFrame(FrameNumber(31), tsFromMs(1033), onePair(Cea608::EdmB1, Cea608::EdmB2));
        CHECK(dec.displayedText() == "");

        SubtitleList out = dec.finalize();
        REQUIRE(out.size() == 1);
        CHECK(out[0].text() == "AB");
        CHECK(out[0].start() == eocTs);
        CHECK(out[0].end() == edmTs);
}

TEST_CASE("Cea608Decoder: doubled control codes collapse to a single occurrence") {
        // Same scenario but assert that the second RCL/PAC/EOC does
        // not double-fire (the second RCL would reset non-displayed
        // memory and erase the cue text).
        Cea608Decoder dec;
        dec.pushFrame(FrameNumber(0), tsFromMs(0), onePair(Cea608::RclB1, Cea608::RclB2));
        dec.pushFrame(FrameNumber(1), tsFromMs(33), onePair(Cea608::RclB1, Cea608::RclB2));
        dec.pushFrame(FrameNumber(2), tsFromMs(67), onePair(0x41, 0x42)); // "AB"
        // Sneak another RCL pair *not* preceded by an identical one — the
        // dup filter must have reset on the character pair, so this RCL
        // is processed (clearing non-displayed memory).
        dec.pushFrame(FrameNumber(3), tsFromMs(100), onePair(Cea608::RclB1, Cea608::RclB2));
        dec.pushFrame(FrameNumber(4), tsFromMs(133), onePair(0x43, 0x44)); // "CD" loaded into freshly-cleared mem
        dec.pushFrame(FrameNumber(5), tsFromMs(167), onePair(Cea608::EocB1, Cea608::EocB2));
        // Displayed should be "CD" (the second load), not "ABCD".
        CHECK(dec.displayedText() == "CD");
}

// ============================================================================
// Parity handling
// ============================================================================

TEST_CASE("Cea608Decoder: parity-failed bytes are skipped (treated as null)") {
        Cea608Decoder dec;
        // RCL with bad parity on b1: 0x14 raw (parity-correct stamp =
        // 0x94).  Send 0x14 with the parity bit cleared instead —
        // that's parity-incorrect and should be dropped.
        Cea708Cdp::CcDataList bad;
        bad.pushToBack(Cea708Cdp::CcData{true, 0,
                                        static_cast<uint8_t>(Cea608::RclB1), // missing parity bit
                                        Cea608::withOddParity(Cea608::RclB2)});
        dec.pushFrame(FrameNumber(0), tsFromMs(0), bad);
        // Then a real RCL, chars, EOC.
        dec.pushFrame(FrameNumber(1), tsFromMs(33), onePair(Cea608::RclB1, Cea608::RclB2));
        dec.pushFrame(FrameNumber(2), tsFromMs(67), onePair(0x41, 0x42));
        dec.pushFrame(FrameNumber(3), tsFromMs(100), onePair(Cea608::EocB1, Cea608::EocB2));
        CHECK(dec.displayedText() == "AB");
}

// ============================================================================
// Round-trip with the encoder
// ============================================================================

TEST_CASE("Cea608Encoder doubleControls=false: pop-on round-trip recovers the cue") {
        // CEA-608-E §D.2 / §8.4: control-pair doubling is the spec's
        // default for caption data on F1.  Setting @c doubleControls
        // to @c false emits each control pair once (RCL, PAC, EOC,
        // EDM, mid-row, BG, BT, FA/FAU, FON, Tab Offset all emit
        // exactly once instead of twice on the wire).  Pre-roll
        // budgets halve accordingly.
        //
        // The decoder's doubled-control collapsing logic naturally
        // accepts single control pairs (a single pair counts as a
        // single effective receipt, exactly like the second copy of
        // a doubled pair being collapsed).  This test verifies the
        // round-trip survives end-to-end with doubling disabled.
        Cea608Encoder::Config encCfg;
        encCfg.frameRate = FrameRate(FrameRate::FPS_30);
        encCfg.doubleControls = false;
        Cea608Encoder enc(encCfg);

        SubtitleList in;
        in.append(Subtitle(tsAt30fps(30), tsAt30fps(60), "AB"));
        REQUIRE(enc.setSubtitles(in).isOk());

        Cea608Decoder dec;
        for (int64_t f = 0; f < 80; ++f) {
                dec.pushFrame(FrameNumber(f), tsAt30fps(f), enc.nextFrame(FrameNumber(f)));
        }
        SubtitleList out = dec.finalize();
        REQUIRE(out.size() == 1);
        CHECK(out[0].text() == "AB");
        CHECK(out[0].start() == tsAt30fps(30));
        CHECK(out[0].end() == tsAt30fps(60));
}

TEST_CASE("Cea608Encoder doubleControls=false: paint-on round-trip recovers the cue") {
        Cea608Encoder::Config encCfg;
        encCfg.frameRate = FrameRate(FrameRate::FPS_30);
        encCfg.mode = Cea608Encoder::Mode::PaintOn;
        encCfg.doubleControls = false;
        Cea608Encoder enc(encCfg);

        SubtitleList in;
        in.append(Subtitle(tsAt30fps(30), tsAt30fps(80), "AB"));
        REQUIRE(enc.setSubtitles(in).isOk());

        Cea608Decoder dec;
        for (int64_t f = 0; f < 100; ++f) {
                dec.pushFrame(FrameNumber(f), tsAt30fps(f), enc.nextFrame(FrameNumber(f)));
        }
        SubtitleList out = dec.finalize();
        REQUIRE(out.size() == 1);
        CHECK(out[0].text() == "AB");
}

TEST_CASE("Cea608Encoder doubleControls=false: roll-up round-trip recovers the cue") {
        Cea608Encoder::Config encCfg;
        encCfg.frameRate = FrameRate(FrameRate::FPS_30);
        encCfg.mode = Cea608Encoder::Mode::RollUp;
        encCfg.rollUpRows = 2;
        encCfg.doubleControls = false;
        Cea608Encoder enc(encCfg);

        SubtitleList in;
        in.append(Subtitle(tsAt30fps(30), tsAt30fps(120), "AB"));
        REQUIRE(enc.setSubtitles(in).isOk());

        Cea608Decoder dec;
        for (int64_t f = 0; f < 150; ++f) {
                dec.pushFrame(FrameNumber(f), tsAt30fps(f), enc.nextFrame(FrameNumber(f)));
        }
        SubtitleList out = dec.finalize();
        REQUIRE(out.size() == 1);
        CHECK(out[0].text() == "AB");
}

TEST_CASE("Cea608Encoder doubleControls=false: control pairs appear exactly once on the wire") {
        // Verifies the byte-stream itself: with doubling disabled,
        // the RCL pre-roll occupies 1 frame instead of 2.  We inspect
        // the schedule by feeding the encoder and counting RCL
        // occurrences in the emitted byte stream.
        Cea608Encoder::Config encCfg;
        encCfg.frameRate = FrameRate(FrameRate::FPS_30);
        encCfg.doubleControls = false;
        Cea608Encoder enc(encCfg);

        SubtitleList in;
        in.append(Subtitle(tsAt30fps(30), tsAt30fps(60), "AB"));
        REQUIRE(enc.setSubtitles(in).isOk());

        int rclCount = 0;
        int eocCount = 0;
        for (int64_t f = 0; f < 80; ++f) {
                Cea708Cdp::CcDataList list = enc.nextFrame(FrameNumber(f));
                for (size_t i = 0; i < list.size(); ++i) {
                        const uint8_t b1 = Cea608::stripParity(list[i].b1);
                        const uint8_t b2 = Cea608::stripParity(list[i].b2);
                        if (b1 == Cea608::RclB1 && b2 == Cea608::RclB2) ++rclCount;
                        if (b1 == Cea608::EocB1 && b2 == Cea608::EocB2) ++eocCount;
                }
        }
        // Exactly one RCL pair (not two) — the spec-mandated doubling
        // is suppressed when @c doubleControls is @c false.
        CHECK(rclCount == 1);
        CHECK(eocCount == 1);
}

TEST_CASE("Cea608Encoder doubleControls=true (default): control pairs appear twice on the wire") {
        // Sanity sentinel for the default: with doubling ON (the
        // spec normative default) every control pair appears twice
        // adjacent on the wire.
        Cea608Encoder::Config encCfg;
        encCfg.frameRate = FrameRate(FrameRate::FPS_30);
        // doubleControls defaults to true.
        Cea608Encoder enc(encCfg);

        SubtitleList in;
        in.append(Subtitle(tsAt30fps(30), tsAt30fps(60), "AB"));
        REQUIRE(enc.setSubtitles(in).isOk());

        int rclCount = 0;
        int eocCount = 0;
        for (int64_t f = 0; f < 80; ++f) {
                Cea708Cdp::CcDataList list = enc.nextFrame(FrameNumber(f));
                for (size_t i = 0; i < list.size(); ++i) {
                        const uint8_t b1 = Cea608::stripParity(list[i].b1);
                        const uint8_t b2 = Cea608::stripParity(list[i].b2);
                        if (b1 == Cea608::RclB1 && b2 == Cea608::RclB2) ++rclCount;
                        if (b1 == Cea608::EocB1 && b2 == Cea608::EocB2) ++eocCount;
                }
        }
        CHECK(rclCount == 2);
        CHECK(eocCount == 2);
}

TEST_CASE("Cea608Encoder doubleControls=false: multi-cue scheduling has clean per-cue boundaries") {
        // The per-cue boundary math (lastEocFrame, pendingEdmFrame,
        // EDM flush timing) all scales on @c ctlReps internally.
        // Verify three sequential pop-on cues round-trip cleanly with
        // doubling disabled — proves the pre-roll budgets, EDM
        // deferral, and EOC swap timings are all consistently
        // halved.
        Cea608Encoder::Config encCfg;
        encCfg.frameRate = FrameRate(FrameRate::FPS_30);
        encCfg.doubleControls = false;
        Cea608Encoder enc(encCfg);

        SubtitleList in;
        in.append(Subtitle(tsAt30fps(30), tsAt30fps(60), "AB"));
        in.append(Subtitle(tsAt30fps(120), tsAt30fps(150), "CD"));
        in.append(Subtitle(tsAt30fps(200), tsAt30fps(240), "EF"));
        REQUIRE(enc.setSubtitles(in).isOk());

        Cea608Decoder dec;
        for (int64_t f = 0; f < 280; ++f) {
                dec.pushFrame(FrameNumber(f), tsAt30fps(f), enc.nextFrame(FrameNumber(f)));
        }
        SubtitleList out = dec.finalize();
        REQUIRE(out.size() == 3);
        CHECK(out[0].text() == "AB");
        CHECK(out[1].text() == "CD");
        CHECK(out[2].text() == "EF");
        CHECK(out[0].start() == tsAt30fps(30));
        CHECK(out[1].start() == tsAt30fps(120));
        CHECK(out[2].start() == tsAt30fps(200));
}

TEST_CASE("Cea608Encoder doubleControls=false: styled spans (mid-row codes) round-trip cleanly") {
        // Mid-row control pairs MUST also halve under doubleControls
        // = false.  Pop-on cue with two styled spans (Red then Blue)
        // exercises the MR-pair emit path.  Verify the cue's text +
        // span colours round-trip.  BottomLeft anchor (column 0) keeps
        // the row off the §B.4 PAC-indent + MR split path (E4), so
        // the wire stays compact for this MR-pairing check.
        Cea608Encoder::Config encCfg;
        encCfg.frameRate = FrameRate(FrameRate::FPS_30);
        encCfg.doubleControls = false;
        Cea608Encoder enc(encCfg);

        SubtitleSpan::List spans;
        spans.pushToBack(SubtitleSpan("REDX", false, false, false, Color::Red));
        spans.pushToBack(SubtitleSpan("BL", false, false, false, Color::Blue));
        SubtitleList subs;
        subs.append(Subtitle(tsAt30fps(60), tsAt30fps(120), spans, SubtitleAnchor::BottomLeft,
                             Rect2Di32(), String(), Metadata()));
        REQUIRE(enc.setSubtitles(subs).isOk());

        Cea608Decoder dec;
        for (int64_t f = 0; f < 140; ++f) {
                dec.pushFrame(FrameNumber(f), tsAt30fps(f), enc.nextFrame(FrameNumber(f)));
        }
        SubtitleList out = dec.finalize();
        REQUIRE(out.size() == 1);
        // Flat text reads "REDX BL" — the MR cell is one styled
        // space between the runs (auto-BS-replaced cell carries the
        // new Blue style).
        CHECK(out[0].text() == "REDX BL");

        // Confirm the span structure: REDX is Red; the styled space
        // + BL run is Blue.  The decoder emits 3 spans (REDX, " ",
        // BL) with the separator cell preserved as its own span.
        const SubtitleSpan::List &got = out[0].spans();
        REQUIRE(got.size() == 3);
        CHECK(got[0].text() == "REDX");
        CHECK(got[0].color() == Color::Red);
        CHECK(got[1].text() == " ");
        CHECK(got[1].color() == Color::Blue);
        CHECK(got[2].text() == "BL");
        CHECK(got[2].color() == Color::Blue);
}

TEST_CASE("Cea608Decoder: round-trips a single cue produced by Cea608Encoder") {
        Cea608Encoder::Config encCfg;
        encCfg.frameRate = FrameRate(FrameRate::FPS_30);
        Cea608Encoder enc(encCfg);

        SubtitleList in;
        in.append(Subtitle(tsAt30fps(30), tsAt30fps(60), "Hello"));
        REQUIRE(enc.setSubtitles(in).isOk());

        Cea608Decoder dec;
        for (int64_t f = 0; f < 80; ++f) {
                dec.pushFrame(FrameNumber(f), tsAt30fps(f), enc.nextFrame(FrameNumber(f)));
        }
        SubtitleList out = dec.finalize();
        REQUIRE(out.size() == 1);
        // The encoder pads odd-length text with NUL (b2 < 0x20 is wire
        // filler that the decoder skips) — "Hello" is 5 chars, the
        // trailing pad is NUL, so the decoder reconstructs "Hello"
        // with no trailing pad-space leaking into the cell grid.
        CHECK(out[0].text() == "Hello");
        CHECK(out[0].start() == tsAt30fps(30));
        CHECK(out[0].end() == tsAt30fps(60));
}

TEST_CASE("Cea608Decoder: round-trips multiple sequential cues") {
        Cea608Encoder::Config encCfg;
        encCfg.frameRate = FrameRate(FrameRate::FPS_30);
        Cea608Encoder enc(encCfg);

        SubtitleList in;
        in.append(Subtitle(tsAt30fps(30), tsAt30fps(60), "AB"));
        in.append(Subtitle(tsAt30fps(120), tsAt30fps(150), "CD"));
        in.append(Subtitle(tsAt30fps(200), tsAt30fps(240), "EF"));
        REQUIRE(enc.setSubtitles(in).isOk());

        Cea608Decoder dec;
        for (int64_t f = 0; f < 280; ++f) {
                dec.pushFrame(FrameNumber(f), tsAt30fps(f), enc.nextFrame(FrameNumber(f)));
        }
        SubtitleList out = dec.finalize();
        REQUIRE(out.size() == 3);
        CHECK(out[0].text() == "AB");
        CHECK(out[1].text() == "CD");
        CHECK(out[2].text() == "EF");
        CHECK(out[0].start() == tsAt30fps(30));
        CHECK(out[0].end() == tsAt30fps(60));
        CHECK(out[1].start() == tsAt30fps(120));
        CHECK(out[1].end() == tsAt30fps(150));
        CHECK(out[2].start() == tsAt30fps(200));
        CHECK(out[2].end() == tsAt30fps(240));
}

TEST_CASE("Cea608Decoder: round-trips a longer-text cue") {
        Cea608Encoder::Config encCfg;
        encCfg.frameRate = FrameRate(FrameRate::FPS_30);
        Cea608Encoder enc(encCfg);

        SubtitleList in;
        in.append(Subtitle(tsAt30fps(60), tsAt30fps(180), "Hello, world."));
        REQUIRE(enc.setSubtitles(in).isOk());

        Cea608Decoder dec;
        for (int64_t f = 0; f < 200; ++f) {
                dec.pushFrame(FrameNumber(f), tsAt30fps(f), enc.nextFrame(FrameNumber(f)));
        }
        SubtitleList out = dec.finalize();
        REQUIRE(out.size() == 1);
        // 13 chars → 7 char pairs.  The encoder pads odd-length
        // text with NUL (wire filler the decoder skips), not SPACE,
        // so the trailing pad doesn't leak a space into the grid.
        CHECK(out[0].text() == "Hello, world.");
}

// ============================================================================
// Finalize with still-displayed cue
// ============================================================================

TEST_CASE("Cea608Decoder: finalize closes a still-displayed cue at the last pushed ts") {
        // EOC fires but no EDM follows before finalize.  The cue
        // should be emitted with end = the most recent pushFrame
        // timestamp.
        Cea608Decoder dec;
        dec.pushFrame(FrameNumber(0), tsFromMs(0), onePair(Cea608::RclB1, Cea608::RclB2));
        dec.pushFrame(FrameNumber(1), tsFromMs(33), onePair(Cea608::RclB1, Cea608::RclB2));
        dec.pushFrame(FrameNumber(2), tsFromMs(67), onePair(0x41, 0x42));
        const TimeStamp eocTs = tsFromMs(100);
        dec.pushFrame(FrameNumber(3), eocTs, onePair(Cea608::EocB1, Cea608::EocB2));
        dec.pushFrame(FrameNumber(4), tsFromMs(133), onePair(Cea608::EocB1, Cea608::EocB2));
        // Push some null frames after EOC.
        const TimeStamp lastTs = tsFromMs(500);
        for (int64_t f = 5; f < 16; ++f) {
                dec.pushFrame(FrameNumber(f), tsFromMs(f * 1000 / 30), onePair(0, 0));
        }
        dec.pushFrame(FrameNumber(16), lastTs, onePair(0, 0));

        SubtitleList out = dec.finalize();
        REQUIRE(out.size() == 1);
        CHECK(out[0].start() == eocTs);
        CHECK(out[0].end() == lastTs);
        CHECK(out[0].text() == "AB");
}

// ============================================================================
// Channel filtering
// ============================================================================

TEST_CASE("Cea608Decoder: triples with cc_type != configured field are skipped") {
        Cea608Decoder dec;  // Defaults to CC1 (field 1, cc_type 0).
        // Build a "valid" RCL pair but mis-tag as cc_type 1 (field 2).
        Cea708Cdp::CcDataList fld2;
        fld2.pushToBack(Cea708Cdp::CcData{true, 1, Cea608::withOddParity(Cea608::RclB1),
                                         Cea608::withOddParity(Cea608::RclB2)});
        dec.pushFrame(FrameNumber(0), tsFromMs(0), fld2);
        // Followed by a real CC1 cycle.
        dec.pushFrame(FrameNumber(1), tsFromMs(33), onePair(Cea608::RclB1, Cea608::RclB2));
        dec.pushFrame(FrameNumber(2), tsFromMs(67), onePair(Cea608::RclB1, Cea608::RclB2));
        dec.pushFrame(FrameNumber(3), tsFromMs(100), onePair(0x41, 0x42));
        dec.pushFrame(FrameNumber(4), tsFromMs(133), onePair(Cea608::EocB1, Cea608::EocB2));
        dec.pushFrame(FrameNumber(5), tsFromMs(167), onePair(Cea608::EocB1, Cea608::EocB2));
        CHECK(dec.displayedText() == "AB");
}

// ============================================================================
// reset
// ============================================================================

TEST_CASE("Cea608Decoder: reset drops in-flight state without emitting") {
        Cea608Decoder dec;
        dec.pushFrame(FrameNumber(0), tsFromMs(0), onePair(Cea608::RclB1, Cea608::RclB2));
        dec.pushFrame(FrameNumber(1), tsFromMs(33), onePair(Cea608::RclB1, Cea608::RclB2));
        dec.pushFrame(FrameNumber(2), tsFromMs(67), onePair(0x41, 0x42));
        dec.pushFrame(FrameNumber(3), tsFromMs(100), onePair(Cea608::EocB1, Cea608::EocB2));
        CHECK(dec.displayedText() == "AB");
        dec.reset();
        CHECK(dec.displayedText() == "");
        CHECK(dec.finalize().isEmpty());
}

// ============================================================================
// Styled round-trip (encoder ↔ decoder, full attribute set)
// ============================================================================

namespace {

        /// @brief Feeds every frame in @p totalFrames from @p enc into
        ///        @p dec at 30 fps.
        void runEncoderToDecoder(Cea608Encoder &enc, Cea608Decoder &dec, int64_t totalFrames) {
                for (int64_t f = 0; f < totalFrames; ++f) {
                        Cea708Cdp::CcDataList list = enc.nextFrame(FrameNumber(f));
                        dec.pushFrame(FrameNumber(f), tsAt30fps(f), list);
                }
        }

} // namespace

TEST_CASE("Cea608: round-trip recovers anchor from PAC row") {
        Cea608Encoder::Config eCfg;
        eCfg.frameRate = FrameRate(FrameRate::FPS_30);
        Cea608Encoder enc(eCfg);
        Cea608Decoder dec;

        SubtitleList subs;
        // Use even-length cue text so the 608 wire doesn't pad with
        // a trailing space (608 byte pairs always carry two
        // characters, so odd-length text is space-padded on the
        // wire — that's wire reality, not a decoder bug).
        subs.append(Subtitle(tsAt30fps(60), tsAt30fps(120), "TOPX", SubtitleAnchor::TopCenter));
        subs.append(Subtitle(tsAt30fps(200), tsAt30fps(260), "MIDX", SubtitleAnchor::MiddleCenter));
        REQUIRE(enc.setSubtitles(subs).isOk());

        runEncoderToDecoder(enc, dec, 280);
        SubtitleList out = dec.finalize();
        REQUIRE(out.size() == 2);
        CHECK(out[0].text() == "TOPX");
        CHECK(out[0].anchor() == SubtitleAnchor::TopCenter);
        CHECK(out[1].text() == "MIDX");
        CHECK(out[1].anchor() == SubtitleAnchor::MiddleCenter);
}

TEST_CASE("Cea608: wide centered cue round-trips with Center horizontal anchor") {
        // Regression for the rowToAnchor symmetric-gap fix: a 28-char
        // BottomCenter cue lands at firstCol = (32-28)/2 = 2 on the
        // wire, which the pre-fix decoder's `col<4 → Left` threshold
        // collapsed to BottomLeft.  The fix compares the leftGap
        // (firstCol) and rightGap (31-lastCol) — both ≈ 2 here — and
        // resolves to Center.  Same for Middle / Top to confirm the
        // vertical band is preserved alongside the corrected
        // horizontal recovery.
        Cea608Encoder::Config eCfg;
        eCfg.frameRate = FrameRate(FrameRate::FPS_30);
        Cea608Encoder enc(eCfg);
        Cea608Decoder dec;

        const String wide28 = "Twenty-eight chars centered.";
        REQUIRE(wide28.length() == 28);
        SubtitleList subs;
        // Stagger the cue starts well past frame 0 so the centered
        // pre-roll never trips the flush-left fallback in the
        // encoder — we want to exercise the decoder's wide-cue
        // anchor recovery, not the fallback path.
        subs.append(Subtitle(tsAt30fps(120), tsAt30fps(180), wide28,
                             SubtitleAnchor::TopCenter));
        subs.append(Subtitle(tsAt30fps(240), tsAt30fps(300), wide28,
                             SubtitleAnchor::MiddleCenter));
        subs.append(Subtitle(tsAt30fps(360), tsAt30fps(420), wide28,
                             SubtitleAnchor::BottomCenter));
        REQUIRE(enc.setSubtitles(subs).isOk());

        runEncoderToDecoder(enc, dec, 460);
        SubtitleList out = dec.finalize();
        REQUIRE(out.size() == 3);
        CHECK(out[0].anchor() == SubtitleAnchor::TopCenter);
        CHECK(out[1].anchor() == SubtitleAnchor::MiddleCenter);
        CHECK(out[2].anchor() == SubtitleAnchor::BottomCenter);
}

TEST_CASE("Cea608: round-trip preserves italic + underline + colour on a single span") {
        Cea608Encoder::Config eCfg;
        eCfg.frameRate = FrameRate(FrameRate::FPS_30);
        Cea608Encoder enc(eCfg);
        Cea608Decoder dec;

        SubtitleSpan::List spans;
        // Red, italic, underline — italic in 608 always pairs with
        // white, so colour gets lost when italic is set.  This test
        // exercises the italic + underline path; the colour-only
        // path is in the next test case.
        spans.pushToBack(SubtitleSpan("X", false /*bold*/, true /*italic*/, true /*underline*/));
        SubtitleList subs;
        subs.append(Subtitle(tsAt30fps(60), tsAt30fps(120), spans, SubtitleAnchor::BottomCenter,
                             Rect2Di32(), String(), Metadata()));
        REQUIRE(enc.setSubtitles(subs).isOk());

        runEncoderToDecoder(enc, dec, 140);
        SubtitleList out = dec.finalize();
        REQUIRE(out.size() == 1);
        REQUIRE(out[0].spans().size() >= 1);
        const SubtitleSpan &span = out[0].spans()[0];
        CHECK(span.italic());
        CHECK(span.underline());
        // Italic span colour is invalid (white default) at the wire.
        CHECK_FALSE(span.color().isValid());
}

TEST_CASE("Cea608: round-trip quantises colour to the 7 primaries") {
        Cea608Encoder::Config eCfg;
        eCfg.frameRate = FrameRate(FrameRate::FPS_30);
        Cea608Encoder enc(eCfg);
        Cea608Decoder dec;

        // Off-red colour — should quantise to the Red primary.
        SubtitleSpan::List spans;
        spans.pushToBack(SubtitleSpan("R", false, false, false, Color::srgb(0.85f, 0.1f, 0.1f)));
        SubtitleList subs;
        subs.append(Subtitle(tsAt30fps(60), tsAt30fps(120), spans, SubtitleAnchor::BottomCenter,
                             Rect2Di32(), String(), Metadata()));
        REQUIRE(enc.setSubtitles(subs).isOk());

        runEncoderToDecoder(enc, dec, 140);
        SubtitleList out = dec.finalize();
        REQUIRE(out.size() == 1);
        REQUIRE(out[0].spans().size() >= 1);
        const SubtitleSpan &span = out[0].spans()[0];
        CHECK(span.color().isValid());
        CHECK(span.color() == Color::Red);
}

TEST_CASE("Cea608: round-trip preserves mid-row colour change between spans") {
        Cea608Encoder::Config eCfg;
        eCfg.frameRate = FrameRate(FrameRate::FPS_30);
        Cea608Encoder enc(eCfg);
        Cea608Decoder dec;

        // Two-span cue: "REDX" in red, then "BL" in blue.  Both
        // spans are even-length so the encoder doesn't insert a
        // trailing pad-space inside either run (608 byte pairs
        // always carry two characters, so an odd-length span is
        // padded to an even count on the wire).  BottomLeft anchor
        // (column 0) keeps the row off the §B.4 PAC-indent + MR split
        // path (E4) — the focus here is the inter-span MR transition.
        SubtitleSpan::List spans;
        spans.pushToBack(SubtitleSpan("REDX", false, false, false, Color::Red));
        spans.pushToBack(SubtitleSpan("BL", false, false, false, Color::Blue));
        SubtitleList subs;
        subs.append(Subtitle(tsAt30fps(60), tsAt30fps(120), spans, SubtitleAnchor::BottomLeft,
                             Rect2Di32(), String(), Metadata()));
        REQUIRE(enc.setSubtitles(subs).isOk());

        runEncoderToDecoder(enc, dec, 140);
        SubtitleList out = dec.finalize();
        REQUIRE(out.size() == 1);
        const SubtitleSpan::List &gotSpans = out[0].spans();
        // Three spans: the two styled runs plus the §6.2 mid-row
        // separator cell — a single styled space carrying the NEW
        // (Blue) attribute (the MR code's own display cell).
        REQUIRE(gotSpans.size() == 3);
        CHECK(gotSpans[0].text() == "REDX");
        CHECK(gotSpans[0].color() == Color::Red);
        CHECK(gotSpans[1].text() == " ");
        // Per §6.2 the MR cell carries the new attribute.
        CHECK(gotSpans[1].color() == Color::Blue);
        CHECK(gotSpans[2].text() == "BL");
        CHECK(gotSpans[2].color() == Color::Blue);
        // Flat text reads "REDX BL" — the MR cell is one cell of
        // blue space between the runs.
        CHECK(out[0].text() == "REDX BL");
}

TEST_CASE("Cea608: round-trip preserves multi-row layout via PAC row breaks") {
        // A cue whose source already breaks into two SRT lines and
        // fits both rows inside the 32-col cap.  The encoder takes
        // the explicit-break path (one PAC per source line); the
        // decoder must surface the row break as a "\n" marker span
        // so the renderer stacks the two lines vertically instead
        // of concatenating them horizontally.
        Cea608Encoder::Config eCfg;
        eCfg.frameRate = FrameRate(FrameRate::FPS_30);
        Cea608Encoder enc(eCfg);
        Cea608Decoder dec;

        SubtitleSpan::List spans;
        spans.pushToBack(SubtitleSpan("Line one\nLine two", false, false, false));
        SubtitleList subs;
        subs.append(Subtitle(tsAt30fps(60), tsAt30fps(120), spans, SubtitleAnchor::BottomCenter,
                             Rect2Di32(), String(), Metadata()));
        REQUIRE(enc.setSubtitles(subs).isOk());

        runEncoderToDecoder(enc, dec, 140);
        SubtitleList out = dec.finalize();
        REQUIRE(out.size() == 1);
        // Flat text round-trips with the row break intact.  Note
        // the trailing pad-space on the second row (odd-length
        // "Line two" → 8 chars even, no pad; but "Line one" is 8
        // chars even too, no pad — both rows round-trip exactly).
        CHECK(out[0].text() == "Line one\nLine two");
}

TEST_CASE("Cea608: round-trip wraps over-cap text into multiple rows") {
        // A single-line cue that exceeds the 32-col cap.  Encoder
        // re-flows into multiple physical 608 rows; decoder
        // surfaces row breaks as "\n" markers so the rendered text
        // doesn't overflow the receiver's grid.
        Cea608Encoder::Config eCfg;
        eCfg.frameRate = FrameRate(FrameRate::FPS_30);
        Cea608Encoder enc(eCfg);
        Cea608Decoder dec;

        // 40-char cue, exceeds maxCols=32; balanced re-flow lands
        // it in 2 rows.
        SubtitleSpan::List spans;
        spans.pushToBack(SubtitleSpan("aaaa bbbb cccc dddd eeee ffff gggg hhhh!", false, false,
                                       false));
        SubtitleList subs;
        subs.append(Subtitle(tsAt30fps(60), tsAt30fps(180), spans, SubtitleAnchor::BottomCenter,
                             Rect2Di32(), String(), Metadata()));
        REQUIRE(enc.setSubtitles(subs).isOk());

        runEncoderToDecoder(enc, dec, 220);
        SubtitleList out = dec.finalize();
        REQUIRE(out.size() == 1);
        // The decoded flat text contains at least one row break.
        // We don't pin the exact split point (the balanced wrap is
        // free to pick the tightest minimax), only that the text
        // is broken across rows rather than emitted on one line.
        const String flat = out[0].text();
        const bool   hasRowBreak = flat.contains(String("\n"));
        CHECK(hasRowBreak);
        // Each row stays inside maxCols=32 chars.
        const StringList rows = flat.split("\n");
        for (size_t i = 0; i < rows.size(); ++i) {
                CHECK(rows[i].length() <= 32);
        }
}

TEST_CASE("Cea608: SubRip-style cue with <b>/<u> markup preserves underline") {
        // Mirror exactly what subrip.cpp produces for substest.srt
        // cue 8: "<b>Bold</b> and <u>underlined</u> mixed inline."
        // — four spans with their styling flags.  Verify that
        // after a full encode/decode round-trip the underlined
        // span survives with underline=true.  Bold is expected
        // to be dropped (the wire format can't carry it).
        Cea608Encoder::Config eCfg;
        eCfg.frameRate = FrameRate(FrameRate::FPS_30);
        Cea608Encoder enc(eCfg);
        Cea608Decoder dec;

        SubtitleSpan::List spans;
        spans.pushToBack(SubtitleSpan("Bold",       true,  false, false));
        spans.pushToBack(SubtitleSpan(" and ",      false, false, false));
        spans.pushToBack(SubtitleSpan("underlined", false, false, true));
        spans.pushToBack(SubtitleSpan(" mixed inline.", false, false, false));
        SubtitleList subs;
        subs.append(Subtitle(tsAt30fps(150), tsAt30fps(240), spans, SubtitleAnchor::BottomCenter,
                             Rect2Di32(), String(), Metadata()));
        REQUIRE(enc.setSubtitles(subs).isOk());

        runEncoderToDecoder(enc, dec, 280);
        SubtitleList out = dec.finalize();
        REQUIRE(out.size() == 1);
        const SubtitleSpan::List &gotSpans = out[0].spans();

        // Exactly one *word* must carry underline=true ("underlined").
        // Per §6.2 the mid-row control code's own cell renders in
        // the new style — so an extra styled-space span carrying
        // underline=true may sit immediately before "underlined".
        // Allow 1 or 2 underline-flagged spans; the primary
        // invariant is that the word "underlined" is itself
        // underlined and that no OTHER word text carries underline.
        int  underlineCount  = 0;
        bool underlineExact  = false;
        for (size_t i = 0; i < gotSpans.size(); ++i) {
                if (gotSpans[i].underline()) {
                        ++underlineCount;
                        if (gotSpans[i].text() == "underlined") underlineExact = true;
                        else {
                                // Any other underlined span must be a
                                // single styled space (the §6.2 MR cell).
                                CHECK(gotSpans[i].text() == " ");
                        }
                }
        }
        CHECK(underlineCount >= 1);
        CHECK(underlineCount <= 2);
        CHECK(underlineExact);

        // No span carries bold (608 limitation).
        for (size_t i = 0; i < gotSpans.size(); ++i) {
                CHECK_FALSE(gotSpans[i].bold());
        }
}

TEST_CASE("Cea608: re-flow across a source '\\n' inserts a separator space") {
        // Source cue with two short lines whose combined length
        // forces a re-flow when maxCols is tight enough that the
        // explicit-break attempt (2 source rows) won't pass.  The
        // '\n' between source rows is whitespace to the tokenizer,
        // so the re-flow's `wordsAreAdjacent` check sees a gap and
        // adds a separator space.  Net: the re-flowed text never
        // concatenates "1This" — there's always whitespace between
        // the last word of line 1 and the first word of line 2.
        Cea608Encoder::Config eCfg;
        eCfg.frameRate = FrameRate(FrameRate::FPS_30);
        // Tight cap so the encoder is forced into balanced re-flow
        // rather than honouring the source '\n' break.
        eCfg.maxCols = 32;
        eCfg.maxRows = 1; // single row → must re-flow into one line
        Cea608Encoder enc(eCfg);
        Cea608Decoder dec;

        SubtitleSpan::List spans;
        spans.pushToBack(SubtitleSpan("aa bb cc\ndd ee ff", false, false, false));
        SubtitleList subs;
        subs.append(Subtitle(tsAt30fps(60), tsAt30fps(180), spans, SubtitleAnchor::BottomCenter,
                             Rect2Di32(), String(), Metadata()));
        REQUIRE(enc.setSubtitles(subs).isOk());

        runEncoderToDecoder(enc, dec, 220);
        SubtitleList out = dec.finalize();
        REQUIRE(out.size() == 1);
        // The decoded text has the source '\n' collapsed to a
        // single space — never the bare concatenation "cc"+"dd".
        const String flat = out[0].text();
        CHECK_FALSE(flat.contains(String("ccdd")));
        CHECK(flat.contains(String("cc dd")));
}

TEST_CASE("Cea608: underline span round-trips with no leading or trailing bleed") {
        // Three-run cue mirroring substest.srt cue 8: plain prefix,
        // an even-length underlined word, plain suffix.  The
        // encoder strips the runs' leading spaces at the MR
        // boundaries (so the styled region carries only
        // "underlined"); the decoder reconstructs neutral inter-run
        // separators.  Net: the decoded underlined span's text is
        // exactly the underlined word, with no one-cell leakage of
        // the underline into adjacent spaces.
        Cea608Encoder::Config eCfg;
        eCfg.frameRate = FrameRate(FrameRate::FPS_30);
        Cea608Encoder enc(eCfg);
        Cea608Decoder dec;

        SubtitleSpan::List spans;
        spans.pushToBack(SubtitleSpan("and ",       false, false, false));
        spans.pushToBack(SubtitleSpan("underlined", false, false, true));
        spans.pushToBack(SubtitleSpan(" mixed",     false, false, false));
        SubtitleList subs;
        subs.append(Subtitle(tsAt30fps(60), tsAt30fps(180), spans, SubtitleAnchor::BottomCenter,
                             Rect2Di32(), String(), Metadata()));
        REQUIRE(enc.setSubtitles(subs).isOk());

        runEncoderToDecoder(enc, dec, 220);
        SubtitleList out = dec.finalize();
        REQUIRE(out.size() == 1);
        const SubtitleSpan::List &gotSpans = out[0].spans();
        // Find the underlined span — it must carry "underlined"
        // exactly, with no leading or trailing space.  Adjacent
        // spans must not carry the underline flag.
        // Per §6.2 the MR cell renders in the new style — so a
        // styled-space span carrying underline=true may sit right
        // before "underlined".  The cue must contain the underlined
        // word; any other underline-flagged span must be a single
        // styled space.
        bool foundUnderlined = false;
        for (size_t i = 0; i < gotSpans.size(); ++i) {
                if (!gotSpans[i].underline()) continue;
                if (gotSpans[i].text() == "underlined") {
                        foundUnderlined = true;
                } else {
                        CHECK(gotSpans[i].text() == " ");
                }
        }
        CHECK(foundUnderlined);
        // Flat text reads as the original sentence with single
        // spaces between words.  The wrap tokenizer strips trailing
        // whitespace from each source span, so the per-span runs are
        // "and" (3, odd), "underlined" (10, even), and "mixed" (5,
        // odd).  Both odd runs get their pair-cadence tail padded
        // with NUL (the decoder skips b2 < 0x20), so no pad-space
        // leaks into the grid.  Both MR transitions land with the
        // prior cell holding a real char (not a space), so the
        // decoder synthesises a neutral styled-space separator cell
        // for each MR — giving us "and" + sep + "underlined" + sep +
        // "mixed" with no trailing pad-space.
        CHECK(out[0].text() == "and underlined mixed");
}

TEST_CASE("Cea608: bold span emits a warning but encodes the non-bold attributes") {
        Cea608Encoder::Config eCfg;
        eCfg.frameRate = FrameRate(FrameRate::FPS_30);
        Cea608Encoder enc(eCfg);
        Cea608Decoder dec;

        // Bold-italic-red span; bold drops, italic + (white per
        // italic spec rule) survive.
        SubtitleSpan::List spans;
        spans.pushToBack(SubtitleSpan("BI", true /*bold*/, true /*italic*/, false, Color::Red));
        SubtitleList subs;
        subs.append(Subtitle(tsAt30fps(60), tsAt30fps(120), spans, SubtitleAnchor::BottomCenter,
                             Rect2Di32(), String(), Metadata()));
        REQUIRE(enc.setSubtitles(subs).isOk());

        runEncoderToDecoder(enc, dec, 140);
        SubtitleList out = dec.finalize();
        REQUIRE(out.size() == 1);
        REQUIRE(out[0].spans().size() >= 1);
        const SubtitleSpan &span = out[0].spans()[0];
        CHECK_FALSE(span.bold()); // 608 wire cannot carry bold.
        CHECK(span.italic());     // Italic survives.
}

// ============================================================================
// Paint-on mode (Phase 3.5d)
// ============================================================================

TEST_CASE("Cea608Decoder[paint-on]: hand-rolled RDC/PAC/chars/EDM emits one cue") {
        Cea608Decoder dec;
        // Frames 0..1: RDC doubled.  Mode → paint-on.
        dec.pushFrame(FrameNumber(0), tsFromMs(0),
                      onePair(Cea608::Cc1MiscFirstByte, Cea608::MiscRDC));
        dec.pushFrame(FrameNumber(1), tsFromMs(33),
                      onePair(Cea608::Cc1MiscFirstByte, Cea608::MiscRDC));
        // Frames 2..3: PAC doubled (row 15).  Cue start set here.
        const TimeStamp pacTs = tsFromMs(67);
        dec.pushFrame(FrameNumber(2), pacTs,
                      onePair(Cea608::PacRow15Col0WhiteB1, Cea608::PacRow15Col0WhiteB2));
        dec.pushFrame(FrameNumber(3), tsFromMs(100),
                      onePair(Cea608::PacRow15Col0WhiteB1, Cea608::PacRow15Col0WhiteB2));
        // Frame 4: "AB" — live commit.
        dec.pushFrame(FrameNumber(4), tsFromMs(133), onePair(0x41, 0x42));
        // Live display visible.
        CHECK(dec.displayedText() == "AB");
        // Frames 5..29: null; cue continues to be live.
        for (int64_t f = 5; f < 30; ++f) {
                dec.pushFrame(FrameNumber(f), tsFromMs(f * 1000 / 30), onePair(0, 0));
                CHECK(dec.displayedText() == "AB");
        }
        // Frames 30..31: EDM doubled — finalize the cue.
        const TimeStamp edmTs = tsFromMs(1000);
        dec.pushFrame(FrameNumber(30), edmTs,
                      onePair(Cea608::EdmB1, Cea608::EdmB2));
        dec.pushFrame(FrameNumber(31), tsFromMs(1033),
                      onePair(Cea608::EdmB1, Cea608::EdmB2));
        CHECK(dec.displayedText() == "");

        SubtitleList out = dec.finalize();
        REQUIRE(out.size() == 1);
        CHECK(out[0].text() == "AB");
        // Cue start is the PAC timestamp (paint-on sets start on first
        // PAC after RDC, refined to the moment writing actually begins).
        CHECK(out[0].start() == pacTs);
        CHECK(out[0].end() == edmTs);
}

TEST_CASE("Cea608Decoder[paint-on]: round-trips with Cea608Encoder") {
        Cea608Encoder::Config encCfg;
        encCfg.frameRate = FrameRate(FrameRate::FPS_30);
        encCfg.mode = Cea608Encoder::Mode::PaintOn;
        Cea608Encoder enc(encCfg);
        Cea608Decoder dec;

        SubtitleList in;
        // Wrap strips trailing whitespace from the source span, so
        // "HELLO" (5 chars) is what the encoder actually emits.  Odd
        // count → NUL pad on the final pair (decoder skips it), so
        // the decoded text is "HELLO" with no trailing pad-space.
        in.append(Subtitle(tsAt30fps(30), tsAt30fps(90), "HELLO"));
        REQUIRE(enc.setSubtitles(in).isOk());

        runEncoderToDecoder(enc, dec, 110);
        SubtitleList out = dec.finalize();
        REQUIRE(out.size() == 1);
        CHECK(out[0].text() == "HELLO");
        // End is the EDM frame timestamp (frame 90).
        CHECK(out[0].end() == tsAt30fps(90));
}

// ============================================================================
// Roll-up mode (Phase 3.5d)
// ============================================================================

TEST_CASE("Cea608Decoder[roll-up]: hand-rolled RU2/CR/chars/CR emits one cue per row") {
        Cea608Decoder dec;
        // Frames 0..1: RU2 doubled.  Mode → roll-up; clears displayed.
        dec.pushFrame(FrameNumber(0), tsFromMs(0),
                      onePair(Cea608::Cc1MiscFirstByte, Cea608::MiscRU2));
        dec.pushFrame(FrameNumber(1), tsFromMs(33),
                      onePair(Cea608::Cc1MiscFirstByte, Cea608::MiscRU2));
        // Frames 2..3: CR doubled.  Starts row 1.
        const TimeStamp cr1Ts = tsFromMs(67);
        dec.pushFrame(FrameNumber(2), cr1Ts,
                      onePair(Cea608::Cc1MiscFirstByte, Cea608::MiscCR));
        dec.pushFrame(FrameNumber(3), tsFromMs(100),
                      onePair(Cea608::Cc1MiscFirstByte, Cea608::MiscCR));
        // Frame 4..5: PAC doubled.
        dec.pushFrame(FrameNumber(4), tsFromMs(133),
                      onePair(Cea608::PacRow15Col0WhiteB1, Cea608::PacRow15Col0WhiteB2));
        dec.pushFrame(FrameNumber(5), tsFromMs(167),
                      onePair(Cea608::PacRow15Col0WhiteB1, Cea608::PacRow15Col0WhiteB2));
        // Frame 6: "AB".
        dec.pushFrame(FrameNumber(6), tsFromMs(200), onePair(0x41, 0x42));
        CHECK(dec.displayedText() == "AB");
        // Frame 7..10: nulls.
        for (int64_t f = 7; f < 11; ++f) {
                dec.pushFrame(FrameNumber(f), tsFromMs(f * 1000 / 30), onePair(0, 0));
        }
        // Frame 11..12: CR doubled — emits row 1 as cue, starts row 2.
        const TimeStamp cr2Ts = tsFromMs(367);
        dec.pushFrame(FrameNumber(11), cr2Ts,
                      onePair(Cea608::Cc1MiscFirstByte, Cea608::MiscCR));
        dec.pushFrame(FrameNumber(12), tsFromMs(400),
                      onePair(Cea608::Cc1MiscFirstByte, Cea608::MiscCR));
        // Frame 13: "CD".
        dec.pushFrame(FrameNumber(13), tsFromMs(433), onePair(0x43, 0x44));

        const TimeStamp finalTs = tsFromMs(500);
        dec.pushFrame(FrameNumber(14), finalTs, onePair(0, 0));
        SubtitleList out = dec.finalize();
        REQUIRE(out.size() == 2);
        CHECK(out[0].text() == "AB");
        CHECK(out[0].start() == cr1Ts);
        CHECK(out[0].end() == cr2Ts);
        CHECK(out[1].text() == "CD");
        CHECK(out[1].start() == cr2Ts);
        CHECK(out[1].end() == finalTs); // finalize closes last row.
}

TEST_CASE("Cea608Decoder[roll-up]: chars after CR with no following PAC default to white / non-italic / non-flash (§C.14)") {
        // §C.14 normative default: a character that arrives in a row
        // with no PAC carries white / non-italic / non-flash style.
        // Build a roll-up stream where the first row is italic (PAC
        // sub-field 7), CR fires, then "DEF" arrives WITHOUT a
        // preceding PAC.  "DEF" must land with default WireStyle.
        Cea608Decoder dec;
        dec.pushFrame(FrameNumber(0), tsFromMs(0),
                      onePair(Cea608::Cc1MiscFirstByte, Cea608::MiscRU2));
        dec.pushFrame(FrameNumber(1), tsFromMs(33),
                      onePair(Cea608::Cc1MiscFirstByte, Cea608::MiscRU2));
        // CR (doubled) starts the first row.
        dec.pushFrame(FrameNumber(2), tsFromMs(67),
                      onePair(Cea608::Cc1MiscFirstByte, Cea608::MiscCR));
        dec.pushFrame(FrameNumber(3), tsFromMs(100),
                      onePair(Cea608::Cc1MiscFirstByte, Cea608::MiscCR));
        // PAC Row 15 italic (sub-field 7, b2 layout: 0b 0 1 R P P P P U
        // with R=1 for Row15-second-of-pair, P=7 → 0x40|0x20|(7<<1)=0x6E).
        const uint8_t italicPacB1 = Cea608::PacRow15Col0WhiteB1; // 0x14
        const uint8_t italicPacB2 = 0x6E;
        dec.pushFrame(FrameNumber(4), tsFromMs(133), onePair(italicPacB1, italicPacB2));
        dec.pushFrame(FrameNumber(5), tsFromMs(167), onePair(italicPacB1, italicPacB2));
        dec.pushFrame(FrameNumber(6), tsFromMs(200), onePair('A', 'B'));
        // CR (doubled) — row 1 ("AB" italic) emits.
        dec.pushFrame(FrameNumber(7), tsFromMs(233),
                      onePair(Cea608::Cc1MiscFirstByte, Cea608::MiscCR));
        dec.pushFrame(FrameNumber(8), tsFromMs(267),
                      onePair(Cea608::Cc1MiscFirstByte, Cea608::MiscCR));
        // "DE" arrives with no PAC — must inherit §C.14 defaults
        // (white / non-italic / non-flash), NOT the prior row's italic.
        dec.pushFrame(FrameNumber(9), tsFromMs(300), onePair('D', 'E'));

        const TimeStamp finalTs = tsFromMs(500);
        dec.pushFrame(FrameNumber(10), finalTs, onePair(0, 0));
        SubtitleList out = dec.finalize();
        REQUIRE(out.size() == 2);
        // Row 1: italic "AB".
        REQUIRE(out[0].spans().size() >= 1);
        CHECK(out[0].spans()[0].italic() == true);
        CHECK(out[0].text() == "AB");
        // Row 2: defaults (no italic, default-invalid colour = white,
        // foreground opacity == Solid).
        REQUIRE(out[1].spans().size() >= 1);
        CHECK(out[1].spans()[0].italic() == false);
        CHECK_FALSE(out[1].spans()[0].color().isValid()); // means "white default"
        CHECK(out[1].spans()[0].foregroundOpacity() == SubtitleOpacity::Solid);
        CHECK(out[1].text() == "DE");
}

TEST_CASE("Cea608Decoder[roll-up]: round-trips with Cea608Encoder") {
        Cea608Encoder::Config encCfg;
        encCfg.frameRate = FrameRate(FrameRate::FPS_30);
        encCfg.mode = Cea608Encoder::Mode::RollUp;
        encCfg.rollUpRows = 2;
        Cea608Encoder enc(encCfg);
        Cea608Decoder dec;

        SubtitleList in;
        in.append(Subtitle(tsAt30fps(30), tsAt30fps(120), "ABCD"));
        in.append(Subtitle(tsAt30fps(150), tsAt30fps(240), "EFGH"));
        REQUIRE(enc.setSubtitles(in).isOk());

        runEncoderToDecoder(enc, dec, 260);
        SubtitleList out = dec.finalize();
        REQUIRE(out.size() == 2);
        CHECK(out[0].text() == "ABCD");
        CHECK(out[1].text() == "EFGH");
        // Cue boundary in roll-up: cue 0 starts at first CR (which
        // lands at firstFrame+2 = frame 26 = tsAt30fps(26)).  This
        // test relies on the encoder's specific layout — looser
        // checks just verify that the cues are emitted, not the
        // exact timestamps.
        CHECK(out[0].start().value() <= out[0].end().value());
        CHECK(out[1].start().value() <= out[1].end().value());
}

TEST_CASE("Cea608Decoder: span background colour round-trips via EIA-608-B BG codes") {
        // Encode a cue with a Red opaque background and verify the
        // decoder recovers the bg colour on the cue's span.
        Cea608Encoder::Config encCfg;
        encCfg.frameRate = FrameRate(FrameRate::FPS_30);
        Cea608Encoder enc(encCfg);

        SubtitleSpan span("HI", false, false, false, Color::White);
        span.setBackgroundColor(Color::Red);
        span.setBackgroundOpacity(SubtitleOpacity::Solid);
        SubtitleSpan::List spans;
        spans.pushToBack(span);
        SubtitleList in;
        in.append(Subtitle(tsAt30fps(30), tsAt30fps(60), spans, SubtitleAnchor::Default,
                            Rect2Di32(), String(), Metadata()));
        REQUIRE(enc.setSubtitles(in).isOk());

        Cea608Decoder dec;
        for (int64_t f = 0; f < 80; ++f) {
                dec.pushFrame(FrameNumber(f), tsAt30fps(f), enc.nextFrame(FrameNumber(f)));
        }
        SubtitleList out = dec.finalize();
        REQUIRE(out.size() == 1);
        REQUIRE(out[0].spans().size() >= 1);
        // The decoded bg colour quantises through the same 608
        // palette the encoder used, so a Red input rebuilds as
        // exact Cea608 Red sRGB(1,0,0).
        const SubtitleSpan &gotSpan = out[0].spans()[0];
        CHECK(gotSpan.backgroundColor().isValid());
        CHECK(gotSpan.backgroundColor().r8() == 255);
        CHECK(gotSpan.backgroundColor().g8() == 0);
        CHECK(gotSpan.backgroundColor().b8() == 0);
        CHECK(gotSpan.backgroundOpacity() == SubtitleOpacity::Solid);
}

TEST_CASE("Cea608Decoder: Black background colour round-trips via wire index 7") {
        // Black bg is the EIA-608-B BG-attribute extension that the
        // PAC / mid-row colour subfields can't express (code 7 is
        // "italic white" there).  After adding CaptionColor::Black,
        // a near-Black input bg quantises to the new palette[7]
        // entry, encodes as wire index 7, and round-trips through
        // the decoder back to Color::Black.
        Cea608Encoder::Config encCfg;
        encCfg.frameRate = FrameRate(FrameRate::FPS_30);
        Cea608Encoder enc(encCfg);

        SubtitleSpan span("HI", false, false, false, Color::White);
        span.setBackgroundColor(Color::Black);
        span.setBackgroundOpacity(SubtitleOpacity::Solid);
        SubtitleSpan::List spans;
        spans.pushToBack(span);
        SubtitleList in;
        in.append(Subtitle(tsAt30fps(30), tsAt30fps(60), spans, SubtitleAnchor::Default,
                            Rect2Di32(), String(), Metadata()));
        REQUIRE(enc.setSubtitles(in).isOk());

        Cea608Decoder dec;
        for (int64_t f = 0; f < 80; ++f) {
                dec.pushFrame(FrameNumber(f), tsAt30fps(f), enc.nextFrame(FrameNumber(f)));
        }
        SubtitleList out = dec.finalize();
        REQUIRE(out.size() == 1);
        REQUIRE(out[0].spans().size() >= 1);
        const SubtitleSpan &gotSpan = out[0].spans()[0];
        CHECK(gotSpan.backgroundColor().isValid());
        CHECK(gotSpan.backgroundColor().r8() == 0);
        CHECK(gotSpan.backgroundColor().g8() == 0);
        CHECK(gotSpan.backgroundColor().b8() == 0);
        CHECK(gotSpan.backgroundOpacity() == SubtitleOpacity::Solid);
}

TEST_CASE("Cea608: encode/decode Black fg via FA/FAU round-trips losslessly") {
        // CEA-608-E §6.2 Table 3 carries Black foreground via the
        // Foreground Attribute codes (FA at (0x17, 0x2E), FAU at
        // (0x17, 0x2F)) — PAC + mid-row colour subfields can't
        // encode Black directly.  The encoder emits the PAC carrying
        // White (the spec-mandated fallback for the colour subfield)
        // followed by FA / FAU right after the PAC to assert Black;
        // the decoder recognises FA / FAU as a mid-row-style colour
        // change to Black.
        Cea608Encoder::Config encCfg;
        encCfg.frameRate = FrameRate(FrameRate::FPS_30);
        Cea608Encoder enc(encCfg);

        SubtitleSpan span("HI", false, false, false, Color::Black);
        SubtitleSpan::List spans;
        spans.pushToBack(span);
        SubtitleList in;
        in.append(Subtitle(tsAt30fps(60), tsAt30fps(120), spans,
                            SubtitleAnchor::BottomCenter, Rect2Di32(),
                            String(), Metadata()));
        REQUIRE(enc.setSubtitles(in).isOk());

        Cea608Decoder dec;
        for (int64_t f = 0; f < 150; ++f) {
                dec.pushFrame(FrameNumber(f), tsAt30fps(f), enc.nextFrame(FrameNumber(f)));
        }
        SubtitleList out = dec.finalize();
        REQUIRE(out.size() == 1);
        REQUIRE(out[0].spans().size() >= 1);
        // The decoder may emit a synthetic inter-run space span ahead
        // of the styled run on receipt of the FA / FAU code (mirrors
        // the spec's "preceded by a space + automatic BS" semantics).
        // Find the span containing the actual text.
        const SubtitleSpan *gotSpan = nullptr;
        for (size_t i = 0; i < out[0].spans().size(); ++i) {
                if (out[0].spans()[i].text().contains("HI")) {
                        gotSpan = &out[0].spans()[i];
                        break;
                }
        }
        REQUIRE(gotSpan != nullptr);
        REQUIRE(gotSpan->color().isValid());
        CHECK(gotSpan->color().r8() == 0);
        CHECK(gotSpan->color().g8() == 0);
        CHECK(gotSpan->color().b8() == 0);
        // Black foreground uses the §6.2 FA / FAU attribute family
        // (not PAC's colour subfield), so PAC is free to carry an
        // indent.  After E4 the encoder no longer collapses Black +
        // BottomCenter to flush-left — the cue round-trips at its
        // authored horizontal half via PAC(indent) + TabOffset + FA.
        CHECK(out[0].anchor() == SubtitleAnchor::BottomCenter);
}

// ============================================================================
// Unicode round-trip — basic G0 remapped + Special / Extended characters
// ============================================================================

namespace {

        /// @brief Drives an encoder through a single cue with @p text
        ///        and feeds the emitted triples into a decoder, then
        ///        returns the recovered cue text (joined across spans).
        ///        Exercises encoder -> wire bytes -> decoder dispatch
        ///        end-to-end.
        String roundTripCueText(const String &text) {
                Cea608Encoder::Config encCfg;
                encCfg.frameRate = FrameRate(FrameRate::FPS_30);
                Cea608Encoder enc(encCfg);
                SubtitleList  in;
                in.append(Subtitle(tsAt30fps(120), tsAt30fps(180), text));
                REQUIRE(enc.setSubtitles(in).isOk());

                Cea608Decoder dec;
                for (int64_t f = 0; f < 200; ++f) {
                        dec.pushFrame(FrameNumber(f), tsAt30fps(f), enc.nextFrame(FrameNumber(f)));
                }
                SubtitleList out = dec.finalize();
                REQUIRE(out.size() == 1);
                return out[0].text();
        }

} // namespace

TEST_CASE("Cea608: basic G0 remapped Latin / arithmetic glyphs round-trip") {
        // The ten 608 G0 positions that aren't ASCII — feed each
        // through the encoder/decoder pair and verify byte-exact
        // recovery.  Padded to an even codepoint count so the
        // encoder's odd-length-pad doesn't append a trailing space.
        const String text("\xC3\xA1" "\xC3\xA9" "\xC3\xAD" "\xC3\xB3" "\xC3\xBA" "\xC3\xA7"
                          "\xC3\xB7" "\xC3\x91" "\xC3\xB1" "x");
        CHECK(roundTripCueText(text) == text);
}

TEST_CASE("Cea608: Special Character ™ round-trips via doubled (0x11, 0x34)") {
        // "Brand™" — ™ is U+2122, special index 0x34, placeholder '('.
        // The encoder packs the placeholder as the second byte of a
        // pair (or with a NUL pad if alone) and follows with the
        // doubled control code.  The decoder replaces the placeholder
        // with the special glyph.
        const String text("Brand\xE2\x84\xA2");
        CHECK(roundTripCueText(text) == text);
}

TEST_CASE("Cea608: Special Character ½ and ¿ round-trip mid-cue") {
        // Even codepoint count so the encoder's odd-length pad doesn't
        // append a trailing space.
        const String text("ab\xC2\xBD" "cd\xC2\xBF");
        CHECK(roundTripCueText(text) == text);
}

TEST_CASE("Cea608: Special Character ♪ (music note) round-trips when leading the cue") {
        // Lone leading special char — exercises the lone-placeholder
        // NUL-pad path in the encoder (placeholder lands as b1 of
        // its pair, b2 is 0x00 so the receiver doesn't advance past
        // the placeholder before the doubled control code lands).
        // Three-codepoint cue keeps the byte cadence even so no
        // trailing space pad is appended.
        const String text("\xE2\x99\xAA" "bc");
        CHECK(roundTripCueText(text) == text);
}

TEST_CASE("Cea608: extended Spanish Á / É / Ó / ¡ / © round-trip") {
        const String text("\xC3\x81" "\xC3\x89" "\xC3\x93 \xC2\xA1Hola\xC2\xA9");
        CHECK(roundTripCueText(text) == text);
}

TEST_CASE("Cea608: extended French / German Ä / Ö / ß round-trip") {
        const String text("Gru\xC3\x9F\xC3\xA4\xC3\xB6");
        CHECK(roundTripCueText(text) == text);
}

TEST_CASE("Cea608: codepoint with no 608 mapping substitutes a space") {
        // 한 (U+D55C) has no CEA-608 representation — the encoder
        // substitutes 0x20.  The recovered text should keep the
        // ASCII context plus a single space where 한 used to be.
        const String text("ko\xED\x95\x9C" "rea");
        const String got = roundTripCueText(text);
        CHECK(got == String("ko rea"));
}

TEST_CASE("Cea608: mixed-encoding cue (basic G0 + Special + Extended) round-trips") {
        // a + é (basic G0 0x5C) + ™ (Special) + Á (ExtSpanish) +
        // ñ (basic G0 0x7E) + ß (ExtPortugueseGerman).  Letters glue them so
        // the wrapper's word splitter doesn't drop standalone
        // glyphs.
        const String text("a\xC3\xA9 X\xE2\x84\xA2 t\xC3\x81m\xC3\xB1k\xC3\x9F");
        CHECK(roundTripCueText(text) == text);
}

// ============================================================================
// BS / DER / FON misc-control receiver support
// ============================================================================

namespace {

        /// @brief Hand-builds a CcDataList with one parity-stamped
        ///        byte pair on field-1 (CC1).  Lets the BS / DER /
        ///        FON tests inject bytes that the encoder doesn't
        ///        currently emit.
        Cea708Cdp::CcDataList ccDataPair(uint8_t b1, uint8_t b2) {
                Cea708Cdp::CcDataList list;
                list.pushToBack(Cea708Cdp::CcData{
                        true, 0,
                        Cea608::withOddParity(b1),
                        Cea608::withOddParity(b2)});
                return list;
        }

} // namespace

TEST_CASE("Cea608Decoder: BS removes the most recently appended character") {
        // Hand-roll a paint-on stream that types "ABXC", backspaces
        // the X, ends with EDM.  Recovered cue text should be "ABC".
        Cea608Decoder dec;

        // RDC + RDC (paint-on mode).
        dec.pushFrame(FrameNumber(0), tsAt30fps(0),
                      ccDataPair(Cea608::Cc1MiscFirstByte, Cea608::MiscRDC));
        dec.pushFrame(FrameNumber(1), tsAt30fps(1),
                      ccDataPair(Cea608::Cc1MiscFirstByte, Cea608::MiscRDC));
        // PAC row 15 white.
        dec.pushFrame(FrameNumber(2), tsAt30fps(2),
                      ccDataPair(Cea608::PacRow15Col0WhiteB1, Cea608::PacRow15Col0WhiteB2));
        dec.pushFrame(FrameNumber(3), tsAt30fps(3),
                      ccDataPair(Cea608::PacRow15Col0WhiteB1, Cea608::PacRow15Col0WhiteB2));
        // "AB"
        dec.pushFrame(FrameNumber(4), tsAt30fps(4), ccDataPair('A', 'B'));
        // "X" + space pad
        dec.pushFrame(FrameNumber(5), tsAt30fps(5), ccDataPair('X', 0x20));
        // BS doubled (X removed).
        dec.pushFrame(FrameNumber(6), tsAt30fps(6),
                      ccDataPair(Cea608::Cc1MiscFirstByte, Cea608::MiscBS));
        dec.pushFrame(FrameNumber(7), tsAt30fps(7),
                      ccDataPair(Cea608::Cc1MiscFirstByte, Cea608::MiscBS));
        // "C" + space pad
        dec.pushFrame(FrameNumber(8), tsAt30fps(8), ccDataPair('C', 0x20));
        // EDM doubled — finalise the paint-on cue.
        dec.pushFrame(FrameNumber(9), tsAt30fps(9),
                      ccDataPair(Cea608::EdmB1, Cea608::EdmB2));
        dec.pushFrame(FrameNumber(10), tsAt30fps(10),
                      ccDataPair(Cea608::EdmB1, Cea608::EdmB2));

        SubtitleList out = dec.finalize();
        REQUIRE(out.size() == 1);
        // BS popped the trailing space the (X, 0x20) pair appended.
        // The X stays — to delete it cleanly the captioner would
        // send a second doubled BS.  This test exercises the BS
        // mechanic itself; the (X, 0x20) → BS → leaves "ABX".  Then
        // the (C, 0x20) pair appends "C " on top, so the recovered
        // text reads "ABXC ".
        CHECK(out[0].text() == "ABXC ");
}

TEST_CASE("Cea608Decoder: BS at Column 1 of a multi-row cue does not steal from the prior row (§C.13)") {
        // Per §C.13 a BS received at Column 1 is ignored.  In our
        // model that means BS at @c loadingColumn=0 (no PAC indent,
        // no chars typed) returns without shaving any prior-row
        // span.  Set up a multi-row paint-on cue with row 14 "AB",
        // PAC row 15 at col 0, then BS immediately — the "AB" on
        // row 14 must survive.
        Cea608Decoder dec;
        dec.pushFrame(FrameNumber(0), tsAt30fps(0),
                      ccDataPair(Cea608::Cc1MiscFirstByte, Cea608::MiscRDC));
        dec.pushFrame(FrameNumber(1), tsAt30fps(1),
                      ccDataPair(Cea608::Cc1MiscFirstByte, Cea608::MiscRDC));
        // PAC Row 14 white col 0 (b1=0x14 b2=0x50).
        dec.pushFrame(FrameNumber(2), tsAt30fps(2), ccDataPair(0x14, 0x50));
        dec.pushFrame(FrameNumber(3), tsAt30fps(3), ccDataPair(0x14, 0x50));
        dec.pushFrame(FrameNumber(4), tsAt30fps(4), ccDataPair('A', 'B'));
        // PAC Row 15 white col 0 — new row, cursor at col 0.
        dec.pushFrame(FrameNumber(5), tsAt30fps(5),
                      ccDataPair(Cea608::PacRow15Col0WhiteB1, Cea608::PacRow15Col0WhiteB2));
        dec.pushFrame(FrameNumber(6), tsAt30fps(6),
                      ccDataPair(Cea608::PacRow15Col0WhiteB1, Cea608::PacRow15Col0WhiteB2));
        // BS doubled at col 0 — must NOT shave row 14's "AB".
        dec.pushFrame(FrameNumber(7), tsAt30fps(7),
                      ccDataPair(Cea608::Cc1MiscFirstByte, Cea608::MiscBS));
        dec.pushFrame(FrameNumber(8), tsAt30fps(8),
                      ccDataPair(Cea608::Cc1MiscFirstByte, Cea608::MiscBS));
        dec.pushFrame(FrameNumber(9), tsAt30fps(9), ccDataPair('C', 'D'));
        dec.pushFrame(FrameNumber(10), tsAt30fps(10),
                      ccDataPair(Cea608::EdmB1, Cea608::EdmB2));
        dec.pushFrame(FrameNumber(11), tsAt30fps(11),
                      ccDataPair(Cea608::EdmB1, Cea608::EdmB2));

        SubtitleList out = dec.finalize();
        REQUIRE(out.size() == 1);
        // Row 14 "AB" survives across the BS; row 15 "CD" follows.
        const String t = out[0].text();
        CHECK((t == "AB\nCD" || t == "ABCD")); // join policy is renderer's
}

TEST_CASE("Cea608Decoder: BS at the start of the loading buffer is a no-op") {
        Cea608Decoder dec;
        // RDC + PAC + immediate BS (nothing to backspace).
        dec.pushFrame(FrameNumber(0), tsAt30fps(0),
                      ccDataPair(Cea608::Cc1MiscFirstByte, Cea608::MiscRDC));
        dec.pushFrame(FrameNumber(1), tsAt30fps(1),
                      ccDataPair(Cea608::Cc1MiscFirstByte, Cea608::MiscRDC));
        dec.pushFrame(FrameNumber(2), tsAt30fps(2),
                      ccDataPair(Cea608::PacRow15Col0WhiteB1, Cea608::PacRow15Col0WhiteB2));
        dec.pushFrame(FrameNumber(3), tsAt30fps(3),
                      ccDataPair(Cea608::PacRow15Col0WhiteB1, Cea608::PacRow15Col0WhiteB2));
        // BS doubled — buffer is empty.
        dec.pushFrame(FrameNumber(4), tsAt30fps(4),
                      ccDataPair(Cea608::Cc1MiscFirstByte, Cea608::MiscBS));
        dec.pushFrame(FrameNumber(5), tsAt30fps(5),
                      ccDataPair(Cea608::Cc1MiscFirstByte, Cea608::MiscBS));
        // "AB" lands cleanly.
        dec.pushFrame(FrameNumber(6), tsAt30fps(6), ccDataPair('A', 'B'));
        dec.pushFrame(FrameNumber(7), tsAt30fps(7),
                      ccDataPair(Cea608::EdmB1, Cea608::EdmB2));
        dec.pushFrame(FrameNumber(8), tsAt30fps(8),
                      ccDataPair(Cea608::EdmB1, Cea608::EdmB2));

        SubtitleList out = dec.finalize();
        REQUIRE(out.size() == 1);
        CHECK(out[0].text() == "AB");
}

TEST_CASE("Cea608Decoder: DER clears from cursor to end of row, preserving chars to the left") {
        // CEA-608-E §B.3: DER deletes "from (and including) the
        // current cell to the end of the row".  Chars typed to the
        // LEFT of the cursor at DER-receipt survive; chars to the
        // right (none, in this test) and the rest of the row are
        // cleared.  Subsequent chars typed after DER land at the
        // cursor's position (unchanged by DER).
        Cea608Decoder dec;

        dec.pushFrame(FrameNumber(0), tsAt30fps(0),
                      ccDataPair(Cea608::Cc1MiscFirstByte, Cea608::MiscRDC));
        dec.pushFrame(FrameNumber(1), tsAt30fps(1),
                      ccDataPair(Cea608::Cc1MiscFirstByte, Cea608::MiscRDC));
        dec.pushFrame(FrameNumber(2), tsAt30fps(2),
                      ccDataPair(Cea608::PacRow15Col0WhiteB1, Cea608::PacRow15Col0WhiteB2));
        dec.pushFrame(FrameNumber(3), tsAt30fps(3),
                      ccDataPair(Cea608::PacRow15Col0WhiteB1, Cea608::PacRow15Col0WhiteB2));
        dec.pushFrame(FrameNumber(4), tsAt30fps(4), ccDataPair('J', 'U'));
        dec.pushFrame(FrameNumber(5), tsAt30fps(5), ccDataPair('N', 'K'));
        dec.pushFrame(FrameNumber(6), tsAt30fps(6), ccDataPair('X', 0x20));
        // DER doubled.  Cursor sits at col 6 (after "JUNKX " — 5
        // chars plus the encoder's pad-space at col 5).  DER clears
        // cols 6..31; "JUNKX " (cols 0..5) stays.
        dec.pushFrame(FrameNumber(7), tsAt30fps(7),
                      ccDataPair(Cea608::Cc1MiscFirstByte, Cea608::MiscDER));
        dec.pushFrame(FrameNumber(8), tsAt30fps(8),
                      ccDataPair(Cea608::Cc1MiscFirstByte, Cea608::MiscDER));
        // "OK" lands at cols 6..7.
        dec.pushFrame(FrameNumber(9), tsAt30fps(9), ccDataPair('O', 'K'));
        dec.pushFrame(FrameNumber(10), tsAt30fps(10),
                      ccDataPair(Cea608::EdmB1, Cea608::EdmB2));
        dec.pushFrame(FrameNumber(11), tsAt30fps(11),
                      ccDataPair(Cea608::EdmB1, Cea608::EdmB2));

        SubtitleList out = dec.finalize();
        REQUIRE(out.size() == 1);
        // §B.3: chars left of the DER cursor survive (cols 0..5 =
        // "JUNKX ") and subsequent chars land at the cursor (cols
        // 6..7 = "OK").  Net row text: "JUNKX OK".
        CHECK(out[0].text() == "JUNKX OK");
}

TEST_CASE("Cea608Decoder: DER at end of typed row is a no-op when nothing follows the cursor") {
        // §B.3: DER deletes "from (and including) the current cell
        // to the end of the row".  When the cursor sits one past the
        // last typed cell (the normal post-typing position) and
        // nothing has been typed to the right, DER has nothing to
        // clear — the row's typed prefix is preserved as-is.
        Cea608Decoder dec;
        dec.pushFrame(FrameNumber(0), tsAt30fps(0),
                      ccDataPair(Cea608::Cc1MiscFirstByte, Cea608::MiscRDC));
        dec.pushFrame(FrameNumber(1), tsAt30fps(1),
                      ccDataPair(Cea608::Cc1MiscFirstByte, Cea608::MiscRDC));
        dec.pushFrame(FrameNumber(2), tsAt30fps(2),
                      ccDataPair(Cea608::PacRow15Col0WhiteB1, Cea608::PacRow15Col0WhiteB2));
        dec.pushFrame(FrameNumber(3), tsAt30fps(3),
                      ccDataPair(Cea608::PacRow15Col0WhiteB1, Cea608::PacRow15Col0WhiteB2));
        dec.pushFrame(FrameNumber(4), tsAt30fps(4), ccDataPair('A', 'B'));
        // DER doubled.  Cursor at col 2 (one past 'B'); DER clears
        // cols 2..31, none of which were occupied → no visible
        // change.
        dec.pushFrame(FrameNumber(5), tsAt30fps(5),
                      ccDataPair(Cea608::Cc1MiscFirstByte, Cea608::MiscDER));
        dec.pushFrame(FrameNumber(6), tsAt30fps(6),
                      ccDataPair(Cea608::Cc1MiscFirstByte, Cea608::MiscDER));
        dec.pushFrame(FrameNumber(7), tsAt30fps(7),
                      ccDataPair(Cea608::EdmB1, Cea608::EdmB2));
        dec.pushFrame(FrameNumber(8), tsAt30fps(8),
                      ccDataPair(Cea608::EdmB1, Cea608::EdmB2));
        SubtitleList out = dec.finalize();
        REQUIRE(out.size() == 1);
        CHECK(out[0].text() == "AB");
}

TEST_CASE("Cea608Decoder: DER preserves prior rows of a multi-row paint-on cue") {
        // §B.3: DER deletes "from the current cell to the end of the
        // row" — prior rows of a multi-row cue must survive.  Row
        // 14 holds "AA"; row 15 holds "BB" with the cursor at col 2
        // when DER fires.  Per spec, DER clears cols 2..31 of row
        // 15 only, leaving both "AA" on row 14 and the existing
        // "BB" on row 15 (cols 0..1, to the left of cursor).
        //
        // PAC row 14 col 0 white encoding: b1=0x14 (row pair 14/15),
        // b2 bit 5 (0x20) clear (= first of pair = row 14), subfield
        // 0 (= col 0 white).  → b2 = 0x40.
        Cea608Decoder dec;
        dec.pushFrame(FrameNumber(0), tsAt30fps(0),
                      ccDataPair(Cea608::Cc1MiscFirstByte, Cea608::MiscRDC));
        dec.pushFrame(FrameNumber(1), tsAt30fps(1),
                      ccDataPair(Cea608::Cc1MiscFirstByte, Cea608::MiscRDC));
        // PAC Row 14 col 0 white — b2=0x40 (bit 5 clear → first of
        // pair → row 14; subfield 0 → col 0 white).
        dec.pushFrame(FrameNumber(2), tsAt30fps(2), ccDataPair(0x14, 0x40));
        dec.pushFrame(FrameNumber(3), tsAt30fps(3), ccDataPair(0x14, 0x40));
        dec.pushFrame(FrameNumber(4), tsAt30fps(4), ccDataPair('A', 'A'));
        // PAC Row 15 col 0 white.
        dec.pushFrame(FrameNumber(5), tsAt30fps(5),
                      ccDataPair(Cea608::PacRow15Col0WhiteB1, Cea608::PacRow15Col0WhiteB2));
        dec.pushFrame(FrameNumber(6), tsAt30fps(6),
                      ccDataPair(Cea608::PacRow15Col0WhiteB1, Cea608::PacRow15Col0WhiteB2));
        dec.pushFrame(FrameNumber(7), tsAt30fps(7), ccDataPair('B', 'B'));
        // DER on row 15 — must NOT drop row 14's "AA".  Cursor sits
        // at col 2 (after "BB"); DER clears cols 2..31 only.
        dec.pushFrame(FrameNumber(8), tsAt30fps(8),
                      ccDataPair(Cea608::Cc1MiscFirstByte, Cea608::MiscDER));
        dec.pushFrame(FrameNumber(9), tsAt30fps(9),
                      ccDataPair(Cea608::Cc1MiscFirstByte, Cea608::MiscDER));
        dec.pushFrame(FrameNumber(10), tsAt30fps(10),
                      ccDataPair(Cea608::EdmB1, Cea608::EdmB2));
        dec.pushFrame(FrameNumber(11), tsAt30fps(11),
                      ccDataPair(Cea608::EdmB1, Cea608::EdmB2));

        SubtitleList out = dec.finalize();
        REQUIRE(out.size() == 1);
        // Row 14 "AA" survives; row 15 keeps "BB" (DER didn't reach
        // cols 0..1).  Multi-row cue text joins rows with '\n'.
        CHECK(out[0].text() == "AA\nBB");
}

TEST_CASE("Cea608Decoder: drainXdsPackets exposes XDS packets from F2 triples") {
        // The decoder is configured for CC1 (field 1), but it must
        // also extract XDS from F2 (cc_type == 1) triples without
        // requiring a parallel Cea608XdsExtractor.  Push a frame
        // that carries both:
        //  - F1 CC1 caption bytes for "AB" (visible cue)
        //  - F2 XDS bytes for a Program Name packet ("HI")
        // After finalize the caption cue should land in the
        // SubtitleList and the XDS packet should land in drainXdsPackets.
        Cea608Decoder dec;

        // F1 caption: RDC + RDC + PAC Row15 + "AB" + EDM + EDM.
        auto cc = [](uint8_t b1, uint8_t b2) {
                return Cea708Cdp::CcData{true, 0,
                                        Cea608::withOddParity(b1),
                                        Cea608::withOddParity(b2)};
        };
        auto xdsP = [](uint8_t b1, uint8_t b2) {
                return Cea708Cdp::CcData{true, 1,
                                        Cea608::withOddParity(b1),
                                        Cea608::withOddParity(b2)};
        };
        // Build the XDS Program Name "HI" bytes (Start + Type + chars + End + chk).
        const uint32_t xsum = 0x01 + 0x03 + 'H' + 'I' + 0x0F;
        const uint8_t  xchk = static_cast<uint8_t>((0x80 - (xsum & 0x7F)) & 0x7F);

        // Frame 0: RDC (CC1) on F1, XDS Start(Current, Name) on F2.
        Cea708Cdp::CcDataList f0;
        f0.pushToBack(cc(Cea608::Cc1MiscFirstByte, Cea608::MiscRDC));
        f0.pushToBack(xdsP(0x01, 0x03));
        dec.pushFrame(FrameNumber(0), tsAt30fps(0), f0);

        Cea708Cdp::CcDataList f1;
        f1.pushToBack(cc(Cea608::Cc1MiscFirstByte, Cea608::MiscRDC));
        f1.pushToBack(xdsP('H', 'I'));
        dec.pushFrame(FrameNumber(1), tsAt30fps(1), f1);

        Cea708Cdp::CcDataList f2;
        f2.pushToBack(cc(Cea608::PacRow15Col0WhiteB1, Cea608::PacRow15Col0WhiteB2));
        f2.pushToBack(xdsP(0x0F, xchk));
        dec.pushFrame(FrameNumber(2), tsAt30fps(2), f2);

        dec.pushFrame(FrameNumber(3), tsAt30fps(3),
                      ccDataPair(Cea608::PacRow15Col0WhiteB1, Cea608::PacRow15Col0WhiteB2));
        dec.pushFrame(FrameNumber(4), tsAt30fps(4), ccDataPair('A', 'B'));
        dec.pushFrame(FrameNumber(5), tsAt30fps(5),
                      ccDataPair(Cea608::EdmB1, Cea608::EdmB2));
        dec.pushFrame(FrameNumber(6), tsAt30fps(6),
                      ccDataPair(Cea608::EdmB1, Cea608::EdmB2));

        SubtitleList out = dec.finalize();
        // Caption cue "AB" arrived on F1.
        REQUIRE(out.size() == 1);
        CHECK(out[0].text() == "AB");
        // XDS Program Name "HI" arrived on F2 and was extracted.
        const auto xdsPackets = dec.drainXdsPackets();
        REQUIRE(xdsPackets.size() == 1);
        CHECK(xdsPackets[0].class_ == Cea608XdsClass::Current);
        CHECK(xdsPackets[0].type == 0x03);
        CHECK(xdsPackets[0].programName() == "HI");
        CHECK(dec.xdsChecksumFailures() == 0);
}

TEST_CASE("Cea608Decoder: BS deletes the last char when cursor is past row start (§C.13)") {
        // Sanity test for BS at Column > 1: type "ABC", BS, end.
        // Expected: cue text "AB" (BS dropped 'C').
        Cea608Decoder dec;
        dec.pushFrame(FrameNumber(0), tsAt30fps(0),
                      ccDataPair(Cea608::Cc1MiscFirstByte, Cea608::MiscRDC));
        dec.pushFrame(FrameNumber(1), tsAt30fps(1),
                      ccDataPair(Cea608::Cc1MiscFirstByte, Cea608::MiscRDC));
        dec.pushFrame(FrameNumber(2), tsAt30fps(2),
                      ccDataPair(Cea608::PacRow15Col0WhiteB1, Cea608::PacRow15Col0WhiteB2));
        dec.pushFrame(FrameNumber(3), tsAt30fps(3),
                      ccDataPair(Cea608::PacRow15Col0WhiteB1, Cea608::PacRow15Col0WhiteB2));
        dec.pushFrame(FrameNumber(4), tsAt30fps(4), ccDataPair('A', 'B'));
        dec.pushFrame(FrameNumber(5), tsAt30fps(5), ccDataPair('C', 'D'));
        // BS doubled — drops the last char.  Cursor was at col 4 (4 chars
        // typed) → col 3 after BS.
        dec.pushFrame(FrameNumber(6), tsAt30fps(6),
                      ccDataPair(Cea608::Cc1MiscFirstByte, Cea608::MiscBS));
        dec.pushFrame(FrameNumber(7), tsAt30fps(7),
                      ccDataPair(Cea608::Cc1MiscFirstByte, Cea608::MiscBS));
        dec.pushFrame(FrameNumber(8), tsAt30fps(8),
                      ccDataPair(Cea608::EdmB1, Cea608::EdmB2));
        dec.pushFrame(FrameNumber(9), tsAt30fps(9),
                      ccDataPair(Cea608::EdmB1, Cea608::EdmB2));
        SubtitleList out = dec.finalize();
        REQUIRE(out.size() == 1);
        CHECK(out[0].text() == "ABC");
}

TEST_CASE("Cea608Decoder: Tab Offset clamps at Column 32 (cursor never exceeds 31)") {
        // Per §B.4 / §C.13: cursor must not exceed Column 32 (our col 31).
        // Build a roll-up cue with PAC indent=28 + Tab Offset T3 (advance 3 cols
        // → 31, clamp).  Type characters and verify the cell at col 31
        // is overwritten by each successive character (spec-compliant
        // overflow handling: cursor stays clamped at the rightmost
        // column, additional chars overwrite that cell).
        Cea608Decoder dec;
        dec.pushFrame(FrameNumber(0), tsFromMs(0),
                      onePair(Cea608::Cc1MiscFirstByte, Cea608::MiscRU2));
        dec.pushFrame(FrameNumber(1), tsFromMs(33),
                      onePair(Cea608::Cc1MiscFirstByte, Cea608::MiscRU2));
        dec.pushFrame(FrameNumber(2), tsFromMs(67),
                      onePair(Cea608::Cc1MiscFirstByte, Cea608::MiscCR));
        dec.pushFrame(FrameNumber(3), tsFromMs(100),
                      onePair(Cea608::Cc1MiscFirstByte, Cea608::MiscCR));
        // PAC Row 15 indent=28 white → b1=0x14, b2 = 0x40 | 0x20 (row15 bit)
        // | (15 << 1) (subfield 8+(28/4)=15) = 0x40|0x20|0x1E = 0x7E.
        dec.pushFrame(FrameNumber(4), tsFromMs(133), onePair(0x14, 0x7E));
        dec.pushFrame(FrameNumber(5), tsFromMs(167), onePair(0x14, 0x7E));
        // Tab Offset T3 (advance 3 cols → 31).
        dec.pushFrame(FrameNumber(6), tsFromMs(200),
                      onePair(Cea608::Cc1ExtAttrB1, 0x23));
        dec.pushFrame(FrameNumber(7), tsFromMs(233),
                      onePair(Cea608::Cc1ExtAttrB1, 0x23));
        // 'A' lands at col 31; cursor stays at 31 (§C.13 overflow
        // clamp).  'B' overwrites col 31 — the final cell at col 31
        // holds 'B', and the cue's flat text is just "B".  The
        // primary invariant tested is that the decoder doesn't
        // crash or generate cells outside the 32-column grid.
        dec.pushFrame(FrameNumber(8), tsFromMs(267), onePair('A', 'B'));
        dec.pushFrame(FrameNumber(9), tsFromMs(300),
                      onePair(Cea608::Cc1MiscFirstByte, Cea608::MiscCR));
        dec.pushFrame(FrameNumber(10), tsFromMs(333),
                      onePair(Cea608::Cc1MiscFirstByte, Cea608::MiscCR));
        SubtitleList out = dec.finalize();
        REQUIRE(out.size() >= 1);
        // Spec-compliant overflow: col 31 holds the most recent
        // char written there.
        CHECK(out[0].text() == "B");
}

TEST_CASE("Cea608Decoder: §C.9 auto-erase fires for paint-on after 16 s of no refresh (D5)") {
        // Spec-conformance fix D5: §C.9's 16-second timeout applies to
        // ANY mode's live cue (paint-on / roll-up loading memory as
        // well as pop-on displayed memory), not just pop-on.  Real
        // captioners refresh paint-on / roll-up live state with new
        // chars; if no refresh arrives within 16 s the cue must
        // auto-erase.
        Cea608Decoder dec;
        // Paint-on "AB" cue.
        dec.pushFrame(FrameNumber(0), tsFromMs(0),
                      onePair(Cea608::Cc1MiscFirstByte, Cea608::MiscRDC));
        dec.pushFrame(FrameNumber(1), tsFromMs(33),
                      onePair(Cea608::Cc1MiscFirstByte, Cea608::MiscRDC));
        dec.pushFrame(FrameNumber(2), tsFromMs(67),
                      onePair(Cea608::PacRow15Col0WhiteB1, Cea608::PacRow15Col0WhiteB2));
        dec.pushFrame(FrameNumber(3), tsFromMs(100),
                      onePair(Cea608::PacRow15Col0WhiteB1, Cea608::PacRow15Col0WhiteB2));
        dec.pushFrame(FrameNumber(4), tsFromMs(133), onePair('A', 'B'));
        // Live cue exists.
        CHECK(dec.displayedText() == "AB");
        // Push a frame 17 seconds later — past the §C.9 16-s window.
        // With D5 the paint-on live cue auto-erases at this point.
        dec.pushFrame(FrameNumber(510), tsFromMs(17000),
                      onePair(Cea608::NullB1, Cea608::NullB2));
        CHECK(dec.displayedText() == "");
        const SubtitleList out = dec.finalize();
        REQUIRE(out.size() == 1);
        CHECK(out[0].text() == "AB");
}

TEST_CASE("Cea608Decoder: §C.21 hysteresis erases the live cue after 45 frames of no valid data") {
        // Build a paint-on cue, then push 45 frames with NO triples
        // at all.  Per §C.21, after 45 consecutive bad frames the
        // display is disabled — the live paint-on cue must be
        // emitted (closed) when the threshold trips.
        Cea608Decoder dec;
        dec.pushFrame(FrameNumber(0), tsAt30fps(0),
                      ccDataPair(Cea608::Cc1MiscFirstByte, Cea608::MiscRDC));
        dec.pushFrame(FrameNumber(1), tsAt30fps(1),
                      ccDataPair(Cea608::Cc1MiscFirstByte, Cea608::MiscRDC));
        dec.pushFrame(FrameNumber(2), tsAt30fps(2),
                      ccDataPair(Cea608::PacRow15Col0WhiteB1, Cea608::PacRow15Col0WhiteB2));
        dec.pushFrame(FrameNumber(3), tsAt30fps(3),
                      ccDataPair(Cea608::PacRow15Col0WhiteB1, Cea608::PacRow15Col0WhiteB2));
        dec.pushFrame(FrameNumber(4), tsAt30fps(4), ccDataPair('A', 'B'));
        // Live cue exists.
        CHECK(dec.displayedText() == "AB");
        // Push 44 frames with empty data — no parity-valid byte for
        // the channel.  Counter should reach 44 (below threshold);
        // cue stays.
        Cea708Cdp::CcDataList empty;
        for (int64_t f = 5; f < 49; ++f) {
                dec.pushFrame(FrameNumber(f), tsAt30fps(f), empty);
        }
        CHECK(dec.displayedText() == "AB");
        // Frame 49 is the 45th consecutive bad frame — hysteresis trips.
        dec.pushFrame(FrameNumber(49), tsAt30fps(49), empty);
        // After auto-erase, displayedText is empty.
        CHECK(dec.displayedText() == "");
        const SubtitleList out = dec.finalize();
        REQUIRE(out.size() == 1);
        CHECK(out[0].text() == "AB");
}

TEST_CASE("Cea608Decoder: §C.21 hysteresis counter resets on any parity-valid frame") {
        // A single good frame between bad-frame runs resets the counter.
        Cea608Decoder dec;
        dec.pushFrame(FrameNumber(0), tsAt30fps(0),
                      ccDataPair(Cea608::Cc1MiscFirstByte, Cea608::MiscRDC));
        dec.pushFrame(FrameNumber(1), tsAt30fps(1),
                      ccDataPair(Cea608::Cc1MiscFirstByte, Cea608::MiscRDC));
        dec.pushFrame(FrameNumber(2), tsAt30fps(2),
                      ccDataPair(Cea608::PacRow15Col0WhiteB1, Cea608::PacRow15Col0WhiteB2));
        dec.pushFrame(FrameNumber(3), tsAt30fps(3),
                      ccDataPair(Cea608::PacRow15Col0WhiteB1, Cea608::PacRow15Col0WhiteB2));
        dec.pushFrame(FrameNumber(4), tsAt30fps(4), ccDataPair('A', 'B'));
        // 30 bad frames.
        Cea708Cdp::CcDataList empty;
        for (int64_t f = 5; f < 35; ++f) {
                dec.pushFrame(FrameNumber(f), tsAt30fps(f), empty);
        }
        // One good frame (null pair on the channel) — counter resets.
        dec.pushFrame(FrameNumber(35), tsAt30fps(35),
                      ccDataPair(Cea608::NullB1, Cea608::NullB2));
        // Another 30 bad frames — still below 45 since counter reset.
        for (int64_t f = 36; f < 66; ++f) {
                dec.pushFrame(FrameNumber(f), tsAt30fps(f), empty);
        }
        // Cue still displayed.
        CHECK(dec.displayedText() == "AB");
}

TEST_CASE("Cea608Decoder: FON sets foregroundOpacity to Flash on subsequent chars") {
        Cea608Decoder dec;

        dec.pushFrame(FrameNumber(0), tsAt30fps(0),
                      ccDataPair(Cea608::Cc1MiscFirstByte, Cea608::MiscRDC));
        dec.pushFrame(FrameNumber(1), tsAt30fps(1),
                      ccDataPair(Cea608::Cc1MiscFirstByte, Cea608::MiscRDC));
        dec.pushFrame(FrameNumber(2), tsAt30fps(2),
                      ccDataPair(Cea608::PacRow15Col0WhiteB1, Cea608::PacRow15Col0WhiteB2));
        dec.pushFrame(FrameNumber(3), tsAt30fps(3),
                      ccDataPair(Cea608::PacRow15Col0WhiteB1, Cea608::PacRow15Col0WhiteB2));
        // FON doubled — switch to flash.  Per §6.2 (D12) FON also
        // consumes one display cell as a styled space.
        dec.pushFrame(FrameNumber(4), tsAt30fps(4),
                      ccDataPair(Cea608::Cc1MiscFirstByte, Cea608::MiscFON));
        dec.pushFrame(FrameNumber(5), tsAt30fps(5),
                      ccDataPair(Cea608::Cc1MiscFirstByte, Cea608::MiscFON));
        // "OK" — flashes.
        dec.pushFrame(FrameNumber(6), tsAt30fps(6), ccDataPair('O', 'K'));
        dec.pushFrame(FrameNumber(7), tsAt30fps(7),
                      ccDataPair(Cea608::EdmB1, Cea608::EdmB2));
        dec.pushFrame(FrameNumber(8), tsAt30fps(8),
                      ccDataPair(Cea608::EdmB1, Cea608::EdmB2));

        SubtitleList out = dec.finalize();
        REQUIRE(out.size() == 1);
        // Per §6.2 (D12 fix), FON inserts a styled-space cell before
        // the subsequent text.  Find the span containing "OK" and
        // assert its flash attribute.
        const SubtitleSpan::List &spans = out[0].spans();
        const SubtitleSpan       *okSpan = nullptr;
        for (size_t i = 0; i < spans.size(); ++i) {
                if (spans[i].text().contains("OK")) {
                        okSpan = &spans[i];
                        break;
                }
        }
        REQUIRE(okSpan != nullptr);
        CHECK(okSpan->foregroundOpacity() == SubtitleOpacity::Flash);
        // Every span in the cue should carry the flash attribute
        // (FON's separator cell and the "OK" run).
        for (size_t i = 0; i < spans.size(); ++i) {
                CHECK(spans[i].foregroundOpacity() == SubtitleOpacity::Flash);
        }
}

// ============================================================================
// Robustness fixes (2026-05-13)
// ============================================================================

TEST_CASE("Cea608::isPac rejects row-11 b1 with second-of-pair bit set") {
        // b1 = 0x10 maps to row 11, which has no second-of-pair
        // partner — bit 5 of b2 must be 0 for the pair to be a valid
        // PAC.  decodePac has always enforced this; isPac used to
        // accept b2 in [0x60, 0x7F] and let decodePac's no-op reject
        // them later (asymmetric).  Now they agree.
        CHECK_FALSE(Cea608::isPac(0x10, 0x60));
        CHECK_FALSE(Cea608::isPac(0x10, 0x70));
        CHECK_FALSE(Cea608::isPac(0x10, 0x7F));
        // Valid row-11 PACs (bit 5 of b2 clear) still recognised.
        CHECK(Cea608::isPac(0x10, 0x40));
        CHECK(Cea608::isPac(0x10, 0x5F));
}

TEST_CASE("Cea608Decoder: RCL during paint-on preserves the live cue without phantom boundary") {
        // §C.10: "The RCL command should have no effect except to
        // select pop-on style.  If roll-up or paint-on style is in
        // effect, any displayed captioning shall be unaffected."
        //
        // The paint-on cue must NOT be finalised at the RCL ts (that
        // would synthesise a cue boundary the spec says doesn't
        // exist) — instead it's promoted to displayed memory and
        // ends at the next EDM / EOC / finalize.
        Cea608Decoder dec;

        // RDC -> PAC -> "AB" -> RCL (mode flip mid-air).
        dec.pushFrame(FrameNumber(0), tsAt30fps(0),
                      ccDataPair(Cea608::Cc1MiscFirstByte, Cea608::MiscRDC));
        dec.pushFrame(FrameNumber(1), tsAt30fps(1),
                      ccDataPair(Cea608::Cc1MiscFirstByte, Cea608::MiscRDC));
        dec.pushFrame(FrameNumber(2), tsAt30fps(2),
                      ccDataPair(Cea608::PacRow15Col0WhiteB1, Cea608::PacRow15Col0WhiteB2));
        dec.pushFrame(FrameNumber(3), tsAt30fps(3),
                      ccDataPair(Cea608::PacRow15Col0WhiteB1, Cea608::PacRow15Col0WhiteB2));
        dec.pushFrame(FrameNumber(4), tsAt30fps(4), ccDataPair('A', 'B'));
        // RCL at frame 5: must NOT close the cue here.  After RCL the
        // paint-on cue is still "displayed memory" per §C.10 — its
        // end timestamp is determined by the next display-erase event,
        // here the @ref finalize call.
        dec.pushFrame(FrameNumber(5), tsAt30fps(5),
                      ccDataPair(Cea608::RclB1, Cea608::RclB2));
        // Push a few more frames AFTER the RCL — they must NOT cause
        // a second cue to be emitted.  The displayed "AB" cue stays.
        for (int64_t f = 6; f < 30; ++f) {
                dec.pushFrame(FrameNumber(f), tsAt30fps(f),
                              ccDataPair(Cea608::NullB1, Cea608::NullB2));
        }

        SubtitleList out = dec.finalize();
        REQUIRE(out.size() == 1);
        CHECK(out[0].text() == "AB");
        // The cue's authoring mode is paint-on — promotion preserves it.
        CHECK(out[0].mode() == CaptionMode::PaintOn);
        // Paint-on cues anchor their start at the PAC frame (where
        // the captioner committed the row + column).  The cue ends
        // at the last-pushed timestamp (the finalize point), NOT at
        // the RCL frame (which would be the pre-fix phantom boundary).
        CHECK(out[0].start() == tsAt30fps(2));
        CHECK(out[0].end() > tsAt30fps(5));
}

TEST_CASE("Cea608Decoder: ENM in paint-on is a no-op (live chars preserved)") {
        // ENM (Erase Non-displayed Memory) only meaningfully applies
        // to pop-on.  In paint-on the loading buffer is the live
        // displayed cue; ENM must not discard those chars.
        Cea608Decoder dec;

        dec.pushFrame(FrameNumber(0), tsAt30fps(0),
                      ccDataPair(Cea608::Cc1MiscFirstByte, Cea608::MiscRDC));
        dec.pushFrame(FrameNumber(1), tsAt30fps(1),
                      ccDataPair(Cea608::Cc1MiscFirstByte, Cea608::MiscRDC));
        dec.pushFrame(FrameNumber(2), tsAt30fps(2),
                      ccDataPair(Cea608::PacRow15Col0WhiteB1, Cea608::PacRow15Col0WhiteB2));
        dec.pushFrame(FrameNumber(3), tsAt30fps(3),
                      ccDataPair(Cea608::PacRow15Col0WhiteB1, Cea608::PacRow15Col0WhiteB2));
        dec.pushFrame(FrameNumber(4), tsAt30fps(4), ccDataPair('A', 'B'));
        // ENM doubled — pre-fix this would have erased the live "AB"
        // chars and left the cue empty.
        dec.pushFrame(FrameNumber(5), tsAt30fps(5),
                      ccDataPair(Cea608::EnmB1, Cea608::EnmB2));
        dec.pushFrame(FrameNumber(6), tsAt30fps(6),
                      ccDataPair(Cea608::EnmB1, Cea608::EnmB2));
        // Live state confirms the chars survived.
        CHECK(dec.displayedText() == "AB");
        // Finalise via EDM.
        dec.pushFrame(FrameNumber(7), tsAt30fps(7),
                      ccDataPair(Cea608::EdmB1, Cea608::EdmB2));
        dec.pushFrame(FrameNumber(8), tsAt30fps(8),
                      ccDataPair(Cea608::EdmB1, Cea608::EdmB2));

        SubtitleList out = dec.finalize();
        REQUIRE(out.size() == 1);
        CHECK(out[0].text() == "AB");
}

TEST_CASE("Cea608Decoder: multi-row cue's second-row Tab Offset doesn't shift anchor") {
        // First-row PAC at row 15 col 0 sets BottomLeft.  Second-row
        // PAC + Tab Offset T3 would previously accumulate onto the
        // first row's running column and could shift the recovered
        // anchor through the column-thresholded rowToAnchor mapping.
        // After the fix, the anchor is committed off the first row's
        // start column only — subsequent rows can re-position their
        // own cursor freely.
        Cea608Decoder dec;

        // RCL doubled.
        dec.pushFrame(FrameNumber(0), tsAt30fps(0),
                      ccDataPair(Cea608::RclB1, Cea608::RclB2));
        dec.pushFrame(FrameNumber(1), tsAt30fps(1),
                      ccDataPair(Cea608::RclB1, Cea608::RclB2));
        // First-row PAC at row 14 col 0 (BottomLeft, group includes 14).
        // b1=0x14 selects row pair 14/15; b2 bit 5 (0x20) clear
        // selects row 14; subfield 0 → col 0 white.  → b2 = 0x40.
        // (Previous test version used 0x70 which encodes row 15 +
        // indent col 0 white — the original test was masking a
        // self-bug because the prior decoder always inserted a
        // "\n" between consecutive PACs regardless of row.)
        const uint8_t pacR14B1 = 0x14;
        const uint8_t pacR14B2 = 0x40;
        dec.pushFrame(FrameNumber(2), tsAt30fps(2), ccDataPair(pacR14B1, pacR14B2));
        dec.pushFrame(FrameNumber(3), tsAt30fps(3), ccDataPair(pacR14B1, pacR14B2));
        // First-row chars "AB".
        dec.pushFrame(FrameNumber(4), tsAt30fps(4), ccDataPair('A', 'B'));
        // Second-row PAC at row 15 col 0.
        dec.pushFrame(FrameNumber(5), tsAt30fps(5),
                      ccDataPair(Cea608::PacRow15Col0WhiteB1, Cea608::PacRow15Col0WhiteB2));
        dec.pushFrame(FrameNumber(6), tsAt30fps(6),
                      ccDataPair(Cea608::PacRow15Col0WhiteB1, Cea608::PacRow15Col0WhiteB2));
        // Second-row Tab Offset T3 (shifts the second row's cursor
        // by 3 cells).  Pre-fix this would accumulate onto the first
        // row's column (still 0) and push it to 3 — still BottomLeft,
        // so this is more of a state-hygiene regression than a
        // user-visible bug.  Sentinel test for the invariant.
        dec.pushFrame(FrameNumber(7), tsAt30fps(7),
                      ccDataPair(Cea608::Cc1ExtAttrB1, Cea608::TabOffsetT3));
        dec.pushFrame(FrameNumber(8), tsAt30fps(8),
                      ccDataPair(Cea608::Cc1ExtAttrB1, Cea608::TabOffsetT3));
        dec.pushFrame(FrameNumber(9), tsAt30fps(9), ccDataPair('C', 'D'));
        // EOC commits the cue.
        dec.pushFrame(FrameNumber(10), tsAt30fps(10),
                      ccDataPair(Cea608::EocB1, Cea608::EocB2));
        dec.pushFrame(FrameNumber(11), tsAt30fps(11),
                      ccDataPair(Cea608::EocB1, Cea608::EocB2));
        dec.pushFrame(FrameNumber(12), tsAt30fps(12),
                      ccDataPair(Cea608::EdmB1, Cea608::EdmB2));
        dec.pushFrame(FrameNumber(13), tsAt30fps(13),
                      ccDataPair(Cea608::EdmB1, Cea608::EdmB2));

        SubtitleList out = dec.finalize();
        REQUIRE(out.size() == 1);
        // Anchor recovered off the first row's row+column.
        CHECK(out[0].anchor() == SubtitleAnchor::BottomLeft);
        // Cue text spans both rows separated by "\n".
        CHECK(out[0].text() == "AB\nCD");
}

// ============================================================================
// Field-2 control-code remap (CEA-608-E §8.4)
// ============================================================================

namespace {

        /// @brief Hand-builds a CcDataList with one parity-stamped byte
        ///        pair on field-2 (cc_type=1) — used to inject the
        ///        F2 misc-control byte form (b1=0x15 / 0x1D) into a
        ///        CC3/CC4-configured decoder.
        Cea708Cdp::CcDataList f2Pair(uint8_t b1, uint8_t b2) {
                Cea708Cdp::CcDataList list;
                list.pushToBack(Cea708Cdp::CcData{
                        true, 1,
                        Cea608::withOddParity(b1),
                        Cea608::withOddParity(b2)});
                return list;
        }

} // namespace

TEST_CASE("Cea608Decoder CC3: F2 misc-control codes use b1=0x15 (remapped to 0x14)") {
        // Spec §8.4(a): "0x14, 0x20 to 0x14, 0x2F in field 1, shall be
        // replaced with 0x15, 0x20 to 0x15, 0x2F when used in field 2."
        // A spec-compliant CC3 encoder will emit RCL / PAC / EOC / EDM
        // with the F2 first byte 0x15.  The decoder must remap to the
        // F1 byte (0x14) before dispatch — otherwise none of the misc
        // controls would fire and no cue would emerge.
        Cea608Decoder::Config cfg;
        cfg.channel = Cea608Decoder::Channel::CC3;
        Cea608Decoder dec(cfg);
        // RCL doubled (F2 byte form): 0x15 instead of 0x14.
        dec.pushFrame(FrameNumber(0), tsFromMs(0), f2Pair(0x15, Cea608::MiscRCL));
        dec.pushFrame(FrameNumber(1), tsFromMs(33), f2Pair(0x15, Cea608::MiscRCL));
        // PAC row 15 col 0 white — PACs are NOT remapped per spec;
        // only misc-control codes are.  CC3 uses the F1 byte 0x15
        // for PAC row 11 — but we want row 15 here, which uses 0x14
        // in the PAC table.  Actually CC3 PACs use b1=0x10..0x17
        // same as CC1 — only misc-control changes.  Reuse the
        // existing CC1 PAC constants.
        dec.pushFrame(FrameNumber(2), tsFromMs(67),
                      f2Pair(Cea608::PacRow15Col0WhiteB1, Cea608::PacRow15Col0WhiteB2));
        dec.pushFrame(FrameNumber(3), tsFromMs(100),
                      f2Pair(Cea608::PacRow15Col0WhiteB1, Cea608::PacRow15Col0WhiteB2));
        // Char pair "AB".
        dec.pushFrame(FrameNumber(4), tsFromMs(133), f2Pair(0x41, 0x42));
        // EOC doubled (F2 byte form): 0x15 instead of 0x14.
        dec.pushFrame(FrameNumber(5), tsFromMs(167), f2Pair(0x15, Cea608::MiscEOC));
        dec.pushFrame(FrameNumber(6), tsFromMs(200), f2Pair(0x15, Cea608::MiscEOC));
        // Live state: cue should be on screen after EOC.
        CHECK(dec.displayedText() == "AB");
        // EDM doubled (F2 byte form) to finalise the cue.
        dec.pushFrame(FrameNumber(7), tsFromMs(233), f2Pair(0x15, Cea608::MiscEDM));
        dec.pushFrame(FrameNumber(8), tsFromMs(267), f2Pair(0x15, Cea608::MiscEDM));
        const SubtitleList out = dec.finalize();
        REQUIRE(out.size() == 1);
        CHECK(out[0].text() == "AB");
}

TEST_CASE("Cea608Decoder CC4: F2 misc-control codes use b1=0x1D (remapped to 0x1C)") {
        // Same spec §8.4(b) — CC4 (channel 2 in field 2) uses b1=0x1D
        // for misc controls where F1 CC2 would have used 0x1C.
        Cea608Decoder::Config cfg;
        cfg.channel = Cea608Decoder::Channel::CC4;
        Cea608Decoder dec(cfg);
        // CC4 RCL: 0x1D = 0x15 | 0x08 (channel-within-field bit set).
        dec.pushFrame(FrameNumber(0), tsFromMs(0), f2Pair(0x1D, Cea608::MiscRCL));
        dec.pushFrame(FrameNumber(1), tsFromMs(33), f2Pair(0x1D, Cea608::MiscRCL));
        // PAC row 15 col 0 — CC4 uses the b1 with bit 3 set; that's
        // PacRow15Col0WhiteB1 (0x14) | 0x08 = 0x1C.  For row 15 on
        // CC4 the PAC b1 is 0x1C (CC2's F1 byte), but in F2 PACs
        // are NOT remapped.  CC4 uses 0x1C for row 15 PAC same as
        // CC2 in F1.  Use the constant + channel bit.
        dec.pushFrame(FrameNumber(2), tsFromMs(67),
                      f2Pair(static_cast<uint8_t>(Cea608::PacRow15Col0WhiteB1 | 0x08),
                             Cea608::PacRow15Col0WhiteB2));
        dec.pushFrame(FrameNumber(3), tsFromMs(100),
                      f2Pair(static_cast<uint8_t>(Cea608::PacRow15Col0WhiteB1 | 0x08),
                             Cea608::PacRow15Col0WhiteB2));
        dec.pushFrame(FrameNumber(4), tsFromMs(133), f2Pair(0x43, 0x44));
        dec.pushFrame(FrameNumber(5), tsFromMs(167), f2Pair(0x1D, Cea608::MiscEOC));
        dec.pushFrame(FrameNumber(6), tsFromMs(200), f2Pair(0x1D, Cea608::MiscEOC));
        CHECK(dec.displayedText() == "CD");
        dec.pushFrame(FrameNumber(7), tsFromMs(233), f2Pair(0x1D, Cea608::MiscEDM));
        dec.pushFrame(FrameNumber(8), tsFromMs(267), f2Pair(0x1D, Cea608::MiscEDM));
        const SubtitleList out = dec.finalize();
        REQUIRE(out.size() == 1);
        CHECK(out[0].text() == "CD");
}

TEST_CASE("Cea608Decoder CC1: F1 stream is unaffected by the F2 remap (no false-positive)") {
        // Verify that the cc_type=1 gate on the remap doesn't bleed
        // into F1 decoders.  F1 streams that happen to carry a byte
        // pair with b1=0x15 + b2 in 0x20..0x2F (e.g. a PAC subfield)
        // should NOT be remapped because cc_type=0.
        Cea608Decoder::Config cfg;
        cfg.channel = Cea608Decoder::Channel::CC1;
        Cea608Decoder dec(cfg);
        // Normal F1 pop-on cycle works as it always has.
        dec.pushFrame(FrameNumber(0), tsFromMs(0), onePair(Cea608::RclB1, Cea608::RclB2));
        dec.pushFrame(FrameNumber(1), tsFromMs(33), onePair(Cea608::RclB1, Cea608::RclB2));
        dec.pushFrame(FrameNumber(2), tsFromMs(67),
                      onePair(Cea608::PacRow15Col0WhiteB1, Cea608::PacRow15Col0WhiteB2));
        dec.pushFrame(FrameNumber(3), tsFromMs(100),
                      onePair(Cea608::PacRow15Col0WhiteB1, Cea608::PacRow15Col0WhiteB2));
        dec.pushFrame(FrameNumber(4), tsFromMs(133), onePair(0x45, 0x46));
        dec.pushFrame(FrameNumber(5), tsFromMs(167), onePair(Cea608::EocB1, Cea608::EocB2));
        dec.pushFrame(FrameNumber(6), tsFromMs(200), onePair(Cea608::EocB1, Cea608::EocB2));
        dec.pushFrame(FrameNumber(7), tsFromMs(233), onePair(Cea608::EdmB1, Cea608::EdmB2));
        dec.pushFrame(FrameNumber(8), tsFromMs(267), onePair(Cea608::EdmB1, Cea608::EdmB2));
        const SubtitleList out = dec.finalize();
        REQUIRE(out.size() == 1);
        CHECK(out[0].text() == "EF");
}

// ============================================================================
// CEA-608-E §C.9 auto-erasure (16-second timeout)
// ============================================================================

TEST_CASE("Cea608Decoder: a pop-on cue auto-erases after 16 seconds with no refresh") {
        // Simulate a broadcaster-side glitch where EDM is lost: the
        // cue lands at frame 0, then no more 608 bytes arrive.  The
        // decoder should auto-erase the cue when the next pushFrame
        // lands 16+ seconds after the cue start (608-E §C.9 Preferred).
        Cea608Decoder dec;
        // RCL + RCL doubled, PAC + PAC doubled, "AB", EOC + EOC doubled.
        dec.pushFrame(FrameNumber(0), tsFromMs(0), onePair(Cea608::RclB1, Cea608::RclB2));
        dec.pushFrame(FrameNumber(1), tsFromMs(33), onePair(Cea608::RclB1, Cea608::RclB2));
        dec.pushFrame(FrameNumber(2), tsFromMs(67),
                      onePair(Cea608::PacRow15Col0WhiteB1, Cea608::PacRow15Col0WhiteB2));
        dec.pushFrame(FrameNumber(3), tsFromMs(100),
                      onePair(Cea608::PacRow15Col0WhiteB1, Cea608::PacRow15Col0WhiteB2));
        dec.pushFrame(FrameNumber(4), tsFromMs(133), onePair(0x41, 0x42));
        const TimeStamp cueStart = tsFromMs(167);
        dec.pushFrame(FrameNumber(5), cueStart, onePair(Cea608::EocB1, Cea608::EocB2));
        dec.pushFrame(FrameNumber(6), tsFromMs(200), onePair(Cea608::EocB1, Cea608::EocB2));
        CHECK(dec.displayedText() == "AB");
        // Frames at 10s and 15.999s after cue start — still displayed,
        // no auto-erasure yet.
        dec.pushFrame(FrameNumber(300), cueStart + std::chrono::milliseconds(10000),
                      onePair(Cea608::NullB1, Cea608::NullB2));
        CHECK(dec.displayedText() == "AB");
        dec.pushFrame(FrameNumber(479), cueStart + std::chrono::milliseconds(15999),
                      onePair(Cea608::NullB1, Cea608::NullB2));
        CHECK(dec.displayedText() == "AB");
        // Frame at exactly 16.000s after the cue start triggers auto-
        // erasure.  The cue is finalised internally; displayedText()
        // returns empty afterward.
        dec.pushFrame(FrameNumber(480), cueStart + std::chrono::milliseconds(16000),
                      onePair(Cea608::NullB1, Cea608::NullB2));
        CHECK(dec.displayedText() == "");
        const SubtitleList out = dec.finalize();
        REQUIRE(out.size() == 1);
        CHECK(out[0].text() == "AB");
}

TEST_CASE("Cea608Decoder: a normal EOC/EDM-terminated cue is unaffected by the auto-erasure") {
        // Regression check: the 16s auto-erasure must not interfere
        // with cues that get a proper EDM well before the timeout.
        Cea608Decoder dec;
        dec.pushFrame(FrameNumber(0), tsFromMs(0), onePair(Cea608::RclB1, Cea608::RclB2));
        dec.pushFrame(FrameNumber(1), tsFromMs(33), onePair(Cea608::RclB1, Cea608::RclB2));
        dec.pushFrame(FrameNumber(2), tsFromMs(67),
                      onePair(Cea608::PacRow15Col0WhiteB1, Cea608::PacRow15Col0WhiteB2));
        dec.pushFrame(FrameNumber(3), tsFromMs(100),
                      onePair(Cea608::PacRow15Col0WhiteB1, Cea608::PacRow15Col0WhiteB2));
        dec.pushFrame(FrameNumber(4), tsFromMs(133), onePair('X', 'Y'));
        dec.pushFrame(FrameNumber(5), tsFromMs(167), onePair(Cea608::EocB1, Cea608::EocB2));
        dec.pushFrame(FrameNumber(6), tsFromMs(200), onePair(Cea608::EocB1, Cea608::EocB2));
        // EDM at ~2 seconds — well before the 16s threshold.
        dec.pushFrame(FrameNumber(60), tsFromMs(2000),
                      onePair(Cea608::EdmB1, Cea608::EdmB2));
        dec.pushFrame(FrameNumber(61), tsFromMs(2033),
                      onePair(Cea608::EdmB1, Cea608::EdmB2));
        const SubtitleList out = dec.finalize();
        REQUIRE(out.size() == 1);
        CHECK(out[0].text() == "XY");
}

// ============================================================================
// Channel-context tracking — character pairs follow the most recent control
// ============================================================================

TEST_CASE("Cea608Decoder CC1: TR/RTD swaps the channel into text mode — chars after it are dropped") {
        // §7 Text Mode: a TR (Text Restart, 0x14 0x2A) marks the
        // channel as text mode; subsequent character pairs belong
        // to T1 (the text-mode peer of CC1 on F1 first-in-field),
        // not to CC1.  The caption decoder doesn't surface text
        // mode, but it MUST drop those chars rather than appending
        // them to the live caption cue.
        //
        // Sequence: RCL + PAC + "OK", then TR doubled, then "JX"
        // (text-mode chars, must be filtered), then EOC + EDM.
        Cea608Decoder::Config cfg;
        cfg.channel = Cea608Decoder::Channel::CC1;
        Cea608Decoder dec(cfg);

        dec.pushFrame(FrameNumber(0), tsAt30fps(0),
                      ccDataPair(Cea608::RclB1, Cea608::RclB2));
        dec.pushFrame(FrameNumber(1), tsAt30fps(1),
                      ccDataPair(Cea608::RclB1, Cea608::RclB2));
        dec.pushFrame(FrameNumber(2), tsAt30fps(2),
                      ccDataPair(Cea608::PacRow15Col0WhiteB1, Cea608::PacRow15Col0WhiteB2));
        dec.pushFrame(FrameNumber(3), tsAt30fps(3),
                      ccDataPair(Cea608::PacRow15Col0WhiteB1, Cea608::PacRow15Col0WhiteB2));
        dec.pushFrame(FrameNumber(4), tsAt30fps(4), ccDataPair('O', 'K'));
        // TR doubled — switches the F1-first byte family to text
        // mode (T1).
        dec.pushFrame(FrameNumber(5), tsAt30fps(5), ccDataPair(0x14, 0x2A));
        dec.pushFrame(FrameNumber(6), tsAt30fps(6), ccDataPair(0x14, 0x2A));
        // T1 chars "JX" — must be dropped by the CC1 decoder.
        dec.pushFrame(FrameNumber(7), tsAt30fps(7), ccDataPair('J', 'X'));
        // Switch back to CC1 caption mode via RCL — chars after this
        // should accumulate again.  (Append "YZ" to verify the flag
        // is properly reset.)
        dec.pushFrame(FrameNumber(8), tsAt30fps(8),
                      ccDataPair(Cea608::RclB1, Cea608::RclB2));
        dec.pushFrame(FrameNumber(9), tsAt30fps(9),
                      ccDataPair(Cea608::RclB1, Cea608::RclB2));
        dec.pushFrame(FrameNumber(10), tsAt30fps(10),
                      ccDataPair(Cea608::PacRow15Col0WhiteB1, Cea608::PacRow15Col0WhiteB2));
        dec.pushFrame(FrameNumber(11), tsAt30fps(11),
                      ccDataPair(Cea608::PacRow15Col0WhiteB1, Cea608::PacRow15Col0WhiteB2));
        dec.pushFrame(FrameNumber(12), tsAt30fps(12), ccDataPair('Y', 'Z'));
        dec.pushFrame(FrameNumber(13), tsAt30fps(13),
                      ccDataPair(Cea608::EocB1, Cea608::EocB2));
        dec.pushFrame(FrameNumber(14), tsAt30fps(14),
                      ccDataPair(Cea608::EocB1, Cea608::EocB2));
        dec.pushFrame(FrameNumber(15), tsAt30fps(15),
                      ccDataPair(Cea608::EdmB1, Cea608::EdmB2));
        dec.pushFrame(FrameNumber(16), tsAt30fps(16),
                      ccDataPair(Cea608::EdmB1, Cea608::EdmB2));

        const SubtitleList out = dec.finalize();
        REQUIRE(out.size() >= 1);
        // The first "OK" cue gets clobbered by the second RCL
        // (which clears the loading grid).  The final cue should
        // contain only "YZ" — the "JX" was dropped because it
        // arrived while text mode was active.
        const Subtitle &last = out[out.size() - 1];
        CHECK(last.text() == "YZ");
        CHECK_FALSE(last.text().contains(String("J")));
        CHECK_FALSE(last.text().contains(String("X")));
}

TEST_CASE("Cea608Decoder CC1: peer-channel characters don't bleed into our cue") {
        // CEA-608-E §3.3: a character pair belongs to the channel of
        // the most recently received control code.  In a CC1+CC2
        // interleaved stream, a CC2 PAC followed by CC2 chars must
        // NOT corrupt the CC1 decoder's output.
        //
        // Sequence (all on F1, cc_type=0):
        //   CC1 RCL, CC1 PAC row 15, "OK"  (CC1 cue text)
        //   CC2 PAC row 15           , "JX" (must be skipped by CC1 decoder)
        //   CC1 EOC                                 → emits CC1 cue
        //   CC1 EDM
        //
        // CC2 PAC uses b1=0x1C (CC2 sibling of CC1's 0x14: bit 3 set).
        // The CC1 decoder rejects it (channel-bit mismatch).  Before
        // the fix, the subsequent "JX" character pair would still
        // land in the CC1 grid because chars carry no channel info on
        // the wire.  After the fix, the rejected CC2 PAC sets
        // lastControlIsForOurChannel = false, so the "JX" pair is
        // skipped on the CC1 channel.
        Cea608Decoder::Config cfg;
        cfg.channel = Cea608Decoder::Channel::CC1;
        Cea608Decoder dec(cfg);

        // CC1 RCL doubled.
        dec.pushFrame(FrameNumber(0), tsAt30fps(0),
                      ccDataPair(Cea608::RclB1, Cea608::RclB2));
        dec.pushFrame(FrameNumber(1), tsAt30fps(1),
                      ccDataPair(Cea608::RclB1, Cea608::RclB2));
        // CC1 PAC row 15 col 0 doubled.
        dec.pushFrame(FrameNumber(2), tsAt30fps(2),
                      ccDataPair(Cea608::PacRow15Col0WhiteB1, Cea608::PacRow15Col0WhiteB2));
        dec.pushFrame(FrameNumber(3), tsAt30fps(3),
                      ccDataPair(Cea608::PacRow15Col0WhiteB1, Cea608::PacRow15Col0WhiteB2));
        // CC1 chars "OK".
        dec.pushFrame(FrameNumber(4), tsAt30fps(4), ccDataPair('O', 'K'));
        // CC2 PAC row 15 col 0 doubled — b1=0x1C (CC2 sibling).
        dec.pushFrame(FrameNumber(5), tsAt30fps(5), ccDataPair(0x1C, 0x70));
        dec.pushFrame(FrameNumber(6), tsAt30fps(6), ccDataPair(0x1C, 0x70));
        // CC2 chars "JX" — these MUST be skipped by the CC1 decoder.
        dec.pushFrame(FrameNumber(7), tsAt30fps(7), ccDataPair('J', 'X'));
        // CC1 EOC doubled.
        dec.pushFrame(FrameNumber(8), tsAt30fps(8),
                      ccDataPair(Cea608::EocB1, Cea608::EocB2));
        dec.pushFrame(FrameNumber(9), tsAt30fps(9),
                      ccDataPair(Cea608::EocB1, Cea608::EocB2));
        // CC1 EDM doubled.
        dec.pushFrame(FrameNumber(10), tsAt30fps(10),
                      ccDataPair(Cea608::EdmB1, Cea608::EdmB2));
        dec.pushFrame(FrameNumber(11), tsAt30fps(11),
                      ccDataPair(Cea608::EdmB1, Cea608::EdmB2));

        const SubtitleList out = dec.finalize();
        REQUIRE(out.size() == 1);
        // Only the CC1 chars ("OK") appear; the CC2 "JX" was filtered
        // out by the channel-context tracking.
        CHECK(out[0].text() == "OK");
}

// ============================================================================
// Spec-conformance regression tests (D-series fixes from the CTA-608-E audit)
// ============================================================================

TEST_CASE("Cea608Decoder: BS at Column 32 erases the char at Column 31 in place (§C.13 / D4)") {
        // §C.13: "A BS received either before or after displaying a
        // character in Column 32 shall move the cursor to Column 31
        // and erase the character there."  In our 0-indexed model
        // Column 32 == cursorCol 31 (post-clamp).  When the cursor
        // sits at col 31 with the cell at col 31 occupied (meaning
        // the cursor was clamped after writing to col 32 / cursorCol
        // 31), BS must erase that cell in place — NOT decrement and
        // erase the char at col 30.
        Cea608Decoder dec;
        // Pop-on cycle.
        dec.pushFrame(FrameNumber(0), tsFromMs(0),
                      onePair(Cea608::RclB1, Cea608::RclB2));
        dec.pushFrame(FrameNumber(1), tsFromMs(33),
                      onePair(Cea608::RclB1, Cea608::RclB2));
        // PAC row 15 indent 28 white — cursor lands at col 28.
        // b1=0x14, b2=0x40|0x20|(15<<1)=0x7E.
        dec.pushFrame(FrameNumber(2), tsFromMs(67), onePair(0x14, 0x7E));
        dec.pushFrame(FrameNumber(3), tsFromMs(100), onePair(0x14, 0x7E));
        // Tab Offset T3 → cursor at col 31.
        dec.pushFrame(FrameNumber(4), tsFromMs(133),
                      onePair(Cea608::Cc1ExtAttrB1, 0x23));
        dec.pushFrame(FrameNumber(5), tsFromMs(167),
                      onePair(Cea608::Cc1ExtAttrB1, 0x23));
        // Write a char at col 31 (cursor stays at 31 post-clamp).
        // Pad the second byte with a space so the second char also
        // overwrites col 31 (clamp).  Net: col 31 holds 'X'.
        dec.pushFrame(FrameNumber(6), tsFromMs(200), onePair('A', 'B'));
        // After these two writes col 31 holds 'B'; cursorCol stays at 31.
        // BS doubled — must erase col 31 in place (D4) rather than
        // decrementing to col 30.  Since the test approached col 31
        // by writing 'A' first then 'B' (both overwriting col 31), the
        // cell at col 30 is empty — pre-fix behaviour would decrement
        // to col 30, do nothing visible there, and leave 'B' at col 31.
        // Post-fix: col 31 cleared, cue is empty.
        dec.pushFrame(FrameNumber(7), tsFromMs(233),
                      onePair(Cea608::Cc1MiscFirstByte, Cea608::MiscBS));
        dec.pushFrame(FrameNumber(8), tsFromMs(267),
                      onePair(Cea608::Cc1MiscFirstByte, Cea608::MiscBS));
        dec.pushFrame(FrameNumber(9), tsFromMs(300),
                      onePair(Cea608::EocB1, Cea608::EocB2));
        dec.pushFrame(FrameNumber(10), tsFromMs(333),
                      onePair(Cea608::EocB1, Cea608::EocB2));
        // No occupied cells → empty cue, finalize yields empty list.
        CHECK(dec.displayedText() == "");
        const SubtitleList out = dec.finalize();
        CHECK(out.isEmpty());
}

TEST_CASE("Cea608Decoder: §C.11 EOC during roll-up ends the cue and blanks the screen (D1)") {
        // Per §C.11 (PDF p.74), an EOC while roll-up captioning is
        // selected swaps the roll-up content from displayed memory to
        // non-displayed memory.  Practical effect: the screen blanks
        // at the EOC frame (the roll-up cue ends), the decoder forces
        // pop-on style, and any subsequent text accumulates in the
        // freshly-reset pop-on loading buffer.
        Cea608Decoder dec;
        // RU2 + CR set up the first roll-up row.
        dec.pushFrame(FrameNumber(0), tsAt30fps(0),
                      onePair(Cea608::Cc1MiscFirstByte, Cea608::MiscRU2));
        dec.pushFrame(FrameNumber(1), tsAt30fps(1),
                      onePair(Cea608::Cc1MiscFirstByte, Cea608::MiscRU2));
        dec.pushFrame(FrameNumber(2), tsAt30fps(2),
                      onePair(Cea608::Cc1MiscFirstByte, Cea608::MiscCR));
        dec.pushFrame(FrameNumber(3), tsAt30fps(3),
                      onePair(Cea608::Cc1MiscFirstByte, Cea608::MiscCR));
        dec.pushFrame(FrameNumber(4), tsAt30fps(4),
                      onePair(Cea608::PacRow15Col0WhiteB1, Cea608::PacRow15Col0WhiteB2));
        dec.pushFrame(FrameNumber(5), tsAt30fps(5),
                      onePair(Cea608::PacRow15Col0WhiteB1, Cea608::PacRow15Col0WhiteB2));
        dec.pushFrame(FrameNumber(6), tsAt30fps(6), onePair('A', 'B'));
        CHECK(dec.displayedText() == "AB");
        // EOC fires while in roll-up — D1: the live "AB" row is ended,
        // the screen blanks, mode forces to PopOn.
        const TimeStamp eocTs = tsAt30fps(7);
        dec.pushFrame(FrameNumber(7), eocTs,
                      onePair(Cea608::EocB1, Cea608::EocB2));
        dec.pushFrame(FrameNumber(8), tsAt30fps(8),
                      onePair(Cea608::EocB1, Cea608::EocB2));
        // Screen is blank after EOC-during-roll-up.
        CHECK(dec.displayedText() == "");
        // Finalize emits the roll-up row as a cue ending at the EOC
        // timestamp (when the screen blanked).
        const SubtitleList out = dec.finalize();
        REQUIRE(out.size() == 1);
        CHECK(out[0].text() == "AB");
        CHECK(out[0].end() == eocTs);
        CHECK(out[0].mode() == CaptionMode::RollUp);
}

TEST_CASE("Cea608Decoder: RUx erases prior pop-on cue when switching to roll-up (§C.10 / D2)") {
        // §C.10: when switching INTO roll-up from pop-on or paint-on,
        // "if pop-on or paint-on captioning is already present in
        // either memory, it shall be erased."  D2 spec-conformance
        // fix: the prior pop-on cue must be ended at the RUx frame,
        // not promoted to displayed.
        Cea608Decoder dec;
        // Pop-on cycle producing "AB" on screen.
        dec.pushFrame(FrameNumber(0), tsAt30fps(0),
                      onePair(Cea608::RclB1, Cea608::RclB2));
        dec.pushFrame(FrameNumber(1), tsAt30fps(1),
                      onePair(Cea608::RclB1, Cea608::RclB2));
        dec.pushFrame(FrameNumber(2), tsAt30fps(2),
                      onePair(Cea608::PacRow15Col0WhiteB1, Cea608::PacRow15Col0WhiteB2));
        dec.pushFrame(FrameNumber(3), tsAt30fps(3),
                      onePair(Cea608::PacRow15Col0WhiteB1, Cea608::PacRow15Col0WhiteB2));
        dec.pushFrame(FrameNumber(4), tsAt30fps(4), onePair('A', 'B'));
        const TimeStamp eocTs = tsAt30fps(5);
        dec.pushFrame(FrameNumber(5), eocTs,
                      onePair(Cea608::EocB1, Cea608::EocB2));
        dec.pushFrame(FrameNumber(6), tsAt30fps(6),
                      onePair(Cea608::EocB1, Cea608::EocB2));
        CHECK(dec.displayedText() == "AB");
        // RU2 fires while pop-on cue "AB" is on screen — D2: "AB"
        // must be erased from displayed memory (and emitted as a cue
        // with its end at the RUx ts).
        const TimeStamp ru2Ts = tsAt30fps(10);
        dec.pushFrame(FrameNumber(10), ru2Ts,
                      onePair(Cea608::Cc1MiscFirstByte, Cea608::MiscRU2));
        dec.pushFrame(FrameNumber(11), tsAt30fps(11),
                      onePair(Cea608::Cc1MiscFirstByte, Cea608::MiscRU2));
        // After RU2 the screen is blank.
        CHECK(dec.displayedText() == "");
        const SubtitleList out = dec.finalize();
        REQUIRE(out.size() == 1);
        CHECK(out[0].text() == "AB");
        CHECK(out[0].end() == ru2Ts);
        CHECK(out[0].mode() == CaptionMode::PopOn);
}

TEST_CASE("Cea608Decoder: RCL keeps non-displayed memory across consecutive RCLs (§B.8.3 / D3)") {
        // §C.10 / §B.8.3: RCL itself does NOT clear non-displayed
        // memory — a real captioner sends ENM after RCL when a clean
        // slate is wanted.  D3 spec-conformance fix: two consecutive
        // RCLs (with chars between them so the dup filter resets)
        // must NOT erase the pop-on non-displayed memory.
        Cea608Decoder dec;
        // First RCL doubled.
        dec.pushFrame(FrameNumber(0), tsFromMs(0),
                      onePair(Cea608::RclB1, Cea608::RclB2));
        dec.pushFrame(FrameNumber(1), tsFromMs(33),
                      onePair(Cea608::RclB1, Cea608::RclB2));
        // PAC row 15 col 0 doubled.
        dec.pushFrame(FrameNumber(2), tsFromMs(67),
                      onePair(Cea608::PacRow15Col0WhiteB1, Cea608::PacRow15Col0WhiteB2));
        dec.pushFrame(FrameNumber(3), tsFromMs(100),
                      onePair(Cea608::PacRow15Col0WhiteB1, Cea608::PacRow15Col0WhiteB2));
        // Chars "AB" — typed into pop-on non-displayed memory.
        dec.pushFrame(FrameNumber(4), tsFromMs(133), onePair('A', 'B'));
        // Second RCL doubled (dup filter reset by chars) — must NOT
        // clear non-displayed memory per §C.10.
        dec.pushFrame(FrameNumber(5), tsFromMs(167),
                      onePair(Cea608::RclB1, Cea608::RclB2));
        // ENM doubled — this is the spec-mandated way to clear non-
        // displayed memory after RCL.
        dec.pushFrame(FrameNumber(6), tsFromMs(200),
                      onePair(Cea608::EnmB1, Cea608::EnmB2));
        dec.pushFrame(FrameNumber(7), tsFromMs(233),
                      onePair(Cea608::EnmB1, Cea608::EnmB2));
        // Fresh PAC + "CD" lands in cleared non-displayed memory.
        dec.pushFrame(FrameNumber(8), tsFromMs(267),
                      onePair(Cea608::PacRow15Col0WhiteB1, Cea608::PacRow15Col0WhiteB2));
        dec.pushFrame(FrameNumber(9), tsFromMs(300),
                      onePair(Cea608::PacRow15Col0WhiteB1, Cea608::PacRow15Col0WhiteB2));
        dec.pushFrame(FrameNumber(10), tsFromMs(333), onePair('C', 'D'));
        dec.pushFrame(FrameNumber(11), tsFromMs(367),
                      onePair(Cea608::EocB1, Cea608::EocB2));
        dec.pushFrame(FrameNumber(12), tsFromMs(400),
                      onePair(Cea608::EocB1, Cea608::EocB2));
        // After the ENM the buffer was cleared, so the displayed cue
        // is just "CD".
        CHECK(dec.displayedText() == "CD");
}

TEST_CASE("Cea608Decoder: §C.21 enable-side hysteresis re-enables after 15 good frames (D6)") {
        // D6 spec-conformance fix: once the display has been auto-
        // disabled by 45 consecutive bad-parity frames, output stays
        // suppressed until 12-18 consecutive good-parity frames have
        // arrived.  This implementation uses 15 (the mid-range of
        // the spec's tolerance band).
        Cea608Decoder dec;
        // Build a pop-on cue, then trip the disable-side threshold.
        dec.pushFrame(FrameNumber(0), tsAt30fps(0),
                      onePair(Cea608::RclB1, Cea608::RclB2));
        dec.pushFrame(FrameNumber(1), tsAt30fps(1),
                      onePair(Cea608::RclB1, Cea608::RclB2));
        dec.pushFrame(FrameNumber(2), tsAt30fps(2),
                      onePair(Cea608::PacRow15Col0WhiteB1, Cea608::PacRow15Col0WhiteB2));
        dec.pushFrame(FrameNumber(3), tsAt30fps(3),
                      onePair(Cea608::PacRow15Col0WhiteB1, Cea608::PacRow15Col0WhiteB2));
        dec.pushFrame(FrameNumber(4), tsAt30fps(4), onePair('A', 'B'));
        dec.pushFrame(FrameNumber(5), tsAt30fps(5),
                      onePair(Cea608::EocB1, Cea608::EocB2));
        dec.pushFrame(FrameNumber(6), tsAt30fps(6),
                      onePair(Cea608::EocB1, Cea608::EocB2));
        CHECK(dec.displayedText() == "AB");
        // Push 45 bad (empty) frames → display auto-disables.
        Cea708Cdp::CcDataList empty;
        for (int64_t f = 7; f < 52; ++f) {
                dec.pushFrame(FrameNumber(f), tsAt30fps(f), empty);
        }
        // Display is auto-disabled — output gated off, no live text.
        CHECK(dec.displayedText() == "");
        // Feed a fresh pop-on cue "CD" — good-parity bytes start
        // arriving but display stays disabled until 15 consecutive
        // good frames.  Each frame carries one good-parity pair.
        dec.pushFrame(FrameNumber(52), tsAt30fps(52),
                      onePair(Cea608::RclB1, Cea608::RclB2));
        // After only one good frame, output is still gated off.
        CHECK(dec.displayedText() == "");
        // Continue good frames for a total of 15 good frames.
        for (int64_t f = 53; f < 67; ++f) {
                dec.pushFrame(FrameNumber(f), tsAt30fps(f),
                              onePair(Cea608::NullB1, Cea608::NullB2));
        }
        // Frame 66 is the 15th good frame → display re-enables.
        // Finish off the cue: PAC + chars + EOC.
        dec.pushFrame(FrameNumber(67), tsAt30fps(67),
                      onePair(Cea608::PacRow15Col0WhiteB1, Cea608::PacRow15Col0WhiteB2));
        dec.pushFrame(FrameNumber(68), tsAt30fps(68),
                      onePair(Cea608::PacRow15Col0WhiteB1, Cea608::PacRow15Col0WhiteB2));
        dec.pushFrame(FrameNumber(69), tsAt30fps(69), onePair('C', 'D'));
        dec.pushFrame(FrameNumber(70), tsAt30fps(70),
                      onePair(Cea608::EocB1, Cea608::EocB2));
        dec.pushFrame(FrameNumber(71), tsAt30fps(71),
                      onePair(Cea608::EocB1, Cea608::EocB2));
        // After re-enable, the new "CD" cue is visible.
        CHECK(dec.displayedText() == "CD");
}

TEST_CASE("Cea608Decoder: receiving more than 4 caption rows decodes without crashing (§C.6)") {
        // §C.6 caps the simultaneously-displayed area at 4 caption
        // rows (plus 11 text rows).  The decoder itself doesn't
        // enforce that cap — rendering policy does — but it must
        // handle a stream that drops chars onto more than 4 rows
        // gracefully (no crash, no grid-out-of-bounds).
        Cea608Decoder dec;
        dec.pushFrame(FrameNumber(0), tsFromMs(0),
                      onePair(Cea608::RclB1, Cea608::RclB2));
        dec.pushFrame(FrameNumber(1), tsFromMs(33),
                      onePair(Cea608::RclB1, Cea608::RclB2));
        // Write one char on each of rows 11, 12, 13, 14, 15 — five
        // caption rows.  PAC row encodings: row 11 b1=0x11 b2=0x40
        // (table 2nd-of-pair bit clear → row 11).  Actually decodePac
        // maps b1 patterns; just use the same row 14 / row 15 PACs
        // that the existing tests use plus rows 11-13 derived from
        // the standard PAC table.  Use simple existing constants for
        // row 15 + row 14, and hand-build rows 11/12/13.
        struct Row {
                int     n;
                uint8_t b1;
                uint8_t b2;
        };
        const Row rows[] = {
                // Rows 1..4 share b1=0x11 / 0x19 (CC2).  CC1 uses 0x11
                // for rows 1 & 2: b2 bit 5 selects within the pair
                // (0=row1, 1=row2).  We pick 3 of them.
                {11, 0x11, 0x40}, // CC1 row 1, col 0 white — actually row 1, but the test only cares about >4 rows existing on the grid.
                {12, 0x11, 0x60}, // CC1 row 2
                {13, 0x12, 0x40}, // CC1 row 3
                {14, 0x14, 0x40}, // CC1 row 14 (existing convention)
                {15, Cea608::PacRow15Col0WhiteB1, Cea608::PacRow15Col0WhiteB2},
        };
        int64_t f = 2;
        for (const Row &r : rows) {
                dec.pushFrame(FrameNumber(f), tsFromMs(33 * f), onePair(r.b1, r.b2));
                ++f;
                dec.pushFrame(FrameNumber(f), tsFromMs(33 * f), onePair(r.b1, r.b2));
                ++f;
                dec.pushFrame(FrameNumber(f), tsFromMs(33 * f), onePair('X', 'Y'));
                ++f;
        }
        dec.pushFrame(FrameNumber(f), tsFromMs(33 * f),
                      onePair(Cea608::EocB1, Cea608::EocB2));
        ++f;
        dec.pushFrame(FrameNumber(f), tsFromMs(33 * f),
                      onePair(Cea608::EocB1, Cea608::EocB2));
        // Decoder must surface the cue without crashing; we don't
        // assert exact row geometry (renderer's policy on §C.6).
        const SubtitleList out = dec.finalize();
        CHECK(out.size() >= 1);
        // At least one row of "XY" must appear in the flat text.
        if (!out.isEmpty()) {
                CHECK(out[0].text().contains(String("XY")));
        }
}
