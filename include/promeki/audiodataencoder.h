/**
 * @file      audiodataencoder.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include <promeki/namespace.h>
#include <promeki/list.h>
#include <promeki/error.h>
#include <promeki/audiodesc.h>
#include <promeki/buffer.h>

PROMEKI_NAMESPACE_BEGIN

class PcmAudioPayload;

/**
 * @brief Sample-domain binary data encoder for PCM audio payloads.
 * @ingroup proav
 *
 * Audio analogue of @ref ImageDataEncoder.  Stamps 64-bit opaque
 * payloads into a chosen channel of a @ref PcmAudioPayload as a
 * Manchester-encoded 76-bit codeword (4 sync bits, 64 payload bits,
 * 8 CRC-8/AUTOSAR bits), where every bit occupies a fixed integer
 * number of audio samples.  The wire format is sample-accurate when
 * the audio path does not resample, and survives ordinary
 * sample-rate conversion thanks to Manchester's mid-bit transition
 * and the integrate-and-compare demodulator on the
 * @ref AudioDataDecoder side.
 *
 * @par Wire format
 * Each Item produces an @c N -sample run laid out as follows, where
 * @c S = samplesPerBit, @c H = S/2, and the trailing pad fills any
 * additional samples in the Item up to its @c sampleCount:
 *
 * @verbatim
 *   |  4 sync bits  |       64 payload bits        |  8 CRC bits  | pad |
 *   |  1010         | MSB ........................ | MSB ... LSB  |     |
 *   each bit = H samples of (+A or -A) followed by H samples of
 *   the opposite sign.  bit '1' = +A, then -A; bit '0' = -A, then +A.
 * @endverbatim
 *
 * - **Sync nibble**: `0b1010` (transmitted MSB-first).
 * - **Payload**: 64-bit value, transmitted MSB-first (bit 63 first).
 * - **CRC**: CRC-8/AUTOSAR over the 8 payload bytes interpreted
 *   big-endian — byte 0 is bits 56..63 of the payload, byte 7 is
 *   bits 0..7.  Transmitted MSB-first.
 * - **Trailing pad**: samples in the Item beyond the @c 76*S codeword
 *   are written as silence (@c 0.0f converted to the format's
 *   neutral value — e.g. @c 128 for unsigned 8-bit).
 *
 * Bits cells are constant amplitude in each half-bit (the encoder
 * does not band-limit the transition); this keeps DC zero, gives
 * Manchester's strong mid-bit timing edge, and lets the encoder use
 * a single @c memcpy per half-bit on planar formats.  The Manchester
 * fundamental frequency is @c sampleRate/(2H) — at the default
 * @c samplesPerBit=8 that is 6 kHz at 48 kHz, comfortably below the
 * passband edge of any reasonable sample-rate converter.
 *
 * @par Channel layout
 * Items target one channel each.  The encoder handles both
 * interleaved and planar PCM transparently using
 * @ref AudioDesc::channelBufferOffset and
 * @ref AudioDesc::bytesPerSampleStride.  The same channel index
 * means "channel @c N of the multi-channel stream" regardless of
 * planar / interleaved layout.
 *
 * @par Per-format value mapping
 * The encoder builds two single-channel "primer" buffers at
 * construction (@c +A and @c -A in the target sample format,
 * @c H samples each) plus a one-sample silence primer for the pad.
 * Floating-point formats use the amplitude directly; integer
 * formats use @ref AudioFormat::floatToSamples to perform the
 * round to the format's quantisation grid.  After construction the
 * hot path is a fixed sequence of @c memcpy / strided byte copy
 * calls — 152 half-bits per Item per channel, plus the pad fill.
 *
 * @par Lifetime and reuse
 * Construct one encoder per (AudioDesc, samplesPerBit, amplitude)
 * triple and reuse it across many payloads.  The encoder owns its
 * primer buffers internally; @ref encode is reentrant on a single
 * instance only with respect to itself (concurrent @c encode calls
 * on the same instance must be externally synchronized).  Use
 * @ref isValid to detect a construction failure (descriptor invalid,
 * compressed format, samplesPerBit out of range, etc.).
 *
 * @par Example
 * @code
 * AudioDataEncoder enc(payload->desc());  // defaults: 8 SPB, 0.1 amp
 * if(!enc.isValid()) return Error::Invalid;
 *
 * AudioDataEncoder::Item items[] = {
 *     { 0, payload->sampleCount(), 0, frameId },   // ch 0: rolling frame ID
 *     { 0, payload->sampleCount(), 1, channelId }, // ch 1: channel marker
 * };
 * Error err = enc.encode(*payload, items);
 * @endcode
 *
 * @par Thread Safety
 * Conditionally thread-safe.  Distinct instances may be used
 * concurrently; concurrent access to a single instance must be
 * externally synchronized.
 *
 * @see AudioDataDecoder, ImageDataEncoder, CRC
 */
