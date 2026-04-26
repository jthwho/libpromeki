/**
 * @file      screen.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cmath>
#include <promeki/tui/screen.h>

PROMEKI_NAMESPACE_BEGIN

// Convert a Color to its perceptual grayscale value (Rec. 709).
static uint8_t colorToGray(const Color &color) {
        return static_cast<uint8_t>(std::round(255.0 * (0.2126 * color.r() + 0.7152 * color.g() + 0.0722 * color.b())));
}

// Map a grayscale value to one of the 4 gray levels in the 16-color palette.
// Only exact black (0) maps to Black; everything else maps to DarkGray, Silver, or White.
static AnsiStream::AnsiColor grayToAnsi16(uint8_t gray) {
        if (gray == 0) return AnsiStream::Black;
        if (gray <= 96) return AnsiStream::DarkGray;
        if (gray <= 192) return AnsiStream::Silver;
        return AnsiStream::White;
}

// Map a grayscale value to the closest entry in the 256-color grayscale ramp.
// Uses Black (0) and White (15) for extremes, and the 24-entry ramp (232-255)
// which covers gray levels 8, 18, 28, ..., 238 in steps of 10.
static AnsiStream::AnsiColor grayToAnsi256(uint8_t gray) {
        if (gray < 4) return AnsiStream::Black;
        if (gray > 246) return AnsiStream::White;
        int idx = static_cast<int>(std::round((gray - 8.0) / 10.0));
        if (idx < 0) idx = 0;
        if (idx > 23) idx = 23;
        return static_cast<AnsiStream::AnsiColor>(232 + idx);
}

TuiScreen::TuiScreen() = default;
TuiScreen::~TuiScreen() = default;

void TuiScreen::resize(int cols, int rows) {
        if (cols == _cols && rows == _rows) return;
        _cols = cols;
        _rows = rows;
        int     total = cols * rows;
        TuiCell blank;
        _front.clear();
        _back.clear();
        _front.resize(total, blank);
        _back.resize(total, blank);
        _fullRedraw = true;
}

void TuiScreen::setCell(int x, int y, const TuiCell &cell) {
        if (!inBounds(x, y)) return;
        _back[index(x, y)] = cell;
}

TuiCell TuiScreen::cell(int x, int y) const {
        if (!inBounds(x, y)) return TuiCell();
        return _back[index(x, y)];
}

void TuiScreen::clear(const Color &fg, const Color &bg) {
        TuiCell blank;
        blank.style = TuiStyle(fg, bg);
        for (int i = 0; i < _cols * _rows; ++i) {
                _back[i] = blank;
        }
}

void TuiScreen::invalidate() {
        _fullRedraw = true;
}

static void emitForeground(AnsiStream &stream, const Color &color, Terminal::ColorSupport mode) {
        switch (mode) {
                case Terminal::NoColor: break;
                case Terminal::Grayscale16: stream.setForeground(grayToAnsi16(colorToGray(color))); break;
                case Terminal::Grayscale256: stream.setForeground(grayToAnsi256(colorToGray(color))); break;
                case Terminal::GrayscaleTrue: {
                        uint8_t g = colorToGray(color);
                        stream.setForegroundRGB(g, g, g);
                        break;
                }
                case Terminal::TrueColor: stream.setForegroundRGB(color.r8(), color.g8(), color.b8()); break;
                case Terminal::Color256: stream.setForeground(AnsiStream::findClosestAnsiColor(color, 255)); break;
                case Terminal::Basic: stream.setForeground(AnsiStream::findClosestAnsiColor(color, 15)); break;
        }
}

static void emitBackground(AnsiStream &stream, const Color &color, Terminal::ColorSupport mode) {
        switch (mode) {
                case Terminal::NoColor: break;
                case Terminal::Grayscale16: stream.setBackground(grayToAnsi16(colorToGray(color))); break;
                case Terminal::Grayscale256: stream.setBackground(grayToAnsi256(colorToGray(color))); break;
                case Terminal::GrayscaleTrue: {
                        uint8_t g = colorToGray(color);
                        stream.setBackgroundRGB(g, g, g);
                        break;
                }
                case Terminal::TrueColor: stream.setBackgroundRGB(color.r8(), color.g8(), color.b8()); break;
                case Terminal::Color256: stream.setBackground(AnsiStream::findClosestAnsiColor(color, 255)); break;
                case Terminal::Basic: stream.setBackground(AnsiStream::findClosestAnsiColor(color, 15)); break;
        }
}

void TuiScreen::emitCell(AnsiStream &stream, const TuiCell &cell, int &cursorX, int &cursorY, int x, int y,
                         Color &lastFg, Color &lastBg, uint8_t &lastStyle) {
        // Move cursor if needed
        if (cursorX != x || cursorY != y) {
                stream.setCursorPosition(y + 1, x + 1); // ANSI is 1-based
                cursorX = x;
                cursorY = y;
        }

        uint8_t cellAttrs = cell.style.attrs();
        Color   cellFg = cell.style.foreground();
        Color   cellBg = cell.style.background();

        // Update style if changed
        if (cellAttrs != lastStyle) {
                stream.reset();
                lastFg = Color();
                lastBg = Color();
                lastStyle = TuiStyle::None;

                if (cellAttrs & TuiStyle::Bold) stream << "\033[1m";
                if (cellAttrs & TuiStyle::Dim) stream << "\033[2m";
                if (cellAttrs & TuiStyle::Italic) stream << "\033[3m";
                if (cellAttrs & TuiStyle::Underline) stream << "\033[4m";
                if (cellAttrs & TuiStyle::Blink) stream << "\033[5m";
                if (cellAttrs & TuiStyle::Inverse) stream << "\033[7m";
                if (cellAttrs & TuiStyle::Strikethrough) stream << "\033[9m";
                lastStyle = cellAttrs;
        }

        // Update foreground if changed
        if (cellFg != lastFg) {
                emitForeground(stream, cellFg, _colorMode);
                lastFg = cellFg;
        }

        // Update background if changed
        if (cellBg != lastBg) {
                emitBackground(stream, cellBg, _colorMode);
                lastBg = cellBg;
        }

        // Output character as UTF-8
        char   buf[4];
        size_t len = cell.ch.toUtf8(buf);
        for (size_t i = 0; i < len; ++i) {
                stream << buf[i];
        }

        cursorX++;
}

void TuiScreen::flush(AnsiStream &stream) {
        if (_cols == 0 || _rows == 0) return;

        int     cursorX = -1, cursorY = -1;
        Color   lastFg, lastBg;
        uint8_t lastStyle = TuiStyle::None;
        bool    emittedAny = false;

        for (int y = 0; y < _rows; ++y) {
                for (int x = 0; x < _cols; ++x) {
                        int idx = index(x, y);
                        if (_fullRedraw || _back[idx] != _front[idx]) {
                                emitCell(stream, _back[idx], cursorX, cursorY, x, y, lastFg, lastBg, lastStyle);
                                emittedAny = true;
                        }
                }
        }

        if (emittedAny) {
                stream.reset();
                stream.flush();
        }

        // Swap: copy back to front
        _front = _back;
        _fullRedraw = false;
}

PROMEKI_NAMESPACE_END
