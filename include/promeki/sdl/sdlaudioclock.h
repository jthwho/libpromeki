/**
 * @file      sdl/sdlaudioclock.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/clock.h>
#include <promeki/objectbase.h>
#include <promeki/periodiccallback.h>
#include <promeki/string.h>

#include <atomic>
#include <cstdint>
#include <climits>

PROMEKI_NAMESPACE_BEGIN

class SDLAudioOutput;

/**
 * @brief Clock driven by an SDL audio device's consumption rate.
 * @ingroup sdl_core
 *
 * SDLAudioClock derives its time from how many bytes the SDL audio
 * device has consumed since the stream was opened.  Video paced
 * through this clock tracks the audio device's actual hardware rate
 * rather than the OS wall clock, keeping A/V sync locked to the
 * device.
 *
 * @par Clock domain
 *
 * The clock registers (or reuses) a per-device @ref ClockDomain at
 * construction.  The domain name is <tt>"sdl.audio:<deviceName>"</tt>
 * when a name is supplied, <tt>"sdl.audio"</tt> otherwise.  The
 * epoch is @ref ClockEpoch::PerStream — two SDL audio clocks are
 * independent time sources, not cross-stream comparable.
 *
 * @par Time model
 *
 * The clock's epoch is the moment the audio stream started (i.e.
 * when @c totalBytesPushed was 0 and the device had consumed
 * nothing).  The current time in nanoseconds is:
 *
 * @code
 * consumed = output->totalBytesPushed() - output->queuedBytes()
 * nowNs    = consumed * 1e9 / bytesPerSec
 * @endcode
 *
 * @par Rate ratio and drift
 *
 * @ref rateRatio returns the audio device's actual consumption rate
 * divided by the nominal rate the stream was configured for, low-pass
 * filtered over roughly a second.  Values above 1.0 mean the device
 * is consuming faster than nominal (clock runs fast); below 1.0
 * means it is consuming slower.  Consumers that resample audio or
 * correct for drift should feed this ratio into their resampler.
 *
 * @par Monotonicity
 *
 * @ref nowNs is strictly monotonic: consecutive reads never produce
 * a decreasing value.  SDL's consumed-byte counter advances in
 * chunks and its callback cadence jitters, so the raw phase that a
 * new checkpoint implies can land behind the value interpolation
 * has already driven the clock to.  When that happens the
 * algorithm back-dates the wall anchor instead of snapping the
 * reported time backward, preserving monotonicity.  A final clamp
 * guards against sub-nanosecond floating-point rounding.
 *
 * @par Jitter
 *
 * The monotonicity guarantee costs a possible early bias: the
 * reported time can lead the true playback position by up to one
 * callback period during transients, until the rate filter
 * converges on the device's actual drain rate.  @ref jitter
 * reports a symmetric envelope @c {-callbackPeriod, callbackPeriod}
 * so consumers that filter clock-derived rate estimates size their
 * window correctly in both directions.
 *
 * @par Sleep model
 *
 * @ref sleepUntilNs computes how many bytes need to drain to reach
 * the target time, sleeps for 90% of the estimated drain duration,
 * then polls the remainder at 250 us granularity.
 *
 * @par Ownership
 *
 * The clock does not own the SDLAudioOutput.  The caller must keep
 * the output alive for the clock's lifetime.
 */
class SDLAudioClock : public Clock {
        public:
                /**
                 * @brief Runtime counters for the clock's smoothing
                 *        machinery.
                 *
                 * Populated as @ref nowNs runs and snapshotable through
                 * @ref stats.  Cumulative since construction (or the
                 * last @ref resetStats).  Non-atomic — the clock is
                 * expected to be driven from a single thread, so
                 * external readers that call @ref stats from a
                 * different thread may see a slightly inconsistent
                 * snapshot.  Good enough for monitoring; not a
                 * synchronisation mechanism.
                 */
                struct Stats {
                        /** @brief Total @ref nowNs calls. */
                        int64_t updateCount        = 0;
                        /** @brief Consumed-byte changes observed. */
                        int64_t checkpointResyncs  = 0;
                        /** @brief Resyncs where the new raw base was
                         *         at or ahead of interpolation. */
                        int64_t forwardSnaps       = 0;
                        /** @brief Resyncs where the new raw base
                         *         landed behind interpolation and the
                         *         wall anchor had to be back-dated. */
                        int64_t backDates          = 0;
                        /** @brief Times the final monotonicity clamp
                         *         kicked in.  Should be zero —
                         *         non-zero means FP rounding in the
                         *         back-date arithmetic would have
                         *         produced a backward step but the
                         *         clamp caught it. */
                        int64_t clampedRegressions = 0;
                        /** @brief Largest forward step between two
                         *         consecutive @ref nowNs values. */
                        int64_t maxStepNs          = 0;
                        /** @brief Largest absorbed interpolation
                         *         overshoot, i.e. interpolated minus
                         *         new raw base when back-dating. */
                        int64_t maxBackDateNs      = 0;
                        /** @brief Longest wall-clock gap observed
                         *         between checkpoint resyncs. */
                        int64_t maxCallbackGapNs   = 0;
                };

