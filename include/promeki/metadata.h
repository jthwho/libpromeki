/**
 * @file      metadata.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/variantdatabase.h>
#include <promeki/sharedptr.h>

PROMEKI_NAMESPACE_BEGIN

class StringList;

/// @brief Tag type that scopes the StringRegistry for Metadata IDs.
struct MetadataTag {};

/**
 * @brief Key-value metadata container using typed Variant values.
 * @ingroup util
 *
 * Stores metadata entries keyed by well-known string-registered IDs.
 * Each value is stored as a Variant, supporting types such as String,
 * int, double, bool, Timecode, and Rational. Supports JSON
 * serialization and deserialization. When shared ownership is needed,
 * use Metadata::Ptr.
 *
 * @par Example
 * @code
 * Metadata meta;
 * meta.set(Metadata::Title, String("My Video"));
 * meta.set(Metadata::FrameRate, Rational<int>(24, 1));
 *
 * String title = meta.get(Metadata::Title).get<String>();
 * bool has = meta.contains(Metadata::Copyright);  // false
 * @endcode
 */
class Metadata : public VariantDatabase<MetadataTag> {
        PROMEKI_SHARED_FINAL(Metadata)
        public:
                /** @brief Shared pointer type for Metadata. */
                using Ptr = SharedPtr<Metadata>;

                /** @brief Base class alias. */
                using Base = VariantDatabase<MetadataTag>;

                using Base::Base;

                /// @brief SMPTE timecode associated with this media unit.
                static inline const ID Timecode{"Timecode"};
                /// @brief Gamma / transfer-function exponent.
                static inline const ID Gamma{"Gamma"};
                /// @brief Title of the media.
                static inline const ID Title{"Title"};
                /// @brief Copyright notice.
                static inline const ID Copyright{"Copyright"};
                /// @brief Software that created or last modified the media.
                static inline const ID Software{"Software"};
                /// @brief Artist or creator name.
                static inline const ID Artist{"Artist"};
                /// @brief Free-form comment.
                static inline const ID Comment{"Comment"};
                /// @brief Creation or origination date.
                static inline const ID Date{"Date"};
                /// @brief Album name (audio media).
                static inline const ID Album{"Album"};
                /// @brief License information.
                static inline const ID License{"License"};
                /// @brief Track number (audio media).
                static inline const ID TrackNumber{"TrackNumber"};
                /// @brief Genre (audio media).
                static inline const ID Genre{"Genre"};
                /// @brief Enable Broadcast Wave Format metadata in audio files.
                static inline const ID EnableBWF{"EnableBWF"};
                /// @brief Human-readable description of the content.
                static inline const ID Description{"Description"};
                /// @brief BWF originator name.
                static inline const ID Originator{"Originator"};
                /// @brief BWF originator reference.
                static inline const ID OriginatorReference{"OriginatorReference"};
                /// @brief BWF origination date and time.
                static inline const ID OriginationDateTime{"OriginationDateTime"};
                /// @brief Frame rate of the associated video.
                static inline const ID FrameRate{"FrameRate"};
                /// @brief SMPTE UMID (Unique Material Identifier).
                static inline const ID UMID{"UMID"};
                /// @brief BWF coding history string.
                static inline const ID CodingHistory{"CodingHistory"};
                /// @brief Compression level hint for lossy codecs.
                static inline const ID CompressionLevel{"CompressionLevel"};
                /// @brief Enable variable bit-rate encoding.
                static inline const ID EnableVBR{"EnableVBR"};
                /// @brief VBR quality setting (codec-specific).
                static inline const ID VBRQuality{"VBRQuality"};
                /// @brief Internal: allocation hint for compressed pixel formats.
                /// Use Image::compressedSize() instead.
                static inline const ID CompressedSize{"CompressedSize"};
                /// @brief Signals end-of-stream to downstream nodes.
                static inline const ID EndOfStream{"EndOfStream"};
                /// @brief Frame sequence number within a stream.
                static inline const ID FrameNumber{"FrameNumber"};

                /**
                 * @brief Converts a metadata ID to its string name.
                 * @param id The metadata ID.
                 * @return The name string.
                 */
                static String idToString(ID id) { return id.name(); }

                /**
                 * @brief Converts a string name to a metadata ID.
                 * @param val The name string.
                 * @return The corresponding ID.
                 */
                static ID stringToID(const String &val) { return ID(val); }

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
                 * @brief Returns a human-readable dump of all metadata entries.
                 * @return A StringList with one entry per metadata key-value pair.
                 */
                StringList dump() const;

                /**
                 * @brief Returns true if both Metadata objects contain the same entries.
                 * @param other The Metadata to compare against.
                 * @return true if equal.
                 */
                bool operator==(const Metadata &other) const;
};

PROMEKI_NAMESPACE_END
