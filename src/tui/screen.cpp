/**
 * @file      screen.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/tui/screen.h>

PROMEKI_NAMESPACE_BEGIN

TuiScreen::TuiScreen() = default;
TuiScreen::~TuiScreen() = default;

void TuiScreen::resize(int cols, int rows) {
        if(cols == _cols && rows == _rows) return;
        _cols = cols;
        _rows = rows;
        int total = cols * rows;
        TuiCell blank;
        _front.clear();
        _back.clear();
        _front.resize(total, blank);
        _back.resize(total, blank);
        _fullRedraw = true;
}

void TuiScreen::setCell(int x, int y, const TuiCell &cell) {
        if(!inBounds(x, y)) return;
        _back[index(x, y)] = cell;
}

TuiCell TuiScreen::cell(int x, int y) const {
        if(!inBounds(x, y)) return TuiCell();
        return _back[index(x, y)];
}

void TuiScreen::clear(const Color &fg, const Color &bg) {
        TuiCell blank;
        blank.fg = fg;
        blank.bg = bg;
        for(int i = 0; i < _cols * _rows; ++i) {
                _back[i] = blank;
        }
}

void TuiScreen::invalidate() {
        _fullRedraw = true;
}

void TuiScreen::emitCell(AnsiStream &stream, const TuiCell &cell,
                         int &cursorX, int &cursorY, int x, int y,
                         Color &lastFg, Color &lastBg, uint8_t &lastStyle) {
        // Move cursor if needed
        if(cursorX != x || cursorY != y) {
                stream.setCursorPosition(y + 1, x + 1); // ANSI is 1-based
                cursorX = x;
                cursorY = y;
        }

        // Update style if changed
        if(cell.style != lastStyle) {
                stream.reset();
                lastFg = Color();
                lastBg = Color();
                lastStyle = TuiStyleNone;

                if(cell.style & TuiStyleBold) stream << "\033[1m";
                if(cell.style & TuiStyleDim) stream << "\033[2m";
                if(cell.style & TuiStyleItalic) stream << "\033[3m";
                if(cell.style & TuiStyleUnderline) stream << "\033[4m";
                if(cell.style & TuiStyleBlink) stream << "\033[5m";
                if(cell.style & TuiStyleInverse) stream << "\033[7m";
                if(cell.style & TuiStyleStrikethrough) stream << "\033[9m";
                lastStyle = cell.style;
        }

        // Update foreground if changed
        if(cell.fg != lastFg) {
                stream.setForegroundRGB(cell.fg.r(), cell.fg.g(), cell.fg.b());
                lastFg = cell.fg;
        }

        // Update background if changed
        if(cell.bg != lastBg) {
                stream.setBackgroundRGB(cell.bg.r(), cell.bg.g(), cell.bg.b());
                lastBg = cell.bg;
        }

        // Output character as UTF-8
        char32_t ch = cell.ch;
        if(ch < 0x80) {
                stream << static_cast<char>(ch);
        } else if(ch < 0x800) {
                stream << static_cast<char>(0xC0 | (ch >> 6));
                stream << static_cast<char>(0x80 | (ch & 0x3F));
        } else if(ch < 0x10000) {
                stream << static_cast<char>(0xE0 | (ch >> 12));
                stream << static_cast<char>(0x80 | ((ch >> 6) & 0x3F));
                stream << static_cast<char>(0x80 | (ch & 0x3F));
        } else {
                stream << static_cast<char>(0xF0 | (ch >> 18));
                stream << static_cast<char>(0x80 | ((ch >> 12) & 0x3F));
                stream << static_cast<char>(0x80 | ((ch >> 6) & 0x3F));
                stream << static_cast<char>(0x80 | (ch & 0x3F));
        }

        cursorX++;
}

void TuiScreen::flush(AnsiStream &stream) {
        if(_cols == 0 || _rows == 0) return;

        int cursorX = -1, cursorY = -1;
        Color lastFg, lastBg;
        uint8_t lastStyle = TuiStyleNone;
        bool emittedAny = false;

        for(int y = 0; y < _rows; ++y) {
                for(int x = 0; x < _cols; ++x) {
                        int idx = index(x, y);
                        if(_fullRedraw || _back[idx] != _front[idx]) {
                                emitCell(stream, _back[idx], cursorX, cursorY,
                                         x, y, lastFg, lastBg, lastStyle);
                                emittedAny = true;
                        }
                }
        }

        if(emittedAny) {
                stream.reset();
                stream << std::flush;
        }

        // Swap: copy back to front
        _front = _back;
        _fullRedraw = false;
}

PROMEKI_NAMESPACE_END
