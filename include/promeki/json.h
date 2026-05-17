/**
 * @file      json.h
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_CORE
#include <functional>
#include <utility>
#include <promeki/function.h>
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/list.h>
#include <promeki/sharedptr.h>
#include <promeki/datatype.h>
#include <promeki/uuid.h>
#include <nlohmann/json.hpp>

PROMEKI_NAMESPACE_BEGIN

class Variant;
class VariantList;
class VariantMap;
class DataStream;

class JsonValue;
class JsonObject;
class JsonArray;

/**
 * @brief Internal CoW storage shared by JsonObject, JsonArray, and JsonValue.
 *
 * Exposed as a (pre-declared) struct in the public header purely so the
 * three handle types can hold @c SharedPtr<JsonData> directly; the
 * member is private in every handle.  Sharing one storage type lets
 * @c JsonValue::JsonValue(const JsonObject &) bump the refcount instead
 * of deep-copying the tree.
 *
 * @see JsonObject for the value-type / CoW contract.
 */
struct JsonData {
                PROMEKI_SHARED_FINAL(JsonData)
                nlohmann::json j;

                JsonData() = default;
                explicit JsonData(nlohmann::json v) : j(std::move(v)) {}
};

/**
 * @brief Tagged-union JSON leaf value matching Qt's QJsonValue.
 * @ingroup util
 *
 * JsonValue holds any JSON value — null, bool, number, string, array, or
 * object — plus an explicit @ref Undefined state for "no such key /
 * index" lookups.  Use it to query containers without committing to a
 * type up front:
 *
 * @code
 * JsonObject obj = JsonObject::parse(text);
 * JsonValue v = obj["width"];
 * if (v.isDouble()) {
 *     int w = static_cast<int>(v.toInt());
 * } else if (v.isUndefined()) {
 *     // "width" key wasn't present
 * }
 * @endcode
 *
 * @par Storage and copy semantics
 * Like @ref JsonObject and @ref JsonArray, JsonValue is a value-type
 * handle backed by an internal @c SharedPtr<JsonData>.  Copying a
 * JsonValue is O(1).  Constructing a JsonValue from a JsonObject /
 * JsonArray shares storage with the source — no deep copy until either
 * side mutates (and JsonValue itself is immutable, so it never forces
 * a detach).  Constructing via @ref JsonObject::operator[] or
 * @ref JsonArray::at does still copy the subtree out of its parent,
 * because the parent is mutable through its handle.
 *
 * @par Thread Safety
 * Conditionally thread-safe.  Distinct instances may be used
 * concurrently; concurrent access to a single instance must be
 * externally synchronized.
 */
class JsonValue {
        public:
                /** @brief Discriminant for the value held. */
                enum Type {
                        Null,      ///< JSON null literal.
                        Bool,      ///< Boolean.
                        Double,    ///< Any JSON number (int or float).
                        String,    ///< String literal.
                        Array,     ///< JSON array.
                        Object,    ///< JSON object.
                        Undefined  ///< No value (e.g. missing key on lookup).
                };

                /** @brief Constructs a Null value. */
                JsonValue();
                /** @brief Constructs a Bool value. */
                JsonValue(bool b);
                /** @brief Constructs a Double value from a signed int. */
                JsonValue(int i);
                /** @brief Constructs a Double value from a signed 64-bit int. */
                JsonValue(int64_t i);
                /** @brief Constructs a Double value from an unsigned 64-bit int. */
                JsonValue(uint64_t i);
                /** @brief Constructs a Double value. */
                JsonValue(double d);
                /** @brief Constructs a String value. */
                JsonValue(const char *s);
                /** @brief Constructs a String value. */
                JsonValue(const promeki::String &s);
                /** @brief Constructs an Object value, sharing storage with @p obj. */
                inline JsonValue(const JsonObject &obj);
                /** @brief Constructs an Array value, sharing storage with @p arr. */
                inline JsonValue(const JsonArray &arr);

                /** @brief Returns an Undefined value (used to signal missing keys). */
                static JsonValue undefined();

                /** @brief Decodes a Variant tree into the equivalent JSON value. */
                static JsonValue fromVariant(const Variant &val);

                /** @brief Returns the discriminant for the held value. */
                Type type() const;

