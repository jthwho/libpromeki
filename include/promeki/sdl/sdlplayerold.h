/**
 * @file      sdl/sdlplayerold.h
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
#include <promeki/framepacer.h>
#include <promeki/framerate.h>
#include <promeki/audiodesc.h>
#include <promeki/image.h>
#include <promeki/objectbase.h>
#include <promeki/periodiccallback.h>
#include <promeki/sdl/sdlaudioclock.h>

PROMEKI_NAMESPACE_BEGIN

class SDLVideoWidget;
class SDLAudioOutput;

/**
 * @brief MediaIOTask writer that plays frames through SDL (legacy path).
 * @ingroup sdl_core
 * @deprecated Use @ref SDLPlayerTask / @ref createSDLPlayer instead.
 *             SDLPlayerOldTask is the original @ref FramePacer-based
 *             implementation, retained only so the new FrameSync-based
 *             sink can be validated side-by-side.  It will be removed
 *             once FrameSync is proven in the field; see
 *             @c devplan/proav_framesync.md Phase 4.  New code must
 *             not depend on it.
 *
 * SDLPlayerOldTask is the MediaIO analog of @c SDLDisplayNode: it is a
 * write-only sink backend that consumes @c Frame objects via
 * @c MediaIO::writeFrame() and displays their video through an
 * application-provided @c SDLVideoWidget and (optionally) plays their
 * audio through an application-provided @c SDLAudioOutput.
 *
 * @par Creation
 *
 * Unlike registered backends, SDLPlayerOldTask cannot be created via
 * @c MediaIO::create() because it needs raw pointers to the video
 * widget and audio output — values that can't be carried in a
 * @c MediaConfig.  Use the free factory function
 * @c createSDLPlayerOld() to construct a ready-to-use @c MediaIO that
 * adopts a properly configured SDLPlayerOldTask.
 *
 * @par Compressed input — automatic decode
 *
 * SDLPlayerOldTask accepts compressed video formats (JPEG, JPEG XS,
 * and anything else with a registered @ref ImageCodec) as input
 * and decodes them transparently before the frame is handed to
 * the widget.  The decode runs inside @ref executeCmd
 * "executeCmd(Write)" on the MediaIO strand worker thread via
 * @ref Image::convert, which dispatches to the codec registry by
 * name — the task does not hard-code any codec.  The decode
 * target is @c PixelDesc::RGBA8_sRGB so the decoded image can
 * be handed to @ref SDLVideoWidget directly, regardless of which
 * codec was involved.
 *
 * Running the decode on the strand thread (rather than the main
 * thread) keeps the UI responsive for heavy codecs: the main
 * thread only ever sees an already-decoded image ready for
 * texture upload.  This means a receiver like
 * @c mediaplay @c -i @c foo.sdp @c -o @c SDL just works for any
 * format the codec layer can decode, with no Converter stage
 * needed in front of the SDL sink.
 *
 * If a decode fails (missing codec, malformed bitstream) the
 * offending frame is counted as dropped and the task continues
 * processing subsequent frames.
 *
 * Uncompressed input with a pixel layout SDL cannot upload
 * directly (e.g. 10-bit planar YUV) is handled by the widget's
 * own CSC fallback — the task does not pre-convert those.
 *
 * @par Open-time setup
 *
 * For a writer, MediaIO delivers the caller-provided @c MediaDesc and
 * (optionally) @c AudioDesc to the task in @c MediaIOCommandOpen.
 * SDLPlayerOldTask reads the frame rate from the @c MediaDesc and
 * configures and opens the SDLAudioOutput using either
 * the explicit @c pendingAudioDesc or the first entry in
 * @c pendingMediaDesc.audioList().  Callers must therefore set the
 * media description before opening:
 *
 * @code
 * MediaIO *player = createSDLPlayerOld(&videoWidget, &audioOutput);
 * player->setMediaDesc(sourceMediaIO->mediaDesc());
 * player->setAudioDesc(sourceMediaIO->audioDesc());
 * player->open(MediaIO::Input);
 * @endcode
 *
 * @par Teardown ordering
 *
 * The main thread's render callable captures a pointer to the task.
 * The caller must therefore @c close() the MediaIO and let the main
 * event loop drain (for example by quitting the @c SDLApplication
 * before @c delete) before destroying the owning MediaIO.  Destroying
 * the MediaIO while a render callable is still queued on the main
 * thread's event loop is undefined behavior — mirroring the same
 * constraint that applies to @c SDLDisplayNode.
 */
