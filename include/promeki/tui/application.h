/**
 * @file      tui/application.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <chrono>
#include <promeki/core/namespace.h>
#include <promeki/core/application.h>
#include <promeki/core/terminal.h>
#include <promeki/core/point.h>
#include <promeki/core/eventloop.h>
#include <promeki/tui/screen.h>
#include <promeki/tui/palette.h>
#include <promeki/tui/inputparser.h>

PROMEKI_NAMESPACE_BEGIN

class TuiWidget;

/**
 * @brief Application class for TUI programs.
 * @ingroup tui_core
 *
 * Derives from Application and manages the Terminal, TuiScreen, and
 * input parsing.  Provides the main event loop integration for TUI
 * applications: reads raw input, parses escape sequences, dispatches
 * events to widgets, and flushes the screen.
 */
class TuiApplication : public Application {
        public:
                /**
                 * @brief Constructs a TuiApplication.
                 * @param argc Argument count from main().
                 * @param argv Argument vector from main().
                 */
                TuiApplication(int argc, char **argv);

                /** @brief Destructor. Restores terminal state. */
                ~TuiApplication();

                TuiApplication(const TuiApplication &) = delete;
                TuiApplication &operator=(const TuiApplication &) = delete;

                /**
                 * @brief Returns the TuiApplication instance.
                 */
                static TuiApplication *instance() { return _instance; }

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
                 * @brief Runs the TUI application event loop.
                 * @return The exit code.
                 */
                int exec();

                /**
                 * @brief Requests the application to quit.
                 * @param exitCode The exit code.
                 */
                void quit(int exitCode = 0);

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
                 * Called internally when widget state changes.  The screen
                 * is only repainted when this flag is set, avoiding
                 * unnecessary work and CPU usage.
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
                static TuiApplication  *_instance;

                EventLoop               _eventLoop;
                Terminal                _terminal;
                TuiScreen               _screen;
                TuiPalette              _palette;
                TuiInputParser          _inputParser;
                AnsiStream              _ansiStream;
                TuiWidget               *_rootWidget = nullptr;
                TuiWidget               *_focusWidget = nullptr;
                TuiWidget               *_mouseGrab = nullptr;
                int                     _exitCode = 0;
                bool                    _running = false;
                bool                    _needsRepaint = true;
                int                     _lastCols = 0;
                int                     _lastRows = 0;

                // Double-click detection state
                using Clock = std::chrono::steady_clock;
                using TimePoint = Clock::time_point;
                static constexpr int    DoubleClickIntervalMs = 400;
                TimePoint               _lastClickTime{};
                Point2Di32                 _lastClickPos{-1, -1};
                MouseEvent::Button      _lastClickButton = MouseEvent::NoButton;

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