                /** @brief True iff the held value is the JSON null literal. */
                bool isNull() const { return _d->j.is_null(); }
                /** @brief True iff the held value is a JSON boolean. */
                bool isBool() const { return _d->j.is_boolean(); }
                /** @brief True iff the held value is any JSON number. */
                bool isDouble() const { return _d->j.is_number(); }
                /** @brief True iff the held value is a JSON string. */
                bool isString() const { return _d->j.is_string(); }
                /** @brief True iff the held value is a JSON array. */
                bool isArray() const { return _d->j.is_array(); }
                /** @brief True iff the held value is a JSON object. */
                bool isObject() const { return _d->j.is_object(); }
                /**
                 * @brief True iff the held value is the synthetic Undefined state.
                 *
                 * Returned by @ref JsonObject::operator[] and
                 * @ref JsonArray::at when the requested key/index is
                 * missing.  Distinct from @ref isNull (a JSON null value
                 * present in the tree).
                 */
                bool isUndefined() const { return _d->j.is_discarded(); }

                /** @brief Returns the boolean value, or @p def if not a Bool. */
                bool toBool(bool def = false) const;
                /** @brief Returns the integer value, or @p def if not a Double. */
                int64_t toInt(int64_t def = 0) const;
                /** @brief Returns the unsigned integer value, or @p def if not a Double. */
                uint64_t toUInt(uint64_t def = 0) const;
                /** @brief Returns the floating-point value, or @p def if not a Double. */
                double toDouble(double def = 0.0) const;
                /** @brief Returns the string value, or @p def if not a String. */
                promeki::String toString(const promeki::String &def = promeki::String()) const;
                /** @brief Returns the embedded JsonObject, or an empty object if not an Object. */
                inline JsonObject toObject() const;
                /** @brief Returns the embedded JsonArray, or an empty array if not an Array. */
                inline JsonArray toArray() const;
                /** @brief Returns a Variant tree mirroring this value (recursive). */
                Variant toVariant() const;

                /** @brief Serializes this value to a JSON string. */
                promeki::String toJsonString(unsigned int indent = 0) const;

                /** @brief Returns true if both values are structurally equal. */
                bool operator==(const JsonValue &other) const;
                /** @brief Returns true if the values differ. */
                bool operator!=(const JsonValue &other) const { return !(*this == other); }

        private:
                friend class JsonObject;
                friend class JsonArray;

                SharedPtr<JsonData> _d = SharedPtr<JsonData>::create();

                explicit JsonValue(nlohmann::json j) : _d(SharedPtr<JsonData>::create(std::move(j))) {}
                explicit JsonValue(const SharedPtr<JsonData> &d) : _d(d) {}

                template <typename T> static T extract(const nlohmann::json &val, bool *good);
};

/**
 * @brief JSON object container wrapping nlohmann::json.
 * @ingroup util
 *
 * Provides a type-safe interface for building and querying JSON objects.
 * Values can be accessed by key with typed getters that perform safe
 * conversions, or via @ref value / @ref operator[] which return a
 * @ref JsonValue.  Supports nesting via JsonObject and JsonArray values.
 *
 * @par Storage and copy semantics
 * JsonObject is a value type with internal copy-on-write sharing:
 * copying a JsonObject is O(1) and does not duplicate the underlying
 * nlohmann::json tree until one of the copies is mutated.  This
 * matters because JsonObject values are routinely returned by value
 * from descriptor @c toJson methods, threaded through HTTP / API
 * plumbing, and re-emitted by serializers — without CoW, every
 * pass-by-value would deep-copy the entire tree.  Mutating accessors
 * detach a private copy on first write.  Subtree accessors
 * (@ref getObject, @ref getArray) return fresh JsonObject / JsonArray
 * values whose trees are independent of the parent.
 *
 * @par Move-set / move-add overloads
 * The @ref set "set(key, JsonObject &&)" / @c JsonArray::add
 * overloads steal the underlying tree from a uniquely-owned rvalue,
 * eliminating the deep-copy cost in the dominant builder pattern
 * @c parent.set("k", child.toJson()).  Shared rvalues fall back to a
 * deep copy (same cost as the lvalue overloads).
 *
 * @par Thread Safety
 * Conditionally thread-safe.  Distinct instances may be used
 * concurrently; concurrent access to a single instance must be
 * externally synchronized.  The parse / toString static helpers
 * are reentrant.
 *
 * @par Example
 * @code
 * // Build a JSON object
 * JsonObject obj;
 * obj.set("name", "clip001");
 * obj.set("width", 1920);
 * obj.set("hdr", true);
 * String json = obj.toString(2);  // pretty-printed
 *
 * // Parse and query (Qt-style)
 * auto parsed = JsonObject::parse(json);
 * String name = parsed["name"].toString();
 * int w = static_cast<int>(parsed["width"].toInt());
 * @endcode
 */
