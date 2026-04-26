/**
 * @file      textarea.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/tui/textarea.h>
#include <promeki/tui/painter.h>
#include <promeki/tui/palette.h>
#include <promeki/tui/tuisubsystem.h>
#include <promeki/keyevent.h>

PROMEKI_NAMESPACE_BEGIN

TuiTextArea::TuiTextArea(ObjectBase *parent) : TuiWidget(parent) {
        setFocusPolicy(StrongFocus);
        _lines += String();
}

TuiTextArea::~TuiTextArea() = default;

void TuiTextArea::setText(const String &text) {
        _lines = text.split("\n");
        if (_lines.isEmpty()) _lines += String();
        _cursorRow = 0;
        _cursorCol = 0;
        _scrollRow = 0;
        textChangedSignal.emit();
        update();
}

String TuiTextArea::text() const {
        String result;
        for (size_t i = 0; i < _lines.size(); ++i) {
                if (i > 0) result += "\n";
                result += _lines[i];
        }
        return result;
}

void TuiTextArea::appendLine(const String &line) {
        _lines += line;
        // Auto-scroll to bottom
        if (static_cast<int>(_lines.size()) > height()) {
                _scrollRow = static_cast<int>(_lines.size()) - height();
        }
        update();
}

Size2Di32 TuiTextArea::sizeHint() const {
        return Size2Di32(40, 10);
}

void TuiTextArea::paintEvent(PaintEvent *) {
        TuiSubsystem *app = TuiSubsystem::instance();
        if (!app) return;

        Point2Di32 screenPos = mapToGlobal(Point2Di32(0, 0));
        Rect2Di32  clipRect(screenPos.x(), screenPos.y(), width(), height());
        TuiPainter painter(app->screen(), clipRect);

        const TuiPalette &pal = app->palette();
        TuiStyle          s = pal.style(TuiPalette::Text, hasFocus(), isEnabled())
                             .merged(pal.style(TuiPalette::Base, hasFocus(), isEnabled()));
        painter.setStyle(s);
        painter.fillRect(Rect2Di32(0, 0, width(), height()));

        for (int row = 0; row < height(); ++row) {
                int lineIdx = _scrollRow + row;
                if (lineIdx < 0 || static_cast<size_t>(lineIdx) >= _lines.size()) continue;
                const String &line = _lines[lineIdx];
                String        visible = line.substr(_scrollCol, std::min(static_cast<size_t>(width()), line.length()));
                painter.drawText(0, row, visible);
        }

        // Draw cursor if focused and not read-only
        if (hasFocus() && !_readOnly) {
                int screenRow = _cursorRow - _scrollRow;
                int screenCol = _cursorCol - _scrollCol;
                if (screenRow >= 0 && screenRow < height() && screenCol >= 0 && screenCol < width()) {
                        painter.setAttrs(TuiStyle::Inverse);
                        char32_t ch = U' ';
                        if (static_cast<size_t>(_cursorRow) < _lines.size() &&
                            static_cast<size_t>(_cursorCol) < _lines[_cursorRow].length()) {
                                ch = _lines[_cursorRow].charAt(_cursorCol).codepoint();
                        }
                        painter.drawChar(screenCol, screenRow, ch);
                        painter.setAttrs(TuiStyle::None);
                }
        }
}

void TuiTextArea::keyPressEvent(KeyEvent *e) {
        if (_readOnly) return;

        // Let Ctrl-modified keys propagate (e.g. Ctrl+Left/Right for tab switching)
        if (e->isCtrl()) return;

        switch (e->key()) {
                case KeyEvent::Key_Up:
                        if (_cursorRow > 0) {
                                _cursorRow--;
                                if (static_cast<size_t>(_cursorCol) > _lines[_cursorRow].length()) {
                                        _cursorCol = static_cast<int>(_lines[_cursorRow].length());
                                }
                        }
                        if (_cursorRow < _scrollRow) _scrollRow = _cursorRow;
                        update();
                        e->accept();
                        break;
                case KeyEvent::Key_Down:
                        if (static_cast<size_t>(_cursorRow) < _lines.size() - 1) {
                                _cursorRow++;
                                if (static_cast<size_t>(_cursorCol) > _lines[_cursorRow].length()) {
                                        _cursorCol = static_cast<int>(_lines[_cursorRow].length());
                                }
                        }
                        if (_cursorRow >= _scrollRow + height()) _scrollRow = _cursorRow - height() + 1;
                        update();
                        e->accept();
                        break;
                case KeyEvent::Key_Left:
                        if (_cursorCol > 0)
                                _cursorCol--;
                        else if (_cursorRow > 0) {
                                _cursorRow--;
                                _cursorCol = static_cast<int>(_lines[_cursorRow].length());
                        }
                        update();
                        e->accept();
                        break;
                case KeyEvent::Key_Right:
                        if (static_cast<size_t>(_cursorCol) < _lines[_cursorRow].length())
                                _cursorCol++;
                        else if (static_cast<size_t>(_cursorRow) < _lines.size() - 1) {
                                _cursorRow++;
                                _cursorCol = 0;
                        }
                        update();
                        e->accept();
                        break;
                case KeyEvent::Key_Enter: {
                        String &line = _lines[_cursorRow];
                        String  after = line.substr(_cursorCol);
                        _lines[_cursorRow] = line.substr(0, _cursorCol);
                        _lines.insert(_cursorRow + 1, after);
                        _cursorRow++;
                        _cursorCol = 0;
                        textChangedSignal.emit();
                        update();
                        e->accept();
                        break;
                }
                case KeyEvent::Key_Backspace:
                        if (_cursorCol > 0) {
                                _lines[_cursorRow].erase(_cursorCol - 1, 1);
                                _cursorCol--;
                                textChangedSignal.emit();
                        } else if (_cursorRow > 0) {
                                _cursorCol = static_cast<int>(_lines[_cursorRow - 1].length());
                                _lines[_cursorRow - 1] += _lines[_cursorRow];
                                _lines.remove(static_cast<size_t>(_cursorRow));
                                _cursorRow--;
                                textChangedSignal.emit();
                        }
                        update();
                        e->accept();
                        break;
                default:
                        if (!e->text().isEmpty()) {
                                _lines[_cursorRow].insert(_cursorCol, e->text());
                                _cursorCol += static_cast<int>(e->text().length());
                                textChangedSignal.emit();
                                update();
                                e->accept();
                        }
                        break;
        }
}

PROMEKI_NAMESPACE_END
