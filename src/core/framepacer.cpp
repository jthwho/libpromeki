/**
 * @file      framepacer.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/framepacer.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_DEBUG(FramePacer)

// ---- WallPacerClock ----

const char *WallPacerClock::name() const { return "wall"; }

int64_t WallPacerClock::resolutionNs() const {
        // steady_clock is typically nanosecond or sub-nanosecond.
        return 1;
}

int64_t WallPacerClock::nowNs() const {
        return TimeStamp::now().nanoseconds();
}

void WallPacerClock::sleepUntilNs(int64_t targetNs) {
        auto tp = TimeStamp::Clock::time_point(
                std::chrono::nanoseconds(targetNs));
        std::this_thread::sleep_until(tp);
}

// ---- FramePacer ----

void FramePacer::ensureClock() {
        if(_clock == nullptr) {
                _clock = new WallPacerClock();
                _ownsClock = true;
        }
}

FramePacer::FramePacer() {
        ensureClock();
}

FramePacer::FramePacer(const String &name, const FrameRate &fps)
        : _name(name), _frameRate(fps)
{
        ensureClock();
}

FramePacer::~FramePacer() {
        if(_ownsClock) delete _clock;
}

void FramePacer::setClock(FramePacerClock *clock) {
        if(_ownsClock) {
                delete _clock;
                _ownsClock = false;
        }
        _clock = clock;
        ensureClock();
}

// ---- Periodic debug summary ----
//
// Emits once per second when PROMEKI_DEBUG(FramePacer) is active.
// Dumps all pacer state in a single line so operators can watch
// the pacer's health without per-frame noise.

static constexpr int64_t kPeriodicIntervalNs = 1000000000LL;   // 1 second

void FramePacer::periodicDebugLog(int64_t nowNs) {
        if(nowNs - _lastPeriodicLogNs < kPeriodicIntervalNs) return;

        int64_t elapsedFrames = _frameCount - _frameCountAtLastLog;
        double elapsedSec = (double)(nowNs - _lastPeriodicLogNs) / 1e9;
        double actualFps = (elapsedSec > 0.0) ? (double)elapsedFrames / elapsedSec : 0.0;

        promekiDebug("FramePacer[%s]: frame=%lld  rate=%s  actual=%.2f fps  "
                     "accErr=%.3f ms  missed=%lld  clock=%s (res=%.1f us)",
                     _name.cstr(),
                     static_cast<long long>(_frameCount),
                     _frameRate.toString().cstr(),
                     actualFps,
                     (double)_accumulatedErrorNs / 1e6,
                     static_cast<long long>(_missedFrames),
                     _clock->name(),
                     (double)_clock->resolutionNs() / 1e3);

        _lastPeriodicLogNs = nowNs;
        _frameCountAtLastLog = _frameCount;
}

// ---- Public API ----

void FramePacer::reset() {
        _originNs = 0;
        _frameCount = 0;
        _missedFrames = 0;
        _accumulatedErrorNs = 0;
        _lastPeriodicLogNs = 0;
        _frameCountAtLastLog = 0;
        _started = false;
        _explicitOrigin = false;
        promekiDebug("FramePacer[%s]: reset (rate %s, origin=auto, clock=%s)",
                     _name.cstr(),
                     _frameRate.isValid() ? _frameRate.toString().cstr() : "none",
                     _clock->name());
}

void FramePacer::reset(int64_t originNs) {
        _originNs = originNs;
        _frameCount = 0;
        _missedFrames = 0;
        _accumulatedErrorNs = 0;
        _lastPeriodicLogNs = 0;
        _frameCountAtLastLog = 0;
        _started = true;       // origin is known — no first-frame capture
        _explicitOrigin = true;
        promekiDebug("FramePacer[%s]: reset (rate %s, origin=%lld ns, clock=%s)",
                     _name.cstr(),
                     _frameRate.isValid() ? _frameRate.toString().cstr() : "none",
                     static_cast<long long>(originNs),
                     _clock->name());
}

FramePacer::PaceResult FramePacer::pace() {
        PaceResult result;
        if(!_frameRate.isValid()) return result;

        // First frame after reset(): anchor the origin to "now" (unless
        // the caller supplied an explicit origin) and return immediately
        // — the first frame should be presented now, not one period
        // from now.
        if(!_started) {
                _originNs = _clock->nowNs();
                _frameCount = 0;
                _lastPeriodicLogNs = _originNs;
                _frameCountAtLastLog = 0;
                _started = true;
                result.frameIndex = 0;
                promekiDebug("FramePacer[%s]: started, origin=%lld ns "
                             "(rate %s, period %.3f ms, clock=%s)",
                             _name.cstr(),
                             static_cast<long long>(_originNs),
                             _frameRate.toString().cstr(),
                             _frameRate.frameDuration().toSecondsDouble() * 1000.0,
                             _clock->name());
                return result;
        }

        _frameCount++;

        // Compute the absolute deadline for this frame using rational
        // arithmetic.  cumulativeTicks with a 1 GHz tick rate gives
        // nanoseconds, which is exact for all standard frame rates
        // including NTSC (30000/1001, 60000/1001, etc.).
        int64_t offsetNs = _frameRate.cumulativeTicks(1000000000LL, _frameCount);
        int64_t deadlineNs = _originNs + offsetNs;

        int64_t nowNs = _clock->nowNs();
        int64_t remainingNs = deadlineNs - nowNs;

        if(remainingNs <= 0) {
                // Already past the deadline.  Compute how many whole
                // frame periods we're behind so the caller knows how
                // many frames it should drop.  The pacer does NOT
                // advance past them — that's up to the caller via
                // advance().
                int64_t lateNs = -remainingNs;
                int64_t periodNs = _frameRate.frameDuration().nanoseconds();
                int64_t recommended = (periodNs > 0) ? (lateNs / periodNs) : 0;

                _missedFrames++;

                // Don't reset the accumulated error here — we want it
                // to reflect the full lateness so subsequent pace()
                // calls return immediately until the caller catches up
                // (or drops frames via advance()).
                _accumulatedErrorNs = lateNs;

                result.frameIndex = _frameCount;
                result.framesToDrop = recommended;
                result.error = Duration::fromNanoseconds(lateNs);

                promekiWarn("FramePacer[%s]: frame %lld missed deadline by "
                            "%.3f ms, recommend dropping %lld frame(s) "
                            "(total misses: %lld)",
                            _name.cstr(),
                            static_cast<long long>(_frameCount),
                            (double)lateNs / 1e6,
                            static_cast<long long>(recommended),
                            static_cast<long long>(_missedFrames));

                periodicDebugLog(nowNs);
                return result;
        }

        // Bias the sleep by the accumulated error from previous
        // frames.  If we've been oversleeping (positive error), we
        // shorten this sleep; if undersleeping (negative), we
        // lengthen it.  When the error exceeds the remaining time,
        // we skip the sleep entirely — the pipeline runs at full
        // speed until the error drains below a frame period.
        int64_t adjustedNs = remainingNs - _accumulatedErrorNs;
        int64_t framePeriodNs = _frameRate.frameDuration().nanoseconds();

        if(adjustedNs <= 0) {
                // Accumulated error exceeds remaining time — don't
                // sleep, let the caller run at full speed to catch up.
                adjustedNs = 0;
        } else if(adjustedNs > framePeriodNs) {
                // Don't sleep longer than one frame period.
                adjustedNs = framePeriodNs;
        }

        // Sleep.
        if(adjustedNs > 0) {
                _clock->sleepUntilNs(nowNs + adjustedNs);
        }

        // Measure wake-up error relative to the ideal deadline.
        // Positive = late (we overslept), negative = early.
        int64_t wakeNs = _clock->nowNs();
        _accumulatedErrorNs = wakeNs - deadlineNs;

        result.frameIndex = _frameCount;
        result.error = Duration::fromNanoseconds(_accumulatedErrorNs);

        // Recommend drops if the wake-up error alone exceeds a full
        // frame period (e.g. the OS scheduled us very late).
        if(_accumulatedErrorNs > framePeriodNs) {
                result.framesToDrop = _accumulatedErrorNs / framePeriodNs;
                promekiWarn("FramePacer[%s]: frame %lld woke %.3f ms late, "
                            "recommend dropping %lld frame(s)",
                            _name.cstr(),
                            static_cast<long long>(_frameCount),
                            (double)_accumulatedErrorNs / 1e6,
                            static_cast<long long>(result.framesToDrop));
        }

        periodicDebugLog(wakeNs);
        return result;
}

void FramePacer::advance(int64_t frames) {
        if(frames <= 0) return;
        _frameCount += frames;

        // Recompute the accumulated error against the new position
        // so the next pace() targets the correct deadline.
        int64_t offsetNs = _frameRate.cumulativeTicks(1000000000LL, _frameCount);
        int64_t deadlineNs = _originNs + offsetNs;
        int64_t nowNs = _clock->nowNs();
        _accumulatedErrorNs = nowNs - deadlineNs;

        promekiDebug("FramePacer[%s]: advance(%lld), now at frame %lld  "
                     "accErr %.3f ms",
                     _name.cstr(),
                     static_cast<long long>(frames),
                     static_cast<long long>(_frameCount),
                     (double)_accumulatedErrorNs / 1e6);
}

PROMEKI_NAMESPACE_END
