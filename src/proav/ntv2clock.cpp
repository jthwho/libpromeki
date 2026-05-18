/**
 * @file      ntv2clock.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/config.h>
#if PROMEKI_ENABLE_NTV2

#include <promeki/ntv2clock.h>

#include <promeki/duration.h>
#include <promeki/logger.h>
#include <promeki/ntv2device.h>
#include <promeki/thread.h>
#include <promeki/timestamp.h>

#include <ntv2card.h>

PROMEKI_NAMESPACE_BEGIN

// ============================================================================
// Per-device ClockDomain registration
// ============================================================================

const ClockDomain &Ntv2DeviceClock::domainFor(const Ntv2Device &device) {
        // Per-device ClockDomain handles cached in a process-static
        // Map.  Returning a reference into the map is safe because the
        // underlying std::map preserves reference stability on
        // insertion, and the cache outlives every consumer (process
        // lifetime).
        static Mutex                    s_mutex;
        static Map<String, ClockDomain> s_domains;

        Mutex::Locker lk(s_mutex);
        auto it = s_domains.find(device.key());
        if (it == s_domains.end()) {
                ClockDomain::ID id = ClockDomain::registerDomain(
                        device.key(),
                        String("AJA NTV2 sample-counter clock for ") + device.displayName(),
                        ClockEpoch::PerStream);
                s_domains.insert(device.key(), ClockDomain(id));
                it = s_domains.find(device.key());
        }
        return it->second;
}

// ============================================================================
// Ntv2DeviceClock
// ============================================================================

Ntv2DeviceClock::Ntv2DeviceClock(Ntv2Device *device, bool vbiFallback)
        : Clock(domainFor(*device)),
          _device(device),
          _vbiFallback(vbiFallback || !device->caps().hasAudioCounter()) {
        if (_vbiFallback) {
                // Seed the VBI tick anchor at construction so the very
                // first raw() reads a sane value instead of zero.
                _lastVbiNs.setValue(TimeStamp::now().nanoseconds());
                promekiInfo("Ntv2DeviceClock: VBI-fallback mode (device='%s')",
                            device->displayName().cstr());
        } else {
                promekiInfo("Ntv2DeviceClock: sample-counter mode (device='%s', kRegAud1Counter)",
                            device->displayName().cstr());
        }
}

Ntv2DeviceClock::Ntv2DeviceClock(const ClockDomain &domain, bool vbiFallback)
        : Clock(domain),
          _device(nullptr),
          _vbiFallback(vbiFallback) {
        if (_vbiFallback) {
                _lastVbiNs.setValue(TimeStamp::now().nanoseconds());
        }
}

Ntv2DeviceClock *Ntv2DeviceClock::createForTest(const String &testDomainName, bool vbiFallback) {
        ClockDomain::ID id = ClockDomain::registerDomain(
                testDomainName, String("NTV2 device clock test domain"), ClockEpoch::PerStream);
        return new Ntv2DeviceClock(ClockDomain(id), vbiFallback);
}

Ntv2DeviceClock::~Ntv2DeviceClock() = default;

int64_t Ntv2DeviceClock::resolutionNs() const {
        if (_vbiFallback) {
                // One frame period when known, else 1 ms as a safe
                // worst-case (the WallClock would otherwise report
                // 1 ns and overstate the precision).
                if (_frameRate.isValid()) {
                        return _frameRate.frameDuration().nanoseconds();
                }
                return 1'000'000;
        }
        // 1 / sampleRate ns per tick.  Use the cached rate without
        // taking the mutex — torn-write is harmless on a quantity
        // used for sizing reports only.
        const double rate = static_cast<double>(_sampleRateHz);
        if (rate <= 0.0) return 1'000;
        return static_cast<int64_t>(1.0e9 / rate);
}

ClockJitter Ntv2DeviceClock::jitter() const {
        const int64_t res = resolutionNs();
        return ClockJitter{Duration::fromNanoseconds(-res / 2), Duration::fromNanoseconds(res / 2)};
}

double Ntv2DeviceClock::rateRatio() const {
        // VBI fallback: the "device" clock is the host wall clock, so
        // by definition the two run at the same rate.  Return 1.0
        // without consulting the estimator (which is never updated in
        // fallback mode).
        if (_vbiFallback) return 1.0;
        return static_cast<double>(_ratePpb.value()) / 1.0e9;
}

void Ntv2DeviceClock::noteVbi(const TimeStamp &now) {
        if (_vbiFallback) _lastVbiNs.setValue(now.nanoseconds());
}

void Ntv2DeviceClock::setSampleRate(float sampleRateHz) {
        Mutex::Locker lk(_mutex);
        _sampleRateHz = sampleRateHz;
}

void Ntv2DeviceClock::setFrameRate(const FrameRate &frameRate) {
        Mutex::Locker lk(_mutex);
        _frameRate = frameRate;
}

void Ntv2DeviceClock::setCounterSourceForTest(bool (*fn)(uint32_t *, void *), void *ctx) {
        Mutex::Locker lk(_mutex);
        _testCounterFn  = fn;
        _testCounterCtx = ctx;
}

void Ntv2DeviceClock::setWallTimeSourceForTest(int64_t (*fn)(void *), void *ctx) {
        Mutex::Locker lk(_mutex);
        _testWallFn  = fn;
        _testWallCtx = ctx;
        // Reset the baseline so the next read latches a fresh anchor
        // against the new wall source — otherwise a test that injects
        // its wall source after a few reads would compare counter
        // deltas against real wall-time baselines and produce
        // nonsense ratios.
        _rateBaselineValid = false;
        _ratePpb.setValue(1'000'000'000);
}

int64_t Ntv2DeviceClock::sampleTicksToNs(uint64_t ticks) const {
        const double rate = static_cast<double>(_sampleRateHz);
        if (rate <= 0.0) return 0;
        // Compute in double for the millisecond-scale resolutions we
        // care about; the precision loss only matters past ~290 years
        // of continuous run-time, well beyond any plausible session.
        return static_cast<int64_t>(static_cast<double>(ticks) * (1.0e9 / rate));
}

MediaTimeStamp Ntv2DeviceClock::mediaTimeStampFromSamples(uint64_t samples) const {
        // The sample-rate getter takes _mutex inside the conversion;
        // wrap the call in a Locker here too so torn reads of
        // _sampleRateHz can't slip in between conversions.
        int64_t ns;
        {
                Mutex::Locker lk(_mutex);
                ns = sampleTicksToNs(samples);
        }
        TimeStamp ts{TimeStamp::Clock::time_point(std::chrono::nanoseconds(ns))};
        return MediaTimeStamp(ts, domain());
}

Result<int64_t> Ntv2DeviceClock::raw() const {
        if (_vbiFallback) {
                // Pure host-side tick — return whatever the most recent
                // VBI event observed.  Zero before any event is fine
                // because the base Clock class's monotonic clamp
                // doesn't propagate the zero downstream once a real
                // tick arrives.
                return Result<int64_t>(_lastVbiNs.value(), Error::Ok);
        }

        // Read the 32-bit counter and extend it under the mutex so the
        // wrap detection is race-free across concurrent readers.
        // GetRawAudioTimer reads kRegAud1Counter — the FPGA-resident
        // free-running 48 kHz counter that's the same one AJA's driver
        // pre-extends and reports as FRAME_STAMP::acAudioClockTimeStamp.
        // The audio-system argument is ignored by current SDKs (kept
        // for forward compat with hypothetical future cards that grow
        // multiple audio clocks).
        Mutex::Locker lk(_mutex);
        uint32_t      low = 0;
        if (_testCounterFn != nullptr) {
                if (!_testCounterFn(&low, _testCounterCtx)) {
                        return Result<int64_t>(0, Error::DeviceError);
                }
        } else {
                ULWord raw = 0;
                if (!_device->card().GetRawAudioTimer(raw)) {
                        return Result<int64_t>(0, Error::DeviceError);
                }
                low = static_cast<uint32_t>(raw);
        }

        if (!_hasShadow) {
                _lastLow   = low;
                _highBits  = 0;
                _hasShadow = true;
        } else if (low < _lastLow) {
                // 32-bit unsigned wrap — increment the high word by
                // one wrap unit (2^32).  Sample-counter wraps run
                // every ~24.8 hours at 48 kHz so we'd need a 100 ns
                // race window between two reads to mis-detect; the
                // mutex eliminates that race entirely.
                _highBits += (uint64_t(1) << 32);
                _lastLow   = low;
        } else {
                _lastLow = low;
        }
        const uint64_t fullTicks  = _highBits | uint64_t(low);
        const int64_t  counterNs  = sampleTicksToNs(fullTicks);

        // Drift estimator (Phase 6): compare device counter advance
        // against host wall-clock advance over a long window and
        // publish the ratio so downstream consumers can pull the
        // true rate instead of measuring it themselves.  Cheap —
        // most reads only update an int64_t; the LPF update path
        // runs at most once a second.  Test seam: setWallTimeSourceForTest
        // injects a synthetic wall clock so unit tests don't have to
        // sleep for the baseline window.
        const int64_t wallNs = (_testWallFn != nullptr)
                                       ? _testWallFn(_testWallCtx)
                                       : TimeStamp::now().nanoseconds();
        if (!_rateBaselineValid) {
                _rateBaselineWallNs    = wallNs;
                _rateBaselineCounterNs = counterNs;
                _lastRateUpdateWallNs  = wallNs;
                _rateBaselineValid     = true;
        } else if (wallNs - _lastRateUpdateWallNs >= kRateUpdateIntervalNs) {
                const int64_t wallDelta    = wallNs - _rateBaselineWallNs;
                const int64_t counterDelta = counterNs - _rateBaselineCounterNs;
                if (wallDelta >= kRateBaselineMinWindowNs && wallDelta > 0) {
                        // Express the ratio in parts-per-billion via
                        // explicit double arithmetic so a misbehaving
                        // counter (going backward) doesn't underflow
                        // the int math; clamped to a sane range so a
                        // first-read transient can't publish ratios
                        // far outside reality.
                        const double measured =
                                static_cast<double>(counterDelta) / static_cast<double>(wallDelta);
                        constexpr double kMinRatio = 0.95;
                        constexpr double kMaxRatio = 1.05;
                        const double clamped =
                                measured < kMinRatio
                                        ? kMinRatio
                                        : (measured > kMaxRatio ? kMaxRatio : measured);
                        const int64_t currentPpb = _ratePpb.value();
                        const int64_t measuredPpb =
                                static_cast<int64_t>(clamped * 1.0e9);
                        // (1 - α) * current + α * measured, α expressed
                        // as alpha/1000 to stay in integer math.
                        const int64_t alphaNum = static_cast<int64_t>(_kRateLpfAlphaPer1000);
                        const int64_t alphaDen = 1000;
                        const int64_t blended =
                                ((alphaDen - alphaNum) * currentPpb + alphaNum * measuredPpb)
                                / alphaDen;
                        _ratePpb.setValue(blended);
                }
                _lastRateUpdateWallNs = wallNs;
        }

        return Result<int64_t>(counterNs, Error::Ok);
}

Error Ntv2DeviceClock::sleepUntilNs(int64_t targetNs) const {
        // Coarse sleep down to ~1 ms before the deadline, then short
        // poll the counter to land within one sample.  Mirrors the
        // pattern used in ndiclock.
        constexpr int64_t kCoarseHeadroomNs = 1'000'000; // 1 ms
        for (;;) {
                Result<int64_t> nowResult = raw();
                if (nowResult.second().isError()) return nowResult.second();
                const int64_t now = nowResult.first();
                if (now >= targetNs) return Error::Ok;
                const int64_t remaining = targetNs - now;
                if (remaining > kCoarseHeadroomNs) {
                        Thread::sleepNs(remaining - kCoarseHeadroomNs);
                } else {
                        Thread::sleepUs(50);
                }
        }
}

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NTV2
