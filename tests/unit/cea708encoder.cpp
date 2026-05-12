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

TEST_CASE("Cea708Encoder: oversize cue -> Error::OutOfRange") {
        Cea708Encoder::Config cfg;
        cfg.frameRate = FrameRate(FrameRate::FPS_30);
        Cea708Encoder enc(cfg);
        SubtitleList  subs;
        // ~150 chars guarantees the service-block bytes exceed the
        // 127-byte packet payload budget (DefineWindow 7 bytes + chars
        // + DSW 2 bytes).
        String        long150;
        for (int i = 0; i < 150; ++i) long150 += "A";
        subs.append(Subtitle(tsFromMs(1000), tsFromMs(2000), long150));
        CHECK(enc.setSubtitles(subs).code() == Error::OutOfRange);
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
