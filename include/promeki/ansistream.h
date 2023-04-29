/*****************************************************************************
 * ansistream.h
 * April 28, 2023
 *
 * Copyright 2023 - Howard Logic
 * https://howardlogic.com
 * All Rights Reserved
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>
 *
 *****************************************************************************/

#pragma once

#include <iostream>
#include <ostream>
#include <cstring>
#include <promeki/string.h>
#include <promeki/util.h>

namespace promeki {

class AnsiStream : public std::ostream {
        public:
                // ANSI escape codes for text styles
                enum TextStyle {
                        Bold = 1,
                        Dim = 2,
                        Italic = 3,
                        Underlined = 4,
                        Blink = 5,
                        Inverted = 7,
                        Hidden = 8
                };

                // ANSI escape codes for colors
                enum Color {
                        Black = 30,
                        Red = 31,
                        Green = 32,
                        Yellow = 33,
                        Blue = 34,
                        Magenta = 35,
                        Cyan = 36,
                        White = 37,
                        Default = 39
                };

                static bool getWindowSize(int &rows, int &cols);
                static bool isAnsiSupported();

                AnsiStream(std::ostream &output) : 
                        std::ostream(output.rdbuf()),
                        _enabled(isAnsiSupported())
                { }

                AnsiStream &setForeground(Color color, TextStyle style = Bold) {
                        if(!_enabled) return *this;
                        *this << "\033[" << style << ";" << color << "m";
                        return *this;
                }

                AnsiStream &setBackground(Color color, TextStyle style = Bold) {
                        if(!_enabled) return *this;
                        *this << "\033[" << style << ";" << (color + 10) << "m";
                        return *this;
                }

                AnsiStream &cursorUp(int n) {
                        if(!_enabled) return *this;
                        *this << "\033[" << n << "A";
                        return *this;
                }

                AnsiStream &cursorDown(int n) {
                        if(!_enabled) return *this;
                        *this << "\033[" << n << "B";
                        return *this;
                }

                AnsiStream &cursorRight(int n) {
                        if(!_enabled) return *this;
                        *this << "\033[" << n << "C";
                        return *this;
                }

                AnsiStream &cursorLeft(int n) {
                        if(!_enabled) return *this;
                        *this << "\033[" << n << "D";
                        return *this;
                }

                AnsiStream &setCursorPosition(int r, int c) {
                        if(!_enabled) return *this;
                        *this << "\033[" << r << ";" << c << "H";
                        return *this;
                }

                AnsiStream &clearScreen() {
                        if(!_enabled) return *this;
                        *this << "\033[2J";
                        return *this;
                }

                AnsiStream &moveToStartOfLine() {
                        if(!_enabled) return *this;
                        *this << "\033[0G";
                        return *this;
                }

                AnsiStream &moveToEndOfLine() {
                        if(!_enabled) return *this;
                        *this << "\033[999G";
                        return *this;
                }

                AnsiStream &clearLine() {
                        if(!_enabled) return *this;
                        *this << "\033[2K";
                        return *this;
                }

                AnsiStream &clearLineBeforeCursor() {
                        if(!_enabled) return *this;
                        *this << "\033[1K";
                        return *this;
                }

                AnsiStream &clearLineAfterCursor() {
                        if(!_enabled) return *this;
                        *this << "\033[0K";
                        return *this;
                }

                AnsiStream &reset() {
                        if(!_enabled) return *this;
                        *this << "\033[0m";
                        return *this;
                }

                AnsiStream &showCursor() {
                        if(!_enabled) return *this;
                        *this << "\033[?25h";
                        return *this;
                }

                AnsiStream &hideCursor() {
                        if(!_enabled) return *this;
                        *this << "\033[?25l";
                        return *this;
                }

                AnsiStream &saveCursorPosition() {
                        if(!_enabled) return *this;
                        *this << "\033[s";
                        return *this;
                }

                AnsiStream &restoreCursorPosition() {
                        if(!_enabled) return *this;
                        *this << "\033[u";
                        return *this;
                }

                AnsiStream &enableScrollingRegion(int startRow, int endRow) {
                        if(!_enabled) return *this;
                        *this << "\033[" << startRow << ";" << endRow << "r";
                        return *this;
                }

                AnsiStream &scrollUp(int n) {
                        if(!_enabled) return *this;
                        *this << "\033[" << n << "S";
                        return *this;
                }

                AnsiStream &scrollDown(int n) {
                        if(!_enabled) return *this;
                        *this << "\033[" << n << "T";
                        return *this;
                }

                AnsiStream &eraseCharacters(int n) {
                        if(!_enabled) return *this;
                        *this << "\033[" << n << "X";
                        return *this;
                }

                AnsiStream &setStrikethrough(bool enable) {
                        if(!_enabled) return *this;
                        *this << "\033[" << (enable ? "9" : "29") << "m";
                        return *this;
                }

                AnsiStream& useAlternateScreenBuffer() {
                        if(!_enabled) return *this;
                        *this << "\033[?1049h";
                        return *this;
                }

                AnsiStream& useMainScreenBuffer() {
                        if(!_enabled) return *this;
                        *this << "\033[?1049l";
                        return *this;
                }

                bool getCursorPosition(std::istream &input, int &row, int &col);

        private:
                bool    _enabled;
};


} // namespace promeki
