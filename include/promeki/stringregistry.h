/**
 * @file      stringregistry.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstdint>
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/map.h>
#include <promeki/list.h>
#include <promeki/readwritelock.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Thread-safe append-only registry that maps unique strings to integer IDs.
 * @ingroup util
 *
 * @tparam Tag A tag type that distinguishes separate registries.  Each unique
 *         Tag type gets its own static registry instance.
 *
 * StringRegistry assigns a unique uint32_t ID to each string it encounters.
 * Once assigned, a string's ID never changes.  The registry is append-only;
 * strings cannot be removed.  All operations are thread-safe.
 *
 * The nested Item class provides a lightweight handle (a single uint32_t)
 * for working with registered strings.  Use Item as the public-facing type
 * and let the registry manage the string storage.
 *
 * @warning The integer IDs are assigned in registration order and must not
 *          be persisted or used outside the current process.  They will
 *          differ between runs.
 *
 * @par Example
 * @code
 * struct MyTag {};
 * using MyRegistry = StringRegistry<MyTag>;
 * using MyItem = MyRegistry::Item;
 *
 * MyItem a("video.width");
 * MyItem b("video.height");
 * MyItem c("video.width"); // same ID as a
 * assert(a == c);
 * assert(a != b);
 * String name = a.name(); // "video.width"
 * @endcode
 */
template <typename Tag>
class StringRegistry {
        public:
                /** @brief Sentinel value representing an invalid/unregistered ID. */
                static constexpr uint32_t InvalidID = UINT32_MAX;

                /**
                 * @brief Lightweight handle identifying a registered string.
                 *
                 * Item wraps a uint32_t ID and provides access to the underlying
                 * string via the owning registry.  Items are cheap to copy and
                 * can be compared by integer value.
                 *
                 * @warning The integer ID is assigned at registration time and
                 *          must not be persisted or used outside the current process.
                 */
                class Item {
                        public:
                                /** @brief Constructs an invalid Item with no associated name. */
                                Item() = default;

                                /**
                                 * @brief Constructs an Item, registering the name if not already known.
                                 * @param name The string to register.
                                 */
                                Item(const String &name) : _id(instance().findOrCreate(name)) {}

                                /**
                                 * @brief Constructs an Item, registering the name if not already known.
                                 * @param name The string to register as a C-string.
                                 */
                                Item(const char *name) : _id(instance().findOrCreate(String(name))) {}

                                /**
                                 * @brief Looks up an Item by name without registering it.
                                 * @param name The string to look up.
                                 * @return The Item if found, or an invalid Item if not registered.
                                 */
                                static Item find(const String &name) {
                                        Item item;
                                        item._id = instance().findId(name);
                                        return item;
                                }

                                /**
                                 * @brief Constructs an Item from a raw integer ID.
                                 * @param id The raw ID value.
                                 * @return An Item wrapping the given ID.  No validation is performed.
                                 */
                                static Item fromId(uint32_t id) {
                                        Item item;
                                        item._id = id;
                                        return item;
                                }

                                /**
                                 * @brief Returns the integer ID for this item.
                                 * @return The ID, or InvalidID if this item is invalid.
                                 */
                                uint32_t id() const { return _id; }

                                /**
                                 * @brief Returns the name associated with this item.
                                 * @return The string, or an empty String if invalid.
                                 */
                                String name() const { return instance().name(_id); }

                                /**
                                 * @brief Returns true if this Item refers to a valid registered name.
                                 * @return True if valid, false otherwise.
                                 */
                                bool isValid() const { return _id != InvalidID; }

                                /** @brief Equality comparison by ID. */
                                bool operator==(const Item &other) const { return _id == other._id; }

                                /** @brief Inequality comparison by ID. */
                                bool operator!=(const Item &other) const { return _id != other._id; }

                                /** @brief Less-than comparison by ID (for use in ordered containers). */
                                bool operator<(const Item &other) const { return _id < other._id; }

                        private:
                                uint32_t _id = InvalidID;
                };

                /** @brief Returns the singleton registry instance for this Tag type. */
                static StringRegistry &instance() {
                        static StringRegistry reg;
                        return reg;
                }

                /**
                 * @brief Looks up the ID for a previously registered string.
                 * @param str The string to look up.
                 * @return The ID if found, or InvalidID if the string has not been registered.
                 */
                uint32_t findId(const String &str) const {
                        ReadWriteLock::ReadLocker lock(_lock);
                        auto it = _map.find(str);
                        if(it == _map.end()) return InvalidID;
                        return it->second;
                }

                /**
                 * @brief Returns the ID for a string, registering it if not already present.
                 * @param str The string to look up or register.
                 * @return The ID for the string (existing or newly assigned).
                 */
                uint32_t findOrCreate(const String &str) {
                        // Fast path: check with read lock first.
                        {
                                ReadWriteLock::ReadLocker lock(_lock);
                                auto it = _map.find(str);
                                if(it != _map.end()) return it->second;
                        }
                        // Slow path: acquire write lock and insert.
                        ReadWriteLock::WriteLocker lock(_lock);
                        // Double-check after acquiring write lock.
                        auto it = _map.find(str);
                        if(it != _map.end()) return it->second;
                        uint32_t id = static_cast<uint32_t>(_names.size());
                        _map.insert(str, id);
                        _names.pushToBack(str);
                        return id;
                }

                /**
                 * @brief Returns the string associated with an ID.
                 * @param id The ID to look up.
                 * @return The string if found, or an empty String if the ID is invalid.
                 */
                String name(uint32_t id) const {
                        ReadWriteLock::ReadLocker lock(_lock);
                        if(id >= static_cast<uint32_t>(_names.size())) return String();
                        return _names[id];
                }

                /**
                 * @brief Returns the number of registered strings.
                 * @return The number of entries in the registry.
                 */
                uint32_t count() const {
                        ReadWriteLock::ReadLocker lock(_lock);
                        return static_cast<uint32_t>(_names.size());
                }

                /**
                 * @brief Returns true if the registry contains the given string.
                 * @param str The string to check.
                 * @return True if the string has been registered.
                 */
                bool contains(const String &str) const {
                        return findId(str) != InvalidID;
                }

        private:
                StringRegistry() = default;
                mutable ReadWriteLock   _lock;
                Map<String, uint32_t>   _map;
                List<String>            _names;
};

PROMEKI_NAMESPACE_END
