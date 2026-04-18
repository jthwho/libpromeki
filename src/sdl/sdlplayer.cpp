/**
 * @file      sdlplayer.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/sdl/sdlplayer.h>
#include <promeki/sdl/sdlsubsystem.h>
#include <promeki/sdl/sdlaudiooutput.h>
#include <promeki/sdl/sdlvideowidget.h>
#include <promeki/sdl/sdlwindow.h>
#include <promeki/clock.h>
#include <promeki/frame.h>
#include <promeki/mediadesc.h>
#include <promeki/mediaconfig.h>
#include <promeki/logger.h>

#include <SDL3/SDL.h>
#include <cstring>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_DEBUG(SDLPlayer)

// Number of frame periods of silence pushed to SDL at open time.
// Gives the device a head-start so its consumed-byte counter
// begins advancing before any produced audio arrives, which
// stabilises the audio clock's rate estimate and prevents the
// startup-underrun feedback loop.
static constexpr int kAudioPrerollFrames = 3;

uint32_t SDLPlayerTask::userEventType() {
        static uint32_t type = SDL_RegisterEvents(1);
        return type;
}

SDLPlayerTask::SDLPlayerTask(SDLVideoWidget *video, SDLAudioOutput *audio,
                               bool useAudioClock)
        : _videoWidget(video),
          _audioOutput(audio),
          _useAudioClock(useAudioClock)
{
        _sync.setName(String("SDLPlayer"));
        _sync.setInputOverflowPolicy(
                FrameSync::InputOverflowPolicy::Block);
        _renderScheduled.setValue(false);
        _pullRunning.setValue(false);
}

SDLPlayerTask::~SDLPlayerTask() {
        // MediaIO guarantees Close has run before delete.  Belt and
        // suspenders in case of misuse.
        if(_pullThread.joinable()) {
                _sync.interrupt();
                _pullThread.join();
        }
        delete _audioClock;
}

Error SDLPlayerTask::executeCmd(MediaIOCommandOpen &cmd) {
        if(cmd.mode != MediaIO::Input) {
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

        AudioDesc adesc = cmd.pendingAudioDesc;
        if(!adesc.isValid() && !mdesc.audioList().isEmpty()) {
                adesc = mdesc.audioList()[0];
        }

        _audioConfigured = false;
        _audioDesc = AudioDesc();
        delete _audioClock;
        _audioClock = nullptr;
        _clock = nullptr;

        if(_audioOutput != nullptr && adesc.isValid()) {
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

                // Pre-fill SDL with a few frame periods of silence so
                // the device starts draining immediately.  This keeps
                // SDL's consumed counter advancing continuously and
                // prevents the startup underrun that would otherwise
                // freeze the audio clock and trigger a feedback loop
                // with FrameSync's rate-based pacing.
                const size_t prerollSamples =
                        static_cast<size_t>(adesc.sampleRate() / fps.toDouble())
                        * kAudioPrerollFrames;
                if(prerollSamples > 0) {
                        Audio silence(adesc, prerollSamples);
                        silence.resize(prerollSamples);
                        // The Audio buffer is allocated for the descriptor's
                        // sample format (e.g. 2 bytes/sample for s16, 4 for
                        // f32).  silence.zero() fills exactly that buffer —
                        // hand-rolled memset with sizeof(float) would overrun
                        // for any non-float descriptor and corrupt the heap.
                        silence.zero();
                        _audioOutput->pushAudio(silence);
                }

                if(_useAudioClock) {
                        double audioBytesPerSec =
                                (double)adesc.sampleRate() *
                                (double)adesc.channels() *
                                (double)sizeof(float);
                        _audioClock = new SDLAudioClock(
                                _audioOutput, audioBytesPerSec);
                        _clock = _audioClock;
                }
        }

        // Fall back to wall clock when the audio clock isn't in play.
        if(_clock == nullptr) {
                static WallClock wallClock;
                _clock = &wallClock;
                if(_useAudioClock) {
                        promekiInfo("SDLPlayerTask: audio clock requested but "
                                    "no audio available; using wall clock");
                }
        }

        _sync.setTargetFrameRate(fps);
        _sync.setTargetAudioDesc(_audioDesc);
        _sync.setClock(_clock);
        _sync.reset();

        _pullRunning.setValue(true);
        _pullThread = std::thread([this]{ pullLoop(); });

        cmd.mediaDesc = mdesc;
        cmd.audioDesc = _audioDesc;
        cmd.frameRate = fps;
        cmd.canSeek = false;
        cmd.frameCount = MediaIO::FrameCountInfinite;
        cmd.defaultStep = 1;
        cmd.defaultPrefetchDepth = 1;

        // Write depth 2: with FrameSync's Block policy, pushFrame
        // back-pressures the strand anyway, so a deep write queue
        // is just extra latency.
        cmd.defaultWriteDepth = 2;
        cmd.defaultSeekMode = MediaIO_SeekExact;

        return Error::Ok;
}

Error SDLPlayerTask::executeCmd(MediaIOCommandClose &cmd) {
        (void)cmd;

        _pullRunning.setValue(false);
        _sync.pushEndOfStream();
        _sync.interrupt();
        if(_pullThread.joinable()) _pullThread.join();

        if(_audioConfigured && _audioOutput != nullptr) {
                _audioOutput->close();
        }
        _audioConfigured = false;
        _sync.setClock(nullptr);
        _clock = nullptr;
        delete _audioClock;
        _audioClock = nullptr;
        _audioDesc = AudioDesc();
        _frameRate = FrameRate();

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

        // Work begins now — the framework bills pushFrame back-pressure
        // time to the writer (matches the old pacer semantics).
        stampWorkBegin();

        // Pre-decode compressed images on the strand so the pull
        // thread only deals with display-ready frames.  Any decode
        // failure is counted and the frame is discarded.
        Frame::Ptr outFrame = frame;
        if(!frame->imageList().isEmpty()) {
                Image::Ptr srcImg = frame->imageList()[0];
                if(srcImg.isValid() && srcImg->isCompressed()) {
                        Image decoded = srcImg->convert(
                                PixelDesc(PixelDesc::RGBA8_sRGB),
                                srcImg->metadata());
                        if(!decoded.isValid()) {
                                promekiWarn("SDLPlayerTask: decode of '%s' to "
                                            "RGBA8_sRGB failed — dropping frame",
                                            srcImg->pixelDesc().name().cstr());
                                noteFrameDropped();
                                stampWorkEnd();
                                cmd.currentFrame++;
                                cmd.frameCount = MediaIO::FrameCountInfinite;
                                return Error::Ok;
                        }
                        outFrame = Frame::Ptr::create();
                        outFrame.modify()->imageList().pushToBack(
                                Image::Ptr::create(decoded));
                        for(const auto &a : frame->audioList()) {
                                outFrame.modify()->audioList().pushToBack(a);
                        }
                        outFrame.modify()->metadata() = frame->metadata();
                }
        }

        Error err = _sync.pushFrame(outFrame);
        stampWorkEnd();

        if(err.isError() && err != Error::Interrupt) {
                promekiWarn("SDLPlayerTask: pushFrame returned %s",
                            err.name().cstr());
        }

        cmd.currentFrame++;
        cmd.frameCount = MediaIO::FrameCountInfinite;
        return Error::Ok;
}

void SDLPlayerTask::pullLoop() {
        while(_pullRunning.value()) {
                auto result = _sync.pullFrame();
                if(result.second().isError()) {
                        // Interrupt on shutdown is expected; anything
                        // else is a real failure.
                        if(result.second() != Error::Interrupt) {
                                promekiWarn("SDLPlayerTask: pullFrame: %s",
                                            result.second().name().cstr());
                        }
                        break;
                }
                const FrameSync::PullResult &pr = result.first();
                if(!pr.frame.isValid()) continue;

                // Video → stash for the main thread.
                if(!pr.frame->imageList().isEmpty()) {
                        Image::Ptr img = pr.frame->imageList()[0];
                        if(img.isValid()) {
                                Mutex::Locker lock(_pendingMutex);
                                if(_pendingImage.isValid()) {
                                        // Previous image hasn't been
                                        // picked up — this replacement
                                        // is a drop at the display stage.
                                        noteFrameDropped();
                                }
                                _pendingImage = img;
                        }
                        if(!_renderScheduled.exchange(true)) {
                                wakeMainThread();
                        }
                }

                // Audio → push to the output.
                if(_audioConfigured && _audioOutput != nullptr) {
                        for(const auto &aud : pr.frame->audioList()) {
                                if(aud.isValid()) {
                                        _audioOutput->pushAudio(*aud);
                                }
                        }
                }
        }
}

void SDLPlayerTask::wakeMainThread() {
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

bool SDLPlayerTask::renderPending() {
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
                          bool useAudioClock,
                          ObjectBase *parent)
{
        auto *task = new SDLPlayerTask(video, audio, useAudioClock);
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