class JsonObject {
        public:
                /**
                 * @brief Parses a JSON object from a string.
                 * @param str The JSON string to parse.
                 * @param err Optional error output; set to Error::Invalid on failure.
                 * @return The parsed JsonObject, or an empty object on failure.
                 */
                static JsonObject parse(const String &str, Error *err = nullptr) {
                        try {
                                nlohmann::json j = nlohmann::json::parse(str.str());
                                if (!j.is_object()) throw std::runtime_error("not an object");
                                if (err) *err = Error::Ok;
                                return JsonObject(std::move(j));
                        } catch (...) {
                                if (err) *err = Error::Invalid;
                                return JsonObject();
                        }
                }

                /**
                 * @brief Result-shaped wrapper around @ref parse.
                 *
                 * Mirrors the project-wide @c Result<T> @c fromString
                 * convention so the @ref DataType registry can
                 * auto-wire the inverse of @ref toString via
                 * @ref Detail::HasResultFromString.
                 */
                static Result<JsonObject> fromString(const String &str) {
                        Error      e;
                        JsonObject o = parse(str, &e);
                        if (e.isError()) return makeError<JsonObject>(e);
                        return makeResult(std::move(o));
                }

                /** @brief Constructs a JsonObject from a VariantMap. */
                static JsonObject fromVariantMap(const VariantMap &map);

                /** @brief Constructs an empty JSON object. */
                JsonObject() = default;

                /** @brief Returns the number of key-value pairs in the object. */
                int size() const { return static_cast<int>(_d->j.size()); }

                /** @brief Alias of @ref size, matching Qt. */
                int count() const { return size(); }

                /** @brief Returns true if the object has no key-value pairs. */
                bool isEmpty() const { return _d->j.empty(); }

                /** @brief Returns true if the object contains at least one key-value pair. */
                bool isValid() const { return !isEmpty(); }

                /** @brief Returns a sorted list of all keys present in the object. */
                List<String> keys() const;

                /**
                 * @brief Returns true if the value for the given key is null.
                 * @param key The key to check.
                 * @return true if the key exists and its value is null.
                 */
                bool valueIsNull(const String &key) const {
                        auto it = _d->j.find(key.str());
                        return it != _d->j.end() && it->is_null();
                }

                /**
                 * @brief Returns true if the value for the given key is a JSON object.
                 * @param key The key to check.
                 * @return true if the key exists and its value is an object.
                 */
                bool valueIsObject(const String &key) const {
                        auto it = _d->j.find(key.str());
                        return it != _d->j.end() && it->is_object();
                }

                /**
                 * @brief Returns true if the value for the given key is a JSON array.
                 * @param key The key to check.
                 * @return true if the key exists and its value is an array.
                 */
                bool valueIsArray(const String &key) const {
                        auto it = _d->j.find(key.str());
                        return it != _d->j.end() && it->is_array();
                }

                /**
                 * @brief Returns true if the object contains the given key.
                 * @param key The key to look up.
                 * @return true if the key exists in the object.
                 */
                bool contains(const String &key) const { return _d->j.contains(key.str()); }

                /**
                 * @brief Returns the JsonValue for @p key, or an Undefined value if absent.
                 *
                 * Qt-style accessor — prefer this when callers want to
                 * branch on type via @ref JsonValue::isDouble /
                 * @c isString etc., or when the missing-key case must
                 * be distinguished from a present null.
                 */
                JsonValue value(const String &key) const;

                /** @brief Alias of @ref value, matching Qt. */
                JsonValue operator[](const String &key) const { return value(key); }

                /**
                 * @brief Returns the boolean value for the given key.
                 * @param key The key to look up.
                 * @param err Optional error output; set to Error::Invalid if the key is missing or not convertible.
                 * @return The boolean value, or false on failure.
                 */
                bool getBool(const String &key, Error *err = nullptr) const { return get<bool>(key, err); }

                /**
                 * @brief Returns the signed 64-bit integer value for the given key.
                 * @param key The key to look up.
                 * @param err Optional error output.
                 * @return The integer value, or 0 on failure.
                 */
                int64_t getInt(const String &key, Error *err = nullptr) const { return get<int64_t>(key, err); }

                /**
                 * @brief Returns the unsigned 64-bit integer value for the given key.
                 * @param key The key to look up.
                 * @param err Optional error output.
                 * @return The unsigned integer value, or 0 on failure.
                 */
                uint64_t getUInt(const String &key, Error *err = nullptr) const { return get<uint64_t>(key, err); }

                /**
                 * @brief Returns the double-precision floating-point value for the given key.
                 * @param key The key to look up.
                 * @param err Optional error output.
                 * @return The double value, or 0.0 on failure.
                 */
                double getDouble(const String &key, Error *err = nullptr) const { return get<double>(key, err); }

