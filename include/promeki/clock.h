/**
 * @file      clock.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <atomic>
#include <cassert>
#include <mutex>
#include <promeki/namespace.h>
#include <promeki/clockdomain.h>
#include <promeki/duration.h>
#include <promeki/error.h>
#include <promeki/mediatimestamp.h>
#include <promeki/result.h>
#include <promeki/sharedptr.h>
#include <promeki/uniqueptr.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Asymmetric bound on the error envelope of a clock's now()
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
 * @brief Pluggable smoothing filter applied to a clock's raw ns reading.
 * @ingroup time
 *
 * Clock takes ownership of the filter passed to its constructor and
 * deletes it when the clock is destroyed.  Filters may hold internal
 * state across calls and are invoked under the clock's internal mutex,
 * so implementations do not need to be independently thread-safe.
 *
 * @par Example
 * @code
 * class ExponentialAverageFilter : public ClockFilter {
 *         public:
 *                 int64_t filter(int64_t rawNs) override {
 *                         _acc = _acc + (rawNs - _acc) / 8;
 *                         return _acc;
 *                 }
 *         private:
 *                 int64_t _acc = 0;
 * };
 * @endcode
 */
class ClockFilter {
        public:
                /** @brief Unique-ownership pointer to a ClockFilter. */
                using UPtr = UniquePtr<ClockFilter>;

                virtual ~ClockFilter() = default;

                /**
                 * @brief Transforms a raw ns reading.
                 * @param rawNs The raw value from the clock's @ref Clock::raw.
                 * @return The filtered ns value.
                 */
                virtual int64_t filter(int64_t rawNs) = 0;
};

/**
 * @brief Describes a clock's pause capability.
 * @ingroup time
 *
 * The base @ref Clock applies the same delta-based pause accounting
 * for both paused modes:  at pause it snapshots the filtered raw
 * value; on resume it adds <tt>raw_now - raw_at_pause</tt> to the
 * paused-offset accumulator.  The enum exists so callers can
 * distinguish @c CannotPause (where @ref Clock::setPause will
 * error) and so consumers can reason about whether the underlying
 * clock source actually stops.
 */
enum class ClockPauseMode {
        CannotPause,              ///< @ref Clock::setPause returns NotSupported.
        PausesRawKeepsRunning,    ///< Pause is bookkeeping only; raw() keeps advancing.
        PausesRawStops            ///< onPause(true) freezes raw(); onPause(false) resumes.
};

/**
 * @brief Abstract clock interface.
 * @ingroup time
 *
 * A Clock is a reference-counted, polymorphic time source with a
 * managed @ref now that applies filtering, pause accounting, fixed
 * offset and a monotonic clamp on top of the subclass's pure-virtual
 * @ref raw reading.
 *
 * @par Construction
 *
 * Derived clocks pass their @ref ClockDomain, any fixed offset from
 * that domain's epoch, their pause capability, and an optional
 * @ref ClockFilter pointer.  The clock takes ownership of the filter
 * and deletes it on destruction.  Pass @c nullptr for no filtering.
 *
 * @par Sharing
 *
 * Clocks are natively reference-counted.  Callers manage lifetime via
 * @ref Clock::Ptr .  Clocks are not copyable — the internal clone
 * helper asserts if invoked.
 *
 * @par Implementations
 *
 * - @ref WallClock — std::chrono::steady_clock, for local playback.
 * - @ref SyntheticClock — frame-count driven, for offline pipelines.
 * - @c SDLAudioClock — derived from an SDL audio device's drain rate.
 * - @c MediaIOClock — derived from a MediaIO's frame position.
 */
class Clock {
        public:
                /**
                 * @brief Shared-pointer alias for lifetime management.
                 *
                 * The third template parameter pins the storage type to
                 * @ref Clock directly — without it, the SharedPtr
                 * instantiation here inside the (still-incomplete)
                 * Clock definition cannot see @ref _promeki_refct via
                 * SFINAE and falls back to a heap-allocating
                 * SharedPtrProxy wrapper.
                 */
                using Ptr = SharedPtr<Clock, /*CopyOnWrite=*/false, Clock>;

