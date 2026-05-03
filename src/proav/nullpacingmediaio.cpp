/**
 * @file      nullpacingmediaio.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/nullpacingmediaio.h>

#include <promeki/enums.h>
#include <promeki/framerate.h>
#include <promeki/logger.h>
#include <promeki/mediaconfig.h>
#include <promeki/mediadesc.h>
#include <promeki/mediaioportgroup.h>
#include <promeki/mediaiorequest.h>
#include <promeki/rational.h>
#include <promeki/variantspec.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_DEBUG(NullPacingMediaIO)

PROMEKI_REGISTER_MEDIAIO_FACTORY(NullPacingFactory)

namespace {

// Resolves a Rational target rate (from the config) plus the upstream
// MediaDesc rate (from CmdOpen.pendingMediaDesc) into a single
// FrameRate.  The configured rate wins when its numerator is > 0;
// the descriptor rate is the fallback.  Returns an invalid FrameRate
// when both are absent, which the caller turns into Error::InvalidArgument.
FrameRate resolveTargetRate(const Rational<int> &configured, const FrameRate &fromDesc) {
        if (configured.numerator() > 0 && configured.denominator() > 0) {
                FrameRate::RationalType r(static_cast<unsigned int>(configured.numerator()),
                                          static_cast<unsigned int>(configured.denominator()));
                return FrameRate(r);
        }
        if (fromDesc.isValid()) return fromDesc;
        return FrameRate();
}

} // namespace

// ============================================================================
// NullPacingFactory
// ============================================================================

MediaIOFactory::Config::SpecMap NullPacingFactory::configSpecs() const {
        Config::SpecMap specs;
        auto            s = [&specs](MediaConfig::ID id, const Variant &def) {
                const VariantSpec *gs = MediaConfig::spec(id);
                specs.insert(id, gs ? VariantSpec(*gs).setDefault(def) : VariantSpec().setDefault(def));
        };
        s(MediaConfig::NullPacingMode, promeki::NullPacingMode::Wallclock);
        s(MediaConfig::NullPacingTargetFps, Rational<int>(0, 1));
        s(MediaConfig::NullPacingBurnTimings, false);
        return specs;
}

MediaIO *NullPacingFactory::create(const Config &config, ObjectBase *parent) const {
        auto *io = new NullPacingMediaIO(parent);
        io->setConfig(config);
        return io;
}

// ============================================================================
// NullPacingMediaIO
// ============================================================================

NullPacingMediaIO::NullPacingMediaIO(ObjectBase *parent) : SharedThreadMediaIO(parent) {}

NullPacingMediaIO::~NullPacingMediaIO() {
        // Close while our subclass state is still alive.  See
        // @ref MediaIO destructor for the rationale.
        if (isOpen()) (void)close().wait();
}

NullPacingSnapshot NullPacingMediaIO::snapshot() const {
        Mutex::Locker lk(_stateMutex);
        return _stats;
}

Error NullPacingMediaIO::executeCmd(MediaIOCommandOpen &cmd) {
        const MediaIO::Config &cfg = cmd.config;

        Error modeErr;
        Enum  modeEnum = cfg.get(MediaConfig::NullPacingMode).asEnum(promeki::NullPacingMode::Type, &modeErr);
        if (modeErr.isError() || !modeEnum.hasListedValue()) {
                promekiErr("NullPacingMediaIO: invalid NullPacingMode");
                return Error::InvalidArgument;
        }
        _mode = promeki::NullPacingMode(modeEnum.value());

        Rational<int> configuredFps = cfg.get(MediaConfig::NullPacingTargetFps).get<Rational<int>>();
        _targetRate = resolveTargetRate(configuredFps, cmd.pendingMediaDesc.frameRate());

        if (_mode.value() == promeki::NullPacingMode::Wallclock.value()) {
                if (!_targetRate.isValid()) {
                        promekiErr("NullPacingMediaIO: Wallclock mode "
                                   "requires NullPacingTargetFps > 0/1 or "
                                   "a valid upstream MediaDesc::frameRate");
                        return Error::InvalidArgument;
                }
                _period = _targetRate.frameDuration();
                // Bind the gate to a fresh WallClock so tryAcquire
                // enforces the rate against real wall time.  Future
                // versions can wire MediaIOCommandSetClock to swap in
                // an external pacing clock without touching this code.
                _gate.setClock(Clock::Ptr::takeOwnership(new WallClock()));
                _gate.setPeriod(_period);
        } else {
                _period = Duration();
                _gate.setClock(Clock::Ptr());
                _gate.setPeriod(Duration());
        }

        _burnTimings = cfg.getAs<bool>(MediaConfig::NullPacingBurnTimings, false);

        _hasLastConsumed = false;
        _lastConsumed = TimeStamp();
        {
                Mutex::Locker lk(_stateMutex);
                _stats = NullPacingSnapshot{};
        }

        promekiInfo("NullPacingMediaIO: opened mode=%s "
                    "targetFps=%.4f burnTimings=%s",
                    _mode.valueName().cstr(), _targetRate.isValid() ? _targetRate.toDouble() : 0.0,
                    _burnTimings ? "yes" : "no");

        _isOpen = true;

        MediaIOPortGroup *group = addPortGroup("nullpacing");
        if (group == nullptr) return Error::Invalid;
        group->setFrameRate(cmd.pendingMediaDesc.frameRate());
        group->setCanSeek(false);
        group->setFrameCount(MediaIO::FrameCountInfinite);
        if (addSink(group, cmd.pendingMediaDesc) == nullptr) return Error::Invalid;
        return Error::Ok;
}

Error NullPacingMediaIO::executeCmd(MediaIOCommandClose &cmd) {
        (void)cmd;
        if (!_isOpen) return Error::Ok;

        NullPacingSnapshot snap;
        {
                Mutex::Locker lk(_stateMutex);
                snap = _stats;
        }
        const double avgUs = snap.latencySamples > 0 ? static_cast<double>(snap.totalLatencyUs) /
                                                               static_cast<double>(snap.latencySamples)
                                                     : 0.0;
        promekiInfo("NullPacingMediaIO: closed — consumed %lld, "
                    "dropped %lld, avgLatencyUs %.1f, peakLatencyUs %lld",
                    static_cast<long long>(snap.framesConsumed), static_cast<long long>(snap.framesDropped), avgUs,
                    static_cast<long long>(snap.peakLatencyUs));

        _isOpen = false;
        _hasLastConsumed = false;
        return Error::Ok;
}

Error NullPacingMediaIO::executeCmd(MediaIOCommandWrite &cmd) {
        if (!_isOpen) return Error::NotOpen;
        if (!cmd.frame.isValid()) return Error::InvalidArgument;

        const TimeStamp arrival = TimeStamp::now();
        // Free mode = no clock bound to the gate; tryAcquire returns
        // true unconditionally so every frame is consumed.
        // Wallclock mode = WallClock + period; tryAcquire enforces the
        // rate (true on or after the next deadline, false on early
        // arrival).  Catch-up after a long stall is handled by the
        // gate's reanchor threshold.
        const bool consume = _gate.tryAcquire();

        // Drain stamp = the moment we decided what to do with the
        // frame, used for the per-frame in-sink latency.  Both
        // consume and drop count toward the latency average since
        // both branches "held" the frame for a measurable interval.
        const TimeStamp drain = TimeStamp::now();
        const Duration  held = drain - arrival;
        const int64_t   heldUs = held.microseconds();

        if (consume) {
                if (_burnTimings && _hasLastConsumed) {
                        const Duration sincePrev = arrival - _lastConsumed;
                        const int64_t  periodUs = _period.microseconds();
                        const int64_t  measuredUs = sincePrev.microseconds();
                        const int64_t  jitterUs = measuredUs - periodUs;
                        promekiDebug("NullPacingMediaIO: consume "
                                     "periodUs=%lld measuredUs=%lld "
                                     "jitterUs=%lld heldUs=%lld",
                                     static_cast<long long>(periodUs), static_cast<long long>(measuredUs),
                                     static_cast<long long>(jitterUs), static_cast<long long>(heldUs));
                }
                _lastConsumed = arrival;
                _hasLastConsumed = true;
        } else {
                noteFrameDropped(portGroup(0));
                if (_burnTimings) {
                        promekiDebug("NullPacingMediaIO: drop "
                                     "heldUs=%lld",
                                     static_cast<long long>(heldUs));
                }
        }

        {
                Mutex::Locker lk(_stateMutex);
                if (consume) {
                        _stats.framesConsumed++;
                } else {
                        _stats.framesDropped++;
                }
                _stats.totalLatencyUs += heldUs;
                _stats.latencySamples++;
                if (heldUs > _stats.peakLatencyUs) {
                        _stats.peakLatencyUs = heldUs;
                }
        }

        cmd.currentFrame = FrameNumber(_stats.framesConsumed);
        cmd.frameCount = MediaIO::FrameCountInfinite;
        return Error::Ok;
}

Error NullPacingMediaIO::executeCmd(MediaIOCommandStats &cmd) {
        NullPacingSnapshot snap;
        {
                Mutex::Locker lk(_stateMutex);
                snap = _stats;
        }
        // MediaIOStats reports latencies in milliseconds; the per-frame
        // arrival → discard interval we measure is in microseconds, so
        // we only convert at publish time.
        const double avgUs = snap.latencySamples > 0 ? static_cast<double>(snap.totalLatencyUs) /
                                                               static_cast<double>(snap.latencySamples)
                                                     : 0.0;
        cmd.stats.set(MediaIOStats::AverageLatencyMs, avgUs / 1000.0);
        cmd.stats.set(MediaIOStats::PeakLatencyMs, static_cast<double>(snap.peakLatencyUs) / 1000.0);
        return Error::Ok;
}

PROMEKI_NAMESPACE_END
