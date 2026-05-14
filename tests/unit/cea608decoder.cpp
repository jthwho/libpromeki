/**
 * @file      cea608decoder.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
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
#include <promeki/enums.h>
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
        // The encoder pads odd-length text with space — "Hello" is 5 chars,
        // so the decoder reconstructs "Hello " (trailing space).
        CHECK(out[0].text() == "Hello ");
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
        // 13 chars → 7 char pairs → "Hello, world." + space pad on
        // odd char → "Hello, world. ".  But "Hello, world." is 13
        // chars (odd), so the encoder appends a trailing space.
        CHECK(out[0].text() == "Hello, world. ");
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
        CHECK(out[0].anchor().value() == SubtitleAnchor::TopCenter.value());
        CHECK(out[1].text() == "MIDX");
        CHECK(out[1].anchor().value() == SubtitleAnchor::MiddleCenter.value());
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
        // padded to an even count on the wire).
        SubtitleSpan::List spans;
        spans.pushToBack(SubtitleSpan("REDX", false, false, false, Color::Red));
        spans.pushToBack(SubtitleSpan("BL", false, false, false, Color::Blue));
        SubtitleList subs;
        subs.append(Subtitle(tsAt30fps(60), tsAt30fps(120), spans, SubtitleAnchor::BottomCenter,
                             Rect2Di32(), String(), Metadata()));
        REQUIRE(enc.setSubtitles(subs).isOk());

        runEncoderToDecoder(enc, dec, 140);
        SubtitleList out = dec.finalize();
        REQUIRE(out.size() == 1);
        const SubtitleSpan::List &gotSpans = out[0].spans();
        // Three spans: the two styled runs plus a neutral
        // single-space separator span injected by the decoder for
        // the mid-row code's display cell (the encoder strips one
        // leading space at MR boundaries so the styled regions
        // contain only the styled text — no one-cell bleed).
        REQUIRE(gotSpans.size() == 3);
        CHECK(gotSpans[0].text() == "REDX");
        CHECK(gotSpans[0].color() == Color::Red);
        CHECK(gotSpans[1].text() == " ");
        CHECK_FALSE(gotSpans[1].color().isValid());
        CHECK(gotSpans[2].text() == "BL");
        CHECK(gotSpans[2].color() == Color::Blue);
        // Flat text now reads as "REDX BL" thanks to the inter-run
        // separator the decoder synthesises for the MR-space cell.
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

        // Exactly one span must carry underline=true, and its text
        // must be exactly "underlined" (no bleed).  Every other
        // span must have underline=false.
        int  underlineCount  = 0;
        bool underlineExact  = false;
        for (size_t i = 0; i < gotSpans.size(); ++i) {
                if (gotSpans[i].underline()) {
                        ++underlineCount;
                        if (gotSpans[i].text() == "underlined") underlineExact = true;
                }
        }
        CHECK(underlineCount == 1);
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
        bool foundUnderlined = false;
        for (size_t i = 0; i < gotSpans.size(); ++i) {
                if (!gotSpans[i].underline()) continue;
                CHECK(gotSpans[i].text() == "underlined");
                foundUnderlined = true;
        }
        CHECK(foundUnderlined);
        // Flat text reads as the original sentence with single
        // spaces between words.  Two kinds of inter-run space
        // appear:
        //   - "and " has a trailing pad-space (the encoder padded
        //     an odd-length 3-char run to 4 bytes for the wire's
        //     byte-pair cadence); the decoder absorbs the MR-cell
        //     into that existing space, so no second separator is
        //     synthesised here.
        //   - "underlined" is even-length and has no trailing
        //     pad; the decoder synthesises a neutral " " separator
        //     for the MR cell before "mixed".
        //
        // The cue ends with a single pad-space from the encoder
        // having had to round "mixed" (5 chars) up to a 6-char
        // byte-pair count.
        CHECK(out[0].text() == "and underlined mixed ");
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
        in.append(Subtitle(tsAt30fps(30), tsAt30fps(90), "HELLO "));
        REQUIRE(enc.setSubtitles(in).isOk());

        runEncoderToDecoder(enc, dec, 110);
        SubtitleList out = dec.finalize();
        REQUIRE(out.size() == 1);
        CHECK(out[0].text() == "HELLO ");
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
        CHECK(gotSpan.backgroundOpacity().value() == SubtitleOpacity::Solid.value());
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
        CHECK(gotSpan.backgroundOpacity().value() == SubtitleOpacity::Solid.value());
}

TEST_CASE("Cea608: encode/decode Black fg silently round-trips through White on the wire") {
        // CEA-608 fg paths (PAC + mid-row) have no Black encoding —
        // a Black-fg span quantises to CaptionColor::Black at the
        // encoder, but wireStyleFor coerces it back to White so the
        // wire byte (and the recovered cue's fg) is plain White.
        // No regression in horizontal positioning either: the cue's
        // anchor is preserved (no degradation to flush-left).
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
        const SubtitleSpan &gotSpan = out[0].spans()[0];
        CHECK(gotSpan.text() == "HI");
        // FG colour: invalid (= renderer's default) or White.
        if (gotSpan.color().isValid()) {
                CHECK(gotSpan.color().r8() == 255);
                CHECK(gotSpan.color().g8() == 255);
                CHECK(gotSpan.color().b8() == 255);
        }
        // Anchor preserved (Black fg doesn't trigger position
        // degradation because Black is wire-indistinguishable from
        // White at the fg level).
        CHECK(out[0].anchor().value() == SubtitleAnchor::BottomCenter.value());
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
        // ñ (basic G0 0x7E) + ß (ExtFrench).  Letters glue them so
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

TEST_CASE("Cea608Decoder: DER drops the loading buffer's chars since the last PAC") {
        // Type "JUNKX" then DER (clears the row), then "OK" + EDM.
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
        // DER doubled.
        dec.pushFrame(FrameNumber(7), tsAt30fps(7),
                      ccDataPair(Cea608::Cc1MiscFirstByte, Cea608::MiscDER));
        dec.pushFrame(FrameNumber(8), tsAt30fps(8),
                      ccDataPair(Cea608::Cc1MiscFirstByte, Cea608::MiscDER));
        // "OK"
        dec.pushFrame(FrameNumber(9), tsAt30fps(9), ccDataPair('O', 'K'));
        dec.pushFrame(FrameNumber(10), tsAt30fps(10),
                      ccDataPair(Cea608::EdmB1, Cea608::EdmB2));
        dec.pushFrame(FrameNumber(11), tsAt30fps(11),
                      ccDataPair(Cea608::EdmB1, Cea608::EdmB2));

        SubtitleList out = dec.finalize();
        REQUIRE(out.size() == 1);
        CHECK(out[0].text() == "OK");
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
        // FON doubled — switch to flash.
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
        REQUIRE(out[0].spans().size() >= 1);
        const SubtitleSpan &span = out[0].spans()[0];
        CHECK(span.text() == "OK");
        CHECK(span.foregroundOpacity().value() == SubtitleOpacity::Flash.value());
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

TEST_CASE("Cea608Decoder: RCL during paint-on finalises the live cue") {
        // Paint-on cue is on-screen; a wild stream sends RCL to
        // switch into pop-on.  The decoder must finalise the
        // paint-on cue at the RCL ts instead of dropping it.
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
        // Wild RCL: would previously drop the paint-on cue.
        dec.pushFrame(FrameNumber(5), tsAt30fps(5),
                      ccDataPair(Cea608::RclB1, Cea608::RclB2));

        SubtitleList out = dec.finalize();
        REQUIRE(out.size() == 1);
        CHECK(out[0].text() == "AB");
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
        const uint8_t pacR14B1 = 0x14;
        const uint8_t pacR14B2 = 0x70; // row 14 / col 0 / white.
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
                      ccDataPair(Cea608::TabOffsetB1, Cea608::TabOffsetT3));
        dec.pushFrame(FrameNumber(8), tsAt30fps(8),
                      ccDataPair(Cea608::TabOffsetB1, Cea608::TabOffsetT3));
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
        CHECK(out[0].anchor().value() == SubtitleAnchor::BottomLeft.value());
        // Cue text spans both rows separated by "\n".
        CHECK(out[0].text() == "AB\nCD");
}
