/**
 * @file      elapsedtimer.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <chrono>
#include <cstdint>
#include <promeki/namespace.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Monotonic elapsed-time measurement using std::chrono::steady_clock.
 * @ingroup time
 *
 * The timer starts automatically on construction. Call start() or restart()
 * to reset. The timer can be invalidated to indicate "not running".
 *
 * @par Thread Safety
 * Conditionally thread-safe.  Distinct instances may be used
 * concurrently; concurrent @c start / @c restart / @c invalidate
 * on a single instance must be externally synchronized.  Read
 * accessors (@c elapsedMs / @c elapsedSeconds / @c isValid) are
 * safe alongside other reads.
 */
class ElapsedTimer {
        public:
                /**
                 * @brief Constructs and starts the timer.
                 */
                ElapsedTimer() : _valid(true), _start(std::chrono::steady_clock::now()) {}

                /**
                 * @brief Records the current time as the start point.
                 */
                void start() {
                        _start = std::chrono::steady_clock::now();
                        _valid = true;
                }

                /**
                 * @brief Restarts the timer and returns the elapsed time since the previous start.
                 * @return Milliseconds elapsed since the previous start.
                 */
                int64_t restart() {
                        auto    now = std::chrono::steady_clock::now();
                        int64_t ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - _start).count();
                        _start = now;
                        _valid = true;
                        return ms;
                }

                /**
                 * @brief Returns milliseconds elapsed since start.
                 * @return Elapsed time in milliseconds.
                 */
                int64_t elapsed() const {
                        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                                                     _start)
                                .count();
                }

                /**
                 * @brief Returns microseconds elapsed since start.
                 * @return Elapsed time in microseconds.
                 */
                int64_t elapsedUs() const {
                        return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() -
                                                                                     _start)
                                .count();
                }

                /**
                 * @brief Returns nanoseconds elapsed since start.
                 * @return Elapsed time in nanoseconds.
                 */
                int64_t elapsedNs() const {
                        return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() -
                                                                                    _start)
                                .count();
                }

                /**
                 * @brief Returns true if the elapsed time is at least @p ms milliseconds.
                 * @param ms The threshold in milliseconds.
                 * @return True if elapsed >= ms.
                 */
                bool hasExpired(int64_t ms) const { return elapsed() >= ms; }

                /**
                 * @brief Returns true if the timer has been started and not invalidated.
                 * @return True if valid.
                 */
                bool isValid() const { return _valid; }

                /**
                 * @brief Invalidates the timer, marking it as not running.
                 */
                void invalidate() { _valid = false; }

        private:
                bool                                  _valid;
                std::chrono::steady_clock::time_point _start;
};

PROMEKI_NAMESPACE_END
