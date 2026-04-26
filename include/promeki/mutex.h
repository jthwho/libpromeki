/**
 * @file      mutex.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <mutex>
#include <promeki/namespace.h>

PROMEKI_NAMESPACE_BEGIN

class WaitCondition;

/**
 * @brief Mutual exclusion lock wrapping std::mutex.
 * @ingroup concurrency
 *
 * Provides a simple mutex with lock/unlock/tryLock and a nested
 * RAII Locker type for scoped locking.  Non-copyable and non-movable.
 *
 * @par Thread Safety
 * Fully thread-safe by construction.  All public methods may be
 * invoked concurrently from any thread on a single instance —
 * that is the entire purpose of the type.
 *
 * @par Example
 * @code
 * Mutex mtx;
 * {
 *     Mutex::Locker lock(mtx);
 *     // ... critical section ...
 * }
 * @endcode
 */
class Mutex {
        public:
                /**
                 * @brief RAII scoped locker for Mutex.
                 *
                 * Acquires the mutex on construction and releases it on
                 * destruction.  Non-copyable and non-movable.
                 */
                class Locker {
                        public:
                                /**
                                 * @brief Constructs a Locker and locks the given mutex.
                                 * @param mutex The mutex to lock.
                                 */
                                Locker(Mutex &mutex) : _mutex(mutex) {
                                        _mutex.lock();
                                }

                                /** @brief Destructor.  Unlocks the mutex. */
                                ~Locker() {
                                        _mutex.unlock();
                                }

                                Locker(const Locker &) = delete;
                                Locker &operator=(const Locker &) = delete;
                                Locker(Locker &&) = delete;
                                Locker &operator=(Locker &&) = delete;

                        private:
                                Mutex &_mutex;
                };

                /** @brief Constructs an unlocked mutex. */
                Mutex() = default;

                /** @brief Destructor. */
                ~Mutex() = default;

                Mutex(const Mutex &) = delete;
                Mutex &operator=(const Mutex &) = delete;
                Mutex(Mutex &&) = delete;
                Mutex &operator=(Mutex &&) = delete;

                /** @brief Locks the mutex.  Blocks if already locked by another thread. */
                void lock() {
                        _mutex.lock();
                }

                /** @brief Unlocks the mutex. */
                void unlock() {
                        _mutex.unlock();
                }

                /**
                 * @brief Attempts to lock the mutex without blocking.
                 * @return True if the lock was acquired, false otherwise.
                 */
                bool tryLock() {
                        return _mutex.try_lock();
                }

        private:
                friend class WaitCondition;
                std::mutex _mutex;
};

PROMEKI_NAMESPACE_END
