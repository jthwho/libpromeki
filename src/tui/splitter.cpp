/**
 * @file      splitter.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/tui/splitter.h>
#include <promeki/tui/painter.h>
#include <promeki/tui/palette.h>
#include <promeki/tui/tuisubsystem.h>
#include <promeki/keyevent.h>
#include <promeki/mouseevent.h>

PROMEKI_NAMESPACE_BEGIN

TuiSplitter::TuiSplitter(Orientation orientation, ObjectBase *parent)
        : TuiWidget(parent), _orientation(orientation) {
        setFocusPolicy(StrongFocus);
}

TuiSplitter::~TuiSplitter() = default;

void TuiSplitter::setFirstWidget(TuiWidget *widget) {
        _first = widget;
        if(_first) _first->setParent(this);
        updateChildGeometry();
}

void TuiSplitter::setSecondWidget(TuiWidget *widget) {
        _second = widget;
        if(_second) _second->setParent(this);
        updateChildGeometry();
}

void TuiSplitter::setSplitRatio(double ratio) {
        if(ratio < 0.0) ratio = 0.0;
        if(ratio > 1.0) ratio = 1.0;
        _splitRatio = ratio;
        updateChildGeometry();
        update();
}

Size2Di32 TuiSplitter::sizeHint() const {
        return Size2Di32(40, 20);
}

void TuiSplitter::updateChildGeometry() {
        if(_orientation == Horizontal) {
                int splitPos = static_cast<int>(width() * _splitRatio);
                if(_first) _first->setGeometry(Rect2Di32(0, 0, splitPos, height()));
                if(_second) _second->setGeometry(Rect2Di32(splitPos + 1, 0,
                        width() - splitPos - 1, height()));
        } else {
                int splitPos = static_cast<int>(height() * _splitRatio);
                if(_first) _first->setGeometry(Rect2Di32(0, 0, width(), splitPos));
                if(_second) _second->setGeometry(Rect2Di32(0, splitPos + 1,
                        width(), height() - splitPos - 1));
        }
}

void TuiSplitter::paintEvent(TuiPaintEvent *) {
        TuiSubsystem *app = TuiSubsystem::instance();
        if(!app) return;

        Point2Di32 screenPos = mapToGlobal(Point2Di32(0, 0));
        Rect2Di32 clipRect(screenPos.x(), screenPos.y(), width(), height());
        TuiPainter painter(app->screen(), clipRect);

        const TuiPalette &pal = app->palette();
        bool focused = hasFocus();

        // Foreground: FocusText when focused, Mid otherwise
        TuiPalette::ColorRole fgRole = focused ? TuiPalette::FocusText : TuiPalette::Mid;
        TuiStyle s = pal.style(fgRole, false, isEnabled())
                        .merged(pal.style(TuiPalette::Window, false, isEnabled()));
        painter.setStyle(s);

        // Draw separator
        if(_orientation == Horizontal) {
                int splitPos = static_cast<int>(width() * _splitRatio);
                painter.drawVLine(splitPos, 0, height(), U'\u2502');
        } else {
                int splitPos = static_cast<int>(height() * _splitRatio);
                painter.drawHLine(0, splitPos, width(), U'\u2500');
        }
}

void TuiSplitter::resizeEvent(TuiResizeEvent *) {
        updateChildGeometry();
}

void TuiSplitter::keyEvent(KeyEvent *e) {
        if(!hasFocus()) return;
        // Let Ctrl-modified keys propagate (e.g. Ctrl+Left/Right for tab switching)
        if(e->isCtrl()) return;

        // Arrow keys move the splitter by one line
        if(_orientation == Horizontal) {
                int total = width();
                if(total <= 0) return;
                double step = 1.0 / total;
                if(e->key() == KeyEvent::Key_Left) {
                        setSplitRatio(_splitRatio - step);
                        e->accept();
                } else if(e->key() == KeyEvent::Key_Right) {
                        setSplitRatio(_splitRatio + step);
                        e->accept();
                }
        } else {
                int total = height();
                if(total <= 0) return;
                double step = 1.0 / total;
                if(e->key() == KeyEvent::Key_Up) {
                        setSplitRatio(_splitRatio - step);
                        e->accept();
                } else if(e->key() == KeyEvent::Key_Down) {
                        setSplitRatio(_splitRatio + step);
                        e->accept();
                }
        }
}

void TuiSplitter::mouseEvent(MouseEvent *e) {
        Point2Di32 local = mapFromGlobal(e->pos());

        if(e->action() == MouseEvent::Press && e->button() == MouseEvent::LeftButton) {
                // Check if click is on the separator
                bool onSep = false;
                if(_orientation == Horizontal) {
                        int splitPos = static_cast<int>(width() * _splitRatio);
                        onSep = (local.x() >= splitPos - 1 && local.x() <= splitPos + 1);
                } else {
                        int splitPos = static_cast<int>(height() * _splitRatio);
                        onSep = (local.y() >= splitPos - 1 && local.y() <= splitPos + 1);
                }
                if(onSep) {
                        _dragging = true;
                        TuiSubsystem *app = TuiSubsystem::instance();
                        if(app) app->grabMouse(this);
                        e->accept();
                }
        } else if(e->action() == MouseEvent::Move && _dragging) {
                if(_orientation == Horizontal && width() > 0) {
                        setSplitRatio(static_cast<double>(local.x()) / width());
                } else if(_orientation == Vertical && height() > 0) {
                        setSplitRatio(static_cast<double>(local.y()) / height());
                }
                e->accept();
        } else if(e->action() == MouseEvent::Release && _dragging) {
                _dragging = false;
                TuiSubsystem *app = TuiSubsystem::instance();
                if(app) app->releaseMouse();
                e->accept();
        }
}

PROMEKI_NAMESPACE_END
