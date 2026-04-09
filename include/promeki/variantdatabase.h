/**
 * @file      variantdatabase.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/stringregistry.h>
#include <promeki/variant.h>
#include <promeki/error.h>
#include <promeki/map.h>
#include <promeki/list.h>
#include <promeki/json.h>
#include <promeki/textstream.h>
#include <promeki/datastream.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief A collection of named Variant values keyed by string-registered IDs.
 * @ingroup util
 *
 * @tparam Tag A tag type that distinguishes this database's ID namespace.
 *         The Tag also serves as the StringRegistry tag, so each
 *         VariantDatabase type gets its own independent ID space.
 *
 * VariantDatabase maps string names to Variant values using integer IDs
 * for fast lookup.  The nested ID type is a StringRegistry::Item scoped
 * to this database's Tag.
 *
 * Supports serialization to/from JSON, TextStream, and DataStream.
 * All serialization formats use the string names (not integer IDs),
 * so data can be safely persisted and loaded across runs.
 *
 * @warning The integer IDs within ID are assigned in registration order
 *          and must not be persisted or used outside the current process.
 *
 * @par Example
 * @code
 * struct ConfigTag {};
 * using Config = VariantDatabase<ConfigTag>;
 *
 * Config cfg;
 * Config::ID width("video.width");
 * Config::ID height("video.height");
 *
 * cfg.set(width, 1920);
 * cfg.set(height, 1080);
 *
 * int w = cfg.get(width).get<int32_t>(); // 1920
 * bool has = cfg.contains(width);        // true
 *
 * // Serialize to JSON
 * JsonObject json = cfg.toJson();
 *
 * // Deserialize from JSON
 * Config cfg2 = Config::fromJson(json);
 * @endcode
 */
template <typename Tag>
class VariantDatabase {
        public:
                /** @brief Lightweight handle identifying an entry by name. */
                using ID = typename StringRegistry<Tag>::Item;

                /** @brief Constructs an empty database. */
                VariantDatabase() = default;

                /**
                 * @brief Creates a VariantDatabase from a JsonObject.
                 *
                 * Each key in the JSON object becomes an ID (registered if new)
                 * and its value is converted to a Variant via Variant::fromJson().
                 *
                 * @param json The JsonObject to deserialize from.
                 * @return A VariantDatabase populated with the JSON contents.
                 */
                static VariantDatabase fromJson(const JsonObject &json) {
                        VariantDatabase db;
                        json.forEach([&db](const String &key, const Variant &val) {
                                db.set(ID(key), val);
                        });
                        return db;
                }

                /**
                 * @brief Sets the value for the given ID.
                 * @param id    The entry identifier.
                 * @param value The value to store.
                 */
                void set(ID id, const Variant &value) {
                        _data.insert(id.id(), value);
                }

                /**
                 * @brief Sets the value for the given ID (move overload).
                 * @param id    The entry identifier.
                 * @param value The value to move-store.
                 */
                void set(ID id, Variant &&value) {
                        _data.insert(id.id(), std::move(value));
                }

                /**
                 * @brief Sets the value only if no entry exists for @p id.
                 *
                 * Unlike @ref set, which always overwrites, this call is a
                 * no-op when the key already has a value.  It is intended
                 * as a primitive for filling in defaults without
                 * clobbering caller-supplied values.
                 *
                 * @param id    The entry identifier.
                 * @param value The value to store if no existing entry.
                 * @return true if the value was stored, false if an entry
                 *         already existed for @p id.
                 */
                bool setIfMissing(ID id, const Variant &value) {
                        if(_data.contains(id.id())) return false;
                        _data.insert(id.id(), value);
                        return true;
                }

                /**
                 * @brief Move overload of @ref setIfMissing.
                 * @param id    The entry identifier.
                 * @param value The value to move-store if no existing entry.
                 * @return true if the value was stored, false if an entry
                 *         already existed for @p id.
                 */
                bool setIfMissing(ID id, Variant &&value) {
                        if(_data.contains(id.id())) return false;
                        _data.insert(id.id(), std::move(value));
                        return true;
                }

