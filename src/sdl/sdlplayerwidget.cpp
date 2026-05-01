/**
 * @file      sdlplayerwidget.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/sdl/sdlplayerwidget.h>
#include <promeki/sdl/sdlplayer.h>
#include <promeki/sdl/sdlsubsystem.h>
#include <promeki/sdl/sdlwindow.h>
#include <promeki/keyevent.h>
#include <promeki/mediaio.h>
#include <promeki/mediaioportgroup.h>
#include <promeki/mediaiorequest.h>
#include <promeki/logger.h>

#include <SDL3/SDL.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_DEBUG(SDLPlayerWidget)

uint32_t SDLPlayerWidget::userEventType() {
        static uint32_t type = SDL_RegisterEvents(1);
        return type;
}

SDLPlayerWidget::SDLPlayerWidget(SDLAudioOutput *audio, bool useAudioClock, ObjectBase *parent)
    : SDLVideoWidget(parent) {
        setFocusPolicy(StrongFocus);
        _renderScheduled.setValue(false);

        // SDLPlayerMediaIO's constructor is private; friendship lets
        // this TU construct it directly with the SDL handles —
        // SDLPlayerMediaIO IS the MediaIO, so there's no separate
        // wrapper to wire up.
        SDLPlayerMediaIO *raw = new SDLPlayerMediaIO(this, audio, useAudioClock);
        _mediaIO = UniquePtr<SDLPlayerMediaIO>::takeOwnership(raw);
        _task = raw;
}

SDLPlayerWidget::~SDLPlayerWidget() {
        // Releasing the MediaIO tears down the task (which joins its
        // pull thread) before our SDLVideoWidget base is destroyed,
        // so the task's last call into presentVideo / renderPending
        // is safe.
        _mediaIO.clear();
        _task = nullptr;
}

void SDLPlayerWidget::togglePause() {
        if (_task == nullptr) return;
        _task->togglePause();
}

bool SDLPlayerWidget::isPaused() const {
        if (_task == nullptr) return false;
        return _task->isPaused();
}

void SDLPlayerWidget::keyPressEvent(KeyEvent *e) {
        // Space toggles pause; other keys left unaccepted so the SDL
        // subsystem's parent-chain walk propagates them upward.
        if (e->key() == KeyEvent::Key_Space && !e->isCtrl() && !e->isAlt() && !e->isMeta()) {
                togglePause();
                e->accept();
        }
}

void SDLPlayerWidget::presentVideo(const UncompressedVideoPayload::Ptr &payload) {
        if (!payload.isValid()) return;
        {
                Mutex::Locker lock(_pendingMutex);
                if (_pendingPayload.isValid() && _task != nullptr) {
                        // Previous payload hasn't been picked up —
                        // this replacement is a drop at the display
                        // stage.  Bill it to the backend's first port
                        // group so MediaIO stats report it.
                        MediaIOPortGroup *grp = _task->portGroup(0);
                        if (grp != nullptr) _task->noteFrameDropped(grp);
                }
                _pendingPayload = payload;
        }
        if (!_renderScheduled.exchange(true)) {
                wakeMainThread();
        }
}

void SDLPlayerWidget::wakeMainThread() {
        SdlSubsystem *app = SdlSubsystem::instance();
        if (app != nullptr && app->eventLoop() != nullptr) {
                app->eventLoop()->postCallable([this]() { renderPending(); });
        }
        SDL_Event event = {};
        event.type = userEventType();
        SDL_PushEvent(&event);
}

bool SDLPlayerWidget::renderPending() {
        _renderScheduled.setValue(false);

        UncompressedVideoPayload::Ptr payload;
        {
                Mutex::Locker lock(_pendingMutex);
                if (!_pendingPayload.isValid()) return false;
                payload = _pendingPayload;
                _pendingPayload = UncompressedVideoPayload::Ptr();
        }

        // Forward directly to the SDLVideoWidget's payload-native
        // upload path — zero-copy, planes shared with the source.
        setPayload(payload);

        // Trigger a paint on the containing SDLWindow if we have one.
        ObjectBase *p = parent();
        while (p != nullptr) {
                SDLWindow *win = dynamic_cast<SDLWindow *>(p);
                if (win != nullptr) {
                        win->paintAll();
                        break;
                }
                p = p->parent();
        }

        _framesPresented.fetchAndAdd(1);
        return true;
}

PROMEKI_NAMESPACE_END
