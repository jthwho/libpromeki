/**
 * @file      sdlplayerold.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/sdl/sdlplayerold.h>
#include <promeki/sdl/sdlsubsystem.h>
#include <promeki/sdl/sdlaudiooutput.h>
#include <promeki/sdl/sdlvideowidget.h>
#include <promeki/sdl/sdlwindow.h>
#include <promeki/frame.h>
#include <promeki/mediadesc.h>
#include <promeki/mediaconfig.h>
#include <promeki/logger.h>

#include <SDL3/SDL.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_DEBUG(SDLPlayerOld)

uint32_t SDLPlayerOldTask::userEventType() {
        static uint32_t type = SDL_RegisterEvents(1);
        return type;
}

SDLPlayerOldTask::SDLPlayerOldTask(SDLVideoWidget *video, SDLAudioOutput *audio, bool useAudioClock) :
        _videoWidget(video),
        _audioOutput(audio),
        _useAudioClock(useAudioClock)
{
        _renderScheduled.setValue(false);
}

SDLPlayerOldTask::~SDLPlayerOldTask() {
        // MediaIO guarantees Close has been dispatched before the task
        // is destroyed, so _audioClock should already be null.  Belt
        // and suspenders.
        delete _audioClock;
}

Error SDLPlayerOldTask::executeCmd(MediaIOCommandOpen &cmd) {
        if(cmd.mode != MediaIO::Input) {
                promekiErr("SDLPlayerOldTask: only Writer mode is supported");
                return Error::NotSupported;
        }

        const MediaDesc &mdesc = cmd.pendingMediaDesc;
        FrameRate fps = mdesc.frameRate();
        if(!fps.isValid()) {
                promekiErr("SDLPlayerOldTask: pendingMediaDesc has no valid frame rate");
                return Error::InvalidArgument;
        }
        _frameRate = fps;

        // Resolve the audio description: explicit pendingAudioDesc wins,
        // otherwise use the first entry in the media desc's audio list.
        AudioDesc adesc = cmd.pendingAudioDesc;
        if(!adesc.isValid() && !mdesc.audioList().isEmpty()) {
                adesc = mdesc.audioList()[0];
        }

        // Configure and open the audio output if one was provided
        // and the stream carries audio.
        _audioConfigured = false;
        _audioDesc = AudioDesc();
        delete _audioClock;
        _audioClock = nullptr;
        if(_audioOutput != nullptr && adesc.isValid()) {
                if(!_audioOutput->configure(adesc)) {
                        promekiErr("SDLPlayerOldTask: audio configure failed");
                        return Error::Invalid;
                }
                if(!_audioOutput->open()) {
                        promekiErr("SDLPlayerOldTask: audio open failed");
                        return Error::Invalid;
                }
                _audioConfigured = true;
                _audioDesc = adesc;

                // When the audio device is the timing source, create
                // an audio-derived clock so the FramePacer tracks
                // the device's actual consumption rate.  When wall
                // clock is selected, audio still plays but is not
                // used as the timing reference.
                if(_useAudioClock) {
                        double audioBytesPerSec =
                                (double)adesc.sampleRate() *
                                (double)adesc.channels() *
                                (double)sizeof(float);
                        _audioClock = new SDLAudioClock(
                                _audioOutput, audioBytesPerSec);
                }
        }

        // Configure the pacer.  When audio is available the audio
        // clock drives timing; otherwise the built-in wall clock
        // takes over.  Either way, pacing always flows through the
        // same FramePacer with all its error compensation, drop
        // recommendations, and periodic debug logging.
        if(_useAudioClock && _audioClock == nullptr) {
                promekiInfo("SDLPlayerOldTask: audio clock requested but no audio "
                            "available; using wall clock pacing");
        }
        _pacer.setName(String("SDLPlayerOld"));
        _pacer.setFrameRate(fps);
        _pacer.setClock(_audioClock);
        _pacer.reset();

        // Set up the 1 Hz debug log.  Computes per-period deltas so
        // operators can watch instantaneous render/drop/repeat rates
        // rather than only lifetime totals.
        _lastLogRenderedFrames  = 0;
        _lastLogDroppedFrames   = 0;
        _lastLogRepeatedFrames  = 0;
        _lastLogFramesPresented = 0;
        _debugReport = PeriodicCallback(1.0, [this] {
                const int64_t rendered  = _pacer.renderedFrames();
                const int64_t dropped   = _pacer.droppedFrames();
                const int64_t repeated  = _pacer.repeatedFrames();
                const int64_t presented = _framesPresented.value();
                const double  accErrMs  =
                        (double)_pacer.accumulatedError().nanoseconds() / 1e6;

                promekiDebug("SDLPlayerOld[%s]: rendered=%lld (+%lld) "
                             "dropped=%lld (+%lld) repeated=%lld (+%lld) "
                             "presented=%lld (+%lld) missed=%lld "
                             "accErr=%.3f ms clock=%s",
                             _pacer.name().cstr(),
                             static_cast<long long>(rendered),
                             static_cast<long long>(rendered - _lastLogRenderedFrames),
                             static_cast<long long>(dropped),
                             static_cast<long long>(dropped - _lastLogDroppedFrames),
                             static_cast<long long>(repeated),
                             static_cast<long long>(repeated - _lastLogRepeatedFrames),
                             static_cast<long long>(presented),
                             static_cast<long long>(presented - _lastLogFramesPresented),
                             static_cast<long long>(_pacer.missedFrames()),
                             accErrMs,
                             _pacer.clock()->domain().name().cstr());

                _lastLogRenderedFrames  = rendered;
                _lastLogDroppedFrames   = dropped;
                _lastLogRepeatedFrames  = repeated;
                _lastLogFramesPresented = presented;
        });

        // Echo the caller's descriptors back — the user already has
        // these, but MediaIO caches whatever Open reports.
        cmd.mediaDesc = mdesc;
        cmd.audioDesc = _audioDesc;
        cmd.frameRate = fps;
        cmd.canSeek = false;
        cmd.frameCount = MediaIO::FrameCountInfinite;
        cmd.defaultStep = 1;
        cmd.defaultPrefetchDepth = 1;
        cmd.defaultSeekMode = MediaIO_SeekExact;

        // The strand worker blocks for a full frame period on every
        // write (audio drain or wall-clock sleep).  A deep write
        // queue just adds latency — each queued frame waits behind
        // the current frame's pacing sleep.  Depth 2 keeps one
        // frame ready-to-go while the current one paces, without
        // piling up.
        cmd.defaultWriteDepth = 2;

        return Error::Ok;
}

Error SDLPlayerOldTask::executeCmd(MediaIOCommandClose &cmd) {
        (void)cmd;

        // Close the audio output if we opened it.
        if(_audioConfigured && _audioOutput != nullptr) {
                _audioOutput->close();
        }
        _audioConfigured = false;
        _pacer.setClock(nullptr);
        delete _audioClock;
        _audioClock = nullptr;
        _audioDesc = AudioDesc();
        _frameRate = FrameRate();

        // Drop any still-stashed image so a lingering render callable
        // from before the close sees nothing to paint.
        {
                Mutex::Locker lock(_pendingMutex);
                _pendingImage = Image::Ptr();
        }
        _renderScheduled.setValue(false);
        _debugReport = PeriodicCallback{};
        return Error::Ok;
}

Error SDLPlayerOldTask::executeCmd(MediaIOCommandWrite &cmd) {
        const Frame::Ptr &frame = cmd.frame;
        if(!frame.isValid()) return Error::InvalidArgument;

        // Push audio before pacing so the audio clock (if active)
        // has up-to-date data.  This keeps the SDL audio queue
        // topped up ahead of the video we're about to display,
        // which matches the normal A/V presentation order.
        if(_audioConfigured && _audioOutput != nullptr) {
                for(const auto &audio : frame->audioList()) {
                        if(audio.isValid()) {
                                _audioOutput->pushAudio(*audio);
                        }
                }
        }

        // Pace through the FramePacer — regardless of whether the
        // clock is audio-driven or wall-clock.  All error compensation,
        // drop recommendations, and logging flow through the same path.
        //
        // framesToDrop > 0 means wall time has advanced past where the
        // pacer thinks we are — typically because the source delivered
        // this frame later than its deadline.  The SDL player always
        // renders the frame it was handed (there is only one, and
        // skipping it would make things worse), and reports the gap
        // back to the pacer as "repeated" frame periods so the
        // pacer's timeline re-syncs to wall time on the next pace().
        {
                auto pr = _pacer.pace();
                if(pr.framesToDrop > 0) {
                        _pacer.noteRepeated(pr.framesToDrop);
                        for(int64_t i = 0; i < pr.framesToDrop; i++) {
                                noteFrameDropped();
                        }
                }
        }

        // Service the 1 Hz debug log.  Sits here so both successful
        // writes and decode-failure early returns below feed into it
        // via the pacer's state.
        _debugReport.service();

        // --- Actual processing work starts here ---
        //
        // Everything above is pacing / throttling.  stampWorkBegin()
        // marks the start of real per-frame processing so the
        // framework can report processing time separately from
        // end-to-end latency.
        stampWorkBegin();

        // Stash the latest video image for the main thread to render.
        // If an older image is still pending, count that as a drop.
        //
        // Notifications are collapsed via _renderScheduled so that a
        // fast-mode pumper running far ahead of the main thread doesn't
        // flood the SDL event queue or the EventLoop callable queue.
        // Only the first writeFrame in each "main thread catch-up"
        // window actually posts a wake; subsequent writes just update
        // the stashed image and let the existing pending wake handle it.
        //
        // Compressed input (JPEG, JPEG XS) is decoded here on the
        // MediaIO strand worker thread before being handed to the
        // main-thread render path.  Image::convert() dispatches to
        // the registered ImageCodec under the hood — the SDL player
        // task does not need to know which codec is involved, only
        // that the target format needs to be something the widget
        // can upload.  RGBA8_sRGB is the universal display target;
        // any per-codec CSC hop (e.g. JPEG XS → planar YUV →
        // RGBA8) is handled by the convert() chain.  Running the
        // decode on the strand thread keeps the UI responsive even
        // for heavy codecs — the main thread only sees the decoded
        // image ready for upload.
        if(!frame->imageList().isEmpty()) {
                Image::Ptr newImage = frame->imageList()[0];
                if(newImage.isValid() && newImage->isCompressed()) {
                        Image decoded = newImage->convert(
                                PixelDesc(PixelDesc::RGBA8_sRGB),
                                newImage->metadata());
                        if(!decoded.isValid()) {
                                promekiWarn("SDLPlayerOldTask: decode of '%s' "
                                            "to RGBA8_sRGB failed — dropping frame",
                                            newImage->pixelDesc().name().cstr());
                                noteFrameDropped();
                                cmd.currentFrame++;
                                cmd.frameCount = MediaIO::FrameCountInfinite;
                                return Error::Ok;
                        }
                        newImage = Image::Ptr::create(std::move(decoded));
                }
                {
                        Mutex::Locker lock(_pendingMutex);
                        if(_pendingImage.isValid()) {
                                noteFrameDropped();
                        }
                        _pendingImage = newImage;
                }
                if(!_renderScheduled.exchange(true)) {
                        wakeMainThread();
                }
                _pacer.noteRendered();
        }

        stampWorkEnd();

        cmd.currentFrame++;
        cmd.frameCount = MediaIO::FrameCountInfinite;
        return Error::Ok;
}

void SDLPlayerOldTask::wakeMainThread() {
        // Post renderPending() to the main thread's EventLoop.  See
        // the "Teardown ordering" note in sdlplayerold.h: the capture of
        // `this` relies on the caller closing the MediaIO and letting
        // the event loop drain before destroying it.
        SdlSubsystem *app = SdlSubsystem::instance();
        if(app != nullptr && app->eventLoop() != nullptr) {
                app->eventLoop()->postCallable([this]() {
                        renderPending();
                });
        }

        // Also push an SDL user event so SDL_WaitEvent in the pump
        // wakes up promptly to run the callable.
        SDL_Event event = {};
        event.type = userEventType();
        SDL_PushEvent(&event);
}

bool SDLPlayerOldTask::renderPending() {
        // Clear the "render scheduled" flag first so that any writeFrame
        // racing with us gets to post a fresh wake.  Clearing before we
        // grab the image means the worst case is one extra spurious
        // renderPending call; clearing after would risk leaving an
        // image stashed with no pending wake.
        _renderScheduled.setValue(false);

        if(_videoWidget == nullptr) return false;

        Image::Ptr img;
        {
                Mutex::Locker lock(_pendingMutex);
                if(!_pendingImage.isValid()) return false;
                img = _pendingImage;
                _pendingImage = Image::Ptr();
        }

        _videoWidget->setImage(*img);

        // Walk up the widget parent chain to find the owning window and
        // trigger a repaint.  This mirrors SDLDisplayNode::renderPending.
        ObjectBase *p = _videoWidget->parent();
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

MediaIO *createSDLPlayerOld(SDLVideoWidget *video,
                         SDLAudioOutput *audio,
                         bool useAudioClock,
                         ObjectBase *parent)
{
        auto *task = new SDLPlayerOldTask(video, audio, useAudioClock);
        auto *io = new MediaIO(parent);
        Error err = io->adoptTask(task);
        if(err.isError()) {
                promekiErr("createSDLPlayerOld: adoptTask failed: %s",
                           err.name().cstr());
                delete task;
                delete io;
                return nullptr;
        }
        return io;
}

PROMEKI_NAMESPACE_END
