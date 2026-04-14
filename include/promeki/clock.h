/**
 * @file      clock.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/clockdomain.h>
#include <promeki/duration.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Asymmetric bound on the error envelope of a clock's nowNs()
 *        reading relative to ground truth.
 * @ingroup time
 *
 * The error is defined as <tt>reportedTime - trueTime</tt>.
 *
 * - @c minError is the most negative value the clock is expected to
 *   return (early bias).  Typically @c <= 0.
 * - @c maxError is the most positive value the clock is expected to
 *   return (late bias).  Typically @c >= 0.
 *
 * The envelope is asymmetric by design.  For a steady clock like
 * @c std::chrono::steady_clock the envelope is symmetric and tiny
 * (@c ~1 ns on either side).  For a clock derived from an audio
 * device's consumed-byte counter the envelope is strictly one-sided
 * late (@c {0, bufferPeriod}) because the counter reads stale
 * between callbacks and jumps forward only when the device fires
 * its next callback.
 *
 * Both bounds are non-strict — a real clock may occasionally exceed
 * them — but they represent the expected operating envelope.  Code
 * that filters clock-derived signals (e.g. rate estimates) should
 * size its window according to @ref span.
 */
struct ClockJitter {
        /** @brief Most negative expected value of @c reportedTime - trueTime. */
        Duration minError;

        /** @brief Most positive expected value of @c reportedTime - trueTime. */
        Duration maxError;

        /** @brief Total width of the error envelope (@c maxError - minError). */
        Duration span() const { return maxError - minError; }

        /** @brief True when the envelope is @c [-maxError, +maxError]. */
        bool isSymmetric() const {
                return minError.nanoseconds() == -maxError.nanoseconds();
        }
};

/**
 * @brief Abstract clock interface.
 * @ingroup time
 *
 * A Clock provides a time source (@ref nowNs) and a blocking wait
 * primitive (@ref sleepUntilNs), paired with enough self-description
 * for callers to reason about its accuracy:
 *
 * - @ref domain returns the @ref ClockDomain that owns this clock.
 *   Timestamps from two clocks in the same domain are comparable.
 * - @ref resolutionNs tells callers the smallest meaningful time step
 *   the clock can distinguish.
 * - @ref jitter returns an asymmetric error envelope callers should
 *   assume when filtering clock-derived signals.
 * - @ref rateRatio exposes the clock's measured rate relative to its
 *   nominal rate.  @c 1.0 for locked clocks; audio clocks report
 *   their actual drain rate relative to the rate the stream was
 *   configured for.  This is the primary drift-correction signal
 *   for media-rate converters built on top of the clock.
 *
 * All times are in nanoseconds from the clock's own epoch.  The
 * epoch is opaque to callers — only deltas between @ref nowNs
 * readings and deadline values matter.
 *
 * @par Implementations
 *
 * - @ref WallClock — std::chrono::steady_clock, for local playback.
 * - @ref SyntheticClock — frame-count driven, for offline / test /
 *   "pristine rate" pipelines.
 * - @c SDLAudioClock — derives time from an SDL audio device's
 *   consumed-byte counter.
 * - PTP / hardware clocks (future).
 */
class Clock {
        public:
                virtual ~Clock() = default;

                /**
                 * @brief Returns the clock's identity domain.
                 *
                 * Timestamps produced by two clocks sharing the same
                 * @ref ClockDomain can be compared directly; timestamps
                 * from different domains may drift relative to each
                 * other.
                 *
                 * @return The clock's @ref ClockDomain.
                 */
                virtual ClockDomain domain() const = 0;

                /**
                 * @brief Smallest meaningful time step in nanoseconds.
                 *
                 * The resolution tells callers what precision to expect
                 * from @ref nowNs.  A 48 kHz audio clock has
                 * @c ~20833 ns resolution (one sample period); a
                 * steady_clock wall clock is near @c 1 ns; a PTP
                 * hardware clock is also @c ~1 ns.
                 *
                 * @return Resolution in nanoseconds (@c >= 1).
                 */
                virtual int64_t resolutionNs() const = 0;

                /**
                 * @brief Expected asymmetric error envelope on @ref nowNs.
                 *
                 * Used by consumers that filter clock-derived rate or
                 * offset estimates.  The filter window should cover at
                 * least @ref ClockJitter::span so that one reading's
                 * jitter is averaged out.
                 *
                 * @return The envelope as a @ref ClockJitter.
                 */
                virtual ClockJitter jitter() const = 0;

                /**
                 * @brief Returns the current time in nanoseconds.
                 *
                 * The epoch is clock-defined.  Only deltas between two
                 * @ref nowNs readings, or between @ref nowNs and a
                 * deadline supplied to @ref sleepUntilNs, are meaningful.
                 *
                 * @return Current time in nanoseconds from the clock's epoch.
                 */
                virtual int64_t nowNs() const = 0;

                /**
                 * @brief Blocks until the clock reaches @p targetNs.
                 *
                 * Implementation determines the waiting mechanism: an
                 * OS sleep for wall clocks, polling a queue depth for
                 * audio clocks, waiting on a hardware timer for PTP.
                 *
                 * If @p targetNs is in the past, the call returns
                 * immediately.  A @ref SyntheticClock implements this
                 * as a no-op — its notion of "now" is driven by
                 * explicit @c advance calls, not by sleeping.
                 *
                 * @param targetNs Target time in nanoseconds from the
                 *                 clock's epoch.
                 */
                virtual void sleepUntilNs(int64_t targetNs) = 0;

                /**
                 * @brief Ratio of actual tick rate to nominal tick rate.
                 *
                 * For clocks that are their own nominal reference
                 * (@ref WallClock, PTP, @ref SyntheticClock) the ratio
                 * is @c 1.0.  For clocks derived from an external rate
                 * source (audio device drain, a remote stream's packet
                 * rate), the ratio reports the actual rate measured
                 * against that reference, which callers can use as a
                 * drift-correction signal (e.g. feeding an audio
                 * resampler ratio).
                 *
                 * The default implementation returns @c 1.0.
                 */
                virtual double rateRatio() const { return 1.0; }
};

/**
 * @brief Wall-clock implementation of @ref Clock.
 * @ingroup time
 *
 * Uses @c std::chrono::steady_clock for timing and
 * @c std::this_thread::sleep_until for waiting.  Its domain is
 * @ref ClockDomain::SystemMonotonic, its jitter envelope is
 * near-zero, and its rate ratio is @c 1.0.
 */
class WallClock : public Clock {
        public:
                ClockDomain domain() const override;
                int64_t     resolutionNs() const override;
                ClockJitter jitter() const override;
                int64_t     nowNs() const override;
                void        sleepUntilNs(int64_t targetNs) override;
};

PROMEKI_NAMESPACE_END
