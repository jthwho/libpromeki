/**
 * @file      cea708encoder.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <chrono>
#include <doctest/doctest.h>
#include <promeki/cea708cdp.h>
#include <promeki/cea708decoder.h>
#include <promeki/cea708encoder.h>
#include <promeki/framenumber.h>
#include <promeki/framerate.h>
#include <promeki/subtitle.h>
#include <promeki/timestamp.h>

using namespace promeki;

namespace {

        TimeStamp tsFromMs(int64_t ms) {
                using ClockDur = TimeStamp::Value::duration;
                return TimeStamp(TimeStamp::Value(std::chrono::duration_cast<ClockDur>(std::chrono::milliseconds(ms))));
        }

        TimeStamp tsAt30fps(int64_t frame) { return tsFromMs((frame * 1000 + 15) / 30); }

} // namespace

// ============================================================================
// Construction / defaults
// ============================================================================

TEST_CASE("Cea708Encoder: empty subtitle list -> no DTVCC triples on any frame") {
        Cea708Encoder::Config cfg;
        cfg.frameRate = FrameRate(FrameRate::FPS_30);
        Cea708Encoder enc(cfg);
        REQUIRE(enc.setSubtitles(SubtitleList()).isOk());
        for (int64_t f = 0; f < 100; ++f) {
                CHECK(enc.nextFrame(FrameNumber(f)).size() == 0);
        }
}

TEST_CASE("Cea708Encoder: invalid frame rate -> setSubtitles fails Invalid") {
        Cea708Encoder::Config cfg;  // default FrameRate invalid
        Cea708Encoder         enc(cfg);
        SubtitleList          subs;
        subs.append(Subtitle(tsFromMs(1000), tsFromMs(2000), "X"));
        CHECK(enc.setSubtitles(subs).code() == Error::Invalid);
}

TEST_CASE("Cea708Encoder: service number out of range -> Invalid") {
        Cea708Encoder::Config cfg;
        cfg.frameRate = FrameRate(FrameRate::FPS_30);
        cfg.serviceNumber = 0;
        Cea708Encoder enc(cfg);
        SubtitleList  subs;
        subs.append(Subtitle(tsFromMs(1000), tsFromMs(2000), "X"));
        CHECK(enc.setSubtitles(subs).code() == Error::Invalid);
}

// ============================================================================
// Schedule shape — show packet at startFrame, hide packet at endFrame
// ============================================================================

TEST_CASE("Cea708Encoder: single cue lays out show packet at startFrame, hide at endFrame") {
        Cea708Encoder::Config cfg;
        cfg.frameRate = FrameRate(FrameRate::FPS_30);
        Cea708Encoder enc(cfg);
        SubtitleList  subs;
        subs.append(Subtitle(tsFromMs(1000), tsFromMs(2000), "HELLO"));
        REQUIRE(enc.setSubtitles(subs).isOk());

        // startFrame == 30, endFrame == 60.
        const auto t29 = enc.nextFrame(FrameNumber(29));
        CHECK(t29.size() == 0);

        const auto t30 = enc.nextFrame(FrameNumber(30));
        REQUIRE(t30.size() >= 1);
        // First triple is DTVCC_PACKET_START (cc_type=2).
        CHECK(t30[0].type == 2);
        for (size_t i = 1; i < t30.size(); ++i) CHECK(t30[i].type == 3);

        const auto t31 = enc.nextFrame(FrameNumber(31));
        CHECK(t31.size() == 0); // nothing between show and hide

        const auto t60 = enc.nextFrame(FrameNumber(60));
        REQUIRE(t60.size() >= 1);
        CHECK(t60[0].type == 2);

        const auto t61 = enc.nextFrame(FrameNumber(61));
        CHECK(t61.size() == 0);
}

TEST_CASE("Cea708Encoder: cue with zero-length window is skipped") {
        Cea708Encoder::Config cfg;
        cfg.frameRate = FrameRate(FrameRate::FPS_30);
        Cea708Encoder enc(cfg);
        SubtitleList  subs;
        subs.append(Subtitle(tsFromMs(1000), tsFromMs(1000), "X"));
        REQUIRE(enc.setSubtitles(subs).isOk());
        for (int64_t f = 0; f < 100; ++f) {
                CHECK(enc.nextFrame(FrameNumber(f)).size() == 0);
        }
}

TEST_CASE("Cea708Encoder: long cue is split into one packet per frame") {
        // The CDP's 5-bit cc_count budget caps each video frame at
        // ~31 cc_data triples, and the encoder's per-packet wire
        // shape (1 service block of <=31 bytes per DTVCC packet)
        // produces ~17 triples per packet — so a long cue has to be
        // distributed across consecutive frames, one packet per
        // frame, starting at the cue's startFrame.
        Cea708Encoder::Config cfg;
        cfg.frameRate = FrameRate(FrameRate::FPS_30);
        cfg.windowCols = 32;
        Cea708Encoder enc(cfg);
        SubtitleList  subs;
        // 150 chars guarantees several chunk boundaries (5 chunks at
        // 31 bytes each, plus the DefineWindow + SetPenAttr prelude
        // and trailing DSW).  Plenty of cue duration (frames 30..120)
        // to absorb the multi-frame show transaction.
        String        long150;
        for (int i = 0; i < 150; ++i) long150 += "A";
        subs.append(Subtitle(tsFromMs(1000), tsFromMs(2000), long150));
        REQUIRE(enc.setSubtitles(subs).isOk());

        // Each emitted frame must hold exactly one DTVCC packet —
        // exactly one cc_type=2 (packet-start) triple, the rest
        // cc_type=3 (packet-data) triples.  Count the frames that
        // got triples; a 150-char cue must use at least two.
        size_t framesWithTriples = 0;
        for (int64_t f = 30; f < 60; ++f) {
                const auto triples = enc.nextFrame(FrameNumber(f));
                if (triples.isEmpty()) continue;
                ++framesWithTriples;
                size_t starts = 0;
                for (size_t i = 0; i < triples.size(); ++i) {
                        if (triples[i].type == 2) ++starts;
                        else CHECK(triples[i].type == 3);
                }
                CHECK(starts == 1);
        }
        CHECK(framesWithTriples >= 2);
}

TEST_CASE("Cea708Encoder + Cea708Decoder: long cue round-trips through chunking") {
        // End-to-end test: a cue whose service-block bytes far exceed
        // the 31-byte block_size limit must recover correctly through
        // the encoder's multi-block split and the decoder's
        // same-service concatenation.  Text length is kept under
        // Cea708Window::MaxCols (42) so single-row window scrolling
        // doesn't truncate the visible cue independently of chunking.
        Cea708Encoder::Config cfg;
        cfg.frameRate = FrameRate(FrameRate::FPS_30);
        cfg.windowCols = 42;
        Cea708Encoder enc(cfg);

        // 38 chars: DefineWindow (7) + SetPenAttr (3) + 38 + DSW (2)
        // = 50 bytes of service stream — splits into 31 + 19 byte
        // service blocks within a single DTVCC packet.
        const String text("Hello world this is a CEA-708 cue test");
        SubtitleList in;
        in.append(Subtitle(tsAt30fps(30), tsAt30fps(120), text));
        REQUIRE(enc.setSubtitles(in).isOk());

        Cea708Decoder dec;
        for (int64_t f = 0; f < 140; ++f) {
                dec.pushFrame(FrameNumber(f), tsAt30fps(f), enc.nextFrame(FrameNumber(f)));
        }
        SubtitleList out = dec.finalize();
        REQUIRE(out.size() == 1);
        CHECK(out[0].text() == text);
}

// ============================================================================
// Round-trip through Cea708Decoder
// ============================================================================

TEST_CASE("Cea708Encoder + Cea708Decoder: round-trips a single cue") {
        Cea708Encoder::Config encCfg;
        encCfg.frameRate = FrameRate(FrameRate::FPS_30);
        Cea708Encoder enc(encCfg);

        SubtitleList in;
        in.append(Subtitle(tsAt30fps(30), tsAt30fps(60), "HELLO"));
        REQUIRE(enc.setSubtitles(in).isOk());

        Cea708Decoder dec;
        // Feed every frame 0..80 through enc -> dec.
        for (int64_t f = 0; f < 80; ++f) {
                Cea708Cdp::CcDataList list = enc.nextFrame(FrameNumber(f));
                dec.pushFrame(FrameNumber(f), tsAt30fps(f), list);
        }
        SubtitleList out = dec.finalize();
        REQUIRE(out.size() == 1);
        CHECK(out[0].text() == "HELLO");
        CHECK(out[0].start() == tsAt30fps(30));
        CHECK(out[0].end() == tsAt30fps(60));
}

TEST_CASE("Cea708Encoder + Cea708Decoder: round-trips multiple sequential cues") {
        Cea708Encoder::Config encCfg;
        encCfg.frameRate = FrameRate(FrameRate::FPS_30);
        Cea708Encoder enc(encCfg);

        SubtitleList in;
        in.append(Subtitle(tsAt30fps(30), tsAt30fps(60), "AB"));
        in.append(Subtitle(tsAt30fps(120), tsAt30fps(150), "CD"));
        in.append(Subtitle(tsAt30fps(200), tsAt30fps(240), "EF"));
        REQUIRE(enc.setSubtitles(in).isOk());

        Cea708Decoder dec;
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

TEST_CASE("Cea708Encoder + Cea708Decoder: longer text round-trip") {
        Cea708Encoder::Config encCfg;
        encCfg.frameRate = FrameRate(FrameRate::FPS_30);
        encCfg.windowCols = 32;
        Cea708Encoder enc(encCfg);

        SubtitleList in;
        in.append(Subtitle(tsAt30fps(30), tsAt30fps(120), "Hello, world."));
        REQUIRE(enc.setSubtitles(in).isOk());

        Cea708Decoder dec;
        for (int64_t f = 0; f < 140; ++f) {
                dec.pushFrame(FrameNumber(f), tsAt30fps(f), enc.nextFrame(FrameNumber(f)));
        }
        SubtitleList out = dec.finalize();
        REQUIRE(out.size() == 1);
        CHECK(out[0].text() == "Hello, world.");
}

// ============================================================================
// Service number round-trip
// ============================================================================

TEST_CASE("Cea708Encoder + Decoder: service 2 round-trips when decoder is configured for service 2") {
        Cea708Encoder::Config encCfg;
        encCfg.frameRate = FrameRate(FrameRate::FPS_30);
        encCfg.serviceNumber = 2;
        Cea708Encoder enc(encCfg);

        SubtitleList in;
        in.append(Subtitle(tsAt30fps(30), tsAt30fps(60), "Q"));
        REQUIRE(enc.setSubtitles(in).isOk());

        Cea708Decoder::Config decCfg;
        decCfg.serviceNumber = 2;
        Cea708Decoder dec(decCfg);
        for (int64_t f = 0; f < 80; ++f) {
                dec.pushFrame(FrameNumber(f), tsAt30fps(f), enc.nextFrame(FrameNumber(f)));
        }
        SubtitleList out = dec.finalize();
        REQUIRE(out.size() == 1);
        CHECK(out[0].text() == "Q");
}

TEST_CASE("Cea708Encoder + Decoder: mismatched service numbers -> no decode") {
        Cea708Encoder::Config encCfg;
        encCfg.frameRate = FrameRate(FrameRate::FPS_30);
        encCfg.serviceNumber = 2;
        Cea708Encoder enc(encCfg);
        SubtitleList  in;
        in.append(Subtitle(tsAt30fps(30), tsAt30fps(60), "Q"));
        REQUIRE(enc.setSubtitles(in).isOk());

        Cea708Decoder dec; // default service 1
        for (int64_t f = 0; f < 80; ++f) {
                dec.pushFrame(FrameNumber(f), tsAt30fps(f), enc.nextFrame(FrameNumber(f)));
        }
        CHECK(dec.finalize().isEmpty());
}

// ============================================================================
// reset()
// ============================================================================

TEST_CASE("Cea708Encoder::reset clears the schedule") {
        Cea708Encoder::Config cfg;
        cfg.frameRate = FrameRate(FrameRate::FPS_30);
        Cea708Encoder enc(cfg);
        SubtitleList  subs;
        subs.append(Subtitle(tsFromMs(1000), tsFromMs(2000), "X"));
        REQUIRE(enc.setSubtitles(subs).isOk());
        CHECK(enc.nextFrame(FrameNumber(30)).size() > 0);
        enc.reset();
        CHECK(enc.nextFrame(FrameNumber(30)).size() == 0);
}

// ============================================================================
// CaptionMode dispatch (per-cue)
// ============================================================================

TEST_CASE("Cea708Encoder: pop-on cue emits show + hide transactions") {
        // Default mode (PopOn) emits a Define/Display at startFrame and
        // a HideWindow at endFrame.
        Cea708Encoder::Config cfg;
        cfg.frameRate = FrameRate(FrameRate::FPS_30);
        Cea708Encoder enc(cfg);
        SubtitleList  subs;
        Subtitle      cue(tsFromMs(1000), tsFromMs(2000), "X");
        cue.setMode(CaptionMode::PopOn);
        subs.append(cue);
        REQUIRE(enc.setSubtitles(subs).isOk());

        // Show packet at startFrame.
        CHECK(enc.nextFrame(FrameNumber(30)).size() > 0);
        // Hide packet at endFrame.
        CHECK(enc.nextFrame(FrameNumber(60)).size() > 0);
}

TEST_CASE("Cea708Encoder: paint-on cue emits show but no hide transaction") {
        // PaintOn skips the HideWindow boundary so the window stays
        // visible after the cue's end.  Real "live" captioning.
        Cea708Encoder::Config cfg;
        cfg.frameRate = FrameRate(FrameRate::FPS_30);
        Cea708Encoder enc(cfg);
        SubtitleList  subs;
        Subtitle      cue(tsFromMs(1000), tsFromMs(2000), "X");
        cue.setMode(CaptionMode::PaintOn);
        subs.append(cue);
        REQUIRE(enc.setSubtitles(subs).isOk());

        CHECK(enc.nextFrame(FrameNumber(30)).size() > 0);
        // No HideWindow at endFrame for paint-on.
        CHECK(enc.nextFrame(FrameNumber(60)).size() == 0);
}

TEST_CASE("Cea708Encoder: roll-up cue emits a multi-row window and no hide") {
        // RollUp uses a multi-row DefineWindow (so the receiver
        // scrolls) and skips the HideWindow boundary.
        Cea708Encoder::Config cfg;
        cfg.frameRate = FrameRate(FrameRate::FPS_30);
        Cea708Encoder enc(cfg);
        SubtitleList  subs;
        Subtitle      cue(tsFromMs(1000), tsFromMs(2000), "X");
        cue.setMode(CaptionMode::RollUp);
        subs.append(cue);
        REQUIRE(enc.setSubtitles(subs).isOk());

        // Show packet present.
        const auto t30 = enc.nextFrame(FrameNumber(30));
        REQUIRE(t30.size() > 0);
        // No HideWindow at endFrame for roll-up.
        CHECK(enc.nextFrame(FrameNumber(60)).size() == 0);
}

TEST_CASE("Cea708Encoder + Decoder: roll-up cue round-trips with mode stamped") {
        // Encode → decode and verify the decoder recovers
        // CaptionMode::RollUp because the wire DefineWindow advertised
        // a row_count > 1.
        Cea708Encoder::Config encCfg;
        encCfg.frameRate = FrameRate(FrameRate::FPS_30);
        Cea708Encoder enc(encCfg);
        SubtitleList  in;
        Subtitle      cue(tsAt30fps(30), tsAt30fps(60), "ROLLUP");
        cue.setMode(CaptionMode::RollUp);
        in.append(cue);
        REQUIRE(enc.setSubtitles(in).isOk());

        Cea708Decoder dec;
        for (int64_t f = 0; f < 80; ++f) {
                dec.pushFrame(FrameNumber(f), tsAt30fps(f), enc.nextFrame(FrameNumber(f)));
        }
        SubtitleList out = dec.finalize();
        REQUIRE(out.size() == 1);
        CHECK(out[0].text() == "ROLLUP");
        CHECK(out[0].mode().value() == CaptionMode::RollUp.value());
}

TEST_CASE("Cea708Encoder + Decoder: styled span round-trips italic/underline/edge/font via SPA") {
        // Encode a cue whose single span carries italic + underline +
        // edge style + font face.  Decoder must recover all four flags
        // by parsing the SetPenAttributes (SPA) command we emit before
        // the character bytes.
        Cea708Encoder::Config encCfg;
        encCfg.frameRate = FrameRate(FrameRate::FPS_30);
        Cea708Encoder enc(encCfg);

        SubtitleSpan span("Q", false, true, true, Color()); // italic + underline
        span.setEdgeStyle(SubtitleEdgeStyle::Raised);
        span.setFontFace(SubtitleFontFace::ProportionalSans);
        SubtitleSpan::List spans;
        spans.pushToBack(span);
        SubtitleList in;
        in.append(Subtitle(tsAt30fps(30), tsAt30fps(60), spans, SubtitleAnchor::Default,
                            Rect2Di32(), String(), Metadata()));
        REQUIRE(enc.setSubtitles(in).isOk());

        Cea708Decoder dec;
        for (int64_t f = 0; f < 80; ++f) {
                dec.pushFrame(FrameNumber(f), tsAt30fps(f), enc.nextFrame(FrameNumber(f)));
        }
        SubtitleList out = dec.finalize();
        REQUIRE(out.size() == 1);
        CHECK(out[0].text() == "Q");
        REQUIRE(out[0].spans().size() == 1);
        const SubtitleSpan &gotSpan = out[0].spans()[0];
        CHECK(gotSpan.italic());
        CHECK(gotSpan.underline());
        CHECK(gotSpan.edgeStyle().value() == SubtitleEdgeStyle::Raised.value());
        CHECK(gotSpan.fontFace().value() == SubtitleFontFace::ProportionalSans.value());
}

TEST_CASE("Cea708Encoder + Decoder: styled span round-trips fg/bg/edge colour via SPC") {
        // Colours are quantised to 2-bit-per-channel on the wire, so
        // we encode with values that survive: pure-red fg (255/0/0),
        // pure-green bg (0/255/0), pure-blue edge (0/0/255).
        Cea708Encoder::Config encCfg;
        encCfg.frameRate = FrameRate(FrameRate::FPS_30);
        Cea708Encoder enc(encCfg);

        SubtitleSpan span("X", false, false, false, Color::Red);
        span.setBackgroundColor(Color::Green);
        span.setEdgeColor(Color::Blue);
        SubtitleSpan::List spans;
        spans.pushToBack(span);
        SubtitleList in;
        in.append(Subtitle(tsAt30fps(30), tsAt30fps(60), spans, SubtitleAnchor::Default,
                            Rect2Di32(), String(), Metadata()));
        REQUIRE(enc.setSubtitles(in).isOk());

        Cea708Decoder dec;
        for (int64_t f = 0; f < 80; ++f) {
                dec.pushFrame(FrameNumber(f), tsAt30fps(f), enc.nextFrame(FrameNumber(f)));
        }
        SubtitleList out = dec.finalize();
        REQUIRE(out.size() == 1);
        REQUIRE(out[0].spans().size() == 1);
        const SubtitleSpan &gotSpan = out[0].spans()[0];
        // Decoder reconstructs colour from the 2-bit wire form via the
        // canonical {0, 85, 170, 255} expansion, so a pure-red input
        // round-trips as r=255, g=0, b=0.
        CHECK(gotSpan.color().isValid());
        CHECK(gotSpan.color().r8() == 255);
        CHECK(gotSpan.color().g8() == 0);
        CHECK(gotSpan.color().b8() == 0);
        CHECK(gotSpan.backgroundColor().isValid());
        CHECK(gotSpan.backgroundColor().g8() == 255);
        CHECK(gotSpan.edgeColor().isValid());
        CHECK(gotSpan.edgeColor().b8() == 255);
}

TEST_CASE("Cea708Encoder + Decoder: multi-style cue recovers separate spans") {
        // Two spans within a single cue: red italic "RED" + green
        // underlined "GRN".  The encoder emits SetPenAttributes and
        // SetPenColor between the two runs; the decoder's per-cell
        // pen tracking reconstructs both spans.
        Cea708Encoder::Config encCfg;
        encCfg.frameRate = FrameRate(FrameRate::FPS_30);
        Cea708Encoder enc(encCfg);

        SubtitleSpan red("RED", false, true, false, Color::Red);
        SubtitleSpan grn("GRN", false, false, true, Color::Green);
        SubtitleSpan::List spans;
        spans.pushToBack(red);
        spans.pushToBack(grn);
        SubtitleList in;
        in.append(Subtitle(tsAt30fps(30), tsAt30fps(60), spans, SubtitleAnchor::Default,
                            Rect2Di32(), String(), Metadata()));
        REQUIRE(enc.setSubtitles(in).isOk());

        Cea708Decoder dec;
        for (int64_t f = 0; f < 80; ++f) {
                dec.pushFrame(FrameNumber(f), tsAt30fps(f), enc.nextFrame(FrameNumber(f)));
        }
        SubtitleList out = dec.finalize();
        REQUIRE(out.size() == 1);
        CHECK(out[0].text() == "REDGRN");
        // Decoder reconstructs two spans because the two runs have
        // different pen states.  Order matches encode.
        REQUIRE(out[0].spans().size() == 2);
        CHECK(out[0].spans()[0].text() == "RED");
        CHECK(out[0].spans()[0].italic());
        CHECK(out[0].spans()[0].color().r8() == 255);
        CHECK(out[0].spans()[1].text() == "GRN");
        CHECK(out[0].spans()[1].underline());
        CHECK(out[0].spans()[1].color().g8() == 255);
}

TEST_CASE("Cea708Encoder + Decoder: pop-on cue round-trips with mode stamped") {
        // Single-row DefineWindow + HideWindow → decoder sees the cue
        // boundary close on content change and recovers PopOn.
        Cea708Encoder::Config encCfg;
        encCfg.frameRate = FrameRate(FrameRate::FPS_30);
        Cea708Encoder enc(encCfg);
        SubtitleList  in;
        Subtitle      cue(tsAt30fps(30), tsAt30fps(60), "POP");
        cue.setMode(CaptionMode::PopOn);
        in.append(cue);
        REQUIRE(enc.setSubtitles(in).isOk());

        Cea708Decoder dec;
        for (int64_t f = 0; f < 80; ++f) {
                dec.pushFrame(FrameNumber(f), tsAt30fps(f), enc.nextFrame(FrameNumber(f)));
        }
        SubtitleList out = dec.finalize();
        REQUIRE(out.size() == 1);
        CHECK(out[0].text() == "POP");
        CHECK(out[0].mode().value() == CaptionMode::PopOn.value());
}
