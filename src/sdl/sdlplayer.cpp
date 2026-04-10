/**
 * @file      sdlplayer.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/sdl/sdlplayer.h>
#include <promeki/sdl/sdlapplication.h>
#include <promeki/sdl/sdlaudiooutput.h>
#include <promeki/sdl/sdlvideowidget.h>
#include <promeki/sdl/sdlwindow.h>
#include <promeki/frame.h>
#include <promeki/mediadesc.h>
#include <promeki/mediaconfig.h>
#include <promeki/logger.h>

#include <SDL3/SDL.h>
#include <chrono>
#include <thread>

PROMEKI_NAMESPACE_BEGIN

uint32_t SDLPlayerTask::userEventType() {
        static uint32_t type = SDL_RegisterEvents(1);
        return type;
}

SDLPlayerTask::SDLPlayerTask(SDLVideoWidget *video, SDLAudioOutput *audio, bool paced) :
        _videoWidget(video),
        _audioOutput(audio),
        _paced(paced)
{
        _renderScheduled.setValue(false);
}

SDLPlayerTask::~SDLPlayerTask() {
        // MediaIO guarantees Close has been dispatched before the task
        // is destroyed, so there is nothing to release here.  The
        // application owns the video widget and audio output.
}

Error SDLPlayerTask::executeCmd(MediaIOCommandOpen &cmd) {
        if(cmd.mode != MediaIO::Writer) {
                promekiErr("SDLPlayerTask: only Writer mode is supported");
                return Error::NotSupported;
        }

        const MediaDesc &mdesc = cmd.pendingMediaDesc;
        FrameRate fps = mdesc.frameRate();
        if(!fps.isValid()) {
                promekiErr("SDLPlayerTask: pendingMediaDesc has no valid frame rate");
                return Error::InvalidArgument;
        }
        _frameRate = fps;

        // Wall-clock pacing fallback interval — used whenever audio-led
        // pacing isn't available (no audio in the stream, no audio
        // output configured, or fast mode).
        Duration framePeriod = fps.frameDuration();
        _frameInterval = std::chrono::nanoseconds(framePeriod.nanoseconds());
        _firstFrame = true;

        // Resolve the audio description: explicit pendingAudioDesc wins,
        // otherwise use the first entry in the media desc's audio list.
        AudioDesc adesc = cmd.pendingAudioDesc;
        if(!adesc.isValid() && !mdesc.audioList().isEmpty()) {
                adesc = mdesc.audioList()[0];
        }

        // Configure and open the audio output, if we're supposed to
        // play audio.  In fast mode we skip audio entirely.
        _audioConfigured = false;
        _maxQueuedBytes = 0;
        _audioDesc = AudioDesc();
        if(_paced && _audioOutput != nullptr && adesc.isValid()) {
                if(!_audioOutput->configure(adesc)) {
                        promekiErr("SDLPlayerTask: audio configure failed");
                        return Error::Invalid;
                }
                if(!_audioOutput->open()) {
                        promekiErr("SDLPlayerTask: audio open failed");
                        return Error::Invalid;
                }
                _audioConfigured = true;
                _audioDesc = adesc;

                // Audio-led pacing threshold: allow up to two frames'
                // worth of audio to sit in the SDL queue.  After pushing
                // each frame we block until the queue drops back below
                // this mark, which effectively throttles writeFrame() to
                // the rate the audio device is actually consuming.
                //
                // SDLAudioOutput always pushes float32 samples regardless
                // of the source format, so size the threshold in float
                // bytes rather than adesc.bytesPerSample().
                double bytesPerFrame =
                        (double)adesc.sampleRate() *
                        (double)adesc.channels() *
                        (double)sizeof(float) *
                        framePeriod.toSecondsDouble();
                _maxQueuedBytes = (int)(bytesPerFrame * 2.0);
                if(_maxQueuedBytes < 1) _maxQueuedBytes = 1;
        }

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
        return Error::Ok;
}

Error SDLPlayerTask::executeCmd(MediaIOCommandClose &cmd) {
        (void)cmd;

        // Close the audio output if we opened it.
        if(_audioConfigured && _audioOutput != nullptr) {
                _audioOutput->close();
        }
        _audioConfigured = false;
        _maxQueuedBytes = 0;
        _audioDesc = AudioDesc();
        _frameRate = FrameRate();
        _firstFrame = true;

        // Drop any still-stashed image so a lingering render callable
        // from before the close sees nothing to paint.
        {
                Mutex::Locker lock(_pendingMutex);
                _pendingImage = Image::Ptr();
        }
        _renderScheduled.setValue(false);
        return Error::Ok;
}

Error SDLPlayerTask::executeCmd(MediaIOCommandWrite &cmd) {
        const Frame::Ptr &frame = cmd.frame;
        if(!frame.isValid()) return Error::InvalidArgument;

        // Audio first, then block on audio-led pacing.  This keeps the
        // SDL audio queue topped up ahead of the video we're about to
        // display, which matches the normal A/V presentation order.
        bool paced = false;
        if(_paced && _audioConfigured && _audioOutput != nullptr) {
                for(const auto &audio : frame->audioList()) {
                        if(audio.isValid()) {
                                _audioOutput->pushAudio(*audio);
                        }
                }

                // Block until the queue drains below the threshold.
                // The 1 ms poll is coarse compared to the audio clock,
                // but it keeps us from burning CPU and is well under a
                // video frame period at all reasonable rates.
                while(_audioOutput->queuedBytes() > _maxQueuedBytes) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                paced = true;
        }

        // If audio pacing wasn't available but the caller asked for
        // paced playback, fall back to wall-clock sleep based on the
        // configured frame rate.
        if(_paced && !paced && _frameInterval > TimeStamp::Duration::zero()) {
                if(_firstFrame) {
                        _nextFrameTime = TimeStamp::now();
                        _firstFrame = false;
                } else {
                        _nextFrameTime += _frameInterval;
                        _nextFrameTime.sleepUntil();
                }
        }

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
                                promekiWarn("SDLPlayerTask: decode of '%s' "
                                            "to RGBA8_sRGB failed — dropping frame",
                                            newImage->pixelDesc().name().cstr());
                                _framesDropped.fetchAndAdd(1);
                                cmd.currentFrame++;
                                cmd.frameCount = MediaIO::FrameCountInfinite;
                                return Error::Ok;
                        }
                        newImage = Image::Ptr::create(std::move(decoded));
                }
                {
                        Mutex::Locker lock(_pendingMutex);
                        if(_pendingImage.isValid()) {
                                _framesDropped.fetchAndAdd(1);
                        }
                        _pendingImage = newImage;
                }
                if(!_renderScheduled.exchange(true)) {
                        wakeMainThread();
                }
        }

        cmd.currentFrame++;
        cmd.frameCount = MediaIO::FrameCountInfinite;
        return Error::Ok;
}

void SDLPlayerTask::wakeMainThread() {
        // Post renderPending() to the main thread's EventLoop.  See
        // the "Teardown ordering" note in sdlplayer.h: the capture of
        // `this` relies on the caller closing the MediaIO and letting
        // the event loop drain before destroying it.
        SDLApplication *app = SDLApplication::instance();
        if(app != nullptr) {
                app->eventLoop().postCallable([this]() {
                        renderPending();
                });
        }

        // Also push an SDL user event so SDL_WaitEvent in the pump
        // wakes up promptly to run the callable.
        SDL_Event event = {};
        event.type = userEventType();
        SDL_PushEvent(&event);
}

bool SDLPlayerTask::renderPending() {
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

MediaIO *createSDLPlayer(SDLVideoWidget *video,
                         SDLAudioOutput *audio,
                         bool paced,
                         ObjectBase *parent)
{
        auto *task = new SDLPlayerTask(video, audio, paced);
        auto *io = new MediaIO(parent);
        Error err = io->adoptTask(task);
        if(err.isError()) {
                promekiErr("createSDLPlayer: adoptTask failed: %s",
                           err.name().cstr());
                delete task;
                delete io;
                return nullptr;
        }
        return io;
}

PROMEKI_NAMESPACE_END
