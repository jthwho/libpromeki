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
static constexpr int64_t kRateFilterTimeConstantNs = 1000000000LL;   // 1 s

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
static constexpr int64_t kCallbackJitterNs = 20000000LL;             // 20 ms

// Monitor tick interval.  Each tick publishes a summary via the
// logger: promekiErr on any counter that indicates a genuine
// contract breach, and promekiDebug (when the runtime debug flag
// for this module is on) with the full delta + running max.
static constexpr double  kMonitorIntervalSeconds = 1.0;

// Warning thresholds reported from the monitor.  The callback-gap
// limit is intentionally loose — ALSA can legitimately batch up to
// ~70 ms under load — so the alert fires only when we're clearly
// starving the device rather than on routine jitter.
static constexpr int64_t kCallbackGapWarnNs      = 200000000LL; // 200 ms
static constexpr int64_t kBackDateWarnNs         = 100000000LL; // 100 ms

SDLAudioClock::SDLAudioClock(SDLAudioOutput *output,
                             double bytesPerSec,
                             const String &deviceName)
        : _output(output),
          _bytesPerSec(bytesPerSec)
{
        // One sample period in nanoseconds.  bytesPerSec includes all
        // channels, so bytes-per-sample (across all channels) divided
        // by bytes-per-second gives the per-sample duration.
        double bytesPerSample = static_cast<double>(
                output->desc().channels()) * sizeof(float);
        _resolutionNs = static_cast<int64_t>(
                bytesPerSample / _bytesPerSec * 1e9);
        if(_resolutionNs < 1) _resolutionNs = 1;

        // Register (or reuse) a per-device ClockDomain.  PerStream
        // epoch reflects that two SDL audio clocks run on independent
        // hardware and cannot be cross-compared.
        String domainName = "sdl.audio";
        if(!deviceName.isEmpty()) {
                domainName += ":";
                domainName += deviceName;
        }
        ClockDomain::ID id = ClockDomain::registerDomain(
                domainName,
                "SDL audio device consumption-rate clock",
                ClockEpoch::PerStream);
        _domain = ClockDomain(id);

        // Periodic health report.  Serviced from nowNs() so it runs
        // on the clock's own thread — no scheduler, no locks.  Fires
        // its first tick one monitor interval after the clock's
        // first nowNs() call (PeriodicCallback starts its own clock
        // on first service()), which keeps the warm-up transient
        // out of the initial summary.
        _monitor.setInterval(kMonitorIntervalSeconds);
        _monitor.setCallback([this]{ reportMonitor(); });
}

ClockDomain SDLAudioClock::domain() const {
        return _domain;
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
        return ClockJitter{
                Duration::fromNanoseconds(-kCallbackJitterNs),
                Duration::fromNanoseconds( kCallbackJitterNs)
        };
}

