/**
 * @file      syntheticclock.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/clock.h>
#include <promeki/framerate.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Frame-count driven synthetic Clock.
 * @ingroup time
 *
 * SyntheticClock reports time as <tt>currentFrame × framePeriod</tt>.
 * It has no wall-time component — its notion of "now" only moves when
 * the caller explicitly advances the frame counter via
 * @ref advance or @ref setCurrentFrame.
 *
 * This is the right clock for any pipeline where each output frame is
 * exactly one @ref FrameRate period, decoupled from real time:
 *
 * - File writers that must emit a fixed, pristine frame rate.
 * - Offline / batch conversion.
 * - Tests that want deterministic, reproducible timing.
 *
 * When handed to a @ref FrameSync, the sync object advances the frame
 * counter by 1 for every output frame it emits, which is how "output
 * frame count drives the clock" gets wired up.
 *
 * @par Properties
 *
 * - @ref domain defaults to @ref ClockDomain::Synthetic.  Override
 *   with @ref setDomain if cross-stream correlation is needed.
 * - @ref resolutionNs returns 1 (the stored counter is ns-accurate).
 * - @ref jitter returns @c {0, 0} — a SyntheticClock is perfect.
 * - @ref rateRatio returns @c 1.0.
 *
 * @par sleepUntilNs
 *
 * @ref sleepUntilNs is a no-op.  It does not advance the internal
 * counter; it does not block.  This is deliberate: the counter is the
 * authoritative state, and only @ref advance or @ref setCurrentFrame
 * move it.
 */
class SyntheticClock : public Clock {
        public:
                /** @brief Constructs an invalid SyntheticClock (no frame rate). */
                SyntheticClock() = default;

                /**
                 * @brief Constructs a SyntheticClock with the given frame rate.
                 * @param frameRate The target frame rate.
                 */
                explicit SyntheticClock(const FrameRate &frameRate);

                /**
                 * @brief Overrides the default Synthetic domain.
                 *
                 * Use to register a per-instance domain if the caller
                 * wants timestamps from this clock to be distinguishable
                 * from other Synthetic-domain streams.
                 *
                 * @param domain The domain to report.
                 */
                void setDomain(const ClockDomain &domain);

                /**
                 * @brief Sets the frame rate.
                 *
                 * The frame duration is recomputed.  Does not change
                 * the current frame counter.
                 *
                 * @param frameRate The new frame rate.
                 */
                void setFrameRate(const FrameRate &frameRate);

                /** @brief Returns the configured frame rate. */
                const FrameRate &frameRate() const { return _frameRate; }

                /**
                 * @brief Sets the current frame counter.
                 *
                 * @ref nowNs will subsequently return
                 * <tt>frame × frameDuration.nanoseconds()</tt>.
                 *
                 * @param frame The new counter value.
                 */
                void setCurrentFrame(int64_t frame);

                /** @brief Returns the current frame counter. */
                int64_t currentFrame() const { return _currentFrame; }

                /**
                 * @brief Advances the frame counter.
                 * @param frames Number of frames to add (default 1).
                 */
                void advance(int64_t frames = 1);

                /**
                 * @brief Resets the frame counter to @p frame.
                 *
                 * Equivalent to @ref setCurrentFrame but named for
                 * symmetry with other library state-carrying objects.
                 *
                 * @param frame Starting frame counter (default 0).
                 */
                void reset(int64_t frame = 0);

                ClockDomain domain() const override;
                int64_t     resolutionNs() const override;
                ClockJitter jitter() const override;
                int64_t     nowNs() const override;
                void        sleepUntilNs(int64_t targetNs) override;
                double      rateRatio() const override;

        private:
                void recomputePeriod();

                FrameRate   _frameRate;
                int64_t     _framePeriodNs = 0;
                int64_t     _currentFrame  = 0;
                ClockDomain _domain = ClockDomain(ClockDomain::Synthetic);
};

PROMEKI_NAMESPACE_END
