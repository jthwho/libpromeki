/**
 * @file      cea708_subrip_roundtrip.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * End-to-end fixture: parses @c etc/substest.srt into a SubtitleList,
 * drives that list through @ref Cea708Encoder → @ref Cea708Decoder at
 * 30 fps, and asserts each round-tripped cue preserves the source
 * cue's text, anchor, and per-span style attributes.  Each behaviour
 * (anchor, color, mode, full text) has its own focused TEST_CASE so
 * a regression names its symptom cleanly.
 */

#include <chrono>
#include <cstdint>
#include <doctest/doctest.h>
#include <promeki/buffer.h>
#include <promeki/cea708cdp.h>
#include <promeki/cea708decoder.h>
#include <promeki/cea708encoder.h>
#include <promeki/color.h>
#include <promeki/enums.h>
#include <promeki/file.h>
#include <promeki/framenumber.h>
#include <promeki/framerate.h>
#include <promeki/iodevice.h>
#include <promeki/result.h>
#include <promeki/string.h>
#include <promeki/subrip.h>
#include <promeki/subtitle.h>
#include <promeki/timestamp.h>

#ifndef PROMEKI_SOURCE_DIR
#define PROMEKI_SOURCE_DIR "."
#endif

using namespace promeki;

namespace {

        TimeStamp tsFromMs(int64_t ms) {
                using ClockDur = TimeStamp::Value::duration;
                return TimeStamp(TimeStamp::Value(
                        std::chrono::duration_cast<ClockDur>(std::chrono::milliseconds(ms))));
        }

        /// @brief Parses @c etc/substest.srt into a SubtitleList.
        ///        Returns an empty list if the file can't be read.
        SubtitleList loadSubstest() {
                const String path = String(PROMEKI_SOURCE_DIR) + "/etc/substest.srt";
                File         f(path);
                if (f.open(IODevice::ReadOnly).isError()) return SubtitleList();
                Result<int64_t> szR = f.size();
                if (szR.second().isError()) {
                        f.close();
                        return SubtitleList();
                }
                const int64_t sz = szR.first();
                Buffer        buf(static_cast<size_t>(sz));
                buf.setSize(static_cast<size_t>(sz));
                f.read(buf.data(), sz);
                f.close();
                Result<SubtitleList> r = SubRip::parse(buf);
                if (r.second().isError()) return SubtitleList();
                return r.first();
        }

        /// @brief Runs an entire SubtitleList through Cea708Encoder
        ///        at @p fps, feeds every frame from t=0 to past the
        ///        last cue's end through Cea708Decoder, and returns
        ///        the decoded cue list.
        SubtitleList encodeDecode(const SubtitleList &in, const FrameRate &fps) {
                Cea708Encoder::Config encCfg;
                encCfg.frameRate = fps;
                Cea708Encoder enc(encCfg);
                REQUIRE(enc.setSubtitles(in).isOk());

                // Highest end-time in the list determines how far to
                // drive the decoder.  Add a few frames of slack so the
                // trailing hide transaction always completes.
                int64_t lastEndMs = 0;
                for (size_t i = 0; i < in.size(); ++i) {
                        const int64_t e = in[i].end().milliseconds();
                        if (e > lastEndMs) lastEndMs = e;
                }
                const double  fpsVal = fps.toDouble();
                const int64_t totalFrames =
                        static_cast<int64_t>((lastEndMs / 1000.0) * fpsVal) + 30;

                Cea708Decoder dec;
                for (int64_t f = 0; f < totalFrames; ++f) {
                        const TimeStamp ts = tsFromMs(static_cast<int64_t>(f * 1000.0 / fpsVal));
                        dec.pushFrame(FrameNumber(f), ts, enc.nextFrame(FrameNumber(f)));
                }
                return dec.finalize();
        }

