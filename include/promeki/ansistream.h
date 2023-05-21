/** 
 * @file ansistream.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the source root folder for license information.
 */

#pragma once

#include <iostream>
#include <ostream>
#include <cstring>
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/util.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Output stream for ANSI codes
 * You can use this class to make it easier to send 
 * ANSI escape codes to an output stream
 */
class AnsiStream : public std::ostream {
        public:
                /**
                 * @brief ANSI text styles.
                 */
                enum TextStyle {
                        Bold = 1,
                        Dim = 2,
                        Italic = 3,
                        Underlined = 4,
                        Blink = 5,
                        Inverted = 7,
                        Hidden = 8
                };

                /**
                 * @brief ANSI colors
                 */
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

                /**
                 * @brief Returns the window size of the current STDOUT device.
                 * @param[out] rows Number of rows in window
                 * @param[out] cols Number of columns in window
                 */
                static bool stdoutWindowSize(int &rows, int &cols);

                /**
                 * @brief Returns true if the current STDOUT can support ANSI signaling
                 */
                static bool stdoutSupportsANSI();

                /**
                 * @brief Default Constructor
                 * You should pass the output stream object you would like to begin
                 * sending ANSI codes to
                 */
                AnsiStream(std::ostream &output) : 
                        std::ostream(output.rdbuf()),
                        _enabled(true)
                { }

                /**
                 * @brief Sets the ANSI output enabled.
                 * If not enabled, no ANSI codes will be output but non ANSI 
                 * content will pass-thru
                 */
                void setAnsiEnabled(bool val) {
                        _enabled = val;
                        return;
                }

                /**
                 * @brief Sets the text foreground color
                 */
                AnsiStream &setForeground(Color color, TextStyle style = Bold) {
                        if(!_enabled) return *this;
                        *this << "\033[" << style << ";" << color << "m";
                        return *this;
                }

                /**
                 * @brief Sets the text background color
                 */
                AnsiStream &setBackground(Color color, TextStyle style = Bold) {
                        if(!_enabled) return *this;
                        *this << "\033[" << style << ";" << (color + 10) << "m";
                        return *this;
                }

                /**
                 * @brief Moves the cursor up N rows
                 * @param[in] n Number of rows to move up
                 */
                AnsiStream &cursorUp(int n) {
                        if(!_enabled) return *this;
                        *this << "\033[" << n << "A";
                        return *this;
                }

                /**
                 * @brief Moves the cursor down N rows
                 * @param[in] n Number of rows to move down
                 */
                AnsiStream &cursorDown(int n) {
                        if(!_enabled) return *this;
                        *this << "\033[" << n << "B";
                        return *this;
                }

                /**
                 * @brief Moves the cursor right N columns
                 * @param[in] n Number of columns to move right
                 */
                AnsiStream &cursorRight(int n) {
                        if(!_enabled) return *this;
                        *this << "\033[" << n << "C";
                        return *this;
                }

                /**
                 * @brief Moves the cursor left N columns
                 * @param[in] n Number of columns to move left
                 */
                AnsiStream &cursorLeft(int n) {
                        if(!_enabled) return *this;
                        *this << "\033[" << n << "D";
                        return *this;
                }

                /**
                 * @brief Sets the absolute cursor position
                 * @param[in] r Row to move cursor
                 * @param[in] c Column to move cursor
                 */
                AnsiStream &setCursorPosition(int r, int c) {
                        if(!_enabled) return *this;
                        *this << "\033[" << r << ";" << c << "H";
                        return *this;
                }

                /**
                 * @brief Clears the screen
                 */
                AnsiStream &clearScreen() {
                        if(!_enabled) return *this;
                        *this << "\033[2J";
                        return *this;
                }

                /**
                 * @brief Moves the cursor to the start of the current line
                 */
                AnsiStream &moveToStartOfLine() {
                        if(!_enabled) return *this;
                        *this << "\033[0G";
                        return *this;
                }

                /**
                 * @brief Moves the cursor to the end of the current line
                 */
                AnsiStream &moveToEndOfLine() {
                        if(!_enabled) return *this;
                        *this << "\033[999G";
                        return *this;
                }

                /**
                 * @brief Clears the current line
                 */
                AnsiStream &clearLine() {
                        if(!_enabled) return *this;
                        *this << "\033[2K";
                        return *this;
                }