                /**
                 * @brief Returns the string value for the given key.
                 * @param key The key to look up.
                 * @param err Optional error output.
                 * @return The string value, or an empty String on failure.
                 */
                String getString(const String &key, Error *err = nullptr) const {
                        return get<std::string>(key, err);
                }

                /**
                 * @brief Returns the nested JsonObject for the given key.
                 * @param key The key to look up.
                 * @param err Optional error output; set to Error::Invalid if the key is missing or not an object.
                 * @return The nested JsonObject, or an empty object on failure.
                 */
                JsonObject getObject(const String &key, Error *err = nullptr) const {
                        auto it = _d->j.find(key.str());
                        if (it == _d->j.end() || !it->is_object()) {
                                if (err) *err = Error::Invalid;
                                return JsonObject();
                        }
                        if (err) *err = Error::Ok;
                        return JsonObject(*it);
                }

                /**
                 * @brief Returns the nested JsonArray for the given key.
                 * @param key The key to look up.
                 * @param err Optional error output; set to Error::Invalid if the key is missing or not an array.
                 * @return The nested JsonArray, or an empty array on failure.
                 */
                inline JsonArray getArray(const String &key, Error *err = nullptr) const;

                /**
                 * @brief Serializes the object to a JSON string.
                 * @param indent Number of spaces per indentation level. Zero produces compact output.
                 * @return The serialized JSON string.
                 */
                String toString(unsigned int indent = 0) const {
                        if (indent == 0) return _d->j.dump();
                        return _d->j.dump(static_cast<int>(indent));
                }

                /** @brief Returns this object as a VariantMap (recursive). */
                VariantMap toVariantMap() const;

                /** @brief Removes all key-value pairs from the object. */
                void clear() { _d.modify()->j.clear(); }

                /**
                 * @brief Removes the entry for @p key, if present.
                 * @return true if the key was present and removed.
                 */
                bool remove(const String &key);

                /**
                 * @brief Removes the entry for @p key and returns the removed value.
                 * @return The removed value, or @ref JsonValue::undefined if absent.
                 */
                JsonValue take(const String &key);

                /**
                 * @brief Sets the value for the given key to null.
                 * @param key The key to set.
                 */
                void setNull(const String &key) { _d.modify()->j[key.str()] = nullptr; }

                /**
                 * @brief Sets a nested JsonObject value for the given key.
                 * @param key The key to set.
                 * @param val The JsonObject value (deep-copied).
                 */
                inline void set(const String &key, const JsonObject &val);

                /** @brief Move-overload of @ref set; steals the source tree when @p val is uniquely owned. */
                inline void set(const String &key, JsonObject &&val);

                /**
                 * @brief Sets a nested JsonArray value for the given key.
                 * @param key The key to set.
                 * @param val The JsonArray value (deep-copied).
                 */
                inline void set(const String &key, const JsonArray &val);

                /** @brief Move-overload of @ref set; steals the source tree when @p val is uniquely owned. */
                inline void set(const String &key, JsonArray &&val);

                /** @brief Sets a JsonValue for the given key. */
                void set(const String &key, const JsonValue &val);

                /** @brief Sets a boolean value for the given key. */
                void set(const String &key, bool val) { _d.modify()->j[key.str()] = val; }
                /** @brief Sets an int value for the given key. */
                void set(const String &key, int val) { _d.modify()->j[key.str()] = val; }
                /** @brief Sets an unsigned int value for the given key. */
                void set(const String &key, unsigned int val) { _d.modify()->j[key.str()] = val; }
                /** @brief Sets a signed 64-bit integer value for the given key. */
                void set(const String &key, int64_t val) { _d.modify()->j[key.str()] = val; }
                /** @brief Sets an unsigned 64-bit integer value for the given key. */
                void set(const String &key, uint64_t val) { _d.modify()->j[key.str()] = val; }
                /** @brief Sets a float value for the given key. */
                void set(const String &key, float val) { _d.modify()->j[key.str()] = val; }
                /** @brief Sets a double value for the given key. */
                void set(const String &key, double val) { _d.modify()->j[key.str()] = val; }
                /** @brief Sets a C-string value for the given key. */
                void set(const String &key, const char *val) {
                        _d.modify()->j[key.str()] = std::string(val);
                }
                /** @brief Sets a String value for the given key. */
                void set(const String &key, const String &val) {
                        _d.modify()->j[key.str()] = val.str();
                }
                /** @brief Sets a UUID value (stored as its string representation) for the given key. */
                void set(const String &key, const UUID &val);

