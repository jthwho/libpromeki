/**
 * @file      syntheticclock.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/syntheticclock.h>

PROMEKI_NAMESPACE_BEGIN

SyntheticClock::SyntheticClock(const FrameRate &frameRate)
        : _frameRate(frameRate)
{
        recomputePeriod();
}

void SyntheticClock::setDomain(const ClockDomain &domain) {
        _domain = domain;
}

void SyntheticClock::setFrameRate(const FrameRate &frameRate) {
        _frameRate = frameRate;
        recomputePeriod();
}

void SyntheticClock::setCurrentFrame(int64_t frame) {
        _currentFrame = frame;
}

void SyntheticClock::advance(int64_t frames) {
        _currentFrame += frames;
}

void SyntheticClock::reset(int64_t frame) {
        _currentFrame = frame;
}

ClockDomain SyntheticClock::domain() const {
        return _domain;
}

int64_t SyntheticClock::resolutionNs() const {
        return 1;
}

ClockJitter SyntheticClock::jitter() const {
        // A SyntheticClock is perfect — its reported time is exactly
        // frame × period, by construction.
        return ClockJitter{Duration(), Duration()};
}

int64_t SyntheticClock::nowNs() const {
        return _currentFrame * _framePeriodNs;
}

void SyntheticClock::sleepUntilNs(int64_t) {
        // No-op by design. The frame counter is authoritative and only
        // setCurrentFrame / advance move time forward.
}

double SyntheticClock::rateRatio() const {
        return 1.0;
}

void SyntheticClock::recomputePeriod() {
        _framePeriodNs = _frameRate.isValid()
                ? _frameRate.frameDuration().nanoseconds()
                : 0;
}

PROMEKI_NAMESPACE_END
