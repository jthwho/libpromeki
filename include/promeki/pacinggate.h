/**
 * @file      pacinggate.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/clock.h>
#include <promeki/duration.h>
#include <promeki/error.h>
#include <promeki/mediatimestamp.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Outcome of a single @ref PacingGate::wait call.
 * @ingroup time
 *
 * The verdict is the gate's recommendation to the caller — it does
 * not prescribe an action.  Backends decide what to do based on what
 * they can do (a sender with no cross-fade just drops on
 * @ref Skip; a sender with an SRC may compress the next block instead).
 */
enum class PacingVerdict {
        /// Slept until the deadline; proceed normally.
        OnTime,
        /// Past the deadline by less than the skip threshold; proceed
        /// without sleeping.  No catch-up action is recommended — the
        /// drift is small enough to absorb.
        Late,
        /// Past the deadline by at least the skip threshold but less
        /// than the reanchor threshold; the backend should drop /
        /// fast-forward this work item to bound the lag.
        Skip,
        /// Past the deadline by at least the reanchor threshold; the
        /// gap is unrecoverable, the gate has re-anchored to
        /// @c clock->now() so the next wait() restarts the timeline.
        Reanchor,
};

/**
 * @brief Result of a single @ref PacingGate::wait call.
 * @ingroup time
 */
struct PacingResult {
                /// @brief What the gate recommends.
                PacingVerdict verdict = PacingVerdict::OnTime;

                /// @brief Difference between the deadline and the
                /// observed @c clock->now() at the wait point.
                ///
                /// Positive means the deadline was in the future and
                /// the gate slept (the magnitude is roughly the sleep
                /// duration).  Negative means the deadline had already
                /// passed by that much (the magnitude is the lag).
                /// Zero on the first wait after a re-anchor and on
                /// no-op (null clock) returns.
                Duration slack;

                /// @brief Number of complete period intervals the gate
                /// is currently behind by, when @ref verdict is
                /// @ref PacingVerdict::Skip.
                ///
                /// Zero for every other verdict.  A backend with a
                /// cross-fade or SRC can use this count to collapse N
                /// blocks worth of drift into a single shorter block
                /// instead of dropping; backends without that
                /// capability ignore it and just drop the current
                /// work item once.
                int skippedTicks = 0;

                /// @brief Underlying clock error if the gate could not
                /// query @c now() or perform the sleep.
                ///
                /// Errors are reported through this field rather than
                /// changing @ref verdict so the caller sees both
                /// "what was the timing intent" and "what went
                /// wrong" in a single result.  The verdict in an
                /// error case is @ref PacingVerdict::OnTime by
                /// convention (no action recommended; the timeline
                /// has not advanced from the gate's view).
                Error error = Error::Ok;
};

/**
 * @brief Reusable scheduling helper that paces a series of work items
 *        against an external @ref Clock.
 * @ingroup time
 *
 * Backends that need to emit work at a steady cadence against a clock
 * they don't own (a capture-card video clock fed forward to an NDI
 * sender, an AES67 PTP clock fed forward to an RTP sender) embed a
 * @c PacingGate per stream.  Each call to @ref wait advances the
 * gate's internal timeline by one period (or a caller-supplied
 * duration), sleeps if there's slack, and returns a verdict the
 * caller acts on.
 *
 * @par Lifecycle
 *
 * 1. Construct with no clock (or pass one in).
 * 2. Set the clock via @ref setClock when one becomes available
 *    (typically in the backend's @c executeCmd(MediaIOCommandSetClock)
 *    handler).
 * 3. Set the per-tick @ref setPeriod (defaults to zero — the no-arg
 *    @ref wait then is a no-op).
 * 4. Call @ref wait once per work item.  The first call after binding
 *    a clock latches an anchor against @c clock->now() and returns
 *    immediately so the first item ships without delay.  Subsequent
 *    calls sleep against @c anchor + cumulative_advance.
 *
 * @par Verdict thresholds
 *
 * - @ref setSkipThreshold (default: one period) — once the lag exceeds
 *   this, @ref wait returns @ref PacingVerdict::Skip.
 * - @ref setReanchorThreshold (default: eight periods) — once the lag
 *   exceeds this, @ref wait re-anchors and returns
 *   @ref PacingVerdict::Reanchor.  Both thresholds are absolute
 *   durations; a backend whose period changes mid-stream should
 *   re-set them after the period change.
 *
 * @par Telemetry
 *
 * Cumulative counters (@ref ticksOnTime, @ref ticksLate,
 * @ref ticksSkipped, @ref reanchors) are exposed for backends that
 * roll them into @ref MediaIOStats — no per-call allocation, no
 * locking, just integers updated on each @ref wait.
 *
 * @par Thread Safety
 *
 * Not internally synchronized.  Each @c PacingGate is meant to be
 * embedded in a backend's thread-confined state — typically the
 * dedicated worker thread of a @ref DedicatedThreadMediaIO or the
 * strand of a @ref SharedThreadMediaIO.  External serialization is
 * required if multiple threads need access.
 */
