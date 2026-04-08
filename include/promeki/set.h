/**
 * @file      set.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <set>
#include <initializer_list>
#include <promeki/namespace.h>
#include <promeki/sharedptr.h>
#include <promeki/list.h>
#include <promeki/pair.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Ordered unique-element container wrapping std::set.
 * @ingroup containers
 *
 * Provides a Qt-inspired API over std::set with consistent naming
 * conventions matching the rest of libpromeki.
 *
 *
 * @par Example
 * @code
 * Set<String> tags = {"alpha", "beta"};
 * tags.insert("gamma");
 * bool has = tags.contains("alpha");  // true
 * tags.remove("beta");
 * List<String> sorted = tags.toList();
 * @endcode
 * @tparam T Element type (must support operator<).
 */
template <typename T>
class Set {
        PROMEKI_SHARED_FINAL(Set)
        public:
                /** @brief Shared pointer type for Set. */
                using Ptr = SharedPtr<Set>;

                /** @brief Underlying std::set storage type. */
                using Data = std::set<T>;

                /** @brief Mutable forward iterator. */
                using Iterator = typename Data::iterator;

                /** @brief Const forward iterator. */
                using ConstIterator = typename Data::const_iterator;

                /** @brief Mutable reverse iterator. */
                using RevIterator = typename Data::reverse_iterator;

                /** @brief Const reverse iterator. */
                using ConstRevIterator = typename Data::const_reverse_iterator;

                /** @brief Default constructor. Creates an empty set. */
                Set() = default;

                /** @brief Copy constructor. */
                Set(const Set &other) : d(other.d) {}

                /** @brief Move constructor. */
                Set(Set &&other) noexcept : d(std::move(other.d)) {}

                /**
                 * @brief Constructs a set from an initializer list.
                 * @param initList Brace-enclosed list of values.
                 */
                Set(std::initializer_list<T> initList) : d(initList) {}

                /** @brief Destructor. */
                ~Set() = default;

                /** @brief Copy assignment operator. */
                Set &operator=(const Set &other) {
                        d = other.d;
                        return *this;
                }

                /** @brief Move assignment operator. */
                Set &operator=(Set &&other) noexcept {
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

                // -- Capacity --

                /** @brief Returns true if the set has no elements. */
                bool isEmpty() const noexcept { return d.empty(); }

                /** @brief Returns the number of elements. */
                size_t size() const noexcept { return d.size(); }

                // -- Lookup --

                /** @brief Returns true if @p value exists in the set. */
                bool contains(const T &value) const { return d.find(value) != d.end(); }

                /**
                 * @brief Finds @p value in the set.
                 * @param value The value to search for.
                 * @return Iterator to the element, or end() if not found.
                 */
                Iterator find(const T &value) { return d.find(value); }

                /** @brief Const overload of find(). */
                ConstIterator find(const T &value) const { return d.find(value); }

                /**
                 * @brief Returns an iterator to the first element not less than @p value.
                 * @param value The value to search for.
                 * @return Iterator to the lower bound.
                 */
                Iterator lowerBound(const T &value) { return d.lower_bound(value); }

                /** @brief Const overload of lowerBound(). */
                ConstIterator lowerBound(const T &value) const { return d.lower_bound(value); }

                /**
                 * @brief Returns an iterator to the first element greater than @p value.
                 * @param value The value to search for.
                 * @return Iterator to the upper bound.
                 */
                Iterator upperBound(const T &value) { return d.upper_bound(value); }

                /** @brief Const overload of upperBound(). */
                ConstIterator upperBound(const T &value) const { return d.upper_bound(value); }

                // -- Modifiers --

                /**
                 * @brief Inserts a value into the set.
                 * @param value The value to insert.
                 * @return A @ref Pair of @c (iterator, bool).  The bool is
                 *         @c true if insertion took place, @c false if the
                 *         element already existed; the iterator points at the
                 *         element either way.
                 */
                Pair<Iterator, bool> insert(const T &value) {
                        return Pair<Iterator, bool>(d.insert(value));
                }

                /**
                 * @brief Inserts a value into the set (move overload).
                 * @param value The value to insert (moved).
                 * @return A @ref Pair of @c (iterator, bool); see
                 *         @ref insert(const T&) for the bool semantics.
                 */
                Pair<Iterator, bool> insert(T &&value) {
                        return Pair<Iterator, bool>(d.insert(std::move(value)));
                }

                /**
                 * @brief Removes @p value from the set.
                 * @param value The value to remove.
                 * @return True if the element was removed, false if not found.
                 */
                bool remove(const T &value) {
                        return d.erase(value) > 0;
                }

                /**
                 * @brief Removes the element at @p pos.
                 * @param pos Iterator to the element to remove.
                 * @return Iterator to the next element.
                 */
                Iterator remove(Iterator pos) {
                        return d.erase(pos);
                }

                /** @brief Removes all elements. */
                void clear() noexcept {
                        d.clear();
                        return;
                }

                /**
                 * @brief Swaps contents with another set.
                 * @param other The set to swap with.
                 */
                void swap(Set &other) noexcept {
                        d.swap(other.d);
                        return;
                }

                // -- Set Operations --

                /**
                 * @brief Returns the union of this set and @p other.
                 * @param other The set to unite with.
                 * @return A new Set containing all elements from both sets.
                 */
                Set unite(const Set &other) const {
                        Set ret = *this;
                        for(const auto &v : other.d) ret.d.insert(v);
                        return ret;
                }

                /**
                 * @brief Returns the intersection of this set and @p other.
                 * @param other The set to intersect with.
                 * @return A new Set containing only elements present in both sets.
                 */
                Set intersect(const Set &other) const {
                        Set ret;
                        for(const auto &v : d) {
                                if(other.contains(v)) ret.d.insert(v);
                        }
                        return ret;
                }

                /**
                 * @brief Returns this set minus @p other.
                 * @param other The set to subtract.
                 * @return A new Set containing elements in this set but not in @p other.
                 */
                Set subtract(const Set &other) const {
                        Set ret;
                        for(const auto &v : d) {
                                if(!other.contains(v)) ret.d.insert(v);
                        }
                        return ret;
                }

                // -- Convenience --

                /**
                 * @brief Converts the set to a List.
                 * @return A List containing all elements in sorted order.
                 */
                List<T> toList() const {
                        List<T> ret;
                        ret.reserve(d.size());
                        for(const auto &v : d) ret.pushToBack(v);
                        return ret;
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

                /** @brief Returns true if both sets have identical contents. */
                friend bool operator==(const Set &lhs, const Set &rhs) { return lhs.d == rhs.d; }

                /** @brief Returns true if the sets differ. */
                friend bool operator!=(const Set &lhs, const Set &rhs) { return lhs.d != rhs.d; }

        private:
                Data d;
};

PROMEKI_NAMESPACE_END
