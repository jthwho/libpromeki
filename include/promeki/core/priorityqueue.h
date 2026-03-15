/**
 * @file      core/priorityqueue.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstddef>
#include <queue>
#include <vector>
#include <functional>
#include <promeki/core/namespace.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Priority queue container wrapping std::priority_queue.
 *
 * Provides a Qt-inspired API over std::priority_queue with consistent naming
 * conventions matching the rest of libpromeki. Not thread-safe; for use
 * inside synchronized contexts. Simple value type — no PROMEKI_SHARED_FINAL.
 *
 * @tparam T Element type.
 * @tparam Compare Comparison function type (default: std::less<T>, which
 *         gives a max-heap where pop() returns the largest element).
 */
template <typename T, typename Compare = std::less<T>>
class PriorityQueue {
        public:
                /** @brief Default constructor. Creates an empty priority queue. */
                PriorityQueue() = default;

                /** @brief Copy constructor. */
                PriorityQueue(const PriorityQueue &other) : d(other.d) {}

                /** @brief Move constructor. */
                PriorityQueue(PriorityQueue &&other) noexcept : d(std::move(other.d)) {}

                /** @brief Destructor. */
                ~PriorityQueue() = default;

                /** @brief Copy assignment operator. */
                PriorityQueue &operator=(const PriorityQueue &other) {
                        d = other.d;
                        return *this;
                }

                /** @brief Move assignment operator. */
                PriorityQueue &operator=(PriorityQueue &&other) noexcept {
                        d = std::move(other.d);
                        return *this;
                }

                // -- Capacity --

                /** @brief Returns true if the priority queue has no elements. */
                bool isEmpty() const { return d.empty(); }

                /** @brief Returns the number of elements. */
                size_t size() const { return d.size(); }

                // -- Access --

                /** @brief Returns a const reference to the highest-priority element. */
                const T &top() const { return d.top(); }

                // -- Modifiers --

                /**
                 * @brief Pushes a value into the priority queue.
                 * @param value The value to insert.
                 */
                void push(const T &value) {
                        d.push(value);
                        return;
                }

                /**
                 * @brief Pushes a value into the priority queue (move overload).
                 * @param value The value to move-insert.
                 */
                void push(T &&value) {
                        d.push(std::move(value));
                        return;
                }

                /**
                 * @brief Removes and returns the highest-priority element.
                 * @return The removed element.
                 */
                T pop() {
                        T val = d.top();
                        d.pop();
                        return val;
                }

                /**
                 * @brief Swaps contents with another priority queue.
                 * @param other The priority queue to swap with.
                 */
                void swap(PriorityQueue &other) noexcept {
                        d.swap(other.d);
                        return;
                }

        private:
                std::priority_queue<T, std::vector<T>, Compare> d;
};

PROMEKI_NAMESPACE_END