class AudioDataEncoder {
        public:
                /** @brief Number of sync bits at the start of every codeword. */
                static constexpr uint32_t SyncBits = 4;
                /** @brief Number of payload bits in every codeword. */
                static constexpr uint32_t PayloadBits = 64;
                /** @brief Number of CRC bits in every codeword. */
                static constexpr uint32_t CrcBits = 8;
                /** @brief Total bits per codeword (sync + payload + CRC). */
                static constexpr uint32_t BitsPerPacket = SyncBits + PayloadBits + CrcBits;

                /**
                 * @brief Sync nibble bit pattern @c 1010 (MSB-first).
                 *
                 * Manchester encodes this as half-bit run-length
                 * @c (H, 2H, 2H, 2H, H) so the decoder gets three
                 * independent same-bit-width measurements bounded on
                 * both sides by encoder transitions.
                 */
                static constexpr uint8_t SyncNibble = 0xAu;

                /**
                 * @brief Default samples-per-bit (8).
                 *
                 * 76 × 8 = 608 samples per codeword.  At 48 kHz the
                 * Manchester fundamental sits at 6 kHz, well inside
                 * the flat passband of any reasonable SRC.  Fits in
                 * one 60p audio frame at 48 kHz (800 samples) or
                 * 44.1 kHz (735 samples) with room to spare.
                 */
                static constexpr uint32_t DefaultSamplesPerBit = 8;

                /**
                 * @brief Minimum samples-per-bit (4).
                 *
                 * Below 4 the Manchester fundamental moves above the
                 * Nyquist of common destination rates (e.g. 24 kHz
                 * fundamental at @c samplesPerBit=2 doesn't survive a
                 * 48 k→44.1 k SRC).  Must be even.
                 */
                static constexpr uint32_t MinSamplesPerBit = 4;

                /**
                 * @brief Maximum samples-per-bit (64).
                 *
                 * Larger values are physically valid but produce
                 * codewords longer than typical per-frame audio
                 * windows.  Cap exists to keep the per-Item upper
                 * bound predictable.
                 */
                static constexpr uint32_t MaxSamplesPerBit = 64;

                /**
                 * @brief Default linear amplitude (0.1 ≈ -20 dBFS).
                 *
                 * Quiet enough not to be obnoxious if accidentally
                 * routed to speakers, and well above the noise floor
                 * of any plausible SRC for reliable demodulation.
                 */
                static constexpr float DefaultAmplitude = 0.1f;

                /**
                 * @brief One stamp request: write a 64-bit payload into
                 *        a contiguous run of samples on a single channel.
                 *
                 * @c firstSample and @c sampleCount are in
                 * payload-sample units (one unit per sample frame, the
                 * same units as @ref AudioPayload::sampleCount).  The
                 * codeword occupies the first @c BitsPerPacket *
                 * samplesPerBit() samples of the run; any additional
                 * samples up to @c sampleCount are zero-filled.
                 */
                struct Item {
                                /// First sample (per-channel) at which the codeword starts.
                                uint64_t firstSample = 0;
                                /// Total samples reserved for the Item; trailing bytes are zeroed.
                                uint64_t sampleCount = 0;
                                /// Zero-based channel index to stamp.
                                uint32_t channel = 0;
                                /// Opaque 64-bit payload.
                                uint64_t payload = 0;
                };

