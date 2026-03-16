/**
 * @file      ansistream.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/core/ansistream.h>
#include <promeki/core/stringiodevice.h>
#include <promeki/core/terminal.h>

using namespace promeki;

TEST_CASE("AnsiStream: construction from IODevice") {
        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::WriteOnly);
        AnsiStream as(&dev);
        as << "hello";
        CHECK(str == "hello");
}

TEST_CASE("AnsiStream: setAnsiEnabled controls output") {
        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::WriteOnly);
        AnsiStream as(&dev);
        as.setAnsiEnabled(false);
        as.setForeground(AnsiStream::Red);
        // With ANSI disabled, no escape codes should be emitted
        CHECK(str.isEmpty());
}

TEST_CASE("AnsiStream: setForeground(AnsiColor) emits escape code") {
        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::WriteOnly);
        AnsiStream as(&dev);
        as.setForeground(AnsiStream::Red);
        CHECK_FALSE(str.isEmpty());
        // Red = 9 (bright), should emit \033[91m
        CHECK(str.find("\033[91m") != String::npos);
}

TEST_CASE("AnsiStream: setBackground(AnsiColor) emits escape code") {
        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::WriteOnly);
        AnsiStream as(&dev);
        as.setBackground(AnsiStream::Blue);
        CHECK_FALSE(str.isEmpty());
        // Blue = 12 (bright), should emit \033[104m
        CHECK(str.find("\033[104m") != String::npos);
}

TEST_CASE("AnsiStream: setForeground dark colors use 30-37 codes") {
        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::WriteOnly);
        AnsiStream as(&dev);
        as.setForeground(AnsiStream::DarkRed);
        CHECK(str.find("\033[31m") != String::npos);
}

TEST_CASE("AnsiStream: setBackground dark colors use 40-47 codes") {
        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::WriteOnly);
        AnsiStream as(&dev);
        as.setBackground(AnsiStream::DarkBlue);
        CHECK(str.find("\033[44m") != String::npos);
}

TEST_CASE("AnsiStream: setForeground extended colors use 38;5 codes") {
        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::WriteOnly);
        AnsiStream as(&dev);
        as.setForeground(static_cast<AnsiStream::AnsiColor>(42));
        CHECK(str.find("\033[38;5;42m") != String::npos);
}

TEST_CASE("AnsiStream: setBackground extended colors use 48;5 codes") {
        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::WriteOnly);
        AnsiStream as(&dev);
        as.setBackground(static_cast<AnsiStream::AnsiColor>(200));
        CHECK(str.find("\033[48;5;200m") != String::npos);
}

TEST_CASE("AnsiStream: reset emits escape code") {
        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::WriteOnly);
        AnsiStream as(&dev);
        as.reset();
        CHECK_FALSE(str.isEmpty());
}

TEST_CASE("AnsiStream: resetForeground emits 39m") {
        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::WriteOnly);
        AnsiStream as(&dev);
        as.resetForeground();
        CHECK(str.find("\033[39m") != String::npos);
}

TEST_CASE("AnsiStream: resetBackground emits 49m") {
        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::WriteOnly);
        AnsiStream as(&dev);
        as.resetBackground();
        CHECK(str.find("\033[49m") != String::npos);
}

TEST_CASE("AnsiStream: cursor movement") {
        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::WriteOnly);
        AnsiStream as(&dev);
        as.cursorUp(3);
        CHECK(str.find("3") != String::npos);
}

TEST_CASE("AnsiStream: clearScreen") {
        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::WriteOnly);
        AnsiStream as(&dev);
        as.clearScreen();
        CHECK_FALSE(str.isEmpty());
}

TEST_CASE("AnsiStream: chaining works") {
        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::WriteOnly);
        AnsiStream as(&dev);
        as.setForeground(AnsiStream::Green).reset();
        CHECK_FALSE(str.isEmpty());
}

TEST_CASE("AnsiStream: device accessor") {
        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::WriteOnly);
        AnsiStream as(&dev);
        CHECK(as.device() == &dev);
}

TEST_CASE("AnsiStream: write char") {
        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::WriteOnly);
        AnsiStream as(&dev);
        as.write('A');
        as.write('B');
        CHECK(str == "AB");
}

TEST_CASE("AnsiStream: write int") {
        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::WriteOnly);
        AnsiStream as(&dev);
        as.write(42);
        CHECK(str == "42");
}

TEST_CASE("AnsiStream: write C string") {
        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::WriteOnly);
        AnsiStream as(&dev);
        as.write("test");
        CHECK(str == "test");
}

TEST_CASE("AnsiStream: write String") {
        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::WriteOnly);
        AnsiStream as(&dev);
        as.write(String("hello"));
        CHECK(str == "hello");
}

TEST_CASE("AnsiStream: flush does not crash") {
        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::WriteOnly);
        AnsiStream as(&dev);
        as << "data";
        as.flush();
        CHECK(str == "data");
}

TEST_CASE("AnsiStream: operator<< char") {
        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::WriteOnly);
        AnsiStream as(&dev);
        as << 'X';
        CHECK(str == "X");
}

TEST_CASE("AnsiStream: operator<< int") {
        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::WriteOnly);
        AnsiStream as(&dev);
        as << 99;
        CHECK(str == "99");
}

TEST_CASE("AnsiStream: stdoutSupportsANSI returns bool") {
        bool result = AnsiStream::stdoutSupportsANSI();
        (void)result;
        CHECK(true);
}

TEST_CASE("AnsiStream: setForeground256 emits 38;5 sequence") {
        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::WriteOnly);
        AnsiStream as(&dev);
        as.setForeground256(42);
        CHECK(str.find("\033[38;5;42m") != String::npos);
}

TEST_CASE("AnsiStream: setBackground256 emits 48;5 sequence") {
        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::WriteOnly);
        AnsiStream as(&dev);
        as.setBackground256(42);
        CHECK(str.find("\033[48;5;42m") != String::npos);
}

TEST_CASE("AnsiStream: setForegroundRGB emits 38;2 sequence") {
        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::WriteOnly);
        AnsiStream as(&dev);
        as.setForegroundRGB(100, 150, 200);
        CHECK(str.find("\033[38;2;100;150;200m") != String::npos);
}

TEST_CASE("AnsiStream: setBackgroundRGB emits 48;2 sequence") {
        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::WriteOnly);
        AnsiStream as(&dev);
        as.setBackgroundRGB(100, 150, 200);
        CHECK(str.find("\033[48;2;100;150;200m") != String::npos);
}

TEST_CASE("AnsiStream: Terminal::colorSupport returns valid enum") {
        Terminal::ColorSupport cs = Terminal::colorSupport();
        CHECK(cs >= Terminal::NoColor);
        CHECK(cs <= Terminal::TrueColor);
}

// ── AnsiStream palette tests ────────────────────────────────────────

TEST_CASE("AnsiStream: ansiColor returns valid colors for indices 0-255") {
        for(int i = 0; i < 256; ++i) {
                Color c = AnsiStream::ansiColor(i);
                CHECK(c.isValid());
        }
}

TEST_CASE("AnsiStream: ansiColor out-of-range returns invalid") {
        CHECK_FALSE(AnsiStream::ansiColor(-1).isValid());
        CHECK_FALSE(AnsiStream::ansiColor(256).isValid());
}

TEST_CASE("AnsiStream: ansiColor(AnsiColor) overload works") {
        Color c = AnsiStream::ansiColor(AnsiStream::Red);
        CHECK(c.r() == 255);
        CHECK(c.g() == 0);
        CHECK(c.b() == 0);
}

TEST_CASE("AnsiStream: ansiColor system color values") {
        CHECK(AnsiStream::ansiColor(AnsiStream::Black) == Color(0, 0, 0));
        CHECK(AnsiStream::ansiColor(AnsiStream::DarkRed) == Color(128, 0, 0));
        CHECK(AnsiStream::ansiColor(AnsiStream::DarkGreen) == Color(0, 128, 0));
        CHECK(AnsiStream::ansiColor(AnsiStream::White) == Color(255, 255, 255));
        CHECK(AnsiStream::ansiColor(AnsiStream::DarkGray) == Color(128, 128, 128));
        CHECK(AnsiStream::ansiColor(AnsiStream::LightGray) == Color(192, 192, 192));
}

TEST_CASE("AnsiStream: findClosestAnsiColor pure red → Red (9)") {
        auto c = AnsiStream::findClosestAnsiColor(Color(255, 0, 0));
        CHECK(c == AnsiStream::Red);
}

TEST_CASE("AnsiStream: findClosestAnsiColor pure white → White (15)") {
        auto c = AnsiStream::findClosestAnsiColor(Color(255, 255, 255));
        CHECK(c == AnsiStream::White);
}

TEST_CASE("AnsiStream: findClosestAnsiColor pure black → Black (0)") {
        auto c = AnsiStream::findClosestAnsiColor(Color(0, 0, 0));
        CHECK(c == AnsiStream::Black);
}

TEST_CASE("AnsiStream: findClosestAnsiColor with maxIndex 15 restricts to system colors") {
        auto c = AnsiStream::findClosestAnsiColor(Color(95, 135, 175), 15);
        // Should pick one of the 16 system colors
        CHECK(static_cast<uint8_t>(c) <= 15);
}

TEST_CASE("AnsiStream: findClosestAnsiColor exact cube vertex round-trips") {
        // Index 42 = (0, 215, 135)
        Color c = AnsiStream::ansiColor(42);
        auto idx = AnsiStream::findClosestAnsiColor(c);
        CHECK(static_cast<uint8_t>(idx) == 42);
}

TEST_CASE("AnsiStream: setForeground(Color) emits correct sequence") {
        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::WriteOnly);
        AnsiStream as(&dev);
        // Pure red should match index 9 (bright red) → \033[91m
        as.setForeground(Color(255, 0, 0));
        CHECK(str.find("\033[91m") != String::npos);
}

TEST_CASE("AnsiStream: setBackground(Color) emits correct sequence") {
        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::WriteOnly);
        AnsiStream as(&dev);
        // Pure blue should match index 12 (bright blue) → \033[104m
        as.setBackground(Color(0, 0, 255));
        CHECK(str.find("\033[104m") != String::npos);
}

TEST_CASE("AnsiStream: setForeground(Color, 15) restricts to 16-color codes") {
        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::WriteOnly);
        AnsiStream as(&dev);
        as.setForeground(Color(95, 135, 175), 15);
        // Should emit a standard or bright foreground code, not a 256-color sequence
        CHECK(str.find("\033[38;5;") == String::npos);
        CHECK(str.find("\033[") != String::npos);
}

TEST_CASE("AnsiStream: all 16 AnsiColor foreground codes are correct") {
        for(int i = 0; i < 16; ++i) {
                String str;
                StringIODevice dev(&str);
                dev.open(IODevice::WriteOnly);
                AnsiStream as(&dev);
                as.setForeground(static_cast<AnsiStream::AnsiColor>(i));
                int expected = (i < 8) ? (30 + i) : (90 + i - 8);
                String expectedSeq = String("\033[") + String::number(expected) + "m";
                INFO("index=", i, " expected code=", expected);
                CHECK(str.find(expectedSeq) != String::npos);
        }
}

TEST_CASE("AnsiStream: all 16 AnsiColor background codes are correct") {
        for(int i = 0; i < 16; ++i) {
                String str;
                StringIODevice dev(&str);
                dev.open(IODevice::WriteOnly);
                AnsiStream as(&dev);
                as.setBackground(static_cast<AnsiStream::AnsiColor>(i));
                int expected = (i < 8) ? (40 + i) : (100 + i - 8);
                String expectedSeq = String("\033[") + String::number(expected) + "m";
                INFO("index=", i, " expected code=", expected);
                CHECK(str.find(expectedSeq) != String::npos);
        }
}

// ── Cursor movement tests ───────────────────────────────────────────

TEST_CASE("AnsiStream: cursorDown emits B sequence") {
        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::WriteOnly);
        AnsiStream as(&dev);
        as.cursorDown(5);
        CHECK(str.find("\033[5B") != String::npos);
}

TEST_CASE("AnsiStream: cursorRight emits C sequence") {
        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::WriteOnly);
        AnsiStream as(&dev);
        as.cursorRight(7);
        CHECK(str.find("\033[7C") != String::npos);
}

TEST_CASE("AnsiStream: cursorLeft emits D sequence") {
        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::WriteOnly);
        AnsiStream as(&dev);
        as.cursorLeft(2);
        CHECK(str.find("\033[2D") != String::npos);
}

TEST_CASE("AnsiStream: setCursorPosition emits H sequence") {
        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::WriteOnly);
        AnsiStream as(&dev);
        as.setCursorPosition(10, 20);
        CHECK(str.find("\033[10;20H") != String::npos);
}

// ── Line clearing tests ─────────────────────────────────────────────

TEST_CASE("AnsiStream: moveToStartOfLine emits 0G") {
        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::WriteOnly);
        AnsiStream as(&dev);
        as.moveToStartOfLine();
        CHECK(str.find("\033[0G") != String::npos);
}

TEST_CASE("AnsiStream: moveToEndOfLine emits 999G") {
        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::WriteOnly);
        AnsiStream as(&dev);
        as.moveToEndOfLine();
        CHECK(str.find("\033[999G") != String::npos);
}

TEST_CASE("AnsiStream: clearLine emits 2K") {
        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::WriteOnly);
        AnsiStream as(&dev);
        as.clearLine();
        CHECK(str.find("\033[2K") != String::npos);
}

TEST_CASE("AnsiStream: clearLineBeforeCursor emits 1K") {
        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::WriteOnly);
        AnsiStream as(&dev);
        as.clearLineBeforeCursor();
        CHECK(str.find("\033[1K") != String::npos);
}

TEST_CASE("AnsiStream: clearLineAfterCursor emits 0K") {
        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::WriteOnly);
        AnsiStream as(&dev);
        as.clearLineAfterCursor();
        CHECK(str.find("\033[0K") != String::npos);
}

// ── Cursor visibility tests ─────────────────────────────────────────

TEST_CASE("AnsiStream: showCursor emits ?25h") {
        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::WriteOnly);
        AnsiStream as(&dev);
        as.showCursor();
        CHECK(str.find("\033[?25h") != String::npos);
}

TEST_CASE("AnsiStream: hideCursor emits ?25l") {
        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::WriteOnly);
        AnsiStream as(&dev);
        as.hideCursor();
        CHECK(str.find("\033[?25l") != String::npos);
}

// ── Cursor save/restore tests ───────────────────────────────────────

TEST_CASE("AnsiStream: saveCursorPosition emits s") {
        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::WriteOnly);
        AnsiStream as(&dev);
        as.saveCursorPosition();
        CHECK(str.find("\033[s") != String::npos);
}

TEST_CASE("AnsiStream: restoreCursorPosition emits u") {
        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::WriteOnly);
        AnsiStream as(&dev);
        as.restoreCursorPosition();
        CHECK(str.find("\033[u") != String::npos);
}

// ── Scrolling tests ─────────────────────────────────────────────────

TEST_CASE("AnsiStream: enableScrollingRegion emits r sequence") {
        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::WriteOnly);
        AnsiStream as(&dev);
        as.enableScrollingRegion(5, 20);
        CHECK(str.find("\033[5;20r") != String::npos);
}

TEST_CASE("AnsiStream: scrollUp emits S sequence") {
        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::WriteOnly);
        AnsiStream as(&dev);
        as.scrollUp(3);
        CHECK(str.find("\033[3S") != String::npos);
}

TEST_CASE("AnsiStream: scrollDown emits T sequence") {
        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::WriteOnly);
        AnsiStream as(&dev);
        as.scrollDown(4);
        CHECK(str.find("\033[4T") != String::npos);
}

TEST_CASE("AnsiStream: eraseCharacters emits X sequence") {
        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::WriteOnly);
        AnsiStream as(&dev);
        as.eraseCharacters(6);
        CHECK(str.find("\033[6X") != String::npos);
}

// ── Strikethrough test ──────────────────────────────────────────────

TEST_CASE("AnsiStream: setStrikethrough enable emits 9m") {
        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::WriteOnly);
        AnsiStream as(&dev);
        as.setStrikethrough(true);
        CHECK(str.find("\033[9m") != String::npos);
}

TEST_CASE("AnsiStream: setStrikethrough disable emits 29m") {
        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::WriteOnly);
        AnsiStream as(&dev);
        as.setStrikethrough(false);
        CHECK(str.find("\033[29m") != String::npos);
}

// ── Alternate screen buffer tests ───────────────────────────────────

TEST_CASE("AnsiStream: useAlternateScreenBuffer emits ?1049h") {
        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::WriteOnly);
        AnsiStream as(&dev);
        as.useAlternateScreenBuffer();
        CHECK(str.find("\033[?1049h") != String::npos);
}

TEST_CASE("AnsiStream: useMainScreenBuffer emits ?1049l") {
        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::WriteOnly);
        AnsiStream as(&dev);
        as.useMainScreenBuffer();
        CHECK(str.find("\033[?1049l") != String::npos);
}

// ── ANSI-disabled passthrough for all commands ──────────────────────

TEST_CASE("AnsiStream: cursor commands suppressed when disabled") {
        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::WriteOnly);
        AnsiStream as(&dev);
        as.setAnsiEnabled(false);
        as.cursorUp(1);
        as.cursorDown(1);
        as.cursorRight(1);
        as.cursorLeft(1);
        as.setCursorPosition(1, 1);
        as.moveToStartOfLine();
        as.moveToEndOfLine();
        as.clearScreen();
        as.clearLine();
        as.clearLineBeforeCursor();
        as.clearLineAfterCursor();
        as.reset();
        as.resetForeground();
        as.resetBackground();
        as.showCursor();
        as.hideCursor();
        as.saveCursorPosition();
        as.restoreCursorPosition();
        as.enableScrollingRegion(1, 10);
        as.scrollUp(1);
        as.scrollDown(1);
        as.eraseCharacters(1);
        as.setForeground256(42);
        as.setBackground256(42);
        as.setForegroundRGB(1, 2, 3);
        as.setBackgroundRGB(1, 2, 3);
        as.setStrikethrough(true);
        as.useAlternateScreenBuffer();
        as.useMainScreenBuffer();
        CHECK(str.isEmpty());
}

// ── getCursorPosition disabled test ─────────────────────────────────

TEST_CASE("AnsiStream: getCursorPosition returns false when disabled") {
        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::WriteOnly);
        AnsiStream as(&dev);
        as.setAnsiEnabled(false);
        int row = 0, col = 0;
        CHECK_FALSE(as.getCursorPosition(&dev, row, col));
}

// ── stdoutWindowSize test ───────────────────────────────────────────

TEST_CASE("AnsiStream: stdoutWindowSize does not crash") {
        int rows = 0, cols = 0;
        // May succeed on TTY or fail on pipe, but should not crash
        AnsiStream::stdoutWindowSize(rows, cols);
        CHECK(true);
}
