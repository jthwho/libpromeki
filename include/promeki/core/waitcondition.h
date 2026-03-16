/**
 * @file      core/waitcondition.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <chrono>
#include <type_traits>
#include <condition_variable>
#include <promeki/core/namespace.h>
#include <promeki/core/error.h>
#include <promeki/core/mutex.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Condition variable wrapping std::condition_variable.
 * @ingroup core_concurrency
 *
 * Used with Mutex to allow threads to wait for a condition to become
 * true.  Non-copyable and non-movable.
 */
class WaitCondition {
        public:
                /** @brief Constructs a WaitCondition. */
                WaitCondition() = default;

                /** @brief Destructor. */
                ~WaitCondition() = default;

                WaitCondition(const WaitCondition &) = delete;
                WaitCondition &operator=(const WaitCondition &) = delete;
                WaitCondition(WaitCondition &&) = delete;
                WaitCondition &operator=(WaitCondition &&) = delete;

                /**
                 * @brief Waits until woken or the timeout expires.
                 *
                 * The caller must hold @p mutex locked.  The mutex is atomically
                 * released while waiting and re-acquired before returning.
                 *
                 * @param mutex The mutex to wait on.
                 * @param timeoutMs Maximum time to wait in milliseconds.
                 *        A value of 0 (the default) waits indefinitely.
                 * @return Error::Ok if woken, Error::Timeout if the timeout elapsed.
                 */
                Error wait(Mutex &mutex, unsigned int timeoutMs = 0) {
                        std::unique_lock<std::mutex> lock(mutex._mutex, std::adopt_lock);
                        if(timeoutMs == 0) {
                                _cv.wait(lock);
                                lock.release();
                                return Error::Ok;
                        }
                        std::cv_status status = _cv.wait_for(lock,
                                std::chrono::milliseconds(timeoutMs));
                        lock.release();
                        return status == std::cv_status::no_timeout
                                ? Error::Ok : Error::Timeout;
                }

                /**
                 * @brief Waits until a predicate becomes true or the timeout expires.
                 *
                 * The caller must hold @p mutex locked.  The mutex is atomically
                 * released while waiting and re-acquired before returning.
                 *
                 * @tparam Predicate A callable returning bool.
                 * @param mutex The mutex to wait on.
                 * @param pred The predicate to check.
                 * @param timeoutMs Maximum time to wait in milliseconds.
                 *        A value of 0 (the default) waits indefinitely.
                 * @return Error::Ok if the predicate became true,
                 *         Error::Timeout if the timeout elapsed.
                 */
                template <typename Predicate,
                          typename = std::enable_if_t<std::is_invocable_r_v<bool, Predicate>>>
                Error wait(Mutex &mutex, Predicate pred, unsigned int timeoutMs = 0) {
                        std::unique_lock<std::mutex> lock(mutex._mutex, std::adopt_lock);
                        if(timeoutMs == 0) {
                                _cv.wait(lock, pred);
                                lock.release();
                                return Error::Ok;
                        }
                        bool result = _cv.wait_for(lock,
                                std::chrono::milliseconds(timeoutMs), pred);
                        lock.release();
                        return result ? Error::Ok : Error::Timeout;
                }

                /** @brief Wakes one waiting thread. */
                void wakeOne() {
                        _cv.notify_one();
                }

                /** @brief Wakes all waiting threads. */
                void wakeAll() {
                        _cv.notify_all();
                }

        private:
                std::condition_variable _cv;
};

PROMEKI_NAMESPACE_END
