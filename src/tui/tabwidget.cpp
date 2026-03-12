/**
 * @file      tabwidget.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/tui/tabwidget.h>
#include <promeki/tui/painter.h>
#include <promeki/tui/palette.h>
#include <promeki/tui/application.h>
#include <promeki/keyevent.h>

PROMEKI_NAMESPACE_BEGIN

TuiTabWidget::TuiTabWidget(ObjectBase *parent) : TuiWidget(parent) {
        setFocusPolicy(NoFocus);
}

TuiTabWidget::~TuiTabWidget() = default;

void TuiTabWidget::addTab(TuiWidget *widget, const String &title) {
        _tabs += Tab{widget, title};
        if(widget) widget->setParent(this);
        if(_currentIndex < 0) _currentIndex = 0;
        updateTabGeometry();
        update();
}

void TuiTabWidget::removeTab(int index) {
        if(index < 0 || static_cast<size_t>(index) >= _tabs.size()) return;
        _tabs.remove(static_cast<size_t>(index));
        if(_currentIndex >= static_cast<int>(_tabs.size())) {
                _currentIndex = static_cast<int>(_tabs.size()) - 1;
        }
        updateTabGeometry();
        update();
}

void TuiTabWidget::setCurrentIndex(int index) {
        if(index < 0 || static_cast<size_t>(index) >= _tabs.size()) return;
        if(_currentIndex == index) return;
        _currentIndex = index;
        updateTabGeometry();
        currentChangedSignal.emit(_currentIndex);
        update();
}

TuiWidget *TuiTabWidget::currentWidget() const {
        if(_currentIndex < 0 || static_cast<size_t>(_currentIndex) >= _tabs.size()) return nullptr;
        return _tabs[_currentIndex].widget;
}

Size2Di32 TuiTabWidget::sizeHint() const {
        return Size2Di32(40, 15);
}

void TuiTabWidget::updateTabGeometry() {
        // Tab bar takes 1 row, content gets the rest
        Rect2Di32 contentArea(0, 1, width(), std::max(0, height() - 1));
        for(size_t i = 0; i < _tabs.size(); ++i) {
                if(_tabs[i].widget) {
                        _tabs[i].widget->setVisible(static_cast<int>(i) == _currentIndex);
                        if(static_cast<int>(i) == _currentIndex) {
                                _tabs[i].widget->setGeometry(contentArea);
                        }
                }
        }
}

void TuiTabWidget::paintEvent(TuiPaintEvent *) {
        TuiApplication *app = TuiApplication::instance();
        if(!app) return;

        Point2Di32 screenPos = mapToGlobal(Point2Di32(0, 0));
        Rect2Di32 clipRect(screenPos.x(), screenPos.y(), width(), height());
        TuiPainter painter(app->screen(), clipRect);

        const TuiPalette &pal = app->palette();
        painter.setForeground(pal.color(TuiPalette::WindowText, false, isEnabled()));
        painter.setBackground(pal.color(TuiPalette::Window, false, isEnabled()));
        painter.fillRect(Rect2Di32(0, 0, width(), 1));

        // Draw tab headers
        int xpos = 0;
        for(size_t i = 0; i < _tabs.size(); ++i) {
                String label = String(" ") + _tabs[i].title + " ";
                if(static_cast<int>(i) == _currentIndex) {
                        painter.setForeground(pal.color(TuiPalette::HighlightedText, true, isEnabled()));
                        painter.setBackground(pal.color(TuiPalette::Highlight, true, isEnabled()));
                } else {
                        painter.setForeground(pal.color(TuiPalette::WindowText, false, isEnabled()));
                        painter.setBackground(pal.color(TuiPalette::Mid, false, isEnabled()));
                }
                painter.drawText(xpos, 0, label);
                xpos += static_cast<int>(label.length());

                painter.setForeground(pal.color(TuiPalette::Mid, false, isEnabled()));
                painter.setBackground(pal.color(TuiPalette::Window, false, isEnabled()));
                if(xpos < width()) {
                        painter.drawChar(xpos, 0, U'\u2502');
                        xpos++;
                }
        }
}

void TuiTabWidget::keyEvent(KeyEvent *e) {
        if(e->isAlt()) {
                // Alt+number to switch tabs
                if(e->key() >= '1' && e->key() <= '9') {
                        int idx = e->key() - '1';
                        if(idx < static_cast<int>(_tabs.size())) {
                                setCurrentIndex(idx);
                                e->accept();
                                return;
                        }
                }
                // Alt+Left / Alt+Right to switch tabs
                if(e->key() == KeyEvent::Key_Left && _tabs.size() > 1) {
                        setCurrentIndex((_currentIndex - 1 + static_cast<int>(_tabs.size())) %
                                        static_cast<int>(_tabs.size()));
                        e->accept();
                        return;
                }
                if(e->key() == KeyEvent::Key_Right && _tabs.size() > 1) {
                        setCurrentIndex((_currentIndex + 1) % static_cast<int>(_tabs.size()));
                        e->accept();
                        return;
                }
        }
}

void TuiTabWidget::resizeEvent(TuiResizeEvent *) {
        updateTabGeometry();
}

PROMEKI_NAMESPACE_END
