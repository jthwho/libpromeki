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
#include <promeki/variantspec.h>
#include <promeki/error.h>
#include <promeki/map.h>
#include <promeki/list.h>
#include <promeki/json.h>
#include <promeki/textstream.h>
#include <promeki/datastream.h>
#include <promeki/readwritelock.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Validation mode for VariantDatabase::set().
 * @ingroup util
 *
 * Controls whether values are checked against the registered
 * VariantSpec when stored via @ref VariantDatabase::set.
 */
enum class SpecValidation {
        None,   ///< @brief No validation — values are stored unconditionally.
        Warn,   ///< @brief Log a warning for out-of-spec values, but store them.
        Strict  ///< @brief Reject out-of-spec values (set returns false).
};

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
 * Each ID can optionally have a @ref VariantSpec registered via
 * @ref declareID.  Specs describe the accepted types, numeric range,
 * default value, and human-readable description.  When a spec is
 * registered, @ref set can optionally validate incoming values
 * (controlled by @ref setValidation).
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
 * static inline const Config::ID Width = Config::declareID("Width",
 *     VariantSpec().setType(Variant::TypeS32).setDefault(1920)
 *                  .setRange(1, 8192).setDescription("Frame width in pixels"));
 *
 * Config cfg;
 * cfg.set(Width, 1920);
 * int w = cfg.getAs<int32_t>(Width); // 1920
 *
 * const VariantSpec *s = Config::spec(Width);
 * String desc = s->description(); // "Frame width in pixels"
 * @endcode
 */
template <typename Tag>
class VariantDatabase {
        public:
                /** @brief Lightweight handle identifying an entry by name. */
                using ID = typename StringRegistry<Tag>::Item;

                /** @brief Map of ID to VariantSpec for batch spec operations. */
                using SpecMap = Map<ID, VariantSpec>;

                // ============================================================
                // Static spec registry
                // ============================================================

                /**
                 * @brief Declares an ID and registers its VariantSpec.
                 *
                 * This is the mandatory way to declare well-known IDs.
                 * The string name is registered in the StringRegistry and
                 * the spec is stored in the per-Tag spec registry.
                 *
                 * @param name The string name for the ID.
                 * @param spec The VariantSpec describing this ID.
                 * @return The newly created (or existing) ID.
                 *
                 * @par Example
                 * @code
                 * static inline const ID Width = declareID("Width",
                 *     VariantSpec().setType(Variant::TypeS32)
                 *                  .setDefault(1920)
                 *                  .setDescription("Frame width"));
                 * @endcode
                 */
                static ID declareID(const String &name, const VariantSpec &spec) {
                        ID id(name);
                        specRegistry().insert(id.id(), spec);
                        return id;
                }

                /**
                 * @brief Returns the VariantSpec for the given ID, or nullptr if none.
                 * @param id The ID to look up.
                 * @return A pointer to the spec, or nullptr if no spec was registered.
                 */
                static const VariantSpec *spec(ID id) {
                        return specRegistry().find(id.id());
                }

                /**
                 * @brief Returns a copy of the entire spec registry.
                 * @return A map from integer ID to VariantSpec.
                 */
                static Map<uint32_t, VariantSpec> registeredSpecs() {
                        return specRegistry().all();
                }

                /**
                 * @brief Builds a VariantDatabase from a SpecMap's default values.
                 *
                 * For each entry in @p specs, the spec's default value is
                 * stored under the corresponding ID.  This is the canonical
                 * way to convert a set of specs into a concrete configuration.
                 *
                 * @param specs The spec map to extract defaults from.
                 * @return A VariantDatabase populated with default values.
                 */
                static VariantDatabase fromSpecs(const SpecMap &specs) {
                        VariantDatabase db;
                        for(auto it = specs.cbegin(); it != specs.cend(); ++it) {
                                const Variant &def = it->second.defaultValue();
                                if(def.isValid()) db._data.insert(it->first.id(), def);
                        }
                        return db;
                }

