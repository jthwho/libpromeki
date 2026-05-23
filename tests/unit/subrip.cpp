/**
 * @file      subrip.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <chrono>
#include <doctest/doctest.h>
#include <promeki/buffer.h>
#include <promeki/color.h>
#include <promeki/duration.h>
#include <promeki/enums.h>
#include <promeki/file.h>
#include <promeki/metadata.h>
#include <promeki/rect.h>
#include <promeki/result.h>
#include <promeki/string.h>
#include <promeki/subrip.h>
#include <promeki/subtitle.h>
#include <promeki/timestamp.h>
#include <promeki/variant.h>

#ifndef PROMEKI_SOURCE_DIR
#define PROMEKI_SOURCE_DIR "."
#endif

using namespace promeki;

namespace {

        // Three-cue sample used by several tests below.
        constexpr const char *kThreeCues =
                "1\r\n"
                "00:00:01,000 --> 00:00:02,500\r\n"
                "Hello, world.\r\n"
                "\r\n"
                "2\r\n"
                "00:00:03,000 --> 00:00:05,000\r\n"
                "Multi-line\r\n"
                "cue text.\r\n"
                "\r\n"
                "3\r\n"
                "00:00:06,000 --> 00:00:08,000\r\n"
                "Third cue.\r\n"
                "\r\n";

        /// @brief Constructs a media-relative TimeStamp from a
        ///        millisecond offset (epoch = media t=0).
        TimeStamp tsFromMs(int64_t ms) {
                using ClockDur = TimeStamp::Value::duration;
                return TimeStamp(TimeStamp::Value(std::chrono::duration_cast<ClockDur>(std::chrono::milliseconds(ms))));
        }

} // namespace

// ============================================================================
// Empty / default
// ============================================================================

TEST_CASE("SubRip: empty input parses to empty SubtitleList, round-trips") {
        Result<SubtitleList> r = SubRip::parse(String(""));
        REQUIRE(r.second().isOk());
        CHECK(r.first().size() == 0);
        Buffer b = SubRip::emit(r.first());
        CHECK(b.size() == 0);
}

// ============================================================================
// Basic parse
// ============================================================================

TEST_CASE("SubRip: parses three sequential cues with CRLF endings") {
        Result<SubtitleList> r = SubRip::parse(String(kThreeCues));
        REQUIRE(r.second().isOk());
        const SubtitleList &sl = r.first();
        REQUIRE(sl.size() == 3);

        CHECK(sl[0].start() == tsFromMs(1000));
        CHECK(sl[0].end() == tsFromMs(2500));
        CHECK(sl[0].text() == "Hello, world.");
        CHECK(sl[0].anchor() == SubtitleAnchor::Default);

        CHECK(sl[1].start() == tsFromMs(3000));
        CHECK(sl[1].end() == tsFromMs(5000));
        CHECK(sl[1].text() == "Multi-line\ncue text.");

        CHECK(sl[2].start() == tsFromMs(6000));
        CHECK(sl[2].end() == tsFromMs(8000));
        CHECK(sl[2].text() == "Third cue.");
}

TEST_CASE("SubRip: tolerates LF-only line endings") {
        String      lf = String(kThreeCues);
        String      lfOnly;
        const char *s = lf.cstr();
        size_t      n = lf.byteCount();
        for (size_t i = 0; i < n; ++i) {
                if (s[i] == '\r') continue;
                char tmp[2] = {s[i], 0};
                lfOnly += tmp;
        }
        Result<SubtitleList> r = SubRip::parse(lfOnly);
        REQUIRE(r.second().isOk());
        CHECK(r.first().size() == 3);
        CHECK(r.first()[0].text() == "Hello, world.");
        CHECK(r.first()[1].text() == "Multi-line\ncue text.");
}

TEST_CASE("SubRip: tolerates a UTF-8 BOM") {
        String input = "\xEF\xBB\xBF";
        input += kThreeCues;
        Result<SubtitleList> r = SubRip::parse(input);
        REQUIRE(r.second().isOk());
        CHECK(r.first().size() == 3);
}

TEST_CASE("SubRip: tolerates missing sequence number lines") {
        const char          *noSeq = "00:00:01,000 --> 00:00:02,000\r\n"
                                     "Cue without a number.\r\n"
                                     "\r\n";
        Result<SubtitleList> r = SubRip::parse(String(noSeq));
        REQUIRE(r.second().isOk());
        REQUIRE(r.first().size() == 1);
        CHECK(r.first()[0].start() == tsFromMs(1000));
        CHECK(r.first()[0].text() == "Cue without a number.");
}

TEST_CASE("SubRip: accepts period as the millisecond separator") {
        const char          *dotted = "1\n"
                                      "00:00:01.250 --> 00:00:02.750\n"
                                      "Dotted timecode.\n"
                                      "\n";
        Result<SubtitleList> r = SubRip::parse(String(dotted));
        REQUIRE(r.second().isOk());
        REQUIRE(r.first().size() == 1);
        CHECK(r.first()[0].start() == tsFromMs(1250));
        CHECK(r.first()[0].end() == tsFromMs(2750));
}

// ============================================================================
// Malformed inputs
// ============================================================================

TEST_CASE("SubRip: malformed start timecode -> ParseFailed") {
        const char          *bad = "1\nBADTC --> 00:00:02,000\nx\n\n";
        Result<SubtitleList> r = SubRip::parse(String(bad));
        CHECK(r.second().code() == Error::ParseFailed);
}

TEST_CASE("SubRip: missing arrow -> ParseFailed") {
        const char          *bad = "1\n00:00:01,000 00:00:02,000\nx\n\n";
        Result<SubtitleList> r = SubRip::parse(String(bad));
        CHECK(r.second().code() == Error::ParseFailed);
}

TEST_CASE("SubRip: malformed end timecode -> ParseFailed") {
        const char          *bad = "1\n00:00:01,000 --> BADTC\nx\n\n";
        Result<SubtitleList> r = SubRip::parse(String(bad));
        CHECK(r.second().code() == Error::ParseFailed);
}

// ============================================================================
// Positioning anchor + coordinate region
// ============================================================================

TEST_CASE("SubRip: parses ASS-style {\\an8} into SubtitleAnchor::TopCenter") {
        const char          *src = "1\r\n"
                                   "00:00:01,000 --> 00:00:02,000\r\n"
                                   "{\\an8}Top-centered cue\r\n"
                                   "\r\n";
        Result<SubtitleList> r = SubRip::parse(String(src));
        REQUIRE(r.second().isOk());
        REQUIRE(r.first().size() == 1);
        CHECK(r.first()[0].anchor() == SubtitleAnchor::TopCenter);
        CHECK(r.first()[0].text() == "Top-centered cue");
}

TEST_CASE("SubRip: parses X1:..Y2: coordinate suffix into region Rect") {
        const char          *src = "1\r\n"
                                   "00:00:01,000 --> 00:00:02,000 X1:40 X2:600 Y1:50 Y2:80\r\n"
                                   "Positioned cue\r\n"
                                   "\r\n";
        Result<SubtitleList> r = SubRip::parse(String(src));
        REQUIRE(r.second().isOk());
        REQUIRE(r.first().size() == 1);
        const Rect2Di32 &reg = r.first()[0].region();
        CHECK(reg.isValid());
        CHECK(reg.x() == 40);
        CHECK(reg.y() == 50);
        CHECK(reg.width() == 560);
        CHECK(reg.height() == 30);
}

// ============================================================================
// Round-trip emit + parse
// ============================================================================

TEST_CASE("SubRip: round-trips parse -> emit -> parse byte-stable") {
        Result<SubtitleList> r = SubRip::parse(String(kThreeCues));
        REQUIRE(r.second().isOk());
        const SubtitleList &original = r.first();

        Buffer               emitted = SubRip::emit(original);
        Result<SubtitleList> r2 = SubRip::parse(emitted);
        REQUIRE(r2.second().isOk());
        CHECK(r2.first() == original);

        Buffer emitted2 = SubRip::emit(r2.first());
        REQUIRE(emitted.size() == emitted2.size());
        for (size_t i = 0; i < emitted.size(); ++i) {
                CHECK(static_cast<const uint8_t *>(emitted.data())[i]
                      == static_cast<const uint8_t *>(emitted2.data())[i]);
        }
}

TEST_CASE("SubRip: emit renumbers sequence indices from 1") {
        SubtitleList sl;
        sl.append(Subtitle(tsFromMs(0), tsFromMs(500), "a"));
        sl.append(Subtitle(tsFromMs(1000), tsFromMs(1500), "b"));
        String out = SubRip::emitString(sl);
        CHECK(out.startsWith("1\r\n"));
        CHECK(out.contains("\r\n2\r\n"));
}

TEST_CASE("SubRip: round-trips anchor + region preserved") {
        SubtitleList sl;
        Subtitle     s(tsFromMs(500), tsFromMs(2000), "Anchored", SubtitleAnchor::MiddleCenter);
        s.setRegion(Rect2Di32(100, 200, 400, 40));
        sl.append(s);

        Buffer               emitted = SubRip::emit(sl);
        Result<SubtitleList> r = SubRip::parse(emitted);
        REQUIRE(r.second().isOk());
        REQUIRE(r.first().size() == 1);
        CHECK(r.first()[0].anchor() == SubtitleAnchor::MiddleCenter);
        CHECK(r.first()[0].region() == Rect2Di32(100, 200, 400, 40));
        CHECK(r.first()[0].text() == "Anchored");
}

// ============================================================================
// SubtitleList helpers
// ============================================================================

TEST_CASE("SubtitleList: sortByStart orders cues chronologically (stable)") {
        SubtitleList sl;
        sl.append(Subtitle(tsFromMs(3000), tsFromMs(3500), "third"));
        sl.append(Subtitle(tsFromMs(1000), tsFromMs(1500), "first"));
        sl.append(Subtitle(tsFromMs(1000), tsFromMs(1200), "first-tie"));
        sl.append(Subtitle(tsFromMs(2000), tsFromMs(2500), "second"));
        sl.sortByStart();
        REQUIRE(sl.size() == 4);
        CHECK(sl[0].text() == "first");
        CHECK(sl[1].text() == "first-tie");
        CHECK(sl[2].text() == "second");
        CHECK(sl[3].text() == "third");
}

TEST_CASE("SubtitleList: findActiveAt locates the cue active at a timestamp") {
        SubtitleList sl;
        sl.append(Subtitle(tsFromMs(1000), tsFromMs(2000), "a"));
        sl.append(Subtitle(tsFromMs(3000), tsFromMs(4000), "b"));
        sl.append(Subtitle(tsFromMs(5000), tsFromMs(6000), "c"));

        CHECK(sl.findActiveAt(tsFromMs(500)) == -1);
        CHECK(sl.findActiveAt(tsFromMs(1500)) == 0);
        CHECK(sl.findActiveAt(tsFromMs(2500)) == -1);
        CHECK(sl.findActiveAt(tsFromMs(3500)) == 1);
        CHECK(sl.findActiveAt(tsFromMs(5500)) == 2);
        CHECK(sl.findActiveAt(tsFromMs(7000)) == -1);
}

TEST_CASE("SubtitleList: findNextAfter locates the next-starting cue") {
        SubtitleList sl;
        sl.append(Subtitle(tsFromMs(1000), tsFromMs(2000), "a"));
        sl.append(Subtitle(tsFromMs(3000), tsFromMs(4000), "b"));
        sl.append(Subtitle(tsFromMs(5000), tsFromMs(6000), "c"));

        CHECK(sl.findNextAfter(tsFromMs(0)) == 0);
        CHECK(sl.findNextAfter(tsFromMs(1500)) == 1);
        CHECK(sl.findNextAfter(tsFromMs(3500)) == 2);
        CHECK(sl.findNextAfter(tsFromMs(10000)) == -1);
}

TEST_CASE("SubtitleList: findInRange returns overlapping subset") {
        SubtitleList sl;
        sl.append(Subtitle(tsFromMs(1000), tsFromMs(2000), "a"));
        sl.append(Subtitle(tsFromMs(3000), tsFromMs(4000), "b"));
        sl.append(Subtitle(tsFromMs(5000), tsFromMs(6000), "c"));

        SubtitleList window = sl.findInRange(tsFromMs(1500), tsFromMs(3500));
        REQUIRE(window.size() == 2);
        CHECK(window[0].text() == "a");
        CHECK(window[1].text() == "b");
}

// ============================================================================
// Subtitle CoW semantics
// ============================================================================

TEST_CASE("Subtitle: copy is cheap; mutator detaches via CoW") {
        Subtitle a(tsFromMs(100), tsFromMs(200), "shared text");
        Subtitle b = a;
        // Pre-mutation: equal.
        CHECK(a == b);
        b.setText("modified");
        // Post-mutation: distinct.
        CHECK(a.text() == "shared text");
        CHECK(b.text() == "modified");
}

TEST_CASE("Subtitle: isActiveAt window check") {
        Subtitle s(tsFromMs(1000), tsFromMs(2000), "a");
        CHECK_FALSE(s.isActiveAt(tsFromMs(999)));
        CHECK(s.isActiveAt(tsFromMs(1000)));
        CHECK(s.isActiveAt(tsFromMs(1500)));
        CHECK_FALSE(s.isActiveAt(tsFromMs(2000)));
        CHECK_FALSE(s.isActiveAt(tsFromMs(2500)));
}

// ============================================================================
// Inline markup → SubtitleSpan
// ============================================================================

TEST_CASE("SubtitleSpan: hasStyle reports any style flag or explicit color") {
        SubtitleSpan plain("hi");
        CHECK_FALSE(plain.hasStyle());

        SubtitleSpan bold("hi", true, false, false);
        CHECK(bold.hasStyle());
        CHECK(bold.bold());
        CHECK_FALSE(bold.italic());

        SubtitleSpan coloured("hi", false, false, false, Color::Red);
        CHECK(coloured.hasStyle());
        CHECK(coloured.color().isValid());
}

TEST_CASE("SubRip: <i>...</i> parses to a single italic span") {
        const char *src = "1\r\n"
                          "00:00:01,000 --> 00:00:02,000\r\n"
                          "<i>only italic</i>\r\n"
                          "\r\n";
        Result<SubtitleList> r = SubRip::parse(String(src));
        REQUIRE(r.second().isOk());
        REQUIRE(r.first().size() == 1);
        REQUIRE(r.first()[0].spans().size() == 1);
        CHECK(r.first()[0].spans()[0].text() == "only italic");
        CHECK(r.first()[0].spans()[0].italic());
        CHECK_FALSE(r.first()[0].spans()[0].bold());
        CHECK(r.first()[0].text() == "only italic");
}

TEST_CASE("SubRip: mixed <b>/<u> markup splits into multiple spans") {
        const char *src = "1\r\n"
                          "00:00:01,000 --> 00:00:02,000\r\n"
                          "<b>Bold</b> and <u>underlined</u> mixed inline.\r\n"
                          "\r\n";
        Result<SubtitleList> r = SubRip::parse(String(src));
        REQUIRE(r.second().isOk());
        REQUIRE(r.first().size() == 1);
        const SubtitleSpan::List &spans = r.first()[0].spans();
        // Expected: [Bold] " and " [underlined] " mixed inline."
        REQUIRE(spans.size() == 4);
        CHECK(spans[0].text() == "Bold");
        CHECK(spans[0].bold());
        CHECK(spans[1].text() == " and ");
        CHECK_FALSE(spans[1].hasStyle());
        CHECK(spans[2].text() == "underlined");
        CHECK(spans[2].underline());
        CHECK(spans[3].text() == " mixed inline.");
        CHECK_FALSE(spans[3].hasStyle());
        CHECK(r.first()[0].text() == "Bold and underlined mixed inline.");
}

TEST_CASE("SubRip: <font color=...> drops a coloured span") {
        const char *src = "1\r\n"
                          "00:00:01,000 --> 00:00:02,000\r\n"
                          "<font color=\"#FF0000\">Red text</font> normal.\r\n"
                          "\r\n";
        Result<SubtitleList> r = SubRip::parse(String(src));
        REQUIRE(r.second().isOk());
        REQUIRE(r.first().size() == 1);
        const SubtitleSpan::List &spans = r.first()[0].spans();
        REQUIRE(spans.size() == 2);
        CHECK(spans[0].text() == "Red text");
        CHECK(spans[0].color().isValid());
        // Should be sRGB(1, 0, 0, 1) once parsed; check the channels.
        CHECK(spans[0].color().r8() == 255);
        CHECK(spans[0].color().g8() == 0);
        CHECK(spans[0].color().b8() == 0);
        CHECK(spans[1].text() == " normal.");
        CHECK_FALSE(spans[1].color().isValid());
}

TEST_CASE("SubRip: <v Speaker>...</v> captures into Subtitle::speaker") {
        const char *src = "1\r\n"
                          "00:00:01,000 --> 00:00:02,000\r\n"
                          "<v Narrator>hello there</v>\r\n"
                          "\r\n";
        Result<SubtitleList> r = SubRip::parse(String(src));
        REQUIRE(r.second().isOk());
        REQUIRE(r.first().size() == 1);
        CHECK(r.first()[0].speaker() == "Narrator");
        CHECK(r.first()[0].text() == "hello there");
}

TEST_CASE("SubRip: <font color=... background=...> picks up both fg and bg") {
        // SubRip's <font> tag commonly carries a `background` attribute
        // (libass / Aegisub convention).  Parser captures both into
        // the styled span; emit round-trips them in canonical form.
        const char *src = "1\r\n"
                          "00:00:01,000 --> 00:00:02,000\r\n"
                          "<font color=\"#FFFFFF\" background=\"#000080\">white on blue</font>\r\n"
                          "\r\n";
        Result<SubtitleList> r = SubRip::parse(String(src));
        REQUIRE(r.second().isOk());
        REQUIRE(r.first().size() == 1);
        const SubtitleSpan::List &spans = r.first()[0].spans();
        REQUIRE(spans.size() == 1);
        CHECK(spans[0].text() == "white on blue");
        CHECK(spans[0].color().isValid());
        CHECK(spans[0].color().r8() == 255);
        CHECK(spans[0].backgroundColor().isValid());
        CHECK(spans[0].backgroundColor().b8() == 128);
}

TEST_CASE("SubRip: styled spans round-trip parse -> emit -> parse") {
        SubtitleList sl;
        SubtitleSpan::List spans;
        spans.pushToBack(SubtitleSpan("Red bold ", true, false, false, Color::Red));
        spans.pushToBack(SubtitleSpan("plain"));
        Subtitle s(tsFromMs(1000), tsFromMs(2000), spans, SubtitleAnchor::Default,
                   Rect2Di32(), String("Narrator"), Metadata());
        sl.append(s);

        Buffer               b = SubRip::emit(sl);
        Result<SubtitleList> r = SubRip::parse(b);
        REQUIRE(r.second().isOk());
        REQUIRE(r.first().size() == 1);
        CHECK(r.first()[0].speaker() == "Narrator");
        REQUIRE(r.first()[0].spans().size() == 2);
        CHECK(r.first()[0].spans()[0].text() == "Red bold ");
        CHECK(r.first()[0].spans()[0].bold());
        CHECK(r.first()[0].spans()[0].color().isValid());
        CHECK(r.first()[0].spans()[0].color().r8() == 255);
        CHECK(r.first()[0].spans()[1].text() == "plain");
        CHECK_FALSE(r.first()[0].spans()[1].hasStyle());
}

// ============================================================================
// Variant round-trip
// ============================================================================

TEST_CASE("Subtitle: round-trips through Variant") {
        Subtitle s(tsFromMs(1000), tsFromMs(2500), "via Variant");
        Variant  v(s);
        const Subtitle *got = v.peek<Subtitle>();
        REQUIRE(got != nullptr);
        CHECK(got->text() == "via Variant");
        CHECK(got->start() == tsFromMs(1000));
        CHECK(got->end() == tsFromMs(2500));
}

TEST_CASE("Subtitle: mode round-trips through Variant + DataStream") {
        Subtitle s(tsFromMs(1000), tsFromMs(2000), "rollup");
        s.setMode(CaptionMode::RollUp);
        Variant         v(s);
        const Subtitle *got = v.peek<Subtitle>();
        REQUIRE(got != nullptr);
        CHECK(got->mode() == CaptionMode::RollUp);
}

TEST_CASE("SubtitleSpan: full styling fields round-trip via Subtitle/Variant") {
        // SubtitleSpan isn't a top-level Variant payload type — it rides
        // inside a Subtitle's spans list.  Wrap a maximally-styled span
        // in a Subtitle, round-trip via Variant (which uses the same
        // DataStream operators internally), and verify every new field
        // survives.
        SubtitleSpan span("styled", true, true, true, Color::Red);
        span.setBackgroundColor(Color::Blue);
        span.setEdgeColor(Color::Green);
        span.setEdgeStyle(SubtitleEdgeStyle::Raised);
        span.setFontFace(SubtitleFontFace::MonoSerif);
        span.setForegroundOpacity(SubtitleOpacity::Translucent);
        span.setBackgroundOpacity(SubtitleOpacity::Flash);
        span.setEdgeOpacity(SubtitleOpacity::Transparent);
        SubtitleSpan::List spans;
        spans.pushToBack(span);
        Subtitle in(tsFromMs(0), tsFromMs(1000), spans, SubtitleAnchor::Default, Rect2Di32(),
                    String(), Metadata());

        Variant         v(in);
        const Subtitle *got = v.peek<Subtitle>();
        REQUIRE(got != nullptr);
        REQUIRE(got->spans().size() == 1);
        const SubtitleSpan &gotSpan = got->spans()[0];
        CHECK(gotSpan.text() == "styled");
        CHECK(gotSpan.bold());
        CHECK(gotSpan.italic());
        CHECK(gotSpan.underline());
        CHECK(gotSpan.color() == Color::Red);
        CHECK(gotSpan.backgroundColor() == Color::Blue);
        CHECK(gotSpan.edgeColor() == Color::Green);
        CHECK(gotSpan.edgeStyle() == SubtitleEdgeStyle::Raised);
        CHECK(gotSpan.fontFace() == SubtitleFontFace::MonoSerif);
        CHECK(gotSpan.foregroundOpacity() == SubtitleOpacity::Translucent);
        CHECK(gotSpan.backgroundOpacity() == SubtitleOpacity::Flash);
        CHECK(gotSpan.edgeOpacity() == SubtitleOpacity::Transparent);
}

// ============================================================================
// etc/substest.srt fixture — exercises every SubRip parser branch
// ============================================================================

TEST_CASE("SubRip: etc/substest.srt parses cleanly and exercises every parser branch") {
        const String path = String(PROMEKI_SOURCE_DIR) + "/etc/substest.srt";
        File         f(path);
        REQUIRE(f.open(IODevice::ReadOnly).isOk());
        Result<int64_t> szR = f.size();
        REQUIRE(szR.second().isOk());
        const int64_t sz = szR.first();
        REQUIRE(sz > 0);
        Buffer buf(static_cast<size_t>(sz));
        buf.setSize(static_cast<size_t>(sz));
        REQUIRE(f.read(buf.data(), sz) == sz);
        f.close();

        Result<SubtitleList> r = SubRip::parse(buf);
        REQUIRE(r.second().isOk());
        const SubtitleList &sl = r.first();
        // The fixture is intended to grow as we add capabilities; assert
        // a lower bound rather than an exact count so adding cues doesn't
        // churn this test.
        REQUIRE(sl.size() >= 20);

        // -- Branch 1: plain ASCII cue with a comma timecode separator.
        CHECK(sl[0].text() == "Welcome to the libpromeki SubRip test file!");
        CHECK(sl[0].start() == tsFromMs(1000));
        CHECK(sl[0].end() == tsFromMs(4000));
        CHECK(sl[0].anchor() == SubtitleAnchor::Default);
        CHECK_FALSE(sl[0].region().isValid());

        // -- Branch 2: multi-line cue text (literal '\n' in Subtitle).
        CHECK(sl[1].text().contains("\n"));
        CHECK(sl[1].text() == "This cue has two lines (this one):\nand one more (this other one)");

        // -- Branch 3: ASS-style {\an8} → SubtitleAnchor::TopCenter (=8).
        CHECK(sl[2].anchor() == SubtitleAnchor::TopCenter);
        // Anchor prefix is stripped from the text.
        CHECK_FALSE(sl[2].text().startsWith("{"));

        // -- Branch 4: {\an5} → MiddleCenter.
        CHECK(sl[3].anchor() == SubtitleAnchor::MiddleCenter);

        // -- Branch 5: {\an1} → BottomLeft.
        CHECK(sl[4].anchor() == SubtitleAnchor::BottomLeft);

        // -- Branch 6: X1:.. Y2:.. coordinate suffix → Rect2Di32 region.
        CHECK(sl[5].region().isValid());
        CHECK(sl[5].region().x() == 40);
        CHECK(sl[5].region().y() == 50);
        CHECK(sl[5].region().width() == 640);
        CHECK(sl[5].region().height() == 30);

        // -- Branch 7: inline <i>…</i> markup parsed into a styled span.
        //    text() is the stripped plain text; spans() carries the
        //    italic flag.
        CHECK(sl[6].text() == "This text should display italic.");
        REQUIRE(sl[6].spans().size() == 1);
        CHECK(sl[6].spans()[0].italic());

        // -- Branch 8: inline <b> + <u> markup parsed into spans with
        //    the appropriate style flags.  The cue text mixes bold and
        //    underline runs, so we expect multiple spans.
        CHECK(sl[7].spans().size() >= 2);
        bool sawBold = false;
        bool sawUnderline = false;
        for (size_t k = 0; k < sl[7].spans().size(); ++k) {
                if (sl[7].spans()[k].bold()) sawBold = true;
                if (sl[7].spans()[k].underline()) sawUnderline = true;
        }
        CHECK(sawBold);
        CHECK(sawUnderline);

        // -- Branch 9: <font color="..."> parsed into a span with a
        //    valid Color override.  Round-trips through Color::fromString
        //    so the comparison is value-based, not byte-based.
        bool sawColoured = false;
        for (size_t k = 0; k < sl[8].spans().size(); ++k) {
                if (sl[8].spans()[k].color().isValid()) sawColoured = true;
        }
        CHECK(sawColoured);

        // -- Branch 10/11: UTF-8 round-trips (the parser is byte-transparent).
        CHECK(sl[10].text().contains("Caf\xC3\xA9")); // "Café"

        // -- Branch 12 (cue 13 in 0-indexed): non-Latin UTF-8 content kept.
        //    The fixture uses ASCII for the non-Latin description so the
        //    test stays portable; the parser-level UTF-8 path is already
        //    exercised by cue 11.

        // -- Branch 13: WebVTT-style <v Speaker>…</v> captured into
        //    Subtitle::speaker; the cue text drops the voice wrapper.
        CHECK(sl[12].speaker() == "John Bigboote");
        CHECK_FALSE(sl[12].text().contains("<v"));

        // -- Branch 14: punctuation + numbers passthrough.
        CHECK(sl[13].text().contains("0123456789"));

        // -- Branch 15: long single-line cue.
        CHECK(sl[14].text().size() > 80);

        // -- Branch 16: period as the ms separator (WebVTT-style).
        //    Fixture cue: 00:01:15.500 --> 00:01:18.500 → ms = 75500 / 78500.
        CHECK(sl[15].start() == tsFromMs(75500));
        CHECK(sl[15].end() == tsFromMs(78500));

        // -- Branch 17: three-line cue.
        size_t newlineCount = 0;
        for (size_t i = 0; i < sl[16].text().byteCount(); ++i) {
                if (sl[16].text().cstr()[i] == '\n') ++newlineCount;
        }
        CHECK(newlineCount == 2);

        // -- Branch 18: cue body with leading whitespace.  The parser
        //    only trims trailing whitespace per line, so the leading
        //    spaces survive verbatim.
        CHECK(sl[17].text().startsWith("  "));

        // -- Branch 19: full-cue <font color="..."> wrapper produces a
        //    color span covering the whole text; plain text() drops the
        //    tags.
        CHECK(sl[18].text() == "Remember: No matter where you go, there you are.");
        bool sawCue19Color = false;
        for (size_t k = 0; k < sl[18].spans().size(); ++k) {
                if (sl[18].spans()[k].color().isValid()) sawCue19Color = true;
        }
        CHECK(sawCue19Color);

        // -- Branch 20: {\an2} → BottomCenter (the SubRip default for
        //    captioning; explicit here so the round-trip emit re-stamps
        //    the prefix).
        CHECK(sl[19].anchor() == SubtitleAnchor::BottomCenter);
}

TEST_CASE("SubRip: etc/substest.srt round-trips byte-stable through parse->emit->parse") {
        const String path = String(PROMEKI_SOURCE_DIR) + "/etc/substest.srt";
        File         f(path);
        REQUIRE(f.open(IODevice::ReadOnly).isOk());
        Result<int64_t> szR = f.size();
        REQUIRE(szR.second().isOk());
        Buffer buf(static_cast<size_t>(szR.first()));
        buf.setSize(static_cast<size_t>(szR.first()));
        REQUIRE(f.read(buf.data(), szR.first()) == szR.first());
        f.close();

        Result<SubtitleList> r1 = SubRip::parse(buf);
        REQUIRE(r1.second().isOk());

        Buffer               emitted = SubRip::emit(r1.first());
        Result<SubtitleList> r2 = SubRip::parse(emitted);
        REQUIRE(r2.second().isOk());

        // Re-emitting r2 should match emitted byte-for-byte.  That's
        // the canonical-form stability check — proves the parser +
        // emitter agree on a fixed point even when the input file
        // uses non-canonical conventions (LF line endings, period ms
        // separator on one cue, etc.).
        Buffer emitted2 = SubRip::emit(r2.first());
        REQUIRE(emitted.size() == emitted2.size());
        for (size_t i = 0; i < emitted.size(); ++i) {
                CHECK(static_cast<const uint8_t *>(emitted.data())[i]
                      == static_cast<const uint8_t *>(emitted2.data())[i]);
        }
}
