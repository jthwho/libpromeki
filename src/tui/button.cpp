/**
 * @file      button.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/tui/button.h>
#include <promeki/tui/painter.h>
#include <promeki/tui/screen.h>
#include <promeki/tui/palette.h>
#include <promeki/tui/application.h>
#include <promeki/keyevent.h>
#include <promeki/mouseevent.h>

PROMEKI_NAMESPACE_BEGIN

TuiButton::TuiButton(const String &text, ObjectBase *parent)
        : TuiWidget(parent), _text(text) {
        setFocusPolicy(StrongFocus);
}

TuiButton::~TuiButton() = default;

void TuiButton::setText(const String &text) {
        if(_text == text) return;
        _text = text;
        update();
}

Size2Di32 TuiButton::sizeHint() const {
        return Size2Di32(static_cast<int>(_text.length()) + 4, 3);
}

void TuiButton::paintEvent(TuiPaintEvent *) {
        TuiApplication *app = TuiApplication::instance();
        if(!app) return;

        Point2Di32 screenPos = mapToGlobal(Point2Di32(0, 0));
        Rect2Di32 clipRect(screenPos.x(), screenPos.y(), width(), height());
        TuiPainter painter(app->screen(), clipRect);

        const TuiPalette &pal = app->palette();
        painter.setForeground(pal.color(TuiPalette::ButtonText, hasFocus(), isEnabled()));
        painter.setBackground(pal.color(TuiPalette::Button, hasFocus(), isEnabled()));

        // Fill background
        painter.fillRect(Rect2Di32(0, 0, width(), height()));

        // Draw border
        if(height() >= 3 && width() >= 2) {
                painter.drawRect(Rect2Di32(0, 0, width(), height()));
        }

        // Draw text centered
        if(!_text.isEmpty()) {
                int textLen = static_cast<int>(_text.length());
                int xoff = (width() - textLen) / 2;
                int yoff = height() / 2;
                if(xoff < 0) xoff = 0;
                painter.drawText(xoff, yoff, _text);
        }
}

void TuiButton::keyEvent(KeyEvent *e) {
        if(e->key() == KeyEvent::Key_Enter || e->key() == KeyEvent::Key_Space) {
                clickedSignal.emit();
                e->accept();
        }
}

void TuiButton::mouseEvent(MouseEvent *e) {
        if(e->action() == MouseEvent::Press && e->button() == MouseEvent::LeftButton) {
                clickedSignal.emit();
                e->accept();
        }
}

void TuiButton::focusInEvent(Event *) {
        update();
}

void TuiButton::focusOutEvent(Event *) {
        update();
}

PROMEKI_NAMESPACE_END