                /**
                 * @brief Writes formatted help text for every spec in a SpecMap.
                 *
                 * Iterates the map in key-name order and calls
                 * @ref VariantSpec::writeHelp for each entry.  This is the
                 * canonical way to dump a backend's configuration reference
                 * to a terminal or log.
                 *
                 * @param stream The output stream.
                 * @param specs  The spec map to format.
                 */
                static void writeSpecMapHelp(TextStream &stream, const SpecMap &specs) {
                        // Collect and sort by name for stable output.
                        StringList names;
                        for(auto it = specs.cbegin(); it != specs.cend(); ++it) {
                                names.pushToBack(it->first.name());
                        }
                        names = names.sort();
                        for(size_t i = 0; i < names.size(); ++i) {
                                ID id(names[i]);
                                auto it = specs.find(id);
                                if(it != specs.end()) {
                                        it->second.writeHelp(stream, names[i]);
                                }
                        }
                }

                // ============================================================
                // Construction
                // ============================================================

                /** @brief Constructs an empty database with Warn validation. */
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

                // ============================================================
                // Validation mode
                // ============================================================

                /**
                 * @brief Sets the validation mode for this database instance.
                 *
                 * - @ref SpecValidation::None — no checking on set().
                 * - @ref SpecValidation::Warn — log a warning but store anyway (default).
                 * - @ref SpecValidation::Strict — reject out-of-spec values.
                 *
                 * @param mode The validation mode.
                 */
                void setValidation(SpecValidation mode) { _validation = mode; }

                /**
                 * @brief Returns the current validation mode.
                 * @return The validation mode.
                 */
                SpecValidation validation() const { return _validation; }

                // ============================================================
                // Value management
                // ============================================================

                /**
                 * @brief Sets the value for the given ID.
                 *
                 * If a VariantSpec is registered for this ID and validation
                 * is enabled, the value is checked before storing.  In Warn
                 * mode, a warning is logged but the value is stored.  In
                 * Strict mode, the value is rejected and false is returned.
                 *
                 * @param id    The entry identifier.
                 * @param value The value to store.
                 * @return True if the value was stored.  False only in Strict
                 *         mode when validation fails.
                 */
                bool set(ID id, const Variant &value) {
                        if(!validateOnSet(id, value)) return false;
                        _data.insert(id.id(), value);
                        return true;
                }

                /**
                 * @brief Sets the value for the given ID (move overload).
                 * @param id    The entry identifier.
                 * @param value The value to move-store.
                 * @return True if the value was stored.
                 */
                bool set(ID id, Variant &&value) {
                        if(!validateOnSet(id, value)) return false;
                        _data.insert(id.id(), std::move(value));
                        return true;
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
                Map<uint32_t, Variant>  _data;
                SpecValidation          _validation = SpecValidation::Warn;

                /**
                 * @brief Thread-safe spec registry for this Tag's ID namespace.
                 *
                 * Mirrors StringRegistry's singleton pattern: one registry per
                 * Tag, accessed via a function-local static.  Read/write locked
                 * so that static-init-time registrations from multiple TUs are safe.
                 */
                struct SpecRegistry {
                        static SpecRegistry &instance() {
                                static SpecRegistry reg;
                                return reg;
                        }

                        void insert(uint32_t id, const VariantSpec &spec) {
                                ReadWriteLock::WriteLocker lock(_lock);
                                _specs.insert(id, spec);
                        }

                        const VariantSpec *find(uint32_t id) const {
                                ReadWriteLock::ReadLocker lock(_lock);
                                auto it = _specs.find(id);
                                if(it == _specs.end()) return nullptr;
                                return &it->second;
                        }

                        Map<uint32_t, VariantSpec> all() const {
                                ReadWriteLock::ReadLocker lock(_lock);
                                return _specs;
                        }

                private:
                        SpecRegistry() = default;
                        mutable ReadWriteLock           _lock;
                        Map<uint32_t, VariantSpec>      _specs;
                };

                static SpecRegistry &specRegistry() { return SpecRegistry::instance(); }

                /**
                 * @brief Validates @p value against the spec for @p id.
                 *
                 * Called by set() when validation is enabled.  Returns true
                 * if the value should be stored, false if Strict mode rejects it.
                 */
                bool validateOnSet(ID id, const Variant &value) {
                        if(_validation == SpecValidation::None) return true;
                        const VariantSpec *s = specRegistry().find(id.id());
                        if(!s) return true;
                        Error err;
                        if(!s->validate(value, &err)) {
                                if(_validation == SpecValidation::Warn) {
                                        promekiWarn("VariantDatabase: value for '%s' fails spec (%s)",
                                                    id.name().cstr(), err.name().cstr());
                                        return true;
                                }
                                return false; // Strict: reject
                        }
                        return true;
                }
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