                /**
                 * @brief Sets a value from a Variant, automatically selecting the JSON type.
                 * @param key The key to set.
                 * @param val The Variant whose value and type determine the stored JSON value.
                 */
                void setFromVariant(const String &key, const Variant &val);

                /**
                 * @brief Iterates over all key-value pairs in the object.
                 * @param func Callback invoked for each key-value pair.
                 */
                void forEach(Function<void(const String &key, const Variant &val)> func) const;

                /** @brief Returns true if both JSON objects have identical contents. */
                bool operator==(const JsonObject &other) const {
                        if (_d == other._d) return true;
                        return _d->j == other._d->j;
                }

                /** @brief Returns true if the objects differ. */
                bool operator!=(const JsonObject &other) const { return !(*this == other); }

        private:
                friend class JsonArray;
                friend class JsonValue;

                SharedPtr<JsonData> _d = SharedPtr<JsonData>::create(JsonData(nlohmann::json::object()));

                /** @brief Constructs from an existing nlohmann::json subtree. */
                explicit JsonObject(nlohmann::json j) : _d(SharedPtr<JsonData>::create(JsonData(std::move(j)))) {}

                /** @brief Constructs by sharing the given JsonData (used by JsonValue::toObject). */
                explicit JsonObject(const SharedPtr<JsonData> &d) : _d(d) {}

                template <typename T> T get(const String &key, Error *err = nullptr) const {
                        auto it = _d->j.find(key.str());
                        if (it == _d->j.end()) {
                                if (err) *err = Error::Invalid;
                                return T{};
                        }
                        bool good = false;
                        T    ret = JsonValue::extract<T>(*it, &good);
                        if (err) *err = good ? Error::Ok : Error::Invalid;
                        return ret;
                }
};

/**
 * @brief JSON array container wrapping nlohmann::json.
 *
 * Provides a type-safe interface for building and querying JSON arrays.
 * Elements are accessed by index with typed getters that perform safe
 * conversions, or via @ref at / @ref operator[] which return a
 * @ref JsonValue.  Supports nesting via JsonObject and JsonArray
 * elements.
 *
 * @par Storage and copy semantics
 * JsonArray is a value type with internal copy-on-write sharing,
 * matching @ref JsonObject.  Copies share storage until one side
 * mutates; subtree accessors return independent values.
 *
 * @par Iteration
 * Range-based for is supported, yielding @ref JsonValue per element:
 * @code
 * for (const JsonValue &v : arr) { ... }
 * @endcode
 */
class JsonArray {
        public:
                /**
                 * @brief Parses a JSON array from a string.
                 * @param str The JSON string to parse.
                 * @param err Optional error output; set to Error::Invalid on failure.
                 * @return The parsed JsonArray, or an empty array on failure.
                 */
                static JsonArray parse(const String &str, Error *err = nullptr) {
                        try {
                                nlohmann::json j = nlohmann::json::parse(str.str());
                                if (!j.is_array()) throw std::runtime_error("not an array");
                                if (err) *err = Error::Ok;
                                return JsonArray(std::move(j));
                        } catch (...) {
                                if (err) *err = Error::Invalid;
                                return JsonArray();
                        }
                }

                /**
                 * @brief Result-shaped wrapper around @ref parse.
                 *
                 * Mirrors the project-wide @c Result<T> @c fromString
                 * convention so the @ref DataType registry can
                 * auto-wire the inverse of @ref toString.
                 */
                static Result<JsonArray> fromString(const String &str) {
                        Error     e;
                        JsonArray a = parse(str, &e);
                        if (e.isError()) return makeError<JsonArray>(e);
                        return makeResult(std::move(a));
                }

                /** @brief Constructs a JsonArray from a VariantList. */
                static JsonArray fromVariantList(const VariantList &list);

                /** @brief Constructs an empty JSON array. */
                JsonArray() = default;

                /** @brief Returns the number of elements in the array. */
                int size() const { return static_cast<int>(_d->j.size()); }

                /** @brief Alias of @ref size, matching Qt. */
                int count() const { return size(); }

                /** @brief Returns true if the array has no elements. */
                bool isEmpty() const { return _d->j.empty(); }

                /** @brief Returns true if the array contains at least one element. */
                bool isValid() const { return !isEmpty(); }

                /**
                 * @brief Returns true if the element at the given index is null.
                 * @param index The zero-based element index.
                 */
                bool valueIsNull(int index) const {
                        return index >= 0 && index < size() && _d->j[index].is_null();
                }

