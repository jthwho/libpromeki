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
        setMaximumSize(Size2Di32(9999, 1));
}

TuiButton::~TuiButton() = default;

void TuiButton::setText(const String &text) {
        if(_text == text) return;
        _text = text;
        update();
}

Size2Di32 TuiButton::sizeHint() const {
        // border + space + text + space + border
        return Size2Di32(static_cast<int>(_text.length()) + 4, 1);
}

Size2Di32 TuiButton::minimumSizeHint() const {
        return sizeHint();
}

void TuiButton::paintEvent(TuiPaintEvent *) {
        TuiApplication *app = TuiApplication::instance();
        if(!app) return;

        Point2Di32 screenPos = mapToGlobal(Point2Di32(0, 0));
        Rect2Di32 clipRect(screenPos.x(), screenPos.y(), width(), height());
        TuiPainter painter(app->screen(), clipRect);

        const TuiPalette &pal = app->palette();
        bool focused = hasFocus();
        bool enabled = isEnabled();

        // Background: ButtonDark when pressed, ButtonLight otherwise
        TuiPalette::ColorRole bgRole = _pressed ? TuiPalette::ButtonDark : TuiPalette::ButtonLight;
        TuiStyle bgStyle = pal.style(bgRole, false, enabled);

        // Foreground: FocusText when focused, ButtonText otherwise
        TuiPalette::ColorRole fgRole = focused ? TuiPalette::FocusText : TuiPalette::ButtonText;
        TuiStyle fgStyle = pal.style(fgRole, false, enabled);

        TuiStyle textStyle = fgStyle.merged(bgStyle);

        // Border
        TuiStyle borderStyle = pal.style(TuiPalette::ButtonBorder, false, enabled).merged(bgStyle);

        // Draw left border
        painter.setStyle(borderStyle);
        painter.drawChar(0, 0, U' ');

        // Draw text area
        painter.setStyle(textStyle);
        if(!_text.isEmpty()) {
                int textLen = static_cast<int>(_text.length());
                int innerWidth = std::max(0, width() - 2);
                int xoff = 1 + (innerWidth - textLen) / 2;
                if(xoff < 1) xoff = 1;
                // Fill inner area
                painter.fillRect(Rect2Di32(1, 0, innerWidth, 1));
                painter.drawText(xoff, 0, _text);
        }

        // Draw right border
        painter.setStyle(borderStyle);
        if(width() > 1) painter.drawChar(width() - 1, 0, U' ');
}

void TuiButton::keyEvent(KeyEvent *e) {
        if(e->key() == KeyEvent::Key_Enter || e->key() == KeyEvent::Key_Space) {
                clickedSignal.emit();
                e->accept();
        }
}

void TuiButton::mouseEvent(MouseEvent *e) {
        if(e->action() == MouseEvent::Press && e->button() == MouseEvent::LeftButton) {
                _pressed = true;
                update();
                e->accept();
        } else if(e->action() == MouseEvent::Release && _pressed) {
                _pressed = false;
                update();
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