                /** @brief Constructs an invalid encoder. */
                AudioDataEncoder() = default;

                /**
                 * @brief Constructs an encoder for the given audio descriptor.
                 *
                 * @param desc           Audio descriptor.  Must be a valid
                 *                       PCM format (@c isCompressed()==false).
                 * @param samplesPerBit  Samples per bit cell.  Must satisfy
                 *                       @c MinSamplesPerBit ≤ @c samplesPerBit
                 *                       ≤ @c MaxSamplesPerBit and be even.
                 *                       Defaults to @ref DefaultSamplesPerBit.
                 * @param amplitude      Linear amplitude per sample
                 *                       (@c 0..1).  Defaults to
                 *                       @ref DefaultAmplitude.
                 *
                 * After construction call @ref isValid to check whether
                 * the encoder is usable.
                 */
                explicit AudioDataEncoder(const AudioDesc &desc, uint32_t samplesPerBit = DefaultSamplesPerBit,
                                          float amplitude = DefaultAmplitude);

                /** @brief Returns @c true if the encoder is ready to use. */
                bool isValid() const { return _valid; }

                /** @brief Returns the configured samples-per-bit. */
                uint32_t samplesPerBit() const { return _samplesPerBit; }

                /** @brief Returns the configured linear amplitude. */
                float amplitude() const { return _amplitude; }

                /** @brief Returns the audio descriptor the encoder was built for. */
                const AudioDesc &desc() const { return _desc; }

                /**
                 * @brief Returns the codeword length in samples.
                 *
                 * Equivalent to @c BitsPerPacket * samplesPerBit().
                 * The smallest @c sampleCount an Item may carry.
                 */
                uint64_t packetSamples() const {
                        return static_cast<uint64_t>(BitsPerPacket) * static_cast<uint64_t>(_samplesPerBit);
                }

                /**
                 * @brief Stamps every Item's payload into the supplied payload.
                 *
                 * The payload's audio descriptor must compare equal to
                 * the one supplied at construction.  Each Item must
                 * satisfy:
                 *   - @c channel < desc().channels()
                 *   - @c sampleCount ≥ packetSamples()
                 *   - @c firstSample + @c sampleCount ≤ inout.sampleCount()
                 *
                 * Items that fail any of these are rejected with
                 * @ref Error::OutOfRange (geometry) or
                 * @ref Error::InvalidArgument (channel out of range).
                 *
                 * @param inout PCM payload to write into.  Must be
                 *              allocated; the data is modified in
                 *              place.
                 * @param items List of Items to stamp.
                 * @return @c Error::Ok on success.
                 */
                Error encode(PcmAudioPayload &inout, const List<Item> &items) const;

                /** @brief Convenience overload for a single Item. */
                Error encode(PcmAudioPayload &inout, const Item &item) const;

                /**
                 * @brief Computes the CRC-8/AUTOSAR over an 8-byte
                 *        big-endian view of @p payload.
                 *
                 * Exposed so the decoder can recompute the CRC
                 * independently and so test code can construct
                 * "expected" codewords without instantiating an
                 * encoder.
                 */
                static uint8_t computeCrc(uint64_t payload);

        private:
                AudioDesc _desc;
                uint32_t  _samplesPerBit = 0;
                float     _amplitude = 0.0f;
                bool      _valid = false;

                // Bytes per single sample of one channel, in the target format.
                size_t _bytesPerSample = 0;
                // Stride (bytes) between consecutive samples of the
                // same channel.  Equals _bytesPerSample for planar,
                // _bytesPerSample * channels for interleaved.
                size_t _channelStride = 0;

                // Pre-built half-bit primers and one-sample silence in
                // the target sample format.  Each primer holds
                // _samplesPerBit/2 single-channel samples; silence is
                // a single sample (zero amplitude, format-encoded).
                Buffer _posHalf;
                Buffer _negHalf;
                Buffer _silenceSample;

                Error stampOne(uint8_t *channelBase, const Item &item) const;
};

PROMEKI_NAMESPACE_END
