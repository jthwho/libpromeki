/**
 * @file      aacbitstream.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstdint>
#include <promeki/namespace.h>
#include <promeki/buffer.h>
#include <promeki/bufferview.h>
#include <promeki/error.h>
#include <promeki/audiodesc.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Parses and emits the MPEG-4 AAC AudioSpecificConfig blob.
 * @ingroup media
 *
 * @c AudioSpecificConfig (ISO/IEC 14496-3 §1.6.2.1) is the 2 to 5 byte
 * configuration descriptor that travels as the AAC sequence header in
 * FLV / RTMP and as the @c esds / @c mp4a sample description bytes in
 * ISO-BMFF.  Decoders consume it once at the start of a stream to
 * derive the sample rate, channel count, and AAC profile they should
 * operate in.
 *
 * @par Wire layout
 * Bit-aligned stream:
 *  - 5 bits: AudioObjectType (1 = Main, 2 = LC, 5 = SBR, 29 = PS, …)
 *  - 4 bits: SamplingFrequencyIndex (0xF means a 24-bit explicit
 *            frequency follows in the next 24 bits)
 *  - 4 bits: ChannelConfiguration (0 = PCE, 1 = mono, 2 = stereo, 6 = 5.1, …)
 *  - GASpecificConfig (for AOT 1,2,3,4,6,7,17,19,20,23): 3 bits — all
 *    zero in normal streams.
 *  - For HE-AAC v1 / v2 (explicit signaling): an additional 5 bits
 *    @c syncExtensionType (0x2B7) followed by extended AOT, ext SFI,
 *    and SBR / PS flags.
 *
 * @par Scope in v1
 * Parses LC (AOT=2), HE-AAC v1 (AOT=5 explicit SBR), and HE-AAC v2
 * (AOT=29 explicit PS).  Other AOTs round-trip the raw bytes via the
 * @ref rawConfig field but the structured fields are populated only
 * for the modes RTMP cares about.  Explicit @c extensionFrequency
 * (24-bit) is preserved on round-trip.
 */
struct AacDecoderConfig {
                /** @brief MPEG-4 AudioObjectType (5-bit field). */
                uint8_t audioObjectType = 2;        ///< Default: AAC-LC.

                /** @brief Index into the standard sample-rate table. */
                uint8_t samplingFrequencyIndex = 0;

                /** @brief Resolved sample rate in Hz (looked up from the index, or
                 *         the explicit 24-bit value for index 0xF). */
                uint32_t samplingFrequency = 0;

                /** @brief Channel configuration (0..7); 0 means "see PCE". */
                uint8_t channelConfiguration = 0;

                /** @brief Set when explicit SBR signaling is present (HE-AAC v1). */
                bool sbr = false;

                /** @brief Set when explicit PS signaling is present (HE-AAC v2). */
                bool ps = false;

                /** @brief Index for the SBR-decoded sample rate (when @ref sbr is set). */
                uint8_t extensionSamplingFrequencyIndex = 0;

                /** @brief Resolved SBR-decoded sample rate (when @ref sbr is set). */
                uint32_t extensionSamplingFrequency = 0;

                /** @brief Verbatim AudioSpecificConfig bytes — preserved through
                 *         a parse / serialize round-trip even when we don't
                 *         interpret every bit. */
                Buffer rawConfig;

                /**
                 * @brief Decode the bytes in @p payload into a config.
                 *
                 * @p payload must be a single-slice BufferView (multi-slice
                 * input is rejected with @ref Error::NotSupported, matching
                 * @ref Amf0Reader's contract).  Truncated input returns
                 * @ref Error::OutOfRange; an invalid sampling-frequency index
                 * returns @ref Error::CorruptData.
                 */
                static Error parse(const BufferView &payload, AacDecoderConfig &out);

                /**
                 * @brief Serialize the structured fields to AudioSpecificConfig
                 *        bytes appended to @p out.
                 *
                 * Emits a 2-byte LC config when @ref sbr / @ref ps are clear,
                 * extended encoding otherwise.  When @ref samplingFrequencyIndex
                 * is 15 (explicit), @ref samplingFrequency is emitted as the
                 * 24-bit field.
                 */
                Error serialize(Buffer &out) const;

                /** @brief Build a minimal LC-profile config from an AudioDesc. */
                static AacDecoderConfig fromAudioDesc(const AudioDesc &desc);

                /** @brief Convert to an AudioDesc (channels + rate). */
                AudioDesc toAudioDesc() const;

                /** @brief Returns the standard sample rate for a given index, or
                 *         0 when @p index is reserved (13, 14) or the explicit
                 *         marker (15). */
                static uint32_t indexToFrequency(uint8_t index);

                /** @brief Returns the index for a standard sample rate, or 15
                 *         when @p hz isn't in the table. */
                static uint8_t  frequencyToIndex(uint32_t hz);
};

/**
 * @brief Strips ADTS headers off raw AAC encoder output.
 * @ingroup media
 *
 * Some encoders emit AAC frames wrapped in 7-byte ADTS headers
 * (with optional 9-byte form when CRC is enabled); FLV / RTMP expect
 * raw access units without ADTS.  AdtsParser walks an ADTS-framed
 * input, validates each header's @c syncword, recovers an
 * @ref AacDecoderConfig from the first header's profile / SFI / CC
 * fields, and produces the concatenation of raw frames the
 * RTMP-side packetizer can ship as-is.
 *
 * Pure-ADTS inputs are recognized by the @c 0xFFF syncword.  Inputs
 * that don't begin with @c 0xFFF are returned unchanged with
 * @ref Error::Ok — i.e. "already raw" — so a producer that emits
 * either form can be funnelled through @c AdtsParser unconditionally.
 */
class AdtsParser {
        public:
                /**
                 * @brief Strip ADTS headers from @p in into @p outRaw and
                 *        recover a config in @p outCfg.
                 *
                 * @return @c Error::Ok on success (whether the input was
                 *         ADTS-framed or already raw — see class doc),
                 *         @c Error::CorruptData if any header's syncword
                 *         or layer is wrong, @c Error::OutOfRange on
                 *         truncation.
                 */
                static Error strip(const BufferView &in, Buffer &outRaw, AacDecoderConfig &outCfg);

                /** @brief True iff @p in begins with the ADTS @c 0xFFF syncword. */
                static bool isAdts(const BufferView &in);
};

PROMEKI_NAMESPACE_END
