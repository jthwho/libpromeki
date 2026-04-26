/**
 * @file      json.h
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information
 */

#pragma once

#include <functional>
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/variant_fwd.h>
#include <promeki/sharedptr.h>
#include <promeki/datastream.h>
#include <nlohmann/json.hpp>

PROMEKI_NAMESPACE_BEGIN

class JsonArray;

/**
 * @brief JSON object container wrapping nlohmann::json.
 * @ingroup util
 *
 * Provides a type-safe interface for building and querying JSON objects.
 * Values can be accessed by key with typed getters that perform safe
 * conversions.  Supports nesting via JsonObject and JsonArray values.
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
 * // Parse and query
 * auto parsed = JsonObject::parse(json);
 * String name = parsed.getString("name");
 * int w = parsed.getInt("width");
 * @endcode
 */
class JsonObject {
                PROMEKI_SHARED_FINAL(JsonObject)
        public:
                /** @brief Shared pointer type for JsonObject. */
                using Ptr = SharedPtr<JsonObject>;

                /**
         * @brief Parses a JSON object from a string.
         * @param str The JSON string to parse.
         * @param err Optional error output; set to Error::Invalid on failure.
         * @return The parsed JsonObject, or an empty object on failure.
         */
                static JsonObject parse(const String &str, Error *err = nullptr) {
                        JsonObject ret;
                        try {
                                ret._j = nlohmann::json::parse(str.str());
                                if (!ret._j.is_object()) throw std::runtime_error("not an object");
                                if (err) *err = Error::Ok;
                        } catch (...) {
                                ret._j = nlohmann::json::object();
                                if (err) *err = Error::Invalid;
                        }
                        return ret;
                }

                /** @brief Constructs an empty JSON object. */
                JsonObject() : _j(nlohmann::json::object()) {}

                /** @brief Returns the number of key-value pairs in the object. */
                int size() const { return static_cast<int>(_j.size()); }

                /** @brief Returns true if the object contains at least one key-value pair. */
                bool isValid() const { return size() > 0; }

                /**
         * @brief Returns true if the value for the given key is null.
         * @param key The key to check.
         * @return true if the key exists and its value is null.
         */
                bool valueIsNull(const String &key) const {
                        auto it = _j.find(key.str());
                        return it != _j.end() && it->is_null();
                }

                /**
         * @brief Returns true if the value for the given key is a JSON object.
         * @param key The key to check.
         * @return true if the key exists and its value is an object.
         */
                bool valueIsObject(const String &key) const {
                        auto it = _j.find(key.str());
                        return it != _j.end() && it->is_object();
                }

                /**
         * @brief Returns true if the value for the given key is a JSON array.
         * @param key The key to check.
         * @return true if the key exists and its value is an array.
         */
                bool valueIsArray(const String &key) const {
                        auto it = _j.find(key.str());
                        return it != _j.end() && it->is_array();
                }

                /**
         * @brief Returns true if the object contains the given key.
         * @param key The key to look up.
         * @return true if the key exists in the object.
         */
                bool contains(const String &key) const { return _j.contains(key.str()); }

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
                String getString(const String &key, Error *err = nullptr) const { return get<std::string>(key, err); }

                /**
         * @brief Returns the nested JsonObject for the given key.
         * @param key The key to look up.
         * @param err Optional error output; set to Error::Invalid if the key is missing or not an object.
         * @return The nested JsonObject, or an empty object on failure.
         */
                JsonObject getObject(const String &key, Error *err = nullptr) const {
                        auto it = _j.find(key.str());
                        if (it == _j.end() || !it->is_object()) {
                                if (err) *err = Error::Invalid;
                                return JsonObject();
                        }
                        if (err) *err = Error::Ok;
                        JsonObject ret;
                        ret._j = *it;
                        return ret;
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
                        if (indent == 0) return _j.dump();
                        return _j.dump(static_cast<int>(indent));
                }

                /** @brief Removes all key-value pairs from the object. */
                void clear() { _j.clear(); }

