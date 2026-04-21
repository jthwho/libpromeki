/**
 * @file      clock.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/clock.h>
#include <promeki/timestamp.h>

#include <algorithm>
#include <chrono>
#include <thread>

PROMEKI_NAMESPACE_BEGIN

// ============================================================================
// Clock (base class)
// ============================================================================

Clock::Clock(const ClockDomain &domain,
             const Duration &fixedOffset,
             ClockPauseMode pauseMode,
             ClockFilter *filter)
        : _domain(domain),
          _pauseMode(pauseMode),
          _filter(filter),
          _fixedOffsetNs(fixedOffset.nanoseconds()),
          _pausedOffsetNs(0),
          _frozenFilteredNs(0),
          _paused(false),
          _lastNowNs(INT64_MIN)
{
}

Clock::~Clock() = default;

Error Clock::onPause(bool) {
        return {};
}

Duration Clock::fixedOffset() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return Duration::fromNanoseconds(_fixedOffsetNs);
}

void Clock::setFixedOffset(const Duration &offset) {
        std::lock_guard<std::mutex> lock(_mutex);
        _fixedOffsetNs = offset.nanoseconds();
}

bool Clock::isPaused() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _paused;
}

Result<int64_t> Clock::applyFilter(int64_t raw) const {
        // _mutex is held by caller where filter mutation matters.
        if(_filter) return makeResult<int64_t>(_filter->filter(raw));
        return makeResult<int64_t>(raw);
}

Result<MediaTimeStamp> Clock::now() const {
        int64_t filteredNs;
        int64_t totalOffsetNs;
        {
                std::lock_guard<std::mutex> lock(_mutex);

                if(_paused) {
                        // While paused, the filtered value is frozen.
                        // raw() is not called — useful both to avoid
                        // cost and to avoid propagating transient
                        // errors in sources that are logically
                        // suspended.
                        filteredNs = _frozenFilteredNs;
                } else {
                        auto rawRes = raw();
                        if(isError(rawRes)) {
                                return makeError<MediaTimeStamp>(error(rawRes));
                        }
                        auto f = applyFilter(value(rawRes));
                        // applyFilter never errors today; kept as
                        // Result so a future filter implementation
                        // can fail gracefully.
                        if(isError(f)) return makeError<MediaTimeStamp>(error(f));
                        filteredNs = value(f);
                }

                totalOffsetNs = _fixedOffsetNs + _pausedOffsetNs;
        }

        int64_t value = filteredNs - totalOffsetNs;

        // Monotonic clamp: never return a value less than the
        // previous one.  CAS loop so concurrent callers all see a
        // non-decreasing sequence even without the mutex.
        int64_t last = _lastNowNs.load(std::memory_order_relaxed);
        while(true) {
                int64_t clamped = std::max(value, last);
                if(_lastNowNs.compare_exchange_weak(last, clamped,
                                                   std::memory_order_acq_rel,
                                                   std::memory_order_relaxed)) {
                        value = clamped;
                        break;
                }
                // last reloaded on failure; loop again.
        }

        TimeStamp ts{TimeStamp::Clock::time_point(std::chrono::nanoseconds(value))};
        return makeResult(MediaTimeStamp(ts, _domain,
                Duration::fromNanoseconds(totalOffsetNs)));
}

Result<int64_t> Clock::nowNs() const {
        auto r = now();
        if(isError(r)) return makeError<int64_t>(error(r));
        return makeResult<int64_t>(value(r).timeStamp().nanoseconds());
}

Error Clock::setPause(bool paused) {
        if(_pauseMode == ClockPauseMode::CannotPause) {
                return Error::NotSupported;
        }

        {
                std::lock_guard<std::mutex> lock(_mutex);
                if(_paused == paused) return {};

                if(paused) {
                        auto rawRes = raw();
                        if(isError(rawRes)) return error(rawRes);
                        auto f = applyFilter(value(rawRes));
                        if(isError(f)) return error(f);
                        _frozenFilteredNs = value(f);
                        _paused = true;
                        Error e = onPause(true);
                        if(e.isError()) {
                                _paused = false;
                                return e;
                        }
                } else {
                        Error e = onPause(false);
                        if(e.isError()) return e;
                        auto rawRes = raw();
                        if(isError(rawRes)) {
                                (void)onPause(true);
                                return error(rawRes);
                        }
                        auto f = applyFilter(value(rawRes));
                        if(isError(f)) {
                                (void)onPause(true);
                                return error(f);
                        }
                        _pausedOffsetNs += value(f) - _frozenFilteredNs;
                        _paused = false;
                }
        }

        return {};
}

Error Clock::sleepUntil(const MediaTimeStamp &deadline) const {
        if(deadline.domain() != _domain) {
                return Error::ClockDomainMismatch;
        }

        int64_t totalOffsetNs;
        {
                std::lock_guard<std::mutex> lock(_mutex);
                if(_paused) return Error::ClockPaused;
                totalOffsetNs = _fixedOffsetNs + _pausedOffsetNs;
        }

        int64_t targetRawNs = deadline.timeStamp().nanoseconds() + totalOffsetNs;
        return sleepUntilNs(targetRawNs);
}

// ============================================================================
// WallClock
// ============================================================================

WallClock::WallClock()
        : Clock(ClockDomain(ClockDomain::SystemMonotonic))
{
}

int64_t WallClock::resolutionNs() const {
        // steady_clock::period on every platform we target is at
        // least as fine as one nanosecond.  Reporting 1 is accurate
        // without over-claiming sub-ns behaviour.
        return 1;
}

ClockJitter WallClock::jitter() const {
        // steady_clock reads are effectively instantaneous — any
        // measurable bias is dominated by the test harness itself.
        return ClockJitter{
                Duration::fromNanoseconds(-1),
                Duration::fromNanoseconds(1)
        };
}

Result<int64_t> WallClock::raw() const {
        return makeResult<int64_t>(TimeStamp::now().nanoseconds());
}

Error WallClock::sleepUntilNs(int64_t targetNs) const {
        auto tp = TimeStamp::Clock::time_point(
                std::chrono::nanoseconds(targetNs));
        std::this_thread::sleep_until(tp);
        return {};
}

PROMEKI_NAMESPACE_END
