/**
 * @file      smpte302m.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_PROAV
#include <cstdint>
#include <promeki/namespace.h>
#include <promeki/audiodesc.h>
#include <promeki/audioformat.h>
#include <promeki/buffer.h>
#include <promeki/bufferview.h>
#include <promeki/error.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief SMPTE ST 302M-2007 mapping of AES3 / linear PCM audio data
 *        into an MPEG-2 Transport Stream PES.
 * @ingroup proav
 *
 * SMPTE 302M is the broadcast-standard way to carry uncompressed
 * audio inside an MPEG-2 Transport Stream.  Each PES @c stream_type
 * is @c 0x06 (PES private data) with a @c registration_descriptor
 * carrying the four-byte @c format_identifier @c "BSSD"
 * (@ref MpegTs::RegFormatSmpte302M).
 *
 * @par Capabilities and limits
 *  - Word rate is fixed at @b 48 @b kHz (§5.4).
 *  - Word size may be 16, 20, or 24 bits (§5.3).
 *  - 1-4 AES3 streams per PES (= 2 / 4 / 6 / 8 audio channels, §5.2).
 *    Higher channel counts require multiple parallel PESes.
 *  - A 4-byte SMPTE 302M header precedes the bit-packed audio data
 *    in every PES payload (§6.6, Table 1).
 *
 * @par Wire layout — header
 * @code
 *   bytes 0-1:  audio_packet_size (uimsbf 16)  bytes of audio after header
 *   byte 2:     number_channels (2)  | channel_identification_hi (6 bits)
 *   byte 3:     channel_identification_lo (2 bits) | bits_per_sample (2)
 *                                                 | alignment_bits (4 = 0)
 * @endcode
 *
 * @par Wire layout — payload (per AES3 frame, all M streams interleaved)
 * The 302M payload is a tight bit-stream of "302M data words", one
 * per AES3 subframe.  Each 302M data word is @c (bits_per_sample + 4)
 * bits long: the PCM sample's bits LSB-first, then @c V, @c U,
 * @c C, and finally @c F as the MSB.  Subframes within one AES3
 * frame group are ordered (stream 1 A, stream 1 B, stream 2 A,
 * stream 2 B, ..., stream M A, stream M B).
 *
 * The bit-stream is emitted MSB-first into the PES bytes (the
 * standard MPEG bit-ordering convention), so the LSB of each PCM
 * sample lands in the most-significant bit of its first byte.
 *
 * @par Compatibility with PCM audio formats
 * The packer accepts these interleaved PCM @ref AudioFormat IDs:
 *  - @c PCMI_S16LE / @c PCMI_S16BE              → 16-bit (@c bits_per_sample=00).
 *  - @c PCMI_S24LE / @c PCMI_S24BE              → 24-bit (@c bits_per_sample=10).
 *  - @c PCMI_S32LE / @c PCMI_S32BE              → 24-bit (high 24 bits used; low 8 dropped).
 *  - @c PCMI_S24LE_HB32 / @c PCMI_S24LE_LB32 / BE counterparts → 24-bit (extract 24 of the 32).
 *
 * The 20-bit format isn't exposed via @ref AudioFormat; callers
 * that want 20-bit @ref BitsPerSample::Bits20 must produce raw
 * samples and use @ref packRaw.
 *
 * @par Thread Safety
 * All public methods are pure functions of their arguments and may
 * be called concurrently.  The @ref Packer carries a 192-frame
 * @c F-bit phase counter and is therefore conditionally thread-safe
 * (one producer per instance).
 */
class Smpte302M {
        public:
                /** @brief @c bits_per_sample field values from §6.7 Table 1. */
                enum BitsPerSample : uint8_t {
                        Bits16 = 0, ///< 16-bit PCM (5 bytes per AES3 frame for 2 channels).
                        Bits20 = 1, ///< 20-bit PCM (6 bytes per AES3 frame for 2 channels).
                        Bits24 = 2, ///< 24-bit PCM (7 bytes per AES3 frame for 2 channels).
                };

                /** @brief Fixed 302M header length (4 bytes). */
                static constexpr size_t HeaderSize = 4;

                /** @brief Required AES3 sample rate (48 kHz). */
                static constexpr float RequiredSampleRate = 48000.0f;

