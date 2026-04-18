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

        private:
                static SdlSubsystem    *_instance;

                EventLoop              *_eventLoop = nullptr;
                SDLEventPump            _eventPump;

                // Self-pipe used as the SDL → EventLoop bridge.
                // SDL_AddEventWatch writes one byte to the write end
                // from whatever thread pushed the event; the read
                // end is registered as an EventLoop IoSource whose
                // callback drains the pipe and pumps SDL's queue.
                SelfPipe                _sdlPipe;
                int                     _sdlSourceHandle = -1;

                static bool sdlEventWatch(void *userdata, SDL_Event *event);
};

PROMEKI_NAMESPACE_END
