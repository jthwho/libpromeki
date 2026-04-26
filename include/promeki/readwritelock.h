/**
 * @file      readwritelock.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <shared_mutex>
#include <promeki/namespace.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Reader-writer lock wrapping std::shared_mutex.
 * @ingroup concurrency
 *
 * Allows multiple concurrent readers or a single exclusive writer.
 * Provides nested RAII ReadLocker and WriteLocker types.
 * Non-copyable and non-movable.
 *
 * @par Thread Safety
 * Fully thread-safe by construction.  All public methods may be
 * invoked concurrently from any thread on a single instance —
 * that is the entire purpose of the type.
 */
class ReadWriteLock {
        public:
                /**
                 * @brief RAII scoped locker for shared (read) access.
                 */
                class ReadLocker {
                        public:
                                /**
                                 * @brief Constructs a ReadLocker and acquires a shared lock.
                                 * @param lock The ReadWriteLock to lock for reading.
                                 */
                                ReadLocker(ReadWriteLock &lock) : _lock(lock) {
                                        _lock.lockForRead();
                                }

                                /** @brief Destructor.  Releases the shared lock. */
                                ~ReadLocker() {
                                        _lock.unlock();
                                }

                                ReadLocker(const ReadLocker &) = delete;
                                ReadLocker &operator=(const ReadLocker &) = delete;
                                ReadLocker(ReadLocker &&) = delete;
                                ReadLocker &operator=(ReadLocker &&) = delete;

                        private:
                                ReadWriteLock &_lock;
                };

                /**
                 * @brief RAII scoped locker for exclusive (write) access.
                 */
                class WriteLocker {
                        public:
                                /**
                                 * @brief Constructs a WriteLocker and acquires an exclusive lock.
                                 * @param lock The ReadWriteLock to lock for writing.
                                 */
                                WriteLocker(ReadWriteLock &lock) : _lock(lock) {
                                        _lock.lockForWrite();
                                }

                                /** @brief Destructor.  Releases the exclusive lock. */
                                ~WriteLocker() {
                                        _lock.unlock();
                                }

                                WriteLocker(const WriteLocker &) = delete;
                                WriteLocker &operator=(const WriteLocker &) = delete;
                                WriteLocker(WriteLocker &&) = delete;
                                WriteLocker &operator=(WriteLocker &&) = delete;

                        private:
                                ReadWriteLock &_lock;
                };

                /** @brief Constructs an unlocked ReadWriteLock. */
                ReadWriteLock() = default;

                /** @brief Destructor. */
                ~ReadWriteLock() = default;

                ReadWriteLock(const ReadWriteLock &) = delete;
                ReadWriteLock &operator=(const ReadWriteLock &) = delete;
                ReadWriteLock(ReadWriteLock &&) = delete;
                ReadWriteLock &operator=(ReadWriteLock &&) = delete;

                /** @brief Acquires a shared (read) lock.  Multiple readers may hold this concurrently. */
                void lockForRead() {
                        _mutex.lock_shared();
                }

                /** @brief Acquires an exclusive (write) lock.  Blocks until all readers and writers release. */
                void lockForWrite() {
                        _mutex.lock();
                }

                /** @brief Releases the currently held lock (shared or exclusive). */
                void unlock() {
                        _mutex.unlock();
                }

                /**
                 * @brief Attempts to acquire a shared (read) lock without blocking.
                 * @return True if the lock was acquired, false otherwise.
                 */
                bool tryLockForRead() {
                        return _mutex.try_lock_shared();
                }

                /**
                 * @brief Attempts to acquire an exclusive (write) lock without blocking.
                 * @return True if the lock was acquired, false otherwise.
                 */
                bool tryLockForWrite() {
                        return _mutex.try_lock();
                }

        private:
                std::shared_mutex _mutex;
};

PROMEKI_NAMESPACE_END
