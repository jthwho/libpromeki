/**
 * @file      sdlaudioclock.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/sdl/sdlaudioclock.h>
#include <promeki/sdl/sdlaudiooutput.h>
#include <promeki/logger.h>
#include <promeki/timestamp.h>

#include <chrono>
#include <cmath>
#include <thread>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_DEBUG(SDLAudioClock)

// Rate-estimate low-pass filter time constant, in nanoseconds.
// Short enough to converge in a few seconds once measurement is
// active; long enough to suppress per-callback consumption jitter.
static constexpr int64_t kRateFilterTimeConstantNs = 1000000000LL; // 1 s

// Minimum wall-time window since the first non-zero consumption
// before rateRatio() returns anything other than 1.0.  A shorter
// window lets pre-roll (partial startup draining) dominate the
// estimate, which biases the ratio low.
static constexpr int64_t kRateMinMeasurementWindowNs = 2000000000LL; // 2 s

// Time constant for the *published* rate ratio.  Once the internal
// LPF's value becomes stable, the published ratio drifts toward it
// at this pace.  Audio consumers feed the published value into
// their resampler; a long constant keeps the induced pitch change
// per unit time below the threshold of audibility.
static constexpr int64_t kPublishedRatioTimeConstantNs = 30000000000LL; // 30 s

// Conservative late-biased jitter envelope.  SDL audio callbacks
// typically run in the 10–20 ms range across supported backends.
// Callers use this to size rate-estimate filters, so overestimating
// is safer than underestimating.
static constexpr int64_t kCallbackJitterNs = 20000000LL; // 20 ms

// Monitor tick interval.  Each tick publishes a summary via the
// logger: promekiErr on any counter that indicates a genuine
// contract breach, and promekiDebug (when the runtime debug flag
// for this module is on) with the full delta + running max.
static constexpr double kMonitorIntervalSeconds = 1.0;

// Warning thresholds reported from the monitor.  The callback-gap
// limit is intentionally loose — ALSA can legitimately batch up to
// ~70 ms under load — so the alert fires only when we're clearly
// starving the device rather than on routine jitter.
static constexpr int64_t kCallbackGapWarnNs = 200000000LL; // 200 ms
static constexpr int64_t kBackDateWarnNs = 100000000LL;    // 100 ms

namespace {

        // Derive float32 bytes-per-second from an AudioDesc.  This is what
        // the SDL stream drains: channels * sampleRate * 4 bytes/float.
        double bytesPerSecFrom(const AudioDesc &desc) {
                return (double)desc.sampleRate() * (double)desc.channels() * (double)sizeof(float);
        }

} // namespace

SDLAudioClock::SDLAudioClock(SDLAudioOutput *output)
    : Clock(output->clockDomain(), Duration(),
            // @ref onPause freezes raw() at the pause-time value
            // (_rawAtPause) and resets the interpolation
            // checkpoint + rate baseline on resume, so the clock
            // truly stops during pause — the base's paused-offset
            // delta comes out to zero and the output-time
            // position survives the pause unchanged.
            ClockPauseMode::PausesRawStops),
      _output(output), _bytesPerSec(bytesPerSecFrom(output->desc())) {
        // One sample period in nanoseconds.  bytesPerSec includes all
        // channels, so bytes-per-sample (across all channels) divided
        // by bytes-per-second gives the per-sample duration.
        double bytesPerSample = static_cast<double>(output->desc().channels()) * sizeof(float);
        _resolutionNs = static_cast<int64_t>(bytesPerSample / _bytesPerSec * 1e9);
        if (_resolutionNs < 1) _resolutionNs = 1;

        // Periodic health report.  Serviced from raw() so it runs
        // on the clock's own thread — no scheduler, no locks.  Fires
        // its first tick one monitor interval after the clock's
        // first read (PeriodicCallback starts its own clock on first
        // service()), which keeps the warm-up transient out of the
        // initial summary.
        _monitor.setInterval(kMonitorIntervalSeconds);
        _monitor.setCallback([this] { reportMonitor(); });
}

int64_t SDLAudioClock::resolutionNs() const {
        return _resolutionNs;
}

ClockJitter SDLAudioClock::jitter() const {
        // The reported time is strictly monotonic by construction
        // (see @ref nowNs).  That guarantee costs a possible early
        // bias: when the interpolator extrapolates past the next
        // checkpoint's raw position we keep the extrapolated value
        // rather than snap back, so the report can lead true audio
        // position by up to one callback period until the rate
        // filter converges.  The symmetric envelope below covers
        // both directions for consumers sizing filter windows.
        return ClockJitter{Duration::fromNanoseconds(-kCallbackJitterNs), Duration::fromNanoseconds(kCallbackJitterNs)};
}

Result<int64_t> SDLAudioClock::raw() const {
        const SDLAudioOutput *out = _output.data();
        if (out == nullptr) return makeError<int64_t>(Error::ObjectGone);

        // Freeze during device pause.  Acquire-load pairs with the
        // release-store in onPause so any checkpoint / baseline
        // state mutated by the resume path is visible before the
        // flag drops back to false.
        if (_devicePaused.load(std::memory_order_acquire)) {
                return makeResult<int64_t>(_rawAtPause.load(std::memory_order_relaxed));
        }
        return makeResult<int64_t>(computeRawNs());
}

int64_t SDLAudioClock::computeRawNs() const {
        const SDLAudioOutput *out = _output.data();
        if (out == nullptr) return _lastReportedNs;

        int64_t pushed = out->totalBytesPushed();
        int64_t queued = static_cast<int64_t>(out->queuedBytes());
        int64_t consumed = pushed - queued;
        if (consumed < 0) consumed = 0;

        int64_t wallNs = TimeStamp::now().nanoseconds();
        ++_stats.updateCount;

        // Pre-audio / silent startup.  SDL typically takes tens to
        // hundreds of milliseconds to open the device and fire the
        // first audio callback; during that window @c consumed stays
        // at zero.  Extrapolating wall-clock-forward from zero here
        // would lie to A/V sync and force a huge back-date when the
        // first real chunk finally lands — instead, hold at zero
        // until that first chunk arrives.  This branch runs only
        // before @c _audioStarted latches, so a transient drop of
        // @c consumed back to zero mid-stream is *not* caught here
        // (it falls through to the normal back-date path, which
        // preserves the monotonicity contract).
        if (!_audioStarted) {
                if (consumed == 0) {
                        if (_checkpointConsumed < 0) {
                                _checkpointConsumed = 0;
                                _checkpointConsumedNs = 0;
                                _checkpointWallNs = wallNs;
                                _checkpointRate = 1.0;
                                _lastReportedNs = 0;
                                _lastCallbackWallNs = wallNs;
                        }
                        updateRateEstimate();
                        _monitor.service();
                        return 0;
                }

                // First non-zero @c consumed — audio playback has
                // just started.  Seed the baseline fresh from the
                // real reading; no prior reported value higher
                // than this needs preserving because every prior
                // report during the silent window returned zero.
                _checkpointConsumed = consumed;
                _checkpointConsumedNs = static_cast<int64_t>((double)consumed / _bytesPerSec * 1e9);
                _checkpointWallNs = wallNs;
                _checkpointRate = 1.0;
                _lastReportedNs = _checkpointConsumedNs;
                _lastCallbackWallNs = wallNs;
                _audioStarted = true;
                updateRateEstimate();
                _monitor.service();
                return _checkpointConsumedNs;
        }

        // Interpolation between checkpoints uses the rate
        // snapshotted at the checkpoint itself (@c _checkpointRate),
        // not the live filter output.  Using the live value would
        // let a rate-filter update mid-interval pull the
        // extrapolated position backward — the monotonicity
        // clamp would catch that, but at the cost of stalling
        // the clock until the new rate's interpolation caught
        // back up to the last reported value.  With a snapshot,
        // a single interval is self-consistent and the new rate
        // only takes effect on the next checkpoint.
        int64_t wallDelta = wallNs - _checkpointWallNs;
        if (wallDelta < 0) wallDelta = 0;
        int64_t interpolated = _checkpointConsumedNs + static_cast<int64_t>((double)wallDelta * _checkpointRate);

        // Live rate used when a new checkpoint is about to be
        // anchored.  Sourced from the filter once its measurement
        // window is stable; 1.0 before that.  Clamped to a sane
        // band so a wild early estimate can't drive the back-date
        // math into a runaway or divide-near-zero path.
        double liveRate = _rateEstimateStable ? _filteredRateRatio : 1.0;
        if (liveRate < 0.5 || liveRate > 2.0) liveRate = 1.0;

        int64_t reported;
        if (consumed != _checkpointConsumed) {
                ++_stats.checkpointResyncs;
                int64_t gap = wallNs - _lastCallbackWallNs;
                if (gap > _stats.maxCallbackGapNs) _stats.maxCallbackGapNs = gap;
                _lastCallbackWallNs = wallNs;

                int64_t newBaseNs = static_cast<int64_t>((double)consumed / _bytesPerSec * 1e9);
                _checkpointConsumed = consumed;
                _checkpointConsumedNs = newBaseNs;

                if (newBaseNs >= interpolated) {
                        int64_t jump = newBaseNs - interpolated;
                        _checkpointWallNs = wallNs;
                        _checkpointRate = liveRate;
                        reported = newBaseNs;
                        ++_stats.forwardSnaps;
                        if (jump > 50000000LL) {
                                // > 50 ms forward snap — steady state is
                                // on the order of one callback period, so
                                // anything this large usually signals a
                                // scheduler stall or device glitch.
                                promekiWarn("SDLAudioClock[%s]: forward snap "
                                            "jump=%lld ns (interpolated=%lld "
                                            "newBaseNs=%lld)",
                                            domain().name().cstr(), (long long)jump, (long long)interpolated,
                                            (long long)newBaseNs);
                        }
                } else {
                        int64_t deficit = interpolated - newBaseNs;
                        _checkpointRate = liveRate;
                        int64_t offset = static_cast<int64_t>(std::ceil((double)deficit / _checkpointRate));
                        _checkpointWallNs = wallNs - offset;
                        reported = interpolated;
                        ++_stats.backDates;
                        if (deficit > _stats.maxBackDateNs) {
                                _stats.maxBackDateNs = deficit;
                        }
                        if (deficit > 50000000LL) {
                                // > 50 ms back-date — steady state is
                                // one callback period (~20 ms) so an
                                // outlier this large usually means SDL
                                // had a long callback stall.
                                promekiWarn("SDLAudioClock[%s]: back-date "
                                            "deficit=%lld ns (interpolated=%lld "
                                            "newBaseNs=%lld reported=%lld)",
                                            domain().name().cstr(), (long long)deficit, (long long)interpolated,
                                            (long long)newBaseNs, (long long)reported);
                        }
                }
        } else {
                reported = interpolated;
        }

        // Final monotonicity guard.  The back-date arithmetic is
        // sound but uses double precision, so a sub-ns rounding
        // error could theoretically drop the first post-resync
        // reading by 1 ns.  The contract is strict — never
        // backward — so clamp explicitly.
        if (reported < _lastReportedNs) {
                reported = _lastReportedNs;
                ++_stats.clampedRegressions;
        }
        int64_t step = reported - _lastReportedNs;
        if (step > _stats.maxStepNs) _stats.maxStepNs = step;
        _lastReportedNs = reported;

        updateRateEstimate();

        // Warmup→stable transition: reset the three max-valued
        // observation fields so the startup-priming transient
        // (first-callback delay, first chunk landing well behind
        // extrapolation, etc.) does not permanently mask any
        // later, real misbehavior the monitor watches for.
        // Counters stay intact — they are informative throughout.
        if (_rateEstimateStable && !_prevRateStable) {
                _stats.maxStepNs = 0;
                _stats.maxBackDateNs = 0;
                _stats.maxCallbackGapNs = 0;
                _monitorSnapshot.maxStepNs = 0;
                _monitorSnapshot.maxBackDateNs = 0;
                _monitorSnapshot.maxCallbackGapNs = 0;
        }
        _prevRateStable = _rateEstimateStable;

        _monitor.service();
        return reported;
}

void SDLAudioClock::resetStats() {
        _stats = Stats{};
        _monitorSnapshot = Stats{};
        _monitor.reset();
}

void SDLAudioClock::reportMonitor() const {
        const Stats &now = _stats;
        const Stats &prev = _monitorSnapshot;

        const int64_t dUpdates = now.updateCount - prev.updateCount;
        const int64_t dResyncs = now.checkpointResyncs - prev.checkpointResyncs;
        const int64_t dFwd = now.forwardSnaps - prev.forwardSnaps;
        const int64_t dBack = now.backDates - prev.backDates;
        const int64_t dClamped = now.clampedRegressions - prev.clampedRegressions;

        // Clamp regressions are expected in the catch-up window
        // right after a pause/resume: @ref onPause resets the
        // checkpoint to the true byte-derived value, which is
        // below the pre-pause wall-interpolated @c _lastReportedNs
        // until interpolation catches up (~one callback period).
        // Every raw() during that window short-circuits through
        // the clamp.  Only the contract-breach case — clamp firing
        // during steady state, which would indicate a back-date
        // arithmetic bug — deserves an error log.  Log at debug
        // otherwise.
        if (dClamped > 0) {
                promekiDebug("SDLAudioClock[%s]: %lld clamp-level "
                             "regression(s) in the last %.1f s",
                             domain().name().cstr(), (long long)dClamped, kMonitorIntervalSeconds);
        }

        // The threshold alerts below only fire once the rate
        // filter is stable.  The warmup window is dominated by
        // one-off device-priming transients that are not
        // indicative of steady-state health, and the maxes those
        // transients populate are reset the moment the filter
        // stabilises (see @ref nowNs) so comparing @c now.max to
        // @c prev.max from that point onward gives a clean
        // interval-local signal.

        // The device isn't feeding us callbacks.  That's usually a
        // starvation symptom (the stream has underrun or the caller
        // stopped pushing) and the clock will freeze on its last
        // checkpoint until feeding resumes.
        if (_rateEstimateStable && now.maxCallbackGapNs > kCallbackGapWarnNs &&
            now.maxCallbackGapNs > prev.maxCallbackGapNs) {
                promekiErr("SDLAudioClock[%s]: longest callback gap is now "
                           "%lld ms (warn threshold %lld ms) — audio device "
                           "may be starving",
                           domain().name().cstr(), (long long)(now.maxCallbackGapNs / 1000000LL),
                           (long long)(kCallbackGapWarnNs / 1000000LL));
        }

        // A sustained large back-date means interpolation keeps
        // overshooting by a lot, which typically means the rate
        // filter hasn't converged on the device's real drain rate.
        if (_rateEstimateStable && now.maxBackDateNs > kBackDateWarnNs && now.maxBackDateNs > prev.maxBackDateNs) {
                promekiErr("SDLAudioClock[%s]: largest back-date is now "
                           "%lld ms (warn threshold %lld ms) — rate filter "
                           "may be diverging",
                           domain().name().cstr(), (long long)(now.maxBackDateNs / 1000000LL),
                           (long long)(kBackDateWarnNs / 1000000LL));
        }

        promekiDebug("[%s] upd=%lld rsy=%lld fwd=%lld bck=%lld clmp=%lld "
                     "mxStep=%lldns mxBck=%lldns mxGap=%lldns "
                     "rate=%.6f pub=%.6f stbl=%d",
                     domain().name().cstr(), (long long)dUpdates, (long long)dResyncs, (long long)dFwd,
                     (long long)dBack, (long long)dClamped, (long long)now.maxStepNs, (long long)now.maxBackDateNs,
                     (long long)now.maxCallbackGapNs, _filteredRateRatio, _publishedRateRatio,
                     (int)_rateEstimateStable);

        _monitorSnapshot = now;
}

Error SDLAudioClock::sleepUntilNs(int64_t targetNs) const {
        auto currentRes = raw();
        if (isError(currentRes)) return error(currentRes);
        int64_t current = value(currentRes);
        if (current >= targetNs) return {};

        // Convert clock-time-to-target into wall-time-to-target using
        // the filtered rate estimate.  The old 90%-then-poll heuristic
        // treated clock-ns as wall-ns, which biases the polling phase
        // upward whenever rate != 1.0 — repeated pulls accumulate that
        // bias into a ~10 ms / 6 s sawtooth on the reported wake error.
        double rate = _rateEstimateStable ? _filteredRateRatio : 1.0;
        if (rate <= 0.5) rate = 1.0; // defensive bound

        int64_t remainingClockNs = targetNs - current;
        int64_t sleepWallNs = static_cast<int64_t>((double)remainingClockNs / rate);

        // Leave a small safety margin so the initial sleep can't
        // overshoot the target; the polling loop covers the rest.
        constexpr int64_t kSleepSafetyMarginNs = 500000; // 500 us
        if (sleepWallNs > kSleepSafetyMarginNs) {
                std::this_thread::sleep_for(std::chrono::nanoseconds(sleepWallNs - kSleepSafetyMarginNs));
        }

        // Tight final approach.  100 us poll cadence — kernel sleep
        // latency dominates below that.  Bail out promptly with
        // @c ClockPaused if pause happens mid-sleep so the caller can
        // retry after the resume, giving interrupts a chance to run
        // and preventing a paced loop from waking stale.
        while (true) {
                if (isPaused()) return Error::ClockPaused;
                auto r = raw();
                if (isError(r)) return error(r);
                if (value(r) >= targetNs) break;
                std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
        return {};
}

Error SDLAudioClock::onPause(bool paused) {
        SDLAudioOutput *out = _output.data();
        if (out == nullptr) return Error::ObjectGone;

        if (paused) {
                // Snapshot where raw() currently is and freeze it.
                // Setting @c _devicePaused happens BEFORE the SDL
                // pause call so there's no window where raw() sees
                // the device stopped but still runs its wall-time
                // interpolation.
                int64_t snapshot = computeRawNs();
                _rawAtPause.store(snapshot, std::memory_order_relaxed);
                _devicePaused.store(true, std::memory_order_release);

                Error e = out->setPaused(true);
                if (e.isError()) {
                        _devicePaused.store(false, std::memory_order_release);
                        return e;
                }
                return {};
        }

        // Resume.  Unpause the SDL device first so the consumed-byte
        // counter starts advancing again, then re-anchor all the
        // checkpoint / rate-estimator state to "now" before dropping
        // the paused flag — keeping the pause snapshot as the
        // authoritative position so the first post-resume raw() call
        // reports exactly @c _rawAtPause, matching the @c
        // ClockPauseMode::PausesRawStops contract the base class
        // uses to compute a zero pause-offset delta.
        Error e = out->setPaused(false);
        if (e.isError()) return e;

        const int64_t pauseValue = _rawAtPause.load(std::memory_order_relaxed);
        const int64_t wallNs = TimeStamp::now().nanoseconds();
        int64_t       pushed = out->totalBytesPushed();
        int64_t       queued = static_cast<int64_t>(out->queuedBytes());
        int64_t       consumed = pushed - queued;
        if (consumed < 0) consumed = 0;

        // Full checkpoint reset — anchor at the true byte-derived
        // value with no interpolation lead carried across pause.
        // Preserving the pre-pause lead sounds cleaner but any first-
        // post-resume callback that lands later than one callback
        // period after resume (common — SDL warms up for a few ms
        // after @c SDL_ResumeAudioStreamDevice) still adds latency
        // that the back-date machinery absorbs as additional lead,
        // and the deficit ratchets up over several pause cycles.
        //
        // With this reset, the monotonic clamp (@c _lastReportedNs
        // kept at @c pauseValue) holds reported time at pauseValue
        // for the ~one-callback-period it takes interpolation to
        // reach byteDerived + cb_period.  That's the same perceived
        // "stuck" window as the steady-state interpolation lead —
        // just located right after resume instead of distributed
        // across the stream — so the audible effect is nil while
        // the back-date deficits stay bounded at their natural
        // steady-state size.
        const int64_t byteDerivedNs = static_cast<int64_t>((double)consumed / _bytesPerSec * 1e9);

        _checkpointConsumed = consumed;
        _checkpointConsumedNs = byteDerivedNs;
        _checkpointWallNs = wallNs;
        _checkpointRate = 1.0;
        _lastCallbackWallNs = wallNs;
        if (pauseValue > _lastReportedNs) _lastReportedNs = pauseValue;

        // Rate-estimator: invalidate the baseline so the next
        // update() call anchors at the current (wallNs, consumed)
        // pair instead of the stale pre-pause pair, which would
        // span the pause interval and pull the filtered ratio
        // toward zero.  Also drop @c _rateEstimateStable so the
        // published ratio (what FrameSync's audio resampler uses)
        // stays pinned at its pre-pause value until the
        // measurement window has re-accumulated — otherwise the
        // first couple of post-resume SDL callbacks produce a
        // spuriously low instant ratio (barely any bytes
        // consumed yet over several milliseconds of wall time),
        // which drags the published ratio down and audibly
        // lowers the pitch until it recovers.
        _rateBaselineValid = false;
        _rateEstimateStable = false;
        _prevRateStable = false;
        _lastRateUpdateWallNs = wallNs;

        _devicePaused.store(false, std::memory_order_release);
        return {};
}

double SDLAudioClock::rateRatio() const {
        // _publishedRateRatio starts at 1.0 and is smoothly ramped
        // toward _filteredRateRatio inside updateRateEstimate once
        // the measurement window is stable.  Consumers always see
        // 1.0 until the slow ramp begins.
        double                     v = _publishedRateRatio;
        static thread_local double lastReported = 1.0;
        if (std::abs(v - lastReported) > 0.005) {
                promekiDebug("SDLAudioClock[%s]::rateRatio published=%.4f "
                             "filtered=%.4f stable=%d baselineValid=%d",
                             domain().name().cstr(), v, _filteredRateRatio, (int)_rateEstimateStable,
                             (int)_rateBaselineValid);
                lastReported = v;
        }
        return v;
}

void SDLAudioClock::updateRateEstimate() const {
        // The estimate is measured-drain-rate / nominal-drain-rate.
        // "Nominal" = _bytesPerSec (what the stream was configured
        // for).  "Measured" = actual bytes consumed over wall-clock
        // time.  We low-pass filter the per-sample ratio so per-
        // callback drain bursts don't throw off consumers.
        const SDLAudioOutput *out = _output.data();
        if (out == nullptr) return;
        int64_t wallNs = TimeStamp::now().nanoseconds();
        int64_t pushed = out->totalBytesPushed();
        int64_t queued = static_cast<int64_t>(out->queuedBytes());
        int64_t consumed = pushed - queued;
        if (consumed < 0) consumed = 0;

        // Don't establish a baseline until audio actually starts
        // flowing.  Baselining from T=0 (before the first push
        // reaches the device) folds the silent startup into the
        // average and never fully recovers.
        if (!_rateBaselineValid) {
                if (consumed <= 0) return;
                _rateBaselineWallNs = wallNs;
                _rateBaselineConsumed = consumed;
                _lastRateUpdateWallNs = wallNs;
                _rateBaselineValid = true;
                return;
        }

        int64_t deltaWallNs = wallNs - _lastRateUpdateWallNs;
        if (deltaWallNs < _resolutionNs) return; // too soon to re-sample

        // Use the baseline-anchored ratio (smooths over the whole
        // run rather than the last delta, which would be noisy).
        int64_t totalWallNs = wallNs - _rateBaselineWallNs;
        int64_t totalBytes = consumed - _rateBaselineConsumed;
        if (totalWallNs <= 0 || totalBytes <= 0) {
                _lastRateUpdateWallNs = wallNs;
                return;
        }
        double measuredRate = (double)totalBytes / ((double)totalWallNs / 1e9);
        double instantRatio = measuredRate / _bytesPerSec;

        // Exponential moving average with the configured time
        // constant.  alpha = deltaWall / tau — a cheap linearisation
        // of 1 - exp(-delta/tau) that's accurate enough here.
        double alpha = (double)deltaWallNs / (double)kRateFilterTimeConstantNs;
        if (alpha > 1.0) alpha = 1.0;
        _filteredRateRatio += alpha * (instantRatio - _filteredRateRatio);
        _lastRateUpdateWallNs = wallNs;

        // Only publish the measured ratio once we've accumulated
        // enough window to trust it — otherwise a transient early
        // underrun would leak into a consumer's drift correction
        // and trigger the very feedback loop we're trying to avoid.
        if (totalWallNs >= kRateMinMeasurementWindowNs) {
                if (!_rateEstimateStable) {
                        promekiDebug("SDLAudioClock[%s]: rate estimate stable "
                                     "(filtered=%.4f published=%.4f "
                                     "totalWallNs=%lld)",
                                     domain().name().cstr(), _filteredRateRatio, _publishedRateRatio,
                                     (long long)totalWallNs);
                }
                _rateEstimateStable = true;
        }

        // Slowly drift the published ratio toward the filtered
        // estimate.  The published ratio is what @ref rateRatio
        // returns — consumers that feed it into a resampler
        // experience audio time-stretch that happens gradually
        // enough to be inaudible.
        if (_rateEstimateStable) {
                double pubAlpha = (double)deltaWallNs / (double)kPublishedRatioTimeConstantNs;
                if (pubAlpha > 1.0) pubAlpha = 1.0;
                _publishedRateRatio += pubAlpha * (_filteredRateRatio - _publishedRateRatio);
        }
}

PROMEKI_NAMESPACE_END
