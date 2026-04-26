/**
 * @file      stack.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstddef>
#include <stack>
#include <stdexcept>
#include <promeki/namespace.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief LIFO stack container wrapping std::stack.
 * @ingroup containers
 *
 * Provides a Qt-inspired API over std::stack with consistent naming
 * conventions matching the rest of libpromeki. Simple value type —
 * no PROMEKI_SHARED_FINAL (not typically shared or iterable).
 *
 * @par Thread Safety
 * Conditionally thread-safe.  Distinct instances may be used
 * concurrently; concurrent access to a single instance must be
 * externally synchronized.  See @ref Queue for a thread-safe
 * FIFO alternative.
 *
 * @tparam T Element type.
 */
template <typename T> class Stack {
        public:
                /** @brief Default constructor. Creates an empty stack. */
                Stack() = default;

                /** @brief Copy constructor. */
                Stack(const Stack &other) : d(other.d) {}

                /** @brief Move constructor. */
                Stack(Stack &&other) noexcept : d(std::move(other.d)) {}

                /** @brief Destructor. */
                ~Stack() = default;

                /** @brief Copy assignment operator. */
                Stack &operator=(const Stack &other) {
                        d = other.d;
                        return *this;
                }

                /** @brief Move assignment operator. */
                Stack &operator=(Stack &&other) noexcept {
                        d = std::move(other.d);
                        return *this;
                }

                // -- Capacity --

                /** @brief Returns true if the stack has no elements. */
                bool isEmpty() const { return d.empty(); }

                /** @brief Returns the number of elements. */
                size_t size() const { return d.size(); }

                // -- Access --

                /** @brief Returns a mutable reference to the top element. */
                T &top() { return d.top(); }

                /** @brief Returns a const reference to the top element. */
                const T &top() const { return d.top(); }

                /** @brief Returns a const reference to the top element. */
                const T &constTop() const { return d.top(); }

                // -- Modifiers --

                /**
                 * @brief Pushes a value onto the top of the stack.
                 * @param value The value to push.
                 */
                void push(const T &value) {
                        d.push(value);
                        return;
                }

                /**
                 * @brief Pushes a value onto the top of the stack (move overload).
                 * @param value The value to move-push.
                 */
                void push(T &&value) {
                        d.push(std::move(value));
                        return;
                }

                /**
                 * @brief Removes and returns the top element.
                 * @pre @c isEmpty() is false; otherwise throws @c std::logic_error.
                 * @return The removed element.
                 */
                T pop() {
                        if (d.empty()) throw std::logic_error("Stack::pop on empty stack");
                        T val = std::move(d.top());
                        d.pop();
                        return val;
                }

                /** @brief Removes all elements. */
                void clear() {
                        while (!d.empty()) d.pop();
                        return;
                }

                /**
                 * @brief Swaps contents with another stack.
                 * @param other The stack to swap with.
                 */
                void swap(Stack &other) noexcept {
                        d.swap(other.d);
                        return;
                }

                // -- Comparison --

                /** @brief Returns true if both stacks have identical contents. */
                friend bool operator==(const Stack &lhs, const Stack &rhs) { return lhs.d == rhs.d; }

                /** @brief Returns true if the stacks differ. */
                friend bool operator!=(const Stack &lhs, const Stack &rhs) { return lhs.d != rhs.d; }

        private:
                std::stack<T> d;
};

PROMEKI_NAMESPACE_END
