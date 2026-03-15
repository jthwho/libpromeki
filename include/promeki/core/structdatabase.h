/**
 * @file      core/structdatabase.h
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/core/namespace.h>
#include <promeki/core/string.h>
#include <promeki/core/map.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief A database that maps keys to struct entries, initialized from an initializer list.
 *
 * Provides a structure database that can be initialized from an initializer list, allowing
 * you to initialize a structure database on startup.  The struct must contain members named
 * `id` and `name`.  The `id` is used as the key in the map.  The ID 0 must exist as it
 * will be returned as the fallback when an id is not found.  The `name` must be a unique
 * name for each struct entry.
 *
 * @tparam KeyType    The type used as the lookup key (must be convertible from 0 for the fallback).
 * @tparam StructType The struct type stored in the database; must have `id` and `name` members.
 */
template<typename KeyType, typename StructType>
class StructDatabase {
        public:
                using Database = promeki::Map<KeyType, StructType>;
                using NameDatabase = promeki::Map<String, KeyType>;

                /** @brief Constructs an empty database. */
                StructDatabase() = default;

                /** @brief Constructs a database and populates it from an initializer list.
                 *  @param list Initializer list of StructType entries to load. */
                StructDatabase(const std::initializer_list<StructType> &&list) {
                        load(std::move(list));
                }

                /** @brief Adds a single entry to the database.
                 *  @param val The struct entry to add (keyed by val.id). */
                void add(const StructType &&val) {
                        _db[val.id] = std::move(val);
                        _nameDb[val.name] = val.id;
                        return;
                }

                /** @brief Retrieves an entry by its key, falling back to key 0 if not found.
                 *  @param id The key to look up.
                 *  @return A const reference to the matching entry, or the fallback entry. */
                const StructType &get(const KeyType &id) const {
                        auto it = _db.find(id);
                        if(it != _db.end()) return it->second;
                        it = _db.find(static_cast<KeyType>(0));
                        if(it != _db.end()) return it->second;
                        static const StructType fallback{};
                        return fallback;
                }

                /** @brief Looks up a key by the entry's name.
                 *  @param name The unique name to search for.
                 *  @return The corresponding key, or KeyType(0) if not found. */
                KeyType lookupKeyByName(const String &name) const {
                        auto it = _nameDb.find(name);
                        return (it != _nameDb.end()) ?
                            it->second :
                            static_cast<KeyType>(0);
                }
                        
                /** @brief Loads all entries from an initializer list.
                 *  @param list The initializer list of entries to add. */
                void load(const std::initializer_list<StructType> &&list) {
                        for(auto &&item : list) add(std::move(item));
                        return;
                }

                /** @brief Returns a const reference to the underlying database map. */
                const Database &database() const { return _db; }
                /** @brief Returns a mutable reference to the underlying database map. */
                Database &database() { return _db; }

        private:
                Database     _db;
                NameDatabase _nameDb;
};

PROMEKI_NAMESPACE_END

