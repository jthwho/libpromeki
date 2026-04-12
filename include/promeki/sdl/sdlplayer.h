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
#include <promeki/timestamp.h>
#include <promeki/framerate.h>
#include <promeki/audiodesc.h>
#include <promeki/image.h>
#include <promeki/objectbase.h>

PROMEKI_NAMESPACE_BEGIN

class SDLVideoWidget;
class SDLAudioOutput;

/**
 * @brief MediaIOTask writer that plays frames through SDL.
 * @ingroup sdl_core
 *
 * SDLPlayerTask is the MediaIO analog of @c SDLDisplayNode: it is a
 * write-only sink backend that consumes @c Frame objects via
 * @c MediaIO::writeFrame() and displays their video through an
 * application-provided @c SDLVideoWidget and (optionally) plays their
 * audio through an application-provided @c SDLAudioOutput.
 *
 * @par Creation
 *
 * Unlike registered backends, SDLPlayerTask cannot be created via
 * @c MediaIO::create() because it needs raw pointers to the video
 * widget and audio output — values that can't be carried in a
 * @c MediaConfig.  Use the free factory function
 * @c createSDLPlayer() to construct a ready-to-use @c MediaIO that
 * adopts a properly configured SDLPlayerTask.
 *
 * @par Pacing
 *
 * The task has two playback modes selected via the @c paced
 * constructor argument:
 *
 * - @b Paced (default): the task enforces the source's frame rate.
 *   When the frame contains audio and the caller supplied an
 *   SDLAudioOutput, the audio stream is used as the playback clock —
 *   each @c writeFrame() pushes the frame's audio to the output and
 *   then blocks until the queued audio drains below a one-frame
 *   threshold.  If there is no audio, the task falls back to a
 *   wall-clock sleep based on the configured frame rate.
 * - @b Fast ("as fast as possible"): audio is dropped entirely (the
 *   SDLAudioOutput is never opened), no pacing is applied, and the
 *   task simply hands each frame to the main thread for rendering.
 *   Because only the most recently stashed image is painted,
 *   intermediate frames are naturally dropped when the main thread
 *   cannot keep up.
 *
 * In both modes the video widget always sees only the most recent
 * image — the render callback posted to the main thread picks up
 * whatever image is currently stashed, so any older image still
 * waiting when a newer one arrives is counted as a dropped frame.
 *
 * @par Compressed input — automatic decode
 *
 * SDLPlayerTask accepts compressed video formats (JPEG, JPEG XS,
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
 * SDLPlayerTask reads the frame rate from the @c MediaDesc and, in
 * paced mode, configures and opens the SDLAudioOutput using either
 * the explicit @c pendingAudioDesc or the first entry in
 * @c pendingMediaDesc.audioList().  Callers must therefore set the
 * media description before opening:
 *
 * @code
 * MediaIO *player = createSDLPlayer(&videoWidget, &audioOutput);
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
class SDLPlayerTask : public MediaIOTask {
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
                 * @param video Video widget used to display frames, or nullptr.
                 * @param audio Audio output used for playback, or nullptr.
                 * @param paced @c true to enforce the source's frame rate
                 *              (audio-led pacing when available, wall-clock
                 *              fallback otherwise); @c false to play frames
                 *              as fast as possible with audio disabled.
                 */
                SDLPlayerTask(SDLVideoWidget *video, SDLAudioOutput *audio,
                              bool paced = true);

                /** @brief Destructor.  Does not close — that is MediaIO's job. */
                ~SDLPlayerTask() override;

                /** @brief Returns the configured video widget (not owned). */
                SDLVideoWidget *videoWidget() const { return _videoWidget; }

                /** @brief Returns the configured audio output (not owned). */
                SDLAudioOutput *audioOutput() const { return _audioOutput; }

                /** @brief Returns true if the task enforces the source frame rate. */
                bool isPaced() const { return _paced; }

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

                // Playback mode.
                bool            _paced = true;

                // Per-open state.
                bool            _audioConfigured = false;
                FrameRate       _frameRate;
                AudioDesc       _audioDesc;
                int             _maxQueuedBytes = 0;
                TimeStamp::Duration _frameInterval{};
                TimeStamp       _nextFrameTime;
                bool            _firstFrame = true;

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
};

/**
 * @brief Constructs a MediaIO writer that plays frames through SDL.
 * @ingroup sdl_core
 *
 * Creates an SDLPlayerTask with the supplied widget/audio pointers
 * and adopts it into a newly allocated MediaIO.  The returned MediaIO
 * is not yet open; the caller must set the source MediaDesc (and
 * optionally an AudioDesc) and then call @c open(MediaIO::Input).
 *
 * The returned MediaIO owns the task and deletes it on destruction.
 * Ownership of @p video and @p audio stays with the caller — they
 * must outlive the returned MediaIO.
 *
 * @param video  Video widget to display frames in (may be nullptr).
 * @param audio  Audio output for playback (may be nullptr).
 * @param paced  @c true for frame-rate enforced playback (default),
 *               @c false for as-fast-as-possible with audio dropped.
 * @param parent Optional parent object for the MediaIO.
 * @return A new MediaIO ready to be configured and opened as a
 *         writer, or nullptr if task adoption fails.
 */
MediaIO *createSDLPlayer(SDLVideoWidget *video,
                         SDLAudioOutput *audio,
                         bool paced = true,
                         ObjectBase *parent = nullptr);

PROMEKI_NAMESPACE_END
