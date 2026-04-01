/**
 * @file      sdl/sdlapplication.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/core/namespace.h>
#include <promeki/core/application.h>
#include <promeki/core/eventloop.h>
#include <promeki/sdl/sdleventpump.h>

PROMEKI_NAMESPACE_BEGIN

class SDLWindow;

/**
 * @brief Application class for SDL-based programs.
 * @ingroup sdl_core
 *
 * Derives from Application and manages SDL initialization, the main
 * EventLoop, and the SDLEventPump.  Provides the main event loop
 * integration for SDL applications: polls SDL events, translates
 * them into promeki events, and dispatches them to windows.
 *
 * On Emscripten, exec() uses emscripten_set_main_loop_arg() instead
 * of a blocking while-loop.
 *
 * @par Example
 * @code
 * int main(int argc, char **argv) {
 *         SDLApplication app(argc, argv);
 *         SDLWindow window("My Window", 1280, 720);
 *         window.show();
 *         return app.exec();
 * }
 * @endcode
 */
class SDLApplication : public Application {
        public:
                /**
                 * @brief Constructs an SDLApplication.
                 * @param argc Argument count from main().
                 * @param argv Argument vector from main().
                 *
                 * Initializes SDL subsystems (video and events).
                 */
                SDLApplication(int argc, char **argv);

                /** @brief Destructor. Shuts down SDL. */
                ~SDLApplication();

                SDLApplication(const SDLApplication &) = delete;
                SDLApplication &operator=(const SDLApplication &) = delete;

                /**
                 * @brief Returns the SDLApplication instance.
                 * @return The singleton instance, or nullptr.
                 */
                static SDLApplication *instance() { return _instance; }

                /**
                 * @brief Returns the application's EventLoop.
                 * @return Reference to the EventLoop.
                 */
                EventLoop &eventLoop() { return _eventLoop; }

                /**
                 * @brief Returns the application's SDLEventPump.
                 * @return Reference to the SDLEventPump.
                 */
                SDLEventPump &eventPump() { return _eventPump; }

                /**
                 * @brief Runs the SDL application event loop.
                 *
                 * Blocks until quit() is called.  On Emscripten, uses
                 * emscripten_set_main_loop_arg() for cooperative scheduling.
                 *
                 * @return The exit code.
                 */
                int exec();

                /**
                 * @brief Requests the application to quit.
                 * @param exitCode The exit code.
                 */
                void quit(int exitCode = 0);

        private:
                static SDLApplication  *_instance;

                EventLoop               _eventLoop;
                SDLEventPump            _eventPump;

                void processFrame();

#if defined(PROMEKI_PLATFORM_EMSCRIPTEN)
                static void emscriptenMainLoop(void *arg);
#endif
};

PROMEKI_NAMESPACE_END
