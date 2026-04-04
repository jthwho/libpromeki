/**
 * @file      promise.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <future>
#include <utility>
#include <promeki/namespace.h>
#include <promeki/future.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Promise wrapping std::promise\<T\>.
 * @ingroup concurrency
 *
 * Used to set a result that will be retrieved via the associated
 * Future\<T\>.  Move-only (non-copyable).
 *
 * @tparam T The result type.
 */
template <typename T>
class Promise {
        public:
                /** @brief Constructs a Promise. */
                Promise() = default;

                /** @brief Destructor. */
                ~Promise() = default;

                Promise(const Promise &) = delete;
                Promise &operator=(const Promise &) = delete;

                /** @brief Move constructor. */
                Promise(Promise &&other) = default;

                /** @brief Move assignment. */
                Promise &operator=(Promise &&other) = default;

                /**
                 * @brief Sets the result value.
                 * @param value The value to make available to the Future.
                 */
                void setValue(T value) {
                        _promise.set_value(std::move(value));
                }

                /**
                 * @brief Sets an error on the promise.
                 *
                 * The associated Future will receive a default-constructed T.
                 * The error is communicated via an exception internally.
                 *
                 * @param error The error to set.
                 */
                void setError(Error error) {
                        _promise.set_exception(
                                std::make_exception_ptr(PromiseError(error)));
                }

                /**
                 * @brief Returns the Future associated with this Promise.
                 * @return A Future\<T\> that will receive the result.
                 */
                Future<T> future() {
                        return Future<T>(_promise.get_future());
                }

        private:
                /**
                 * @brief Internal exception type for error propagation.
                 */
                struct PromiseError {
                        Error error;
                        PromiseError(Error e) : error(e) {}
                };

                std::promise<T> _promise;
};

/**
 * @brief Specialization of Promise for void results.
 */
template <>
class Promise<void> {
        public:
                /** @brief Constructs a Promise. */
                Promise() = default;

                /** @brief Destructor. */
                ~Promise() = default;

                Promise(const Promise &) = delete;
                Promise &operator=(const Promise &) = delete;
                Promise(Promise &&other) = default;
                Promise &operator=(Promise &&other) = default;

                /**
                 * @brief Signals completion (no value).
                 */
                void setValue() {
                        _promise.set_value();
                }

                /**
                 * @brief Sets an error on the promise.
                 * @param error The error to set.
                 */
                void setError(Error error) {
                        _promise.set_exception(
                                std::make_exception_ptr(PromiseError(error)));
                }

                /**
                 * @brief Returns the Future associated with this Promise.
                 * @return A Future\<void\> that will receive the completion signal.
                 */
                Future<void> future() {
                        return Future<void>(_promise.get_future());
                }

        private:
                struct PromiseError {
                        Error error;
                        PromiseError(Error e) : error(e) {}
                };

                std::promise<void> _promise;
};

PROMEKI_NAMESPACE_END