                /** @brief Maximum channel count per PES (4 AES3 streams). */
                static constexpr unsigned MaxChannels = 8;

                /** @brief Minimum channel count per PES (1 AES3 stream). */
                static constexpr unsigned MinChannels = 2;

                /**
                 * @brief Maps an @ref AudioFormat to a @ref BitsPerSample value.
                 *
                 * @return The matching code, or @c -1 when the format isn't
                 *         supported by 302M (compressed formats, planar PCM,
                 *         unsupported bit depths, float formats).
                 */
                static int bitsPerSampleCode(const AudioFormat &fmt);

                /**
                 * @brief @c true when @p fmt can be packed by @ref pack.
                 */
                static bool isFormatSupported(const AudioFormat &fmt);

                /**
                 * @brief Bytes per AES3 frame across all @p channels.
                 *
                 * One AES3 frame = one A subframe + one B subframe per AES3
                 * stream = @c channels subframes total at @c (bits + 4) bits
                 * each, packed into bytes.
                 */
                static size_t bytesPerAes3Frame(BitsPerSample bps, unsigned channels);

                /**
                 * @brief Bytes per PES payload for @p sampleCount AES3 frames.
                 *
                 * @return @ref HeaderSize plus @ref bytesPerAes3Frame ×
                 *         @p sampleCount, or 0 when @p channels is invalid.
                 */
                static size_t payloadSize(BitsPerSample bps, unsigned channels, size_t sampleCount);

                /**
                 * @brief Bit flags for one AES3 subframe's V / U / C bits.
                 *
                 * Pack 3 bits into a single byte so callers can pass a
                 * compact per-subframe array (one byte per A or B
                 * subframe per AES3 frame) to @ref pack.  The F bit is
                 * synthesised internally from @c blockPhase and isn't
                 * exposed here.
                 */
                enum VucBits : uint8_t {
                        VucNone = 0,         ///< All zero (default for synthesized PCM).
                        VucValidity = 1 << 0, ///< V bit (1 = sample is not valid linear PCM).
                        VucUser     = 1 << 1, ///< U bit (1 = user-data carried).
                        VucChannelStatus = 1 << 2, ///< C bit (1 = channel-status carried).
                };

                /**
                 * @brief Per-subframe V/U/C source for @ref pack.
                 *
                 * The pointer is interpreted as a contiguous array of
                 * @c (sampleCount × channels) @ref VucBits bytes, in
                 * the same A1 B1 A2 B2 ... channel-pair ordering as
                 * the PCM data: byte @c (f × channels + c) supplies
                 * the V/U/C bits for AES3 frame @c f, channel @c c.
                 *
                 * Pass @c nullptr for the "all zeros" default (matches
                 * synthesized linear PCM).
                 */
                using VucSource = const uint8_t *;

                /**
                 * @brief Packs interleaved PCM samples into a 302M PES payload.
                 *
                 * The input must carry exactly @ref AudioDesc::channels
                 * channels in the layout described by
                 * @ref AudioDesc::format, with @ref AudioDesc::sampleRate
                 * equal to @ref RequiredSampleRate.
                 *
                 * @param pcm                Interleaved PCM bytes.  Must hold
                 *                           @c desc.bufferSize(sampleCount)
                 *                           bytes; that contract is the
                 *                           caller's to honour.
                 * @param desc               Descriptor of @p pcm — its
                 *                           @c sampleRate must be 48 kHz and
                 *                           its @c format must be
                 *                           @ref isFormatSupported.
                 * @param sampleCount        Number of AES3 frames (= per-channel
                 *                           sample count) to emit.
                 * @param blockPhase         Caller-maintained 192-frame phase
                 *                           counter.  Incremented by
                 *                           @c sampleCount on return (mod 192)
                 *                           so successive calls can stamp the
                 *                           AES3 block @c F bit at the right
                 *                           frame.  Pass an initial value of
                 *                           0 at stream start; the packer
                 *                           sets @c F=1 whenever the counter
                 *                           rolls past 0.
                 * @param firstChannelId     Value for the @c channel_identification
                 *                           field — the audio channel number of
                 *                           the first sample in this PES (§6.6).
                 *                           Typically 0 for the first 302M
                 *                           PES in a program.
                 * @param outPesPayload      Receives the full PES payload
                 *                           (header + packed samples).  The
                 *                           buffer's existing contents are
                 *                           overwritten and its logical size
                 *                           is set to the encoded length.
                 * @param vuc                Optional V/U/C source array
                 *                           — one @ref VucBits byte per
                 *                           AES3 subframe in the same
                 *                           @c (f × channels + c) order
                 *                           as @p pcm.  Pass @c nullptr
                 *                           to default every subframe's
                 *                           V, U, and C bits to 0
                 *                           (correct for synthesized
                 *                           linear PCM that has no
                 *                           AES3 channel-status metadata).
                 * @return @c Error::Ok on success,
                 *         @c Error::InvalidArgument when @p desc / @p
                 *         sampleCount / @p firstChannelId are out of range,
                 *         @c Error::NotSupported when @p desc.format()
                 *         is unsupported, @c Error::NoMem on allocation
                 *         failure.
                 */
                static Error pack(const void *pcm, const AudioDesc &desc, size_t sampleCount,
                                  uint32_t &blockPhase, uint8_t firstChannelId,
                                  Buffer &outPesPayload, VucSource vuc = nullptr);

