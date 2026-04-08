/**
 * @file      sdlapplication.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/sdl/sdlapplication.h>
#include <promeki/logger.h>

#include <SDL3/SDL.h>

#if defined(PROMEKI_PLATFORM_EMSCRIPTEN)
#include <emscripten.h>
#endif

PROMEKI_NAMESPACE_BEGIN

SDLApplication *SDLApplication::_instance = nullptr;

SDLApplication::SDLApplication(int argc, char **argv) : Application(argc, argv) {
        _instance = this;
        if(!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_AUDIO)) {
                promekiErr("SDLApplication: SDL_Init failed: %s", SDL_GetError());
        }
        // Wire the promeki EventLoop's wake callback so that any
        // cross-thread post (signals, events, quit) pushed onto the
        // main loop automatically wakes SDL_WaitEvent().  This is
        // what lets signal-driven pipelines work without any caller
        // knowledge of the SDL event pump.
        _eventLoop.setWakeCallback([]() { SDLApplication::wakeUp(); });
        return;
}

SDLApplication::~SDLApplication() {
        SDL_Quit();
        if(_instance == this) _instance = nullptr;
        return;
}

int SDLApplication::exec() {
#if defined(PROMEKI_PLATFORM_EMSCRIPTEN)
        emscripten_set_main_loop_arg(emscriptenMainLoop, this, 0, 1);
#else
        while(!shouldQuit()) {
                processFrame();
        }
#endif

        return exitCode();
}

void SDLApplication::quit(int ec) {
        Application::quit(ec);
#if defined(PROMEKI_PLATFORM_EMSCRIPTEN)
        emscripten_cancel_main_loop();
#else
        // Wake the SDL_WaitEvent() blocked in processFrame() so the
        // exec() loop can observe shouldQuit() and return.  Without
        // this, a quit() call from a worker thread in a headless
        // configuration (no window, no player sink) would hang the
        // main loop forever because nothing else posts SDL events.
        wakeUp();
#endif
        return;
}

void SDLApplication::wakeUp() {
        // Safe to call from any thread and before/after SDL is
        // initialized — SDL_PushEvent returns an error when the
        // event subsystem isn't up, which we intentionally ignore.
        if(_instance == nullptr) return;
        SDL_Event event = {};
        event.type = SDL_EVENT_USER;
        SDL_PushEvent(&event);
        return;
}

void SDLApplication::processFrame() {
        // Block until an SDL event arrives (keyboard, mouse, window,
        // custom user event from worker threads, etc).  This avoids
        // busy-waiting on the main thread.
        _eventPump.pumpEvents(true);
        _eventLoop.processEvents();
        return;
}

#if defined(PROMEKI_PLATFORM_EMSCRIPTEN)
void SDLApplication::emscriptenMainLoop(void *arg) {
        auto *app = static_cast<SDLApplication *>(arg);
        // Emscripten: non-blocking pump since the browser owns the loop
        app->_eventPump.pumpEvents(false);
        app->_eventLoop.processEvents();
        return;
}
#endif

PROMEKI_NAMESPACE_END
