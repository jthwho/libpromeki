/**
 * @file      tuisubsystem.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/tui/tuisubsystem.h>
#include <promeki/tui/widget.h>
#include <promeki/tui/painter.h>
#include <promeki/tui/layout.h>
#include <promeki/keyevent.h>
#include <promeki/mouseevent.h>
#include <promeki/eventloop.h>
#include <promeki/atomic.h>
#include <promeki/platform.h>
#include <promeki/logger.h>
#include <promeki/signalhandler.h>
#include <promeki/util.h>

#if defined(PROMEKI_PLATFORM_POSIX)
#include <csignal>
#include <unistd.h>
#endif

PROMEKI_NAMESPACE_BEGIN

TuiSubsystem *TuiSubsystem::_instance = nullptr;

TuiSubsystem::TuiSubsystem()
        : _eventLoop(Application::mainEventLoop()),
          _ansiStream(Application::stdoutDevice()) {
        // Singletons by design — constructing a second TuiSubsystem
        // while one is still live would silently clobber the instance
        // pointer and leave the terminal in an undefined state.
        // Catch it as a programming error at construction.
        PROMEKI_ASSERT(_instance == nullptr);
        if(_eventLoop == nullptr) {
                promekiErr("TuiSubsystem: no Application / main EventLoop — "
                           "construct an Application before a TuiSubsystem");
        }
        _instance = this;
        _screen.setColorMode(Terminal::colorSupport());

        _terminal.enableRawMode();
        _terminal.enableAlternateScreen();
        _terminal.enableMouseTracking();
        _terminal.enableBracketedPaste();
        _terminal.installSignalHandlers();

        _ansiStream.hideCursor();
        _ansiStream.clearScreen();
        _ansiStream.flush();

        // Capture initial window size so the root widget (when it is
        // set later via setRootWidget) can be laid out to the
        // terminal.  Repeated in setRootWidget to handle the case
        // where the size changes between TuiSubsystem construction
        // and the first widget attach.
        int cols, rows;
        if(_terminal.windowSize(cols, rows).isOk()) {
                _lastCols = cols;
                _lastRows = rows;
                _screen.resize(cols, rows);
        }

        setupEventSources();

        // Queue the initial paint through the same coalescing post
        // path runtime repaints use.  The callable fires on the
        // first processEvents() iteration inside app.exec().
        markNeedsRepaint();
}

TuiSubsystem::~TuiSubsystem() {
        teardownEventSources();

        _terminal.disableMouseTracking();
        _terminal.disableBracketedPaste();
        _terminal.disableAlternateScreen();
        _terminal.disableRawMode();
        _ansiStream.reset();
        _ansiStream.showCursor();
        _ansiStream.flush();
        if(_instance == this) _instance = nullptr;
}

void TuiSubsystem::setRootWidget(TuiWidget *widget) {
        _rootWidget = widget;
        if(_rootWidget) {
                int cols, rows;
                if(_terminal.windowSize(cols, rows).isOk()) {
                        _rootWidget->setGeometry(Rect2Di32(0, 0, cols, rows));
                }
                if(!_focusWidget) focusNext(false);
                markNeedsRepaint();
        }
}

void TuiSubsystem::updateAll() {
        _screen.invalidate();
        markNeedsRepaint();
}

void TuiSubsystem::markNeedsRepaint() {
        // Coalesce: at most one repaint pending at any time.  If we
        // were the first to flip the flag to true we post the
        // actual paint; subsequent calls before that post fires are
        // absorbed.  Safe from any thread — the paint callable
        // always runs on the EventLoop thread.
        if(_eventLoop == nullptr) return;
        if(_repaintQueued.exchange(true)) return;
        _eventLoop->postCallable([this] { doPaint(); });
}

void TuiSubsystem::doPaint() {
        _repaintQueued.setValue(false);
        paintWidgets();
        _screen.flush(_ansiStream);
}

void TuiSubsystem::setupEventSources() {
#if defined(PROMEKI_PLATFORM_POSIX)
        if(_eventLoop == nullptr) return;

        _stdinSourceHandle = _eventLoop->addIoSource(STDIN_FILENO,
                EventLoop::IoRead,
                [this](int, uint32_t) { processInput(); });

        // Route SIGWINCH through the single SignalHandler watcher
        // thread.  Its callback runs in normal context, so all it
        // needs to do is post the resize handling onto the main
        // EventLoop — there is no sigaction, no self-pipe, and no
        // IoSource owned by this subsystem.  SignalHandler is
        // installed by Application's constructor, so by the time any
        // TuiSubsystem is built the subscriber API is live.
        EventLoop *loop = _eventLoop;
        _winchSubscription = SignalHandler::subscribe(SIGWINCH,
                [this, loop](int) {
                        if(loop != nullptr) {
                                loop->postCallable([this] { handleResize(); });
                        }
                });
#endif // PROMEKI_PLATFORM_POSIX
}

void TuiSubsystem::teardownEventSources() {
#if defined(PROMEKI_PLATFORM_POSIX)
        if(_winchSubscription >= 0) {
                SignalHandler::unsubscribe(_winchSubscription);
                _winchSubscription = -1;
        }
        if(_eventLoop != nullptr && _stdinSourceHandle >= 0) {
                _eventLoop->removeIoSource(_stdinSourceHandle);
                _stdinSourceHandle = -1;
        }
#endif
}

void TuiSubsystem::setFocusWidget(TuiWidget *widget) {
        if(_focusWidget == widget) return;
        if(_focusWidget) {
                _focusWidget->setFocused(false);
                Event e(Event::InvalidType);
                _focusWidget->focusOutEvent(&e);
                _focusWidget->update();
        }
        _focusWidget = widget;
        if(_focusWidget) {
                _focusWidget->setFocused(true);
                Event e(Event::InvalidType);
                _focusWidget->focusInEvent(&e);
                _focusWidget->update();
        }
        markNeedsRepaint();
}

void TuiSubsystem::focusNext(bool reverse) {
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

void TuiSubsystem::collectFocusable(TuiWidget *widget, List<TuiWidget *> &list) {
        if(!widget->isEffectivelyVisible() || !widget->isEnabled()) return;
        if(widget->focusPolicy() == StrongFocus || widget->focusPolicy() == TabFocus) {
                list += widget;
        }
        for(auto child : widget->childList()) {
                TuiWidget *tw = dynamic_cast<TuiWidget *>(child);
                if(tw) collectFocusable(tw, list);
        }
}

void TuiSubsystem::processInput() {
        char buf[256];
        auto [n, readErr] = _terminal.readInput(buf, sizeof(buf));
        if(readErr.isError() || n <= 0) return;

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

void TuiSubsystem::dispatchKeyEvent(const TuiInputParser::ParsedEvent &ev) {
        // Ctrl+Q quits the application
        if(ev.key == static_cast<KeyEvent::Key>('q') &&
           (ev.modifiers & KeyEvent::CtrlModifier)) {
                Application::quit(0);
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
                        target->sendEvent(&keyEv);
                        if(keyEv.isAccepted()) break;
                        target = dynamic_cast<TuiWidget *>(target->parent());
                }
                markNeedsRepaint();
        }
}

TuiWidget *TuiSubsystem::widgetAt(TuiWidget *widget, const Point2Di32 &globalPos) {
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

void TuiSubsystem::dispatchMouseEvent(const TuiInputParser::ParsedEvent &ev) {
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

        if(_mouseGrab) {
                _mouseGrab->mouseEvent(&mouseEv);
                markNeedsRepaint();
                return;
        }

        TuiWidget *target = widgetAt(_rootWidget, ev.mousePos);
        if(!target) return;

        if(action == MouseEvent::Press || action == MouseEvent::DoubleClick) {
                if(target->focusPolicy() == ClickFocus || target->focusPolicy() == StrongFocus) {
                        setFocusWidget(target);
                }
        }

        TuiWidget *t = target;
        while(t) {
                t->mouseEvent(&mouseEv);
                if(mouseEv.isAccepted()) break;
                t = dynamic_cast<TuiWidget *>(t->parent());
        }
        markNeedsRepaint();
}

void TuiSubsystem::handleResize() {
        int cols, rows;
        if(_terminal.windowSize(cols, rows).isError()) return;
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

void TuiSubsystem::paintWidgets() {
        if(!_rootWidget) return;
        _screen.clear();
        paintWidget(_rootWidget);
}

void TuiSubsystem::paintWidget(TuiWidget *widget) {
        if(!widget->isVisible()) return;

        Point2Di32 screenPos = widget->mapToGlobal(Point2Di32(0, 0));
        Rect2Di32 clipRect(screenPos.x(), screenPos.y(),
                        widget->width(), widget->height());

        TuiPainter painter(_screen, clipRect);
        PaintEvent ev;
        widget->paintEvent(&ev);

        for(auto child : widget->childList()) {
                TuiWidget *tw = dynamic_cast<TuiWidget *>(child);
                if(tw) paintWidget(tw);
        }
}

PROMEKI_NAMESPACE_END