                /**
                 * @brief Clears between the cursor and the start of the current line
                 */
                AnsiStream &clearLineBeforeCursor() {
                        if(!_enabled) return *this;
                        *this << "\033[1K";
                        return *this;
                }

                /**
                 * @brief Clears between the cursor and the end of the current line
                 */
                AnsiStream &clearLineAfterCursor() {
                        if(!_enabled) return *this;
                        *this << "\033[0K";
                        return *this;
                }

                /**
                 * @brief Resets the terminal to default configuration
                 */
                AnsiStream &reset() {
                        if(!_enabled) return *this;
                        *this << "\033[0m";
                        return *this;
                }

                /**
                 * @brief Makes the cursor visible
                 */
                AnsiStream &showCursor() {
                        if(!_enabled) return *this;
                        *this << "\033[?25h";
                        return *this;
                }

                /**
                 * @brief Makes the cursor invisible
                 */
                AnsiStream &hideCursor() {
                        if(!_enabled) return *this;
                        *this << "\033[?25l";
                        return *this;
                }

                /**
                 * @brief Saves the cursor position for later recall
                 */
                AnsiStream &saveCursorPosition() {
                        if(!_enabled) return *this;
                        *this << "\033[s";
                        return *this;
                }

                /**
                 * @brief Recalls a saved cursor position
                 * See: saveCursorPosition()
                 */
                AnsiStream &restoreCursorPosition() {
                        if(!_enabled) return *this;
                        *this << "\033[u";
                        return *this;
                }

                /**
                 * @brief Enables a region of the screen scroll
                 * @param[in] startRow Row where scrolling should start
                 * @param[in] endRow Row where scrolling should end
                 */
                AnsiStream &enableScrollingRegion(int startRow, int endRow) {
                        if(!_enabled) return *this;
                        *this << "\033[" << startRow << ";" << endRow << "r";
                        return *this;
                }

                /**
                 * @brief Causes the scrolling region to scroll up N rows
                 * @param[in] n Number of rows to scroll
                 */
                AnsiStream &scrollUp(int n) {
                        if(!_enabled) return *this;
                        *this << "\033[" << n << "S";
                        return *this;
                }

                /**
                 * @brief Causes the scrolling region to scroll down N rows
                 * @param[in] n Number of rows to scroll
                 */
                AnsiStream &scrollDown(int n) {
                        if(!_enabled) return *this;
                        *this << "\033[" << n << "T";
                        return *this;
                }

                /** @brief Erases N characters at cursor
                 *  @param n Number of characters to erase
                 */
                AnsiStream &eraseCharacters(int n) {
                        if(!_enabled) return *this;
                        *this << "\033[" << n << "X";
                        return *this;
                }

                /**
                 * @brief Enables strike-through mode if supported
                 * @param[in] enable True if strike-through should be enabled
                 */
                AnsiStream &setStrikethrough(bool enable) {
                        if(!_enabled) return *this;
                        *this << "\033[" << (enable ? "9" : "29") << "m";
                        return *this;
                }

                /**
                 * @brief Terminal should switch to an alternate buffer
                 * This can be useful for switching to an alternate screen buffer
                 * for your application while leaving the main buffer unaltered.
                 * This makes it easy to switch back to the main buffer on exiting
                 * your application 
                 */
                AnsiStream& useAlternateScreenBuffer() {
                        if(!_enabled) return *this;
                        *this << "\033[?1049h";
                        return *this;
                }

                /**
                 * @brief Terminal should switch to main screen buffer
                 */
                AnsiStream& useMainScreenBuffer() {
                        if(!_enabled) return *this;
                        *this << "\033[?1049l";
                        return *this;
                }

                /**
                 * @brief Requests the current cursor position from the terminal.
                 * @param[in] input Input stream from terminal
                 * @param[out] row Cursor Row
                 * @param[out] col Cursor Column
                 * @return True if successful.
                 *
                 * This sends a cursor position request code to the terminal and
                 * parses the response.  You must pass an input stream for the 
                 * terminal device for it to get the response.
                 */
                bool getCursorPosition(std::istream &input, int &row, int &col);

        private:
                bool    _enabled;
};


PROMEKI_NAMESPACE_END


