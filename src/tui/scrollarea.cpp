/**
 * @file      scrollarea.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/tui/scrollarea.h>
#include <promeki/tui/painter.h>
#include <promeki/tui/palette.h>
#include <promeki/tui/tuisubsystem.h>
#include <promeki/keyevent.h>

PROMEKI_NAMESPACE_BEGIN

TuiScrollArea::TuiScrollArea(ObjectBase *parent) : TuiWidget(parent) {
        setFocusPolicy(StrongFocus);
}

TuiScrollArea::~TuiScrollArea() = default;

void TuiScrollArea::setContentWidget(TuiWidget *widget) {
        _contentWidget = widget;
        if (_contentWidget) {
                _contentWidget->setParent(this);
        }
        update();
}

void TuiScrollArea::setScrollX(int val) {
        _scrollX = val;
        clampScroll();
        update();
}

void TuiScrollArea::setScrollY(int val) {
        _scrollY = val;
        clampScroll();
        update();
}

void TuiScrollArea::scrollTo(int x, int y) {
        _scrollX = x;
        _scrollY = y;
        clampScroll();
        update();
}

void TuiScrollArea::clampScroll() {
        if (_scrollX < 0) _scrollX = 0;
        if (_scrollY < 0) _scrollY = 0;
        if (_contentWidget) {
                int maxX = std::max(0, _contentWidget->width() - width());
                int maxY = std::max(0, _contentWidget->height() - height());
                if (_scrollX > maxX) _scrollX = maxX;
                if (_scrollY > maxY) _scrollY = maxY;
        }
}

Size2Di32 TuiScrollArea::sizeHint() const {
        if (_contentWidget) return _contentWidget->sizeHint();
        return Size2Di32(20, 10);
}

void TuiScrollArea::paintEvent(PaintEvent *) {
        TuiSubsystem *app = TuiSubsystem::instance();
        if (!app) return;

        Point2Di32 screenPos = mapToGlobal(Point2Di32(0, 0));
        Rect2Di32  clipRect(screenPos.x(), screenPos.y(), width(), height());
        TuiPainter painter(app->screen(), clipRect);

        const TuiPalette &pal = app->palette();
        TuiStyle          s = pal.style(TuiPalette::WindowText, hasFocus(), isEnabled())
                             .merged(pal.style(TuiPalette::Window, hasFocus(), isEnabled()));
        painter.setStyle(s);
        painter.fillRect(Rect2Di32(0, 0, width(), height()));

        // Content widget is positioned by offset
        if (_contentWidget) {
                _contentWidget->setGeometry(
                        Rect2Di32(-_scrollX, -_scrollY, _contentWidget->width(), _contentWidget->height()));
        }
}

void TuiScrollArea::keyPressEvent(KeyEvent *e) {
        // Let Ctrl-modified keys propagate (e.g. Ctrl+Left/Right for tab switching)
        if (e->isCtrl()) return;
        switch (e->key()) {
                case KeyEvent::Key_Up:
                        setScrollY(_scrollY - 1);
                        e->accept();
                        break;
                case KeyEvent::Key_Down:
                        setScrollY(_scrollY + 1);
                        e->accept();
                        break;
                case KeyEvent::Key_Left:
                        setScrollX(_scrollX - 1);
                        e->accept();
                        break;
                case KeyEvent::Key_Right:
                        setScrollX(_scrollX + 1);
                        e->accept();
                        break;
                case KeyEvent::Key_PageUp:
                        setScrollY(_scrollY - height());
                        e->accept();
                        break;
                case KeyEvent::Key_PageDown:
                        setScrollY(_scrollY + height());
                        e->accept();
                        break;
                default: break;
        }
}

void TuiScrollArea::resizeEvent(ResizeEvent *) {
        clampScroll();
}

PROMEKI_NAMESPACE_END
