/**
 * @file      splitter.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/tui/splitter.h>
#include <promeki/tui/painter.h>
#include <promeki/tui/palette.h>
#include <promeki/tui/application.h>
#include <promeki/keyevent.h>

PROMEKI_NAMESPACE_BEGIN

TuiSplitter::TuiSplitter(Orientation orientation, ObjectBase *parent)
        : TuiWidget(parent), _orientation(orientation) {
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
        TuiApplication *app = TuiApplication::instance();
        if(!app) return;

        Point2Di32 screenPos = mapToGlobal(Point2Di32(0, 0));
        Rect2Di32 clipRect(screenPos.x(), screenPos.y(), width(), height());
        TuiPainter painter(app->screen(), clipRect);

        const TuiPalette &pal = app->palette();
        painter.setForeground(pal.color(TuiPalette::Mid, false, isEnabled()));
        painter.setBackground(pal.color(TuiPalette::Window, false, isEnabled()));

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
        // Allow adjusting split with Ctrl+arrows when focused
        if(e->isCtrl()) {
                if(_orientation == Horizontal) {
                        if(e->key() == KeyEvent::Key_Left) {
                                setSplitRatio(_splitRatio - 0.05);
                                e->accept();
                        } else if(e->key() == KeyEvent::Key_Right) {
                                setSplitRatio(_splitRatio + 0.05);
                                e->accept();
                        }
                } else {
                        if(e->key() == KeyEvent::Key_Up) {
                                setSplitRatio(_splitRatio - 0.05);
                                e->accept();
                        } else if(e->key() == KeyEvent::Key_Down) {
                                setSplitRatio(_splitRatio + 0.05);
                                e->accept();
                        }
                }
        }
}

PROMEKI_NAMESPACE_END