                /**
         * @brief Sets the value for the given key to null.
         * @param key The key to set.
         */
                void setNull(const String &key) { _j[key.str()] = nullptr; }

                /**
         * @brief Sets a nested JsonObject value for the given key.
         * @param key The key to set.
         * @param val The JsonObject value.
         */
                void set(const String &key, const JsonObject &val) { _j[key.str()] = val._j; }

                /**
         * @brief Sets a nested JsonArray value for the given key.
         * @param key The key to set.
         * @param val The JsonArray value.
         */
                inline void set(const String &key, const JsonArray &val);

                /** @brief Sets a boolean value for the given key. */
                void set(const String &key, bool val) { _j[key.str()] = val; }
                /** @brief Sets an int value for the given key. */
                void set(const String &key, int val) { _j[key.str()] = val; }
                /** @brief Sets an unsigned int value for the given key. */
                void set(const String &key, unsigned int val) { _j[key.str()] = val; }
                /** @brief Sets a signed 64-bit integer value for the given key. */
                void set(const String &key, int64_t val) { _j[key.str()] = val; }
                /** @brief Sets an unsigned 64-bit integer value for the given key. */
                void set(const String &key, uint64_t val) { _j[key.str()] = val; }
                /** @brief Sets a float value for the given key. */
                void set(const String &key, float val) { _j[key.str()] = val; }
                /** @brief Sets a double value for the given key. */
                void set(const String &key, double val) { _j[key.str()] = val; }
                /** @brief Sets a C-string value for the given key. */
                void set(const String &key, const char *val) { _j[key.str()] = std::string(val); }
                /** @brief Sets a String value for the given key. */
                void set(const String &key, const String &val) { _j[key.str()] = val.str(); }
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
                void forEach(std::function<void(const String &key, const Variant &val)> func) const;

                /** @brief Returns true if both JSON objects have identical contents. */
                bool operator==(const JsonObject &other) const { return _j == other._j; }

        private:
                friend class JsonArray;

                nlohmann::json _j;

                template <typename T> T get(const String &key, Error *err = nullptr) const {
                        auto it = _j.find(key.str());
                        if (it == _j.end()) {
                                if (err) *err = Error::Invalid;
                                return T{};
                        }
                        return getVal<T>(*it, err);
                }

                template <typename T> static T getVal(const nlohmann::json &val, Error *err) {
                        T    ret{};
                        bool good = false;
                        try {
                                if constexpr (std::is_same_v<T, bool>) {
                                        if (val.is_boolean()) {
                                                ret = val.get<bool>();
                                                good = true;
                                        } else if (val.is_number_integer()) {
                                                ret = val.get<int64_t>() != 0;
                                                good = true;
                                        }
                                } else if constexpr (std::is_integral_v<T>) {
                                        if (val.is_number()) {
                                                ret = static_cast<T>(val.get<int64_t>());
                                                good = true;
                                        } else if (val.is_boolean()) {
                                                ret = val.get<bool>() ? 1 : 0;
                                                good = true;
                                        }
                                } else if constexpr (std::is_floating_point_v<T>) {
                                        if (val.is_number()) {
                                                ret = val.get<T>();
                                                good = true;
                                        }
                                } else if constexpr (std::is_same_v<T, std::string>) {
                                        if (val.is_string()) {
                                                ret = val.get<std::string>();
                                                good = true;
                                        } else if (!val.is_null()) {
                                                ret = val.dump();
                                                good = true;
                                        }
                                }
                        } catch (...) {}
                        if (err) *err = good ? Error::Ok : Error::Invalid;
                        return ret;
                }
};

/**
 * @brief JSON array container wrapping nlohmann::json.
 *
 * Provides a type-safe interface for building and querying JSON arrays.
 * Elements are accessed by index with typed getters that perform safe
 * conversions.  Supports nesting via JsonObject and JsonArray elements.
 */
class JsonArray {
                PROMEKI_SHARED_FINAL(JsonArray)
        public:
                /** @brief Shared pointer type for JsonArray. */
                using Ptr = SharedPtr<JsonArray>;

