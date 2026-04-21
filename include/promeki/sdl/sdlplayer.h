/**
 * @file      sdl/sdlplayer.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/mediaiotask.h>
#include <promeki/mediaio.h>
#include <promeki/mutex.h>
#include <promeki/atomic.h>
#include <promeki/framerate.h>
#include <promeki/audiodesc.h>
#include <promeki/image.h>
#include <promeki/framesync.h>
#include <promeki/sdl/sdlaudioclock.h>

#include <thread>

PROMEKI_NAMESPACE_BEGIN

class SDLPlayerWidget;
class SDLAudioOutput;
class Clock;

/**
 * @brief MediaIOTask writer that plays frames through SDL via FrameSync.
 * @ingroup sdl_core
 *
 * SDLPlayerTask is a write-only MediaIO sink that consumes @c Frame
 * objects via @c MediaIO::writeFrame().  It is always owned by an
 * @ref SDLPlayerWidget which provides both the render target and the
 * user-facing controls (play/pause, focus routing, etc.) — clients
 * should construct an @ref SDLPlayerWidget rather than instantiating
 * the task directly.
 *
 * @par Architecture
 *
 * - The strand worker does not pace per frame.  @c executeCmd(Write)
 *   decodes compressed images and hands the frame to a @ref FrameSync
 *   instance, then returns.
 * - A dedicated pull thread drives @ref FrameSync::pullFrame in a
 *   loop.  The clock (SDL audio or wall) blocks the pull thread
 *   until each destination deadline; the output frame is then
 *   delivered to the @ref SDLPlayerWidget for display and, if
 *   configured, to the @ref SDLAudioOutput.
 * - Back-pressure comes from FrameSync's input queue running in
 *   @ref FrameSync::InputOverflowPolicy::Block mode.
 * - Audio drift correction is real — the audio clock's rate ratio
 *   feeds FrameSync's audio resampler.
 */
class SDLPlayerTask : public MediaIOTask {
        friend class SDLPlayerWidget;
        public:
                ~SDLPlayerTask() override;

                /** @brief Returns the widget that owns this task. */
                SDLPlayerWidget *widget() const { return _widget; }

                /** @brief Returns the configured audio output. */
                SDLAudioOutput *audioOutput() const { return _audioOutput; }

                /** @brief True when the audio device drives timing. */
                bool useAudioClock() const { return _useAudioClock; }

                /**
                 * @brief Pauses playback.
                 *
                 * Pauses the playback clock (which stops SDL audio
                 * consumption via @ref SDLAudioClock::onPause) and
                 * tears down the pull thread so it isn't blocked on
                 * the clock.  Safe to call from any thread; no-ops
                 * when no MediaIO is open or playback is already
                 * paused.
                 */
                void pause();

                /**
                 * @brief Resumes playback after a @ref pause.
                 *
                 * Unpauses the clock and spawns a fresh pull thread.
                 * The base @ref Clock's paused-offset accumulator
                 * rolls the deadline forward so the next output frame
                 * lands where it would have without the pause.
                 * No-op when no MediaIO is open or playback is not
                 * paused.
                 */
                void resume();

                /** @brief Convenience: calls @ref pause or @ref resume. */
                void togglePause();

                /** @brief Returns true when the playback clock is paused. */
                bool isPaused() const;

        private:
                /**
                 * @brief Constructs a FrameSync-based player task.
                 *
                 * Private — only @ref SDLPlayerWidget constructs tasks.
                 *
                 * @param widget        Owning player widget (required).
                 * @param audio         Audio output (may be nullptr).
                 * @param useAudioClock Prefer the audio device as the
                 *                      timing source (default true).
                 */
                SDLPlayerTask(SDLPlayerWidget *widget,
                              SDLAudioOutput *audio,
                              bool useAudioClock);

                Error executeCmd(MediaIOCommandOpen &cmd) override;
                Error executeCmd(MediaIOCommandClose &cmd) override;
                Error executeCmd(MediaIOCommandWrite &cmd) override;

                Error describe(MediaIODescription *out) const override;
                Error proposeInput(const MediaDesc &offered,
                                   MediaDesc *preferred) const override;

                // MediaIO::close(false) (the watchdog's forced-close
                // path) calls this from a thread that's NOT the
                // strand — we're typically deadlocked with the
                // strand blocked inside FrameSync::pushFrame on
                // queue backpressure while the submitted CmdClose
                // sits behind it in the strand queue.  Interrupting
                // FrameSync unblocks the pending pushFrame so the
                // strand can drain to CmdClose.
                void cancelBlockingWork() override;

                PixelDesc pickNativePixelDesc(const PixelDesc &offered) const;

                // Spawns the pull thread.  Caller must hold
                // @ref _clockMutex and guarantee no thread is already
                // running (i.e. @ref _pullThread is not joinable).
                void startPullThread();

                // Sets the running flag false, interrupts FrameSync,
                // and joins the pull thread if any.  Caller must hold
                // @ref _clockMutex.
                void stopPullThread();

                void pullLoop();

                // Owner back-pointer.  The widget outlives the task
                // (widget owns the MediaIO which owns us) so this
                // pointer is always valid for the task's lifetime.
                SDLPlayerWidget *_widget = nullptr;
                SDLAudioOutput  *_audioOutput = nullptr;

                bool            _useAudioClock = true;

                // Per-open state.
                bool            _audioConfigured = false;
                FrameRate       _frameRate;
                AudioDesc       _audioDesc;
                Clock::Ptr      _clock;
                mutable Mutex   _clockMutex;    ///< Guards @ref _clock.
                FrameSync       _sync;

                // Pull-thread state.
                std::thread     _pullThread;
                Atomic<bool>    _pullRunning;
};

PROMEKI_NAMESPACE_END
