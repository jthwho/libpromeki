/**
 * @file      framesync.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/framesync.h>
#include <promeki/clock.h>
#include <promeki/syntheticclock.h>
#include <promeki/audioresampler.h>
#include <promeki/mediatimestamp.h>
#include <promeki/metadata.h>
#include <promeki/image.h>
#include <promeki/logger.h>

#include <cstring>
#include <cmath>
#include <chrono>
#include <algorithm>
#include <climits>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_DEBUG(FrameSync)

// Periodic debug log interval (1 s).
static constexpr int64_t kPeriodicIntervalNs = 1000000000LL;

// Source-rate LPF time constant.  Match the clock-ratio LPF in
// SDLAudioClock so the two signals converge on the same timescale.
static constexpr double kSourceRateTimeConstantS = 1.0;

// ============================================================================
// Helpers
// ============================================================================

static bool firstAudioTimestampNs(const Audio &audio, int64_t &outNs) {
        MediaTimeStamp mts = audio.metadata()
                .get(Metadata::MediaTimeStamp)
                .get<MediaTimeStamp>();
        if(!mts.isValid()) return false;
        outNs = mts.timeStamp().nanoseconds() + mts.offset().nanoseconds();
        return true;
}

static bool firstVideoTimestampNs(const Image &image, int64_t &outNs) {
        MediaTimeStamp mts = image.metadata()
                .get(Metadata::MediaTimeStamp)
                .get<MediaTimeStamp>();
        if(!mts.isValid()) return false;
        outNs = mts.timeStamp().nanoseconds() + mts.offset().nanoseconds();
        return true;
}

static MediaTimeStamp stampAt(int64_t deadlineNs, const ClockDomain &domain) {
        TimeStamp ts;
        ts.setValue(TimeStamp::Value(std::chrono::nanoseconds(deadlineNs)));
        return MediaTimeStamp(ts, domain);
}

// ============================================================================
// Construction
// ============================================================================

FrameSync::FrameSync() {
        _framesIn.setValue(0);
        _framesOut.setValue(0);
        _framesRepeated.setValue(0);
        _framesDropped.setValue(0);
        _overflowDrops.setValue(0);
}

FrameSync::FrameSync(const String &name) : FrameSync() {
        _name = name;
}

FrameSync::~FrameSync() {
        delete _resampler;
}

// ============================================================================
// Configuration
// ============================================================================

void FrameSync::setTargetFrameRate(const FrameRate &fps) {
        Mutex::Locker lock(_mutex);
        _targetFrameRate = fps;
        _framePeriodNs = fps.isValid()
                ? fps.frameDuration().nanoseconds()
                : 0;
}

void FrameSync::setTargetAudioDesc(const AudioDesc &desc) {
        Mutex::Locker lock(_mutex);
        _targetAudioDesc = desc;
        // Teardown resampler on channel/rate change — lazy re-init on
        // the next pull.
        delete _resampler;
        _resampler = nullptr;
}

void FrameSync::setClock(Clock *clock) {
        Mutex::Locker lock(_mutex);
        _clock = clock;
        _syntheticClock = dynamic_cast<SyntheticClock *>(clock);
}

void FrameSync::setInputQueueCapacity(int capacity) {
        if(capacity < 1) capacity = 1;
        Mutex::Locker lock(_mutex);
        _queueCapacity = capacity;
        // Waking here lets any blocked producer re-check the new
        // capacity without waiting for the next pull.
        _cv.wakeAll();
}

void FrameSync::setInputOverflowPolicy(InputOverflowPolicy policy) {
        Mutex::Locker lock(_mutex);
        _overflowPolicy = policy;
        _cv.wakeAll();
}

// ============================================================================
// Reset
// ============================================================================

void FrameSync::resetLocked(bool setExplicitOrigin, int64_t originNs) {
        _queue.clear();
        _audioInput.clear();
        _audioSamplesConsumed = 0;
        _eos = false;
        _interrupted = false;

        _started = setExplicitOrigin;
        _explicitOrigin = setExplicitOrigin;
        _originNs = originNs;
        _frameCount = 0;

        _sourceOriginValid = false;
        _sourceVideoOriginNs = 0;
        _sourceAudioOriginNs = 0;

        _heldVideo = Image::Ptr();
        _hasHeldVideo = false;
        _heldVideoSourceTsNs = 0;

        _pendingFrameSyncDrops = 0;
        _frameSyncRepeatIndex  = 0;

        _sourceAudioRateHz = 0.0;
        _sourceVideoRateHz = 0.0;
        _currentResampleRatio = 1.0;
        _lastAudioTsForRateNs = 0;
        _lastAudioTsSamples = 0;
        _lastVideoTsForRateNs = 0;

        _accumulatedErrorNs = 0;
        _deadlineBiasNs = 0;
        _lastPeriodicLogNs = 0;
        _frameCountAtLastLog = 0;
        _lastEmitFrameCount = -1;

        _framesIn.setValue(0);
        _framesOut.setValue(0);
        _framesRepeated.setValue(0);
        _framesDropped.setValue(0);
        _overflowDrops.setValue(0);

        if(_resampler != nullptr) _resampler->reset();
}

void FrameSync::reset() {
        Mutex::Locker lock(_mutex);
        resetLocked(false, 0);
        promekiDebug("FrameSync[%s]: reset (target %s, clock=%s)",
                     _name.cstr(),
                     _targetFrameRate.isValid()
                        ? _targetFrameRate.toString().cstr() : "none",
                     _clock ? _clock->domain().name().cstr() : "none");
}

void FrameSync::reset(int64_t originNs) {
        Mutex::Locker lock(_mutex);
        resetLocked(true, originNs);
        promekiDebug("FrameSync[%s]: reset origin=%lld ns (target %s, clock=%s)",
                     _name.cstr(),
                     static_cast<long long>(originNs),
                     _targetFrameRate.isValid()
                        ? _targetFrameRate.toString().cstr() : "none",
                     _clock ? _clock->domain().name().cstr() : "none");
}

// ============================================================================
// Producer
// ============================================================================

Error FrameSync::pushFrame(const Frame::Ptr &frame) {
        if(!frame.isValid()) return Error::InvalidArgument;

        QueuedFrame qf;
        qf.frame = frame;
        if(!frame->imageList().isEmpty()) {
                const Image::Ptr &img = frame->imageList()[0];
                if(img.isValid()) {
                        qf.hasVideoTs = firstVideoTimestampNs(
                                *img, qf.videoTsNs);
                }
        }
        if(!frame->audioList().isEmpty()) {
                const Audio::Ptr &aud = frame->audioList()[0];
                if(aud.isValid()) {
                        qf.hasAudioTs = firstAudioTimestampNs(
                                *aud, qf.audioTsNs);
                }
        }

        {
                Mutex::Locker lock(_mutex);

                // Anchor source origin on the first valid-timestamped push.
                if(!_sourceOriginValid) {
                        if(qf.hasVideoTs) {
                                _sourceVideoOriginNs = qf.videoTsNs;
                                _sourceOriginValid = true;
                        }
                        if(qf.hasAudioTs) {
                                _sourceAudioOriginNs = qf.audioTsNs;
                                _sourceOriginValid = true;
                        }
                }

                // Feed the source video-rate estimator.  Video-only
                // streams have no audio-ts path, so this is the only
                // source-rate signal FrameSync has.
                if(qf.hasVideoTs) updateSourceVideoRate(qf.videoTsNs);

                // Apply overflow policy.
                while((int)_queue.size() >= _queueCapacity) {
                        if(_overflowPolicy == InputOverflowPolicy::DropOldest) {
                                _queue.remove(_queue.begin());
                                _overflowDrops.fetchAndAdd(1);
                                break;
                        }
                        // Block until pullFrame makes room or the
                        // sync is interrupted.
                        _cv.wait(_mutex);
                        if(_interrupted) {
                                _interrupted = false;
                                return Error::Interrupt;
                        }
                }
                _queue.pushToBack(qf);

                // Append audio to the resampler input FIFO.  The
                // resampler operates in native float, so non-native
                // inputs (e.g. PCMI_S16LE coming from the QuickTime
                // reader's @c sowt audio) are converted up front —
                // skipping conversion would cause produceAudio() to
                // discard the buffer at the @c isNative() check and
                // emit silence instead.
                if(!frame->audioList().isEmpty()) {
                        const Audio::Ptr &aud = frame->audioList()[0];
                        if(aud.isValid() && aud->samples() > 0) {
                                updateSourceAudioRate(*aud, qf.audioTsNs);
                                Audio::Ptr toQueue = aud;
                                if(!aud->isNative()) {
                                        Audio conv = aud->convertTo(AudioDesc::NativeType);
                                        if(conv.isValid()) {
                                                toQueue = Audio::Ptr::create(std::move(conv));
                                        } else {
                                                promekiWarn("FrameSync[%s]: convertTo native float failed; "
                                                            "dropping audio buffer",
                                                            _name.cstr());
                                                toQueue = Audio::Ptr();
                                        }
                                }
                                if(toQueue.isValid()) _audioInput.pushToBack(toQueue);
                        }
                }

                _framesIn.fetchAndAdd(1);
                _cv.wakeAll();
        }

        return Error::Ok;
}

void FrameSync::pushEndOfStream() {
        Mutex::Locker lock(_mutex);
        _eos = true;
        _cv.wakeAll();
}

// ============================================================================
// Source-rate tracking
// ============================================================================

void FrameSync::updateSourceAudioRate(const Audio &audio, int64_t audioTsNs) {
        // Seed from the nominal source rate the first time we see audio.
        if(_sourceAudioRateHz <= 0.0) {
                _sourceAudioRateHz = audio.desc().sampleRate();
        }

        // On subsequent pushes, compare this push's timestamp against
        // the previous push's timestamp + its sample count to derive
        // a measured rate.  audioTsNs is the arrival time of the
        // first sample in this push; the previous push covered
        // _lastAudioTsSamples samples starting at
        // _lastAudioTsForRateNs.  If timestamps are monotone and
        // contiguous, (deltaTs) should equal (_lastAudioTsSamples /
        // sampleRate).  Any mismatch feeds the LPF.
        if(_lastAudioTsForRateNs != 0 && _lastAudioTsSamples > 0) {
                double deltaSec = (double)(audioTsNs - _lastAudioTsForRateNs) / 1e9;
                if(deltaSec > 0.0) {
                        double measured = (double)_lastAudioTsSamples / deltaSec;
                        // Exponential moving average.  alpha based on
                        // deltaSec / timeConstant, clamped.
                        double alpha = deltaSec / kSourceRateTimeConstantS;
                        if(alpha > 1.0) alpha = 1.0;
                        if(alpha < 0.0) alpha = 0.0;
                        _sourceAudioRateHz +=
                                alpha * (measured - _sourceAudioRateHz);
                }
        }

        _lastAudioTsForRateNs = audioTsNs;
        _lastAudioTsSamples   = audio.samples();
}

void FrameSync::updateSourceVideoRate(int64_t videoTsNs) {
        // Seed from the target rate on first sight.  We have no
        // better prior; the estimator converges to the true rate
        // within a few frames.
        if(_sourceVideoRateHz <= 0.0) {
                _sourceVideoRateHz = _targetFrameRate.isValid()
                        ? _targetFrameRate.toDouble() : 0.0;
        }

        // One frame per push, so the delta between two consecutive
        // push timestamps is one source frame period.  Same LPF
        // structure as the audio estimator; identical time constant
        // keeps the two signals on a common timescale.
        if(_lastVideoTsForRateNs != 0) {
                double deltaSec = (double)(videoTsNs - _lastVideoTsForRateNs) / 1e9;
                if(deltaSec > 0.0) {
                        double measured = 1.0 / deltaSec;
                        double alpha = deltaSec / kSourceRateTimeConstantS;
                        if(alpha > 1.0) alpha = 1.0;
                        if(alpha < 0.0) alpha = 0.0;
                        _sourceVideoRateHz +=
                                alpha * (measured - _sourceVideoRateHz);
                }
        }

        _lastVideoTsForRateNs = videoTsNs;
}

// ============================================================================
// Pull path
// ============================================================================

void FrameSync::selectVideo(int64_t sourceTimeNs,
                            int64_t /*nextSourceTimeNs*/,
                            Image::Ptr &outImage,
                            int64_t &outRepeated,
                            int64_t &outDropped)
{
        // Each pull owns the source-time slot centred on
        // @p sourceTimeNs.  We pick the best-matching frame in the
        // queue — the one whose relTs is closest to sourceTimeNs —
        // and drop any older frame whose successor is a closer
        // match.  A frame whose relTs is more than half a frame
        // period past sourceTimeNs is "future": it belongs to a
        // later pull, so we leave it queued and emit a repeat.
        //
        // The nearest-neighbour-with-lookahead semantics is a
        // deliberate move away from a fixed "late" threshold.  A
        // fixed threshold (e.g. half a period before sourceTimeNs)
        // breaks on real-world sources the moment the producer's
        // steady-state delivery lag reaches the threshold — for
        // example a UVC webcam whose kernel capture timestamp
        // trails user-space dequeue time by 15–25 ms at 30 fps.
        // With the lookahead rule the queue front is always the
        // emit candidate; it is only demoted to a drop when a
        // better (later) candidate has already arrived.  Matched-
        // rate sources with a steady producer lag therefore emit
        // every pull, and rate-mismatched sources still drop or
        // repeat the correct amount because the nearest neighbour
        // is still correct.
        //
        // Frames without a video timestamp take the legacy path —
        // they are treated as matching the current window.  MediaIO
        // guarantees stamps on real inputs so this is a fallback.
        const int64_t halfPeriod = _framePeriodNs / 2;
        const int64_t upperBound = sourceTimeNs + halfPeriod;

        auto absDelta = [](int64_t a, int64_t b) {
                int64_t d = a - b;
                return d < 0 ? -d : d;
        };

        bool emitted = false;
        while(!_queue.isEmpty() && !emitted) {
                const QueuedFrame &front = _queue.front();
                int64_t frontRelTs = 0;
                const bool frontHasTs = front.hasVideoTs;
                if(frontHasTs) {
                        frontRelTs = front.videoTsNs - _sourceVideoOriginNs;
                        if(frontRelTs > upperBound) break; // future
                }

                // Lookahead: does the queue carry a later candidate
                // that is strictly closer to sourceTimeNs than the
                // front?  If so, the front has been superseded and
                // should be dropped.  A later candidate that is
                // itself in the future doesn't count — its slot is
                // a later pull, not this one.
                bool hasBetter = false;
                if(frontHasTs && _queue.size() >= 2) {
                        const QueuedFrame &next = _queue[1];
                        if(next.hasVideoTs) {
                                int64_t nextRelTs =
                                        next.videoTsNs - _sourceVideoOriginNs;
                                if(nextRelTs <= upperBound &&
                                   absDelta(nextRelTs, sourceTimeNs) <
                                   absDelta(frontRelTs, sourceTimeNs)) {
                                        hasBetter = true;
                                }
                        }
                }

                Image::Ptr img;
                if(!front.frame->imageList().isEmpty()) {
                        img = front.frame->imageList()[0];
                }
                _queue.remove(_queue.begin());

                if(img.isValid()) {
                        _heldVideo = img;
                        _hasHeldVideo = true;
                        _heldVideoSourceTsNs = frontRelTs;
                }

                if(hasBetter) {
                        // Drop this frame; the later queued frame
                        // is a better match.  Keep the dropped
                        // frame's image as the held "current" view
                        // so any subsequent repeat shows the
                        // freshest known frame rather than holding
                        // a stale one.
                        outDropped++;
                        continue;
                }

                _lastEmitFrameCount = _frameCount;
                emitted = true;
        }

        if(!emitted) outRepeated = 1;
        outImage = _heldVideo;
}

