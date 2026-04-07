/**
 * @file      future.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <future>
#include <chrono>
#include <promeki/namespace.h>
#include <promeki/result.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Internal exception type used by Promise to propagate errors
 *        through std::future.
 * @ingroup concurrency
 *
 * Promise::setError() throws an instance of this through the future's
 * shared state.  Future::result() catches it and returns the wrapped
 * Error in the Result's error field, so callers see a normal error
 * code rather than an unhandled exception.
 */
struct PromiseError {
        Error error;
        explicit PromiseError(Error e) : error(e) {}
};

/**
 * @brief Asynchronous result wrapping std::future\<T\>.
 * @ingroup concurrency
 *
 * Returned by Promise\<T\>::%future() and ThreadPool::submit().
 *
 * @par Example
 * @code
 * Promise<int> promise;
 * Future<int> future = promise.%future();
 *
 * // In another thread:
 * promise.setValue(42);
 *
 * // In the waiting thread:
 * int result = future.get();  // 42
 * @endcode
 * Move-only (non-copyable).
 *
 * @tparam T The result type.
 */
template <typename T>
class Future {
        public:
                /** @brief Constructs an invalid (empty) Future. */
                Future() = default;

                /** @brief Constructs a Future from a std::future. */
                Future(std::future<T> &&f) : _future(std::move(f)) {}

                /** @brief Destructor. */
                ~Future() = default;

                Future(const Future &) = delete;
                Future &operator=(const Future &) = delete;

                /** @brief Move constructor. */
                Future(Future &&other) = default;

                /** @brief Move assignment. */
                Future &operator=(Future &&other) = default;

                /**
                 * @brief Checks whether the result is ready without blocking.
                 * @return True if the result is available.
                 */
                bool isReady() const {
                        return _future.valid() &&
                               _future.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
                }

                /**
                 * @brief Returns the result, blocking until available or timeout.
                 *
                 * @param timeoutMs Maximum time to wait in milliseconds.
                 *        A value of 0 (the default) waits indefinitely.
                 * @return A Result containing the value and Error::Ok, or a
                 *         default-constructed T and an error code on failure
                 *         (Error::Invalid, Error::Timeout, Error::Cancelled,
                 *         or another error set via Promise::setError).
                 */
                Result<T> result(unsigned int timeoutMs = 0) {
                        if(!_future.valid()) return Result<T>(T{}, Error::Invalid);
                        if(timeoutMs != 0) {
                                if(_future.wait_for(std::chrono::milliseconds(timeoutMs)) !=
                                        std::future_status::ready) {
                                        return Result<T>(T{}, Error::Timeout);
                                }
                        }
                        try {
                                return Result<T>(_future.get(), Error::Ok);
                        } catch(const PromiseError &pe) {
                                return Result<T>(T{}, pe.error);
                        } catch(const std::future_error &) {
                                return Result<T>(T{}, Error::Invalid);
                        } catch(...) {
                                return Result<T>(T{}, Error::LibraryFailure);
                        }
                }

                /**
                 * @brief Blocks until the result is ready.
                 */
                void waitForFinished() {
                        if(_future.valid()) _future.wait();
                }

                /**
                 * @brief Blocks until the result is ready or the timeout expires.
                 * @param timeoutMs Maximum time to wait in milliseconds.
                 * @return Error::Ok if finished, Error::Timeout if the timeout elapsed.
                 */
                Error waitForFinished(unsigned int timeoutMs) {
                        if(!_future.valid()) return Error::Invalid;
                        if(_future.wait_for(std::chrono::milliseconds(timeoutMs)) ==
                                std::future_status::ready) {
                                return Error::Ok;
                        }
                        return Error::Timeout;
                }

                /**
                 * @brief Returns whether this Future holds a valid shared state.
                 * @return True if a result can be retrieved.
                 */
                bool isValid() const {
                        return _future.valid();
                }

        private:
                mutable std::future<T> _future;
};

/**
 * @brief Specialization of Future for void results.
 *
 * Provides the same interface but result() returns just an Error.
 */
template <>
class Future<void> {
        public:
                /** @brief Constructs an invalid (empty) Future. */
                Future() = default;

                /** @brief Constructs a Future from a std::future. */
                Future(std::future<void> &&f) : _future(std::move(f)) {}

                /** @brief Destructor. */
                ~Future() = default;

                Future(const Future &) = delete;
                Future &operator=(const Future &) = delete;
                Future(Future &&other) = default;
                Future &operator=(Future &&other) = default;

                /**
                 * @brief Checks whether the result is ready without blocking.
                 * @return True if the result is available.
                 */
                bool isReady() const {
                        return _future.valid() &&
                               _future.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
                }

                /**
                 * @brief Waits for completion and returns the status.
                 * @param timeoutMs Maximum time to wait in milliseconds.
                 *        A value of 0 (the default) waits indefinitely.
                 * @return Error::Ok on completion, or an error code (Timeout,
                 *         Invalid, Cancelled, or one set via setError).
                 */
                Error result(unsigned int timeoutMs = 0) {
                        if(!_future.valid()) return Error::Invalid;
                        if(timeoutMs != 0) {
                                if(_future.wait_for(std::chrono::milliseconds(timeoutMs)) !=
                                        std::future_status::ready) {
                                        return Error::Timeout;
                                }
                        }
                        try {
                                _future.get();
                                return Error::Ok;
                        } catch(const PromiseError &pe) {
                                return pe.error;
                        } catch(const std::future_error &) {
                                return Error::Invalid;
                        } catch(...) {
                                return Error::LibraryFailure;
                        }
                }

                /**
                 * @brief Blocks until the result is ready.
                 */
                void waitForFinished() {
                        if(_future.valid()) _future.wait();
                }

                /**
                 * @brief Blocks until the result is ready or the timeout expires.
                 * @param timeoutMs Maximum time to wait in milliseconds.
                 * @return Error::Ok if finished, Error::Timeout if the timeout elapsed.
                 */
                Error waitForFinished(unsigned int timeoutMs) {
                        if(!_future.valid()) return Error::Invalid;
                        if(_future.wait_for(std::chrono::milliseconds(timeoutMs)) ==
                                std::future_status::ready) {
                                return Error::Ok;
                        }
                        return Error::Timeout;
                }

                /**
                 * @brief Returns whether this Future holds a valid shared state.
                 * @return True if a result can be retrieved.
                 */
                bool isValid() const {
                        return _future.valid();
                }

        private:
                mutable std::future<void> _future;
};

PROMEKI_NAMESPACE_END
