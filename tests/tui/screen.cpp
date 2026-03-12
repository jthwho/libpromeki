/**
 * @file      screen.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/tui/screen.h>

using namespace promeki;

TEST_CASE("TuiScreen: resize and clear") {
        TuiScreen screen;
        screen.resize(80, 24);
        CHECK(screen.cols() == 80);
        CHECK(screen.rows() == 24);

        screen.clear(Color::White, Color::Black);
        TuiCell cell = screen.cell(0, 0);
        CHECK(cell.ch == U' ');
        CHECK(cell.fg == Color::White);
        CHECK(cell.bg == Color::Black);
}

TEST_CASE("TuiScreen: setCell and cell") {
        TuiScreen screen;
        screen.resize(10, 5);

        TuiCell cell;
        cell.ch = U'A';
        cell.fg = Color::Red;
        cell.bg = Color::Blue;
        cell.style = TuiStyleBold;

        screen.setCell(3, 2, cell);
        TuiCell result = screen.cell(3, 2);
        CHECK(result.ch == U'A');
        CHECK(result.fg == Color::Red);
        CHECK(result.bg == Color::Blue);
        CHECK(result.style == TuiStyleBold);
}

TEST_CASE("TuiScreen: out-of-bounds access") {
        TuiScreen screen;
        screen.resize(10, 5);

        TuiCell cell;
        cell.ch = U'X';

        // Should not crash
        screen.setCell(-1, 0, cell);
        screen.setCell(10, 0, cell);
        screen.setCell(0, -1, cell);
        screen.setCell(0, 5, cell);

        // Out-of-bounds cell should return default
        TuiCell result = screen.cell(-1, 0);
        CHECK(result.ch == U' ');
}

TEST_CASE("TuiScreen: flush to stream") {
        TuiScreen screen;
        screen.resize(5, 3);

        TuiCell cell;
        cell.ch = U'H';
        cell.fg = Color::White;
        cell.bg = Color::Black;
        screen.setCell(0, 0, cell);

        std::ostringstream oss;
        AnsiStream stream(oss);
        screen.flush(stream);

        // Should have produced some output
        CHECK(!oss.str().empty());
}