class SDLPlayerOldTask : public MediaIOTask {
        public:
                /**
                 * @brief Constructs a player task.
                 *
                 * Neither pointer is owned.  @p video may be @c nullptr
                 * if the caller has no interest in displaying video
                 * (audio-only playback).  @p audio may be @c nullptr if
                 * the caller does not want audio, or if the source will
                 * never carry audio.
                 *
                 * The player is always paced to the source's frame
                 * rate.  When @p useAudioClock is true and an audio
                 * output is available, timing is derived from the
                 * audio device's consumption rate; otherwise the
                 * system's monotonic wall clock is used.
                 *
                 * @param video         Video widget for display, or nullptr.
                 * @param audio         Audio output for playback, or nullptr.
                 * @param useAudioClock @c true to prefer the audio device
                 *                      as the timing source (default);
                 *                      @c false to use wall clock.
                 */
                SDLPlayerOldTask(SDLVideoWidget *video, SDLAudioOutput *audio,
                              bool useAudioClock = true);

                /** @brief Destructor.  Does not close — that is MediaIO's job. */
                ~SDLPlayerOldTask() override;

                /** @brief Returns the configured video widget (not owned). */
                SDLVideoWidget *videoWidget() const { return _videoWidget; }

                /** @brief Returns the configured audio output (not owned). */
                SDLAudioOutput *audioOutput() const { return _audioOutput; }

                /** @brief Returns true if the audio device drives timing. */
                bool useAudioClock() const { return _useAudioClock; }

                /** @brief Total frames presented to the video widget. */
                int64_t framesPresented() const { return _framesPresented.value(); }

                /**
                 * @brief Paints the currently stashed image, if any.
                 *
                 * Posted to the main thread's EventLoop by @c writeFrame()
                 * whenever a new frame is stashed.  Safe to call with no
                 * pending image — returns false in that case.  Must be
                 * called from the main thread.
                 *
                 * @return @c true if a frame was actually painted.
                 */
                bool renderPending();

        private:
                Error executeCmd(MediaIOCommandOpen &cmd) override;
                Error executeCmd(MediaIOCommandClose &cmd) override;
                Error executeCmd(MediaIOCommandWrite &cmd) override;

                void wakeMainThread();

                static uint32_t userEventType();

                // Application-provided outputs (not owned).
                SDLVideoWidget *_videoWidget = nullptr;
                SDLAudioOutput *_audioOutput = nullptr;

                // Timing source preference.
                bool            _useAudioClock = true;

                // Per-open state.
                bool            _audioConfigured = false;
                FrameRate       _frameRate;
                AudioDesc       _audioDesc;
                FramePacer      _pacer;
                SDLAudioClock  *_audioClock = nullptr;

                // Latest image staged for the main thread to render.
                Image::Ptr      _pendingImage;
                mutable Mutex   _pendingMutex;

                // True when a render notification has been posted to
                // the main thread but not yet consumed.  Used to
                // collapse notifications in fast mode so writeFrame()
                // cannot flood the SDL event queue or the EventLoop
                // callable queue.  Cleared at the start of every
                // @c renderPending() call.
                Atomic<bool>    _renderScheduled;

                // Stats.
                Atomic<int64_t> _framesPresented;

                // 1 Hz debug log covering pacer + player health.
                // Serviced from executeCmd(Write) on the strand worker
                // thread.  Reset on Close so a subsequent Open starts
                // with a fresh interval.
                PeriodicCallback _debugReport;

                // Baselines for the 1 Hz log — let the callback
                // report per-period deltas alongside lifetime totals.
                int64_t         _lastLogRenderedFrames  = 0;
                int64_t         _lastLogDroppedFrames   = 0;
                int64_t         _lastLogRepeatedFrames  = 0;
                int64_t         _lastLogFramesPresented = 0;
};

/**
 * @brief Constructs a MediaIO writer that plays frames through SDL
 *        using the legacy @ref FramePacer path.
 * @ingroup sdl_core
 * @deprecated Use @ref createSDLPlayer instead — see
 *             @ref SDLPlayerOldTask.
 *
 * Creates an SDLPlayerOldTask with the supplied widget/audio pointers
 * and adopts it into a newly allocated MediaIO.  The returned MediaIO
 * is not yet open; the caller must set the source MediaDesc (and
 * optionally an AudioDesc) and then call @c open(MediaIO::Input).
 *
 * The returned MediaIO owns the task and deletes it on destruction.
 * Ownership of @p video and @p audio stays with the caller — they
 * must outlive the returned MediaIO.
 *
 * @param video         Video widget to display frames in (may be nullptr).
 * @param audio         Audio output for playback (may be nullptr).
 * @param useAudioClock @c true to prefer the audio device as the
 *                      timing source (default); @c false to use
 *                      wall clock.
 * @param parent        Optional parent object for the MediaIO.
 * @return A new MediaIO ready to be configured and opened as a
 *         writer, or nullptr if task adoption fails.
 */
MediaIO *createSDLPlayerOld(SDLVideoWidget *video,
                         SDLAudioOutput *audio,
                         bool useAudioClock = true,
                         ObjectBase *parent = nullptr);

PROMEKI_NAMESPACE_END
