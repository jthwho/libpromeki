/**
 * @file      application.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/tui/application.h>
#include <promeki/tui/widget.h>
#include <promeki/tui/painter.h>
#include <promeki/tui/layout.h>
#include <promeki/keyevent.h>
#include <promeki/mouseevent.h>
#include <promeki/eventloop.h>

#if defined(PROMEKI_PLATFORM_POSIX)
#include <unistd.h>
#include <poll.h>
#endif

PROMEKI_NAMESPACE_BEGIN

TuiApplication *TuiApplication::_instance = nullptr;

TuiApplication::TuiApplication(int argc, char **argv)
        : Application(argc, argv), _ansiStream(std::cout) {
        _instance = this;
}

TuiApplication::~TuiApplication() {
        _terminal.disableMouseTracking();
        _terminal.disableBracketedPaste();
        _terminal.disableAlternateScreen();
        _terminal.disableRawMode();
        _ansiStream.reset();
        _ansiStream.showCursor();
        _ansiStream << std::flush;
        if(_instance == this) _instance = nullptr;
}

void TuiApplication::setRootWidget(TuiWidget *widget) {
        _rootWidget = widget;
        if(_rootWidget) {
                int cols, rows;
                if(_terminal.windowSize(cols, rows)) {
                        _rootWidget->setGeometry(Rect2Di32(0, 0, cols, rows));
                }
        }
}

int TuiApplication::exec() {
        _terminal.enableRawMode();
        _terminal.enableAlternateScreen();
        _terminal.enableMouseTracking();
        _terminal.enableBracketedPaste();
        _terminal.installSignalHandlers();

        _terminal.setResizeCallback([this](int cols, int rows) {
                (void)cols;
                (void)rows;
                handleResize();
        });

        _ansiStream.hideCursor();
        _ansiStream.clearScreen();
        _ansiStream << std::flush;

        // Initial size
        int cols, rows;
        if(_terminal.windowSize(cols, rows)) {
                _lastCols = cols;
                _lastRows = rows;
                _screen.resize(cols, rows);
                if(_rootWidget) {
                        _rootWidget->setGeometry(Rect2Di32(0, 0, cols, rows));
                }
        }

        // Set initial focus to first focusable widget if none set
        if(!_focusWidget) {
                focusNext(false);
        }

        _running = true;
        _needsRepaint = true; // Force initial paint

        while(_running) {
                // Check for resize
                handleResize();

                // Process EventLoop events (timers, posted callables, etc.)
                _eventLoop.processEvents();

                // Process any available input
                processInput();

                // Paint if anything is dirty
                if(_needsRepaint) {
                        paintWidgets();
                        _screen.flush(_ansiStream);
                        _needsRepaint = false;
                }

                // Wait for input (blocks up to 16ms)
#if defined(PROMEKI_PLATFORM_POSIX)
                struct pollfd pfd;
                pfd.fd = STDIN_FILENO;
                pfd.events = POLLIN;
                poll(&pfd, 1, 16); // ~60fps max
#endif
        }

        return _exitCode;
}

void TuiApplication::quit(int exitCode) {
        _exitCode = exitCode;
        _running = false;
}

void TuiApplication::updateAll() {
        _screen.invalidate();
        markNeedsRepaint();
}

void TuiApplication::markNeedsRepaint() {
        _needsRepaint = true;
}

void TuiApplication::setFocusWidget(TuiWidget *widget) {
        if(_focusWidget == widget) return;
        if(_focusWidget) {
                _focusWidget->_focused = false;
                Event e(Event::InvalidType);
                _focusWidget->focusOutEvent(&e);
                _focusWidget->update();
        }
        _focusWidget = widget;
        if(_focusWidget) {
                _focusWidget->_focused = true;
                Event e(Event::InvalidType);
                _focusWidget->focusInEvent(&e);
                _focusWidget->update();
        }
        markNeedsRepaint();
}

void TuiApplication::focusNext(bool reverse) {
        List<TuiWidget *> focusable;
        if(_rootWidget) collectFocusable(_rootWidget, focusable);
        if(focusable.isEmpty()) return;

        int current = -1;
        for(size_t i = 0; i < focusable.size(); ++i) {
                if(focusable[i] == _focusWidget) {
                        current = static_cast<int>(i);
                        break;
                }
        }

        int next;
        if(reverse) {
                next = (current <= 0) ? static_cast<int>(focusable.size()) - 1 : current - 1;
        } else {
                next = (current < 0 || current >= static_cast<int>(focusable.size()) - 1)
                       ? 0 : current + 1;
        }

        setFocusWidget(focusable[next]);
}

void TuiApplication::collectFocusable(TuiWidget *widget, List<TuiWidget *> &list) {
        if(!widget->isEffectivelyVisible() || !widget->isEnabled()) return;
        if(widget->focusPolicy() == StrongFocus || widget->focusPolicy() == TabFocus) {
                list += widget;
        }
        for(auto child : widget->childList()) {
                TuiWidget *tw = dynamic_cast<TuiWidget *>(child);
                if(tw) collectFocusable(tw, list);
        }
}

void TuiApplication::processInput() {
        char buf[256];
        int n = _terminal.readInput(buf, sizeof(buf));
        if(n <= 0) return;

        List<TuiInputParser::ParsedEvent> events = _inputParser.feed(buf, n);
        for(size_t i = 0; i < events.size(); ++i) {
                const TuiInputParser::ParsedEvent &ev = events[i];
                if(ev.type == TuiInputParser::ParsedEvent::Key) {
                        dispatchKeyEvent(ev);
                } else if(ev.type == TuiInputParser::ParsedEvent::Mouse) {
                        dispatchMouseEvent(ev);
                }
        }
}

void TuiApplication::dispatchKeyEvent(const TuiInputParser::ParsedEvent &ev) {
        // Ctrl+Q quits the application
        if(ev.key == static_cast<KeyEvent::Key>('q') &&
           (ev.modifiers & KeyEvent::CtrlModifier)) {
                quit(0);
                return;
        }

        // Tab cycles focus
        if(ev.key == KeyEvent::Key_Tab) {
                bool shift = (ev.modifiers & KeyEvent::ShiftModifier) != 0;
                focusNext(shift);
                return;
        }

        if(_focusWidget) {
                KeyEvent keyEv(KeyEvent::KeyPress, ev.key, ev.modifiers, ev.text);
                // Try focused widget first, then propagate up to parents
                TuiWidget *target = _focusWidget;
                while(target) {
                        target->keyEvent(&keyEv);
                        if(keyEv.isAccepted()) break;
                        target = dynamic_cast<TuiWidget *>(target->parent());
                }
                markNeedsRepaint();
        }
}

TuiWidget *TuiApplication::widgetAt(TuiWidget *widget, const Point2Di32 &globalPos) {
        if(!widget->isEffectivelyVisible()) return nullptr;

        Point2Di32 screenPos = widget->mapToGlobal(Point2Di32(0, 0));
        Rect2Di32 widgetRect(screenPos.x(), screenPos.y(), widget->width(), widget->height());
        if(!widgetRect.contains(globalPos)) return nullptr;

        // Check children in reverse order (top-most first)
        const auto &children = widget->childList();
        for(int i = static_cast<int>(children.size()) - 1; i >= 0; --i) {
                TuiWidget *tw = dynamic_cast<TuiWidget *>(children[i]);
                if(!tw) continue;
                TuiWidget *hit = widgetAt(tw, globalPos);
                if(hit) return hit;
        }

        return widget;
}

void TuiApplication::dispatchMouseEvent(const TuiInputParser::ParsedEvent &ev) {
        if(!_rootWidget) return;

        MouseEvent::Action action = ev.mouseAction;

        // Double-click detection: if this is a Press at the same position
        // and button within the threshold, promote to DoubleClick.
        if(action == MouseEvent::Press) {
                auto now = Clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now - _lastClickTime).count();
                if(elapsed <= DoubleClickIntervalMs &&
                   ev.mouseButton == _lastClickButton &&
                   ev.mousePos == _lastClickPos) {
                        action = MouseEvent::DoubleClick;
                        // Reset so a third click doesn't become another double-click
                        _lastClickTime = TimePoint{};
                        _lastClickButton = MouseEvent::NoButton;
                        _lastClickPos = Point2Di32(-1, -1);
                } else {
                        _lastClickTime = now;
                        _lastClickButton = ev.mouseButton;
                        _lastClickPos = ev.mousePos;
                }
        }

        MouseEvent mouseEv(ev.mousePos, ev.mouseButton, action, ev.modifiers, ev.mouseButtons);

        // If a widget has the mouse grab, send all events directly to it
        if(_mouseGrab) {
                _mouseGrab->mouseEvent(&mouseEv);
                markNeedsRepaint();
                return;
        }

        // Find the widget under the mouse
        TuiWidget *target = widgetAt(_rootWidget, ev.mousePos);
        if(!target) return;

        // Focus the clicked widget if it accepts focus
        if(action == MouseEvent::Press || action == MouseEvent::DoubleClick) {
                if(target->focusPolicy() == ClickFocus || target->focusPolicy() == StrongFocus) {
                        setFocusWidget(target);
                }
        }

        // Propagate up the widget tree until accepted
        TuiWidget *t = target;
        while(t) {
                t->mouseEvent(&mouseEv);
                if(mouseEv.isAccepted()) break;
                t = dynamic_cast<TuiWidget *>(t->parent());
        }
        markNeedsRepaint();
}

void TuiApplication::handleResize() {
        int cols, rows;
        if(!_terminal.windowSize(cols, rows)) return;
        if(cols == _lastCols && rows == _lastRows) return;

        _lastCols = cols;
        _lastRows = rows;
        _screen.resize(cols, rows);
        _screen.invalidate();

        if(_rootWidget) {
                _rootWidget->setGeometry(Rect2Di32(0, 0, cols, rows));
                if(_rootWidget->layout()) {
                        _rootWidget->layout()->calculateLayout(
                                Rect2Di32(0, 0, cols, rows));
                }
        }
        markNeedsRepaint();
}

void TuiApplication::paintWidgets() {
        if(!_rootWidget) return;
        _screen.clear();
        paintWidget(_rootWidget);
}

void TuiApplication::paintWidget(TuiWidget *widget) {
        if(!widget->isVisible()) return;

        // Calculate screen position
        Point2Di32 screenPos = widget->mapToGlobal(Point2Di32(0, 0));
        Rect2Di32 clipRect(screenPos.x(), screenPos.y(),
                        widget->width(), widget->height());

        TuiPainter painter(_screen, clipRect);
        TuiPaintEvent ev;
        widget->paintEvent(&ev);

        // Paint children
        for(auto child : widget->childList()) {
                TuiWidget *tw = dynamic_cast<TuiWidget *>(child);
                if(tw) paintWidget(tw);
        }
}

PROMEKI_NAMESPACE_END