class PacingGate {
        public:
                /** @brief Default factor of @ref period for @ref setReanchorThreshold. */
                static constexpr int DefaultReanchorMultiple = 8;

                /** @brief Constructs an unconfigured gate (no clock, zero period). */
                PacingGate() = default;

                /**
                 * @brief Constructs a gate bound to @p clock with @p period.
                 * @param clock  The pacing clock; may be null.
                 * @param period Per-tick interval used by the no-arg @ref wait.
                 */
                explicit PacingGate(const Clock::Ptr &clock, const Duration &period = Duration());

                // ---- Configuration ----

                /**
                 * @brief Replaces the pacing clock.
                 *
                 * Re-arms the gate so the next @ref wait latches a
                 * fresh anchor against the new clock's @c now().  Pass
                 * a null Ptr to disable pacing — subsequent waits
                 * become no-ops that return @c OnTime with zero slack.
                 *
                 * @param clock The new clock, or null to disable.
                 */
                void               setClock(const Clock::Ptr &clock);

                /** @brief Returns the bound clock (may be null). */
                const Clock::Ptr  &clock() const { return _clock; }

                /** @brief True when a non-null clock is bound. */
                bool               hasClock() const { return _clock.isValid(); }

                /**
                 * @brief Sets the default per-tick period.
                 *
                 * Used by the no-arg @ref wait overload.  Setting a
                 * new period also resets the skip + reanchor
                 * thresholds to their defaults
                 * (@c period and
                 * @c DefaultReanchorMultiple * period) unless the
                 * caller previously set explicit values via
                 * @ref setSkipThreshold / @ref setReanchorThreshold.
                 *
                 * @param period The new per-tick interval.
                 */
                void               setPeriod(const Duration &period);

                /** @brief Returns the configured per-tick period. */
                const Duration    &period() const { return _period; }

                /**
                 * @brief Sets the skip-verdict lag threshold.
                 *
                 * Once @c (now - deadline) exceeds this duration the
                 * gate returns @ref PacingVerdict::Skip.  Default:
                 * one @ref period.
                 */
                void               setSkipThreshold(const Duration &t);

                /** @brief Returns the skip-verdict lag threshold. */
                const Duration    &skipThreshold() const { return _skipThreshold; }

                /**
                 * @brief Sets the reanchor-verdict lag threshold.
                 *
                 * Once @c (now - deadline) exceeds this duration the
                 * gate re-anchors and returns
                 * @ref PacingVerdict::Reanchor.  Default:
                 * @ref DefaultReanchorMultiple * @ref period.
                 */
                void               setReanchorThreshold(const Duration &t);

                /** @brief Returns the reanchor-verdict lag threshold. */
                const Duration    &reanchorThreshold() const { return _reanchorThreshold; }

                /**
                 * @brief Re-arms the gate so the next @ref wait latches
                 *        a fresh anchor against @c clock->now().
                 *
                 * Call after a backend-internal event that invalidates
                 * the previous timeline (a seek, a format change, a
                 * resume after pause).  Cheap — a single bool flip.
                 */
                void               rearm();

                // ---- Wait ----

