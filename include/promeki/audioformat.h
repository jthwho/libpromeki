/**
 * @file      audioformat.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/list.h>
#include <promeki/fourcc.h>
#include <promeki/audiocodec.h>
#include <promeki/result.h>
#include <promeki/system.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief First-class audio sample-format descriptor.
 * @ingroup proav
 *
 * Uses the @ref typeregistry "TypeRegistry pattern": a lightweight inline
 * wrapper around an immutable Data record, identified by an integer ID.
 * Well-known formats are provided as ID constants; user-defined formats
 * can be registered at runtime via @ref registerType and
 * @ref registerData — exactly parallel to @ref PixelFormat on the pixel
 * side.
 *
 * AudioFormat describes the *format* of an audio stream (sample type,
 * bit depth, endianness, planar vs interleaved, optional compressed
 * codec identity) independent of sample rate and channel count.  The
 * tuple format + sampleRate + channels is bundled by @ref AudioDesc.
 *
 * @par PCM vs compressed
 *
 * PCM formats (e.g. @ref PCMI_S16LE, @ref PCMP_Float32LE) populate every
 * field meaningfully and expose sample conversion helpers via
 * @ref samplesToFloat / @ref floatToSamples.  Compressed formats
 * (e.g. @ref Opus, @ref AAC) set @ref isCompressed to @c true and carry
 * an @ref audioCodec identifying the codec family; their sample-level
 * fields are zero / default because a compressed bitstream has no
 * single bit depth or endianness.
 *
 * @par Naming convention
 *
 * C++ identifiers and string names for PCM use a short prefix followed
 * by the sample type and byte order:
 *
 *   - @b PCMI_ — interleaved PCM (all channels stored sample-by-sample).
 *   - @b PCMP_ — planar PCM (each channel stored in its own plane).
 *
 * After the prefix, the name spells out the sample type (@c S8, @c U8,
 * @c S16LE, @c S32BE, @c Float32LE, …).  Compressed formats use the
 * codec's canonical name as the identifier (@c Opus, @c AAC, @c FLAC,
 * @c MP3, @c AC3).
 *
 * @par Examples
 * | C++ identifier        | Description                                            |
 * |-----------------------|--------------------------------------------------------|
 * | @c PCMI_Float32LE     | Interleaved 32-bit IEEE 754 float, little-endian        |
 * | @c PCMI_S16LE         | Interleaved signed 16-bit, little-endian                |
 * | @c PCMP_S24BE         | Planar signed 24-bit, big-endian                        |
 * | @c PCMI_U8            | Interleaved unsigned 8-bit                              |
 * | @c Opus               | Opus compressed bitstream (RFC 6716)                    |
 * | @c AAC                | Advanced Audio Coding (ISO/IEC 14496-3)                |
 *
 * @par Example usage
 * @code
 * AudioFormat fmt(AudioFormat::PCMI_S16LE);
 * assert(fmt.bytesPerSample() == 2);
 * assert(fmt.isSigned());
 * assert(!fmt.isCompressed());
 *
 * AudioFormat opus(AudioFormat::Opus);
 * assert(opus.isCompressed());
 * assert(opus.audioCodec().id() == AudioCodec::Opus);
 * @endcode
 *
 * @see AudioDesc, AudioCodec, @ref typeregistry "TypeRegistry Pattern"
 */
class AudioFormat {
        public:
                /** @brief Minimum value of a signed 24-bit integer. */
                static constexpr int32_t MinS24 = -8388608;
                /** @brief Maximum value of a signed 24-bit integer. */
                static constexpr int32_t MaxS24 = 8388607;
                /** @brief Minimum value of an unsigned 24-bit integer. */
                static constexpr int32_t MinU24 = 0;
                /** @brief Maximum value of an unsigned 24-bit integer. */
                static constexpr int32_t MaxU24 = 16777215;

