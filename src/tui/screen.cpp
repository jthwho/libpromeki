/**
 * @file      screen.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/tui/screen.h>

PROMEKI_NAMESPACE_BEGIN

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

                if (cellAttrs & TuiStyle::Bold) stream.setStyle(AnsiStream::Bold);
                if (cellAttrs & TuiStyle::Dim) stream.setStyle(AnsiStream::Dim);
                if (cellAttrs & TuiStyle::Italic) stream.setStyle(AnsiStream::Italic);
                if (cellAttrs & TuiStyle::Underline) stream.setStyle(AnsiStream::Underlined);
                if (cellAttrs & TuiStyle::Blink) stream.setStyle(AnsiStream::Blink);
                if (cellAttrs & TuiStyle::Inverse) stream.setStyle(AnsiStream::Inverted);
                if (cellAttrs & TuiStyle::Strikethrough) stream.setStyle(AnsiStream::Strikethrough);
                lastStyle = cellAttrs;
        }

        // Update foreground if changed
        if (cellFg != lastFg) {
                stream.setForeground(cellFg, _colorMode);
                lastFg = cellFg;
        }

        // Update background if changed
        if (cellBg != lastBg) {
                stream.setBackground(cellBg, _colorMode);
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
                                // Open the synchronized-output span lazily on the
                                // first changed cell so an idle flush (nothing
                                // changed) emits nothing at all.  Terminals that
                                // support mode 2026 present everything between
                                // begin/end atomically (no tearing); others
                                // ignore the sequences.
                                if (!emittedAny) stream.beginSynchronizedUpdate();
                                emitCell(stream, _back[idx], cursorX, cursorY, x, y, lastFg, lastBg, lastStyle);
                                emittedAny = true;
                        }
                }
        }

        if (emittedAny) {
                stream.reset();
                stream.endSynchronizedUpdate();
                stream.flush();
        }

        // Swap: copy back to front
        _front = _back;
        _fullRedraw = false;
}

PROMEKI_NAMESPACE_END
