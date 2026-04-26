/**
 * @file      lineedit.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/tui/lineedit.h>
#include <promeki/tui/painter.h>
#include <promeki/tui/palette.h>
#include <promeki/tui/tuisubsystem.h>
#include <promeki/keyevent.h>

PROMEKI_NAMESPACE_BEGIN

TuiLineEdit::TuiLineEdit(const String &text, ObjectBase *parent)
    : TuiWidget(parent), _text(text), _cursorPos(static_cast<int>(text.length())) {
        setFocusPolicy(StrongFocus);
}

TuiLineEdit::~TuiLineEdit() = default;

void TuiLineEdit::setText(const String &text) {
        if (_text == text) return;
        _text = text;
        _cursorPos = static_cast<int>(_text.length());
        textChangedSignal.emit(_text);
        update();
}

Size2Di32 TuiLineEdit::sizeHint() const {
        return Size2Di32(20, 1);
}

void TuiLineEdit::paintEvent(PaintEvent *) {
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

        // Ensure cursor is visible
        if (_cursorPos - _scrollOffset >= width()) {
                _scrollOffset = _cursorPos - width() + 1;
        }
        if (_cursorPos < _scrollOffset) {
                _scrollOffset = _cursorPos;
        }

        const String &display = _text.isEmpty() && !hasFocus() ? _placeholder : _text;
        if (!display.isEmpty()) {
                String visible = display.substr(
                        _scrollOffset, std::min(static_cast<size_t>(width()), display.length() - _scrollOffset));
                if (_text.isEmpty() && !hasFocus()) {
                        TuiStyle ph = pal.style(TuiPalette::PlaceholderText, false, isEnabled()).merged(s);
                        painter.setStyle(ph);
                }
                painter.drawText(0, 0, visible);
        }

        // Draw cursor
        if (hasFocus()) {
                int cursorScreenPos = _cursorPos - _scrollOffset;
                if (cursorScreenPos >= 0 && cursorScreenPos < width()) {
                        char32_t ch = (static_cast<size_t>(_cursorPos) < _text.length())
                                              ? _text.charAt(_cursorPos).codepoint()
                                              : U' ';
                        painter.setAttrs(TuiStyle::Inverse);
                        painter.drawChar(cursorScreenPos, 0, ch);
                        painter.setAttrs(TuiStyle::None);
                }
        }
}

void TuiLineEdit::keyPressEvent(KeyEvent *e) {
        // Let Ctrl-modified keys propagate (e.g. Ctrl+Left/Right for tab switching)
        if (e->isCtrl()) return;
        switch (e->key()) {
                case KeyEvent::Key_Left:
                        if (_cursorPos > 0) _cursorPos--;
                        update();
                        e->accept();
                        break;
                case KeyEvent::Key_Right:
                        if (static_cast<size_t>(_cursorPos) < _text.length()) _cursorPos++;
                        update();
                        e->accept();
                        break;
                case KeyEvent::Key_Home:
                        _cursorPos = 0;
                        update();
                        e->accept();
                        break;
                case KeyEvent::Key_End:
                        _cursorPos = static_cast<int>(_text.length());
                        update();
                        e->accept();
                        break;
                case KeyEvent::Key_Backspace:
                        if (_cursorPos > 0) {
                                _text.erase(_cursorPos - 1, 1);
                                _cursorPos--;
                                textChangedSignal.emit(_text);
                                update();
                        }
                        e->accept();
                        break;
                case KeyEvent::Key_Delete:
                        if (static_cast<size_t>(_cursorPos) < _text.length()) {
                                _text.erase(_cursorPos, 1);
                                textChangedSignal.emit(_text);
                                update();
                        }
                        e->accept();
                        break;
                case KeyEvent::Key_Enter:
                        returnPressedSignal.emit();
                        e->accept();
                        break;
                default:
                        if (!e->text().isEmpty()) {
                                _text.insert(_cursorPos, e->text());
                                _cursorPos += static_cast<int>(e->text().length());
                                textChangedSignal.emit(_text);
                                update();
                                e->accept();
                        }
                        break;
        }
}

void TuiLineEdit::focusInEvent(Event *) {
        update();
}

void TuiLineEdit::focusOutEvent(Event *) {
        update();
}

PROMEKI_NAMESPACE_END
