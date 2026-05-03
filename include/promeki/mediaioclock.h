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

class MediaIOPortGroup;

/**
 * @brief Default @ref Clock for a @ref MediaIOPortGroup without a backend-supplied clock.
 * @ingroup mediaio_user
 *
 * MediaIOClock synthesizes raw time from its bound port group's
 * current frame position and frame rate:
 *
 * @code
 * rawNs = group->currentFrame() * group->frameRate().frameDuration().nanoseconds()
 * @endcode
 *
 * The group is tracked via an @ref ObjectBasePtr so that when the
 * owning @ref MediaIOPortGroup is destroyed, @ref raw returns
 * @ref Error::ObjectGone and the clock's @ref now / @ref nowNs
 * propagate that error to callers.
 *
 * @par Late-bind construction
 * The clock can be constructed with a null group and later bound via
 * @ref setGroup.  This supports the @ref CommandMediaIO::addPortGroup
 * helper, which needs a clock to construct the group, and a group
 * pointer to populate the clock — the helper allocates the clock
 * with a null group, constructs the group with the clock, then ties
 * the two together via @ref setGroup.
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
 * A no-op — @ref MediaIOPortGroup::currentFrame is the authoritative
 * driver, so there is no wall-clock to wait for.  Callers that need
 * real-time pacing should use a @ref WallClock or the device-supplied
 * clock from a task backend.
 *
 * @par Thread Safety
 * Conditionally thread-safe.  Distinct instances may be used concurrently;
 * concurrent access to a single instance must be externally synchronized.
 * The clock reads its driving frame counter from the bound
 * @ref MediaIOPortGroup and inherits that object's threading contract.
 */
class MediaIOClock : public Clock {
        public:
                /**
                 * @brief Constructs a MediaIOClock bound to @p group.
                 * @param group The port group that drives this clock; may
                 *              be null and later bound via @ref setGroup.
                 */
                explicit MediaIOClock(MediaIOPortGroup *group = nullptr);

                /**
                 * @brief Binds this clock to @p group.
                 *
                 * Used by @ref CommandMediaIO::addPortGroup to break the
                 * construction-order chicken-and-egg between the group
                 * (which requires a clock at construction) and the
                 * default synthetic clock (which needs the group to
                 * read @c currentFrame).
                 */
                void setGroup(MediaIOPortGroup *group);

                int64_t     resolutionNs() const override;
                ClockJitter jitter() const override;

        protected:
                Result<int64_t> raw() const override;
                Error           sleepUntilNs(int64_t targetNs) const override;

        private:
                int64_t framePeriodNs() const;

                ObjectBasePtr<MediaIOPortGroup> _group;
};

PROMEKI_NAMESPACE_END
