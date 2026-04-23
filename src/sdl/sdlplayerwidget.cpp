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
#include <promeki/logger.h>

#include <SDL3/SDL.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_DEBUG(SDLPlayerWidget)

uint32_t SDLPlayerWidget::userEventType() {
        static uint32_t type = SDL_RegisterEvents(1);
        return type;
}

SDLPlayerWidget::SDLPlayerWidget(SDLAudioOutput *audio,
                                 bool useAudioClock,
                                 ObjectBase *parent)
        : SDLVideoWidget(parent)
{
        setFocusPolicy(StrongFocus);
        _renderScheduled.setValue(false);

        // SDLPlayerTask's constructor is private; friendship lets this
        // TU construct it directly, but UniquePtr::create cannot reach it,
        // so wrap via takeOwnership from a raw allocation instead.
        auto task = SDLPlayerTask::UPtr::takeOwnership(
                new SDLPlayerTask(this, audio, useAudioClock));
        _mediaIO = MediaIO::UPtr::create(nullptr);
        SDLPlayerTask *taskRaw = task.ptr();
        Error err = _mediaIO->adoptTask(taskRaw);
        if(err.isError()) {
                promekiErr("SDLPlayerWidget: adoptTask failed: %s",
                           err.name().cstr());
                _task = nullptr;
                _mediaIO.clear();
                // task UniquePtr falls out of scope and deletes on failure.
        } else {
                // On success the MediaIO owns the task; disarm the local
                // UniquePtr and keep a non-owning observer pointer for
                // the widget's own use.
                (void)task.release();
                _task = taskRaw;
        }
}

SDLPlayerWidget::~SDLPlayerWidget() {
        // Releasing the MediaIO tears down the task (which joins its
        // pull thread) before our SDLVideoWidget base is destroyed,
        // so the task's last call into presentImage / renderPending
        // is safe.
        _mediaIO.clear();
        _task = nullptr;
}

void SDLPlayerWidget::togglePause() {
        if(_task == nullptr) return;
        _task->togglePause();
}

bool SDLPlayerWidget::isPaused() const {
        if(_task == nullptr) return false;
        return _task->isPaused();
}

void SDLPlayerWidget::keyPressEvent(KeyEvent *e) {
        // Space toggles pause; other keys left unaccepted so the SDL
        // subsystem's parent-chain walk propagates them upward.
        if(e->key() == KeyEvent::Key_Space
                        && !e->isCtrl() && !e->isAlt()
                        && !e->isMeta()) {
                togglePause();
                e->accept();
        }
}

void SDLPlayerWidget::presentImage(const Image::Ptr &image) {
        if(!image.isValid()) return;
        {
                Mutex::Locker lock(_pendingMutex);
                if(_pendingImage.isValid() && _task != nullptr) {
                        // Previous image hasn't been picked up — this
                        // replacement is a drop at the display stage.
                        // Bill it to the task's lifetime counter so
                        // MediaIO stats report it.
                        _task->noteFrameDropped();
                }
                _pendingImage = image;
        }
        if(!_renderScheduled.exchange(true)) {
                wakeMainThread();
        }
}

void SDLPlayerWidget::wakeMainThread() {
        SdlSubsystem *app = SdlSubsystem::instance();
        if(app != nullptr && app->eventLoop() != nullptr) {
                app->eventLoop()->postCallable([this]() {
                        renderPending();
                });
        }
        SDL_Event event = {};
        event.type = userEventType();
        SDL_PushEvent(&event);
}

bool SDLPlayerWidget::renderPending() {
        _renderScheduled.setValue(false);

        Image::Ptr img;
        {
                Mutex::Locker lock(_pendingMutex);
                if(!_pendingImage.isValid()) return false;
                img = _pendingImage;
                _pendingImage = Image::Ptr();
        }

        setImage(*img);

        // Trigger a paint on the containing SDLWindow if we have one.
        ObjectBase *p = parent();
        while(p != nullptr) {
                SDLWindow *win = dynamic_cast<SDLWindow *>(p);
                if(win != nullptr) {
                        win->paintAll();
                        break;
                }
                p = p->parent();
        }

        _framesPresented.fetchAndAdd(1);
        return true;
}

PROMEKI_NAMESPACE_END
