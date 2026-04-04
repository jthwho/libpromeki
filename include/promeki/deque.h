/**
 * @file      deque.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstddef>
#include <deque>
#include <initializer_list>
#include <promeki/namespace.h>
#include <promeki/sharedptr.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Double-ended queue container wrapping std::deque.
 * @ingroup containers
 *
 * Provides a Qt-inspired API over std::deque with consistent naming
 *  *
 * @par Example
 * @code
 * Deque<int> dq;
 * dq.pushToBack(1);
 * dq.pushToFront(0);
 * int first = dq.front();  // 0
 * dq.popFromFront();
 * @endcode
conventions matching the rest of libpromeki.
 *
 * @tparam T Element type.
 */
template <typename T>
class Deque {
        PROMEKI_SHARED_FINAL(Deque)
        public:
                /** @brief Shared pointer type for Deque. */
                using Ptr = SharedPtr<Deque>;

                /** @brief Underlying std::deque storage type. */
                using Data = std::deque<T>;

                /** @brief Value type. */
                using Value = T;

                /** @brief Mutable forward iterator. */
                using Iterator = typename Data::iterator;

                /** @brief Const forward iterator. */
                using ConstIterator = typename Data::const_iterator;

                /** @brief Mutable reverse iterator. */
                using RevIterator = typename Data::reverse_iterator;

                /** @brief Const reverse iterator. */
                using ConstRevIterator = typename Data::const_reverse_iterator;

                /** @brief Default constructor. Creates an empty deque. */
                Deque() = default;

                /** @brief Copy constructor. */
                Deque(const Deque &other) : d(other.d) {}

                /** @brief Move constructor. */
                Deque(Deque &&other) noexcept : d(std::move(other.d)) {}

                /**
                 * @brief Constructs a deque from an initializer list.
                 * @param initList Brace-enclosed list of values.
                 */
                Deque(std::initializer_list<T> initList) : d(initList) {}

                /** @brief Destructor. */
                ~Deque() = default;

                /** @brief Copy assignment operator. */
                Deque &operator=(const Deque &other) {
                        d = other.d;
                        return *this;
                }

                /** @brief Move assignment operator. */
                Deque &operator=(Deque &&other) noexcept {
                        d = std::move(other.d);
                        return *this;
                }

                // -- Iterators --

                /** @brief Returns a mutable iterator to the first element. */
                Iterator begin() noexcept { return d.begin(); }

                /** @brief Returns a const iterator to the first element. */
                ConstIterator begin() const noexcept { return d.cbegin(); }

                /** @brief Returns a const iterator to the first element. */
                ConstIterator cbegin() const noexcept { return d.cbegin(); }

                /// @copydoc cbegin()
                ConstIterator constBegin() const noexcept { return d.cbegin(); }

                /** @brief Returns a mutable iterator to one past the last element. */
                Iterator end() noexcept { return d.end(); }

                /** @brief Returns a const iterator to one past the last element. */
                ConstIterator end() const noexcept { return d.cend(); }

                /** @brief Returns a const iterator to one past the last element. */
                ConstIterator cend() const noexcept { return d.cend(); }

                /// @copydoc cend()
                ConstIterator constEnd() const noexcept { return d.cend(); }

                /** @brief Returns a mutable reverse iterator to the last element. */
                RevIterator rbegin() noexcept { return d.rbegin(); }

                /// @copydoc rbegin()
                RevIterator revBegin() noexcept { return d.rbegin(); }

                /** @brief Returns a const reverse iterator to the last element. */
                ConstRevIterator crbegin() const noexcept { return d.crbegin(); }

                /// @copydoc crbegin()
                ConstRevIterator constRevBegin() const noexcept { return d.crbegin(); }

                /** @brief Returns a mutable reverse iterator to one before the first element. */
                RevIterator rend() noexcept { return d.rend(); }

                /// @copydoc rend()
                RevIterator revEnd() noexcept { return d.rend(); }

                /** @brief Returns a const reverse iterator to one before the first element. */
                ConstRevIterator crend() const noexcept { return d.crend(); }

                /// @copydoc crend()
                ConstRevIterator constRevEnd() const noexcept { return d.crend(); }

                // -- Access --

                /**
                 * @brief Returns a reference to the element at @p index with bounds checking.
                 * @param index Zero-based element index.
                 * @return Reference to the element.
                 */
                T &at(size_t index) { return d.at(index); }

                /// @copydoc at()
                const T &at(size_t index) const { return d.at(index); }

                /**
                 * @brief Returns a reference to the element at @p index without bounds checking.
                 * @param index Zero-based element index.
                 * @return Reference to the element.
                 */
                T &operator[](size_t index) { return d[index]; }

                /// @copydoc operator[]()
                const T &operator[](size_t index) const { return d[index]; }

                /** @brief Returns a reference to the first element. */
                T &front() { return d.front(); }

                /// @copydoc front()
                const T &front() const { return d.front(); }

                /** @brief Returns a reference to the last element. */
                T &back() { return d.back(); }

                /// @copydoc back()
                const T &back() const { return d.back(); }

                // -- Capacity --

                /** @brief Returns true if the deque has no elements. */
                bool isEmpty() const noexcept { return d.empty(); }

                /** @brief Returns the number of elements. */
                size_t size() const noexcept { return d.size(); }

                // -- Modifiers --

                /**
                 * @brief Pushes an item onto the front of the deque.
                 * @param value The value to prepend.
                 */
                void pushToFront(const T &value) {
                        d.push_front(value);
                        return;
                }

                /**
                 * @brief Pushes an item onto the front of the deque (move overload).
                 * @param value The value to move-prepend.
                 */
                void pushToFront(T &&value) {
                        d.push_front(std::move(value));
                        return;
                }

                /**
                 * @brief Pushes an item onto the back of the deque.
                 * @param value The value to append.
                 */
                void pushToBack(const T &value) {
                        d.push_back(value);
                        return;
                }

                /**
                 * @brief Pushes an item onto the back of the deque (move overload).
                 * @param value The value to move-append.
                 */
                void pushToBack(T &&value) {
                        d.push_back(std::move(value));
                        return;
                }

                /**
                 * @brief Removes and returns the first element.
                 * @return The removed element.
                 */
                T popFromFront() {
                        T val = std::move(d.front());
                        d.pop_front();
                        return val;
                }

                /**
                 * @brief Removes and returns the last element.
                 * @return The removed element.
                 */
                T popFromBack() {
                        T val = std::move(d.back());
                        d.pop_back();
                        return val;
                }

                /** @brief Removes all elements. */
                void clear() noexcept {
                        d.clear();
                        return;
                }

                /**
                 * @brief Swaps contents with another deque.
                 * @param other The deque to swap with.
                 */
                void swap(Deque &other) noexcept {
                        d.swap(other.d);
                        return;
                }

                /**
                 * @brief Calls @p func for every element.
                 * @tparam Func Callable with signature void(const T &).
                 * @param func The function to invoke.
                 */
                template <typename Func>
                void forEach(Func &&func) const {
                        for(const auto &v : d) func(v);
                        return;
                }

                // -- Comparison --

                /** @brief Returns true if both deques have identical contents. */
                friend bool operator==(const Deque &lhs, const Deque &rhs) { return lhs.d == rhs.d; }

                /** @brief Returns true if the deques differ. */
                friend bool operator!=(const Deque &lhs, const Deque &rhs) { return lhs.d != rhs.d; }

        private:
                Data d;
};

PROMEKI_NAMESPACE_END