Audio::Ptr FrameSync::produceAudio(int64_t targetSamples) {
        if(!_targetAudioDesc.isValid() || targetSamples <= 0) {
                return Audio::Ptr();
        }
        const unsigned int channels = _targetAudioDesc.channels();
        const float       targetRate = _targetAudioDesc.sampleRate();
        if(channels == 0 || targetRate <= 0.0f) return Audio::Ptr();

        // Lazy init the resampler once we know channel count.
        if(_resampler == nullptr) {
                _resampler = new AudioResampler();
                Error err = _resampler->setup(channels);
                if(err.isError()) {
                        promekiWarn("FrameSync[%s]: resampler setup failed",
                                    _name.cstr());
                        delete _resampler;
                        _resampler = nullptr;
                        return Audio::Ptr();
                }
        }

        // Update the ratio.  ratio = outputRate / inputRate where
        // outputRate is the destination-rate adjusted for clock
        // drift, and inputRate is the measured source rate.  Seed
        // with nominal if no measurement yet.
        double sourceRate = (_sourceAudioRateHz > 0.0)
                ? _sourceAudioRateHz
                : (double)targetRate;
        double destRate = (double)targetRate;
        if(_clock != nullptr) destRate *= _clock->rateRatio();
        double ratio = destRate / sourceRate;
        if(ratio <= 0.0) ratio = 1.0;
        _currentResampleRatio = ratio;
        _resampler->setRatio(ratio);

        // Allocate output.  The full sample count is always produced
        // (we pad with silence if the resampler or input runs dry).
        // The internal resampler operates in native float, and the
        // pull side casts via @c data<float>() — so the output buffer
        // is allocated as native float regardless of @c _targetAudioDesc 's
        // declared sample format.  Downstream consumers that need a
        // different sample format must convert via @c Audio::convertTo.
        AudioDesc outDesc(AudioDesc::NativeType, targetRate, channels);
        Audio out(outDesc, targetSamples);
        out.resize(targetSamples);
        out.zero();
        float *outPtr = out.data<float>();

        long outWritten = 0;
        while(outWritten < (long)targetSamples) {
                if(_audioInput.isEmpty()) break;
                const Audio::Ptr &front = _audioInput.front();
                if(!front.isValid() || front->samples() == 0 ||
                   !front->isNative()) {
                        _audioInput.remove(_audioInput.begin());
                        _audioSamplesConsumed = 0;
                        continue;
                }

                const float *inPtr = front->data<float>();
                long inAvail = (long)front->samples() - (long)_audioSamplesConsumed;
                if(inAvail <= 0) {
                        _audioInput.remove(_audioInput.begin());
                        _audioSamplesConsumed = 0;
                        continue;
                }

                long inUsed = 0;
                long outGen = 0;
                Error err = _resampler->process(
                        inPtr + (long)_audioSamplesConsumed * channels,
                        inAvail,
                        outPtr + outWritten * channels,
                        (long)targetSamples - outWritten,
                        inUsed, outGen, false);
                if(err.isError()) {
                        promekiWarn("FrameSync[%s]: resampler process failed",
                                    _name.cstr());
                        break;
                }

                _audioSamplesConsumed += (size_t)inUsed;
                outWritten += outGen;

                // If libsamplerate consumed nothing AND produced
                // nothing we would spin forever — break out.
                if(inUsed == 0 && outGen == 0) break;

                // If we drained the front buffer, pop it.
                if((size_t)_audioSamplesConsumed >= front->samples()) {
                        _audioInput.remove(_audioInput.begin());
                        _audioSamplesConsumed = 0;
                }
        }

        return Audio::Ptr::create(out);
}