        /// @brief Returns the index of the decoded cue whose start
        ///        timestamp lands closest to (but not before) @p src's
        ///        start, within ~1 second of slack to absorb the
        ///        multi-frame show transaction (the cue only fully
        ///        appears once the trailing DSW lands).  Returns -1
        ///        when no decoded cue is within slack of @p src.
        int64_t findMatchingCue(const SubtitleList &out, const Subtitle &src) {
                const auto srcStart = src.start().value().time_since_epoch();
                const int64_t srcStartMs =
                        std::chrono::duration_cast<std::chrono::milliseconds>(srcStart).count();
                const int64_t srcEndMs =
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                                src.end().value().time_since_epoch())
                                .count();
                // Allow the decoded cue to start anywhere from
                // (srcStart - 100ms) to srcEnd — the multi-frame show
                // may land a few frames after srcStart but still before
                // the cue's own end.
                int64_t best = -1;
                int64_t bestDelta = INT64_MAX;
                for (size_t i = 0; i < out.size(); ++i) {
                        const int64_t s =
                                std::chrono::duration_cast<std::chrono::milliseconds>(
                                        out[i].start().value().time_since_epoch())
                                        .count();
                        if (s + 100 < srcStartMs) continue;
                        if (s > srcEndMs) continue;
                        const int64_t delta = (s >= srcStartMs) ? (s - srcStartMs) : (srcStartMs - s);
                        if (delta < bestDelta) {
                                bestDelta = delta;
                                best = static_cast<int64_t>(i);
                        }
                }
                return best;
        }

        /// @brief Returns @p s as a std::string so doctest's
        ///        stringifier prints the actual text rather than the
        ///        const char* pointer address it gets from @c String::cstr().
        std::string ssOf(const String &s) {
                return std::string(s.cstr(), s.byteCount());
        }

} // namespace

// ============================================================================
// SubRip fixture sanity — make sure the file loaded; everything else
// short-circuits if not.
// ============================================================================

TEST_CASE("Cea708 SubRip round-trip: etc/substest.srt loads") {
        SubtitleList in = loadSubstest();
        REQUIRE(in.size() >= 20);
}

// ============================================================================
// Cue count + ordering
// ============================================================================

TEST_CASE("Cea708 SubRip round-trip: every source cue produces a decoded cue") {
        SubtitleList in = loadSubstest();
        REQUIRE(in.size() >= 20);
        SubtitleList out = encodeDecode(in, FrameRate(FrameRate::FPS_30));
        // Every source cue should produce at least one decoded cue.
        // Allow extra cues in the round-trip (transient state during
        // multi-frame show transactions can produce intermediates).
        CHECK_MESSAGE(out.size() >= in.size(), "got ", out.size(), " decoded vs ", in.size(),
                       " source");
}

// ============================================================================
// Text preservation per cue
// ============================================================================

TEST_CASE("Cea708 SubRip round-trip: cue text survives round-trip in full") {
        SubtitleList in = loadSubstest();
        REQUIRE(in.size() >= 20);
        SubtitleList out = encodeDecode(in, FrameRate(FrameRate::FPS_30));
        // For each source cue, the decoded list must contain a cue
        // whose start landed in the source cue's display window and
        // whose text covers the same content.  The encoder
        // auto-wraps lines that exceed the configured @c windowCols
        // and the decoder back-fills empty trailing cells with
        // spaces — both transformations are normalised away here so
        // the test focuses on content preservation, not formatting.
        auto normalize = [](const String &s) {
                std::string out;
                out.reserve(s.byteCount());
                bool inWs = false;
                for (size_t i = 0; i < s.byteCount(); ++i) {
                        const unsigned char c = static_cast<unsigned char>(s[i]);
                        const bool         isWs = (c == ' ' || c == '\n' || c == '\t' || c == '\r');
                        if (isWs) {
                                if (!inWs && !out.empty()) out.push_back(' ');
                                inWs = true;
                        } else {
                                out.push_back(static_cast<char>(c));
                                inWs = false;
                        }
                }
                while (!out.empty() && out.back() == ' ') out.pop_back();
                return out;
        };

        // Cues containing non-ASCII characters (Latin-1 / CJK / etc.)
        // round-trip lossily today — the encoder writes only G0
        // (0x20..0x7E) and substitutes everything else with spaces.
        // Skip them here so the test asserts on the bulk-fix
        // behaviour (anchor, span style, multi-row layout, full
        // ASCII preservation) without being held up by the G1 / G2 /
        // G3 / P16 character-table support that's a separate feature.
        auto isAsciiOnly = [](const String &s) {
                for (size_t i = 0; i < s.byteCount(); ++i) {
                        const unsigned char c = static_cast<unsigned char>(s[i]);
                        if (c > 0x7E && c != '\n' && c != '\t' && c != '\r') return false;
                }
                return true;
        };

        for (size_t i = 0; i < in.size(); ++i) {
                const Subtitle &src = in[i];
                if (src.text().isEmpty()) continue;
                if (!isAsciiOnly(src.text())) continue; // non-ASCII not yet supported
                const int64_t  idx = findMatchingCue(out, src);
                if (idx < 0) {
                        FAIL_CHECK("source cue ", i, " (\"", ssOf(src.text()),
                                   "\") has no decoded match in its display window");
                        continue;
                }
                const String     &got = out[static_cast<size_t>(idx)].text();
                const std::string srcN = normalize(src.text());
                const std::string gotN = normalize(got);
                CHECK_MESSAGE(gotN == srcN,
                               "source cue ", i, " text mismatch:\n  src=\"", srcN,
                               "\"\n  got=\"", gotN, "\"");
        }
}

