/**
 * @file      once.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_CORE
#include <mutex>
#include <utility>
#include <promeki/namespace.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief One-shot initialisation flag.
 * @ingroup concurrency
 *
 * Library wrapper around @c std::once_flag.  Pair with @ref callOnce
 * to guarantee that a piece of code runs exactly one time across all
 * threads that race to perform a one-shot initialisation — common
 * for lazy caches, global registrations, and idempotent
 * library-startup helpers.
 *
 * The flag is non-copyable and non-movable, matching
 * @c std::once_flag.  Construction is trivial; the underlying
 * synchronisation primitives are initialised on the first call to
 * @ref callOnce.
 *
 * @par Thread Safety
 * Fully thread-safe by construction.  Any number of threads may
 * race on the same OnceFlag through @ref callOnce; exactly one
 * thread will execute the callable, all others will block until
 * that thread returns and then continue past the call.  If the
 * winning thread throws, the flag remains unset and a subsequent
 * @ref callOnce will try again.
 *
 * @par Example
 * @code
 * namespace {
 *     OnceFlag       g_initFlag;
 *     SomeResource * g_resource = nullptr;
 * }
 *
 * SomeResource * getResource() {
 *     callOnce(g_initFlag, []() {
 *         g_resource = createResource();
 *     });
 *     return g_resource;
 * }
 * @endcode
 */
class OnceFlag {
        public:
                /** @brief Constructs a fresh, unset flag. */
                OnceFlag() = default;

                /** @brief Destructor. */
                ~OnceFlag() = default;

                OnceFlag(const OnceFlag &) = delete;
                OnceFlag &operator=(const OnceFlag &) = delete;
                OnceFlag(OnceFlag &&) = delete;
                OnceFlag &operator=(OnceFlag &&) = delete;

        private:
                template <typename Callable, typename... Args> friend void callOnce(OnceFlag &, Callable &&, Args &&...);

                std::once_flag _flag;
};

/**
 * @brief Invokes @p callable exactly once for the given @p flag.
 * @ingroup concurrency
 *
 * Mirrors @c std::call_once.  Threads racing on the same
 * @ref OnceFlag synchronise so that exactly one thread runs the
 * callable; all other threads block until that thread returns,
 * then continue past the call with the side effects visible.
 *
 * If the winning thread propagates an exception out of @p callable
 * the flag remains unset and the next @ref callOnce will try
 * again — same recovery semantics as @c std::call_once.
 *
 * @tparam Callable Any callable type compatible with @c std::invoke.
 * @tparam Args     Argument types forwarded to @p callable.
 * @param  flag     The flag arbitrating the one-shot execution.
 * @param  callable The function / lambda / functor to invoke once.
 * @param  args     Arguments forwarded to @p callable.
 */
template <typename Callable, typename... Args> void callOnce(OnceFlag &flag, Callable &&callable, Args &&...args) {
        std::call_once(flag._flag, std::forward<Callable>(callable), std::forward<Args>(args)...);
}

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_CORE