                /**
                 * @brief Identifies an audio format.
                 *
                 * Well-known formats have named enumerators.  User-defined
                 * formats obtain IDs from @ref registerType, starting at
                 * @c UserDefined.
                 */
                enum ID {
                        Invalid = 0, ///< Invalid or uninitialized.

                        // -- Interleaved PCM -------------------------------
                        PCMI_Float32LE = 1, ///< Interleaved 32-bit IEEE 754 float, little-endian.
                        PCMI_Float32BE = 2, ///< Interleaved 32-bit IEEE 754 float, big-endian.
                        PCMI_S8 = 3,        ///< Interleaved signed 8-bit.
                        PCMI_U8 = 4,        ///< Interleaved unsigned 8-bit.
                        PCMI_S16LE = 5,     ///< Interleaved signed 16-bit, little-endian.
                        PCMI_U16LE = 6,     ///< Interleaved unsigned 16-bit, little-endian.
                        PCMI_S16BE = 7,     ///< Interleaved signed 16-bit, big-endian.
                        PCMI_U16BE = 8,     ///< Interleaved unsigned 16-bit, big-endian.
                        PCMI_S24LE = 9,     ///< Interleaved signed 24-bit, little-endian (3 bytes per sample).
                        PCMI_U24LE = 10,    ///< Interleaved unsigned 24-bit, little-endian.
                        PCMI_S24BE = 11,    ///< Interleaved signed 24-bit, big-endian.
                        PCMI_U24BE = 12,    ///< Interleaved unsigned 24-bit, big-endian.
                        PCMI_S32LE = 13,    ///< Interleaved signed 32-bit, little-endian.
                        PCMI_U32LE = 14,    ///< Interleaved unsigned 32-bit, little-endian.
                        PCMI_S32BE = 15,    ///< Interleaved signed 32-bit, big-endian.
                        PCMI_U32BE = 16,    ///< Interleaved unsigned 32-bit, big-endian.
                        // 24-bit data carried in a 32-bit container.  HB32 = data
                        // occupies the high 3 bytes of the word (low byte is 0);
                        // LB32 = data occupies the low 3 bytes (high byte is 0).
                        // Endianness applies to the 32-bit word as a whole, not
                        // the 24-bit data subset.
                        PCMI_S24LE_HB32 = 17, ///< Interleaved signed 24-bit in high 3 bytes of LE 32-bit word.
                        PCMI_S24LE_LB32 = 18, ///< Interleaved signed 24-bit in low 3 bytes of LE 32-bit word.
                        PCMI_S24BE_HB32 = 19, ///< Interleaved signed 24-bit in high 3 bytes of BE 32-bit word.
                        PCMI_S24BE_LB32 = 20, ///< Interleaved signed 24-bit in low 3 bytes of BE 32-bit word.
                        PCMI_U24LE_HB32 = 21, ///< Interleaved unsigned 24-bit in high 3 bytes of LE 32-bit word.
                        PCMI_U24LE_LB32 = 22, ///< Interleaved unsigned 24-bit in low 3 bytes of LE 32-bit word.
                        PCMI_U24BE_HB32 = 23, ///< Interleaved unsigned 24-bit in high 3 bytes of BE 32-bit word.
                        PCMI_U24BE_LB32 = 24, ///< Interleaved unsigned 24-bit in low 3 bytes of BE 32-bit word.

