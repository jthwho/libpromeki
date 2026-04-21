/**
 * @file      syntheticclock.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <atomic>
#include <promeki/namespace.h>
#include <promeki/clock.h>
#include <promeki/framerate.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Frame-count driven synthetic Clock.
 * @ingroup time
 *
 * SyntheticClock reports raw time as <tt>currentFrame × framePeriod</tt>.
 * It has no wall-time component — the counter only moves when the
 * caller calls @ref advance, @ref setCurrentFrame, or @ref reset.
 *
 * This is the right clock for any pipeline where each output frame is
 * exactly one @ref FrameRate period, decoupled from real time: file
 * writers that need a pristine frame rate, offline conversion, and
 * tests that want deterministic timing.
 *
 * @par Properties
 *
 * - Domain defaults to @ref ClockDomain::Synthetic.  Override via the
 *   constructor.
 * - @ref resolutionNs returns 1 (the stored counter is ns-accurate).
 * - @ref jitter returns @c {0, 0} — a SyntheticClock is perfect.
 * - @ref rateRatio returns @c 1.0.
 * - Cannot be paused.
 *
 * @par sleepUntilNs
 *
 * The base-class sleep is a no-op here — the counter is authoritative
 * and only @ref advance / @ref setCurrentFrame move time forward.
 */
class SyntheticClock : public Clock {
        public:
                /**
                 * @brief Constructs a SyntheticClock with no frame rate.
                 * @param domain Optional domain override.
                 */
                explicit SyntheticClock(
                        const ClockDomain &domain = ClockDomain(ClockDomain::Synthetic));

                /**
                 * @brief Constructs a SyntheticClock with the given frame rate.
                 * @param frameRate The target frame rate.
                 * @param domain    Optional domain override.
                 */
                explicit SyntheticClock(
                        const FrameRate &frameRate,
                        const ClockDomain &domain = ClockDomain(ClockDomain::Synthetic));

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
                 * @param frame The new counter value.
                 */
                void setCurrentFrame(int64_t frame);

                /** @brief Returns the current frame counter. */
                int64_t currentFrame() const {
                        return _currentFrame.load(std::memory_order_relaxed);
                }

                /**
                 * @brief Advances the frame counter.
                 * @param frames Number of frames to add (default 1).
                 */
                void advance(int64_t frames = 1);

                /**
                 * @brief Resets the frame counter to @p frame.
                 * @param frame Starting frame counter (default 0).
                 */
                void reset(int64_t frame = 0);

                int64_t     resolutionNs() const override;
                ClockJitter jitter() const override;

        protected:
                Result<int64_t> raw() const override;
                Error           sleepUntilNs(int64_t targetNs) const override;

        private:
                void recomputePeriod();

                FrameRate                   _frameRate;
                std::atomic<int64_t>        _framePeriodNs{0};
                std::atomic<int64_t>        _currentFrame{0};
};

PROMEKI_NAMESPACE_END
