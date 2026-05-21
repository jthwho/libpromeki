/**
 * @file      gate.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_CORE
#include <cstdint>
#include <promeki/atomic.h>
#include <promeki/namespace.h>
#include <promeki/timestamp.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Single-shot atomic gate.
 * @ingroup concurrency
 *
 * Lightweight "first caller wins, others fall through" primitive.
 * Unlike @ref OnceFlag / @ref callOnce — which blocks competing
 * callers until the chosen one finishes — @ref OnceGate is
 * non-blocking: every caller of @ref fire returns immediately,
 * exactly one observes @c true (the first), all others observe
 * @c false.
 *
 * Intended for "do this thing at most once" gates where the work
 * is short, the losers should drop their attempt rather than wait,
 * and the side effect itself is the synchronisation (e.g. emit a
 * warning, set a sticky error, retire a deprecated path).
 *
 * Use @ref PROMEKI_ONCE for the common block-statement form.
 *
 * @par Thread Safety
 * Fully thread-safe.  @ref fire and @ref reset may be called
 * concurrently from any thread.
 *
 * @par Example
 * @code
 * static OnceGate g_warned;
 * if (g_warned.fire()) {
 *         promekiWarn("Codec '%s' is not supported on this platform", name);
 * }
 *
 * // ...or via the block macro at the call site:
 * PROMEKI_ONCE {
 *         promekiWarn("Codec '%s' is not supported on this platform", name);
 * }
 * @endcode
 *
 * @see PROMEKI_ONCE
 * @see ThrottleGate
 */
class OnceGate {
        public:
                /**
                 * @brief Attempts to fire the gate.
                 *
                 * Atomically transitions the gate to the "fired" state.
                 * The first caller observes @c true; every subsequent
                 * caller observes @c false until @ref reset is invoked.
                 *
                 * @return @c true exactly once between resets; @c false thereafter.
                 */
                bool fire() noexcept {
                        return !_fired.exchange(true, MemoryOrder::AcqRel);
                }

                /**
                 * @brief Returns whether the gate has been fired.
                 *
                 * Snapshot read; the answer may be stale by the time the
                 * caller looks at it.  Prefer @ref fire when the goal is
                 * "act only if not yet fired".
                 */
                bool hasFired() const noexcept {
                        return _fired.value();
                }

                /**
                 * @brief Re-arms the gate so the next @ref fire returns @c true again.
                 */
                void reset() noexcept {
                        _fired.setValue(false);
                }

        private:
                Atomic<bool> _fired{false};
};

/**
 * @brief Time-rate-limited atomic gate.
 * @ingroup concurrency
 *
 * Permits @ref fire to succeed at most once per @p intervalMs.
 * Subsequent attempts within the same interval increment a
 * suppressed-attempt counter that callers may inspect (typically
 * to report "[+N suppressed]" alongside the next emission).
 *
 * The interval is supplied per call to @ref fire rather than baked
 * into the gate, so a single gate can serve callers with
 * different tolerances if desired — but the typical pattern is one
 * gate per call site with a fixed interval, which the
 * @ref PROMEKI_THROTTLED block macro encapsulates.
 *
 * @par Thread Safety
 * Fully thread-safe.  Concurrent @ref fire calls race on a CAS;
 * exactly one caller per interval observes @c true.
 *
 * @par Example
 * @code
 * static ThrottleGate g_dropGate;
 * if (g_dropGate.fire(1000)) {
 *         promekiWarn("Frame dropped (queue=%u, +%llu suppressed)",
 *                     queue.size(), g_dropGate.consumeSuppressed());
 * }
 *
 * // ...or via the block macro at the call site:
 * PROMEKI_THROTTLED(1000) {
 *         promekiWarn("Frame dropped (queue=%u)", queue.size());
 * }
 * @endcode
 *
 * @see PROMEKI_THROTTLED
 * @see OnceGate
 */