Result<FrameSync::PullResult> FrameSync::pullFrame(bool blockOnEmpty) {
        PullResult result;
        if(_clock == nullptr) {
                return Result<PullResult>(result, Error::Invalid);
        }
        if(!_targetFrameRate.isValid() || _framePeriodNs <= 0) {
                return Result<PullResult>(result, Error::Invalid);
        }

        int64_t currentIndex = 0;
        int64_t deadlineNs   = 0;
        {
                Mutex::Locker lock(_mutex);
                if(_interrupted) {
                        _interrupted = false;
                        return Result<PullResult>(result, Error::Interrupt);
                }

                // Anchor origin on the first pull if no explicit
                // origin was supplied.  We defer anchoring until the
                // producer has actually queued a frame so the pull
                // timeline doesn't begin earlier than the source
                // (otherwise the first few pulls are wasted as
                // repeats before the stream catches up).
                //
                // Callers that cannot afford to block (the MediaIO
                // strand — see @ref pullFrame docs) pass
                // @c blockOnEmpty=false and receive @c TryAgain
                // instead.  Callers on a dedicated thread (e.g.
                // SDLPlayerTask::pullLoop) keep the legacy blocking
                // behaviour so they can wait indefinitely for the
                // first push.
                if(!_started) {
                        if(blockOnEmpty) {
                                while(_queue.isEmpty() && !_eos &&
                                      !_interrupted) {
                                        _cv.wait(_mutex);
                                }
                                if(_interrupted) {
                                        _interrupted = false;
                                        return Result<PullResult>(
                                                result, Error::Interrupt);
                                }
                        } else if(_queue.isEmpty()) {
                                if(_eos) return Result<PullResult>(
                                                result, Error::EndOfFile);
                                return Result<PullResult>(
                                        result, Error::TryAgain);
                        }
                        _originNs = _clock->nowNs();
                        _started = true;
                        _lastPeriodicLogNs = _originNs;
                        _frameCountAtLastLog = 0;
                }

                currentIndex = _frameCount;
                // Deadline is the ideal schedule minus the running
                // bias from prior wake-errors.  This absorbs any
                // systematic offset (kernel sleep latency, clock
                // interpolation mismatch, per-pull work time) so
                // the realised pull cadence tracks the target rate.
                deadlineNs   = _originNs +
                        _targetFrameRate.cumulativeTicks(
                                1000000000LL, currentIndex) -
                        _deadlineBiasNs;
        }

        // Block on the clock — no-op for SyntheticClock.
        _clock->sleepUntilNs(deadlineNs);

        // Build the output frame.
        Image::Ptr outImage;
        int64_t    outRepeated = 0;
        int64_t    outDropped  = 0;
        int64_t    audioTargetSamples = 0;
        if(_targetAudioDesc.isValid()) {
                audioTargetSamples = (int64_t)_targetFrameRate.samplesPerFrame(
                        (int64_t)_targetAudioDesc.sampleRate(), currentIndex);
        }
        Audio::Ptr outAudio;
        int64_t    actualNs = 0;
        int64_t    frameSyncDrop = 0;
        int64_t    frameSyncRepeat = 0;
        {
                Mutex::Locker lock(_mutex);
                if(_interrupted) {
                        _interrupted = false;
                        return Result<PullResult>(result, Error::Interrupt);
                }

                int64_t sourceTimeNs = _targetFrameRate.cumulativeTicks(
                        1000000000LL, currentIndex);
                int64_t nextSourceTimeNs = _targetFrameRate.cumulativeTicks(
                        1000000000LL, currentIndex + 1);
                selectVideo(sourceTimeNs, nextSourceTimeNs,
                            outImage, outRepeated, outDropped);
                outAudio = produceAudio(audioTargetSamples);

                // Compute FrameSyncDrop/FrameSyncRepeat values for
                // this output.  Drops that occur while the output is
                // stuck on a repeat are held in _pendingFrameSyncDrops
                // and flushed on the next fresh emit so the reported
                // drop count is attached to the exact output that
                // represents the "resumed new input" — per the spec,
                // FrameSyncDrop is always zero on repeat frames.
                _pendingFrameSyncDrops += outDropped;
                if(outRepeated > 0) {
                        _frameSyncRepeatIndex++;
                        frameSyncRepeat = _frameSyncRepeatIndex;
                        // frameSyncDrop stays 0 on repeats.
                } else {
                        frameSyncDrop = _pendingFrameSyncDrops;
                        _pendingFrameSyncDrops = 0;
                        _frameSyncRepeatIndex  = 0;
                }

                actualNs = _clock->nowNs();
                _accumulatedErrorNs = actualNs - deadlineNs;

                // LPF the per-pull error into the deadline bias.
                // At steady state, bias converges to the average
                // wake-error so that the realised pull cadence
                // matches the ideal schedule.  Alpha = 1/16 gives
                // a ~0.5 s half-life at 30 fps.
                _deadlineBiasNs +=
                        (_accumulatedErrorNs - _deadlineBiasNs) / 16;

                _frameCount++;
                periodicDebugLog(actualNs);
                // Wake any producer blocked on a full queue.
                _cv.wakeAll();
        }

        // Stamp essences with output timestamps in the destination domain.
        MediaTimeStamp outStamp = stampAt(deadlineNs, _clock->domain());

        Frame::Ptr outFrame = Frame::Ptr::create();
        if(outImage.isValid()) {
                // Clone at Ptr level so our output stamp doesn't
                // overwrite the source Image's metadata when the
                // same image is emitted across multiple repeats.
                Image fresh = *outImage;
                fresh.metadata().set(Metadata::MediaTimeStamp, outStamp);
                fresh.metadata().set(Metadata::PresentationTime, outStamp);
                outFrame.modify()->imageList().pushToBack(
                        Image::Ptr::create(fresh));
        }
        if(outAudio.isValid()) {
                outAudio.modify()->metadata().set(
                        Metadata::MediaTimeStamp, outStamp);
                outAudio.modify()->metadata().set(
                        Metadata::PresentationTime, outStamp);
                outFrame.modify()->audioList().pushToBack(outAudio);
        }
        outFrame.modify()->metadata().set(Metadata::MediaTimeStamp, outStamp);
        outFrame.modify()->metadata().set(Metadata::FrameNumber, currentIndex);
        // FrameSyncDrop/FrameSyncRepeat are declared as int32 in the
        // Metadata schema; clamp here so an unexpectedly large
        // accumulated drop count (e.g. a badly stalled pipeline) does
        // not silently wrap or truncate on the 64→32-bit cast.
        constexpr int64_t kMaxS32 = static_cast<int64_t>(INT32_MAX);
        outFrame.modify()->metadata().set(Metadata::FrameSyncDrop,
                static_cast<int32_t>(std::min(frameSyncDrop, kMaxS32)));
        outFrame.modify()->metadata().set(Metadata::FrameSyncRepeat,
                static_cast<int32_t>(std::min(frameSyncRepeat, kMaxS32)));

        // Advance the synthetic clock in lockstep so its nowNs() tracks
        // the number of emitted output frames.
        if(_syntheticClock != nullptr) {
                _syntheticClock->advance(1);
        }

        if(outRepeated > 0) _framesRepeated.fetchAndAdd(outRepeated);
        if(outDropped  > 0) _framesDropped.fetchAndAdd(outDropped);
        _framesOut.fetchAndAdd(1);

        result.frame           = outFrame;
        result.frameIndex      = currentIndex;
        result.framesRepeated  = outRepeated;
        result.framesDropped   = outDropped;
        result.error           = Duration::fromNanoseconds(actualNs - deadlineNs);
        return Result<PullResult>(result, Error::Ok);
}