                        // -- Planar PCM ------------------------------------
                        PCMP_Float32LE = 32, ///< Planar 32-bit IEEE 754 float, little-endian.
                        PCMP_Float32BE = 33, ///< Planar 32-bit IEEE 754 float, big-endian.
                        PCMP_S8 = 34,        ///< Planar signed 8-bit.
                        PCMP_U8 = 35,        ///< Planar unsigned 8-bit.
                        PCMP_S16LE = 36,     ///< Planar signed 16-bit, little-endian.
                        PCMP_U16LE = 37,     ///< Planar unsigned 16-bit, little-endian.
                        PCMP_S16BE = 38,     ///< Planar signed 16-bit, big-endian.
                        PCMP_U16BE = 39,     ///< Planar unsigned 16-bit, big-endian.
                        PCMP_S24LE = 40,     ///< Planar signed 24-bit, little-endian.
                        PCMP_U24LE = 41,     ///< Planar unsigned 24-bit, little-endian.
                        PCMP_S24BE = 42,     ///< Planar signed 24-bit, big-endian.
                        PCMP_U24BE = 43,     ///< Planar unsigned 24-bit, big-endian.
                        PCMP_S32LE = 44,     ///< Planar signed 32-bit, little-endian.
                        PCMP_U32LE = 45,     ///< Planar unsigned 32-bit, little-endian.
                        PCMP_S32BE = 46,     ///< Planar signed 32-bit, big-endian.
                        PCMP_U32BE = 47,     ///< Planar unsigned 32-bit, big-endian.
                        // Planar 24-bit-in-32-bit container variants, mirroring
                        // the interleaved set above.
                        PCMP_S24LE_HB32 = 48, ///< Planar signed 24-bit in high 3 bytes of LE 32-bit word.
                        PCMP_S24LE_LB32 = 49, ///< Planar signed 24-bit in low 3 bytes of LE 32-bit word.
                        PCMP_S24BE_HB32 = 50, ///< Planar signed 24-bit in high 3 bytes of BE 32-bit word.
                        PCMP_S24BE_LB32 = 51, ///< Planar signed 24-bit in low 3 bytes of BE 32-bit word.
                        PCMP_U24LE_HB32 = 52, ///< Planar unsigned 24-bit in high 3 bytes of LE 32-bit word.
                        PCMP_U24LE_LB32 = 53, ///< Planar unsigned 24-bit in low 3 bytes of LE 32-bit word.
                        PCMP_U24BE_HB32 = 54, ///< Planar unsigned 24-bit in high 3 bytes of BE 32-bit word.
                        PCMP_U24BE_LB32 = 55, ///< Planar unsigned 24-bit in low 3 bytes of BE 32-bit word.

                        // -- Compressed ------------------------------------
                        Opus = 64, ///< Opus compressed bitstream (RFC 6716).
                        AAC = 65,  ///< Advanced Audio Coding (ISO/IEC 14496-3).
                        FLAC = 66, ///< Free Lossless Audio Codec.
                        MP3 = 67,  ///< MPEG-1 Audio Layer III.
                        AC3 = 68,  ///< Dolby Digital (AC-3).

                        UserDefined = 1024 ///< First ID available for user-registered formats.
                };

                /** @brief List of AudioFormat IDs. */
                using IDList = List<ID>;

                /**
                 * @brief The platform's native float32 PCM format.
                 *
                 * Interleaved float32 using the CPU's native byte order.
                 * Used as the canonical internal representation for DSP
                 * kernels and conversion pipelines.
                 */
                static constexpr ID NativeFloat = System::isLittleEndian() ? PCMI_Float32LE : PCMI_Float32BE;