class ThrottleGate {
        public:
                /**
                 * @brief Attempts to fire the gate.
                 *
                 * Succeeds (returns @c true) if at least @p intervalMs
                 * milliseconds have elapsed since the last successful
                 * fire on the steady clock.  On failure, increments
                 * @ref suppressedCount and returns @c false.
                 *
                 * The suppressed counter is @em not cleared on success
                 * — callers that want a "[+N suppressed]" style report
                 * should call @ref consumeSuppressed immediately after
                 * a successful fire.
                 *
                 * @param intervalMs Minimum spacing between successful fires.
                 *                   Non-positive values cause every call to fire.
                 * @return @c true if the gate fired; @c false otherwise.
                 */
                bool fire(int64_t intervalMs) noexcept {
                        if (intervalMs <= 0) return true;
                        const int64_t now = TimeStamp::now().milliseconds();
                        int64_t       prev = _lastMs.value();
                        for (;;) {
                                if (now - prev < intervalMs) {
                                        _suppressed.fetchAndAdd(1, MemoryOrder::Relaxed);
                                        return false;
                                }
                                if (_lastMs.compareAndSwap(prev, now)) return true;
                                // prev was updated by compareAndSwap; retry.
                        }
                }

                /**
                 * @brief Returns the number of attempts suppressed since the last successful fire.
                 *
                 * Snapshot read; concurrent @ref fire calls may change
                 * the value immediately after the read.
                 */
                uint64_t suppressedCount() const noexcept {
                        return _suppressed.value();
                }

                /**
                 * @brief Atomically reads and clears the suppressed counter.
                 *
                 * Typically called immediately after a successful
                 * @ref fire to attribute the suppressed attempts to the
                 * gate's next emission.
                 *
                 * @return The number of suppressed attempts since the last clear.
                 */
                uint64_t consumeSuppressed() noexcept {
                        return _suppressed.exchange(0, MemoryOrder::AcqRel);
                }

                /**
                 * @brief Re-arms the gate so the next @ref fire returns @c true
                 *        regardless of elapsed time, and clears the suppressed counter.
                 */
                void reset() noexcept {
                        _lastMs.setValue(INT64_MIN / 2);
                        _suppressed.setValue(0);
                }

        private:
                Atomic<int64_t>  _lastMs{INT64_MIN / 2};
                Atomic<uint64_t> _suppressed{0};
};

PROMEKI_NAMESPACE_END

/**
 * @def PROMEKI_ONCE
 * @ingroup concurrency
 * @brief Runs the following statement (or block) exactly once across the program's lifetime.
 *
 * Each expansion of the macro creates a function-local @c static
 * @ref OnceGate at the call site, so distinct call sites are
 * independent — the body is guaranteed to run only on the first
 * thread that reaches it; all subsequent callers skip the body
 * entirely.  Safe to use in any function (including templates and
 * inline functions); each translation unit may have its own gate
 * but inside a single TU the static is shared as expected.
 *
 * @par Example
 * @code
 * void process(const Frame &f) {
 *         if (f.format() == Unsupported) {
 *                 PROMEKI_ONCE {
 *                         promekiWarn("Frame with unsupported format dropped (logged once)");
 *                 }
 *                 return;
 *         }
 *         ...
 * }
 * @endcode
 *
 * @note Implemented as a @c for loop so the body sees the surrounding
 *       scope normally and can use @c break / @c continue if desired.
 */
#define PROMEKI_ONCE                                                                                                   \
        for (static ::promeki::OnceGate _promeki_once_gate; _promeki_once_gate.fire();)

/**
 * @def PROMEKI_THROTTLED
 * @ingroup concurrency
 * @brief Runs the following statement (or block) at most once per @p intervalMs.
 *
 * Each expansion creates a function-local @c static
 * @ref ThrottleGate at the call site.  The first caller per
 * interval runs the body; subsequent callers within the same
 * interval are silently suppressed (the gate's suppressed-attempt
 * counter is incremented but not consulted by this macro — use the
 * gate directly if you need to report it).
 *
 * @param intervalMs Minimum milliseconds between successive runs of the body.
 *
 * @par Example
 * @code
 * void onFrameDrop(uint32_t pending) {
 *         PROMEKI_THROTTLED(1000) {
 *                 promekiWarn("Frame dropped, queue=%u", pending);
 *         }
 * }
 * @endcode
 */
#define PROMEKI_THROTTLED(intervalMs)                                                                                  \
        for (static ::promeki::ThrottleGate _promeki_throttled_gate;                                                   \
             _promeki_throttled_gate.fire(static_cast<int64_t>(intervalMs));)

#endif // PROMEKI_ENABLE_CORE
