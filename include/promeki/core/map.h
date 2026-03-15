/**
 * @file      core/map.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <map>
#include <initializer_list>
#include <promeki/core/namespace.h>
#include <promeki/core/sharedptr.h>
#include <promeki/core/list.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Ordered associative container wrapping std::map.
 *
 * Provides a Qt-inspired API over std::map with consistent naming
 * conventions matching the rest of libpromeki.
 *
 * @tparam K Key type.
 * @tparam V Value type.
 */
template <typename K, typename V>
class Map {
        PROMEKI_SHARED_FINAL(Map)
        public:
                /** @brief Shared pointer type for Map. */
                using Ptr = SharedPtr<Map>;

                /** @brief Underlying std::map storage type. */
                using Data = std::map<K, V>;

                /** @brief Mutable forward iterator. */
                using Iterator = typename Data::iterator;

                /** @brief Const forward iterator. */
                using ConstIterator = typename Data::const_iterator;

                /** @brief Mutable reverse iterator. */
                using RevIterator = typename Data::reverse_iterator;

                /** @brief Const reverse iterator. */
                using ConstRevIterator = typename Data::const_reverse_iterator;

                /** @brief Default constructor. Creates an empty map. */
                Map() = default;

                /** @brief Copy constructor. */
                Map(const Map &other) : d(other.d) {}

                /** @brief Move constructor. */
                Map(Map &&other) noexcept : d(std::move(other.d)) {}

                /**
                 * @brief Constructs a map from an initializer list of key-value pairs.
                 * @param initList Brace-enclosed list of {key, value} pairs.
                 */
                Map(std::initializer_list<std::pair<const K, V>> initList) : d(initList) {}

                /** @brief Destructor. */
                ~Map() = default;

                /** @brief Copy assignment operator. */
                Map &operator=(const Map &other) {
                        d = other.d;
                        return *this;
                }

                /** @brief Move assignment operator. */
                Map &operator=(Map &&other) noexcept {
                        d = std::move(other.d);
                        return *this;
                }

                // -- Iterators --

                /** @brief Returns a mutable iterator to the first entry. */
                Iterator begin() noexcept { return d.begin(); }

                /** @brief Returns a const iterator to the first entry. */
                ConstIterator begin() const noexcept { return d.cbegin(); }

                /** @brief Returns a const iterator to the first entry. */
                ConstIterator cbegin() const noexcept { return d.cbegin(); }

                /// @copydoc cbegin()
                ConstIterator constBegin() const noexcept { return d.cbegin(); }

                /** @brief Returns a mutable iterator to one past the last entry. */
                Iterator end() noexcept { return d.end(); }

                /** @brief Returns a const iterator to one past the last entry. */
                ConstIterator end() const noexcept { return d.cend(); }

                /** @brief Returns a const iterator to one past the last entry. */
                ConstIterator cend() const noexcept { return d.cend(); }

                /// @copydoc cend()
                ConstIterator constEnd() const noexcept { return d.cend(); }

                /** @brief Returns a mutable reverse iterator to the last entry. */
                RevIterator rbegin() noexcept { return d.rbegin(); }

                /** @brief Returns a const reverse iterator to the last entry. */
                ConstRevIterator crbegin() const noexcept { return d.crbegin(); }

                /** @brief Returns a mutable reverse iterator to one before the first entry. */
                RevIterator rend() noexcept { return d.rend(); }

                /** @brief Returns a const reverse iterator to one before the first entry. */
                ConstRevIterator crend() const noexcept { return d.crend(); }

                // -- Capacity --

                /** @brief Returns true if the map has no entries. */
                bool isEmpty() const noexcept { return d.empty(); }

                /** @brief Returns the number of key-value pairs. */
                size_t size() const noexcept { return d.size(); }

                // -- Lookup --

                /**
                 * @brief Returns a reference to the value for @p key, inserting a
                 *        default-constructed value if it does not exist.
                 * @param key The key to look up.
                 * @return Reference to the mapped value.
                 */
                V &operator[](const K &key) { return d[key]; }

                /**
                 * @brief Returns a const reference to the value for @p key.
                 *
                 * The key must already exist; behavior is undefined otherwise.
                 * Prefer value() or find() when the key may not exist.
                 *
                 * @param key The key to look up.
                 * @return Const reference to the mapped value.
                 */
                const V &operator[](const K &key) const { return d.at(key); }

                /**
                 * @brief Returns the value for @p key, or @p defaultValue if
                 *        the key is not present.
                 * @param key The key to look up.
                 * @param defaultValue Fallback value.
                 * @return The mapped value or the default.
                 */
                V value(const K &key, const V &defaultValue = V{}) const {
                        auto it = d.find(key);
                        if(it != d.end()) return it->second;
                        return defaultValue;
                }

                /** @brief Returns true if @p key exists in the map. */
                bool contains(const K &key) const { return d.find(key) != d.end(); }

                /**
                 * @brief Finds the entry for @p key.
                 * @param key The key to search for.
                 * @return Iterator to the entry, or end() if not found.
                 */
                Iterator find(const K &key) { return d.find(key); }

                /** @brief Const overload of find(). */
                ConstIterator find(const K &key) const { return d.find(key); }

                // -- Modifiers --

                /**
                 * @brief Inserts or assigns a key-value pair.
                 * @param key The key.
                 * @param val The value.
                 */
                void insert(const K &key, const V &val) {
                        d.insert_or_assign(key, val);
                        return;
                }

                /**
                 * @brief Inserts or assigns a key-value pair (move overload).
                 * @param key The key.
                 * @param val The value (moved).
                 */
                void insert(const K &key, V &&val) {
                        d.insert_or_assign(key, std::move(val));
                        return;
                }

                /**
                 * @brief Removes the entry for @p key.
                 * @param key The key to remove.
                 * @return True if an entry was removed, false if the key was not found.
                 */
                bool remove(const K &key) {
                        return d.erase(key) > 0;
                }

                /**
                 * @brief Removes the entry at @p pos.
                 * @param pos Iterator to the entry to remove.
                 * @return Iterator to the next entry.
                 */
                Iterator remove(Iterator pos) {
                        return d.erase(pos);
                }

                /** @brief Removes all entries. */
                void clear() noexcept {
                        d.clear();
                        return;
                }

                // -- Convenience --

                /** @brief Returns a list of all keys. */
                List<K> keys() const {
                        List<K> ret;
                        ret.reserve(d.size());
                        for(const auto &[k, v] : d) ret.pushToBack(k);
                        return ret;
                }

                /** @brief Returns a list of all values. */
                List<V> values() const {
                        List<V> ret;
                        ret.reserve(d.size());
                        for(const auto &[k, v] : d) ret.pushToBack(v);
                        return ret;
                }

                /**
                 * @brief Calls @p func for every key-value pair.
                 * @tparam Func Callable with signature void(const K &, const V &).
                 * @param func The function to invoke.
                 */
                template <typename Func>
                void forEach(Func &&func) const {
                        for(const auto &[k, v] : d) func(k, v);
                        return;
                }

                // -- Comparison --

                /** @brief Returns true if both maps have identical contents. */
                friend bool operator==(const Map &lhs, const Map &rhs) { return lhs.d == rhs.d; }

                /** @brief Returns true if the maps differ. */
                friend bool operator!=(const Map &lhs, const Map &rhs) { return lhs.d != rhs.d; }

        private:
                Data d;
};

PROMEKI_NAMESPACE_END