                /**
                 * @brief Immutable data record for an audio format.
                 *
                 * Populated by the library for well-known formats, or by
                 * applications via @ref registerData for custom formats.
                 * The sample-level fields (@c bytesPerSample,
                 * @c bitsPerSample, @c isSigned, @c isFloat, @c isPlanar,
                 * @c isBigEndian) only have meaningful values for PCM
                 * formats; compressed formats set @c compressed to
                 * @c true and leave those fields at defaults.
                 */
                struct Data {
                                ID           id = Invalid;        ///< Unique format identifier.
                                String       name;                ///< Short name (e.g. @c "PCMI_S16LE").
                                String       desc;                ///< Human-readable description.
                                size_t       bytesPerSample = 0;  ///< Bytes per single sample (PCM only).
                                size_t       bitsPerSample = 0;   ///< Bits per single sample (PCM only).
                                bool         isSigned = false;    ///< True for signed integer / float samples.
                                bool         isFloat = false;     ///< True for IEEE 754 floating-point samples.
                                bool         isPlanar = false;    ///< True if channels are stored in separate planes.
                                bool         isBigEndian = false; ///< True for big-endian multi-byte samples.
                                bool         compressed = false;  ///< True for compressed bitstream formats.
                                AudioCodec   audioCodec;          ///< Codec identity for compressed formats.
                                FourCC::List fourccList;          ///< Associated FourCC codes (if any).
                                /**
                         * @brief Converts samples from this format to normalized floats in [-1, 1].
                         * @param out     Destination float buffer.
                         * @param in      Source raw sample bytes.
                         * @param samples Number of samples (not sample frames) to convert.
                         */
                                void (*samplesToFloat)(float *out, const uint8_t *in, size_t samples) = nullptr;
                                /**
                         * @brief Converts normalized floats in [-1, 1] to samples in this format.
                         * @param out     Destination raw sample bytes.
                         * @param in      Source float buffer.
                         * @param samples Number of samples (not sample frames) to convert.
                         */
                                void (*floatToSamples)(uint8_t *out, const float *in, size_t samples) = nullptr;
                };

                /**
                 * @brief Allocates and returns a unique ID for a user-defined audio format.
                 * @return A unique ID value.
                 * @see registerData()
                 */
                static ID registerType();

                /**
                 * @brief Registers a Data record in the registry.
                 *
                 * After this call, constructing an AudioFormat from
                 * @c data.id resolves to the registered data.
                 *
                 * @param data The populated Data struct with id set to a
                 *             value from @ref registerType or one of the
                 *             well-known enumerators.
                 */
                static void registerData(Data &&data);

                /**
                 * @brief Returns the list of every registered format's ID.
                 * @return IDs of every registered format, excluding @ref Invalid.
                 */
                static IDList registeredIDs();

                /**
                 * @brief Looks up an AudioFormat by its registered name.
                 * @param name The format name to search for (e.g. @c "PCMI_S16LE").
                 * @return The matching AudioFormat on success, or
                 *         @ref Error::IdNotFound if the name is not
                 *         registered.
                 */
                static Result<AudioFormat> lookup(const String &name);

                /**
                 * @brief Parses a string into an @c AudioFormat.
                 *
                 * Equivalent to @ref lookup — AudioFormat has no backend
                 * suffix to parse — but spelled @c fromString for parity
                 * with @ref AudioCodec::fromString and @ref VideoCodec::fromString.
                 * Used by the Variant string→AudioFormat conversion path.
                 */
                static Result<AudioFormat> fromString(const String &name);

                /**
                 * @brief Looks up an AudioFormat by associated FourCC code.
                 *
                 * Scans every registered format's @c fourccList for a
                 * match.  Primarily used when parsing containers
                 * (QuickTime / MP4 @c stsd, WAV @c fmt) that carry the
                 * codec identity as a FourCC tag.
                 *
                 * @param fcc The FourCC to search for.
                 * @return A matching AudioFormat, or an invalid
                 *         AudioFormat if not found.
                 */
                static AudioFormat lookupByFourCC(const FourCC &fcc);

                /**
                 * @brief Constructs an AudioFormat from an ID.
                 * @param id The format to wrap (default: @ref Invalid).
                 */
                inline AudioFormat(ID id = Invalid);

                /** @brief Returns true if this wrapper references a registered format. */
                bool isValid() const { return d != nullptr && d->id != Invalid; }

                /** @brief Returns the unique ID. */
                ID id() const { return d->id; }

                /** @brief Returns the short registered name (e.g. @c "PCMI_S16LE"). */
                const String &name() const { return d->name; }

                /** @brief Returns the human-readable description. */
                const String &desc() const { return d->desc; }

                /** @brief Returns the number of bytes per single sample (PCM only). */
                size_t bytesPerSample() const { return d->bytesPerSample; }

                /** @brief Returns the number of bits per single sample (PCM only). */
                size_t bitsPerSample() const { return d->bitsPerSample; }

