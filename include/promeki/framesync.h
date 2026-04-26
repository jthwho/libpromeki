/**
 * @file      framesync.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/frame.h>
#include <promeki/audiodesc.h>
#include <promeki/audioresampler.h>
#include <promeki/videopayload.h>
#include <promeki/pcmaudiopayload.h>
#include <promeki/framecount.h>
#include <promeki/framenumber.h>
#include <promeki/framerate.h>
#include <promeki/duration.h>
#include <promeki/error.h>
#include <promeki/result.h>
#include <promeki/mutex.h>
#include <promeki/waitcondition.h>
#include <promeki/list.h>
#include <promeki/periodiccallback.h>
#include <promeki/atomic.h>
#include <promeki/clock.h>

PROMEKI_NAMESPACE_BEGIN

class SyntheticClock;

/**
 * @brief Resyncs a source media stream to a destination clock's cadence.
 * @ingroup time
 *
 * FrameSync converts a variable-rate or differently-clocked source
 * stream into exactly one output @ref Frame per tick of a destination
 * @ref Clock.  Video frames are repeated or dropped to hit the target
 * rate; audio is resampled continuously, tracking both source rate
 * (derived from input audio timestamps) and destination drift
 * (reported by the clock's @ref Clock::rateRatio).
 *
 * @par Modes via clock choice
 *
 * FrameSync has a single operating mode.  Its behaviour depends
 * entirely on which @ref Clock it is handed:
 *
 * - @ref WallClock — paced to the system wall clock.
 * - @c SDLAudioClock — paced to an audio device's consumption rate,
 *   with @ref Clock::rateRatio feeding audio drift correction.
 * - @ref SyntheticClock — "source-only" pristine output.  Each
 *   output frame advances the clock by exactly one frame period;
 *   @ref pullFrame never blocks.  Suitable for file writers and
 *   offline conversion.
 *
 * @par Protocol
 *
 * Producer and consumer run in their own threads.  The producer
 * calls @ref pushFrame to queue a source frame.  The consumer calls
 * @ref pullFrame, which blocks on the clock until the next deadline
 * and returns a synthesised output Frame.  @ref pullFrame always
 * returns — with a repeat of the last-held frame if no input is
 * available, or an empty frame if nothing has been pushed yet.
 *
 * @par Drift correction
 *
 * All input frames are required to carry real @c MediaTimeStamp
 * metadata (MediaIO's invariant).  FrameSync derives the source's
 * actual sample rate from audio timestamp deltas, low-pass filtered
 * using a window sized against the sum of the clock's
 * @ref Clock::jitter and source jitter.  The resampler ratio is
 * <tt>sourceActualRate / (targetRate × clock->rateRatio())</tt>.
 *
 * @par Ownership
 *
 * The supplied clock is not owned by the FrameSync.  The caller
 * must keep it alive for the sync's lifetime.
 *
 * @par Thread Safety
 * Mixed.  @c pushFrame() (producer side) and @c pullFrame() (consumer
 * side) are designed to be called from different threads concurrently —
 * the input queue is internally synchronized.  Configuration and
 * lifecycle calls are not synchronized; they should be quiesced before
 * other threads interact with the instance.
 */
class FrameSync {
        public:
                /**
                 * @brief Policy used when @ref pushFrame finds the
                 *        input queue full.
                 */
                enum class InputOverflowPolicy {
                        /** Drop the oldest queued frame to make room.
                         *  Inherently lossy; right for drop-oldest
                         *  playback or when a synthetic clock pulls
                         *  on demand. */
                        DropOldest,

                        /** Block the producer until the consumer pops.
                         *  Right when the sink paces real time and
                         *  the producer should be back-pressured
                         *  instead of losing data. */
                        Block,
                };

                /**
                 * @brief Result returned by @ref pullFrame.
                 *
                 * Carries the synthesised output frame along with
                 * bookkeeping for the caller: which output frame
                 * index this is, and how many source frames were
                 * repeated or dropped to produce it.
                 */
                struct PullResult {
                                /** @brief Synthesised output frame. */
                                Frame::Ptr frame;

                                /** @brief Zero-based output frame index since reset. */
                                FrameNumber frameIndex{0};

