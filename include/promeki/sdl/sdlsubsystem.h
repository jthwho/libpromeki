/**
 * @file      sdl/sdlsubsystem.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/application.h>
#include <promeki/eventloop.h>
#include <promeki/selfpipe.h>
#include <promeki/sdl/sdleventpump.h>

PROMEKI_NAMESPACE_BEGIN

class Widget;

/**
 * @brief SDL subsystem installed alongside an @ref Application.
 * @ingroup sdl_core
 *
 * Owns the SDL library state (init / quit), the @ref SDLEventPump,
 * and the SDL-to-EventLoop bridge (a self-pipe fed by
 * @c SDL_AddEventWatch and registered as an
 * @ref EventLoop::IoSource).  Stack-construct one after
 * @c Application:
 *
 * @code
 * int main(int argc, char **argv) {
 *         Application  app(argc, argv);
 *         SdlSubsystem sdl;
 *         SDLWindow    window("demo", 1280, 720);
 *         window.show();
 *         return app.exec();
 * }
 * @endcode
 *
 * Multiple subsystems can coexist in the same process — e.g. a
 * program that wants both a graphical SDL window and a text-mode
 * TUI can construct a @ref TuiSubsystem alongside this one and
 * both will install their I/O sources on the same
 * @c Application main @ref EventLoop.
 */
class SdlSubsystem {
        public:
                /**
                 * @brief Initialises SDL and installs the event-loop bridge.
                 *
                 * Requires an @ref Application to have been constructed
                 * before this object.  Calls @c SDL_Init with video /
                 * audio / event subsystems, creates the self-pipe
                 * used to wake the main @ref EventLoop on SDL events,
                 * and registers the watcher + I/O source.
                 */
                SdlSubsystem();

                /** @brief Destructor. Removes I/O source, closes pipe, calls SDL_Quit. */
                ~SdlSubsystem();

                SdlSubsystem(const SdlSubsystem &) = delete;
                SdlSubsystem &operator=(const SdlSubsystem &) = delete;

                /**
                 * @brief Returns the active SdlSubsystem instance.
                 * @return The singleton instance, or @c nullptr.
                 */
                static SdlSubsystem *instance() { return _instance; }

                /**
                 * @brief Returns the subsystem's @ref SDLEventPump.
                 *
                 * SDLWindow uses this to register itself for event
                 * routing.
                 */
                SDLEventPump &eventPump() { return _eventPump; }

                /**
                 * @brief Returns the main @ref EventLoop the subsystem is bound to.
                 *
                 * Convenience accessor used by SDL-side code that
                 * wants to post a callable onto the main loop
                 * (e.g. SDL player sinks marshalling a resize to
                 * the main thread).  Same as
                 * @c Application::mainEventLoop().
                 */
                EventLoop *eventLoop() { return _eventLoop; }

                /**
                 * @brief Returns the widget that currently receives
                 *        keyboard input, or @c nullptr if none is set.
                 */
                Widget *focusedWidget() const { return _focusedWidget; }

                /**
                 * @brief Sets the widget that receives keyboard input.
                 *
                 * Key events arriving from SDL are delivered to @p widget
                 * first; if the widget does not @c accept the event,
                 * delivery walks up the parent chain until a handler
                 * accepts or the chain ends.
                 *
                 * Passing @c nullptr clears focus — key events with no
                 * focused widget fall back to the receiving window.
                 *
                 * Managing @ref Widget::setFocused on the old / new
                 * widget is the caller's responsibility.
                 *
                 * @param widget The new focused widget, or nullptr.
                 */
                void setFocusedWidget(Widget *widget) { _focusedWidget = widget; }

        private:
                static SdlSubsystem    *_instance;

                EventLoop              *_eventLoop = nullptr;
                SDLEventPump            _eventPump;
                Widget                 *_focusedWidget = nullptr;

                // Self-pipe used as the SDL → EventLoop bridge.
                // SDL_AddEventWatch writes one byte to the write end
                // from whatever thread pushed the event; the read
                // end is registered as an EventLoop IoSource whose
                // callback drains the pipe and pumps SDL's queue.
                SelfPipe                _sdlPipe;
                int                     _sdlSourceHandle = -1;

                // Periodic kick that calls @c SDL_PumpEvents so OS
                // input (keyboard, window close, mouse) gets pulled
                // into SDL's queue even when nothing on the promeki
                // side is pushing events.  Without this, a paused
                // playback pipeline (no frames, no wakeMainThread
                // user events) leaves OS events stranded: the
                // @c sdlEventWatch callback only fires when
                // @c SDL_PushEvent or a pump pulls a new event
                // in, so the watch → pipe → IoSource → pump chain
                // deadlocks without an external driver.
                int                     _pumpTimerId = -1;

                static bool sdlEventWatch(void *userdata, SDL_Event *event);
};

PROMEKI_NAMESPACE_END