                /** @brief Returns true if the sample type is signed (or float). */
                bool isSigned() const { return d->isSigned; }

                /** @brief Returns true if the sample type is IEEE 754 floating-point. */
                bool isFloat() const { return d->isFloat; }

                /** @brief Returns true if channels are stored in separate planes. */
                bool isPlanar() const { return d->isPlanar; }

                /** @brief Returns true if samples are big-endian. */
                bool isBigEndian() const { return d->isBigEndian; }

                /** @brief Returns true if this is a compressed bitstream format. */
                bool isCompressed() const { return d->compressed; }

                /**
                 * @brief Returns the codec identity for compressed formats.
                 *
                 * For PCM formats this returns an invalid @ref AudioCodec
                 * (whose @c id() is @ref AudioCodec::Invalid).  Use
                 * @ref isCompressed to gate before querying the codec.
                 */
                const AudioCodec &audioCodec() const { return d->audioCodec; }

                /** @brief Returns the list of associated FourCC codes. */
                const FourCC::List &fourccList() const { return d->fourccList; }

                /**
                 * @brief Converts samples in this format to normalized floats.
                 * @param out     Destination float buffer.
                 * @param in      Source raw sample bytes.
                 * @param samples Number of samples to convert (total samples,
                 *                not sample frames).
                 */
                void samplesToFloat(float *out, const uint8_t *in, size_t samples) const {
                        if (d->samplesToFloat != nullptr) d->samplesToFloat(out, in, samples);
                }

                /**
                 * @brief Converts normalized floats to samples in this format.
                 * @param out     Destination raw sample bytes.
                 * @param in      Source float buffer.
                 * @param samples Number of samples to convert.
                 */
                void floatToSamples(uint8_t *out, const float *in, size_t samples) const {
                        if (d->floatToSamples != nullptr) d->floatToSamples(out, in, samples);
                }

                // -- Direct (no-float) format-to-format conversion --------
                //
                // The library tracks a per-(src,dst) registry of "direct"
                // converters that bypass the via-float intermediate step.
                // Direct converters are faster (one memory pass) and
                // additionally permit @ref isBitAccurateTo, which matters
                // when audio buffers carry non-PCM payloads (SMPTE 337M
                // data bursts, AES3 user bits, …) — those bytes survive
                // a direct integer-to-integer transform but are scrambled
                // by an int → float → int round-trip.

                /**
                 * @brief Function signature for a registered direct converter.
                 *
                 * Operates on a contiguous run of samples (interleaved
                 * formats: total samples; planar formats: per-channel
                 * run, with the caller walking channels itself).
                 */
                using DirectConvertFn = void (*)(void *out, const void *in, size_t samples);

                /**
                 * @brief Returns the registered direct converter from @p src to @p dst.
                 *
                 * @return The function pointer, or @c nullptr when no
                 *         direct converter is registered.
                 */
                static DirectConvertFn directConverter(ID src, ID dst);

                /**
                 * @brief Returns true if a registered (src, dst) path is bit-accurate.
                 *
                 * A converter is bit-accurate when the destination sample
                 * pattern preserves every input bit, allowing trivial
                 * reversible repositioning (endian byte swap, signed↔unsigned
                 * sign flip, lossless upcast with zero-fill of unused
                 * lower bits).  Bit-accurate paths are the only ones safe
                 * for non-PCM data carried in audio payloads.
                 *
                 * Returns false if no direct converter is registered for
                 * the pair, or if the registered converter is not flagged
                 * bit-accurate.
                 */
                static bool isBitAccurate(ID src, ID dst);

                /**
                 * @brief Registers a direct converter for a @p src → @p dst pair.
                 *
                 * Used at static-init time by the library's built-in PCM
                 * paths and at run time by applications adding custom
                 * direct converters between user-registered formats.
                 *
                 * @param src         Source format ID.
                 * @param dst         Destination format ID.
                 * @param fn          Converter function.
                 * @param bitAccurate True if the converter preserves every
                 *                    input bit losslessly.
                 */
                static void registerDirectConverter(ID src, ID dst, DirectConvertFn fn, bool bitAccurate);

