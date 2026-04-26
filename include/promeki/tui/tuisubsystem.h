/**
 * @file      tui/tuisubsystem.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <chrono>
#include <promeki/namespace.h>
#include <promeki/application.h>
#include <promeki/atomic.h>
#include <promeki/terminal.h>
#include <promeki/point.h>
#include <promeki/eventloop.h>
#include <promeki/tui/screen.h>
#include <promeki/tui/palette.h>
#include <promeki/tui/inputparser.h>

PROMEKI_NAMESPACE_BEGIN

class TuiWidget;

/**
 * @brief TUI subsystem installed alongside an @ref Application.
 * @ingroup tui_core
 *
 * Owns the @ref Terminal, @ref TuiScreen, palette, input parser, and
 * the event-loop plumbing (STDIN + SIGWINCH self-pipe as
 * @ref EventLoop::IoSource callbacks) that drive a text UI.  Stack-
 * construct one after @c Application:
 *
 * @code
 * int main(int argc, char **argv) {
 *         Application app(argc, argv);
 *         TuiSubsystem  tui;
 *         tui.setRootWidget(myRoot);
 *         return app.exec();
 * }
 * @endcode
 *
 * The subsystem resolves its @ref EventLoop from the current
 * @ref Application via @c Application::mainEventLoop() — no
 * constructor argument is needed.  The constructor puts the terminal
 * into raw mode, enables the alternate screen, and installs signal
 * handlers and event sources; the destructor reverses all of that.
 *
 * @par Thread Safety
 * Thread-affine.  @ref TuiSubsystem must be constructed, used, and
 * destroyed on the thread that owns the bound @ref EventLoop (typically
 * the main thread).  All TUI widgets and their input/paint callbacks are
 * dispatched on the same thread.  Cross-thread interaction is supported
 * only through @ref EventLoop::postCallable / @ref ObjectBase signal/slot
 * dispatch.
 */
class TuiSubsystem {
        public:
                /**
                 * @brief Installs the TUI on the current @ref Application.
                 *
                 * Requires an @ref Application to have been constructed
                 * before this object.  Puts the terminal in raw mode
                 * and registers STDIN / SIGWINCH as EventLoop I/O
                 * sources.
                 */
                TuiSubsystem();

                /** @brief Destructor. Restores terminal state and releases I/O sources. */
                ~TuiSubsystem();

                TuiSubsystem(const TuiSubsystem &) = delete;
                TuiSubsystem &operator=(const TuiSubsystem &) = delete;

                /**
                 * @brief Returns the active TuiSubsystem instance.
                 */
                static TuiSubsystem *instance() { return _instance; }

                /**
                 * @brief Sets the top-level (root) widget.
                 * @param widget The root widget to display.
                 */
                void setRootWidget(TuiWidget *widget);

                /** @brief Returns the root widget. */
                TuiWidget *rootWidget() const { return _rootWidget; }

                /** @brief Returns the screen. */
                TuiScreen &screen() { return _screen; }

                /** @brief Returns the terminal. */
                Terminal &terminal() { return _terminal; }

                /** @brief Returns the palette. */
                const TuiPalette &palette() const { return _palette; }

                /** @brief Returns the palette for modification. */
                TuiPalette &palette() { return _palette; }

                /** @brief Sets the palette. */
                void setPalette(const TuiPalette &palette) { _palette = palette; }

                /**
                 * @brief Sets the color mode for the TUI screen.
                 *
                 * Changes how RGB colors are converted to ANSI output.
                 * The screen will do its best to gracefully degrade colors
                 * to the requested mode, but for optimal appearance,
                 * provide a TuiPalette whose colors suit the target mode.
                 *
                 * @param mode The color support level to use.
                 * @see TuiPalette
                 */
                void setColorMode(Terminal::ColorSupport mode) {
                        _screen.setColorMode(mode);
                        updateAll();
                }

                /**
                 * @brief Returns the current color mode.
                 * @return The color support level in use.
                 */
                Terminal::ColorSupport colorMode() const { return _screen.colorMode(); }

                /**
                 * @brief Forces a full screen repaint.
                 */
                void updateAll();

                /**
                 * @brief Sets the focused widget.
                 * @param widget The widget to focus.
                 */
                void setFocusWidget(TuiWidget *widget);

                /** @brief Returns the currently focused widget. */
                TuiWidget *focusWidget() const { return _focusWidget; }

                /**
                 * @brief Cycles focus to the next focusable widget.
                 * @param reverse If true, cycles in reverse order.
                 */
                void focusNext(bool reverse = false);

                /**
                 * @brief Marks the screen as needing a repaint.
                 *
                 * Thread-safe.  At most one repaint is pending at any
                 * time — repeated calls before the pending paint fires
                 * are coalesced, so widgets can call this freely in
                 * response to state changes without flooding the
                 * event loop.
                 */
                void markNeedsRepaint();

                /**
                 * @brief Grabs mouse events for a widget.
                 *
                 * While grabbed, all mouse events are sent to the grabbing
                 * widget regardless of cursor position.
                 */
                void grabMouse(TuiWidget *widget) { _mouseGrab = widget; }

                /**
                 * @brief Releases the mouse grab.
                 */
                void releaseMouse() { _mouseGrab = nullptr; }

        private:
                static TuiSubsystem    *_instance;

                EventLoop              *_eventLoop = nullptr;
                Terminal                _terminal;
                TuiScreen               _screen;
                TuiPalette              _palette;
                TuiInputParser          _inputParser;
                AnsiStream              _ansiStream;
                TuiWidget               *_rootWidget = nullptr;
                TuiWidget               *_focusWidget = nullptr;
                TuiWidget               *_mouseGrab = nullptr;
                int                     _lastCols = 0;
                int                     _lastRows = 0;

                // Event-loop-driven input / resize / repaint state.
                // - _stdinSourceHandle: IoSource handle for STDIN_FILENO.
                // - _winchSubscription: SignalHandler subscription handle
                //   for SIGWINCH.  The SignalHandler watcher thread
                //   invokes the subscriber callback, which in turn
                //   posts handleResize() onto the main EventLoop.
                // - _repaintQueued: coalesces markNeedsRepaint() calls
                //   into a single pending postCallable — if a repaint
                //   is already queued we skip reposting.
                int                     _stdinSourceHandle = -1;
                int                     _winchSubscription = -1;
                Atomic<bool>            _repaintQueued;

                // Double-click detection state
                using Clock = std::chrono::steady_clock;
                using TimePoint = Clock::time_point;
                static constexpr int    DoubleClickIntervalMs = 400;
                TimePoint               _lastClickTime{};
                Point2Di32              _lastClickPos{-1, -1};
                MouseEvent::Button      _lastClickButton = MouseEvent::NoButton;

                void setupEventSources();
                void teardownEventSources();
                void doPaint();
                void processInput();
                void paintWidgets();
                void paintWidget(TuiWidget *widget);
                void handleResize();
                void dispatchKeyEvent(const TuiInputParser::ParsedEvent &ev);
                void dispatchMouseEvent(const TuiInputParser::ParsedEvent &ev);
                void collectFocusable(TuiWidget *widget, List<TuiWidget *> &list);
                TuiWidget *widgetAt(TuiWidget *widget, const Point2Di32 &globalPos);
};

PROMEKI_NAMESPACE_END
