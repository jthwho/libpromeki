/**
 * @file      sdl/sdlplayerwidget.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/sdl/sdlvideowidget.h>
#include <promeki/mutex.h>
#include <promeki/atomic.h>
#include <promeki/image.h>

PROMEKI_NAMESPACE_BEGIN

class MediaIO;
class SDLPlayerTask;
class SDLAudioOutput;
class KeyEvent;

/**
 * @brief Widget that plays frames through an @ref SDLPlayerTask.
 * @ingroup sdl_core
 *
 * SDLPlayerWidget subclasses @ref SDLVideoWidget so it paints frames
 * directly, and owns the @ref MediaIO / @ref SDLPlayerTask pair that
 * consumes frames.  Clients retrieve the sink via @ref mediaIO and
 * write frames as they would with any MediaIO backend.
 *
 * @par Pause control
 *
 * The widget is the player's keyboard surface.  With
 * @ref FocusPolicy::StrongFocus set at construction, the SDL
 * subsystem routes key events here when the app gives it focus
 * (@c setFocus or explicit
 * @c SdlSubsystem::setFocusedWidget).  The space key toggles the
 * playback clock's pause state; other keys are left unaccepted so
 * the @ref SdlSubsystem walk propagates them up the parent chain.
 *
 * @par Ownership
 *
 * The widget owns the MediaIO, which owns the task.  The task holds
 * a back-pointer to this widget so its pull thread can call back
 * into @ref presentImage — safe because the destruction order
 * guarantees the MediaIO (and therefore the task + its threads) is
 * torn down before the widget's bases.
 *
 * @par Example
 * @code
 * SDLWindow        window("player", 1280, 720);
 * SDLAudioOutput   audio(&window);
 * SDLPlayerWidget  player(&audio, true, &window);
 * player.setFocus();
 * window.show();
 *
 * MediaIO *sink = player.mediaIO();
 * sink->open(MediaIO::Sink);
 * // ... write frames via sink->writeFrame(...) ...
 * @endcode
 */
class SDLPlayerWidget : public SDLVideoWidget {
        PROMEKI_OBJECT(SDLPlayerWidget, SDLVideoWidget)
        friend class SDLPlayerTask;
        public:
                /**
                 * @brief Constructs an SDLPlayerWidget.
                 *
                 * Creates the owned @ref SDLPlayerTask / @ref MediaIO
                 * pair and sets @ref FocusPolicy::StrongFocus on the
                 * widget so the app can @c setFocus on it to receive
                 * key events.
                 *
                 * @param audio         Audio output (may be nullptr).
                 * @param useAudioClock Prefer the audio device as the
                 *                      timing source (default true).
                 * @param parent        Optional parent widget / window.
                 */
                explicit SDLPlayerWidget(SDLAudioOutput *audio = nullptr,
                                         bool useAudioClock = true,
                                         ObjectBase *parent = nullptr);

                ~SDLPlayerWidget() override;

                SDLPlayerWidget(const SDLPlayerWidget &) = delete;
                SDLPlayerWidget &operator=(const SDLPlayerWidget &) = delete;

                /** @brief Returns the MediaIO the widget consumes frames through. */
                MediaIO *mediaIO() const { return _mediaIO; }

                /** @brief Returns the underlying task (may be useful for tests). */
                SDLPlayerTask *task() const { return _task; }

                /** @brief Total frames the widget has presented since construction. */
                int64_t framesPresented() const { return _framesPresented.value(); }

                /**
                 * @brief Toggles the playback clock's pause state.
                 *
                 * No-op when no MediaIO is currently open (i.e. no
                 * pause-capable clock exists yet).
                 */
                void togglePause();

                /** @brief Returns true when the playback clock is paused. */
                bool isPaused() const;

        protected:
                /**
                 * @brief Handles @c Key_Space by toggling pause.
                 *
                 * All other key codes are left unaccepted so the SDL
                 * subsystem's parent-chain walk propagates them
                 * upward.
                 */
                void keyPressEvent(KeyEvent *e) override;

        private:
                // Called by the owning @ref SDLPlayerTask on its pull
                // thread when a new frame is ready for display.
                // Stashes the image and wakes the main thread.
                void presentImage(const Image::Ptr &image);

                // Main-thread handler invoked in response to the wake
                // event — swaps the stashed image into the
                // SDLVideoWidget and triggers a paint on the containing
                // window.
                bool renderPending();

                void wakeMainThread();
                static uint32_t userEventType();

                MediaIO         *_mediaIO = nullptr;     ///< Owned.
                SDLPlayerTask   *_task = nullptr;        ///< Owned via @ref _mediaIO.

                // Main-thread render stash (written by the pull thread,
                // drained by the main thread).
                Image::Ptr      _pendingImage;
                mutable Mutex   _pendingMutex;
                Atomic<bool>    _renderScheduled;
                Atomic<int64_t> _framesPresented;
};

PROMEKI_NAMESPACE_END
