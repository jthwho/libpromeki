/**
 * @file      mediaioclock.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/mediaioclock.h>
#include <promeki/mediaio.h>
#include <promeki/framerate.h>

PROMEKI_NAMESPACE_BEGIN

MediaIOClock::MediaIOClock(MediaIO *owner) : Clock(ClockDomain(ClockDomain::Synthetic)), _owner(owner) {}

int64_t MediaIOClock::framePeriodNs() const {
        const MediaIO *io = _owner.data();
        if (io == nullptr) return 0;
        const FrameRate &fps = io->frameRate();
        if (!fps.isValid()) return 0;
        return fps.frameDuration().nanoseconds();
}

int64_t MediaIOClock::resolutionNs() const {
        int64_t period = framePeriodNs();
        return period > 0 ? period : 1;
}

ClockJitter MediaIOClock::jitter() const {
        // The underlying source updates once per frame, so reported
        // time is accurate to at best ± half a frame period —
        // "between" frames we have no information.
        int64_t period = framePeriodNs();
        int64_t half = period / 2;
        return ClockJitter{Duration::fromNanoseconds(-half), Duration::fromNanoseconds(half)};
}

Result<int64_t> MediaIOClock::raw() const {
        const MediaIO *io = _owner.data();
        if (io == nullptr) {
                return makeError<int64_t>(Error::ObjectGone);
        }
        const FrameRate &fps = io->frameRate();
        if (!fps.isValid()) {
                // No frame rate — report zero rather than erroring
                // so pipelines that haven't fully opened yet can
                // query the clock without a hard failure.
                return makeResult<int64_t>(0);
        }
        int64_t     period = fps.frameDuration().nanoseconds();
        FrameNumber frame = io->currentFrame();
        int64_t     v = frame.isValid() ? frame.value() : 0;
        return makeResult<int64_t>(v * period);
}

Error MediaIOClock::sleepUntilNs(int64_t) const {
        // No-op by design — the clock is driven by MediaIO's frame
        // position, not by wall time.  Callers that need real-time
        // pacing should use a device-supplied clock or a WallClock.
        return {};
}

PROMEKI_NAMESPACE_END