int64_t SDLAudioClock::nowNs() const {
        int64_t pushed = _output->totalBytesPushed();
        int64_t queued = static_cast<int64_t>(_output->queuedBytes());
        int64_t consumed = pushed - queued;
        if(consumed < 0) consumed = 0;

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
        if(!_audioStarted) {
                if(consumed == 0) {
                        if(_checkpointConsumed < 0) {
                                _checkpointConsumed   = 0;
                                _checkpointConsumedNs = 0;
                                _checkpointWallNs     = wallNs;
                                _checkpointRate       = 1.0;
                                _lastReportedNs       = 0;
                                _lastCallbackWallNs   = wallNs;
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
                _checkpointConsumed   = consumed;
                _checkpointConsumedNs = static_cast<int64_t>(
                        (double)consumed / _bytesPerSec * 1e9);
                _checkpointWallNs     = wallNs;
                _checkpointRate       = 1.0;
                _lastReportedNs       = _checkpointConsumedNs;
                _lastCallbackWallNs   = wallNs;
                _audioStarted         = true;
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
        if(wallDelta < 0) wallDelta = 0;
        int64_t interpolated = _checkpointConsumedNs +
                static_cast<int64_t>((double)wallDelta * _checkpointRate);

        // Live rate used when a new checkpoint is about to be
        // anchored.  Sourced from the filter once its measurement
        // window is stable; 1.0 before that.  Clamped to a sane
        // band so a wild early estimate can't drive the back-date
        // math into a runaway or divide-near-zero path.
        double liveRate = _rateEstimateStable ? _filteredRateRatio : 1.0;
        if(liveRate < 0.5 || liveRate > 2.0) liveRate = 1.0;

        int64_t reported;
        if(consumed != _checkpointConsumed) {
                ++_stats.checkpointResyncs;
                int64_t gap = wallNs - _lastCallbackWallNs;
                if(gap > _stats.maxCallbackGapNs) _stats.maxCallbackGapNs = gap;
                _lastCallbackWallNs = wallNs;

                int64_t newBaseNs = static_cast<int64_t>(
                        (double)consumed / _bytesPerSec * 1e9);
                _checkpointConsumed   = consumed;
                _checkpointConsumedNs = newBaseNs;

                if(newBaseNs >= interpolated) {
                        // Reality has caught up with (or overtaken)
                        // interpolation — snap the wall anchor to
                        // "now" and report the new, larger value.
                        _checkpointWallNs = wallNs;
                        _checkpointRate   = liveRate;
                        reported = newBaseNs;
                        ++_stats.forwardSnaps;
                } else {
                        // The raw position derived from @c consumed
                        // is behind where interpolation has already
                        // driven the reported time.  Snapping to
                        // @c newBaseNs would walk the clock backward,
                        // which downstream consumers are not allowed
                        // to see.  Back-date the wall anchor instead
                        // so the reported value stays at
                        // @c interpolated now and continues forward
                        // from there at @c rate.  The rate filter
                        // converges toward the true consumption rate
                        // over time, so the induced lead is
                        // self-correcting rather than runaway.
                        int64_t deficit = interpolated - newBaseNs;
                        // Adopt the live rate for the new
                        // checkpoint and compute the back-date
                        // offset against *that* rate.  Ceil (not
                        // floor/truncate) — the next interpolation
                        // computes @c newBaseNs + (int64_t)
                        // (wallDelta * _checkpointRate); with a
                        // floored offset, @c wallDelta*rate
                        // typically lands one ns below @c deficit,
                        // dropping the first post-resync value
                        // one ns and tripping the monotonicity
                        // clamp.  Rounding up guarantees the
                        // interpolation reaches at least
                        // @c interpolated on the next read.
                        _checkpointRate   = liveRate;
                        int64_t offset    = static_cast<int64_t>(
                                std::ceil((double)deficit / _checkpointRate));
                        _checkpointWallNs = wallNs - offset;
                        reported = interpolated;
                        ++_stats.backDates;
                        if(deficit > _stats.maxBackDateNs) {
                                _stats.maxBackDateNs = deficit;
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
        if(reported < _lastReportedNs) {
                reported = _lastReportedNs;
                ++_stats.clampedRegressions;
        }
        int64_t step = reported - _lastReportedNs;
        if(step > _stats.maxStepNs) _stats.maxStepNs = step;
        _lastReportedNs = reported;

        updateRateEstimate();

        // Warmup→stable transition: reset the three max-valued
        // observation fields so the startup-priming transient
        // (first-callback delay, first chunk landing well behind
        // extrapolation, etc.) does not permanently mask any
        // later, real misbehavior the monitor watches for.
        // Counters stay intact — they are informative throughout.
        if(_rateEstimateStable && !_prevRateStable) {
                _stats.maxStepNs         = 0;
                _stats.maxBackDateNs     = 0;
                _stats.maxCallbackGapNs  = 0;
                _monitorSnapshot.maxStepNs         = 0;
                _monitorSnapshot.maxBackDateNs     = 0;
                _monitorSnapshot.maxCallbackGapNs  = 0;
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
        const Stats &now  = _stats;
        const Stats &prev = _monitorSnapshot;

        const int64_t dUpdates  = now.updateCount        - prev.updateCount;
        const int64_t dResyncs  = now.checkpointResyncs  - prev.checkpointResyncs;
        const int64_t dFwd      = now.forwardSnaps       - prev.forwardSnaps;
        const int64_t dBack     = now.backDates          - prev.backDates;
        const int64_t dClamped  = now.clampedRegressions - prev.clampedRegressions;

        // Contract-level breach: the back-date arithmetic rounded
        // into a regression and only the final clamp saved us.
        // Monotonicity is still intact for callers, but if this
        // fires the back-date math has a bug worth investigating.
        if(dClamped > 0) {
                promekiErr("SDLAudioClock[%s]: %lld clamp-level regression(s) "
                           "in the last %.1f s — back-date math rounded into "
                           "a backward step (clamp held monotonicity)",
                           _domain.name().cstr(),
                           (long long)dClamped,
                           kMonitorIntervalSeconds);
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
        if(_rateEstimateStable &&
           now.maxCallbackGapNs > kCallbackGapWarnNs &&
           now.maxCallbackGapNs > prev.maxCallbackGapNs) {
                promekiErr("SDLAudioClock[%s]: longest callback gap is now "
                           "%lld ms (warn threshold %lld ms) — audio device "
                           "may be starving",
                           _domain.name().cstr(),
                           (long long)(now.maxCallbackGapNs / 1000000LL),
                           (long long)(kCallbackGapWarnNs    / 1000000LL));
        }

        // A sustained large back-date means interpolation keeps
        // overshooting by a lot, which typically means the rate
        // filter hasn't converged on the device's real drain rate.
        if(_rateEstimateStable &&
           now.maxBackDateNs > kBackDateWarnNs &&
           now.maxBackDateNs > prev.maxBackDateNs) {
                promekiErr("SDLAudioClock[%s]: largest back-date is now "
                           "%lld ms (warn threshold %lld ms) — rate filter "
                           "may be diverging",
                           _domain.name().cstr(),
                           (long long)(now.maxBackDateNs / 1000000LL),
                           (long long)(kBackDateWarnNs   / 1000000LL));
        }

        promekiDebug("[%s] upd=%lld rsy=%lld fwd=%lld bck=%lld clmp=%lld "
                     "mxStep=%lldns mxBck=%lldns mxGap=%lldns "
                     "rate=%.6f pub=%.6f stbl=%d",
                     _domain.name().cstr(),
                     (long long)dUpdates, (long long)dResyncs,
                     (long long)dFwd,     (long long)dBack,
                     (long long)dClamped,
                     (long long)now.maxStepNs,
                     (long long)now.maxBackDateNs,
                     (long long)now.maxCallbackGapNs,
                     _filteredRateRatio,
                     _publishedRateRatio,
                     (int)_rateEstimateStable);

        _monitorSnapshot = now;
}

void SDLAudioClock::sleepUntilNs(int64_t targetNs) {
        int64_t current = nowNs();
        if(current >= targetNs) return;

        // Convert clock-time-to-target into wall-time-to-target using
        // the filtered rate estimate.  The old 90%-then-poll heuristic
        // treated clock-ns as wall-ns, which biases the polling phase
        // upward whenever rate != 1.0 — repeated pulls accumulate that
        // bias into a ~10 ms / 6 s sawtooth on the reported wake error.
        double rate = _rateEstimateStable ? _filteredRateRatio : 1.0;
        if(rate <= 0.5) rate = 1.0;  // defensive bound

        int64_t remainingClockNs = targetNs - current;
        int64_t sleepWallNs = static_cast<int64_t>(
                (double)remainingClockNs / rate);

        // Leave a small safety margin so the initial sleep can't
        // overshoot the target; the polling loop covers the rest.
        constexpr int64_t kSleepSafetyMarginNs = 500000;  // 500 us
        if(sleepWallNs > kSleepSafetyMarginNs) {
                std::this_thread::sleep_for(std::chrono::nanoseconds(
                        sleepWallNs - kSleepSafetyMarginNs));
        }

        // Tight final approach.  100 us poll cadence — kernel sleep
        // latency dominates below that.
        while(nowNs() < targetNs) {
                std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
}

double SDLAudioClock::rateRatio() const {
        // _publishedRateRatio starts at 1.0 and is smoothly ramped
        // toward _filteredRateRatio inside updateRateEstimate once
        // the measurement window is stable.  Consumers always see
        // 1.0 until the slow ramp begins.
        return _publishedRateRatio;
}

void SDLAudioClock::updateRateEstimate() const {
        // The estimate is measured-drain-rate / nominal-drain-rate.
        // "Nominal" = _bytesPerSec (what the stream was configured
        // for).  "Measured" = actual bytes consumed over wall-clock
        // time.  We low-pass filter the per-sample ratio so per-
        // callback drain bursts don't throw off consumers.
        int64_t wallNs  = TimeStamp::now().nanoseconds();
        int64_t pushed  = _output->totalBytesPushed();
        int64_t queued  = static_cast<int64_t>(_output->queuedBytes());
        int64_t consumed = pushed - queued;
        if(consumed < 0) consumed = 0;

        // Don't establish a baseline until audio actually starts
        // flowing.  Baselining from T=0 (before the first push
        // reaches the device) folds the silent startup into the
        // average and never fully recovers.
        if(!_rateBaselineValid) {
                if(consumed <= 0) return;
                _rateBaselineWallNs    = wallNs;
                _rateBaselineConsumed  = consumed;
                _lastRateUpdateWallNs  = wallNs;
                _rateBaselineValid     = true;
                return;
        }

        int64_t deltaWallNs  = wallNs - _lastRateUpdateWallNs;
        if(deltaWallNs < _resolutionNs) return;   // too soon to re-sample

        // Use the baseline-anchored ratio (smooths over the whole
        // run rather than the last delta, which would be noisy).
        int64_t totalWallNs  = wallNs - _rateBaselineWallNs;
        int64_t totalBytes   = consumed - _rateBaselineConsumed;
        if(totalWallNs <= 0 || totalBytes <= 0) {
                _lastRateUpdateWallNs = wallNs;
                return;
        }
        double measuredRate = (double)totalBytes / ((double)totalWallNs / 1e9);
        double instantRatio = measuredRate / _bytesPerSec;

        // Exponential moving average with the configured time
        // constant.  alpha = deltaWall / tau — a cheap linearisation
        // of 1 - exp(-delta/tau) that's accurate enough here.
        double alpha = (double)deltaWallNs / (double)kRateFilterTimeConstantNs;
        if(alpha > 1.0) alpha = 1.0;
        _filteredRateRatio += alpha * (instantRatio - _filteredRateRatio);
        _lastRateUpdateWallNs = wallNs;

        // Only publish the measured ratio once we've accumulated
        // enough window to trust it — otherwise a transient early
        // underrun would leak into a consumer's drift correction
        // and trigger the very feedback loop we're trying to avoid.
        if(totalWallNs >= kRateMinMeasurementWindowNs) {
                _rateEstimateStable = true;
        }

        // Slowly drift the published ratio toward the filtered
        // estimate.  The published ratio is what @ref rateRatio
        // returns — consumers that feed it into a resampler
        // experience audio time-stretch that happens gradually
        // enough to be inaudible.
        if(_rateEstimateStable) {
                double pubAlpha = (double)deltaWallNs /
                        (double)kPublishedRatioTimeConstantNs;
                if(pubAlpha > 1.0) pubAlpha = 1.0;
                _publishedRateRatio +=
                        pubAlpha * (_filteredRateRatio - _publishedRateRatio);
        }
}

PROMEKI_NAMESPACE_END
