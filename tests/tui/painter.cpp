/**
 * @file      painter.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/tui/painter.h>
#include <promeki/tui/screen.h>

using namespace promeki;

TEST_CASE("TuiPainter: drawChar") {
        TuiScreen screen;
        screen.resize(20, 10);

        Rect2Di32 clip(0, 0, 20, 10);
        TuiPainter painter(screen, clip);

        painter.setForeground(Color::Red);
        painter.drawChar(5, 3, U'X');

        TuiCell cell = screen.cell(5, 3);
        CHECK(cell.ch == U'X');
        CHECK(cell.fg == Color::Red);
}

TEST_CASE("TuiPainter: drawText") {
        TuiScreen screen;
        screen.resize(20, 10);

        Rect2Di32 clip(0, 0, 20, 10);
        TuiPainter painter(screen, clip);

        painter.drawText(0, 0, "Hello");

        CHECK(screen.cell(0, 0).ch == U'H');
        CHECK(screen.cell(1, 0).ch == U'e');
        CHECK(screen.cell(2, 0).ch == U'l');
        CHECK(screen.cell(3, 0).ch == U'l');
        CHECK(screen.cell(4, 0).ch == U'o');
}

TEST_CASE("TuiPainter: fillRect") {
        TuiScreen screen;
        screen.resize(20, 10);

        Rect2Di32 clip(0, 0, 20, 10);
        TuiPainter painter(screen, clip);

        painter.setForeground(Color::Green);
        painter.setBackground(Color::Blue);
        painter.fillRect(Rect2Di32(2, 2, 3, 3), U'#');

        CHECK(screen.cell(2, 2).ch == U'#');
        CHECK(screen.cell(4, 4).ch == U'#');
        CHECK(screen.cell(2, 2).fg == Color::Green);
        CHECK(screen.cell(2, 2).bg == Color::Blue);
}

TEST_CASE("TuiPainter: drawRect with box chars") {
        TuiScreen screen;
        screen.resize(20, 10);

        Rect2Di32 clip(0, 0, 20, 10);
        TuiPainter painter(screen, clip);

        painter.drawRect(Rect2Di32(0, 0, 5, 3));

        CHECK(screen.cell(0, 0).ch == U'\u250C');  // top-left
        CHECK(screen.cell(4, 0).ch == U'\u2510');  // top-right
        CHECK(screen.cell(0, 2).ch == U'\u2514');  // bottom-left
        CHECK(screen.cell(4, 2).ch == U'\u2518');  // bottom-right
        CHECK(screen.cell(1, 0).ch == U'\u2500');  // top horizontal
        CHECK(screen.cell(0, 1).ch == U'\u2502');  // left vertical
}

TEST_CASE("TuiPainter: clipping") {
        TuiScreen screen;
        screen.resize(20, 10);

        // Clip to 5x5 at position (2,2)
        Rect2Di32 clip(2, 2, 5, 5);
        TuiPainter painter(screen, clip);

        // Drawing at (0,0) in painter coords = screen (2,2)
        painter.drawChar(0, 0, U'A');
        CHECK(screen.cell(2, 2).ch == U'A');

        // Drawing outside clip should be ignored
        painter.drawChar(10, 10, U'B');
        CHECK(screen.cell(12, 12).ch == U' ');  // unchanged
}

TEST_CASE("TuiPainter: drawHLine and drawVLine") {
        TuiScreen screen;
        screen.resize(20, 10);

        Rect2Di32 clip(0, 0, 20, 10);
        TuiPainter painter(screen, clip);

        painter.drawHLine(0, 0, 5);
        for(int i = 0; i < 5; ++i) {
                CHECK(screen.cell(i, 0).ch == U'\u2500');
        }

        painter.drawVLine(0, 1, 3);
        for(int i = 1; i <= 3; ++i) {
                CHECK(screen.cell(0, i).ch == U'\u2502');
        }
}

TEST_CASE("TuiPainter: style flags") {
        TuiScreen screen;
        screen.resize(20, 10);

        Rect2Di32 clip(0, 0, 20, 10);
        TuiPainter painter(screen, clip);

        painter.setStyle(TuiStyleBold | TuiStyleUnderline);
        painter.drawChar(0, 0, U'S');

        TuiCell cell = screen.cell(0, 0);
        CHECK(cell.style == (TuiStyleBold | TuiStyleUnderline));
}