                                /** @brief Number of times the held source frame was
                         *         repeated to produce this output. */
                                FrameCount framesRepeated{0};

                                /** @brief Number of source frames discarded to produce
                         *         this output (advanced past without use). */
                                FrameCount framesDropped{0};

                                /** @brief Wake-up error relative to the deadline. */
                                Duration error;
                };

                /** @brief Constructs an unconfigured FrameSync. */
                FrameSync();

                /**
                 * @brief Constructs a named FrameSync.
                 * @param name Identifier used as the log-line prefix.
                 */
                explicit FrameSync(const String &name);

                /** @brief Destructor.  Does not delete the supplied clock. */
                ~FrameSync();

                FrameSync(const FrameSync &) = delete;
                FrameSync &operator=(const FrameSync &) = delete;
                FrameSync(FrameSync &&) = delete;
                FrameSync &operator=(FrameSync &&) = delete;

                /** @brief Sets the log-line prefix. */
                void setName(const String &name) { _name = name; }

                /** @brief Returns the configured name. */
                const String &name() const { return _name; }

                /**
                 * @brief Sets the target output frame rate.
                 *
                 * The destination @ref Clock's @c sleepUntilNs is
                 * called with deadlines spaced at this period.  Must
                 * be set before the first @ref pullFrame.
                 */
                void setTargetFrameRate(const FrameRate &fps);

                /** @brief Returns the configured target frame rate. */
                const FrameRate &targetFrameRate() const { return _targetFrameRate; }

                /**
                 * @brief Sets the target audio description.
                 *
                 * Sample rate and channel count drive the resampler
                 * and the output sample count per pull.  Set to an
                 * invalid @ref AudioDesc to disable audio processing.
                 */
                void setTargetAudioDesc(const AudioDesc &desc);

                /** @brief Returns the configured target audio descriptor. */
                const AudioDesc &targetAudioDesc() const { return _targetAudioDesc; }

                /**
                 * @brief Sets the destination clock.
                 *
                 * Ownership is shared via the @ref Clock::Ptr.
                 * Setting a different clock mid-run requires a
                 * @ref reset to re-anchor the timeline.
                 */
                void setClock(const Clock::Ptr &clock);

                /** @brief Returns the configured clock (may be null). */
                Clock::Ptr clock() const { return _clock; }

                /**
                 * @brief Sets the maximum number of input frames to
                 *        buffer.
                 *
                 * When the producer outruns the consumer, the oldest
                 * buffered frame is dropped to make room (and counted
                 * as an overflow drop).
                 *
                 * @param capacity Max frame count (default 8).
                 */
                void setInputQueueCapacity(int capacity);

                /** @brief Returns the configured queue capacity. */
                int inputQueueCapacity() const { return _queueCapacity; }

                /**
                 * @brief Selects the overflow behaviour used when
                 *        @ref pushFrame finds the queue full.
                 *
                 * Default: @ref InputOverflowPolicy::DropOldest.
                 */
                void setInputOverflowPolicy(InputOverflowPolicy policy);

                /** @brief Returns the configured overflow policy. */
                InputOverflowPolicy inputOverflowPolicy() const { return _overflowPolicy; }

                /**
                 * @brief Resets counters and timeline.
                 *
                 * Drops any buffered input, clears the held repeat
                 * frame, resets the resampler, and re-anchors the
                 * timeline on the next @ref pullFrame.
                 */
                void reset();

                /**
                 * @brief Resets with an explicit origin.
                 *
                 * Sets the timeline origin to @p originNs in the
                 * clock's time domain.  Frame N's deadline is
                 * <tt>originNs + N × framePeriodNs</tt>.
                 *
                 * @param originNs Origin in nanoseconds from the
                 *                 clock's epoch.
                 */
                void reset(int64_t originNs);

                /**
                 * @brief Pushes a source Frame into the input queue.
                 *
                 * Thread-safe.  Blocks briefly on the internal mutex
                 * but does not block for queue space — on overflow
                 * the oldest queued frame is discarded.
                 *
                 * @param frame Source frame (must have valid essence
                 *              metadata carrying a MediaTimeStamp).
                 * @return Error::Ok on success.
                 */
                Error pushFrame(const Frame::Ptr &frame);