                /**
                 * @brief Constructs an SDL audio clock bound to @p output.
                 *
                 * Derives the drain rate (bytes per second of float32
                 * audio) from the output's @ref AudioDesc, adopts the
                 * output's per-device @ref ClockDomain, and tracks the
                 * output's lifetime via an @ref ObjectBasePtr.  The
                 * clock's pause mode is
                 * @ref ClockPauseMode::PausesRawStops — pausing the
                 * clock pauses the SDL device, and the base-class
                 * pause accounting leaves the reported time frozen
                 * across the pause interval.
                 *
                 * Typically constructed by
                 * @ref SDLAudioOutput::createClock rather than
                 * directly.
                 *
                 * @param output The SDL audio output to derive time
                 *               from.  Must already be open.
                 */
                explicit SDLAudioClock(SDLAudioOutput *output);

                int64_t     resolutionNs() const override;
                ClockJitter jitter() const override;
                double      rateRatio() const override;

                /**
                 * @brief Returns a snapshot of the cumulative runtime
                 *        counters.
                 *
                 * The returned value is a copy; it can be safely held
                 * and compared to a later snapshot to derive per-
                 * interval deltas.  See @ref Stats for per-field
                 * semantics.
                 */
                Stats stats() const { return _stats; }

                /**
                 * @brief Resets all counters in @ref stats to zero.
                 *
                 * Also resets the internal monitor snapshot so the
                 * next periodic report publishes deltas starting
                 * from fresh.
                 */
                void resetStats();

        protected:
                Result<int64_t> raw() const override;
                Error           sleepUntilNs(int64_t targetNs) const override;
                Error           onPause(bool paused) override;

        private:
                // Core interpolation math shared between the normal
                // raw() path and the pause-snapshot path in @ref
                // onPause.  Updates checkpoint state and logs stats.
                int64_t computeRawNs() const;

                void updateRateEstimate() const;
                void reportMonitor() const;

                // When @c _devicePaused is true, @ref raw returns
                // @c _rawAtPause unchanged so wall time advancing
                // through the paused interval does not drag the
                // clock forward.  Rate estimation and interpolation
                // checkpoints are reset in @ref onPause on resume.
                mutable std::atomic<bool>    _devicePaused{false};
                mutable std::atomic<int64_t> _rawAtPause{0};

                ObjectBasePtr<SDLAudioOutput> _output;
                double          _bytesPerSec;
                int64_t         _resolutionNs;

                // Rate-estimate state.  Kept mutable so the update
                // can run inside nowNs().  Caller is expected to use
                // the clock from a single thread (the pacer or
                // FrameSync worker).
                mutable bool    _rateBaselineValid = false;
                mutable bool    _rateEstimateStable = false;
                mutable int64_t _rateBaselineWallNs = 0;
                mutable int64_t _rateBaselineConsumed = 0;
                mutable int64_t _lastRateUpdateWallNs = 0;
                mutable double  _filteredRateRatio = 1.0;

                // Slowly-ramped, externally-visible ratio.  Starts at
                // 1.0 and drifts toward the internal LPF over a long
                // time constant so that consumers (e.g. an audio
                // resampler) can't detect the moment measurement
                // stabilises as an audible pitch step.
                mutable double  _publishedRateRatio = 1.0;

                // Smooth-nowNs interpolation state.  Each advance
                // of @c consumed (an SDL audio callback) is treated
                // as a phase checkpoint; between checkpoints the
                // clock extrapolates forward at the fast-tracking
                // filtered rate.  When a checkpoint lands behind
                // the extrapolated value the wall anchor is
                // back-dated rather than snapped, keeping the
                // reported time monotonic while the rate filter
                // converges on the true consumption rate.
                mutable int64_t _checkpointConsumed = -1;
                mutable int64_t _checkpointConsumedNs = 0;
                mutable int64_t _checkpointWallNs = 0;

                // Rate snapshot at the moment the checkpoint was
                // anchored.  Interpolation between checkpoints
                // uses this value rather than the live rate
                // filter output so that rate-filter updates
                // mid-interval cannot pull the extrapolated
                // position backward and stall the clock on the
                // monotonicity clamp.  The live filter still
                // drives @ref rateRatio and still takes effect at
                // the next checkpoint update.
                mutable double  _checkpointRate = 1.0;

                // Last value returned from @ref nowNs.  Used as a
                // final clamp to defend the monotonicity contract
                // against sub-nanosecond floating-point rounding
                // in the back-date arithmetic.
                mutable int64_t _lastReportedNs = INT64_MIN;

                // Latches true the first time SDL reports a non-
                // zero @c consumed.  Gates the silent-startup
                // path: before audio has started, @ref nowNs holds
                // at zero rather than extrapolating wall time
                // forward (which would lie to A/V sync and force
                // an enormous back-date when the first real chunk
                // finally lands).  Once latched, transient drops
                // of @c consumed back to zero are treated as noise
                // and routed through the normal back-date path so
                // monotonicity is preserved.
                mutable bool    _audioStarted = false;

                // Monitoring state.  @c _stats accumulates runtime
                // counters; @c _lastCallbackWallNs is used to
                // compute callback-gap maxima; @c _monitor fires
                // once per second from inside @ref nowNs and
                // publishes an error/debug summary through the
                // logger; @c _monitorSnapshot holds the previous
                // tick's counters so the monitor can report deltas.
                // @c _prevRateStable latches the previous rate-
                // filter stable flag so the maxes can be reset once
                // at the warmup→stable transition — startup
                // transients dominate those maxes and would mask
                // later, real misbehavior if left in place.
                mutable Stats             _stats;
                mutable int64_t           _lastCallbackWallNs = 0;
                mutable PeriodicCallback  _monitor;
                mutable Stats             _monitorSnapshot;
                mutable bool              _prevRateStable = false;
};

PROMEKI_NAMESPACE_END
