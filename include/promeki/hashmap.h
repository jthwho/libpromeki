/**
 * @file      hashmap.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <unordered_map>
#include <initializer_list>
#include <promeki/namespace.h>
#include <promeki/sharedptr.h>
#include <promeki/list.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Unordered associative container wrapping std::unordered_map.
 * @ingroup containers
 *
 * Provides a Qt-inspired API over std::unordered_map with consistent naming
 * conventions matching the rest of libpromeki.
 *
 * @par Thread Safety
 * Conditionally thread-safe.  Distinct instances may be used
 * concurrently; concurrent access to a single instance must be
 * externally synchronized.
 *
 * @tparam K Key type (must be hashable).
 * @tparam V Value type.
 *
 * @par Example
 * @code
 * HashMap<String, int> cache;
 * cache.insert("width", 1920);
 * cache.insert("height", 1080);
 * int w = cache.value("width", 0);  // 1920
 * @endcode
 */
template <typename K, typename V> class HashMap {
                PROMEKI_SHARED_FINAL(HashMap)
        public:
                /** @brief Shared pointer type for HashMap. */
                using Ptr = SharedPtr<HashMap>;

                /** @brief Underlying std::unordered_map storage type. */
                using Data = std::unordered_map<K, V>;

                /** @brief Key type. */
                using Key = K;

                /** @brief Value type. */
                using Value = V;

                /** @brief Mutable forward iterator. */
                using Iterator = typename Data::iterator;

                /** @brief Const forward iterator. */
                using ConstIterator = typename Data::const_iterator;

                /** @brief Default constructor. Creates an empty hash map. */
                HashMap() = default;

                /** @brief Copy constructor. */
                HashMap(const HashMap &other) : d(other.d) {}

                /** @brief Move constructor. */
                HashMap(HashMap &&other) noexcept : d(std::move(other.d)) {}

                /**
                 * @brief Constructs a hash map from an initializer list of key-value pairs.
                 * @param initList Brace-enclosed list of {key, value} pairs.
                 */
                HashMap(std::initializer_list<std::pair<const K, V>> initList) : d(initList) {}

                /** @brief Destructor. */
                ~HashMap() = default;

                /** @brief Copy assignment operator. */
                HashMap &operator=(const HashMap &other) {
                        d = other.d;
                        return *this;
                }

                /** @brief Move assignment operator. */
                HashMap &operator=(HashMap &&other) noexcept {
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

                // -- Capacity --

                /** @brief Returns true if the hash map has no entries. */
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
                        if (it != d.end()) return it->second;
                        return defaultValue;
                }

                /** @brief Returns true if @p key exists in the hash map. */
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
                bool remove(const K &key) { return d.erase(key) > 0; }

                /**
                 * @brief Removes the entry at @p pos.
                 * @param pos Iterator to the entry to remove.
                 * @return Iterator to the next entry.
                 */
                Iterator remove(Iterator pos) { return d.erase(pos); }

                /** @brief Removes all entries. */
                void clear() noexcept {
                        d.clear();
                        return;
                }

                /**
                 * @brief Swaps contents with another hash map.
                 * @param other The hash map to swap with.
                 */
                void swap(HashMap &other) noexcept {
                        d.swap(other.d);
                        return;
                }

                // -- Convenience --

                /** @brief Returns a list of all keys. */
                List<K> keys() const {
                        List<K> ret;
                        ret.reserve(d.size());
                        for (const auto &[k, v] : d) ret.pushToBack(k);
                        return ret;
                }

                /** @brief Returns a list of all values. */
                List<V> values() const {
                        List<V> ret;
                        ret.reserve(d.size());
                        for (const auto &[k, v] : d) ret.pushToBack(v);
                        return ret;
                }

                /**
                 * @brief Calls @p func for every key-value pair.
                 * @tparam Func Callable with signature void(const K &, const V &).
                 * @param func The function to invoke.
                 */
                template <typename Func> void forEach(Func &&func) const {
                        for (const auto &[k, v] : d) func(k, v);
                        return;
                }

                // -- Comparison --

                /** @brief Returns true if both hash maps have identical contents. */
                friend bool operator==(const HashMap &lhs, const HashMap &rhs) { return lhs.d == rhs.d; }

                /** @brief Returns true if the hash maps differ. */
                friend bool operator!=(const HashMap &lhs, const HashMap &rhs) { return lhs.d != rhs.d; }

        private:
                Data d;
};

PROMEKI_NAMESPACE_END
