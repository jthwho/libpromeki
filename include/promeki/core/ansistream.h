/**
 * @file      core/ansistream.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstring>
#include <promeki/core/namespace.h>
#include <promeki/core/string.h>
#include <promeki/core/util.h>

PROMEKI_NAMESPACE_BEGIN

class IODevice;

/**
 * @brief ANSI escape code writer backed by an IODevice.
 * Writes ANSI escape sequences and raw text to an IODevice.
 * @ingroup core_strings
 */
class AnsiStream {
        public:
                /**
                 * @brief ANSI text styles.
                 */
                enum TextStyle {
                        Bold = 1,       ///< Bold or increased intensity.
                        Dim = 2,        ///< Faint or decreased intensity.
                        Italic = 3,     ///< Italic text.
                        Underlined = 4, ///< Underlined text.
                        Blink = 5,      ///< Blinking text.
                        Inverted = 7,   ///< Swapped foreground and background colors.
                        Hidden = 8      ///< Hidden (invisible) text.
                };

                /**
                 * @brief ANSI colors.
                 */
                enum Color {
                        Black = 30,   ///< Black.
                        Red = 31,     ///< Red.
                        Green = 32,   ///< Green.
                        Yellow = 33,  ///< Yellow.
                        Blue = 34,    ///< Blue.
                        Magenta = 35, ///< Magenta.
                        Cyan = 36,    ///< Cyan.
                        White = 37,   ///< White.
                        Default = 39  ///< Terminal default color.
                };

                /**
                 * @brief Returns the window size of the current STDOUT device.
                 * @param[out] rows Number of rows in window.
                 * @param[out] cols Number of columns in window.
                 * @return True if the window size was retrieved successfully.
                 */
                static bool stdoutWindowSize(int &rows, int &cols);

                /**
                 * @brief Returns true if the current STDOUT can support ANSI signaling.
                 * @return True if STDOUT supports ANSI escape sequences.
                 */
                static bool stdoutSupportsANSI();

                /**
                 * @brief Constructs an AnsiStream writing to the given device.
                 * @param device The IODevice to write to. Must be open for writing.
                 */
                AnsiStream(IODevice *device) : _device(device), _enabled(true) { }

                /**
                 * @brief Sets the ANSI output enabled.
                 * If not enabled, no ANSI codes will be output but non-ANSI
                 * content will pass-thru.
                 * @param val True to enable ANSI output, false to disable.
                 */
                void setAnsiEnabled(bool val) {
                        _enabled = val;
                        return;
                }

                /**
                 * @brief Returns the underlying IODevice.
                 * @return Pointer to the IODevice this stream writes to.
                 */
                IODevice *device() const { return _device; }

                /**
                 * @brief Writes raw text to the underlying device.
                 * @param text The text to write.
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &write(const String &text);

                /**
                 * @brief Writes a C string to the underlying device.
                 * @param text The null-terminated string to write.
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &write(const char *text);

                /**
                 * @brief Writes a single character to the underlying device.
                 * @param ch The character to write.
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &write(char ch);

                /**
                 * @brief Writes an integer to the underlying device.
                 * @param val The integer to write (formatted as decimal).
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &write(int val);

                /**
                 * @brief Flushes the underlying device.
                 */
                void flush();

                /** @brief Writes a String via operator<<. */
                AnsiStream &operator<<(const String &text) { return write(text); }
                /** @brief Writes a C string via operator<<. */
                AnsiStream &operator<<(const char *text) { return write(text); }
                /** @brief Writes a single character via operator<<. */
                AnsiStream &operator<<(char ch) { return write(ch); }
                /** @brief Writes an integer via operator<<. */
                AnsiStream &operator<<(int val) { return write(val); }

