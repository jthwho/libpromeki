/**
 * @file      sdlplayer.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/sdl/sdlplayer.h>
#include <promeki/sdl/sdlplayerwidget.h>
#include <promeki/sdl/sdlsubsystem.h>
#include <promeki/sdl/sdlaudiooutput.h>
#include <promeki/sdl/sdlvideowidget.h>
#include <promeki/sdl/sdlwindow.h>
#include <promeki/clock.h>
#include <promeki/colormodel.h>
#include <promeki/frame.h>
#include <promeki/uncompressedvideopayload.h>
#include <promeki/compressedvideopayload.h>
#include <promeki/pcmaudiopayload.h>
#include <promeki/audiopayload.h>
#include <promeki/videocodec.h>
#include <promeki/videodecoder.h>
#include <promeki/buffer.h>
#include <promeki/imagedesc.h>
#include <promeki/mediadesc.h>
#include <promeki/mediaconfig.h>
#include <promeki/mediaiodescription.h>
#include <promeki/mediaioportgroup.h>
#include <promeki/mediaiorequest.h>
#include <promeki/logger.h>

#include <cstring>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_DEBUG(SDLPlayer)

// Number of frame periods of silence pushed to SDL at open time.
// Gives the device a head-start so its consumed-byte counter
// begins advancing before any produced audio arrives, which
// stabilises the audio clock's rate estimate and prevents the
// startup-underrun feedback loop.
static constexpr int kAudioPrerollFrames = 3;

SDLPlayerMediaIO::SDLPlayerMediaIO(SDLPlayerWidget *widget, SDLAudioOutput *audio, bool useAudioClock)
    : DedicatedThreadMediaIO(nullptr), _widget(widget), _audioOutput(audio), _useAudioClock(useAudioClock) {
        _sync.setName(String("SDLPlayer"));
        _sync.setInputOverflowPolicy(FrameSync::InputOverflowPolicy::Block);
        _pullRunning.setValue(false);
}

SDLPlayerMediaIO::~SDLPlayerMediaIO() {
        if (isOpen()) (void)close().wait();
        // Belt and suspenders in case of misuse.
        if (_pullThread.joinable()) {
                _sync.interrupt();
                _pullThread.join();
        }
}

void SDLPlayerMediaIO::pause() {
        Mutex::Locker lock(_clockMutex);
        if (_clock.isNull() || !_clock->canPause()) return;
        if (_clock->isPaused()) return;

        Error err = _clock.modify()->setPause(true);
        if (err.isError()) {
                promekiWarn("SDLPlayerMediaIO: setPause(true) failed: %s", err.name().cstr());
                return;
        }
        stopPullThread();

        // The next push that unblocks on resume will carry a
        // timestamp relative to the last pre-pause push's timestamp;
        // upstream stages may have ticked MediaTimeStamps forward
        // during the paused interval even though back-pressure
        // swallowed the sample data.  Drop the estimator's last
        // sample so the first post-resume push just re-anchors the
        // baseline without poisoning the audio resample ratio.
        _sync.resetSourceRateEstimator();
}

void SDLPlayerMediaIO::resume() {
        Mutex::Locker lock(_clockMutex);
        if (_clock.isNull() || !_clock->canPause()) return;
        if (!_clock->isPaused()) return;

        Error err = _clock.modify()->setPause(false);
        if (err.isError()) {
                promekiWarn("SDLPlayerMediaIO: setPause(false) failed: %s", err.name().cstr());
                return;
        }
        startPullThread();
}

void SDLPlayerMediaIO::togglePause() {
        bool paused = false;
        {
                Mutex::Locker lock(_clockMutex);
                if (_clock.isValid() && _clock->canPause()) {
                        paused = _clock->isPaused();
                }
        }
        if (paused)
                resume();
        else
                pause();
}

bool SDLPlayerMediaIO::isPaused() const {
        Mutex::Locker lock(_clockMutex);
        if (_clock.isNull()) return false;
        return _clock->isPaused();
}

void SDLPlayerMediaIO::startPullThread() {
        // Caller holds _clockMutex.  Join any leftover std::thread
        // handle first — a paused pull loop exits on its own but
        // leaves the handle joinable; reassigning onto a joinable
        // std::thread would std::terminate().
        if (_pullThread.joinable()) _pullThread.join();
        // A prior @ref stopPullThread on this task may have left
        // @c FrameSync::_interrupted set — the pull thread can exit
        // through the ClockPaused / _pullRunning check without
        // consuming the flag.  Clear it so the fresh pull thread
        // isn't killed by a stale interrupt on its first pullFrame.
        _sync.clearInterrupt();
        _pullRunning.setValue(true);
        _pullThread = std::thread([this] { pullLoop(); });
}

void SDLPlayerMediaIO::cancelBlockingWork() {
        promekiDebug("SDLPlayerMediaIO::cancelBlockingWork");
        // Called from outside the strand when close is trying to
        // unwedge a deadlock — typically the strand is parked in
        // FrameSync::pushFrame waiting for queue room while the
        // CmdClose we want to run is queued behind it, and the
        // strand may have several more queued CmdWrites ahead of
        // CmdClose that will each re-enter pushFrame once the head
        // one returns.  Pushing EOS is sticky: any pushFrame that
        // would otherwise wait for queue room sees it on entry (or
        // on wake from a prior interrupt) and returns
        // @c Error::EndOfFile immediately, so the strand drains all
        // remaining writes to EOF and reaches CmdClose without
        // further intervention.  Interrupt wakes the current waiter.
        _sync.pushEndOfStream();
        _sync.interrupt();
}

void SDLPlayerMediaIO::stopPullThread() {
        // Caller holds _clockMutex.  We deliberately do NOT call
        // @c FrameSync::interrupt here — that would also wake any
        // strand thread parked in @c pushFrame waiting for queue
        // room, causing it to return @c Error::Interrupt and drop
        // the in-flight frame.  Drop a frame on every pause and
        // you get an audible glitch when playback catches up to
        // where the missing frame would have landed.
        //
        // In normal playback the pull thread is always in
        // @ref Clock::sleepUntil (on the SDL audio clock), which
        // polls @c isPaused and returns @c ClockPaused within
        // ~100 us of @c setPause(true).  pullLoop then breaks out
        // and the thread exits on its own.  If the pull thread is
        // between pullFrame calls, it sees @c _pullRunning=false
        // at the top of the loop and exits.
        //
        // Close needs a stronger signal (the clock isn't paused
        // during close) — close calls @c _sync.interrupt explicitly
        // before invoking this helper.
        _pullRunning.setValue(false);
        if (_pullThread.joinable()) _pullThread.join();
}

Error SDLPlayerMediaIO::executeCmd(MediaIOCommandOpen &cmd) {
        const MediaDesc &mdesc = cmd.pendingMediaDesc;
        FrameRate        fps = mdesc.frameRate();
        if (!fps.isValid()) {
                promekiErr("SDLPlayerMediaIO: pendingMediaDesc has no valid frame rate");
                return Error::InvalidArgument;
        }
        _frameRate = fps;

        AudioDesc adesc = cmd.pendingAudioDesc;
        if (!adesc.isValid() && !mdesc.audioList().isEmpty()) {
                adesc = mdesc.audioList()[0];
        }

        _audioConfigured = false;
        _audioDesc = AudioDesc();
        {
                Mutex::Locker lock(_clockMutex);
                _clock.clear();
        }

        if (_audioOutput != nullptr && adesc.isValid()) {
                if (!_audioOutput->configure(adesc)) {
                        promekiErr("SDLPlayerMediaIO: audio configure failed");
                        return Error::Invalid;
                }
                if (!_audioOutput->open()) {
                        promekiErr("SDLPlayerMediaIO: audio open failed");
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
                        static_cast<size_t>(adesc.sampleRate() / fps.toDouble()) * kAudioPrerollFrames;
                if (prerollSamples > 0) {
                        // Build a zero-filled silence payload in the
                        // descriptor's sample format and hand it to
                        // the audio output.  bufferSize() returns the
                        // exact byte count so std::memset fills no
                        // more than the allocated buffer regardless
                        // of sample stride.
                        size_t      sz = adesc.bufferSize(prerollSamples);
                        Buffer::Ptr pcm = Buffer::Ptr::create(sz);
                        pcm.modify()->setSize(sz);
                        std::memset(pcm.modify()->data(), 0, sz);
                        BufferView view(pcm, 0, sz);
                        auto       silence = PcmAudioPayload::Ptr::create(adesc, prerollSamples, view);
                        _audioOutput->pushAudio(*silence);
                }

                if (_useAudioClock) {
                        Mutex::Locker lock(_clockMutex);
                        _clock = Clock::Ptr::takeOwnership(_audioOutput->createClock());
                }
        }

        // Fall back to wall clock when the audio clock isn't in play.
        {
                Mutex::Locker lock(_clockMutex);
                if (_clock.isNull()) {
                        _clock = Clock::Ptr::takeOwnership(new WallClock());
                        if (_useAudioClock) {
                                promekiInfo("SDLPlayerMediaIO: audio clock "
                                            "requested but no audio available; "
                                            "using wall clock");
                        }
                }
        }

        _sync.setTargetFrameRate(fps);
        _sync.setTargetAudioDesc(_audioDesc);
        _sync.setClock(_clock);
        _sync.reset();

        {
                Mutex::Locker lock(_clockMutex);
                startPullThread();
        }

        // Build the per-port output via the multi-port API: a single
        // sink port in a group whose accounting reports infinite
        // length and no seek capability.
        MediaIOPortGroup *group = addPortGroup("sdlplayer");
        if (group == nullptr) return Error::Invalid;
        group->setFrameRate(fps);
        group->setCanSeek(false);
        group->setFrameCount(MediaIO::FrameCountInfinite);
        if (addSink(group, mdesc) == nullptr) return Error::Invalid;
        return Error::Ok;
}

Error SDLPlayerMediaIO::executeCmd(MediaIOCommandClose &cmd) {
        (void)cmd;
        promekiDebug("SDLPlayerMediaIO::executeCmd(Close) ENTER");

        // Pause the clock before joining the pull thread.  The pull
        // thread can be parked deep inside @ref Clock::sleepUntil,
        // which is uninterruptible — @ref FrameSync::interrupt only
        // wakes @c _cv.wait inside @c pullFrame, not the lock-free
        // @c sleepUntil section.  Pausing the clock makes
        // @ref SDLAudioClock::sleepUntilNs's polling loop return
        // @ref Error::ClockPaused within ~100 us, which @c pullLoop
        // treats as a clean exit.  Without this, a stalled audio
        // device (consumed counter not advancing) leaves the pull
        // thread spinning forever and the close hangs in @c join().
        // The strand's @c pushFrame back-pressure is unwound by the
        // separate @c pushEndOfStream + @c interrupt below.
        {
                Mutex::Locker lock(_clockMutex);
                if (_clock.isValid() && !_clock->isPaused() && _clock->canPause()) {
                        (void)_clock.modify()->setPause(true);
                }
                _sync.pushEndOfStream();
                _sync.interrupt();
                stopPullThread();
        }

        if (_audioConfigured && _audioOutput != nullptr) {
                _audioOutput->close();
        }
        _audioConfigured = false;
        _sync.setClock(Clock::Ptr());
        {
                Mutex::Locker lock(_clockMutex);
                _clock.clear();
        }
        _audioDesc = AudioDesc();
        _frameRate = FrameRate();
        return Error::Ok;
}

Error SDLPlayerMediaIO::executeCmd(MediaIOCommandWrite &cmd) {
        const Frame::Ptr &frame = cmd.frame;
        if (!frame.isValid()) return Error::InvalidArgument;

        // Work begins now — the framework bills pushFrame back-pressure
        // time to the writer (matches the old pacer semantics).

        // Pre-decode compressed images on the strand so the pull
        // thread only deals with display-ready frames.  Any decode
        // failure is counted and the frame is discarded.
        Frame::Ptr outFrame = frame;
        auto       vids = frame->videoPayloads();
        if (!vids.isEmpty() && vids[0].isValid() && vids[0]->isCompressed()) {
                auto cvp = sharedPointerCast<CompressedVideoPayload>(vids[0]);
                if (cvp.isValid() && cvp->planeCount() > 0) {
                        // Spin up a one-shot decoder for this codec,
                        // configured to output RGBA8_sRGB so the widget
                        // can upload the result directly.
                        VideoCodec  codec = cvp->desc().pixelFormat().videoCodec();
                        MediaConfig decCfg;
                        decCfg.set(MediaConfig::OutputPixelFormat, PixelFormat(PixelFormat::RGBA8_sRGB));
                        auto decResult = codec.createDecoder(&decCfg);
                        if (error(decResult).isError()) {
                                promekiWarn("SDLPlayerMediaIO: createDecoder(%s) failed — "
                                            "dropping frame",
                                            codec.name().cstr());
                                noteFrameDropped(cmd.group);
                                cmd.currentFrame++;
                                cmd.frameCount = MediaIO::FrameCountInfinite;
                                return Error::Ok;
                        }
                        VideoDecoder                 *dec = value(decResult);
                        UncompressedVideoPayload::Ptr decoded;
                        if (Error de = dec->submitPayload(cvp); de.isError()) {
                                delete dec;
                                promekiWarn("SDLPlayerMediaIO: submitPayload failed — "
                                            "dropping frame");
                                noteFrameDropped(cmd.group);
                                cmd.currentFrame++;
                                cmd.frameCount = MediaIO::FrameCountInfinite;
                                return Error::Ok;
                        }
                        decoded = dec->receiveVideoPayload();
                        delete dec;
                        if (!decoded.isValid()) {
                                promekiWarn("SDLPlayerMediaIO: decode of '%s' to "
                                            "RGBA8_sRGB failed — dropping frame",
                                            cvp->desc().pixelFormat().name().cstr());
                                noteFrameDropped(cmd.group);
                                cmd.currentFrame++;
                                cmd.frameCount = MediaIO::FrameCountInfinite;
                                return Error::Ok;
                        }
                        outFrame = Frame::Ptr::create();
                        outFrame.modify()->addPayload(decoded);
                        for (const AudioPayload::Ptr &ap : frame->audioPayloads()) {
                                if (ap.isValid()) outFrame.modify()->addPayload(ap);
                        }
                        outFrame.modify()->metadata() = frame->metadata();
                }
        }

        Error err = _sync.pushFrame(outFrame);

        // Interrupt / EndOfFile come through during close and don't
        // indicate a real problem — the close path's cancelBlockingWork
        // intentionally unwinds any pending pushFrame via both signals.
        if (err.isError() && err != Error::Interrupt && err != Error::EndOfFile) {
                promekiWarn("SDLPlayerMediaIO: pushFrame returned %s", err.name().cstr());
        }

        cmd.currentFrame++;
        cmd.frameCount = MediaIO::FrameCountInfinite;
        return Error::Ok;
}

void SDLPlayerMediaIO::pullLoop() {
        while (_pullRunning.value()) {
                auto result = _sync.pullFrame();
                if (result.second().isError()) {
                        // Interrupt on shutdown and ClockPaused on
                        // @ref pause are expected clean exits; any
                        // other error is a real failure.
                        if (result.second() != Error::Interrupt && result.second() != Error::ClockPaused) {
                                promekiWarn("SDLPlayerMediaIO: pullFrame: %s", result.second().name().cstr());
                        }
                        break;
                }
                const FrameSync::PullResult &pr = result.first();
                if (!pr.frame.isValid()) continue;

                // Video → hand off to the widget for main-thread paint.
                auto pullVids = pr.frame->videoPayloads();
                if (!pullVids.isEmpty() && pullVids[0].isValid() && _widget != nullptr) {
                        auto uvp = sharedPointerCast<UncompressedVideoPayload>(pullVids[0]);
                        if (uvp.isValid()) _widget->presentVideo(uvp);
                }

                // Audio → push to the output.
                if (_audioConfigured && _audioOutput != nullptr) {
                        for (const AudioPayload::Ptr &ap : pr.frame->audioPayloads()) {
                                if (!ap.isValid()) continue;
                                const auto *uap = ap->as<PcmAudioPayload>();
                                if (uap != nullptr) _audioOutput->pushAudio(*uap);
                        }
                }
        }
}

// ---- Phase 3 introspection / negotiation overrides ----

PixelFormat SDLPlayerMediaIO::pickNativePixelFormat(const PixelFormat &offered) const {
        // The mapPixelFormat table on SDLVideoWidget is the source of
        // truth for what SDL can ingest directly.  We pick from that
        // set the format closest to @p offered, preferring same
        // colour family (YUV vs RGB) and same bit depth so the
        // upstream CSC stage stays as cheap as possible.

        const bool offerYuv = offered.isValid() && offered.colorModel().type() == ColorModel::TypeYCbCr;
        const int  offerBits = (offered.isValid() && offered.memLayout().compCount() > 0)
                                       ? static_cast<int>(offered.memLayout().compDesc(0).bits)
                                       : 8;

        if (offerYuv) {
                // YUV source — keep it in YUV.  Pick the closest of
                // the SDL-mappable YUV formats based on the offered
                // chroma subsampling.
                switch (offered.memLayout().sampling()) {
                        case PixelMemLayout::Sampling422: return PixelFormat(PixelFormat::YUV8_422_Rec709);
                        case PixelMemLayout::Sampling420:
                        case PixelMemLayout::Sampling411:
                        case PixelMemLayout::Sampling444:
                        case PixelMemLayout::SamplingUndefined:
                        default:
                                // 4:4:4 isn't natively supported by SDL — drop
                                // to 4:2:0 semi-planar, the production-typical
                                // form.  4:2:0 stays 4:2:0, 4:1:1 also lands
                                // here.
                                return PixelFormat(PixelFormat::YUV8_420_SemiPlanar_Rec709);
                }
        }

        // RGB source — match bit depth where possible.  SDL handles
        // 8-bit RGBA universally; 16-bit RGBA only on a host whose
        // endianness matches the LE/BE variant.
        if (offerBits >= 16) {
                return PixelFormat(PixelFormat::RGBA16_LE_sRGB);
        }
        return PixelFormat(PixelFormat::RGBA8_sRGB);
}

Error SDLPlayerMediaIO::describe(MediaIODescription *out) const {
        if (out == nullptr) return Error::Invalid;

        // Advertise every PixelFormat the SDL widget natively maps so
        // the planner can pick one that matches the source closely
        // (no inline CSC happens inside SDLVideoWidget any more —
        // an upstream CSC stage handles all conversion now).
        const PixelFormat::ID nativeIds[] = {
                PixelFormat::RGB8_sRGB,
                PixelFormat::BGR8_sRGB,
                PixelFormat::RGBA8_sRGB,
                PixelFormat::BGRA8_sRGB,
                PixelFormat::ARGB8_sRGB,
                PixelFormat::ABGR8_sRGB,
                PixelFormat::RGB16_LE_sRGB,
                PixelFormat::BGR16_LE_sRGB,
                PixelFormat::RGBA16_LE_sRGB,
                PixelFormat::BGRA16_LE_sRGB,
                PixelFormat::ARGB16_LE_sRGB,
                PixelFormat::ABGR16_LE_sRGB,
                PixelFormat::YUV8_422_Rec709,
                PixelFormat::YUV8_422_UYVY_Rec709,
                PixelFormat::YUV8_420_SemiPlanar_Rec709,
                PixelFormat::YUV8_420_NV21_Rec709,
                PixelFormat::YUV8_420_Planar_Rec709,
        };
        for (PixelFormat::ID id : nativeIds) {
                MediaDesc accepted;
                accepted.imageList().pushToBack(ImageDesc(Size2Du32(0, 0), PixelFormat(id)));
                out->acceptableFormats().pushToBack(accepted);
        }
        return Error::Ok;
}

Error SDLPlayerMediaIO::proposeInput(const MediaDesc &offered, MediaDesc *preferred) const {
        if (preferred == nullptr) return Error::Invalid;
        if (offered.imageList().isEmpty()) {
                // Audio-only frame (rare for SDL sink) — accept as-is.
                *preferred = offered;
                return Error::Ok;
        }

        const PixelFormat &offeredPd = offered.imageList()[0].pixelFormat();

        // Compressed sources go through the planner-inserted
        // VideoDecoder bridge; SDLPlayerMediaIO only consumes uncompressed.
        if (offeredPd.isCompressed()) {
                MediaDesc want = offered;
                want.imageList()[0].setPixelFormat(pickNativePixelFormat(offeredPd));
                *preferred = want;
                return Error::Ok;
        }

        // If the offered shape is already SDL-native we accept it
        // verbatim — the planner doesn't need to insert a CSC.
        if (SDLVideoWidget::mapPixelFormat(offeredPd) != 0) {
                *preferred = offered;
                return Error::Ok;
        }

        MediaDesc want = offered;
        want.imageList()[0].setPixelFormat(pickNativePixelFormat(offeredPd));
        *preferred = want;
        return Error::Ok;
}

PROMEKI_NAMESPACE_END