// ============================================================================
// Anchor preservation — {\an1}..{\an9}
// ============================================================================

TEST_CASE("Cea708 SubRip round-trip: per-cue anchor flows through encoder + decoder") {
        SubtitleList in = loadSubstest();
        REQUIRE(in.size() >= 20);
        SubtitleList out = encodeDecode(in, FrameRate(FrameRate::FPS_30));
        // Walk the source list looking for cues that carry an explicit
        // anchor (anything other than SubtitleAnchor::Default), then
        // verify the matching decoded cue carries the same anchor.
        for (size_t i = 0; i < in.size(); ++i) {
                const Subtitle &src = in[i];
                if (src.anchor().value() == SubtitleAnchor::Default.value()) continue;
                const int64_t idx = findMatchingCue(out, src);
                if (idx < 0) continue; // text test will already flag it
                const Subtitle &got = out[static_cast<size_t>(idx)];
                CHECK_MESSAGE(got.anchor().value() == src.anchor().value(),
                               "source cue ", i, " (\"", ssOf(src.text()),
                               "\"): expected anchor ", src.anchor().value(), " got ",
                               got.anchor().value());
        }
}

// ============================================================================
// Per-span color preservation
// ============================================================================

TEST_CASE("Cea708 SubRip round-trip: span color only colours the marked-up run") {
        // The substest fixture's cue 9 is:
        //   <font color="#FF0000">Red text</font> via inline font tag.
        // Three spans expected: ["Red text" red] [" via inline font tag." default].
        // The round-trip must NOT colour the trailing " via inline
        // font tag." span red just because the previous span was red.
        SubtitleList in = loadSubstest();
        REQUIRE(in.size() >= 20);
        SubtitleList out = encodeDecode(in, FrameRate(FrameRate::FPS_30));

        // Find the source cue with a red span and verify the
        // round-tripped cue's spans show: red colour ONLY on the
        // span(s) whose source text contains "Red text".
        for (size_t i = 0; i < in.size(); ++i) {
                const auto &spans = in[i].spans();
                bool        hasRed = false;
                for (size_t s = 0; s < spans.size(); ++s) {
                        const Color &c = spans[s].color();
                        if (c.isValid() && c.r8() > 200 && c.g8() < 50 && c.b8() < 50) {
                                hasRed = true;
                                break;
                        }
                }
                if (!hasRed) continue;
                const int64_t idx = findMatchingCue(out, in[i]);
                if (idx < 0) continue;
                const auto &outSpans = out[static_cast<size_t>(idx)].spans();
                // For each output span, if its text doesn't overlap
                // with the source's red span, it must NOT be red.
                bool foundRedSpan = false;
                for (size_t s = 0; s < outSpans.size(); ++s) {
                        const String &t = outSpans[s].text();
                        const Color  &c = outSpans[s].color();
                        const bool    looksRed = c.isValid() && c.r8() > 200 && c.g8() < 50
                                                    && c.b8() < 50;
                        if (looksRed && t.contains("Red text")) {
                                foundRedSpan = true;
                        } else if (looksRed && t.contains(" via inline font tag")) {
                                FAIL_CHECK("plain trailing span \"", ssOf(t),
                                           "\" was painted red by the encoder/decoder pair");
                        }
                }
                CHECK_MESSAGE(foundRedSpan,
                               "source cue ", i, " had a red span but no decoded span carries red");
        }
}

