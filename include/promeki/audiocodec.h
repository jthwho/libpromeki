/**
 * @file      audiocodec.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 */

#pragma once

#include <functional>
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/list.h>
#include <promeki/fourcc.h>

PROMEKI_NAMESPACE_BEGIN

class AudioEncoder;
class AudioDecoder;

/**
 * @brief First-class identifier for an audio codec family.
 * @ingroup proav
 *
 * Symmetric counterpart to @ref VideoCodec — same TypeRegistry
 * pattern, same split between Data record (metadata + factory
 * pointers) and inline wrapper (resolved pointer to an immutable
 * entry).  See @ref VideoCodec for the narrative; the data flowing
 * through an audio codec is just samples rather than frames.
 *
 * Well-known IDs cover the codec families libpromeki's pipelines
 * will realistically see: PCM variants (no real "codec" but
 * convenient for identifying sample formats uniformly), AAC, Opus,
 * FLAC, MP3, AC-3.  The @c PCM_ variants are intentionally granular
 * so a pipeline can say "this track is PCM 24-bit LE little-endian"
 * without losing that information when it passes through the codec
 * registry.
 *
 * Factory pointers can be null on codecs we recognise but don't yet
 * implement — the registry still carries their metadata (FourCCs,
 * descriptions) so CLI listings and introspection keep working.
 */
class AudioCodec {
        public:
                /**
                 * @brief Identifies an audio codec family.
                 *
                 * Well-known codecs have named enumerators.  User-defined
                 * codecs obtain IDs from @ref registerType, starting at
                 * @c UserDefined.
                 */
                enum ID {
                        Invalid         = 0,    ///< Invalid or uninitialised.
                        PCMI_S16LE      = 1,    ///< Interleaved PCM signed 16-bit little-endian.
                        PCMI_S16BE      = 2,    ///< Interleaved PCM signed 16-bit big-endian.
                        PCMI_S24LE      = 3,    ///< Interleaved PCM signed 24-bit little-endian.
                        PCMI_S24BE      = 4,    ///< Interleaved PCM signed 24-bit big-endian.
                        PCMI_S32LE      = 5,    ///< Interleaved PCM signed 32-bit little-endian.
                        PCMI_S32BE      = 6,    ///< Interleaved PCM signed 32-bit big-endian.
                        PCMI_Float32LE  = 7,    ///< Interleaved PCM 32-bit IEEE 754 float little-endian.
                        PCMI_Float32BE  = 8,    ///< Interleaved PCM 32-bit IEEE 754 float big-endian.
                        AAC             = 9,    ///< Advanced Audio Coding (ISO/IEC 14496-3).
                        Opus            = 10,   ///< Opus (RFC 6716).
                        FLAC            = 11,   ///< Free Lossless Audio Codec.
                        MP3             = 12,   ///< MPEG-1 Audio Layer III.
                        AC3             = 13,   ///< Dolby Digital (AC-3).
                        UserDefined     = 1024  ///< First ID available for user-registered codecs.
                };

                /** @brief List of AudioCodec IDs. */
                using IDList = List<ID>;

                /** @brief Factory for a new AudioEncoder session (may be null). */
                using EncoderFactory = std::function<AudioEncoder *()>;

                /** @brief Factory for a new AudioDecoder session (may be null). */
                using DecoderFactory = std::function<AudioDecoder *()>;

                /**
                 * @brief Immutable descriptor for an audio codec.
                 *
                 * Populated by the library for well-known codecs, or by
                 * applications via @ref registerData for custom codecs.
                 */
                struct Data {
                        ID              id = Invalid;   ///< Unique codec identifier.
                        String          name;           ///< Short name (e.g. @c "AAC").
                        String          desc;           ///< Human-readable description.
                        FourCCList      fourccList;     ///< Associated FourCC codes.
                        EncoderFactory  createEncoder;  ///< Factory for AudioEncoder sessions; null = encode not supported.
                        DecoderFactory  createDecoder;  ///< Factory for AudioDecoder sessions; null = decode not supported.
                };

                /** @brief Allocates and returns a unique ID for a user-defined codec. */
                static ID registerType();

                /**
                 * @brief Registers a Data record in the registry.
                 *
                 * After this call, constructing an @ref AudioCodec from
                 * @c data.id resolves to the registered data.
                 */
                static void registerData(Data &&data);

                /**
                 * @brief Returns the list of every registered codec's ID.
                 * @return IDs of every registered codec, excluding @ref Invalid.
                 */
                static IDList registeredIDs();

                /**
                 * @brief Looks up a codec by its registered name.
                 * @param name The name to search for (e.g. @c "AAC").
                 * @return The matching codec, or an invalid codec if not found.
                 */
                static AudioCodec lookup(const String &name);

                /**
                 * @brief Constructs an AudioCodec from an ID.
                 * @param id The codec ID (default: @ref Invalid).
                 */
                inline AudioCodec(ID id = Invalid);

                /** @brief Returns true when this wrapper references a registered codec. */
                bool isValid() const { return d != nullptr && d->id != Invalid; }

                /** @brief Returns the unique ID. */
                ID id() const { return d->id; }

                /** @brief Returns the codec's short registered name. */
                const String &name() const { return d->name; }

                /** @brief Returns the codec's human-readable description. */
                const String &description() const { return d->desc; }

                /** @brief Returns the list of FourCCs associated with this codec. */
                const FourCCList &fourccList() const { return d->fourccList; }

                /** @brief Returns true when this codec has a registered encoder factory. */
                bool canEncode() const { return d->createEncoder != nullptr; }

                /** @brief Returns true when this codec has a registered decoder factory. */
                bool canDecode() const { return d->createDecoder != nullptr; }

                /**
                 * @brief Creates a new @ref AudioEncoder session for this codec.
                 * @return A freshly-allocated encoder (caller owns), or nullptr when
                 *         encode is not supported by this codec.
                 */
                AudioEncoder *createEncoder() const {
                        return d->createEncoder ? d->createEncoder() : nullptr;
                }

                /**
                 * @brief Creates a new @ref AudioDecoder session for this codec.
                 * @return A freshly-allocated decoder (caller owns), or nullptr when
                 *         decode is not supported by this codec.
                 */
                AudioDecoder *createDecoder() const {
                        return d->createDecoder ? d->createDecoder() : nullptr;
                }

                /** @brief Equality compares the underlying Data pointer. */
                bool operator==(const AudioCodec &o) const { return d == o.d; }

                /** @brief Inequality. */
                bool operator!=(const AudioCodec &o) const { return d != o.d; }

                /** @brief Returns the underlying Data pointer. */
                const Data *data() const { return d; }

        private:
                const Data *d = nullptr;
                static const Data *lookupData(ID id);
};

inline AudioCodec::AudioCodec(ID id) : d(lookupData(id)) {}

PROMEKI_NAMESPACE_END
