/**
 * @file      core/metadata.h
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/core/namespace.h>
#include <promeki/core/variant.h>
#include <promeki/core/map.h>
#include <promeki/core/util.h>
#include <promeki/core/json.h>
#include <promeki/core/sharedptr.h>

#define PROMEKI_ENUM_METADATA_ID \
        X(Invalid, std::monostate) \
        X(Timecode, class Timecode) \
        X(Gamma, double) \
        X(Title, String) \
        X(Copyright, String) \
        X(Software, String) \
        X(Artist, String) \
        X(Comment, String) \
        X(Date, String) \
        X(Album, String) \
        X(License, String) \
        X(TrackNumber, int) \
        X(Genre, String) \
        X(EnableBWF, bool) \
        X(Description, String) \
        X(Originator, String) \
        X(OriginatorReference, String) \
        X(OriginationDateTime, String) \
        X(FrameRate, Rational<int>) \
        X(UMID, String) \
        X(CodingHistory, String) \
        X(CompressionLevel, double) \
        X(EnableVBR, bool) \
        X(VBRQuality, double) \
        X(CompressedSize, int)

PROMEKI_NAMESPACE_BEGIN

class StringList;

/**
 * @brief Key-value metadata container using typed Variant values.
 * @ingroup core_util
 *
 * Stores metadata entries keyed by a well-known ID enum. Each value
 * is stored as a Variant, supporting types such as String, int,
 * double, bool, Timecode, and Rational. Supports JSON serialization
 *
 * @par Example
 * @code
 * Metadata meta;
 * meta.set(Metadata::Title, String("My Video"));
 * meta.set(Metadata::FrameRate, Rational<int>(24, 1));
 *
 * String title = meta.value(Metadata::Title).get<String>();
 * bool has = meta.isSet(Metadata::Copyright);  // false
 * @endcode
 * and deserialization. When shared ownership is needed, use Metadata::Ptr.
 */
class Metadata {
        PROMEKI_SHARED_FINAL(Metadata)
        public:
                /** @brief Shared pointer type for Metadata. */
                using Ptr = SharedPtr<Metadata>;

                /** @brief Enumeration of well-known metadata keys. */
                #define X(name, type) name,
                enum ID { PROMEKI_ENUM_METADATA_ID };
                #undef X

                /**
                 * @brief Converts a metadata ID to its string name.
                 * @param id The metadata ID.
                 * @return A const reference to the name string.
                 */
                static const String &idToString(ID id);

                /**
                 * @brief Converts a string name to a metadata ID.
                 * @param val The name string.
                 * @return The corresponding ID, or Invalid if not found.
                 */
                static ID stringToID(const String &val);

                /**
                 * @brief Deserializes a Metadata object from a JSON object.
                 * @param json The source JSON object.
                 * @param err  Optional error output.
                 * @return The deserialized Metadata.
                 */
                static Metadata fromJson(const JsonObject &json, Error *err = nullptr);

                /** @brief Constructs an empty Metadata object. */
                Metadata() = default;

                /**
                 * @brief Sets a metadata value for the given ID.
                 * @tparam T The value type.
                 * @param id    The metadata key.
                 * @param value The value to store.
                 */
                template <typename T> void set(ID id, const T &value) {
                        _map[id] = Variant(value);
                        return;
                }

                /**
                 * @brief Returns the Variant value for the given ID.
                 * @param id The metadata key.
                 * @return A const reference to the Variant value.
                 */
                const Variant &get(ID id) const { return _map[id]; }

                /**
                 * @brief Returns true if the given ID has been set.
                 * @param id The metadata key.
                 * @return true if the key exists.
                 */
                bool contains(ID id) const { return _map.contains(id); }

                /**
                 * @brief Removes the entry for the given ID.
                 * @param id The metadata key to remove.
                 */
                void remove(ID id) { _map.remove(id); return; }

                /** @brief Removes all metadata entries. */
                void clear() { _map.clear(); return; }

                /**
                 * @brief Returns the number of metadata entries.
                 * @return The entry count.
                 */
                size_t size() const { return _map.size(); }

                /**
                 * @brief Returns true if no metadata entries are stored.
                 * @return true if empty.
                 */
                bool isEmpty() const { return _map.size() == 0; }

                /**
                 * @brief Iterates over all metadata entries, invoking a callback for each.
                 * @tparam Func Callable type with signature void(ID, const Variant &).
                 * @param func The callback to invoke for each entry.
                 */
                template <typename Func> void forEach(Func &&func) const {
                        for(const auto &[id, value] : _map) {
                                func(id, value);
                        }
                        return;
                }

                /**
                 * @brief Returns a human-readable dump of all metadata entries.
                 * @return A StringList with one entry per metadata key-value pair.
                 */
                StringList dump() const;

                /**
                 * @brief Returns true if both Metadata objects contain the same entries.
                 * @param other The Metadata to compare against.
                 * @return true if equal.
                 */
                bool operator==(const Metadata &other) const {
                        if(_map.size() != other._map.size()) return false;
                        return toJson() == other.toJson();
                }

                /**
                 * @brief Serializes this Metadata to a JSON object.
                 * @return A JsonObject containing all metadata entries.
                 */
                JsonObject toJson() const {
                    JsonObject ret;
                    for(const auto &[id, value] : _map) {
                        ret.setFromVariant(idToString(id), value);
                    }
                    return ret;
                }

        private:
                Map<ID, Variant> _map;
};

PROMEKI_NAMESPACE_END