                /**
                 * @brief Returns the value for the given ID.
                 * @param id           The entry identifier.
                 * @param defaultValue Value returned if the ID is not present.
                 * @return The stored value, or defaultValue if not found.
                 */
                Variant get(ID id, const Variant &defaultValue = Variant()) const {
                        auto it = _data.find(id.id());
                        if(it == _data.end()) return defaultValue;
                        return it->second;
                }

                /**
                 * @brief Returns the stored value converted to the requested type.
                 *
                 * Combines get() and Variant::get<T>() in a single call.
                 *
                 * @tparam T           The desired result type.
                 * @param id           The entry identifier.
                 * @param defaultValue Value returned if the ID is not present or
                 *                     the conversion fails.
                 * @param err          Optional error output from the Variant conversion.
                 * @return The converted value, or defaultValue if not found or on
                 *         conversion failure.
                 */
                template <typename T>
                T getAs(ID id, const T &defaultValue = T{}, Error *err = nullptr) const {
                        auto it = _data.find(id.id());
                        if(it == _data.end()) {
                                if(err) *err = Error::IdNotFound;
                                return defaultValue;
                        }
                        Error e;
                        T result = it->second.template get<T>(&e);
                        if(e.isError()) {
                                if(err) *err = Error::ConversionFailed;
                                return defaultValue;
                        }
                        if(err) *err = Error::Ok;
                        return result;
                }

                /**
                 * @brief Returns true if the database contains a value for the given ID.
                 * @param id The entry identifier.
                 * @return True if a value is stored for the ID.
                 */
                bool contains(ID id) const {
                        return _data.contains(id.id());
                }

                /**
                 * @brief Removes the value for the given ID.
                 * @param id The entry identifier.
                 * @return True if the entry was removed, false if it was not present.
                 */
                bool remove(ID id) {
                        return _data.remove(id.id());
                }

                /**
                 * @brief Returns the number of entries in the database.
                 * @return The number of stored key-value pairs.
                 */
                size_t size() const {
                        return _data.size();
                }

                /**
                 * @brief Returns true if the database has no entries.
                 * @return True if empty.
                 */
                bool isEmpty() const {
                        return _data.isEmpty();
                }

                /**
                 * @brief Removes all entries from the database.
                 */
                void clear() {
                        _data.clear();
                }

                /**
                 * @brief Returns a list of all IDs that have values in this database.
                 * @return A List of ID handles for every stored entry.
                 */
                List<ID> ids() const {
                        List<ID> ret;
                        for(auto it = _data.cbegin(); it != _data.cend(); ++it) {
                                ret.pushToBack(ID::fromId(it->first));
                        }
                        return ret;
                }

                /**
                 * @brief Iterates over all entries in the database.
                 *
                 * @tparam Func Callable with signature void(ID id, const Variant &val).
                 * @param func The function to invoke for each entry.
                 */
                template <typename Func>
                void forEach(Func &&func) const {
                        for(auto it = _data.cbegin(); it != _data.cend(); ++it) {
                                func(ID::fromId(it->first), it->second);
                        }
                }

                /**
                 * @brief Merges entries from another database into this one.
                 *
                 * For each entry in @p other, the value is copied into this
                 * database.  Existing entries with the same ID are overwritten.
                 *
                 * @param other The database to merge from.
                 */
                void merge(const VariantDatabase &other) {
                        other.forEach([this](ID id, const Variant &val) {
                                set(id, val);
                        });
                }

                /**
                 * @brief Creates a new database containing only the specified IDs.
                 *
                 * IDs that are not present in this database are silently skipped.
                 *
                 * @param idList The list of IDs to extract.
                 * @return A new VariantDatabase containing only the matching entries.
                 */
                VariantDatabase extract(const List<ID> &idList) const {
                        VariantDatabase result;
                        for(size_t i = 0; i < idList.size(); ++i) {
                                auto it = _data.find(idList[i].id());
                                if(it != _data.end()) {
                                        result._data.insert(it->first, it->second);
                                }
                        }
                        return result;
                }

                // ============================================================
                // Comparison
                // ============================================================

