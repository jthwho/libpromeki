/**
 * @file      function.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <functional>
#include <type_traits>
#include <utility>
#include <promeki/namespace.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Type-erased callable wrapper, wrapping std::function.
 * @ingroup util
 *
 * Provides a Qt-inspired API over @c std::function with consistent
 * naming conventions matching the rest of libpromeki.  The signature
 * is supplied as a single function-type template parameter, e.g.
 * @code
 * Function<void(int, const String &)> onFrameReady;
 * @endcode
 *
 * Simple value type — no PROMEKI_SHARED_FINAL.  Copy/move are
 * supported with the same semantics as the underlying
 * @c std::function: a copy may share the bound target (lambda
 * capture state is duplicated by value, not by reference), and an
 * empty @c Function evaluates to @c false in boolean contexts.
 *
 * @par Thread Safety
 * Distinct @c Function instances may be invoked concurrently.  A
 * single instance must be externally synchronized if one thread
 * invokes it while another thread reassigns or destroys it.  The
 * thread safety of the bound target is its own concern.
 *
 * @par Example
 * @code
 * Function<int(int, int)> add = [](int a, int b) { return a + b; };
 * int n = add(2, 3);              // 5
 * if (add) { ... }                // explicit-bool conversion
 *
 * Function<int(int, int)> empty;
 * empty(1, 2);                    // throws std::bad_function_call
 * @endcode
 *
 * @tparam Sig Function signature, e.g. @c void(int) or @c int(int,int).
 */
template <typename Sig> class Function;

/**
 * @brief Specialization of Function for callable signatures @c R(Args...).
 * @ingroup util
 *
 * @tparam R Return type.
 * @tparam Args Argument types.
 */
template <typename R, typename... Args> class Function<R(Args...)> {
        public:
                /** @brief Underlying @c std::function storage type. */
                using DataType = std::function<R(Args...)>;

                /** @brief The function's return type. */
                using ResultType = R;

                /** @brief Default constructor.  Creates an empty (unbound) Function. */
                Function() = default;

                /** @brief Constructs an empty (unbound) Function from @c nullptr. */
                Function(std::nullptr_t) noexcept {}

                /**
                 * @brief Constructs from any callable matching the signature.
                 *
                 * Accepts function pointers, lambdas, and any object with an
                 * @c operator() compatible with @c R(Args...).  SFINAE-guarded
                 * so the copy/move/<tt>std::function</tt> constructors are not shadowed
                 * by accidental single-argument matches.
                 *
                 * @tparam F Callable type (deduced).
                 * @param  f The callable to wrap.
                 */
                template <typename F,
                          typename = std::enable_if_t<!std::is_same_v<std::decay_t<F>, Function> &&
                                                      std::is_invocable_r_v<R, F &, Args...>>>
                Function(F &&f) : d(std::forward<F>(f)) {}

                /**
                 * @brief Constructs from an existing @c std::function (lvalue).
                 * @param f The @c std::function to copy from.
                 */
                Function(const DataType &f) : d(f) {}

                /**
                 * @brief Constructs from an existing @c std::function (rvalue).
                 * @param f The @c std::function to move from.
                 */
                Function(DataType &&f) noexcept : d(std::move(f)) {}

                /** @brief Copy constructor. */
                Function(const Function &other) = default;

                /** @brief Move constructor. */
                Function(Function &&other) noexcept = default;

                /** @brief Destructor. */
                ~Function() = default;

                /** @brief Copy assignment operator. */
                Function &operator=(const Function &other) = default;

                /** @brief Move assignment operator. */
                Function &operator=(Function &&other) noexcept = default;

                /** @brief Unbinds the function, leaving it empty. */
                Function &operator=(std::nullptr_t) noexcept {
                        d = nullptr;
                        return *this;
                }

                /**
                 * @brief Assigns any compatible callable.
                 * @tparam F Callable type (deduced).
                 * @param  f The callable to wrap.
                 * @return Reference to this Function.
                 */
                template <typename F,
                          typename = std::enable_if_t<!std::is_same_v<std::decay_t<F>, Function> &&
                                                      std::is_invocable_r_v<R, F &, Args...>>>
                Function &operator=(F &&f) {
                        d = std::forward<F>(f);
                        return *this;
                }

                /**
                 * @brief Invokes the wrapped callable.
                 *
                 * Throws @c std::bad_function_call if the Function is empty.
                 *
                 * @param args Arguments forwarded to the underlying callable.
                 * @return The callable's return value.
                 */
                R operator()(Args... args) const { return d(std::forward<Args>(args)...); }

                /**
                 * @brief Returns true if a callable is bound.
                 * @return True when the Function holds a target.
                 */
                explicit operator bool() const noexcept { return static_cast<bool>(d); }

                /** @brief Returns true if no callable is bound (empty Function). */
                bool isNull() const noexcept { return !d; }

                /** @brief Unbinds any held callable, leaving the Function empty. */
                void clear() noexcept { d = nullptr; }

                /** @brief Swaps contents with another Function. */
                void swap(Function &other) noexcept { d.swap(other.d); }

                /** @brief Returns a const reference to the underlying @c std::function. */
                const DataType &toStdFunction() const noexcept { return d; }

                /** @brief Returns a mutable reference to the underlying @c std::function. */
                DataType &toStdFunction() noexcept { return d; }

                /** @brief Returns true if the Function is empty (no target bound). */
                friend bool operator==(const Function &f, std::nullptr_t) noexcept { return !f.d; }

                /** @brief Returns true if the Function is empty (no target bound). */
                friend bool operator==(std::nullptr_t, const Function &f) noexcept { return !f.d; }

                /** @brief Returns true if the Function holds a target. */
                friend bool operator!=(const Function &f, std::nullptr_t) noexcept { return static_cast<bool>(f.d); }

                /** @brief Returns true if the Function holds a target. */
                friend bool operator!=(std::nullptr_t, const Function &f) noexcept { return static_cast<bool>(f.d); }

        private:
                DataType d;
};

PROMEKI_NAMESPACE_END