                /** @brief Returns true if a direct converter is registered for this → @p dst. */
                bool hasDirectConverterTo(const AudioFormat &dst) const {
                        return directConverter(id(), dst.id()) != nullptr;
                }

                /** @brief Returns true if this → @p dst has a registered bit-accurate path. */
                bool isBitAccurateTo(const AudioFormat &dst) const { return isBitAccurate(id(), dst.id()); }

                /**
                 * @brief Converts samples from this format to @p dst.
                 *
                 * Uses the registered direct converter when one exists
                 * for the (this, dst) pair, otherwise falls back to a
                 * via-float trip through @ref samplesToFloat /
                 * @ref floatToSamples using the supplied @p scratch
                 * buffer (must hold @p samples float values).  When the
                 * fastpath hits, @p scratch is unused and may be
                 * @c nullptr.
                 *
                 * Both buffers must be sized for @p samples in their
                 * respective formats (interleaved: total samples;
                 * planar: per-channel run).
                 *
                 * @param dst     Destination format.
                 * @param out     Destination bytes.
                 * @param in      Source bytes.
                 * @param samples Number of samples to convert.
                 * @param scratch Float scratch buffer (samples floats);
                 *                ignored on the direct path.
                 * @return @c Error::Ok on success, @c Error::NotSupported
                 *         when no direct converter exists and at least
                 *         one side is compressed (no via-float possible),
                 *         @c Error::InvalidArgument when @p scratch is
                 *         null but the via-float fallback is required.
                 */
                Error convertTo(const AudioFormat &dst, void *out, const void *in, size_t samples,
                                float *scratch = nullptr) const;

                /** @brief Equality compares the underlying Data pointer. */
                bool operator==(const AudioFormat &o) const { return d == o.d; }

                /** @brief Inequality. */
                bool operator!=(const AudioFormat &o) const { return d != o.d; }

                /** @brief Returns a human-readable string — same as @ref name. */
                const String &toString() const { return d->name; }

                /** @brief Returns the underlying Data pointer. */
                const Data *data() const { return d; }

                // -- Integer <-> normalized float conversion helpers -------
                // These are used by the registered PCM formats' sample
                // conversion functions and are exposed as static utilities
                // so user-registered formats can reuse them.

                /**
                 * @brief Converts an integer sample value to a normalized float in [-1, 1].
                 *
                 * Maps @p [Min, Max] linearly onto @c [-1.0, 1.0].
                 *
                 * @tparam IntegerType The integer sample type.
                 * @tparam Min         Minimum value of the integer range.
                 * @tparam Max         Maximum value of the integer range.
                 * @param  value       The integer sample to convert.
                 * @return The corresponding normalized float value.
                 */
                template <typename IntegerType, IntegerType Min, IntegerType Max>
                static float integerToFloat(IntegerType value) {
                        static_assert(std::is_integral<IntegerType>::value, "IntegerType must be an integer.");
                        constexpr float min = static_cast<float>(Min);
                        constexpr float max = static_cast<float>(Max);
                        return ((static_cast<float>(value) - min) * 2.0f / (max - min)) - 1.0f;
                }

                /**
                 * @brief Converts an integer sample using the type's full numeric range.
                 * @tparam IntegerType The integer sample type.
                 * @param  value       The integer sample to convert.
                 * @return The corresponding normalized float value.
                 */
                template <typename IntegerType> static float integerToFloat(IntegerType value) {
                        static_assert(std::is_integral<IntegerType>::value, "IntegerType must be an integer.");
                        return integerToFloat<IntegerType, std::numeric_limits<IntegerType>::min(),
                                              std::numeric_limits<IntegerType>::max()>(value);
                }

