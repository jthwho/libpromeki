/**
 * @file      menu.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/tui/menu.h>
#include <promeki/tui/painter.h>
#include <promeki/tui/palette.h>
#include <promeki/tui/application.h>
#include <promeki/keyevent.h>

PROMEKI_NAMESPACE_BEGIN

// TuiMenu

TuiMenu::TuiMenu(const String &title, ObjectBase *parent)
        : TuiWidget(parent), _title(title) {
        setFocusPolicy(StrongFocus);
        setVisible(false);
}

TuiMenu::~TuiMenu() = default;

TuiAction *TuiMenu::addAction(const String &text) {
        TuiAction *action = new TuiAction(text, this);
        _actions += action;
        _separators += 0;
        return action;
}

void TuiMenu::addSeparator() {
        _separators += 1;
        _actions += nullptr;
}

void TuiMenu::setCurrentIndex(int index) {
        if(index < 0) index = 0;
        if(index >= static_cast<int>(_actions.size())) index = static_cast<int>(_actions.size()) - 1;
        _currentIndex = index;
        // Skip separators
        while(static_cast<size_t>(_currentIndex) < _separators.size() &&
              _separators[_currentIndex]) {
                _currentIndex++;
        }
        update();
}

void TuiMenu::open() {
        _open = true;
        setVisible(true);
        update();
}

void TuiMenu::close() {
        _open = false;
        setVisible(false);
        update();
}

Size2Di32 TuiMenu::sizeHint() const {
        int maxWidth = 0;
        for(size_t i = 0; i < _actions.size(); ++i) {
                if(_actions[i]) {
                        int len = static_cast<int>(_actions[i]->text().length());
                        if(len > maxWidth) maxWidth = len;
                }
        }
        return Size2Di32(maxWidth + 4, static_cast<int>(_actions.size()) + 2);
}

void TuiMenu::paintEvent(TuiPaintEvent *) {
        if(!_open) return;

        TuiApplication *app = TuiApplication::instance();
        if(!app) return;

        Point2Di32 screenPos = mapToGlobal(Point2Di32(0, 0));
        Rect2Di32 clipRect(screenPos.x(), screenPos.y(), width(), height());
        TuiPainter painter(app->screen(), clipRect);

        const TuiPalette &pal = app->palette();
        bool enabled = isEnabled();

        TuiStyle bgStyle = pal.style(TuiPalette::Mid, true, enabled)
                                .merged(pal.style(TuiPalette::Window, true, enabled));
        painter.setStyle(bgStyle);
        painter.fillRect(Rect2Di32(0, 0, width(), height()));
        painter.drawRect(Rect2Di32(0, 0, width(), height()));

        TuiStyle normalStyle = pal.style(TuiPalette::WindowText, true, enabled)
                                .merged(pal.style(TuiPalette::Window, true, enabled));
        TuiStyle hlStyle = pal.style(TuiPalette::HighlightedText, true, enabled)
                                .merged(pal.style(TuiPalette::Highlight, true, enabled));

        for(size_t i = 0; i < _actions.size(); ++i) {
                int row = static_cast<int>(i) + 1;
                if(_separators[i]) {
                        painter.setStyle(bgStyle);
                        painter.drawHLine(1, row, width() - 2, U'\u2500');
                        continue;
                }
                if(!_actions[i]) continue;

                if(static_cast<int>(i) == _currentIndex) {
                        painter.setStyle(hlStyle);
                        painter.fillRect(Rect2Di32(1, row, width() - 2, 1));
                } else {
                        painter.setStyle(normalStyle);
                }
                painter.drawText(2, row, _actions[i]->text());
        }
}

void TuiMenu::keyEvent(KeyEvent *e) {
        // Let Ctrl-modified keys propagate (e.g. Ctrl+Left/Right for tab switching)
        if(e->isCtrl()) return;
        switch(e->key()) {
                case KeyEvent::Key_Up:
                        if(_currentIndex > 0) {
                                _currentIndex--;
                                while(_currentIndex > 0 && _separators[_currentIndex]) _currentIndex--;
                        }
                        update();
                        e->accept();
                        break;
                case KeyEvent::Key_Down:
                        if(_currentIndex < static_cast<int>(_actions.size()) - 1) {
                                _currentIndex++;
                                while(static_cast<size_t>(_currentIndex) < _separators.size() &&
                                      _separators[_currentIndex]) _currentIndex++;
                        }
                        update();
                        e->accept();
                        break;
                case KeyEvent::Key_Enter:
                        if(_currentIndex >= 0 && static_cast<size_t>(_currentIndex) < _actions.size() &&
                           _actions[_currentIndex] && _actions[_currentIndex]->isEnabled()) {
                                _actions[_currentIndex]->triggeredSignal.emit();
                        }
                        close();
                        e->accept();
                        break;
                case KeyEvent::Key_Escape:
                        close();
                        e->accept();
                        break;
                default:
                        break;
        }
}

// TuiMenuBar

TuiMenuBar::TuiMenuBar(ObjectBase *parent) : TuiWidget(parent) {
        setFocusPolicy(TabFocus);
}

TuiMenuBar::~TuiMenuBar() = default;

TuiMenu *TuiMenuBar::addMenu(const String &title) {
        TuiMenu *menu = new TuiMenu(title, this);
        _menus += menu;
        return menu;
}

Size2Di32 TuiMenuBar::sizeHint() const {
        return Size2Di32(40, 1);
}

void TuiMenuBar::paintEvent(TuiPaintEvent *) {
        TuiApplication *app = TuiApplication::instance();
        if(!app) return;

        Point2Di32 screenPos = mapToGlobal(Point2Di32(0, 0));
        Rect2Di32 clipRect(screenPos.x(), screenPos.y(), width(), height());
        TuiPainter painter(app->screen(), clipRect);

        const TuiPalette &pal = app->palette();
        bool enabled = isEnabled();

        TuiStyle barStyle = pal.style(TuiPalette::StatusBarText, false, enabled)
                                .merged(pal.style(TuiPalette::StatusBar, false, enabled));
        painter.setStyle(barStyle);
        painter.fillRect(Rect2Di32(0, 0, width(), height()));

        TuiStyle hlStyle = pal.style(TuiPalette::HighlightedText, true, enabled)
                                .merged(pal.style(TuiPalette::Highlight, true, enabled));

        int xpos = 0;
        for(size_t i = 0; i < _menus.size(); ++i) {
                String label = String(" ") + _menus[i]->title() + " ";
                if(static_cast<int>(i) == _currentIndex && _active) {
                        painter.setStyle(hlStyle);
                } else {
                        painter.setStyle(barStyle);
                }
                painter.drawText(xpos, 0, label);
                xpos += static_cast<int>(label.length());
        }
}

void TuiMenuBar::keyEvent(KeyEvent *e) {
        if(e->isAlt() && !_active) {
                _active = true;
                update();
                e->accept();
                return;
        }

        if(!_active) return;

        switch(e->key()) {
                case KeyEvent::Key_Left:
                        if(_currentIndex > 0) _currentIndex--;
                        update();
                        e->accept();
                        break;
                case KeyEvent::Key_Right:
                        if(_currentIndex < static_cast<int>(_menus.size()) - 1) _currentIndex++;
                        update();
                        e->accept();
                        break;
                case KeyEvent::Key_Enter:
                case KeyEvent::Key_Down:
                        if(_currentIndex >= 0 && static_cast<size_t>(_currentIndex) < _menus.size()) {
                                TuiMenu *menu = _menus[_currentIndex];
                                // Position menu below the bar
                                Point2Di32 pos = mapToGlobal(Point2Di32(0, 1));
                                Size2Di32 hint = menu->sizeHint();
                                menu->setGeometry(Rect2Di32(pos.x(), pos.y(),
                                        hint.width(), hint.height()));
                                menu->open();
                                TuiApplication *app = TuiApplication::instance();
                                if(app) app->setFocusWidget(menu);
                        }
                        e->accept();
                        break;
                case KeyEvent::Key_Escape:
                        _active = false;
                        update();
                        e->accept();
                        break;
                default:
                        break;
        }
}

PROMEKI_NAMESPACE_END
