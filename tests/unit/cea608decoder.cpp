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
        REQUIRE(gotSpans.size() == 2);
        CHECK(gotSpans[0].text() == "REDX");
        CHECK(gotSpans[0].color() == Color::Red);
        CHECK(gotSpans[1].text() == "BL");
        CHECK(gotSpans[1].color() == Color::Blue);
        // Flat text concatenation also survives.
        CHECK(out[0].text() == "REDXBL");
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
