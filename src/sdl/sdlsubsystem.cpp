/**
 * @file      sdlsubsystem.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/sdl/sdlsubsystem.h>
#include <promeki/eventloop.h>
#include <promeki/logger.h>
#include <promeki/platform.h>
#include <promeki/util.h>

#include <SDL3/SDL.h>

PROMEKI_NAMESPACE_BEGIN

SdlSubsystem *SdlSubsystem::_instance = nullptr;

SdlSubsystem::SdlSubsystem()
        : _eventLoop(Application::mainEventLoop()) {
        // Singletons by design — constructing a second SdlSubsystem
        // while one is still live would silently clobber the instance
        // pointer and leak SDL state.  Catch it as a programming
        // error at construction.
        PROMEKI_ASSERT(_instance == nullptr);
        if(_eventLoop == nullptr) {
                promekiErr("SdlSubsystem: no Application / main EventLoop — "
                           "construct an Application before an SdlSubsystem");
        }
        _instance = this;
        if(!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_AUDIO)) {
                promekiErr("SdlSubsystem: SDL_Init failed: %s", SDL_GetError());
        }

        // SDL_AddEventWatch runs its callback on whatever thread
        // pushed the event; SelfPipe's write end tolerates
        // cross-thread and signal-safe use.  The read end is
        // registered as an EventLoop IoSource so poll() wakes
        // whenever SDL has events to drain.  Install the watcher
        // before registering the IoSource so we don't miss the very
        // first event slipping between the two calls.
        if(_sdlPipe.isValid() && _eventLoop != nullptr) {
                SDL_AddEventWatch(&SdlSubsystem::sdlEventWatch, this);

                _sdlSourceHandle = _eventLoop->addIoSource(
                        _sdlPipe.readFd(), EventLoop::IoRead,
                        [this](int, uint32_t) {
                                _sdlPipe.drain();
                                _eventPump.pumpEvents();
                        });
        }
        return;
}

SdlSubsystem::~SdlSubsystem() {
        if(_sdlSourceHandle >= 0 && _eventLoop != nullptr) {
                _eventLoop->removeIoSource(_sdlSourceHandle);
                _sdlSourceHandle = -1;
        }
        SDL_RemoveEventWatch(&SdlSubsystem::sdlEventWatch, this);
        SDL_Quit();
        if(_instance == this) _instance = nullptr;
        return;
}

bool SdlSubsystem::sdlEventWatch(void *userdata, SDL_Event *event) {
        (void)event;
        auto *self = static_cast<SdlSubsystem *>(userdata);
        if(self == nullptr) return true;
        self->_sdlPipe.wake();
        return true;
}

PROMEKI_NAMESPACE_END
