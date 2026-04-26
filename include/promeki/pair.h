/**
 * @file      pair.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstddef>
#include <utility>
#include <tuple>
#include <promeki/namespace.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Typed pair container wrapping std::pair.
 * @ingroup util
 *
 * Provides a Qt-inspired API over std::pair with consistent naming
 * conventions matching the rest of libpromeki. Simple value type —
 * no PROMEKI_SHARED_FINAL.
 *
 * Supports structured bindings via std::tuple_size/std::%tuple_element
 * specializations.
 *
 * @par Thread Safety
 * Distinct instances may be used concurrently.  A single instance
 * inherits the thread-safety of @c A and @c B — concurrent access
 * to the same Pair must be externally synchronized unless both
 * element types document their own thread safety.
 *
 * @tparam A First element type.
 * @tparam B Second element type.
 */
template <typename A, typename B>
class Pair {
        public:
                /** @brief Type of the first element. */
                using FirstType = A;

                /** @brief Type of the second element. */
                using SecondType = B;

                /** @brief Default constructor. Value-initializes both elements. */
                Pair() = default;

                /**
                 * @brief Constructs a pair from two values, perfect-forwarded.
                 *
                 * Accepts any combination of lvalue and rvalue arguments and
                 * forwards them into the underlying @c std::pair, so move-only
                 * element types like @c std::unique_ptr work seamlessly.
                 * SFINAE-guarded to only participate in overload resolution
                 * when both A and B are actually constructible from the
                 * supplied argument types — this keeps unrelated single-arg
                 * constructors like the @c std::pair conversion overload from
                 * being shadowed by accidental two-arg matches.
                 *
                 * @tparam A2 First argument type (deduced).
                 * @tparam B2 Second argument type (deduced).
                 * @param  a  First element.
                 * @param  b  Second element.
                 */
                template <typename A2, typename B2,
                          typename = std::enable_if_t<
                              std::is_constructible_v<A, A2&&> &&
                              std::is_constructible_v<B, B2&&>>>
                Pair(A2 &&a, B2 &&b)
                        : d(std::forward<A2>(a), std::forward<B2>(b)) {}

                /**
                 * @brief Constructs from an existing std::pair.
                 * @param p The std::pair to copy from.
                 */
                Pair(const std::pair<A, B> &p) : d(p) {}

                /**
                 * @brief Constructs from an existing std::pair (move overload).
                 * @param p The std::pair to move from.
                 */
                Pair(std::pair<A, B> &&p) : d(std::move(p)) {}

                /** @brief Copy constructor. */
                Pair(const Pair &other) = default;

                /** @brief Move constructor. */
                Pair(Pair &&other) noexcept = default;

                /** @brief Destructor. */
                ~Pair() = default;

                /** @brief Copy assignment operator. */
                Pair &operator=(const Pair &other) = default;

                /** @brief Move assignment operator. */
                Pair &operator=(Pair &&other) noexcept = default;

                /** @brief Returns a mutable reference to the first element. */
                A &first() { return d.first; }

                /** @brief Returns a const reference to the first element. */
                const A &first() const { return d.first; }

                /** @brief Returns a mutable reference to the second element. */
                B &second() { return d.second; }

                /** @brief Returns a const reference to the second element. */
                const B &second() const { return d.second; }

                /**
                 * @brief Sets the first element.
                 * @param a The new value for the first element.
                 */
                void setFirst(const A &a) { d.first = a; }

                /**
                 * @brief Sets the second element.
                 * @param b The new value for the second element.
                 */
                void setSecond(const B &b) { d.second = b; }

                /**
                 * @brief Returns a const reference to the underlying std::pair.
                 * @return The wrapped std::pair.
                 */
                const std::pair<A, B> &toStdPair() const { return d; }

                /**
                 * @brief Swaps contents with another pair.
                 * @param other The pair to swap with.
                 */
                void swap(Pair &other) noexcept {
                        d.swap(other.d);
                        return;
                }

                /**
                 * @brief Factory function to create a Pair.
                 * @param a First element.
                 * @param b Second element.
                 * @return A new Pair.
                 */
                static Pair make(A a, B b) {
                        return Pair(std::move(a), std::move(b));
                }

                /** @brief Returns true if both pairs have identical contents. */
                friend bool operator==(const Pair &lhs, const Pair &rhs) {
                        return lhs.d == rhs.d;
                }

                /** @brief Returns true if the pairs differ. */
                friend bool operator!=(const Pair &lhs, const Pair &rhs) {
                        return lhs.d != rhs.d;
                }

                /** @brief Lexicographic less-than comparison. */
                friend bool operator<(const Pair &lhs, const Pair &rhs) {
                        return lhs.d < rhs.d;
                }

                /**
                 * @brief Structured bindings support: element access by index.
                 * @tparam I Element index (0 for first, 1 for second).
                 * @return Reference to the requested element.
                 */
                template <std::size_t I>
                auto &get() & {
                        if constexpr (I == 0) return d.first;
                        else return d.second;
                }

                /// @copydoc get()
                template <std::size_t I>
                const auto &get() const & {
                        if constexpr (I == 0) return d.first;
                        else return d.second;
                }

                /// @copydoc get()
                template <std::size_t I>
                auto &&get() && {
                        if constexpr (I == 0) return std::move(d.first);
                        else return std::move(d.second);
                }

        private:
                std::pair<A, B> d;
};

PROMEKI_NAMESPACE_END

// Structured bindings support
namespace std {
template <typename A, typename B>
struct tuple_size<promeki::Pair<A, B>> : std::integral_constant<std::size_t, 2> {};

template <typename A, typename B>
struct tuple_element<0, promeki::Pair<A, B>> { using type = A; };

template <typename A, typename B>
struct tuple_element<1, promeki::Pair<A, B>> { using type = B; };
} // namespace std