                /**
                 * @brief Signals end-of-stream to the consumer.
                 *
                 * Subsequent @ref pullFrame calls will return the
                 * final held frame as a repeat and then produce an
                 * empty-frame PullResult once the queue is fully
                 * drained.
                 */
                void pushEndOfStream();

                /**
                 * @brief Pulls one output frame.
                 *
                 * Blocks on the clock until the next deadline, then
                 * synthesises one output Frame from the buffered
                 * input.  With a @ref SyntheticClock the wait is a
                 * no-op — the clock is advanced by one frame per
                 * call.
                 *
                 * @param blockOnEmpty If true (default), the first
                 *        pull on an empty input queue waits on the
                 *        producer's @ref pushFrame.  If false, it
                 *        returns @c Error::TryAgain instead.  Set
                 *        this to false when the caller cannot afford
                 *        to block (e.g. the MediaIO strand, which
                 *        serves both push and pull — a blocking pull
                 *        would hold the strand against the very push
                 *        that would wake it).
                 *
                 * @return The output frame and accompanying stats.
                 */
                Result<PullResult> pullFrame(bool blockOnEmpty = true);

                /**
                 * @brief Unblocks any thread waiting in @ref pullFrame.
                 */
                void interrupt();

                /**
                 * @brief Clears a pending interrupt request.
                 *
                 * Callers that @c interrupt() to stop a pull thread
                 * and then spin up a fresh one (e.g. a pause/resume
                 * cycle) should clear the flag before the new
                 * pullFrame call so the replacement thread doesn't
                 * see a stale interrupt and exit immediately.  A
                 * no-op if no interrupt is pending.
                 */
                void clearInterrupt();

                /**
                 * @brief Drops the source-rate estimator's most
                 *        recent measurement so the next @c pushFrame
                 *        establishes a fresh baseline.
                 *
                 * Callers that pause frame delivery for an extended
                 * period should invoke this before resuming.  The
                 * first post-resume push otherwise produces a
                 * timestamp-delta vs previous-push-samples ratio
                 * that straddles the paused interval — upstream
                 * MediaTimeStamps may have advanced faster than the
                 * sample count because the queue back-pressure
                 * dropped whole chunks — and the EMA filter takes
                 * that spurious ratio as an honest measurement,
                 * silently biasing @c _sourceAudioRateHz (and with
                 * it the audio resample ratio) until it reconverges.
                 */
                void resetSourceRateEstimator();

                // ---- Stats ----

                /** @brief Total frames pushed since reset. */
                FrameCount framesIn() const { return FrameCount(_framesIn.value()); }

                /** @brief Total output frames produced since reset. */
                FrameCount framesOut() const { return FrameCount(_framesOut.value()); }

                /** @brief Total input frames repeated since reset. */
                FrameCount framesRepeated() const { return FrameCount(_framesRepeated.value()); }

                /** @brief Total input frames dropped since reset. */
                FrameCount framesDropped() const { return FrameCount(_framesDropped.value()); }

                /** @brief Total queue-overflow drops since reset. */
                FrameCount overflowDrops() const { return FrameCount(_overflowDrops.value()); }

                /** @brief Last-measured wake-up error. */
                Duration accumulatedError() const { return Duration::fromNanoseconds(_accumulatedErrorNs); }

                /** @brief Current resampler ratio (source rate / dest rate). */
                double currentResampleRatio() const { return _currentResampleRatio; }

                /** @brief Current source-rate estimate (Hz, from audio timestamps). */
                double currentSourceAudioRate() const { return _sourceAudioRateHz; }

                /** @brief Current source video-rate estimate (Hz, from video
                 *         timestamps).  Zero until at least two pushes have
                 *         been seen with video timestamps. */
                double currentSourceVideoRate() const { return _sourceVideoRateHz; }

        private:
                struct QueuedFrame {
                                Frame::Ptr frame;
                                int64_t    videoTsNs = 0; // source video timestamp
                                bool       hasVideoTs = false;
                                int64_t    audioTsNs = 0; // first audio timestamp
                                bool       hasAudioTs = false;
                };