                /**
                 * @brief Converts a normalized float in [-1, 1] to an integer sample value.
                 *
                 * Maps @c [-1.0, 1.0] linearly onto @p [Min, Max]; out-of-range
                 * floats clamp to the endpoints.
                 *
                 * @tparam IntegerType The integer sample type.
                 * @tparam Min         Minimum value of the integer range.
                 * @tparam Max         Maximum value of the integer range.
                 * @param  value       The normalized float sample.
                 * @return The corresponding integer sample value.
                 */
                template <typename IntegerType, IntegerType Min, IntegerType Max>
                static IntegerType floatToInteger(float value) {
                        static_assert(std::is_integral<IntegerType>::value, "IntegerType must be an integer.");
                        const float min = static_cast<float>(Min);
                        const float max = static_cast<float>(Max);
                        if (value <= -1.0f)
                                return Min;
                        else if (value >= 1.0f)
                                return Max;
                        return static_cast<IntegerType>((value + 1.0f) * 0.5f * (max - min) + min);
                }

                /**
                 * @brief Converts a float to an integer sample using the type's full range.
                 * @tparam IntegerType The integer sample type.
                 * @param  value       The normalized float sample.
                 * @return The corresponding integer sample value.
                 */
                template <typename IntegerType> static IntegerType floatToInteger(float value) {
                        static_assert(std::is_integral<IntegerType>::value, "IntegerType must be an integer.");
                        return floatToInteger<IntegerType, std::numeric_limits<IntegerType>::min(),
                                              std::numeric_limits<IntegerType>::max()>(value);
                }

                /**
                 * @brief Converts a buffer of integer samples to normalized floats.
                 *
                 * Handles endian conversion if @p InputIsBigEndian differs
                 * from the host byte order.
                 *
                 * @tparam IntegerType      The integer sample type.
                 * @tparam InputIsBigEndian True if @p inbuf is big-endian.
                 * @param  out     Destination float buffer.
                 * @param  inbuf   Source raw integer sample bytes.
                 * @param  samples Number of samples to convert.
                 */
                template <typename IntegerType, bool InputIsBigEndian>
                static void samplesToFloatImpl(float *out, const uint8_t *inbuf, size_t samples) {
                        static_assert(std::is_integral<IntegerType>::value, "IntegerType must be an integer.");
                        const IntegerType *in = reinterpret_cast<const IntegerType *>(inbuf);
                        for (size_t i = 0; i < samples; ++i) {
                                IntegerType val = *in++;
                                if constexpr (InputIsBigEndian != System::isBigEndian()) System::swapEndian(val);
                                *out++ = integerToFloat<IntegerType>(val);
                        }
                }

                /**
                 * @brief Converts a buffer of normalized floats to integer samples.
                 *
                 * Handles endian conversion if @p OutputIsBigEndian differs
                 * from the host byte order.
                 *
                 * @tparam IntegerType       The integer sample type.
                 * @tparam OutputIsBigEndian True if @p outbuf should be big-endian.
                 * @param  outbuf  Destination raw integer sample bytes.
                 * @param  in      Source float buffer.
                 * @param  samples Number of samples to convert.
                 */
                template <typename IntegerType, bool OutputIsBigEndian>
                static void floatToSamplesImpl(uint8_t *outbuf, const float *in, size_t samples) {
                        static_assert(std::is_integral<IntegerType>::value, "IntegerType must be an integer.");
                        IntegerType *out = reinterpret_cast<IntegerType *>(outbuf);
                        for (size_t i = 0; i < samples; ++i) {
                                IntegerType val = floatToInteger<IntegerType>(*in++);
                                if constexpr (OutputIsBigEndian != System::isBigEndian()) System::swapEndian(val);
                                *out++ = val;
                        }
                }

        private:
                const Data        *d = nullptr;
                static const Data *lookupData(ID id);
};

inline AudioFormat::AudioFormat(ID id) : d(lookupData(id)) {}

PROMEKI_NAMESPACE_END
