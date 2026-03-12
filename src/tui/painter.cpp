/**
 * @file      painter.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/tui/painter.h>

PROMEKI_NAMESPACE_BEGIN

TuiPainter::TuiPainter(TuiScreen &screen, const Rect2Di32 &clipRect)
        : _screen(screen), _clipRect(clipRect) {
}

void TuiPainter::putCell(int x, int y, char32_t ch) {
        // x, y are relative to clipRect origin
        int screenX = _clipRect.x() + x;
        int screenY = _clipRect.y() + y;
        if(!_clipRect.contains(Point2Di32(screenX, screenY))) return;
        TuiCell cell;
        cell.ch = ch;
        cell.fg = _fg;
        cell.bg = _bg;
        cell.style = _style;
        _screen.setCell(screenX, screenY, cell);
}

void TuiPainter::drawChar(int x, int y, char32_t ch) {
        putCell(x, y, ch);
}

void TuiPainter::drawText(int x, int y, const String &text) {
        const std::string &s = text.stds();
        int col = x;
        size_t i = 0;
        while(i < s.size()) {
                // Decode UTF-8
                char32_t ch;
                uint8_t c = s[i];
                if(c < 0x80) {
                        ch = c;
                        i += 1;
                } else if((c & 0xE0) == 0xC0) {
                        ch = (c & 0x1F) << 6;
                        if(i + 1 < s.size()) ch |= (s[i + 1] & 0x3F);
                        i += 2;
                } else if((c & 0xF0) == 0xE0) {
                        ch = (c & 0x0F) << 12;
                        if(i + 1 < s.size()) ch |= (s[i + 1] & 0x3F) << 6;
                        if(i + 2 < s.size()) ch |= (s[i + 2] & 0x3F);
                        i += 3;
                } else if((c & 0xF8) == 0xF0) {
                        ch = (c & 0x07) << 18;
                        if(i + 1 < s.size()) ch |= (s[i + 1] & 0x3F) << 12;
                        if(i + 2 < s.size()) ch |= (s[i + 2] & 0x3F) << 6;
                        if(i + 3 < s.size()) ch |= (s[i + 3] & 0x3F);
                        i += 4;
                } else {
                        ch = U'?';
                        i += 1;
                }
                putCell(col, y, ch);
                col++;
        }
}

void TuiPainter::drawRect(const Rect2Di32 &rect, char32_t ch) {
        if(rect.width() < 2 || rect.height() < 2) return;

        int x = rect.x();
        int y = rect.y();
        int w = rect.width();
        int h = rect.height();

        if(ch == 0) {
                // Unicode box-drawing characters
                putCell(x, y, U'\u250C');                       // top-left
                putCell(x + w - 1, y, U'\u2510');               // top-right
                putCell(x, y + h - 1, U'\u2514');               // bottom-left
                putCell(x + w - 1, y + h - 1, U'\u2518');       // bottom-right

                for(int i = 1; i < w - 1; ++i) {
                        putCell(x + i, y, U'\u2500');            // top
                        putCell(x + i, y + h - 1, U'\u2500');   // bottom
                }
                for(int i = 1; i < h - 1; ++i) {
                        putCell(x, y + i, U'\u2502');            // left
                        putCell(x + w - 1, y + i, U'\u2502');   // right
                }
        } else {
                for(int i = 0; i < w; ++i) {
                        putCell(x + i, y, ch);
                        putCell(x + i, y + h - 1, ch);
                }
                for(int i = 1; i < h - 1; ++i) {
                        putCell(x, y + i, ch);
                        putCell(x + w - 1, y + i, ch);
                }
        }
}

void TuiPainter::fillRect(const Rect2Di32 &rect, char32_t ch) {
        for(int ry = 0; ry < rect.height(); ++ry) {
                for(int rx = 0; rx < rect.width(); ++rx) {
                        putCell(rect.x() + rx, rect.y() + ry, ch);
                }
        }
}

void TuiPainter::drawHLine(int x, int y, int len, char32_t ch) {
        for(int i = 0; i < len; ++i) {
                putCell(x + i, y, ch);
        }
}

void TuiPainter::drawVLine(int x, int y, int len, char32_t ch) {
        for(int i = 0; i < len; ++i) {
                putCell(x, y + i, ch);
        }
}

PROMEKI_NAMESPACE_END