                /**
         * @brief Parses a JSON array from a string.
         * @param str The JSON string to parse.
         * @param err Optional error output; set to Error::Invalid on failure.
         * @return The parsed JsonArray, or an empty array on failure.
         */
                static JsonArray parse(const String &str, Error *err = nullptr) {
                        JsonArray ret;
                        try {
                                ret._j = nlohmann::json::parse(str.str());
                                if (!ret._j.is_array()) throw std::runtime_error("not an array");
                                if (err) *err = Error::Ok;
                        } catch (...) {
                                ret._j = nlohmann::json::array();
                                if (err) *err = Error::Invalid;
                        }
                        return ret;
                }

                /** @brief Constructs an empty JSON array. */
                JsonArray() : _j(nlohmann::json::array()) {}

                /** @brief Returns the number of elements in the array. */
                int size() const { return static_cast<int>(_j.size()); }

                /** @brief Returns true if the array contains at least one element. */
                bool isValid() const { return size() > 0; }

                /**
         * @brief Returns true if the element at the given index is null.
         * @param index The zero-based element index.
         */
                bool valueIsNull(int index) const { return index >= 0 && index < size() && _j[index].is_null(); }

                /**
         * @brief Returns true if the element at the given index is a JSON object.
         * @param index The zero-based element index.
         */
                bool valueIsObject(int index) const { return index >= 0 && index < size() && _j[index].is_object(); }

                /**
         * @brief Returns true if the element at the given index is a JSON array.
         * @param index The zero-based element index.
         */
                bool valueIsArray(int index) const { return index >= 0 && index < size() && _j[index].is_array(); }

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
                        if (index < 0 || index >= size() || !_j[index].is_object()) {
                                if (err) *err = Error::Invalid;
                                return JsonObject();
                        }
                        if (err) *err = Error::Ok;
                        JsonObject ret;
                        ret._j = _j[index];
                        return ret;
                }

                /**
         * @brief Returns the nested JsonArray at the given index.
         * @param index The zero-based element index.
         * @param err Optional error output.
         * @return The nested JsonArray, or an empty array on failure.
         */
                JsonArray getArray(int index, Error *err = nullptr) const {
                        if (index < 0 || index >= size() || !_j[index].is_array()) {
                                if (err) *err = Error::Invalid;
                                return JsonArray();
                        }
                        if (err) *err = Error::Ok;
                        JsonArray ret;
                        ret._j = _j[index];
                        return ret;
                }

                /**
         * @brief Serializes the array to a JSON string.
         * @param indent Number of spaces per indentation level. Zero produces compact output.
         * @return The serialized JSON string.
         */
                String toString(unsigned int indent = 0) const {
                        if (indent == 0) return _j.dump();
                        return _j.dump(static_cast<int>(indent));
                }

                /** @brief Removes all elements from the array. */
                void clear() { _j.clear(); }

                /** @brief Appends a null value to the array. */
                void addNull() { _j.push_back(nullptr); }
                /** @brief Appends a JsonObject to the array. */
                void add(const JsonObject &val) { _j.push_back(val._j); }
                /** @brief Appends a JsonArray to the array. */
                void add(const JsonArray &val) { _j.push_back(val._j); }
                /** @brief Appends a boolean value to the array. */
                void add(bool val) { _j.push_back(val); }
                /** @brief Appends an int value to the array. */
                void add(int val) { _j.push_back(val); }
                /** @brief Appends an unsigned int value to the array. */
                void add(unsigned int val) { _j.push_back(val); }
                /** @brief Appends a signed 64-bit integer to the array. */
                void add(int64_t val) { _j.push_back(val); }
                /** @brief Appends an unsigned 64-bit integer to the array. */
                void add(uint64_t val) { _j.push_back(val); }
                /** @brief Appends a float value to the array. */
                void add(float val) { _j.push_back(val); }
                /** @brief Appends a double value to the array. */
                void add(double val) { _j.push_back(val); }
                /** @brief Appends a C-string value to the array. */
                void add(const char *val) { _j.push_back(std::string(val)); }
                /** @brief Appends a String value to the array. */
                void add(const String &val) { _j.push_back(val.str()); }
                /** @brief Appends a UUID (stored as its string representation) to the array. */
                void add(const UUID &val);

                /**
         * @brief Appends a value from a Variant, automatically selecting the JSON type.
         * @param val The Variant whose value and type determine the appended JSON element.
         */
                void addFromVariant(const Variant &val);

                /**
         * @brief Iterates over all elements in the array.
         * @param func Callback invoked for each element.
         */
                void forEach(std::function<void(const Variant &val)> func) const;

        private:
                friend class JsonObject;

                nlohmann::json _j;

                template <typename T> T get(int index, Error *err = nullptr) const {
                        if (index < 0 || index >= size()) {
                                if (err) *err = Error::Invalid;
                                return T{};
                        }
                        return JsonObject::getVal<T>(_j[index], err);
                }
};

