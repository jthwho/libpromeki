/**
 * @file      clock.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/clock.h>
#include <promeki/timestamp.h>

#include <chrono>
#include <thread>

PROMEKI_NAMESPACE_BEGIN

// ============================================================================
// WallClock
// ============================================================================

ClockDomain WallClock::domain() const {
        return ClockDomain(ClockDomain::SystemMonotonic);
}

int64_t WallClock::resolutionNs() const {
        // steady_clock::period on every platform we target is at least
        // as fine as one nanosecond.  Reporting 1 is accurate without
        // over-claiming sub-ns behaviour.
        return 1;
}

ClockJitter WallClock::jitter() const {
        // steady_clock reads are effectively instantaneous — any
        // measurable bias is dominated by the test harness itself.
        // Report a symmetric 1 ns envelope so the struct is still a
        // meaningful input to rate-estimate filters rather than zero
        // (which would imply a perfect clock like SyntheticClock).
        return ClockJitter{
                Duration::fromNanoseconds(-1),
                Duration::fromNanoseconds(1)
        };
}

int64_t WallClock::nowNs() const {
        return TimeStamp::now().nanoseconds();
}

void WallClock::sleepUntilNs(int64_t targetNs) {
        auto tp = TimeStamp::Clock::time_point(
                std::chrono::nanoseconds(targetNs));
        std::this_thread::sleep_until(tp);
}

PROMEKI_NAMESPACE_END