                /**
                 * @brief Sets the text foreground color.
                 * @param color The foreground color to set.
                 * @param style The text style to apply (default: Bold).
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &setForeground(Color color, TextStyle style = Bold) {
                        if(!_enabled) return *this;
                        *this << "\033[" << static_cast<int>(style) << ";" << static_cast<int>(color) << "m";
                        return *this;
                }

                /**
                 * @brief Sets the text background color.
                 * @param color The background color to set.
                 * @param style The text style to apply (default: Bold).
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &setBackground(Color color, TextStyle style = Bold) {
                        if(!_enabled) return *this;
                        *this << "\033[" << static_cast<int>(style) << ";" << (static_cast<int>(color) + 10) << "m";
                        return *this;
                }

                /**
                 * @brief Moves the cursor up N rows.
                 * @param[in] n Number of rows to move up.
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &cursorUp(int n) {
                        if(!_enabled) return *this;
                        *this << "\033[" << n << "A";
                        return *this;
                }

                /**
                 * @brief Moves the cursor down N rows.
                 * @param[in] n Number of rows to move down.
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &cursorDown(int n) {
                        if(!_enabled) return *this;
                        *this << "\033[" << n << "B";
                        return *this;
                }

                /**
                 * @brief Moves the cursor right N columns.
                 * @param[in] n Number of columns to move right.
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &cursorRight(int n) {
                        if(!_enabled) return *this;
                        *this << "\033[" << n << "C";
                        return *this;
                }

                /**
                 * @brief Moves the cursor left N columns.
                 * @param[in] n Number of columns to move left.
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &cursorLeft(int n) {
                        if(!_enabled) return *this;
                        *this << "\033[" << n << "D";
                        return *this;
                }

                /**
                 * @brief Sets the absolute cursor position.
                 * @param[in] r Row to move cursor.
                 * @param[in] c Column to move cursor.
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &setCursorPosition(int r, int c) {
                        if(!_enabled) return *this;
                        *this << "\033[" << r << ";" << c << "H";
                        return *this;
                }

                /**
                 * @brief Clears the screen.
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &clearScreen() {
                        if(!_enabled) return *this;
                        *this << "\033[2J";
                        return *this;
                }

                /**
                 * @brief Moves the cursor to the start of the current line.
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &moveToStartOfLine() {
                        if(!_enabled) return *this;
                        *this << "\033[0G";
                        return *this;
                }

                /**
                 * @brief Moves the cursor to the end of the current line.
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &moveToEndOfLine() {
                        if(!_enabled) return *this;
                        *this << "\033[999G";
                        return *this;
                }

                /**
                 * @brief Clears the current line.
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &clearLine() {
                        if(!_enabled) return *this;
                        *this << "\033[2K";
                        return *this;
                }

                /**
                 * @brief Clears between the cursor and the start of the current line.
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &clearLineBeforeCursor() {
                        if(!_enabled) return *this;
                        *this << "\033[1K";
                        return *this;
                }

                /**
                 * @brief Clears between the cursor and the end of the current line.
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &clearLineAfterCursor() {
                        if(!_enabled) return *this;
                        *this << "\033[0K";
                        return *this;
                }

                /**
                 * @brief Resets the terminal to default configuration.
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &reset() {
                        if(!_enabled) return *this;
                        *this << "\033[0m";
                        return *this;
                }

                /**
                 * @brief Makes the cursor visible.
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &showCursor() {
                        if(!_enabled) return *this;
                        *this << "\033[?25h";
                        return *this;
                }

                /**
                 * @brief Makes the cursor invisible.
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &hideCursor() {
                        if(!_enabled) return *this;
                        *this << "\033[?25l";
                        return *this;
                }

                /**
                 * @brief Saves the cursor position for later recall.
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &saveCursorPosition() {
                        if(!_enabled) return *this;
                        *this << "\033[s";
                        return *this;
                }

                /**
                 * @brief Recalls a saved cursor position.
                 * @see saveCursorPosition()
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &restoreCursorPosition() {
                        if(!_enabled) return *this;
                        *this << "\033[u";
                        return *this;
                }

                /**
                 * @brief Enables a region of the screen to scroll.
                 * @param[in] startRow Row where scrolling should start.
                 * @param[in] endRow Row where scrolling should end.
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &enableScrollingRegion(int startRow, int endRow) {
                        if(!_enabled) return *this;
                        *this << "\033[" << startRow << ";" << endRow << "r";
                        return *this;
                }

                /**
                 * @brief Causes the scrolling region to scroll up N rows.
                 * @param[in] n Number of rows to scroll.
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &scrollUp(int n) {
                        if(!_enabled) return *this;
                        *this << "\033[" << n << "S";
                        return *this;
                }

                /**
                 * @brief Causes the scrolling region to scroll down N rows.
                 * @param[in] n Number of rows to scroll.
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &scrollDown(int n) {
                        if(!_enabled) return *this;
                        *this << "\033[" << n << "T";
                        return *this;
                }

                /**
                 * @brief Erases N characters at cursor.
                 * @param n Number of characters to erase.
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &eraseCharacters(int n) {
                        if(!_enabled) return *this;
                        *this << "\033[" << n << "X";
                        return *this;
                }

                /**
                 * @brief Sets the foreground to a 256-color palette index.
                 * @param index Color index (0-255).
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &setForeground256(uint8_t index) {
                        if(!_enabled) return *this;
                        *this << "\033[38;5;" << static_cast<int>(index) << "m";
                        return *this;
                }

                /**
                 * @brief Sets the background to a 256-color palette index.
                 * @param index Color index (0-255).
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &setBackground256(uint8_t index) {
                        if(!_enabled) return *this;
                        *this << "\033[48;5;" << static_cast<int>(index) << "m";
                        return *this;
                }

                /**
                 * @brief Sets the foreground to a 24-bit RGB color.
                 * @param r Red component (0-255).
                 * @param g Green component (0-255).
                 * @param b Blue component (0-255).
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &setForegroundRGB(uint8_t r, uint8_t g, uint8_t b) {
                        if(!_enabled) return *this;
                        *this << "\033[38;2;" << static_cast<int>(r) << ";"
                              << static_cast<int>(g) << ";" << static_cast<int>(b) << "m";
                        return *this;
                }

                /**
                 * @brief Sets the background to a 24-bit RGB color.
                 * @param r Red component (0-255).
                 * @param g Green component (0-255).
                 * @param b Blue component (0-255).
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &setBackgroundRGB(uint8_t r, uint8_t g, uint8_t b) {
                        if(!_enabled) return *this;
                        *this << "\033[48;2;" << static_cast<int>(r) << ";"
                              << static_cast<int>(g) << ";" << static_cast<int>(b) << "m";
                        return *this;
                }

                /**
                 * @brief Enables strike-through mode if supported.
                 * @param[in] enable True if strike-through should be enabled.
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &setStrikethrough(bool enable) {
                        if(!_enabled) return *this;
                        *this << "\033[" << (enable ? "9" : "29") << "m";
                        return *this;
                }

                /**
                 * @brief Terminal should switch to an alternate buffer.
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream& useAlternateScreenBuffer() {
                        if(!_enabled) return *this;
                        *this << "\033[?1049h";
                        return *this;
                }

                /**
                 * @brief Terminal should switch to main screen buffer.
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream& useMainScreenBuffer() {
                        if(!_enabled) return *this;
                        *this << "\033[?1049l";
                        return *this;
                }

                /**
                 * @brief Requests the current cursor position from the terminal.
                 * @param[in] input Input IODevice for the terminal (e.g. stdinDevice())
                 * @param[out] row Cursor Row
                 * @param[out] col Cursor Column
                 * @return True if successful.
                 */
                bool getCursorPosition(IODevice *input, int &row, int &col);

        private:
                IODevice *_device;
                bool      _enabled;
};


PROMEKI_NAMESPACE_END