// Inline definitions that depend on JsonArray being complete
inline JsonArray JsonObject::getArray(const String &key, Error *err) const {
        auto it = _j.find(key.str());
        if (it == _j.end() || !it->is_array()) {
                if (err) *err = Error::Invalid;
                return JsonArray();
        }
        if (err) *err = Error::Ok;
        JsonArray ret;
        ret._j = *it;
        return ret;
}

inline void JsonObject::set(const String &key, const JsonArray &val) {
        _j[key.str()] = val._j;
}

// ============================================================================
// DataStream serialization
// ============================================================================
//
// JsonObject and JsonArray are serialized as their compact JSON text form.
// This keeps the wire format stable across nlohmann::json upgrades and makes
// cross-reading with json text-based tools trivial. Readers that see a
// truncated or malformed payload are marked as ReadCorruptData.

/**
 * @brief Writes a JsonObject as a tagged, length-prefixed JSON string.
 * @param stream The DataStream to write to.
 * @param obj    The JsonObject to serialize.
 * @return The stream, for chaining.
 */
inline DataStream &operator<<(DataStream &stream, const JsonObject &obj) {
        stream.writeTag(DataStream::TypeJsonObject);
        stream << obj.toString(0);
        return stream;
}

/**
 * @brief Reads a JsonObject from a tagged, length-prefixed JSON string.
 * @param stream The DataStream to read from.
 * @param obj    The JsonObject to populate.
 * @return The stream, for chaining.
 */
inline DataStream &operator>>(DataStream &stream, JsonObject &obj) {
        if (!stream.readTag(DataStream::TypeJsonObject)) {
                obj = JsonObject();
                return stream;
        }
        String text;
        stream >> text;
        if (stream.status() != DataStream::Ok) {
                obj = JsonObject();
                return stream;
        }
        Error err;
        obj = JsonObject::parse(text, &err);
        if (err.isError()) {
                stream.setError(DataStream::ReadCorruptData, String("JsonObject::parse failed"));
        }
        return stream;
}

/**
 * @brief Writes a JsonArray as a tagged, length-prefixed JSON string.
 */
inline DataStream &operator<<(DataStream &stream, const JsonArray &arr) {
        stream.writeTag(DataStream::TypeJsonArray);
        stream << arr.toString(0);
        return stream;
}

/**
 * @brief Reads a JsonArray from a tagged, length-prefixed JSON string.
 */
inline DataStream &operator>>(DataStream &stream, JsonArray &arr) {
        if (!stream.readTag(DataStream::TypeJsonArray)) {
                arr = JsonArray();
                return stream;
        }
        String text;
        stream >> text;
        if (stream.status() != DataStream::Ok) {
                arr = JsonArray();
                return stream;
        }
        Error err;
        arr = JsonArray::parse(text, &err);
        if (err.isError()) {
                stream.setError(DataStream::ReadCorruptData, String("JsonArray::parse failed"));
        }
        return stream;
}

PROMEKI_NAMESPACE_END

PROMEKI_FORMAT_VIA_TOSTRING(promeki::JsonObject);
PROMEKI_FORMAT_VIA_TOSTRING(promeki::JsonArray);