                /**
                 * @brief Constructs a Clock.
                 * @param domain     The clock's identity @ref ClockDomain.
                 * @param fixedOffset Fixed delay applied on top of raw().
                 *                    May be negative.  Mutable via
                 *                    @ref setFixedOffset.
                 * @param pauseMode  Pause capability (default CannotPause).
                 * @param filter     Optional filter; the clock takes
                 *                   ownership.  @c nullptr disables
                 *                   filtering.
                 */
                Clock(const ClockDomain &domain,
                      const Duration &fixedOffset = Duration(),
                      ClockPauseMode pauseMode = ClockPauseMode::CannotPause,
                      ClockFilter *filter = nullptr);

                /** @brief Virtual destructor. */
                virtual ~Clock();

                Clock(const Clock &) = delete;
                Clock &operator=(const Clock &) = delete;
                Clock(Clock &&) = delete;
                Clock &operator=(Clock &&) = delete;

                // ---- Identity / self-description ----

                /** @brief Returns the clock's identity domain. */
                ClockDomain domain() const { return _domain; }

                /** @brief Returns the clock's pause capability. */
                ClockPauseMode pauseMode() const { return _pauseMode; }

                /** @brief True if @ref setPause will accept requests. */
                bool canPause() const { return _pauseMode != ClockPauseMode::CannotPause; }

                /**
                 * @brief Smallest meaningful time step in nanoseconds.
                 * @return Resolution in nanoseconds (@c >= 1).
                 */
                virtual int64_t resolutionNs() const = 0;

                /**
                 * @brief Expected asymmetric error envelope on @ref now.
                 * @return The envelope as a @ref ClockJitter.
                 */
                virtual ClockJitter jitter() const = 0;

                /**
                 * @brief Ratio of actual tick rate to nominal tick rate.
                 * @return The rate ratio (@c 1.0 for self-referenced clocks).
                 */
                virtual double rateRatio() const { return 1.0; }

                // ---- Managed readings ----

                /**
                 * @brief Returns the current time as a @ref MediaTimeStamp.
                 *
                 * Applies, in order: @ref raw, then the filter (if any),
                 * then the paused-snapshot freeze (if paused), then the
                 * combined fixed + paused offsets, then a monotonic clamp
                 * against the last value returned.  The returned
                 * @ref MediaTimeStamp carries this clock's domain and the
                 * total offset that was in effect.
                 *
                 * @return The timestamp, or an error propagated from raw().
                 */
                Result<MediaTimeStamp> now() const;

                /**
                 * @brief Convenience: the ns value @ref now would carry.
                 *
                 * Equivalent to <tt>now()</tt> followed by extracting
                 * <tt>timeStamp().nanoseconds()</tt>.  Errors propagate
                 * identically.
                 */
                Result<int64_t> nowNs() const;

                // ---- Offsets ----

                /** @brief Returns the currently-set fixed offset. */
                Duration fixedOffset() const;

                /**
                 * @brief Replaces the fixed offset.
                 *
                 * Subsequent @ref now calls apply the new offset.  The
                 * monotonic clamp is not reset — changing the offset can
                 * only increase the reported time, never decrease it.
                 *
                 * @param offset The new fixed offset (may be negative).
                 */
                void setFixedOffset(const Duration &offset);

                // ---- Pause ----

                /** @brief True if the clock is currently paused. */
                bool isPaused() const;

                /**
                 * @brief Sets the pause state.
                 *
                 * Calls @ref onPause to let the subclass stop or resume
                 * any underlying resource.  On pause, snapshots the
                 * filtered raw value; on resume, adds the accumulated
                 * raw delta to the paused-offset accumulator so that the
                 * reported time resumes seamlessly.
                 *
                 * @param paused Target state.
                 * @return Error::NotSupported if !canPause(), otherwise
                 *         forwards the result of @ref onPause (which is
                 *         typically Error::Ok).
                 */
                Error setPause(bool paused);

                // ---- Wait primitives ----

