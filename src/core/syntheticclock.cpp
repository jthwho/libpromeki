/**
 * @file      syntheticclock.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/syntheticclock.h>

PROMEKI_NAMESPACE_BEGIN

SyntheticClock::SyntheticClock(const ClockDomain &domain)
        : Clock(domain)
{
}

SyntheticClock::SyntheticClock(const FrameRate &frameRate, const ClockDomain &domain)
        : Clock(domain),
          _frameRate(frameRate)
{
        recomputePeriod();
}

void SyntheticClock::setFrameRate(const FrameRate &frameRate) {
        _frameRate = frameRate;
        recomputePeriod();
}

void SyntheticClock::setCurrentFrame(int64_t frame) {
        _currentFrame.store(frame, std::memory_order_relaxed);
}

void SyntheticClock::advance(int64_t frames) {
        _currentFrame.fetch_add(frames, std::memory_order_relaxed);
}

void SyntheticClock::reset(int64_t frame) {
        _currentFrame.store(frame, std::memory_order_relaxed);
}

int64_t SyntheticClock::resolutionNs() const {
        return 1;
}

ClockJitter SyntheticClock::jitter() const {
        // A SyntheticClock is perfect — its reported time is exactly
        // frame × period, by construction.
        return ClockJitter{Duration(), Duration()};
}

Result<int64_t> SyntheticClock::raw() const {
        int64_t frame = _currentFrame.load(std::memory_order_relaxed);
        int64_t period = _framePeriodNs.load(std::memory_order_relaxed);
        return makeResult<int64_t>(frame * period);
}

Error SyntheticClock::sleepUntilNs(int64_t) const {
        // No-op by design. The frame counter is authoritative and only
        // setCurrentFrame / advance move time forward.
        return {};
}

void SyntheticClock::recomputePeriod() {
        int64_t period = _frameRate.isValid()
                ? _frameRate.frameDuration().nanoseconds()
                : 0;
        _framePeriodNs.store(period, std::memory_order_relaxed);
}

PROMEKI_NAMESPACE_END
