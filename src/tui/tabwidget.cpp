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
#include <promeki/mouseevent.h>

PROMEKI_NAMESPACE_BEGIN

TuiTabWidget::TuiTabWidget(ObjectBase *parent) : TuiWidget(parent) {
        setFocusPolicy(StrongFocus);
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
        bool enabled = isEnabled();

        // Clear tab bar background
        TuiStyle barBg = pal.style(TuiPalette::WindowText, false, enabled)
                                .merged(pal.style(TuiPalette::Window, false, enabled));
        painter.setStyle(barBg);
        painter.fillRect(Rect2Di32(0, 0, width(), 1));

        // Draw tab headers and record positions for mouse hit-testing
        bool focused = hasFocus();
        _tabPositions.clear();
        int xpos = 0;
        for(size_t i = 0; i < _tabs.size(); ++i) {
                bool isCurrent = (static_cast<int>(i) == _currentIndex);
                String label = String(" ") + _tabs[i].title + " ";
                int labelLen = static_cast<int>(label.length());
                _tabPositions += TabPos{xpos, xpos + labelLen};

                if(isCurrent && focused) {
                        // Focused active tab: FocusText fg + ButtonLight bg + Bold
                        TuiStyle s = pal.style(TuiPalette::FocusText, false, enabled)
                                        .merged(pal.style(TuiPalette::ButtonLight, false, enabled));
                        s.setAttrs(TuiStyle::Bold);
                        painter.setStyle(s);
                } else if(isCurrent) {
                        // Unfocused active tab: ButtonText fg + ButtonLight bg + Bold
                        TuiStyle s = pal.style(TuiPalette::ButtonText, false, enabled)
                                        .merged(pal.style(TuiPalette::ButtonLight, false, enabled));
                        s.setAttrs(TuiStyle::Bold);
                        painter.setStyle(s);
                } else {
                        // Inactive tab: ButtonText fg + ButtonDark bg
                        TuiStyle s = pal.style(TuiPalette::ButtonText, false, enabled)
                                        .merged(pal.style(TuiPalette::ButtonDark, false, enabled));
                        s.setAttrs(TuiStyle::None);
                        painter.setStyle(s);
                }
                painter.drawText(xpos, 0, label);
                xpos += labelLen;

                // Separator space
                painter.setStyle(barBg);
                if(xpos < width()) {
                        painter.drawChar(xpos, 0, U' ');
                        xpos++;
                }
        }
}

void TuiTabWidget::keyEvent(KeyEvent *e) {
        // When focused, Left/Right/Enter/Space select tabs directly
        if(hasFocus()) {
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
                if(e->key() == KeyEvent::Key_Enter || e->key() == KeyEvent::Key_Space) {
                        // Activate the current tab (move focus into it)
                        TuiApplication *app = TuiApplication::instance();
                        if(app) app->focusNext(false);
                        e->accept();
                        return;
                }
        }

        // Ctrl+Left / Ctrl+Right to switch tabs from anywhere
        if(e->isCtrl()) {
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

void TuiTabWidget::mouseEvent(MouseEvent *e) {
        if(e->action() != MouseEvent::Press || e->button() != MouseEvent::LeftButton) return;

        // Convert global mouse position to local coordinates
        Point2Di32 local = mapFromGlobal(e->pos());
        if(local.y() != 0) return; // Only tab bar row

        for(size_t i = 0; i < _tabPositions.size(); ++i) {
                if(local.x() >= _tabPositions[i].startX && local.x() < _tabPositions[i].endX) {
                        setCurrentIndex(static_cast<int>(i));
                        e->accept();
                        return;
                }
        }
}

void TuiTabWidget::resizeEvent(TuiResizeEvent *) {
        updateTabGeometry();
}

void TuiTabWidget::focusInEvent(Event *) {
        update();
}

void TuiTabWidget::focusOutEvent(Event *) {
        update();
}

PROMEKI_NAMESPACE_END
