/**
 * @file      cadence.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_CORE
#include <cstdint>
#include <promeki/duration.h>
#include <promeki/namespace.h>
#include <promeki/timestamp.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Deadline-anchored packet pacer.
 * @ingroup time
 *
 * @c Cadence emits a sequence of monotone deadlines spaced by a
 * fixed @ref interval, anchored to the first deadline supplied via
 * @ref anchor.  Each call to @ref next returns the *next* deadline
 * the caller should sleep until and advances the internal cursor by
 * @ref interval.  Anchored deadlines (@c t0 + N × interval) instead
 * of accumulated @c sleep_for(interval) is what keeps per-packet
 * drift from accumulating: a worker that misses a few deadlines
 * catches up at the next call without dragging the wire schedule.
 *
 * @par Long-stall recovery
 * @ref reanchor lets the caller skip the back-pressure burst that
 * would otherwise happen after a long stall: instead of returning
 * a deadline already in the past (which makes @c sleep_until a
 * no-op and floods the wire), the cadence is re-pinned to
 * @c t + interval so the next emission lands one full interval
 * after the stall recovered.  @ref ticks remains monotone across
 * @ref reanchor so per-stream stats reflect total emissions, not
 * just emissions since the last anchor.
 *
 * @par Use cases
 * @c AudioTxThread paces AES67 packet emission at the configured
 * @c packetTimeUs (typically 1 ms).  @c VideoTxThread::Userspace
 * paces user-space spread of an access unit's packets across one
 * frame interval.  Future SCM_TXTIME paths use the same deadlines
 * as kernel cmsg values rather than userspace sleeps.
 *
 * @par Thread safety
 * @c Cadence carries no internal synchronization.  Each instance is
 * meant to be owned by one thread (the TX thread driving the
 * cadence); cross-thread reads of @ref ticks are safe only via the
 * caller's external lock.
 *
 * @par Example
 * @code
 * Cadence pacer(Duration::fromMicroseconds(1000));   // 1 ms cadence
 * pacer.anchor(TimeStamp::now());
 * for (int i = 0; i < N; ++i) {
 *         TimeStamp deadline = pacer.next();
 *         deadline.sleepUntil();
 *         emitPacket();
 * }
 * @endcode
 */
class Cadence {
        public:
                /**
                 * @brief Constructs a @c Cadence with the supplied
                 *        per-tick interval.
                 *
                 * The cadence is unanchored until @ref anchor (or
                 * the first @ref next call) installs a reference
                 * instant.  Calling @ref next on an unanchored
                 * cadence returns @c TimeStamp() (epoch) and
                 * advances the cursor — the contract is that
                 * callers must @ref anchor before the first
                 * @ref next.
                 *
                 * @param interval Per-tick interval.  Must be > 0;
                 *                 a zero or negative interval
                 *                 collapses to a no-op cadence
                 *                 (every @ref next returns the
                 *                 same deadline).
                 */
                explicit Cadence(const Duration &interval);

                /**
                 * @brief Pins the next emission deadline to @p t0.
                 *
                 * After @c anchor(t0), the *first* @ref next call
                 * returns @c t0; the second returns
                 * @c t0 + interval, etc.  Resets @ref ticks back
                 * to @c 0 — use @ref reanchor (not @c anchor) when
                 * recovering from a stall mid-stream so per-stream
                 * stats stay monotone.
                 *
                 * @param t0 Absolute deadline of the first
                 *           emission.
                 */
                void anchor(const TimeStamp &t0);

                /**
                 * @brief Returns the next deadline and advances the
                 *        internal cursor.
                 *
                 * Each call advances by @ref interval; consecutive
                 * results are strictly increasing as long as
                 * @ref interval > 0.  Returning the deadline before
                 * advancing means the caller can use the result
                 * directly as a @c sleep_until argument without
                 * additional bookkeeping.
                 */
                TimeStamp next();

                /**
                 * @brief Re-pins the cadence to @p t + interval
                 *        without resetting @ref ticks.
                 *
                 * Use this on long stalls (>N × interval) when the
                 * accumulated deadline is so far in the past that
                 * naively continuing would burst-emit dozens of
                 * packets back-to-back.  The next @ref next call
                 * will return @c t + interval, so emission resumes
                 * one interval after recovery rather than producing
                 * a back-to-back burst.
                 *
                 * @param t Recovery instant.  Conventionally
                 *          @c TimeStamp::now() in the TX thread,
                 *          but any caller-determined instant
                 *          works.
                 */
                void reanchor(const TimeStamp &t);

                /**
                 * @brief Returns the total number of @ref next calls
                 *        since construction.
                 *
                 * Monotone across @ref reanchor — re-pinning the
                 * cadence does not reset the counter.  Useful for
                 * per-stream emission stats.
                 */
                uint64_t ticks() const;

                /// @brief Returns the configured per-tick interval.
                Duration interval() const;

        private:
                Duration  _interval;
                TimeStamp _next;
                uint64_t  _ticks = 0;
                bool      _anchored = false;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_CORE