                /**
                 * @brief Returns true if the element at the given index is a JSON object.
                 * @param index The zero-based element index.
                 */
                bool valueIsObject(int index) const {
                        return index >= 0 && index < size() && _d->j[index].is_object();
                }

                /**
                 * @brief Returns true if the element at the given index is a JSON array.
                 * @param index The zero-based element index.
                 */
                bool valueIsArray(int index) const {
                        return index >= 0 && index < size() && _d->j[index].is_array();
                }

                /**
                 * @brief Returns the JsonValue at @p index, or Undefined if out of bounds.
                 *
                 * Qt-style accessor.  Negative indices are also reported
                 * as Undefined.
                 */
                JsonValue at(int index) const;

                /** @brief Alias of @ref at, matching Qt. */
                JsonValue operator[](int index) const { return at(index); }

                /** @brief Returns the first element, or Undefined if the array is empty. */
                JsonValue first() const { return at(0); }

                /** @brief Returns the last element, or Undefined if the array is empty. */
                JsonValue last() const { return at(size() - 1); }

                /**
                 * @brief Returns the boolean value at the given index.
                 * @param index The zero-based element index.
                 * @param err Optional error output.
                 * @return The boolean value, or false on failure.
                 */
                bool getBool(int index, Error *err = nullptr) const { return get<bool>(index, err); }

                /**
                 * @brief Returns the signed 64-bit integer value at the given index.
                 * @param index The zero-based element index.
                 * @param err Optional error output.
                 * @return The integer value, or 0 on failure.
                 */
                int64_t getInt(int index, Error *err = nullptr) const { return get<int64_t>(index, err); }

                /**
                 * @brief Returns the unsigned 64-bit integer value at the given index.
                 * @param index The zero-based element index.
                 * @param err Optional error output.
                 * @return The unsigned integer value, or 0 on failure.
                 */
                uint64_t getUInt(int index, Error *err = nullptr) const { return get<uint64_t>(index, err); }

                /**
                 * @brief Returns the double-precision floating-point value at the given index.
                 * @param index The zero-based element index.
                 * @param err Optional error output.
                 * @return The double value, or 0.0 on failure.
                 */
                double getDouble(int index, Error *err = nullptr) const { return get<double>(index, err); }

                /**
                 * @brief Returns the string value at the given index.
                 * @param index The zero-based element index.
                 * @param err Optional error output.
                 * @return The string value, or an empty String on failure.
                 */
                String getString(int index, Error *err = nullptr) const { return get<std::string>(index, err); }

                /**
                 * @brief Returns the nested JsonObject at the given index.
                 * @param index The zero-based element index.
                 * @param err Optional error output.
                 * @return The nested JsonObject, or an empty object on failure.
                 */
                JsonObject getObject(int index, Error *err = nullptr) const {
                        if (index < 0 || index >= size() || !_d->j[index].is_object()) {
                                if (err) *err = Error::Invalid;
                                return JsonObject();
                        }
                        if (err) *err = Error::Ok;
                        return JsonObject(_d->j[index]);
                }

                /**
                 * @brief Returns the nested JsonArray at the given index.
                 * @param index The zero-based element index.
                 * @param err Optional error output.
                 * @return The nested JsonArray, or an empty array on failure.
                 */
                JsonArray getArray(int index, Error *err = nullptr) const {
                        if (index < 0 || index >= size() || !_d->j[index].is_array()) {
                                if (err) *err = Error::Invalid;
                                return JsonArray();
                        }
                        if (err) *err = Error::Ok;
                        return JsonArray(_d->j[index]);
                }

                /**
                 * @brief Serializes the array to a JSON string.
                 * @param indent Number of spaces per indentation level. Zero produces compact output.
                 * @return The serialized JSON string.
                 */
                String toString(unsigned int indent = 0) const {
                        if (indent == 0) return _d->j.dump();
                        return _d->j.dump(static_cast<int>(indent));
                }

                /** @brief Returns this array as a VariantList (recursive). */
                VariantList toVariantList() const;

                /** @brief Removes all elements from the array. */
                void clear() { _d.modify()->j.clear(); }

                /**
                 * @brief Removes the element at @p index, if in range.
                 * @return true if the index was in range and removed.
                 */
                bool removeAt(int index);

                /**
                 * @brief Removes the element at @p index and returns it.
                 * @return The removed value, or @ref JsonValue::undefined if out of range.
                 */
                JsonValue takeAt(int index);