                /**
                 * @brief Decoded fields of a parsed 302M PES payload.
                 */
                struct ParsedHeader {
                                uint16_t      audioPacketSize = 0;
                                uint8_t       numberChannels = 0;        ///< Raw 2-bit field (0..3).
                                unsigned      channels = 0;              ///< 2 / 4 / 6 / 8.
                                uint8_t       channelIdentification = 0; ///< First-channel index.
                                BitsPerSample bitsPerSample = Bits16;
                };

                /**
                 * @brief Decodes the 4-byte 302M header at the start of @p in.
                 *
                 * @param in   PES payload (must be at least @ref HeaderSize bytes).
                 * @param out  Receives the decoded fields.
                 * @return @c Error::Ok on success,
                 *         @c Error::OutOfRange on truncation,
                 *         @c Error::CorruptData on a reserved
                 *         @c bits_per_sample value.
                 */
                static Error parseHeader(const BufferView &in, ParsedHeader *out);

                /**
                 * @brief Unpacks a 302M PES payload into interleaved PCM.
                 *
                 * Always emits @ref PCMI_S16LE / @ref PCMI_S24LE depending
                 * on @c bits_per_sample (20-bit input is widened to 24-bit
                 * output with the low 4 bits zeroed, mirroring the
                 * 16-bit-in-20-bit convention from §5.5).
                 *
                 * @param in            PES payload, including the 4-byte
                 *                      302M header.
                 * @param outPcm        Receives the decoded PCM bytes.
                 * @param outDesc       Receives a populated @ref AudioDesc
                 *                      (48 kHz, channel count from header,
                 *                      format @c PCMI_S16LE or
                 *                      @c PCMI_S24LE).
                 * @param outSampleCount Receives the per-channel sample
                 *                      count.
                 * @param outFirstChannelId Receives the @c channel_identification
                 *                      field; nullable when the caller
                 *                      doesn't care.
                 * @return @c Error::Ok on success,
                 *         @c Error::OutOfRange when the payload's size
                 *         doesn't match its declared layout,
                 *         @c Error::CorruptData on a reserved bps value.
                 */
                static Error parse(const BufferView &in, Buffer &outPcm, AudioDesc &outDesc,
                                   size_t &outSampleCount, uint8_t *outFirstChannelId = nullptr);

                /**
                 * @brief Variant of @ref parse that also captures the
                 *        per-subframe V/U/C bits into @p outVuc.
                 *
                 * @p outVuc is resized to @c sampleCount × channels
                 * bytes and each byte is one @ref VucBits value (F is
                 * not surfaced — clients that want the AES3 block
                 * boundary should recover it from the PES PTS).
                 *
                 * Pass @c nullptr for @p outVuc to skip V/U/C
                 * collection (delegates to the simpler @ref parse).
                 */
                static Error parseWithVuc(const BufferView &in, Buffer &outPcm, AudioDesc &outDesc,
                                          size_t &outSampleCount, Buffer *outVuc,
                                          uint8_t *outFirstChannelId = nullptr);
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_PROAV