                /**
                 * @brief Returns true if both databases contain the same entries.
                 *
                 * Two databases are equal if they hold the same set of IDs and each
                 * corresponding value compares equal via Variant::operator==.
                 *
                 * @par Example
                 * @code
                 * VariantDatabase<MyTag> a, b;
                 * VariantDatabase<MyTag>::ID key("width");
                 * a.set(key, 1920);
                 * b.set(key, 1920);
                 * bool same = (a == b);  // true
                 * @endcode
                 */
                bool operator==(const VariantDatabase &other) const { return _data == other._data; }

                /** @brief Returns true if the databases differ. */
                bool operator!=(const VariantDatabase &other) const { return _data != other._data; }

                // ============================================================
                // JSON serialization
                // ============================================================

                /**
                 * @brief Serializes the database to a JsonObject.
                 *
                 * Each entry is stored as a key-value pair where the key is the
                 * string name of the ID and the value is set via
                 * JsonObject::setFromVariant().
                 *
                 * @return A JsonObject containing all entries.
                 */
                JsonObject toJson() const {
                        JsonObject json;
                        for(auto it = _data.cbegin(); it != _data.cend(); ++it) {
                                String name = StringRegistry<Tag>::instance().name(it->first);
                                json.setFromVariant(name, it->second);
                        }
                        return json;
                }

                // ============================================================
                // DataStream serialization
                // ============================================================

                /**
                 * @brief Writes the database to a DataStream.
                 *
                 * Format: uint32_t entry count, then for each entry:
                 * String name followed by Variant value.
                 *
                 * @param stream The DataStream to write to.
                 */
                void writeTo(DataStream &stream) const {
                        stream << static_cast<uint32_t>(_data.size());
                        for(auto it = _data.cbegin(); it != _data.cend(); ++it) {
                                String name = StringRegistry<Tag>::instance().name(it->first);
                                stream << name << it->second;
                        }
                }

                /**
                 * @brief Reads the database from a DataStream.
                 *
                 * The database is cleared before reading.  Each string name
                 * is registered as an ID (via findOrCreate) so that new IDs
                 * are created as needed.
                 *
                 * @param stream The DataStream to read from.
                 */
                void readFrom(DataStream &stream) {
                        _data.clear();
                        uint32_t count = 0;
                        stream >> count;
                        for(uint32_t i = 0; i < count && stream.status() == DataStream::Ok; ++i) {
                                String name;
                                Variant value;
                                stream >> name >> value;
                                if(stream.status() == DataStream::Ok) {
                                        set(ID(name), std::move(value));
                                }
                        }
                }

                // ============================================================
                // TextStream serialization
                // ============================================================

                /**
                 * @brief Writes the database to a TextStream.
                 *
                 * Each entry is written as a line in the form:
                 * @code
                 * name = value
                 * @endcode
                 * where value is the Variant's string representation.
                 *
                 * @param stream The TextStream to write to.
                 */
                void writeTo(TextStream &stream) const {
                        for(auto it = _data.cbegin(); it != _data.cend(); ++it) {
                                String name = StringRegistry<Tag>::instance().name(it->first);
                                stream << name << " = " << it->second << endl;
                        }
                }

        private:
                Map<uint32_t, Variant> _data;
};

/**
 * @brief Writes a VariantDatabase to a DataStream.
 * @param stream The DataStream to write to.
 * @param db     The VariantDatabase to serialize.
 * @return A reference to the stream.
 */
template <typename Tag>
DataStream &operator<<(DataStream &stream, const VariantDatabase<Tag> &db) {
        db.writeTo(stream);
        return stream;
}

/**
 * @brief Reads a VariantDatabase from a DataStream.
 * @param stream The DataStream to read from.
 * @param db     The VariantDatabase to populate (cleared first).
 * @return A reference to the stream.
 */
template <typename Tag>
DataStream &operator>>(DataStream &stream, VariantDatabase<Tag> &db) {
        db.readFrom(stream);
        return stream;
}

/**
 * @brief Writes a VariantDatabase to a TextStream.
 * @param stream The TextStream to write to.
 * @param db     The VariantDatabase to serialize.
 * @return A reference to the stream.
 */
template <typename Tag>
TextStream &operator<<(TextStream &stream, const VariantDatabase<Tag> &db) {
        db.writeTo(stream);
        return stream;
}

PROMEKI_NAMESPACE_END