                /**
                 * @brief Blocks until @ref now would return at or past @p deadline.
                 *
                 * Verifies @p deadline is in this clock's domain and
                 * translates its value into the current raw timebase by
                 * adding the clock's current fixed + paused offsets, so
                 * a mid-wait offset change does what callers expect
                 * ("wake when @ref now reads X").  The stored
                 * @ref MediaTimeStamp::offset is informational.
                 *
                 * @param deadline When to wake, expressed in this clock's
                 *                 domain.
                 * @return Error::Ok on successful wake,
                 *         Error::ClockDomainMismatch when the deadline
                 *         belongs to another domain,
                 *         Error::ClockPaused when the clock is paused
                 *         (either at entry or during the wait — the
                 *         caller should retry after resume), or
                 *         whatever error the subclass's
                 *         @ref sleepUntilNs produces.
                 */
                Error sleepUntil(const MediaTimeStamp &deadline) const;

                // Native shared-pointer plumbing.  Clock is not copyable;
                // _promeki_clone asserts if the SharedPtr ever tries to
                // detach, which should be impossible for CopyOnWrite=false.
                /** @brief Atomic reference count for @ref SharedPtr. */
                RefCount _promeki_refct;

                /**
                 * @brief Clone hook for @ref SharedPtr.  Always asserts —
                 *        Clock objects are not copyable.
                 */
                virtual Clock *_promeki_clone() const {
                        assert(false && "Clock is not copyable");
                        return nullptr;
                }

        protected:
                /**
                 * @brief Returns the raw clock value in nanoseconds.
                 *
                 * The epoch is clock-defined.  The base class applies
                 * the filter, pause accounting, and offsets on top.
                 * An error result indicates the clock has become
                 * unavailable (for example, the underlying hardware or
                 * owner went away); the error propagates from @ref now.
                 */
                virtual Result<int64_t> raw() const = 0;

                /**
                 * @brief Native blocking wait in raw clock nanoseconds.
                 *
                 * Invoked by @ref sleepUntil after domain validation
                 * and offset translation.  If @p targetNs is in the
                 * past, implementations should return Error::Ok
                 * immediately.  Implementations backed by a
                 * disappearing resource (audio device, remote clock)
                 * return the relevant error when the wait cannot
                 * complete.
                 *
                 * @param targetNs Raw deadline in this clock's epoch.
                 * @return Error::Ok on successful wake.
                 */
                virtual Error sleepUntilNs(int64_t targetNs) const = 0;

                /**
                 * @brief Called when pause state changes.
                 *
                 * Subclasses whose underlying source actually stops on
                 * pause (@ref ClockPauseMode::PausesRawStops) override
                 * this to start and stop the hardware.  Called while
                 * the Clock's internal pause mutex is held, so
                 * implementations must not call back into the Clock's
                 * public API.  Default returns @c Error::Ok.
                 *
                 * @param paused The new pause state.
                 */
                virtual Error onPause(bool paused);

        private:
                Result<int64_t> applyFilter(int64_t raw) const;

                ClockDomain                   _domain;
                ClockPauseMode                _pauseMode;
                ClockFilter::UPtr             _filter;

                mutable std::mutex            _mutex;
                int64_t                       _fixedOffsetNs;
                int64_t                       _pausedOffsetNs;
                int64_t                       _frozenFilteredNs;
                bool                          _paused;

                mutable std::atomic<int64_t>  _lastNowNs;
};

/**
 * @brief Wall-clock implementation of @ref Clock.
 * @ingroup time
 *
 * Uses @c std::chrono::steady_clock for timing and
 * @c std::this_thread::sleep_until for waiting.  Its domain is
 * @ref ClockDomain::SystemMonotonic, its jitter envelope is
 * near-zero, and its rate ratio is @c 1.0.  Cannot be paused and
 * applies no filtering by default.
 */
class WallClock : public Clock {
        public:
                /** @brief Constructs a WallClock in the SystemMonotonic domain. */
                WallClock();

                int64_t     resolutionNs() const override;
                ClockJitter jitter() const override;

        protected:
                Result<int64_t> raw() const override;
                Error           sleepUntilNs(int64_t targetNs) const override;
};

PROMEKI_NAMESPACE_END