                /**
                 * @brief Sleeps until the next deadline and returns a verdict.
                 *
                 * On the first call after construction or @ref rearm,
                 * latches @c anchor = clock->now() and returns
                 * immediately with @c OnTime / zero slack so the first
                 * work item ships without delay.  Subsequent calls
                 * compute @c deadline = anchor + accumulated and sleep
                 * via @c clock->sleepUntil(deadline).
                 *
                 * No-op when the clock is null (returns @c OnTime,
                 * zero slack, @c Error::Ok).
                 *
                 * @param advance How much to advance the timeline by
                 *                for this work item.  For fixed-rate
                 *                streams, pass @ref period (or use
                 *                the no-arg overload).  For
                 *                variable-rate streams (e.g. audio
                 *                blocks of irregular sample counts),
                 *                pass the per-block duration.
                 * @return        @ref PacingResult describing what the
                 *                gate did and what the caller should
                 *                consider doing.
                 */
                PacingResult       wait(const Duration &advance);

                /**
                 * @brief Convenience overload using @ref period.
                 * @return @ref wait(period())
                 */
                PacingResult       wait() { return wait(_period); }

                /**
                 * @brief Non-blocking "is the next tick due?" probe.
                 *
                 * Counterpart to @ref wait for callers that want a
                 * rate-limiter shape rather than a pacer shape.  Where
                 * @ref wait sleeps to slow a fast pump down to the
                 * clock's cadence, @ref tryAcquire just answers "yes,
                 * advance the timeline" or "no, you arrived early —
                 * drop / no-op."  Used by sinks that simulate real
                 * playback devices: such a sink can only consume one
                 * frame per period, and an early arrival is a drop
                 * rather than something to wait for.
                 *
                 * Behavior:
                 *  - No clock bound → always returns @c true (no
                 *    pacing constraint to enforce).  The timeline
                 *    does not advance — there is no deadline to track.
                 *  - First call after construction / @ref rearm /
                 *    @ref setClock → latches the anchor against
                 *    @c clock->now() and returns @c true.
                 *  - Subsequent call with @c now >= anchor +
                 *    accumulated + advance → advances the timeline by
                 *    @c advance and returns @c true.  If the new lag
                 *    after the advance exceeds
                 *    @ref reanchorThreshold the gate re-anchors and
                 *    counts a reanchor.
                 *  - Otherwise (now < deadline, "arrived early") →
                 *    returns @c false; the timeline does not advance.
                 *  - Clock @c now() failure → returns @c false (treat
                 *    as "not due"); callers needing the underlying
                 *    error should call @ref wait.
                 *
                 * @param advance The per-tick interval.
                 * @return @c true to consume, @c false to drop.
                 */
                bool               tryAcquire(const Duration &advance);

                /**
                 * @brief Convenience overload using @ref period.
                 * @return @ref tryAcquire(period())
                 */
                bool               tryAcquire() { return tryAcquire(_period); }

                // ---- Telemetry ----

                /** @brief Total @c OnTime returns since construction. */
                int64_t            ticksOnTime() const { return _ticksOnTime; }

                /** @brief Total @c Late returns since construction. */
                int64_t            ticksLate() const { return _ticksLate; }

                /** @brief Total @c Skip returns since construction. */
                int64_t            ticksSkipped() const { return _ticksSkipped; }

                /** @brief Total @c Reanchor returns since construction. */
                int64_t            reanchors() const { return _reanchors; }

                /** @brief Total @ref tryAcquire calls that returned @c false (early arrivals). */
                int64_t            tryAcquireRejected() const { return _tryAcquireRejected; }

                /** @brief True once the gate has latched an anchor. */
                bool               isArmed() const { return _armed; }

                /**
                 * @brief Last latched anchor (invalid until first @ref wait).
                 *
                 * Exposed primarily for tests; callers should not
                 * depend on the anchor's value as a stable identity
                 * — it changes on every @ref rearm and every
                 * @ref Reanchor.
                 */
                const MediaTimeStamp &anchor() const { return _anchor; }

                /** @brief Cumulative advance applied since the latest anchor. */
                const Duration &accumulated() const { return _accumulated; }

        private:
                Clock::Ptr     _clock;
                Duration       _period;
                Duration       _skipThreshold;
                Duration       _reanchorThreshold;
                MediaTimeStamp _anchor;
                Duration       _accumulated;
                bool           _armed = false;
                bool           _customSkipThreshold = false;
                bool           _customReanchorThreshold = false;
                int64_t        _ticksOnTime = 0;
                int64_t        _ticksLate = 0;
                int64_t        _ticksSkipped = 0;
                int64_t        _reanchors = 0;
                int64_t        _tryAcquireRejected = 0;
};

PROMEKI_NAMESPACE_END
