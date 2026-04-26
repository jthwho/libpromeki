/**
 * @file      mediaiotask_nullpacing.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <chrono>

#include <promeki/mediaiotask_nullpacing.h>
#include <promeki/enums.h>
#include <promeki/framerate.h>
#include <promeki/logger.h>
#include <promeki/mediaconfig.h>
#include <promeki/mediadesc.h>
#include <promeki/rational.h>
#include <promeki/variantspec.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_DEBUG(MediaIOTask_NullPacing)

PROMEKI_REGISTER_MEDIAIO(MediaIOTask_NullPacing)

namespace {

// Resolves a Rational target rate (from the config) plus the upstream
// MediaDesc rate (from CmdOpen.pendingMediaDesc) into a single
// FrameRate.  The configured rate wins when its numerator is > 0;
// the descriptor rate is the fallback.  Returns an invalid FrameRate
// when both are absent, which the caller turns into Error::InvalidArgument.
FrameRate resolveTargetRate(const Rational<int> &configured,
                            const FrameRate &fromDesc) {
        if(configured.numerator() > 0 && configured.denominator() > 0) {
                FrameRate::RationalType r(
                        static_cast<unsigned int>(configured.numerator()),
                        static_cast<unsigned int>(configured.denominator()));
                return FrameRate(r);
        }
        if(fromDesc.isValid()) return fromDesc;
        return FrameRate();
}

}  // namespace

// ---------------------------------------------------------------------------
// MediaIO factory descriptor
// ---------------------------------------------------------------------------

MediaIO::FormatDesc MediaIOTask_NullPacing::formatDesc() {
        return {
                "NullPacing",
                "Null Pacing Sink",
                "Frame-pacing null sink (consumes and discards frames "
                "at a target rate).",
                {},     // No file extensions — pure sink.
                false,  // canBeSource
                true,   // canBeSink
                false,  // canBeTransform
                []() -> MediaIOTask * {
                        return new MediaIOTask_NullPacing();
                },
                []() -> MediaIO::Config::SpecMap {
                        MediaIO::Config::SpecMap specs;
                        auto s = [&specs](MediaConfig::ID id, const Variant &def) {
                                const VariantSpec *gs = MediaConfig::spec(id);
                                specs.insert(id, gs ? VariantSpec(*gs).setDefault(def)
                                                    : VariantSpec().setDefault(def));
                        };
                        s(MediaConfig::NullPacingMode,
                          promeki::NullPacingMode::Wallclock);
                        s(MediaConfig::NullPacingTargetFps,
                          Rational<int>(0, 1));
                        s(MediaConfig::NullPacingBurnTimings, false);
                        return specs;
                },
                []() -> Metadata { return Metadata(); }
        };
}

// ---------------------------------------------------------------------------
// Construction / lifecycle
// ---------------------------------------------------------------------------

MediaIOTask_NullPacing::MediaIOTask_NullPacing() = default;
MediaIOTask_NullPacing::~MediaIOTask_NullPacing() = default;

NullPacingSnapshot MediaIOTask_NullPacing::snapshot() const {
        Mutex::Locker lk(_stateMutex);
        return _stats;
}

Error MediaIOTask_NullPacing::executeCmd(MediaIOCommandOpen &cmd) {
        if(cmd.mode != MediaIO::Sink) return Error::NotSupported;

        const MediaIO::Config &cfg = cmd.config;

        Error modeErr;
        Enum modeEnum = cfg.get(MediaConfig::NullPacingMode)
                .asEnum(promeki::NullPacingMode::Type, &modeErr);
        if(modeErr.isError() || !modeEnum.hasListedValue()) {
                promekiErr("MediaIOTask_NullPacing: invalid NullPacingMode");
                return Error::InvalidArgument;
        }
        _mode = promeki::NullPacingMode(modeEnum.value());

        Rational<int> configuredFps =
                cfg.get(MediaConfig::NullPacingTargetFps).get<Rational<int>>();
        _targetRate = resolveTargetRate(configuredFps,
                                        cmd.pendingMediaDesc.frameRate());

        if(_mode.value() == promeki::NullPacingMode::Wallclock.value()) {
                if(!_targetRate.isValid()) {
                        promekiErr("MediaIOTask_NullPacing: Wallclock mode "
                                   "requires NullPacingTargetFps > 0/1 or "
                                   "a valid upstream MediaDesc::frameRate");
                        return Error::InvalidArgument;
                }
                _period = _targetRate.frameDuration();
        } else {
                _period = Duration();
        }

        _burnTimings = cfg.getAs<bool>(MediaConfig::NullPacingBurnTimings, false);

        _hasLastConsumed = false;
        _lastConsumed    = TimeStamp();
        _nextDeadline    = TimeStamp();
        {
                Mutex::Locker lk(_stateMutex);
                _stats = NullPacingSnapshot{};
        }

        promekiInfo("MediaIOTask_NullPacing: opened mode=%s "
                    "targetFps=%.4f burnTimings=%s",
                    _mode.valueName().cstr(),
                    _targetRate.isValid() ? _targetRate.toDouble() : 0.0,
                    _burnTimings ? "yes" : "no");

        _isOpen        = true;
        cmd.canSeek    = false;
        cmd.frameCount = MediaIO::FrameCountInfinite;
        return Error::Ok;
}

Error MediaIOTask_NullPacing::executeCmd(MediaIOCommandClose &cmd) {
        (void)cmd;
        if(!_isOpen) return Error::Ok;

        NullPacingSnapshot snap;
        {
                Mutex::Locker lk(_stateMutex);
                snap = _stats;
        }
        const double avgUs = snap.latencySamples > 0
                ? static_cast<double>(snap.totalLatencyUs)
                  / static_cast<double>(snap.latencySamples)
                : 0.0;
        promekiInfo("MediaIOTask_NullPacing: closed — consumed %lld, "
                    "dropped %lld, avgLatencyUs %.1f, peakLatencyUs %lld",
                    static_cast<long long>(snap.framesConsumed),
                    static_cast<long long>(snap.framesDropped),
                    avgUs,
                    static_cast<long long>(snap.peakLatencyUs));

        _isOpen          = false;
        _hasLastConsumed = false;
        return Error::Ok;
}

Error MediaIOTask_NullPacing::executeCmd(MediaIOCommandWrite &cmd) {
        if(!_isOpen) return Error::NotOpen;
        if(!cmd.frame.isValid()) return Error::InvalidArgument;
        stampWorkBegin();

        const TimeStamp arrival = TimeStamp::now();
        bool consume = false;

        if(_mode.value() == promeki::NullPacingMode::Free.value()) {
                consume = true;
        } else {
                if(!_hasLastConsumed) {
                        // First frame after open — anchor the
                        // wallclock cadence on this arrival so the
                        // sink starts producing immediately rather
                        // than waiting one full period.  Subsequent
                        // ticks march forward by _period from here.
                        _nextDeadline = arrival + _period;
                        consume       = true;
                } else if(arrival.value() >= _nextDeadline.value()) {
                        consume = true;
                        // Catch-up rule: if the arrival is well past
                        // the deadline (e.g. the upstream was paused),
                        // re-anchor on the arrival rather than letting
                        // the deadline drift far enough to consume a
                        // burst of subsequent frames without pacing.
                        TimeStamp candidate = _nextDeadline + _period;
                        if(candidate.value() <= arrival.value()) {
                                _nextDeadline = arrival + _period;
                        } else {
                                _nextDeadline = candidate;
                        }
                } else {
                        consume = false;
                }
        }

        // Drain stamp = the moment we decided what to do with the
        // frame, used for the per-frame in-sink latency.  Both
        // consume and drop count toward the latency average since
        // both branches "held" the frame for a measurable interval.
        const TimeStamp drain = TimeStamp::now();
        const Duration  held  = Duration::fromNanoseconds(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                        drain.value() - arrival.value()).count());
        const int64_t   heldUs = held.microseconds();

        if(consume) {
                if(_burnTimings && _hasLastConsumed) {
                        const Duration sincePrev = Duration::fromNanoseconds(
                                std::chrono::duration_cast<std::chrono::nanoseconds>(
                                        arrival.value() - _lastConsumed.value()).count());
                        const int64_t periodUs   = _period.microseconds();
                        const int64_t measuredUs = sincePrev.microseconds();
                        const int64_t jitterUs   = measuredUs - periodUs;
                        promekiDebug(
                                "MediaIOTask_NullPacing: consume "
                                "periodUs=%lld measuredUs=%lld "
                                "jitterUs=%lld heldUs=%lld",
                                static_cast<long long>(periodUs),
                                static_cast<long long>(measuredUs),
                                static_cast<long long>(jitterUs),
                                static_cast<long long>(heldUs));
                }
                _lastConsumed    = arrival;
                _hasLastConsumed = true;
        } else {
                noteFrameDropped();
                if(_burnTimings) {
                        promekiDebug(
                                "MediaIOTask_NullPacing: drop "
                                "heldUs=%lld",
                                static_cast<long long>(heldUs));
                }
        }

        {
                Mutex::Locker lk(_stateMutex);
                if(consume) {
                        _stats.framesConsumed++;
                } else {
                        _stats.framesDropped++;
                }
                _stats.totalLatencyUs += heldUs;
                _stats.latencySamples++;
                if(heldUs > _stats.peakLatencyUs) {
                        _stats.peakLatencyUs = heldUs;
                }
        }

        cmd.currentFrame = FrameNumber(_stats.framesConsumed);
        cmd.frameCount   = MediaIO::FrameCountInfinite;
        stampWorkEnd();
        return Error::Ok;
}

Error MediaIOTask_NullPacing::executeCmd(MediaIOCommandStats &cmd) {
        NullPacingSnapshot snap;
        {
                Mutex::Locker lk(_stateMutex);
                snap = _stats;
        }
        // MediaIOStats reports latencies in milliseconds; the per-frame
        // arrival → discard interval we measure is in microseconds, so
        // we only convert at publish time.  The base class overwrites
        // these keys when benchmarking is enabled and a reporter is
        // attached — that path measures end-to-end latency, while ours
        // is the in-sink hold time.  Whichever fires last wins; the
        // benchmark numbers are strictly more informative when
        // available.
        const double avgUs = snap.latencySamples > 0
                ? static_cast<double>(snap.totalLatencyUs)
                  / static_cast<double>(snap.latencySamples)
                : 0.0;
        cmd.stats.set(MediaIOStats::AverageLatencyMs, avgUs / 1000.0);
        cmd.stats.set(MediaIOStats::PeakLatencyMs,
                      static_cast<double>(snap.peakLatencyUs) / 1000.0);
        return Error::Ok;
}

PROMEKI_NAMESPACE_END
