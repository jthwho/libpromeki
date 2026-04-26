/**
 * @file      hashset.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <unordered_set>
#include <initializer_list>
#include <promeki/namespace.h>
#include <promeki/sharedptr.h>
#include <promeki/list.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Unordered unique-element container wrapping std::unordered_set.
 * @ingroup containers
 *
 * Provides a Qt-inspired API over std::unordered_set with consistent naming
 * conventions matching the rest of libpromeki.
 *
 * @par Thread Safety
 * Conditionally thread-safe.  Distinct instances may be used
 * concurrently; concurrent access to a single instance must be
 * externally synchronized.
 *
 * @tparam T Element type (must be hashable).
 */
template <typename T>
class HashSet {
        PROMEKI_SHARED_FINAL(HashSet)
        public:
                /** @brief Shared pointer type for HashSet. */
                using Ptr = SharedPtr<HashSet>;

                /** @brief Underlying std::unordered_set storage type. */
                using Data = std::unordered_set<T>;

                /** @brief Value type. */
                using Value = T;

                /** @brief Mutable forward iterator. */
                using Iterator = typename Data::iterator;

                /** @brief Const forward iterator. */
                using ConstIterator = typename Data::const_iterator;

                /** @brief Default constructor. Creates an empty hash set. */
                HashSet() = default;

                /** @brief Copy constructor. */
                HashSet(const HashSet &other) : d(other.d) {}

                /** @brief Move constructor. */
                HashSet(HashSet &&other) noexcept : d(std::move(other.d)) {}

                /**
                 * @brief Constructs a hash set from an initializer list.
                 * @param initList Brace-enclosed list of values.
                 */
                HashSet(std::initializer_list<T> initList) : d(initList) {}

                /** @brief Destructor. */
                ~HashSet() = default;

                /** @brief Copy assignment operator. */
                HashSet &operator=(const HashSet &other) {
                        d = other.d;
                        return *this;
                }

                /** @brief Move assignment operator. */
                HashSet &operator=(HashSet &&other) noexcept {
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

                // -- Capacity --

                /** @brief Returns true if the hash set has no elements. */
                bool isEmpty() const noexcept { return d.empty(); }

                /** @brief Returns the number of elements. */
                size_t size() const noexcept { return d.size(); }

                // -- Lookup --

                /** @brief Returns true if @p value exists in the hash set. */
                bool contains(const T &value) const { return d.find(value) != d.end(); }

                // -- Modifiers --

                /**
                 * @brief Inserts a value into the hash set.
                 * @param value The value to insert.
                 * @return True if the value was inserted, false if it already existed.
                 */
                bool insert(const T &value) {
                        return d.insert(value).second;
                }

                /**
                 * @brief Inserts a value into the hash set (move overload).
                 * @param value The value to insert (moved).
                 * @return True if the value was inserted, false if it already existed.
                 */
                bool insert(T &&value) {
                        return d.insert(std::move(value)).second;
                }

                /**
                 * @brief Removes @p value from the hash set.
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
                 * @brief Swaps contents with another hash set.
                 * @param other The hash set to swap with.
                 */
                void swap(HashSet &other) noexcept {
                        d.swap(other.d);
                        return;
                }

                // -- Set Operations --

                /**
                 * @brief Returns the union of this set and @p other.
                 * @param other The set to unite with.
                 * @return A new HashSet containing all elements from both sets.
                 */
                HashSet unite(const HashSet &other) const {
                        HashSet ret = *this;
                        for(const auto &v : other.d) ret.d.insert(v);
                        return ret;
                }

                /**
                 * @brief Returns the intersection of this set and @p other.
                 * @param other The set to intersect with.
                 * @return A new HashSet containing only elements present in both sets.
                 */
                HashSet intersect(const HashSet &other) const {
                        HashSet ret;
                        for(const auto &v : d) {
                                if(other.contains(v)) ret.d.insert(v);
                        }
                        return ret;
                }

                /**
                 * @brief Returns this set minus @p other.
                 * @param other The set to subtract.
                 * @return A new HashSet containing elements in this set but not in @p other.
                 */
                HashSet subtract(const HashSet &other) const {
                        HashSet ret;
                        for(const auto &v : d) {
                                if(!other.contains(v)) ret.d.insert(v);
                        }
                        return ret;
                }

                // -- Convenience --

                /**
                 * @brief Converts the hash set to a List.
                 * @return A List containing all elements (order is unspecified).
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

                /** @brief Returns true if both hash sets have identical contents. */
                friend bool operator==(const HashSet &lhs, const HashSet &rhs) { return lhs.d == rhs.d; }

                /** @brief Returns true if the hash sets differ. */
                friend bool operator!=(const HashSet &lhs, const HashSet &rhs) { return lhs.d != rhs.d; }

        private:
                Data d;
};

PROMEKI_NAMESPACE_END
