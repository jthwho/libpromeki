/**
 * @file      metadata.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/config.h>
#include <promeki/variantdatabase.h>
#include <promeki/sharedptr.h>
#if PROMEKI_ENABLE_NETWORK
#include <promeki/eui64.h>
#endif

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
 * Each ID is declared via @ref declareID with a mandatory @ref VariantSpec
 * that captures the accepted type, default value, and description.
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

                // ============================================================
                // Core metadata
                // ============================================================

                /// @brief SMPTE timecode associated with this media unit.
                static inline const ID Timecode = declareID("Timecode",
                        VariantSpec().setType(Variant::TypeTimecode)
                                .setDefault(promeki::Timecode())
                                .setDescription("SMPTE timecode associated with this media unit."));

                /// @brief Gamma / transfer-function exponent.
                static inline const ID Gamma = declareID("Gamma",
                        VariantSpec().setType(Variant::TypeDouble)
                                .setDefault(0.0)
                                .setMin(0.0)
                                .setDescription("Gamma / transfer-function exponent."));

                /// @brief Title of the media.
                static inline const ID Title = declareID("Title",
                        VariantSpec().setType(Variant::TypeString)
                                .setDefault(String())
                                .setDescription("Title of the media."));

                /// @brief Copyright notice.
                static inline const ID Copyright = declareID("Copyright",
                        VariantSpec().setType(Variant::TypeString)
                                .setDefault(String())
                                .setDescription("Copyright notice."));

                /// @brief Software that created or last modified the media.
                static inline const ID Software = declareID("Software",
                        VariantSpec().setType(Variant::TypeString)
                                .setDefault(String())
                                .setDescription("Software that created or modified the media."));

                /// @brief Artist or creator name.
                static inline const ID Artist = declareID("Artist",
                        VariantSpec().setType(Variant::TypeString)
                                .setDefault(String())
                                .setDescription("Artist or creator name."));

                /// @brief Free-form comment.
                static inline const ID Comment = declareID("Comment",
                        VariantSpec().setType(Variant::TypeString)
                                .setDefault(String())
                                .setDescription("Free-form comment."));

                /// @brief Creation or origination date.
                static inline const ID Date = declareID("Date",
                        VariantSpec().setType(Variant::TypeString)
                                .setDefault(String())
                                .setDescription("Creation or origination date."));

                /// @brief Album name (audio media).
                static inline const ID Album = declareID("Album",
                        VariantSpec().setType(Variant::TypeString)
                                .setDefault(String())
                                .setDescription("Album name (audio media)."));

                /// @brief License information.
                static inline const ID License = declareID("License",
                        VariantSpec().setType(Variant::TypeString)
                                .setDefault(String())
                                .setDescription("License information."));

                /// @brief Track number (audio media).
                static inline const ID TrackNumber = declareID("TrackNumber",
                        VariantSpec().setTypes({Variant::TypeS32, Variant::TypeString})
                                .setDefault(String())
                                .setDescription("Track number (audio media)."));

                /// @brief Genre (audio media).
                static inline const ID Genre = declareID("Genre",
                        VariantSpec().setType(Variant::TypeString)
                                .setDefault(String())
                                .setDescription("Genre (audio media)."));

                /// @brief Enable Broadcast Wave Format metadata in audio files.
                static inline const ID EnableBWF = declareID("EnableBWF",
                        VariantSpec().setType(Variant::TypeBool)
                                .setDefault(false)
                                .setDescription("Enable Broadcast Wave Format metadata."));

                /// @brief Human-readable description of the content.
                static inline const ID Description = declareID("Description",
                        VariantSpec().setType(Variant::TypeString)
                                .setDefault(String())
                                .setDescription("Human-readable description of the content."));

                /// @brief BWF originator name.
                static inline const ID Originator = declareID("Originator",
                        VariantSpec().setType(Variant::TypeString)
                                .setDefault(String())
                                .setDescription("BWF originator name."));

                /// @brief BWF originator reference.
                static inline const ID OriginatorReference = declareID("OriginatorReference",
                        VariantSpec().setTypes({Variant::TypeString, Variant::TypeUUID})
                                .setDefault(String())
                                .setDescription("BWF originator reference."));

                /// @brief BWF origination date and time.
                static inline const ID OriginationDateTime = declareID("OriginationDateTime",
                        VariantSpec().setType(Variant::TypeString)
                                .setDefault(String())
                                .setDescription("BWF origination date and time."));

                /// @brief Frame rate of the associated video.
                static inline const ID FrameRate = declareID("FrameRate",
                        VariantSpec().setTypes({Variant::TypeRational, Variant::TypeDouble, Variant::TypeFrameRate})
                                .setDefault(Rational<int>())
                                .setDescription("Frame rate of the associated video."));

                /// @brief Source that supplied the associated FrameRate (String).
                /// One of: @c "file" (read from the container/sidecar),
                /// @c "config" (caller-supplied override), or @c "default"
                /// (backend fell back to its built-in default).
                static inline const ID FrameRateSource = declareID("FrameRateSource",
                        VariantSpec().setType(Variant::TypeString)
                                .setDefault(String())
                                .setDescription("Source of the FrameRate value (file, config, or default)."));

                /// @brief SMPTE UMID (Unique Material Identifier).
                static inline const ID UMID = declareID("UMID",
                        VariantSpec().setTypes({Variant::TypeString, Variant::TypeUMID})
                                .setDefault(String())
                                .setDescription("SMPTE UMID (Unique Material Identifier)."));

                /// @brief BWF coding history string.
                static inline const ID CodingHistory = declareID("CodingHistory",
                        VariantSpec().setType(Variant::TypeString)
                                .setDefault(String())
                                .setDescription("BWF coding history string."));

                // ============================================================
                // Compression metadata
                // ============================================================

                /// @brief Compression level hint for lossy codecs.
                static inline const ID CompressionLevel = declareID("CompressionLevel",
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(0))
                                .setDescription("Compression level hint for lossy codecs."));

                /// @brief Enable variable bit-rate encoding.
                static inline const ID EnableVBR = declareID("EnableVBR",
                        VariantSpec().setType(Variant::TypeBool)
                                .setDefault(false)
                                .setDescription("Enable variable bit-rate encoding."));

                /// @brief VBR quality setting (codec-specific).
                static inline const ID VBRQuality = declareID("VBRQuality",
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(0))
                                .setDescription("VBR quality setting (codec-specific)."));

                /// @brief Internal: allocation hint for compressed pixel formats.
                /// Use Image::compressedSize() instead.
                static inline const ID CompressedSize = declareID("CompressedSize",
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(0))
                                .setMin(int32_t(0))
                                .setDescription("Internal allocation hint for compressed pixel formats."));

                // ============================================================
                // Streaming / frame status
                // ============================================================

                /// @brief Signals end-of-stream to downstream nodes.
                static inline const ID EndOfStream = declareID("EndOfStream",
                        VariantSpec().setType(Variant::TypeBool)
                                .setDefault(false)
                                .setDescription("Signals end-of-stream to downstream nodes."));

                /// @brief Frame sequence number within a stream.
                static inline const ID FrameNumber = declareID("FrameNumber",
                        VariantSpec().setType(Variant::TypeS64)
                                .setDefault(int64_t(0))
                                .setMin(int64_t(0))
                                .setDescription("Frame sequence number within a stream."));

                /// @brief Timestamp of when the library or device captured this data.
                ///
                /// Records when the frame was first observed by the library
                /// or the device it talks to.  For live capture this is
                /// typically SystemMonotonic; for network streams it is the
                /// moment the first packet arrived.
                static inline const ID CaptureTime = declareID("CaptureTime",
                        VariantSpec().setType(Variant::TypeMediaTimeStamp)
                                .setDefault(promeki::MediaTimeStamp())
                                .setDescription("Timestamp when the library or device captured this data."));

                /// @brief Timestamp at which this essence should be presented.
                ///
                /// Set by downstream scheduling or pacing logic to indicate
                /// when a frame should be rendered or transmitted.
                static inline const ID PresentationTime = declareID("PresentationTime",
                        VariantSpec().setType(Variant::TypeMediaTimeStamp)
                                .setDefault(promeki::MediaTimeStamp())
                                .setDescription("Timestamp when this essence should be presented."));

                /// @brief Clock-domain-aware timestamp for media timing.
                static inline const ID MediaTimeStamp = declareID("MediaTimeStamp",
                        VariantSpec().setType(Variant::TypeMediaTimeStamp)
                                .setDefault(promeki::MediaTimeStamp())
                                .setDescription("Clock-domain-aware timestamp for media timing."));

                /// @brief RTP timestamp from the packet header (uint32_t).
                ///
                /// The raw 32-bit RTP timestamp carried in the packet(s)
                /// that delivered this essence.  Clock rate is stream-defined
                /// (typically 90 kHz for video).
                static inline const ID RtpTimestamp = declareID("RtpTimestamp",
                        VariantSpec().setType(Variant::TypeU32)
                                .setDefault(uint32_t(0))
                                .setDescription("RTP timestamp from the packet header."));

                /// @brief Number of RTP packets that composed this essence (int32_t).
                static inline const ID RtpPacketCount = declareID("RtpPacketCount",
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(0))
                                .setMin(int32_t(0))
                                .setDescription("Number of RTP packets that composed this essence."));

#if PROMEKI_ENABLE_NETWORK
                /// @brief PTP grandmaster clock identity (EUI-64).
                static inline const ID PtpGrandmasterId = declareID("PtpGrandmasterId",
                        VariantSpec().setType(Variant::TypeEUI64)
                                .setDefault(EUI64())
                                .setDescription("PTP grandmaster clock identity (EUI-64)."));

                /// @brief PTP domain number (0-127).
                static inline const ID PtpDomainNumber = declareID("PtpDomainNumber",
                        VariantSpec().setType(Variant::TypeU8)
                                .setDefault(uint8_t(0))
                                .setMax(uint8_t(127))
                                .setDescription("PTP domain number (0-127)."));
#endif

                /// @brief Number of times this frame was repeated due to underrun (int).
                static inline const ID FrameRepeated = declareID("FrameRepeated",
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(0))
                                .setMin(int32_t(0))
                                .setDescription("Number of times frame repeated due to underrun."));

                /// @brief Number of frames dropped immediately before this one (int).
                static inline const ID FrameDropped = declareID("FrameDropped",
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(0))
                                .setMin(int32_t(0))
                                .setDescription("Number of frames dropped before this one."));

                /// @brief This frame arrived later than its scheduled time (bool).
                static inline const ID FrameLate = declareID("FrameLate",
                        VariantSpec().setType(Variant::TypeBool)
                                .setDefault(false)
                                .setDescription("Frame arrived later than scheduled."));

                /// @brief This frame is a keyframe / intra frame (bool).
                static inline const ID FrameKeyframe = declareID("FrameKeyframe",
                        VariantSpec().setType(Variant::TypeBool)
                                .setDefault(false)
                                .setDescription("Frame is a keyframe / intra frame."));

                /// @brief This frame's MediaDesc differs from the previously
                /// reported one (bool).
                static inline const ID MediaDescChanged = declareID("MediaDescChanged",
                        VariantSpec().setType(Variant::TypeBool)
                                .setDefault(false)
                                .setDescription("Frame's MediaDesc differs from previously reported."));

                // ============================================================
                // DPX file info
                // ============================================================

                /// @brief Original source filename (from previous save).
                static inline const ID FileOrigName = declareID("FileOrigName",
                        VariantSpec().setType(Variant::TypeString)
                                .setDefault(String())
                                .setDescription("Original source filename."));

                /// @brief Project name.
                static inline const ID Project = declareID("Project",
                        VariantSpec().setType(Variant::TypeString)
                                .setDefault(String())
                                .setDescription("Project name."));

                /// @brief Reel or input device name.
                static inline const ID Reel = declareID("Reel",
                        VariantSpec().setType(Variant::TypeString)
                                .setDefault(String())
                                .setDescription("Reel or input device name."));

                // ============================================================
                // DPX film info
                // ============================================================

                /// @brief Film manufacturer ID code (2 chars).
                static inline const ID FilmMfgID = declareID("FilmMfgID",
                        VariantSpec().setType(Variant::TypeString)
                                .setDefault(String())
                                .setDescription("Film manufacturer ID code (2 chars)."));

                /// @brief Film type (2 chars).
                static inline const ID FilmType = declareID("FilmType",
                        VariantSpec().setType(Variant::TypeString)
                                .setDefault(String())
                                .setDescription("Film type (2 chars)."));

                /// @brief Film offset in perfs (2 chars).
                static inline const ID FilmOffset = declareID("FilmOffset",
                        VariantSpec().setType(Variant::TypeString)
                                .setDefault(String())
                                .setDescription("Film offset in perfs (2 chars)."));

                /// @brief Film prefix (6 chars).
                static inline const ID FilmPrefix = declareID("FilmPrefix",
                        VariantSpec().setType(Variant::TypeString)
                                .setDefault(String())
                                .setDescription("Film prefix (6 chars)."));

                /// @brief Film count (4 chars).
                static inline const ID FilmCount = declareID("FilmCount",
                        VariantSpec().setType(Variant::TypeString)
                                .setDefault(String())
                                .setDescription("Film count (4 chars)."));

                /// @brief Film format (e.g. "Academy").
                static inline const ID FilmFormat = declareID("FilmFormat",
                        VariantSpec().setType(Variant::TypeString)
                                .setDefault(String())
                                .setDescription("Film format (e.g. Academy)."));

                /// @brief Sequence position (frame number in sequence).
                static inline const ID FilmSeqPos = declareID("FilmSeqPos",
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(0))
                                .setMin(int32_t(0))
                                .setDescription("Sequence position (frame number in sequence)."));

                /// @brief Sequence length (total frames in sequence).
                static inline const ID FilmSeqLen = declareID("FilmSeqLen",
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(0))
                                .setMin(int32_t(0))
                                .setDescription("Sequence length (total frames in sequence)."));

                /// @brief Held count (1 = default, >1 = repeated frame).
                static inline const ID FilmHoldCount = declareID("FilmHoldCount",
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(1))
                                .setMin(int32_t(1))
                                .setDescription("Held count (1 = default, >1 = repeated frame)."));

                /// @brief Film shutter angle in degrees.
                static inline const ID FilmShutter = declareID("FilmShutter",
                        VariantSpec().setType(Variant::TypeDouble)
                                .setDefault(0.0)
                                .setMin(0.0)
                                .setMax(360.0)
                                .setDescription("Film shutter angle in degrees."));

                /// @brief Film frame identification (e.g. keycode).
                static inline const ID FilmFrameID = declareID("FilmFrameID",
                        VariantSpec().setType(Variant::TypeString)
                                .setDefault(String())
                                .setDescription("Film frame identification (e.g. keycode)."));

                /// @brief Film slate information.
                static inline const ID FilmSlate = declareID("FilmSlate",
                        VariantSpec().setType(Variant::TypeString)
                                .setDefault(String())
                                .setDescription("Film slate information."));

                // ============================================================
                // DPX TV info
                // ============================================================

                /// @brief Field number within an interlaced frame (0 or 1).
                static inline const ID FieldID = declareID("FieldID",
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(0))
                                .setRange(int32_t(0), int32_t(1))
                                .setDescription("Field number within an interlaced frame (0 or 1)."));

                // ============================================================
                // DPX image element info
                // ============================================================

                /// @brief SMPTE 268M transfer characteristic code.
                static inline const ID TransferCharacteristic = declareID("TransferCharacteristic",
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(0))
                                .setMin(int32_t(0))
                                .setDescription("SMPTE 268M transfer characteristic code."));

                /// @brief SMPTE 268M colorimetric specification code.
                static inline const ID Colorimetric = declareID("Colorimetric",
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(0))
                                .setMin(int32_t(0))
                                .setDescription("SMPTE 268M colorimetric specification code."));

                /// @brief Image orientation code (0 = left-to-right, top-to-bottom).
                static inline const ID Orientation = declareID("Orientation",
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(0))
                                .setMin(int32_t(0))
                                .setDescription("Image orientation code (0 = left-to-right, top-to-bottom)."));

                // ============================================================
                // Methods
                // ============================================================

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

                /**
                 * @brief Fills in standard defaults used for MediaIO writes.
                 *
                 * Each of the fields below is populated only if the
                 * corresponding key is not already present (via
                 * @ref VariantDatabase::setIfMissing), so caller-supplied
                 * values are always preserved.  This helper is intended to
                 * be called by @ref MediaIO on the container metadata just
                 * before dispatching a writer open; it is @em not called
                 * automatically by any other part of the Metadata class.
                 *
                 * Defaults populated:
                 *
                 *  - @ref Date — current wall-clock UTC date, formatted as
                 *    @c "YYYY-MM-DD".
                 *  - @ref OriginationDateTime — current wall-clock UTC
                 *    timestamp, formatted as @c "YYYY-MM-DDTHH:MM:SS"
                 *    (BWF-friendly ISO 8601).
                 *  - @ref Software — the Application name if
                 *    @ref Application::appName is non-empty, otherwise
                 *    @c "libpromeki (https://howardlogic.com)".
                 *  - @ref Originator — always
                 *    @c "libpromeki howardlogic.com".  BWF caps this
                 *    field at 32 characters; this value fits and acts
                 *    as a persistent libpromeki signature.
                 *  - @ref OriginatorReference — a fresh UUID v7
                 *    (time-sortable) as a string.
                 *  - @ref UMID — a fresh Extended SMPTE 330M UMID with
                 *    a random material number and a @c "MEKI"
                 *    organization tag in the source pack.
                 */
                void applyMediaIOWriteDefaults();
};

PROMEKI_NAMESPACE_END
