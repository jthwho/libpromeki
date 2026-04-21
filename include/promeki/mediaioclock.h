/**
 * @file      mediaioclock.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/clock.h>
#include <promeki/objectbase.h>

PROMEKI_NAMESPACE_BEGIN

class MediaIO;

/**
 * @brief Default @ref Clock for a @ref MediaIO without a task-supplied clock.
 * @ingroup proav
 *
 * MediaIOClock synthesizes raw time from its parent MediaIO's current
 * frame position and frame rate:
 *
 * @code
 * rawNs = mediaIo->currentFrame() * frameRate->frameDuration().nanoseconds()
 * @endcode
 *
 * The parent is tracked via an @ref ObjectBasePtr so that when the
 * owning @ref MediaIO is destroyed, @ref raw returns
 * @ref Error::ObjectGone and the clock's @ref now / @ref nowNs
 * propagate that error to callers.
 *
 * @par Properties
 * - Domain: @ref ClockDomain::Synthetic.
 * - Resolution: one frame period (or 1 ns if the frame rate is not
 *   yet valid).
 * - Jitter: symmetric, ± half a frame period — the underlying source
 *   can only report time at frame boundaries.
 * - Cannot be paused.
 *
 * @par sleepUntilNs
 * A no-op — @ref currentFrame is the authoritative driver, so there
 * is no wall-clock to wait for.  Callers that need real-time pacing
 * should use a @ref WallClock or the device-supplied clock from a
 * task backend.
 */
class MediaIOClock : public Clock {
        public:
                /**
                 * @brief Constructs a MediaIOClock bound to @p owner.
                 * @param owner The MediaIO that drives this clock.
                 */
                explicit MediaIOClock(MediaIO *owner);

                int64_t     resolutionNs() const override;
                ClockJitter jitter() const override;

        protected:
                Result<int64_t> raw() const override;
                Error           sleepUntilNs(int64_t targetNs) const override;

        private:
                int64_t framePeriodNs() const;

                ObjectBasePtr<MediaIO> _owner;
};

PROMEKI_NAMESPACE_END