                /** @brief Appends a null value to the array. */
                void addNull() { _d.modify()->j.push_back(nullptr); }
                /** @brief Appends a JsonObject to the array (deep-copied). */
                inline void add(const JsonObject &val);
                /** @brief Move-overload of @ref add; steals the source tree when @p val is uniquely owned. */
                inline void add(JsonObject &&val);
                /** @brief Appends a JsonArray to the array (deep-copied). */
                inline void add(const JsonArray &val);
                /** @brief Move-overload of @ref add; steals the source tree when @p val is uniquely owned. */
                inline void add(JsonArray &&val);
                /** @brief Appends a JsonValue to the array. */
                void add(const JsonValue &val);
                /** @brief Appends a boolean value to the array. */
                void add(bool val) { _d.modify()->j.push_back(val); }
                /** @brief Appends an int value to the array. */
                void add(int val) { _d.modify()->j.push_back(val); }
                /** @brief Appends an unsigned int value to the array. */
                void add(unsigned int val) { _d.modify()->j.push_back(val); }
                /** @brief Appends a signed 64-bit integer to the array. */
                void add(int64_t val) { _d.modify()->j.push_back(val); }
                /** @brief Appends an unsigned 64-bit integer to the array. */
                void add(uint64_t val) { _d.modify()->j.push_back(val); }
                /** @brief Appends a float value to the array. */
                void add(float val) { _d.modify()->j.push_back(val); }
                /** @brief Appends a double value to the array. */
                void add(double val) { _d.modify()->j.push_back(val); }
                /** @brief Appends a C-string value to the array. */
                void add(const char *val) { _d.modify()->j.push_back(std::string(val)); }
                /** @brief Appends a String value to the array. */
                void add(const String &val) { _d.modify()->j.push_back(val.str()); }
                /** @brief Appends a UUID (stored as its string representation) to the array. */
                void add(const UUID &val);

                /**
                 * @brief Appends a value from a Variant, automatically selecting the JSON type.
                 * @param val The Variant whose value and type determine the appended JSON element.
                 */
                void addFromVariant(const Variant &val);

                /** @brief Alias of @ref add, matching Qt. */
                template <typename T> void append(T &&val) { add(std::forward<T>(val)); }

                /**
                 * @brief Inserts a JsonValue before the given index.
                 *
                 * Out-of-range indices are clamped to @c [0, size()].
                 * @ref JsonValue::Undefined values are inserted as JSON
                 * null.
                 */
                void insert(int index, const JsonValue &val);

                /** @brief Prepends a JsonValue. */
                void prepend(const JsonValue &val) { insert(0, val); }

                /**
                 * @brief Iterates over all elements in the array.
                 * @param func Callback invoked for each element.
                 */
                void forEach(Function<void(const Variant &val)> func) const;

                /** @brief Returns true if both JSON arrays have identical contents. */
                bool operator==(const JsonArray &other) const {
                        if (_d == other._d) return true;
                        return _d->j == other._d->j;
                }

                /** @brief Returns true if the arrays differ. */
                bool operator!=(const JsonArray &other) const { return !(*this == other); }

                /** @brief Range-for iterator yielding JsonValue per element. */
                class const_iterator {
                        public:
                                using iterator_category = std::forward_iterator_tag;
                                using value_type = JsonValue;
                                using difference_type = std::ptrdiff_t;
                                using pointer = void;
                                using reference = JsonValue;

                                const_iterator() = default;

                                JsonValue operator*() const;
                                const_iterator &operator++() {
                                        ++_index;
                                        return *this;
                                }
                                const_iterator operator++(int) {
                                        const_iterator tmp = *this;
                                        ++_index;
                                        return tmp;
                                }
                                bool operator==(const const_iterator &other) const {
                                        return _arr == other._arr && _index == other._index;
                                }
                                bool operator!=(const const_iterator &other) const { return !(*this == other); }

                        private:
                                friend class JsonArray;
                                const_iterator(const JsonArray *arr, int index) : _arr(arr), _index(index) {}

                                const JsonArray *_arr = nullptr;
                                int              _index = 0;
                };

                /** @brief Returns an iterator to the first element. */
                const_iterator begin() const { return const_iterator(this, 0); }
                /** @brief Returns an iterator past the last element. */
                const_iterator end() const { return const_iterator(this, size()); }
                /// @copydoc begin
                const_iterator cbegin() const { return begin(); }
                /// @copydoc end
                const_iterator cend() const { return end(); }

        private:
                friend class JsonObject;
                friend class JsonValue;

                SharedPtr<JsonData> _d = SharedPtr<JsonData>::create(JsonData(nlohmann::json::array()));

                /** @brief Constructs from an existing nlohmann::json subtree. */
                explicit JsonArray(nlohmann::json j) : _d(SharedPtr<JsonData>::create(JsonData(std::move(j)))) {}

