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

class SDLVideoWidget;
class SDLAudioOutput;
class Clock;

/**
 * @brief MediaIOTask writer that plays frames through SDL via FrameSync.
 * @ingroup sdl_core
 *
 * SDLPlayerTask is a write-only MediaIO sink that consumes @c Frame
 * objects via @c MediaIO::writeFrame() and displays their video
 * through an application-provided @c SDLVideoWidget and (optionally)
 * plays their audio through an application-provided
 * @c SDLAudioOutput.
 *
 * @par Architecture
 *
 * - The strand worker does not pace per frame.  @c executeCmd(Write)
 *   decodes compressed images and hands the frame to a @ref FrameSync
 *   instance, then returns.
 * - A dedicated pull thread drives @ref FrameSync::pullFrame in a
 *   loop.  The clock (SDL audio or wall) blocks the pull thread
 *   until each destination deadline; the output frame is then
 *   routed to the video widget (main-thread render) and the audio
 *   output.
 * - Back-pressure comes from FrameSync's input queue running in
 *   @ref FrameSync::InputOverflowPolicy::Block mode: if the
 *   producer outruns the sink, @c pushFrame blocks until the pull
 *   thread makes room.
 * - Audio drift correction is real.  The audio clock reports a
 *   live @ref Clock::rateRatio and FrameSync resamples audio to
 *   match.
 *
 * The legacy FramePacer-based implementation is retained as
 * @c SDLPlayerOldTask in @c sdlplayerold.h for comparison.
 *
 * @par Creation
 *
 * Use @ref createSDLPlayer to build a ready-to-use MediaIO.
 *
 * @par Teardown ordering
 *
 * Close the MediaIO and let the main event loop drain before
 * destroying the owning MediaIO — the main-thread render callable
 * captures a pointer to the task.
 */
class SDLPlayerTask : public MediaIOTask {
        public:
                /**
                 * @brief Constructs a FrameSync-based player task.
                 *
                 * @param video         Video widget (may be nullptr).
                 * @param audio         Audio output (may be nullptr).
                 * @param useAudioClock Prefer the audio device as the
                 *                      timing source (default true).
                 */
                SDLPlayerTask(SDLVideoWidget *video, SDLAudioOutput *audio,
                               bool useAudioClock = true);

                ~SDLPlayerTask() override;

                /** @brief Returns the configured video widget. */
                SDLVideoWidget *videoWidget() const { return _videoWidget; }

                /** @brief Returns the configured audio output. */
                SDLAudioOutput *audioOutput() const { return _audioOutput; }

                /** @brief True when the audio device drives timing. */
                bool useAudioClock() const { return _useAudioClock; }

                /** @brief Total frames presented to the video widget. */
                int64_t framesPresented() const { return _framesPresented.value(); }

                /**
                 * @brief Paints the currently stashed image on the main
                 *        thread.  Callable in response to a wake event
                 *        posted by the pull thread.
                 */
                bool renderPending();

        private:
                Error executeCmd(MediaIOCommandOpen &cmd) override;
                Error executeCmd(MediaIOCommandClose &cmd) override;
                Error executeCmd(MediaIOCommandWrite &cmd) override;

                Error describe(MediaIODescription *out) const override;
                Error proposeInput(const MediaDesc &offered,
                                   MediaDesc *preferred) const override;

                // Picks the best SDL-native PixelDesc for an offered
                // image — the format closest to @p offered that
                // SDLVideoWidget::mapPixelDesc returns a non-zero
                // SDL pixel format for.  This is what the planner
                // uses to know "given source X, which native format
                // should the upstream CSC convert to?"  Returns
                // @ref PixelDesc::RGBA8_sRGB as the universal
                // fallback when no closer match is available.
                PixelDesc pickNativePixelDesc(const PixelDesc &offered) const;

                void pullLoop();
                void wakeMainThread();

                static uint32_t userEventType();

                // Application-provided outputs (not owned).
                SDLVideoWidget *_videoWidget = nullptr;
                SDLAudioOutput *_audioOutput = nullptr;

                bool            _useAudioClock = true;

                // Per-open state.
                bool            _audioConfigured = false;
                FrameRate       _frameRate;
                AudioDesc       _audioDesc;
                Clock          *_clock = nullptr;
                SDLAudioClock  *_audioClock = nullptr;
                FrameSync       _sync;

                // Pull-thread state.
                std::thread     _pullThread;
                Atomic<bool>    _pullRunning;

                // Main-thread render stash.
                Image::Ptr      _pendingImage;
                mutable Mutex   _pendingMutex;
                Atomic<bool>    _renderScheduled;

                // Stats.
                Atomic<int64_t> _framesPresented;
};

/**
 * @brief Constructs a FrameSync-based SDL player MediaIO.
 * @ingroup sdl_core
 *
 * Creates an SDLPlayerTask with the supplied widget/audio pointers
 * and adopts it into a newly allocated MediaIO.  The returned MediaIO
 * owns the task; @p video and @p audio remain caller-owned and must
 * outlive the returned MediaIO.
 *
 * @param video         Video widget (may be nullptr).
 * @param audio         Audio output (may be nullptr).
 * @param useAudioClock Prefer the audio device as the timing source.
 * @param parent        Optional parent object.
 * @return A new MediaIO ready to be configured and opened as a writer.
 */
MediaIO *createSDLPlayer(SDLVideoWidget *video,
                          SDLAudioOutput *audio,
                          bool useAudioClock = true,
                          ObjectBase *parent = nullptr);

PROMEKI_NAMESPACE_END
