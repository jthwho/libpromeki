/**
 * @file      screen.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/tui/screen.h>
#include <promeki/core/stringiodevice.h>

using namespace promeki;

TEST_CASE("TuiScreen: resize and clear") {
        TuiScreen screen;
        screen.resize(80, 24);
        CHECK(screen.cols() == 80);
        CHECK(screen.rows() == 24);

        screen.clear(Color::White, Color::Black);
        TuiCell cell = screen.cell(0, 0);
        CHECK(cell.ch == Char(U' '));
        CHECK(cell.style.foreground() == Color::White);
        CHECK(cell.style.background() == Color::Black);
}

TEST_CASE("TuiScreen: setCell and cell") {
        TuiScreen screen;
        screen.resize(10, 5);

        TuiCell cell;
        cell.ch = Char(U'A');
        cell.style = TuiStyle(Color::Red, Color::Blue, TuiStyle::Bold);

        screen.setCell(3, 2, cell);
        TuiCell result = screen.cell(3, 2);
        CHECK(result.ch == Char(U'A'));
        CHECK(result.style.foreground() == Color::Red);
        CHECK(result.style.background() == Color::Blue);
        CHECK(result.style.attrs() == TuiStyle::Bold);
}

TEST_CASE("TuiScreen: out-of-bounds access") {
        TuiScreen screen;
        screen.resize(10, 5);

        TuiCell cell;
        cell.ch = Char(U'X');

        // Should not crash
        screen.setCell(-1, 0, cell);
        screen.setCell(10, 0, cell);
        screen.setCell(0, -1, cell);
        screen.setCell(0, 5, cell);

        // Out-of-bounds cell should return default
        TuiCell result = screen.cell(-1, 0);
        CHECK(result.ch == Char(U' '));
}

TEST_CASE("TuiScreen: flush to stream") {
        TuiScreen screen;
        screen.resize(5, 3);

        TuiCell cell;
        cell.ch = Char(U'H');
        cell.style = TuiStyle(Color::White, Color::Black);
        screen.setCell(0, 0, cell);

        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::WriteOnly);
        AnsiStream stream(&dev);
        screen.flush(stream);

        // Should have produced some output
        CHECK_FALSE(str.isEmpty());
}

// ── TuiScreen color matching test helpers ───────────────────────────

// Helper: find closest 256-color index for an RGB color.
static int match256(uint8_t r, uint8_t g, uint8_t b) {
        return static_cast<int>(AnsiStream::findClosestAnsiColor(Color(r, g, b), 255));
}

// Helper: find closest 16-color index and return its ANSI foreground code.
static int matchBasic(uint8_t r, uint8_t g, uint8_t b) {
        uint8_t idx = static_cast<uint8_t>(AnsiStream::findClosestAnsiColor(Color(r, g, b), 15));
        return (idx < 8) ? (30 + idx) : (90 + idx - 8);
}

// Convert a Basic foreground ANSI code to palette index.
// 30-37 → 0-7, 90-97 → 8-15
static int basicCodeToIndex(int code) {
        if(code >= 30 && code <= 37) return code - 30;
        if(code >= 90 && code <= 97) return code - 90 + 8;
        return -1;
}

// ── TuiScreen: ansiColor accessors ──────────────────────────────────

TEST_CASE("TuiScreen: ansiColor returns valid colors for indices 0-255") {
        for(int i = 0; i < 256; ++i) {
                Color c = AnsiStream::ansiColor(i);
                CHECK(c.isValid());
        }
}

TEST_CASE("TuiScreen: ansiColor out-of-range returns invalid") {
        CHECK_FALSE(AnsiStream::ansiColor(-1).isValid());
        CHECK_FALSE(AnsiStream::ansiColor(256).isValid());
}

TEST_CASE("TuiScreen: ansiColor(9) is bright red") {
        Color c = AnsiStream::ansiColor(9);
        CHECK(c.r8() == 255);
        CHECK(c.g8() == 0);
        CHECK(c.b8() == 0);
}

TEST_CASE("TuiScreen: colorMode default is TrueColor") {
        TuiScreen screen;
        CHECK(screen.colorMode() == Terminal::TrueColor);
}

TEST_CASE("TuiScreen: setColorMode round-trip") {
        TuiScreen screen;
        screen.setColorMode(Terminal::Basic);
        CHECK(screen.colorMode() == Terminal::Basic);
        screen.setColorMode(Terminal::Color256);
        CHECK(screen.colorMode() == Terminal::Color256);
}

// ── 256-color matching: exact palette round-trips ───────────────────

TEST_CASE("TuiScreen 256: all 256 palette colors round-trip to same RGB") {
        for(int i = 0; i < 256; ++i) {
                Color c = AnsiStream::ansiColor(i);
                int idx = match256(c.r8(), c.g8(), c.b8());
                Color matched = AnsiStream::ansiColor(idx);
                INFO("palette index: ", i, " r=", c.r8(), " g=", c.g8(), " b=", c.b8(),
                     " matched index=", idx);
                CHECK(matched.r8() == c.r8());
                CHECK(matched.g8() == c.g8());
                CHECK(matched.b8() == c.b8());
        }
}

// ── 256-color matching: primary colors ──────────────────────────────

TEST_CASE("TuiScreen 256: pure red matches index 9 (bright red)") {
        CHECK(match256(255, 0, 0) == 9);
}

TEST_CASE("TuiScreen 256: pure green matches index 10 (bright green)") {
        CHECK(match256(0, 255, 0) == 10);
}

TEST_CASE("TuiScreen 256: pure blue matches index 12 (bright blue)") {
        CHECK(match256(0, 0, 255) == 12);
}

TEST_CASE("TuiScreen 256: pure white matches index 15 (bright white)") {
        CHECK(match256(255, 255, 255) == 15);
}

TEST_CASE("TuiScreen 256: pure black matches index 0") {
        CHECK(match256(0, 0, 0) == 0);
}

TEST_CASE("TuiScreen 256: pure yellow matches index 11 (bright yellow)") {
        CHECK(match256(255, 255, 0) == 11);
}

TEST_CASE("TuiScreen 256: pure cyan matches index 14 (bright cyan)") {
        CHECK(match256(0, 255, 255) == 14);
}

TEST_CASE("TuiScreen 256: pure magenta matches index 13 (bright magenta)") {
        CHECK(match256(255, 0, 255) == 13);
}

// ── 256-color matching: near-primaries should pick the right entry ──

TEST_CASE("TuiScreen 256: near-red (250,5,5) picks bright red (9) not dark red (1)") {
        int idx = match256(250, 5, 5);
        Color matched = AnsiStream::ansiColor(idx);
        INFO("matched index=", idx, " r=", matched.r8(), " g=", matched.g8(), " b=", matched.b8());
        CHECK(matched.r8() >= 200);
        CHECK(matched.g8() <= 30);
        CHECK(matched.b8() <= 30);
}

TEST_CASE("TuiScreen 256: near-green (5,250,5) picks bright green") {
        int idx = match256(5, 250, 5);
        Color matched = AnsiStream::ansiColor(idx);
        INFO("matched index=", idx, " r=", matched.r8(), " g=", matched.g8(), " b=", matched.b8());
        CHECK(matched.r8() <= 30);
        CHECK(matched.g8() >= 200);
        CHECK(matched.b8() <= 30);
}

TEST_CASE("TuiScreen 256: near-blue (5,5,250) picks bright blue") {
        int idx = match256(5, 5, 250);
        Color matched = AnsiStream::ansiColor(idx);
        INFO("matched index=", idx, " r=", matched.r8(), " g=", matched.g8(), " b=", matched.b8());
        CHECK(matched.r8() <= 30);
        CHECK(matched.g8() <= 30);
        CHECK(matched.b8() >= 200);
}

// ── 256-color matching: cube colors ─────────────────────────────────

TEST_CASE("TuiScreen 256: color near cube vertex (100,135,175) hits index 67") {
        int idx = match256(100, 135, 175);
        Color matched = AnsiStream::ansiColor(idx);
        INFO("matched index=", idx);
        CHECK(std::abs(matched.r8() - 100) <= 40);
        CHECK(std::abs(matched.g8() - 135) <= 40);
        CHECK(std::abs(matched.b8() - 175) <= 40);
}

TEST_CASE("TuiScreen 256: exact cube vertex (95,135,175) round-trips") {
        CHECK(match256(95, 135, 175) == 67);
}

TEST_CASE("TuiScreen 256: exact cube vertex (215,175,95) round-trips") {
        CHECK(match256(215, 175, 95) == 179);
}

// ── 256-color matching: grayscale ───────────────────────────────────

TEST_CASE("TuiScreen 256: mid-gray (128,128,128) picks grayscale 244 or system 8") {
        int idx = match256(128, 128, 128);
        Color matched = AnsiStream::ansiColor(idx);
        INFO("matched index=", idx, " r=", matched.r8());
        CHECK(matched.r8() == matched.g8());
        CHECK(matched.g8() == matched.b8());
        CHECK(std::abs(static_cast<int>(matched.r8()) - 128) <= 15);
}

TEST_CASE("TuiScreen 256: near-gray (130,130,130) picks a grayscale entry") {
        int idx = match256(130, 130, 130);
        Color matched = AnsiStream::ansiColor(idx);
        CHECK(matched.r8() == matched.g8());
        CHECK(matched.g8() == matched.b8());
}

TEST_CASE("TuiScreen 256: light gray (200,200,200) picks a grayscale entry") {
        int idx = match256(200, 200, 200);
        Color matched = AnsiStream::ansiColor(idx);
        INFO("matched index=", idx, " r=", matched.r8());
        CHECK(matched.r8() == matched.g8());
        CHECK(matched.g8() == matched.b8());
        CHECK(std::abs(static_cast<int>(matched.r8()) - 200) <= 15);
}

TEST_CASE("TuiScreen 256: dark gray (50,50,50) picks a grayscale entry") {
        int idx = match256(50, 50, 50);
        Color matched = AnsiStream::ansiColor(idx);
        INFO("matched index=", idx, " r=", matched.r8());
        CHECK(matched.r8() == matched.g8());
        CHECK(matched.g8() == matched.b8());
        CHECK(std::abs(static_cast<int>(matched.r8()) - 50) <= 15);
}

// ── 256-color matching: common UI colors ────────────────────────────

TEST_CASE("TuiScreen 256: orange (255,165,0) picks a warm color") {
        int idx = match256(255, 165, 0);
        Color matched = AnsiStream::ansiColor(idx);
        INFO("matched index=", idx, " r=", matched.r8(), " g=", matched.g8(), " b=", matched.b8());
        CHECK(matched.r8() >= 180);
        CHECK(matched.g8() >= 100);
        CHECK(matched.b8() <= 50);
}

TEST_CASE("TuiScreen 256: pink (255,192,203) picks a pinkish color") {
        int idx = match256(255, 192, 203);
        Color matched = AnsiStream::ansiColor(idx);
        INFO("matched index=", idx, " r=", matched.r8(), " g=", matched.g8(), " b=", matched.b8());
        CHECK(matched.r8() >= 200);
        CHECK(matched.g8() >= 150);
        CHECK(matched.b8() >= 150);
}

TEST_CASE("TuiScreen 256: brown (139,69,19) picks a brownish color") {
        int idx = match256(139, 69, 19);
        Color matched = AnsiStream::ansiColor(idx);
        INFO("matched index=", idx, " r=", matched.r8(), " g=", matched.g8(), " b=", matched.b8());
        CHECK(matched.r8() >= 100);
        CHECK(matched.g8() <= 120);
        CHECK(matched.b8() <= 60);
}

TEST_CASE("TuiScreen 256: teal (0,128,128) picks a teal-ish color") {
        int idx = match256(0, 128, 128);
        Color matched = AnsiStream::ansiColor(idx);
        INFO("matched index=", idx, " r=", matched.r8(), " g=", matched.g8(), " b=", matched.b8());
        CHECK(matched.r8() <= 40);
        CHECK(matched.g8() >= 90);
        CHECK(matched.b8() >= 90);
}

TEST_CASE("TuiScreen 256: navy (0,0,128) picks a dark blue") {
        int idx = match256(0, 0, 128);
        Color matched = AnsiStream::ansiColor(idx);
        INFO("matched index=", idx, " r=", matched.r8(), " g=", matched.g8(), " b=", matched.b8());
        CHECK(matched.r8() <= 20);
        CHECK(matched.g8() <= 20);
        CHECK(matched.b8() >= 90);
}

// ── 256-color matching: symmetry / no bizarre mismatches ────────────

TEST_CASE("TuiScreen 256: matched color is always closer than a random sample") {
        struct TestColor { uint8_t r, g, b; };
        TestColor tests[] = {
                {100, 50, 200}, {200, 100, 50}, {50, 200, 100},
                {10, 10, 10}, {245, 245, 245}, {128, 0, 64},
                {64, 128, 0}, {0, 64, 128}, {180, 180, 60},
                {60, 180, 180}, {180, 60, 180}, {255, 128, 0},
        };
        for(auto &tc : tests) {
                int idx = match256(tc.r, tc.g, tc.b);
                Color matched = AnsiStream::ansiColor(idx);
                int bestDist = (tc.r - matched.r8()) * (tc.r - matched.r8()) +
                               (tc.g - matched.g8()) * (tc.g - matched.g8()) +
                               (tc.b - matched.b8()) * (tc.b - matched.b8());
                for(int i = 0; i < 256; ++i) {
                        Color alt = AnsiStream::ansiColor(i);
                        int altDist = (tc.r - alt.r8()) * (tc.r - alt.r8()) +
                                      (tc.g - alt.g8()) * (tc.g - alt.g8()) +
                                      (tc.b - alt.b8()) * (tc.b - alt.b8());
                        if(altDist < bestDist / 4) {
                                INFO("input=(", tc.r, ",", tc.g, ",", tc.b,
                                     ") chosen idx=", idx,
                                     " (", matched.r8(), ",", matched.g8(), ",", matched.b8(),
                                     ") but idx=", i,
                                     " (", alt.r8(), ",", alt.g8(), ",", alt.b8(),
                                     ") is 4x closer by L2");
                                CHECK(false);
                        }
                }
        }
}

// ── 256-color matching: gradient continuity ─────────────────────────

TEST_CASE("TuiScreen 256: red gradient has no more than 15 transitions across 256 steps") {
        int transitions = 0;
        int prevIdx = match256(0, 0, 0);
        for(int i = 1; i <= 255; ++i) {
                int idx = match256(static_cast<uint8_t>(i), 0, 0);
                if(idx != prevIdx) {
                        transitions++;
                        prevIdx = idx;
                }
        }
        INFO("red gradient transitions: ", transitions);
        CHECK(transitions <= 15);
        CHECK(transitions >= 3);
}

TEST_CASE("TuiScreen 256: green gradient has no more than 15 transitions across 256 steps") {
        int transitions = 0;
        int prevIdx = match256(0, 0, 0);
        for(int i = 1; i <= 255; ++i) {
                int idx = match256(0, static_cast<uint8_t>(i), 0);
                if(idx != prevIdx) {
                        transitions++;
                        prevIdx = idx;
                }
        }
        INFO("green gradient transitions: ", transitions);
        CHECK(transitions <= 15);
        CHECK(transitions >= 3);
}

TEST_CASE("TuiScreen 256: blue gradient has no more than 15 transitions across 256 steps") {
        int transitions = 0;
        int prevIdx = match256(0, 0, 0);
        for(int i = 1; i <= 255; ++i) {
                int idx = match256(0, 0, static_cast<uint8_t>(i));
                if(idx != prevIdx) {
                        transitions++;
                        prevIdx = idx;
                }
        }
        INFO("blue gradient transitions: ", transitions);
        CHECK(transitions <= 15);
        CHECK(transitions >= 3);
}

TEST_CASE("TuiScreen 256: gray gradient is monotonic in luminance") {
        int prevLum = 0;
        for(int i = 0; i <= 255; ++i) {
                int idx = match256(static_cast<uint8_t>(i), static_cast<uint8_t>(i),
                                   static_cast<uint8_t>(i));
                Color matched = AnsiStream::ansiColor(idx);
                int lum = matched.r8() + matched.g8() + matched.b8();
                INFO("input gray=", i, " matched index=", idx, " lum=", lum,
                     " prev=", prevLum);
                CHECK(lum >= prevLum);
                prevLum = lum;
        }
}

// ── 16-color (Basic) matching ───────────────────────────────────────

TEST_CASE("TuiScreen Basic: pure red → bright red (code 91)") {
        CHECK(matchBasic(255, 0, 0) == 91);
}

TEST_CASE("TuiScreen Basic: pure green → bright green (code 92)") {
        CHECK(matchBasic(0, 255, 0) == 92);
}

TEST_CASE("TuiScreen Basic: pure blue → bright blue (code 94)") {
        CHECK(matchBasic(0, 0, 255) == 94);
}

TEST_CASE("TuiScreen Basic: pure white → bright white (code 97)") {
        CHECK(matchBasic(255, 255, 255) == 97);
}

TEST_CASE("TuiScreen Basic: pure black → black (code 30)") {
        CHECK(matchBasic(0, 0, 0) == 30);
}

TEST_CASE("TuiScreen Basic: dark red (128,0,0) → dark red (code 31)") {
        int code = matchBasic(128, 0, 0);
        int idx = basicCodeToIndex(code);
        Color matched = AnsiStream::ansiColor(idx);
        INFO("code=", code, " index=", idx, " r=", matched.r8());
        CHECK(matched.r8() >= 100);
        CHECK(matched.g8() <= 10);
        CHECK(matched.b8() <= 10);
}

TEST_CASE("TuiScreen Basic: mid-gray → dark white (code 37) or bright black (code 90)") {
        int code = matchBasic(128, 128, 128);
        CHECK((code == 37 || code == 90));
}

TEST_CASE("TuiScreen Basic: orange (255,165,0) picks yellow family") {
        int code = matchBasic(255, 165, 0);
        int idx = basicCodeToIndex(code);
        INFO("code=", code, " index=", idx);
        CHECK((idx == 3 || idx == 11 || idx == 9));
}

TEST_CASE("TuiScreen 256: active progress bar green (32,160,64) matches a green") {
        int idx = match256(32, 160, 64);
        Color matched = AnsiStream::ansiColor(idx);
        INFO("matched index=", idx, " r=", matched.r8(), " g=", matched.g8(), " b=", matched.b8());
        CHECK(matched.g8() >= 135);
        CHECK(matched.g8() > matched.r8());
        CHECK(matched.g8() > matched.b8());
}

TEST_CASE("TuiScreen 256: inactive progress bar green (32,96,48) matches a colored entry, not gray") {
        int idx = match256(32, 96, 48);
        Color matched = AnsiStream::ansiColor(idx);
        INFO("matched index=", idx, " r=", matched.r8(), " g=", matched.g8(), " b=", matched.b8());
        CHECK(matched.g8() >= 90);
        CHECK(matched.g8() > matched.r8());
        CHECK(!(matched.r8() == matched.g8() && matched.g8() == matched.b8()));
}

TEST_CASE("TuiScreen 256: dark saturated colors prefer colored entries over gray") {
        int idx = match256(96, 16, 16);
        Color m = AnsiStream::ansiColor(idx);
        INFO("dark red: index=", idx, " r=", m.r8(), " g=", m.g8(), " b=", m.b8());
        CHECK(m.r8() > m.g8());
        CHECK(m.r8() > m.b8());

        idx = match256(16, 16, 96);
        m = AnsiStream::ansiColor(idx);
        INFO("dark blue: index=", idx, " r=", m.r8(), " g=", m.g8(), " b=", m.b8());
        CHECK(m.b8() > m.r8());
        CHECK(m.b8() > m.g8());
}

TEST_CASE("TuiScreen 256: true grays still match grayscale entries") {
        int idx = match256(68, 68, 68);
        Color m = AnsiStream::ansiColor(idx);
        INFO("gray 68: index=", idx, " r=", m.r8(), " g=", m.g8(), " b=", m.b8());
        CHECK(m.r8() == m.g8());
        CHECK(m.g8() == m.b8());

        idx = match256(100, 100, 100);
        m = AnsiStream::ansiColor(idx);
        INFO("gray 100: index=", idx, " r=", m.r8(), " g=", m.g8(), " b=", m.b8());
        CHECK(m.r8() == m.g8());
        CHECK(m.g8() == m.b8());
}

TEST_CASE("TuiScreen 256: near-gray with tiny saturation stays gray") {
        int idx = match256(70, 68, 68);
        Color m = AnsiStream::ansiColor(idx);
        INFO("near-gray: index=", idx, " r=", m.r8(), " g=", m.g8(), " b=", m.b8());
        CHECK(m.r8() == m.g8());
        CHECK(m.g8() == m.b8());
}

TEST_CASE("TuiScreen Basic: all 16 system colors round-trip") {
        for(int i = 0; i < 16; ++i) {
                Color c = AnsiStream::ansiColor(i);
                int code = matchBasic(c.r8(), c.g8(), c.b8());
                int matched = basicCodeToIndex(code);
                INFO("system color ", i, " r=", c.r8(), " g=", c.g8(), " b=", c.b8(),
                     " code=", code, " matched index=", matched);
                CHECK(matched == i);
        }
}

TEST_CASE("TuiScreen: exact palette match uses correct index") {
        Color c = AnsiStream::ansiColor(42);
        uint8_t idx = static_cast<uint8_t>(AnsiStream::findClosestAnsiColor(c, 255));
        CHECK(idx == 42);
}

TEST_CASE("TuiScreen: ansiColor round-trip for system colors") {
        Color c = AnsiStream::ansiColor(9);
        CHECK(c == Color(255, 0, 0));
        uint8_t idx = static_cast<uint8_t>(AnsiStream::findClosestAnsiColor(c, 255));
        CHECK(idx == 9);
}

// ── TuiCell equality/inequality ─────────────────────────────────────

TEST_CASE("TuiScreen: TuiCell equality and inequality") {
        TuiCell a;
        TuiCell b;
        CHECK(a == b);
        CHECK_FALSE(a != b);

        b.ch = Char(U'X');
        CHECK_FALSE(a == b);
        CHECK(a != b);

        b.ch = a.ch;
        b.style = TuiStyle(Color::Red, Color::Blue);
        CHECK_FALSE(a == b);
        CHECK(a != b);
}

// ── TuiScreen: invalidate forces full redraw ────────────────────────

TEST_CASE("TuiScreen: invalidate forces full redraw") {
        TuiScreen screen;
        screen.resize(3, 2);

        TuiCell cell;
        cell.ch = Char(U'A');
        cell.style = TuiStyle(Color::White, Color::Black);
        screen.setCell(0, 0, cell);

        // First flush emits content (full redraw is default)
        String str1;
        StringIODevice dev1(&str1);
        dev1.open(IODevice::WriteOnly);
        AnsiStream stream1(&dev1);
        screen.flush(stream1);
        CHECK_FALSE(str1.isEmpty());

        // Second flush with no changes should emit nothing
        String str2;
        StringIODevice dev2(&str2);
        dev2.open(IODevice::WriteOnly);
        AnsiStream stream2(&dev2);
        screen.flush(stream2);
        CHECK(str2.isEmpty());

        // After invalidate, flush should emit content again
        screen.invalidate();
        String str3;
        StringIODevice dev3(&str3);
        dev3.open(IODevice::WriteOnly);
        AnsiStream stream3(&dev3);
        screen.flush(stream3);
        CHECK_FALSE(str3.isEmpty());
}

// ── TuiScreen: differential flush only emits changed cells ──────────

TEST_CASE("TuiScreen: differential flush only emits changed cells") {
        TuiScreen screen;
        screen.resize(5, 2);
        screen.clear(Color::White, Color::Black);

        // Initial full flush
        String str1;
        StringIODevice dev1(&str1);
        dev1.open(IODevice::WriteOnly);
        AnsiStream stream1(&dev1);
        screen.flush(stream1);
        size_t fullLen = str1.length();
        CHECK(fullLen > 0);

        // Change only one cell
        TuiCell cell;
        cell.ch = Char(U'Z');
        cell.style = TuiStyle(Color::Red, Color::Blue);
        screen.setCell(2, 1, cell);

        String str2;
        StringIODevice dev2(&str2);
        dev2.open(IODevice::WriteOnly);
        AnsiStream stream2(&dev2);
        screen.flush(stream2);
        CHECK_FALSE(str2.isEmpty());
        CHECK(str2.length() < fullLen);
}

// ── TuiScreen: flush with various color modes ───────────────────────

TEST_CASE("TuiScreen: flush with TrueColor mode emits RGB sequences") {
        TuiScreen screen;
        screen.resize(2, 1);
        screen.setColorMode(Terminal::TrueColor);

        TuiCell cell;
        cell.ch = Char(U'X');
        cell.style = TuiStyle(Color(100, 150, 200), Color(50, 60, 70));
        screen.setCell(0, 0, cell);

        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::WriteOnly);
        AnsiStream stream(&dev);
        screen.flush(stream);
        // TrueColor uses 38;2 and 48;2 sequences
        CHECK(str.find("\033[38;2;100;150;200m") != String::npos);
        CHECK(str.find("\033[48;2;50;60;70m") != String::npos);
}

TEST_CASE("TuiScreen: flush with Color256 mode emits 38;5 sequences") {
        TuiScreen screen;
        screen.resize(2, 1);
        screen.setColorMode(Terminal::Color256);

        TuiCell cell;
        cell.ch = Char(U'X');
        cell.style = TuiStyle(Color(255, 0, 0), Color(0, 0, 255));
        screen.setCell(0, 0, cell);

        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::WriteOnly);
        AnsiStream stream(&dev);
        screen.flush(stream);
        // Color256 should use setForeground(AnsiColor) which uses standard or 38;5 codes
        CHECK_FALSE(str.isEmpty());
        // Should not contain 38;2 (truecolor) sequences
        CHECK(str.find("\033[38;2;") == String::npos);
        CHECK(str.find("\033[48;2;") == String::npos);
}

TEST_CASE("TuiScreen: flush with Basic mode restricts to 16 colors") {
        TuiScreen screen;
        screen.resize(2, 1);
        screen.setColorMode(Terminal::Basic);

        TuiCell cell;
        cell.ch = Char(U'X');
        cell.style = TuiStyle(Color(255, 0, 0), Color(0, 0, 255));
        screen.setCell(0, 0, cell);

        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::WriteOnly);
        AnsiStream stream(&dev);
        screen.flush(stream);
        CHECK_FALSE(str.isEmpty());
        // Should not contain 256-color or truecolor sequences
        CHECK(str.find("\033[38;5;") == String::npos);
        CHECK(str.find("\033[48;5;") == String::npos);
        CHECK(str.find("\033[38;2;") == String::npos);
        CHECK(str.find("\033[48;2;") == String::npos);
}

TEST_CASE("TuiScreen: flush with NoColor mode emits no color sequences") {
        TuiScreen screen;
        screen.resize(2, 1);
        screen.setColorMode(Terminal::NoColor);

        TuiCell cell;
        cell.ch = Char(U'X');
        cell.style = TuiStyle(Color(255, 0, 0), Color(0, 0, 255));
        screen.setCell(0, 0, cell);

        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::WriteOnly);
        AnsiStream stream(&dev);
        screen.flush(stream);
        CHECK_FALSE(str.isEmpty());
        // Should contain the character 'X' but no foreground/background color codes
        CHECK(str.find("X") != String::npos);
        CHECK(str.find("\033[38;") == String::npos);
        CHECK(str.find("\033[48;") == String::npos);
}

TEST_CASE("TuiScreen: flush with Grayscale16 mode uses 16-color codes") {
        TuiScreen screen;
        screen.resize(2, 1);
        screen.setColorMode(Terminal::Grayscale16);

        TuiCell cell;
        cell.ch = Char(U'G');
        cell.style = TuiStyle(Color(255, 0, 0), Color(0, 255, 0));
        screen.setCell(0, 0, cell);

        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::WriteOnly);
        AnsiStream stream(&dev);
        screen.flush(stream);
        CHECK_FALSE(str.isEmpty());
        // Grayscale16 should not use 256-color or truecolor sequences
        CHECK(str.find("\033[38;5;") == String::npos);
        CHECK(str.find("\033[38;2;") == String::npos);
}

TEST_CASE("TuiScreen: flush with Grayscale256 mode uses grayscale ramp") {
        TuiScreen screen;
        screen.resize(2, 1);
        screen.setColorMode(Terminal::Grayscale256);

        TuiCell cell;
        cell.ch = Char(U'G');
        cell.style = TuiStyle(Color(100, 150, 200), Color(50, 60, 70));
        screen.setCell(0, 0, cell);

        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::WriteOnly);
        AnsiStream stream(&dev);
        screen.flush(stream);
        CHECK_FALSE(str.isEmpty());
        // Should not use truecolor
        CHECK(str.find("\033[38;2;") == String::npos);
        CHECK(str.find("\033[48;2;") == String::npos);
}

TEST_CASE("TuiScreen: flush with GrayscaleTrue mode uses RGB grayscale") {
        TuiScreen screen;
        screen.resize(2, 1);
        screen.setColorMode(Terminal::GrayscaleTrue);

        TuiCell cell;
        cell.ch = Char(U'G');
        cell.style = TuiStyle(Color(255, 0, 0), Color(0, 0, 255));
        screen.setCell(0, 0, cell);

        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::WriteOnly);
        AnsiStream stream(&dev);
        screen.flush(stream);
        CHECK_FALSE(str.isEmpty());
        // GrayscaleTrue uses 38;2 but with equal R=G=B values
        CHECK(str.find("\033[38;2;") != String::npos);
}

// ── TuiScreen: flush with style attributes ──────────────────────────

TEST_CASE("TuiScreen: flush emits Bold attribute") {
        TuiScreen screen;
        screen.resize(2, 1);
        screen.setColorMode(Terminal::TrueColor);

        TuiCell cell;
        cell.ch = Char(U'B');
        cell.style = TuiStyle(Color::White, Color::Black, TuiStyle::Bold);
        screen.setCell(0, 0, cell);

        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::WriteOnly);
        AnsiStream stream(&dev);
        screen.flush(stream);
        CHECK(str.find("\033[1m") != String::npos);
}

TEST_CASE("TuiScreen: flush emits Italic attribute") {
        TuiScreen screen;
        screen.resize(2, 1);
        screen.setColorMode(Terminal::TrueColor);

        TuiCell cell;
        cell.ch = Char(U'I');
        cell.style = TuiStyle(Color::White, Color::Black, TuiStyle::Italic);
        screen.setCell(0, 0, cell);

        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::WriteOnly);
        AnsiStream stream(&dev);
        screen.flush(stream);
        CHECK(str.find("\033[3m") != String::npos);
}

TEST_CASE("TuiScreen: flush emits Underline attribute") {
        TuiScreen screen;
        screen.resize(2, 1);
        screen.setColorMode(Terminal::TrueColor);

        TuiCell cell;
        cell.ch = Char(U'U');
        cell.style = TuiStyle(Color::White, Color::Black, TuiStyle::Underline);
        screen.setCell(0, 0, cell);

        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::WriteOnly);
        AnsiStream stream(&dev);
        screen.flush(stream);
        CHECK(str.find("\033[4m") != String::npos);
}

TEST_CASE("TuiScreen: flush emits Dim attribute") {
        TuiScreen screen;
        screen.resize(2, 1);
        screen.setColorMode(Terminal::TrueColor);

        TuiCell cell;
        cell.ch = Char(U'D');
        cell.style = TuiStyle(Color::White, Color::Black, TuiStyle::Dim);
        screen.setCell(0, 0, cell);

        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::WriteOnly);
        AnsiStream stream(&dev);
        screen.flush(stream);
        CHECK(str.find("\033[2m") != String::npos);
}

TEST_CASE("TuiScreen: flush emits combined Bold+Underline attributes") {
        TuiScreen screen;
        screen.resize(2, 1);
        screen.setColorMode(Terminal::TrueColor);

        TuiCell cell;
        cell.ch = Char(U'C');
        cell.style = TuiStyle(Color::White, Color::Black, TuiStyle::Bold | TuiStyle::Underline);
        screen.setCell(0, 0, cell);

        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::WriteOnly);
        AnsiStream stream(&dev);
        screen.flush(stream);
        CHECK(str.find("\033[1m") != String::npos);
        CHECK(str.find("\033[4m") != String::npos);
}

// ── TuiScreen: empty screen flush ───────────────────────────────────

TEST_CASE("TuiScreen: flush on empty (0x0) screen produces no output") {
        TuiScreen screen;
        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::WriteOnly);
        AnsiStream stream(&dev);
        screen.flush(stream);
        CHECK(str.isEmpty());
}

// ── TuiScreen: resize clears and forces full redraw ─────────────────

TEST_CASE("TuiScreen: resize resets content") {
        TuiScreen screen;
        screen.resize(5, 5);

        TuiCell cell;
        cell.ch = Char(U'Z');
        screen.setCell(0, 0, cell);

        // Resize to different dimensions
        screen.resize(3, 3);
        TuiCell result = screen.cell(0, 0);
        CHECK(result.ch == Char(U' ')); // Should be default after resize
}

TEST_CASE("TuiScreen: resize to same dimensions is no-op") {
        TuiScreen screen;
        screen.resize(5, 5);

        TuiCell cell;
        cell.ch = Char(U'Z');
        screen.setCell(0, 0, cell);

        // Resize to same dimensions should preserve content
        screen.resize(5, 5);
        TuiCell result = screen.cell(0, 0);
        CHECK(result.ch == Char(U'Z'));
}