                /** @brief Constructs by sharing the given JsonData (used by JsonValue::toArray). */
                explicit JsonArray(const SharedPtr<JsonData> &d) : _d(d) {}

                template <typename T> T get(int index, Error *err = nullptr) const {
                        if (index < 0 || index >= size()) {
                                if (err) *err = Error::Invalid;
                                return T{};
                        }
                        bool good = false;
                        T    ret = JsonValue::extract<T>(_d->j[index], &good);
                        if (err) *err = good ? Error::Ok : Error::Invalid;
                        return ret;
                }
};

// ============================================================================
// Inline definitions that depend on multiple types being complete
// ============================================================================

inline JsonValue::JsonValue(const JsonObject &obj) : _d(obj._d) {}
inline JsonValue::JsonValue(const JsonArray &arr) : _d(arr._d) {}

template <typename T> T JsonValue::extract(const nlohmann::json &val, bool *good) {
        T    ret{};
        bool ok = false;
        try {
                if constexpr (std::is_same_v<T, bool>) {
                        if (val.is_boolean()) {
                                ret = val.get<bool>();
                                ok = true;
                        } else if (val.is_number_integer()) {
                                ret = val.get<int64_t>() != 0;
                                ok = true;
                        }
                } else if constexpr (std::is_integral_v<T>) {
                        if (val.is_number()) {
                                ret = static_cast<T>(val.get<int64_t>());
                                ok = true;
                        } else if (val.is_boolean()) {
                                ret = val.get<bool>() ? 1 : 0;
                                ok = true;
                        }
                } else if constexpr (std::is_floating_point_v<T>) {
                        if (val.is_number()) {
                                ret = val.get<T>();
                                ok = true;
                        }
                } else if constexpr (std::is_same_v<T, std::string>) {
                        if (val.is_string()) {
                                ret = val.get<std::string>();
                                ok = true;
                        } else if (!val.is_null() && !val.is_discarded()) {
                                ret = val.dump();
                                ok = true;
                        }
                }
        } catch (...) {}
        if (good) *good = ok;
        return ret;
}

inline JsonObject JsonValue::toObject() const {
        if (!_d->j.is_object()) return JsonObject();
        return JsonObject(_d);
}

inline JsonArray JsonValue::toArray() const {
        if (!_d->j.is_array()) return JsonArray();
        return JsonArray(_d);
}

inline JsonArray JsonObject::getArray(const String &key, Error *err) const {
        auto it = _d->j.find(key.str());
        if (it == _d->j.end() || !it->is_array()) {
                if (err) *err = Error::Invalid;
                return JsonArray();
        }
        if (err) *err = Error::Ok;
        return JsonArray(*it);
}

inline void JsonObject::set(const String &key, const JsonObject &val) {
        _d.modify()->j[key.str()] = val._d->j;
}

inline void JsonObject::set(const String &key, JsonObject &&val) {
        // If the source is uniquely owned, modify() returns the data
        // without detaching and we can safely move-out the inner json.
        // Otherwise modify() does a deep-copy detach, then we move from
        // that fresh copy — same total cost as the lvalue overload.
        _d.modify()->j[key.str()] = std::move(val._d.modify()->j);
}

inline void JsonObject::set(const String &key, const JsonArray &val) {
        _d.modify()->j[key.str()] = val._d->j;
}

inline void JsonObject::set(const String &key, JsonArray &&val) {
        _d.modify()->j[key.str()] = std::move(val._d.modify()->j);
}

inline void JsonArray::add(const JsonObject &val) { _d.modify()->j.push_back(val._d->j); }

inline void JsonArray::add(JsonObject &&val) { _d.modify()->j.push_back(std::move(val._d.modify()->j)); }

inline void JsonArray::add(const JsonArray &val) { _d.modify()->j.push_back(val._d->j); }

inline void JsonArray::add(JsonArray &&val) { _d.modify()->j.push_back(std::move(val._d.modify()->j)); }

inline JsonValue JsonArray::const_iterator::operator*() const { return _arr->at(_index); }

// The DataStream <<, >> operators for JsonObject / JsonArray live in
// datastream.h's bottom half (after json.h is pulled in) so this
// header can stay free of any datastream.h dependency — that's what
// lets datastream.h's @ref Detail::makeDefaultOps populate the
// @c toJson / @c fromJson Ops slots without a circular include.

PROMEKI_NAMESPACE_END

PROMEKI_FORMAT_VIA_TOSTRING(promeki::JsonObject);
PROMEKI_FORMAT_VIA_TOSTRING(promeki::JsonArray);

#endif // PROMEKI_ENABLE_CORE
