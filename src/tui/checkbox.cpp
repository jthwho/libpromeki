/**
 * @file      checkbox.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/tui/checkbox.h>
#include <promeki/tui/painter.h>
#include <promeki/tui/palette.h>
#include <promeki/tui/application.h>
#include <promeki/keyevent.h>
#include <promeki/mouseevent.h>

PROMEKI_NAMESPACE_BEGIN

TuiCheckBox::TuiCheckBox(const String &text, ObjectBase *parent)
        : TuiWidget(parent), _text(text) {
        setFocusPolicy(StrongFocus);
}

TuiCheckBox::~TuiCheckBox() = default;

void TuiCheckBox::setText(const String &text) {
        if(_text == text) return;
        _text = text;
        update();
}

void TuiCheckBox::setChecked(bool checked) {
        if(_checked == checked) return;
        _checked = checked;
        toggledSignal.emit(_checked);
        update();
}

void TuiCheckBox::toggle() {
        setChecked(!_checked);
}

Size2Di32 TuiCheckBox::sizeHint() const {
        // [x] Text
        return Size2Di32(static_cast<int>(_text.utf8Length()) + 4, 1);
}

void TuiCheckBox::paintEvent(TuiPaintEvent *) {
        TuiApplication *app = TuiApplication::instance();
        if(!app) return;

        Point2Di32 screenPos = mapToGlobal(Point2Di32(0, 0));
        Rect2Di32 clipRect(screenPos.x(), screenPos.y(), width(), height());
        TuiPainter painter(app->screen(), clipRect);

        const TuiPalette &pal = app->palette();
        painter.setForeground(pal.color(TuiPalette::WindowText, hasFocus(), isEnabled()));
        painter.setBackground(pal.color(TuiPalette::Window, hasFocus(), isEnabled()));
        painter.fillRect(Rect2Di32(0, 0, width(), height()));

        String indicator = _checked ? "[x] " : "[ ] ";
        painter.drawText(0, 0, indicator + _text);
}

void TuiCheckBox::keyEvent(KeyEvent *e) {
        if(e->key() == KeyEvent::Key_Enter || e->key() == KeyEvent::Key_Space) {
                toggle();
                e->accept();
        }
}

void TuiCheckBox::mouseEvent(MouseEvent *e) {
        if(e->action() == MouseEvent::Press && e->button() == MouseEvent::LeftButton) {
                toggle();
                e->accept();
        }
}

void TuiCheckBox::focusInEvent(Event *) {
        update();
}

void TuiCheckBox::focusOutEvent(Event *) {
        update();
}

PROMEKI_NAMESPACE_END
