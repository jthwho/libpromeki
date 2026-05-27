/**
 * @file      syntheticclock.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/syntheticclock.h>

PROMEKI_NAMESPACE_BEGIN

SyntheticClock::SyntheticClock(const ClockDomain &domain)
    : Clock(domain, Duration::zero(), ClockPauseMode::PausesRawKeepsRunning) {}

SyntheticClock::SyntheticClock(const FrameRate &frameRate, const ClockDomain &domain)
    : Clock(domain, Duration::zero(), ClockPauseMode::PausesRawKeepsRunning),
      _frameRate(frameRate) {
        recomputePeriod();
}

void SyntheticClock::setFrameRate(const FrameRate &frameRate) {
        _frameRate = frameRate;
        recomputePeriod();
}

void SyntheticClock::setCurrentFrame(const FrameNumber &frame) {
        _currentFrame.setValue(frame.isValid() ? frame.value() : 0);
}

void SyntheticClock::advance(int64_t frames) {
        _currentFrame.fetchAndAdd(frames);
}

void SyntheticClock::reset(const FrameNumber &frame) {
        _currentFrame.setValue(frame.isValid() ? frame.value() : 0);
}

int64_t SyntheticClock::resolutionNs() const {
        return 1;
}

ClockJitter SyntheticClock::jitter() const {
        // A SyntheticClock is perfect — its reported time is exactly
        // frame × period, by construction.
        return ClockJitter{Duration::zero(), Duration::zero()};
}

Result<int64_t> SyntheticClock::raw() const {
        int64_t frame = _currentFrame.value();
        int64_t period = _framePeriodNs.value();
        return makeResult<int64_t>(frame * period);
}

Error SyntheticClock::sleepUntilNs(int64_t) const {
        // No-op by design. The frame counter is authoritative and only
        // setCurrentFrame / advance move time forward.
        return {};
}

void SyntheticClock::recomputePeriod() {
        int64_t period = _frameRate.isValid() ? _frameRate.frameDuration().nanoseconds() : 0;
        _framePeriodNs.setValue(period);
}

PROMEKI_NAMESPACE_END
