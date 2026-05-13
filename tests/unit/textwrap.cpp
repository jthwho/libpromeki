/**
 * @file      textwrap.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/string.h>
#include <promeki/stringlist.h>
#include <promeki/textwrap.h>

using namespace promeki;

// ============================================================================
// Empty / degenerate inputs
// ============================================================================

TEST_CASE("TextWrap: empty input returns empty list") {
        TextWrap::Config cfg;
        cfg.maxCols = 32;
        StringList rows = TextWrap::reflow(String(), cfg);
        CHECK(rows.isEmpty());
}

TEST_CASE("TextWrap: all-whitespace input returns empty list") {
        TextWrap::Config cfg;
        cfg.maxCols = 32;
        StringList rows = TextWrap::reflow(String("   \n\t  \n  "), cfg);
        CHECK(rows.isEmpty());
}

TEST_CASE("TextWrap: maxCols = 0 returns single normalised row") {
        // No width cap: collapse internal whitespace, drop leading/
        // trailing — but keep everything on one row.
        TextWrap::Config cfg;
        StringList       rows = TextWrap::reflow(String("  hello   world\n\t foo  "), cfg);
        REQUIRE(rows.size() == 1);
        CHECK(rows[0] == "hello world foo");
}

TEST_CASE("TextWrap: single short line below maxCols stays on one row") {
        TextWrap::Config cfg;
        cfg.maxCols = 32;
        StringList rows = TextWrap::reflow(String("Hello world"), cfg);
        REQUIRE(rows.size() == 1);
        CHECK(rows[0] == "Hello world");
}

// ============================================================================
// Greedy policy
// ============================================================================

TEST_CASE("TextWrap[Greedy]: first-fit packing leaves a short trailing row") {
        TextWrap::Config cfg;
        cfg.maxCols = 16;
        cfg.policy = TextWrap::Policy::Greedy;
        // "Hello world this is a test" — words = [5,5,4,2,1,4].
        // Row 1: "Hello world this" (16, fits exactly).
        // Row 2: "is a test" (9).
        StringList rows = TextWrap::reflow(String("Hello world this is a test"), cfg);
        REQUIRE(rows.size() == 2);
        CHECK(rows[0] == "Hello world this");
        CHECK(rows[1] == "is a test");
}

TEST_CASE("TextWrap[Greedy]: long word longer than maxCols rides on its own row") {
        TextWrap::Config cfg;
        cfg.maxCols = 8;
        cfg.policy = TextWrap::Policy::Greedy;
        // "ok extraordinary ok" — middle word is 13 chars, exceeds 8.
        // Row 1: "ok" (2).
        // Row 2: "extraordinary" (13, exceeds cap).
        // Row 3: "ok" (2).
        StringList rows = TextWrap::reflow(String("ok extraordinary ok"), cfg);
        REQUIRE(rows.size() == 3);
        CHECK(rows[0] == "ok");
        CHECK(rows[1] == "extraordinary");
        CHECK(rows[2] == "ok");
}

// ============================================================================
// Balanced (minimax) policy
// ============================================================================

TEST_CASE("TextWrap[Balanced]: minimax produces tighter rows than greedy") {
        // "Hello world this is a test sentence" (35 chars).
        // Greedy at 32: ["Hello world this is a test" (26), "sentence" (8)]
        // Balanced uses min rows (2) and binary-searches for the tightest L.
        // Optimal L for 2 rows: ["Hello world this" (16), "is a test sentence" (18)]
        // → max line width 18.
        TextWrap::Config cfg;
        cfg.maxCols = 32;
        cfg.policy = TextWrap::Policy::Balanced;
        StringList rows = TextWrap::reflow(String("Hello world this is a test sentence"), cfg);
        REQUIRE(rows.size() == 2);
        CHECK(rows[0] == "Hello world this");
        CHECK(rows[1] == "is a test sentence");
}

TEST_CASE("TextWrap[Balanced]: row count tracks minimum feasible at maxCols") {
        // Words total 14 chars + 5 spaces = 19 wire chars at maxCols 32 → 1 row.
        TextWrap::Config cfg;
        cfg.maxCols = 32;
        cfg.policy = TextWrap::Policy::Balanced;
        StringList rows = TextWrap::reflow(String("a b c d e f"), cfg);
        REQUIRE(rows.size() == 1);
        CHECK(rows[0] == "a b c d e f");
}

TEST_CASE("TextWrap[Balanced]: 3-row layout when 2 rows would overflow") {
        // 7-word sentence, 36 chars + 6 spaces = 42 total.
        // Greedy at 16: row 1 "alpha bravo" (11), row 2 "charlie delta" (13),
        // row 3 "echo foxtrot golf" (17 — exceeds 16!).
        // Try maxCols 16 → greedy gives:
        //   "alpha bravo" (11), "charlie" (7), "delta echo" (10), "foxtrot golf" (12)
        //   = 4 rows.
        // Minimax for 4 rows at L = max word = 7 minimum.
        // Result balances at the smallest L producing 4 rows.
        TextWrap::Config cfg;
        cfg.maxCols = 16;
        cfg.policy = TextWrap::Policy::Balanced;
        StringList rows = TextWrap::reflow(String("alpha bravo charlie delta echo foxtrot golf"), cfg);
        // Every row fits maxCols.
        for (size_t i = 0; i < rows.size(); ++i) {
                CHECK(rows[i].length() <= 16);
        }
        // The longest row is at most as long as the greedy-at-16 longest.
        size_t maxLen = 0;
        for (size_t i = 0; i < rows.size(); ++i) {
                if (rows[i].length() > maxLen) maxLen = rows[i].length();
        }
        CHECK(maxLen <= 13); // greedy-at-16 longest was 13 → balanced ≤ 13
}

// ============================================================================
// tryExplicitBreaks
// ============================================================================

TEST_CASE("TextWrap[tryExplicitBreaks]: layout fits → returned verbatim") {
        TextWrap::Config cfg;
        cfg.maxCols = 32;
        cfg.maxRows = 3;
        cfg.tryExplicitBreaks = true;
        StringList rows = TextWrap::reflow(String("Line one\nLine two"), cfg);
        REQUIRE(rows.size() == 2);
        CHECK(rows[0] == "Line one");
        CHECK(rows[1] == "Line two");
}

TEST_CASE("TextWrap[tryExplicitBreaks]: row overflows maxCols → re-flow whole text") {
        TextWrap::Config cfg;
        cfg.maxCols = 16;
        cfg.maxRows = 3;
        cfg.tryExplicitBreaks = true;
        // Explicit layout: "Short" (5) + "This second line is far too long for the cap" (44).
        // Second explicit row overflows 16 → ignore '\n', re-flow whole text.
        StringList rows = TextWrap::reflow(
                String("Short\nThis second line is far too long for the cap"), cfg);
        // Joined: "Short This second line is far too long for the cap" (51 chars).
        // Either we got the verbatim 2-row explicit layout (forbidden:
        // row[1] was > 16) or we re-flowed; in either case every row
        // must fit the width cap.
        for (size_t i = 0; i < rows.size(); ++i) {
                CHECK(rows[i].length() <= 16);
        }
        // And the second row in particular must not be the original
        // 44-char explicit line.
        REQUIRE(rows.size() >= 2);
        CHECK(rows[1] != "This second line is far too long for the cap");
}

TEST_CASE("TextWrap[tryExplicitBreaks]: explicit row count exceeds maxRows → re-flow") {
        TextWrap::Config cfg;
        cfg.maxCols = 32;
        cfg.maxRows = 2;
        cfg.tryExplicitBreaks = true;
        // Explicit layout has 3 rows, exceeds maxRows=2 → re-flow whole.
        StringList rows = TextWrap::reflow(String("alpha\nbravo\ncharlie"), cfg);
        // Re-flow: "alpha bravo charlie" (19 chars) fits in one row at maxCols 32.
        REQUIRE(rows.size() == 1);
        CHECK(rows[0] == "alpha bravo charlie");
}

TEST_CASE("TextWrap[tryExplicitBreaks]: empty lines in input are dropped") {
        // SubRip / WebVTT can produce blank rows from cosmetic line
        // groupings — the wrapper should drop them, not honour empty
        // rows.
        TextWrap::Config cfg;
        cfg.maxCols = 32;
        cfg.maxRows = 3;
        cfg.tryExplicitBreaks = true;
        StringList rows = TextWrap::reflow(String("first\n\nsecond"), cfg);
        REQUIRE(rows.size() == 2);
        CHECK(rows[0] == "first");
        CHECK(rows[1] == "second");
}

// ============================================================================
// Overflow surfacing
// ============================================================================

TEST_CASE("TextWrap: row count may exceed maxRows when text genuinely doesn't fit") {
        // 4 distinct words of 12 chars each at maxCols=12 → must produce
        // 4 rows.  maxRows=2 is a soft target — the caller checks.
        TextWrap::Config cfg;
        cfg.maxCols = 12;
        cfg.maxRows = 2;
        cfg.policy = TextWrap::Policy::Balanced;
        StringList rows = TextWrap::reflow(String("aaaaaaaaaaaa bbbbbbbbbbbb cccccccccccc dddddddddddd"), cfg);
        REQUIRE(rows.size() == 4);
        // Caller can compare rows.size() > cfg.maxRows and take action.
        CHECK(rows.size() > static_cast<size_t>(cfg.maxRows));
}

// ============================================================================
// Whitespace normalisation
// ============================================================================

TEST_CASE("TextWrap: collapses internal whitespace runs to single spaces") {
        TextWrap::Config cfg;
        cfg.maxCols = 32;
        StringList rows = TextWrap::reflow(String("foo    bar\t\tbaz"), cfg);
        REQUIRE(rows.size() == 1);
        CHECK(rows[0] == "foo bar baz");
}

TEST_CASE("TextWrap: leading and trailing whitespace are dropped") {
        TextWrap::Config cfg;
        cfg.maxCols = 32;
        StringList rows = TextWrap::reflow(String("   hello world   "), cfg);
        REQUIRE(rows.size() == 1);
        CHECK(rows[0] == "hello world");
}

// ============================================================================
// rowBreaks primitive (preserves per-word metadata)
// ============================================================================

TEST_CASE("TextWrap::rowBreaks: empty word list returns {0}") {
        TextWrap::Config cfg;
        cfg.maxCols = 32;
        List<size_t> breaks = TextWrap::rowBreaks(List<size_t>(), cfg);
        REQUIRE(breaks.size() == 1);
        CHECK(breaks[0] == 0);
}

TEST_CASE("TextWrap::rowBreaks: single row when everything fits") {
        TextWrap::Config cfg;
        cfg.maxCols = 32;
        // widths 5+5+4+2+1+4 = 21 + 5 spaces = 26.
        List<size_t> widths;
        for (size_t w : {size_t(5), size_t(5), size_t(4), size_t(2), size_t(1), size_t(4)})
                widths.pushToBack(w);
        List<size_t> breaks = TextWrap::rowBreaks(widths, cfg);
        REQUIRE(breaks.size() == 2);
        CHECK(breaks[0] == 0);
        CHECK(breaks[1] == 6);
}

TEST_CASE("TextWrap::rowBreaks[Balanced]: two-row split is minimax-balanced") {
        TextWrap::Config cfg;
        cfg.maxCols = 32;
        cfg.policy = TextWrap::Policy::Balanced;
        // "Hello world this is a test sentence" (widths 5,5,4,2,1,4,8).
        // Greedy at 32 would give one row.  Force two rows via tight cap.
        cfg.maxCols = 18;
        List<size_t> widths;
        for (size_t w : {size_t(5), size_t(5), size_t(4), size_t(2), size_t(1), size_t(4), size_t(8)})
                widths.pushToBack(w);
        List<size_t> breaks = TextWrap::rowBreaks(widths, cfg);
        // Expect 2 rows.  Break at word 3:
        //   row 0 = words[0..3) widths 5+5+4 + 2 spaces = 16
        //   row 1 = words[3..7) widths 2+1+4+8 + 3 spaces = 18
        // Both ≤ 18, max = 18, balanced.
        REQUIRE(breaks.size() == 3);
        CHECK(breaks[0] == 0);
        CHECK(breaks[1] == 3);
        CHECK(breaks[2] == 7);
}

TEST_CASE("TextWrap::rowBreaks[Greedy]: first-fit packing") {
        TextWrap::Config cfg;
        cfg.maxCols = 16;
        cfg.policy = TextWrap::Policy::Greedy;
        // widths 5,5,4,2,1,4 — same as the greedy-policy reflow test.
        List<size_t> widths;
        for (size_t w : {size_t(5), size_t(5), size_t(4), size_t(2), size_t(1), size_t(4)})
                widths.pushToBack(w);
        List<size_t> breaks = TextWrap::rowBreaks(widths, cfg);
        // Greedy fills row 1 with [0,1,2] (widths 5+5+4 + 2 spaces = 16,
        // fits exactly), row 2 with [3,4,5] (2+1+4 + 2 spaces = 9).
        REQUIRE(breaks.size() == 3);
        CHECK(breaks[0] == 0);
        CHECK(breaks[1] == 3);
        CHECK(breaks[2] == 6);
}
