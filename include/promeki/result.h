/**
 * @file      result.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/pair.h>
#include <promeki/error.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Convenience alias for fallible return types.
 * @ingroup util
 *
 * Wraps a value and an Error in a Pair. Structured bindings work
 * naturally: `auto [val, err] = someFactory();`
 *
 * @par Thread Safety
 * Inherits @ref Pair: distinct instances may be used concurrently;
 * a single instance is thread-safe only as far as @c T is.  Result
 * is most often a transient return value, so concurrent access is
 * rarely meaningful.
 *
 * @tparam T The value type.
 */
template <typename T>
using Result = Pair<T, Error>;

/**
 * @brief Creates a successful Result wrapping @p value.
 * @tparam T The value type.
 * @param value The value to wrap.
 * @return A Result with the value and Error::Ok.
 */
template <typename T>
Result<T> makeResult(T value) {
        return Result<T>(std::move(value), Error::Ok);
}

/**
 * @brief Creates an error Result with a default-constructed value.
 * @tparam T The value type.
 * @param err The error code.
 * @return A Result with default-constructed T and the given error.
 */
template <typename T>
Result<T> makeError(Error err) {
        return Result<T>(T{}, std::move(err));
}

/**
 * @brief Returns the value from a Result.
 * @tparam T The value type.
 * @param r The Result to query.
 * @return Const reference to the value.
 */
template <typename T>
const T &value(const Result<T> &r) { return r.first(); }

/**
 * @brief Returns the error from a Result.
 * @tparam T The value type.
 * @param r The Result to query.
 * @return The Error.
 */
template <typename T>
const Error &error(const Result<T> &r) { return r.second(); }

/**
 * @brief Returns true if the Result is successful (Error::Ok).
 * @tparam T The value type.
 * @param r The Result to query.
 * @return True if no error.
 */
template <typename T>
bool isOk(const Result<T> &r) { return r.second().isOk(); }

/**
 * @brief Returns true if the Result has an error.
 * @tparam T The value type.
 * @param r The Result to query.
 * @return True if error.
 */
template <typename T>
bool isError(const Result<T> &r) { return r.second().isError(); }

PROMEKI_NAMESPACE_END
