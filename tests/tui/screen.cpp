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