                // Initial setup done under the mutex on demand.
                void ensureInitialised();
                void resetLocked(bool setExplicitOrigin, int64_t originNs);

                // Pull-path helpers (all assume _mutex is NOT held).
                void selectVideo(int64_t sourceTimeNs, int64_t nextSourceTimeNs, VideoPayload::Ptr &outVideo,
                                 int64_t &outRepeated, int64_t &outDropped);
                PcmAudioPayload::Ptr produceAudio(int64_t targetSamples);
                void                 updateSourceAudioRate(const PcmAudioPayload &audio, int64_t audioTsNs);
                void                 updateSourceVideoRate(int64_t videoTsNs);

                // Periodic debug log.
                void periodicDebugLog(int64_t nowNs);

                // Configuration.
                String              _name;
                FrameRate           _targetFrameRate;
                AudioDesc           _targetAudioDesc;
                Clock::Ptr          _clock;
                SyntheticClock     *_syntheticClock = nullptr; // cached downcast
                int                 _queueCapacity = 8;
                InputOverflowPolicy _overflowPolicy = InputOverflowPolicy::DropOldest;

                // Shared state (all guarded by _mutex unless noted).
                mutable Mutex _mutex;
                WaitCondition _cv;

                // Input queue.
                List<QueuedFrame> _queue;
                bool              _eos = false;
                bool              _interrupted = false;

                // Timeline state.
                bool       _started = false;
                bool       _explicitOrigin = false;
                int64_t    _originNs = 0;
                int64_t    _framePeriodNs = 0;
                FrameCount _frameCount{0};

                // Source-origin anchoring — captured on first pushed frame.
                bool    _sourceOriginValid = false;
                int64_t _sourceVideoOriginNs = 0;
                int64_t _sourceAudioOriginNs = 0;

                // Held "current" video payload (used for repeats when
                // no new input qualifies for this pull).  Carries the
                // source payload with its original metadata.
                VideoPayload::Ptr _heldVideo;
                int64_t           _heldVideoSourceTsNs = 0;
                bool              _hasHeldVideo = false;

                // FrameSyncDrop/FrameSyncRepeat metadata state.
                // _pendingFrameSyncDrops accumulates input drops across
                // pulls that emit a repeat — the spec requires
                // FrameSyncDrop to be zero on repeat outputs, so any
                // drops that occur while the output is stuck on a
                // repeat are deferred to the next fresh emit.
                // _frameSyncRepeatIndex is the position of the current
                // output within a repeat sequence (0 on fresh emit;
                // 1, 2, ... on successive repeats).
                int64_t _pendingFrameSyncDrops = 0;
                int64_t _frameSyncRepeatIndex = 0;

                // Audio resampler pipeline.
                AudioResampler::UPtr       _resampler;
                List<PcmAudioPayload::Ptr> _audioInput;               // pending input audio, FIFO
                int64_t                    _audioSamplesConsumed = 0; // of current front audio

                // Rate tracking.
                double  _sourceAudioRateHz = 0.0; // LPF'd source rate
                double  _sourceVideoRateHz = 0.0; // LPF'd source video rate
                double  _currentResampleRatio = 1.0;
                int64_t _lastAudioTsForRateNs = 0;
                int64_t _lastAudioTsSamples = 0; // samples covered by last push
                int64_t _lastVideoTsForRateNs = 0;

                // Error / logging.
                int64_t    _accumulatedErrorNs = 0;
                int64_t    _lastPeriodicLogNs = 0;
                FrameCount _frameCountAtLastLog{0};
                FrameCount _lastEmitFrameCount = FrameCount::unknown(); // debug only

                // PLL-style deadline bias.  The per-pull measured
                // wake-error feeds this LPF; the next deadline is
                // shifted earlier by this amount so any systematic
                // bias (sleep latency, clock interpolation rate
                // mismatch, per-pull work time) self-corrects.
                int64_t _deadlineBiasNs = 0;

                // Atomic stats (readable from any thread without lock).
                Atomic<int64_t> _framesIn;
                Atomic<int64_t> _framesOut;
                Atomic<int64_t> _framesRepeated;
                Atomic<int64_t> _framesDropped;
                Atomic<int64_t> _overflowDrops;
};

PROMEKI_NAMESPACE_END