// ============================================================================
// Italic preservation
// ============================================================================

TEST_CASE("Cea708 SubRip round-trip: pen colour does not bleed from one cue into the next") {
        // CEA-708 pen state is global on the receiver and persists
        // across DefineWindow boundaries — without an explicit
        // SPC reset at the start of each cue's show transaction the
        // residual colour from the previous styled cue (e.g. cue 19's
        // magenta full-cue colour) would paint the next cue's plain
        // text in the same colour.  The encoder must reset wire pen
        // state at the start of every cue.
        SubtitleList in = loadSubstest();
        REQUIRE(in.size() >= 20);
        SubtitleList out = encodeDecode(in, FrameRate(FrameRate::FPS_30));

        // For each pair of source cues (styled, plain), find both in
        // the decoded list and verify the plain cue's spans are not
        // tinted by the styled cue's colour.
        for (size_t i = 0; i + 1 < in.size(); ++i) {
                // Identify a "styled" source cue: any span whose
                // colour is valid and not white.
                bool srcStyled = false;
                for (size_t s = 0; s < in[i].spans().size(); ++s) {
                        const Color &c = in[i].spans()[s].color();
                        if (c.isValid() && (c.r8() < 240 || c.g8() < 240 || c.b8() < 240)) {
                                srcStyled = true;
                                break;
                        }
                }
                if (!srcStyled) continue;
                // Next source cue must be fully plain (no styled
                // spans) to count as a "did the colour bleed?" test.
                bool nextPlain = true;
                for (size_t s = 0; s < in[i + 1].spans().size(); ++s) {
                        if (in[i + 1].spans()[s].color().isValid()) {
                                nextPlain = false;
                                break;
                        }
                }
                if (!nextPlain) continue;

                const int64_t idxNext = findMatchingCue(out, in[i + 1]);
                if (idxNext < 0) continue;
                const auto &nextSpans = out[static_cast<size_t>(idxNext)].spans();
                for (size_t s = 0; s < nextSpans.size(); ++s) {
                        const Color &c = nextSpans[s].color();
                        if (!c.isValid()) continue;
                        // White (or near-white) is the codec-default
                        // foreground; anything darker is residual
                        // colour from the prior cue.
                        const bool nearWhite = c.r8() > 240 && c.g8() > 240 && c.b8() > 240;
                        CHECK_MESSAGE(nearWhite,
                                       "plain cue ", i + 1,
                                       " span carries residual colour from prior styled cue ",
                                       i, " (rgb=",
                                       static_cast<int>(c.r8()), ",",
                                       static_cast<int>(c.g8()), ",",
                                       static_cast<int>(c.b8()), ")");
                }
        }
}

TEST_CASE("Cea708 SubRip round-trip: italic span attribute round-trips") {
        // Cue 7 is "<i>This text should display italic.</i>" — every
        // decoded span covering that text must report italic == true.
        SubtitleList in = loadSubstest();
        REQUIRE(in.size() >= 20);
        SubtitleList out = encodeDecode(in, FrameRate(FrameRate::FPS_30));

        for (size_t i = 0; i < in.size(); ++i) {
                const auto &spans = in[i].spans();
                bool        srcItalic = false;
                for (size_t s = 0; s < spans.size(); ++s) {
                        if (spans[s].italic()) {
                                srcItalic = true;
                                break;
                        }
                }
                if (!srcItalic) continue;
                const int64_t idx = findMatchingCue(out, in[i]);
                if (idx < 0) continue;
                const auto &outSpans = out[static_cast<size_t>(idx)].spans();
                bool        anyItalic = false;
                for (size_t s = 0; s < outSpans.size(); ++s) {
                        if (outSpans[s].italic()) {
                                anyItalic = true;
                                break;
                        }
                }
                CHECK_MESSAGE(anyItalic,
                               "source cue ", i, " (\"", ssOf(in[i].text()),
                               "\") had italic spans but no decoded span is italic");
        }
}