void FrameSync::interrupt() {
        Mutex::Locker lock(_mutex);
        _interrupted = true;
        _cv.wakeAll();
}

// ============================================================================
// Periodic debug log
// ============================================================================

void FrameSync::periodicDebugLog(int64_t nowNs) {
        if(nowNs - _lastPeriodicLogNs < kPeriodicIntervalNs) return;

        int64_t elapsedFrames = _frameCount - _frameCountAtLastLog;
        double  elapsedSec    = (double)(nowNs - _lastPeriodicLogNs) / 1e9;
        double  actualFps     = (elapsedSec > 0.0)
                ? (double)elapsedFrames / elapsedSec : 0.0;

        promekiDebug("[%s] out=%lld fps=%.2f in=%lld rpt=%lld drp=%lld ovf=%lld "
                     "srcVHz=%.2f srcAHz=%.2f ratio=%.6f accErr=%.3fms bias=%.3fms "
                     "clk=%s rRatio=%.6f",
                     _name.cstr(),
                     static_cast<long long>(_frameCount),
                     actualFps,
                     static_cast<long long>(_framesIn.value()),
                     static_cast<long long>(_framesRepeated.value()),
                     static_cast<long long>(_framesDropped.value()),
                     static_cast<long long>(_overflowDrops.value()),
                     _sourceVideoRateHz,
                     _sourceAudioRateHz,
                     _currentResampleRatio,
                     (double)_accumulatedErrorNs / 1e6,
                     (double)_deadlineBiasNs / 1e6,
                     _clock ? _clock->domain().name().cstr() : "none",
                     _clock ? _clock->rateRatio() : 1.0);

        _lastPeriodicLogNs   = nowNs;
        _frameCountAtLastLog = _frameCount;
}

PROMEKI_NAMESPACE_END
