/**
 * @file      optional.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <optional>
#include <type_traits>
#include <utility>
#include <promeki/namespace.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Optional value wrapping std::optional.
 * @ingroup util
 *
 * Provides a Qt-inspired API over @c std::optional with consistent
 * naming conventions matching the rest of libpromeki.  Holds either
 * a value of type @c T or no value (an "empty" Optional).  Simple
 * value type — no PROMEKI_SHARED_FINAL.
 *
 * @par Thread Safety
 * Distinct Optional instances may be used concurrently.  A single
 * instance inherits the thread-safety of @c T — concurrent access
 * to the same Optional must be externally synchronized unless @c T
 * documents its own thread safety.
 *
 * @par Example
 * @code
 * Optional<int> a;                  // empty
 * Optional<int> b(42);              // holds 42
 * if (b) {                          // explicit bool conversion
 *     int v = b.value();            // 42
 * }
 * int z = a.valueOr(-1);            // -1
 * a = 7;                            // a now holds 7
 * a.reset();                        // a is empty again
 * @endcode
 *
 * @tparam T The contained value type.
 */
template <typename T> class Optional {
        public:
                /** @brief The contained value type. */
                using ValueType = T;

                /** @brief Underlying @c std::optional storage type. */
                using DataType = std::optional<T>;

                /** @brief Default constructor.  Creates an empty Optional. */
                Optional() = default;

                /** @brief Constructs an empty Optional from @c std::nullopt. */
                Optional(std::nullopt_t) noexcept {}

                /**
                 * @brief Constructs an Optional holding @p value (copy).
                 * @param value The value to store.
                 */
                Optional(const T &value) : d(value) {}

                /**
                 * @brief Constructs an Optional holding @p value (move).
                 * @param value The value to store.
                 */
                Optional(T &&value) noexcept(std::is_nothrow_move_constructible_v<T>) : d(std::move(value)) {}

                /**
                 * @brief Constructs from an existing @c std::optional (lvalue).
                 * @param other The @c std::optional to copy from.
                 */
                Optional(const DataType &other) : d(other) {}

                /**
                 * @brief Constructs from an existing @c std::optional (rvalue).
                 * @param other The @c std::optional to move from.
                 */
                Optional(DataType &&other) noexcept(std::is_nothrow_move_constructible_v<T>) : d(std::move(other)) {}

                /** @brief Copy constructor. */
                Optional(const Optional &other) = default;

                /** @brief Move constructor. */
                Optional(Optional &&other) noexcept(std::is_nothrow_move_constructible_v<T>) = default;

                /** @brief Destructor. */
                ~Optional() = default;

                /** @brief Copy assignment operator. */
                Optional &operator=(const Optional &other) = default;

                /** @brief Move assignment operator. */
                Optional &operator=(Optional &&other) noexcept(std::is_nothrow_move_assignable_v<T>) = default;

                /** @brief Unsets the value, leaving the Optional empty. */
                Optional &operator=(std::nullopt_t) noexcept {
                        d.reset();
                        return *this;
                }

                /**
                 * @brief Assigns a value (perfect-forwarded).
                 *
                 * SFINAE-guarded so the copy/move/nullopt overloads are not
                 * shadowed.
                 *
                 * @tparam U Source value type (deduced).
                 * @param  value The value to assign.
                 * @return Reference to this Optional.
                 */
                template <typename U,
                          typename = std::enable_if_t<!std::is_same_v<std::decay_t<U>, Optional> &&
                                                      !std::is_same_v<std::decay_t<U>, std::nullopt_t> &&
                                                      std::is_constructible_v<T, U &&>>>
                Optional &operator=(U &&value) {
                        d = std::forward<U>(value);
                        return *this;
                }

                /**
                 * @brief Returns true if the Optional holds a value.
                 * @return True when a value is present.
                 */
                bool hasValue() const noexcept { return d.has_value(); }

                /** @brief Returns true if the Optional holds a value. */
                explicit operator bool() const noexcept { return d.has_value(); }

                /**
                 * @brief Returns a const reference to the contained value.
                 *
                 * Throws @c std::bad_optional_access if the Optional is empty.
                 *
                 * @return Const reference to the stored value.
                 */
                const T &value() const & { return d.value(); }

                /** @brief Returns a mutable reference to the contained value.
                 *
                 * Throws @c std::bad_optional_access if the Optional is empty.
                 *
                 * @return Mutable reference to the stored value.
                 */
                T &value() & { return d.value(); }

                /** @brief Returns an rvalue reference to the contained value.
                 *
                 * Throws @c std::bad_optional_access if the Optional is empty.
                 *
                 * @return Rvalue reference to the stored value.
                 */
                T &&value() && { return std::move(d).value(); }

                /**
                 * @brief Returns the contained value, or @p fallback when empty.
                 * @param fallback The value to return when the Optional is empty.
                 * @return The contained value or @p fallback.
                 */
                template <typename U> T valueOr(U &&fallback) const & {
                        return d.value_or(std::forward<U>(fallback));
                }

                /** @brief Returns the contained value, or @p fallback when empty (rvalue overload).
                 * @param fallback The value to return when the Optional is empty.
                 * @return The contained value or @p fallback.
                 */
                template <typename U> T valueOr(U &&fallback) && {
                        return std::move(d).value_or(std::forward<U>(fallback));
                }

                /**
                 * @brief Dereference: returns a reference to the contained value (no check).
                 *
                 * Behaviour is undefined if the Optional is empty.
                 */
                const T &operator*() const & noexcept { return *d; }

                /** @brief Dereference: returns a mutable reference to the contained value (no check).
                 *
                 * Behaviour is undefined if the Optional is empty.
                 */
                T &operator*() & noexcept { return *d; }

                /** @brief Dereference: returns an rvalue reference to the contained value (no check).
                 *
                 * Behaviour is undefined if the Optional is empty.
                 */
                T &&operator*() && noexcept { return std::move(*d); }

                /**
                 * @brief Member-access: returns a pointer to the contained value (no check).
                 *
                 * Behaviour is undefined if the Optional is empty.
                 */
                const T *operator->() const noexcept { return d.operator->(); }

                /// @copydoc operator->() const
                T *operator->() noexcept { return d.operator->(); }

                /** @brief Unsets the value, leaving the Optional empty. */
                void reset() noexcept { d.reset(); }

                /**
                 * @brief Constructs a new value in place, replacing any existing one.
                 * @tparam ArgsT Constructor argument types (deduced).
                 * @param  args Arguments forwarded to @c T's constructor.
                 * @return Reference to the new contained value.
                 */
                template <typename... ArgsT> T &emplace(ArgsT &&...args) {
                        return d.emplace(std::forward<ArgsT>(args)...);
                }

                /** @brief Swaps contents with another Optional. */
                void swap(Optional &other) noexcept(std::is_nothrow_swappable_v<T> &&
                                                    std::is_nothrow_move_constructible_v<T>) {
                        d.swap(other.d);
                }

                /** @brief Returns a const reference to the underlying @c std::optional. */
                const DataType &toStdOptional() const noexcept { return d; }

                /** @brief Returns a mutable reference to the underlying @c std::optional. */
                DataType &toStdOptional() noexcept { return d; }

                /** @brief Equality: both empty, or both hold equal values. */
                friend bool operator==(const Optional &lhs, const Optional &rhs) { return lhs.d == rhs.d; }

                /** @brief Inequality. */
                friend bool operator!=(const Optional &lhs, const Optional &rhs) { return lhs.d != rhs.d; }

                /** @brief Equality against the empty state. */
                friend bool operator==(const Optional &lhs, std::nullopt_t) noexcept { return !lhs.d.has_value(); }

                /// @copydoc operator==(const Optional &, std::nullopt_t)
                friend bool operator==(std::nullopt_t, const Optional &rhs) noexcept { return !rhs.d.has_value(); }

                /** @brief Inequality against the empty state. */
                friend bool operator!=(const Optional &lhs, std::nullopt_t) noexcept { return lhs.d.has_value(); }

                /// @copydoc operator!=(const Optional &, std::nullopt_t)
                friend bool operator!=(std::nullopt_t, const Optional &rhs) noexcept { return rhs.d.has_value(); }

        private:
                DataType d;
};

PROMEKI_NAMESPACE_END
