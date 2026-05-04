/**
 * @file      audiodatadecoder.h
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
#include <promeki/audiodataencoder.h>

PROMEKI_NAMESPACE_BEGIN

class PcmAudioPayload;

/**
 * @brief Sample-domain binary data decoder for PCM audio payloads.
 * @ingroup proav
 *
 * Audio analogue of @ref ImageDataDecoder.  Recovers the 64-bit
 * payload + CRC + sync nibble emitted by @ref AudioDataEncoder from
 * a contiguous run of audio samples on a single channel.  The
 * decoder works at sample-accurate alignment when no resampling has
 * occurred and tolerates ±25 % per-bit drift introduced by ordinary
 * sample-rate conversion via a sync-nibble run-length measurement
 * and Manchester integrate-and-compare demodulation.
 *
 * @par Algorithm
 *  1. Extract @c sampleCount samples of the requested channel into
 *     a normalized float array (@ref AudioFormat::samplesToFloat
 *     handles every PCM format uniformly).
 *  2. Locate the sync nibble: search a small window around
 *     @c firstSample for the first positive zero-crossing-going
 *     transition, then measure the next three transitions
 *     (@c (+→-, -→+, +→-)).  Reject if any run is more than 25 %
 *     off the average — a strong signal that the band is corrupted
 *     or mis-located.
 *  3. Validate the recovered samples-per-bit against the
 *     constructor hint with a ±50 % tolerance band.
 *  4. For each of the 76 bit positions, integrate the first half-bit
 *     and the second half-bit.  bit '1' if first > second, bit '0'
 *     otherwise.  Integration is what makes Manchester robust to the
 *     low-pass filtering an SRC introduces — the smoothed transition
 *     averages out symmetrically.
 *  5. Verify the recovered sync nibble equals @c 0xA, recompute the
 *     CRC over the 8 payload bytes (big-endian), and compare to the
 *     decoded CRC.
 *
 * @par Lifetime and reuse
 * Construct one decoder per (AudioDesc, samplesPerBit hint) pair
 * and reuse it across many payloads.  @ref decode is reentrant on a
 * single instance only with respect to itself.
 *
 * @par Example
 * @code
 * AudioDataDecoder dec(payload->desc());
 * AudioDataDecoder::Band band{0, payload->sampleCount(), 0};
 * auto item = dec.decode(*payload, band);
 * if(item.error.isOk()) {
 *     // item.payload, item.decodedCrc, item.samplesPerBit
 * }
 * @endcode
 *
 * @par Thread Safety
 * Conditionally thread-safe.  Distinct instances may be used
 * concurrently; concurrent access to a single instance must be
 * externally synchronized.
 *
 * @see AudioDataEncoder, ImageDataDecoder, CRC
 */
class AudioDataDecoder {
        public:
                /** @brief Number of bits per codeword (mirrors @ref AudioDataEncoder::BitsPerPacket). */
                static constexpr uint32_t BitsPerPacket = AudioDataEncoder::BitsPerPacket;
                /** @brief Number of sync bits (mirrors encoder). */
                static constexpr uint32_t SyncBits = AudioDataEncoder::SyncBits;
                /** @brief Number of payload bits (mirrors encoder). */
                static constexpr uint32_t PayloadBits = AudioDataEncoder::PayloadBits;
                /** @brief Number of CRC bits (mirrors encoder). */
                static constexpr uint32_t CrcBits = AudioDataEncoder::CrcBits;
                /** @brief Sync nibble bit pattern (mirrors encoder). */
                static constexpr uint8_t SyncNibble = AudioDataEncoder::SyncNibble;

                /**
                 * @brief One sample-region request: decode a codeword
                 *        from a contiguous run of samples on one channel.
                 */
                struct Band {
                                /// First sample of the codeword search window.
                                uint64_t firstSample = 0;
                                /// Number of samples available for decoding.
                                uint64_t sampleCount = 0;
                                /// Zero-based channel index to read from.
                                uint32_t channel = 0;
                };

                /**
                 * @brief Result of decoding one Band or one streaming packet.
                 *
                 * @c error reports the outcome.  On success
                 * (@c error.isOk()) all fields are valid.  On a
                 * decode failure, @c error names the failure mode
                 * and any partially-recovered values are populated
                 * for diagnostics (e.g. @c decodedSync may be
                 * non-zero when only the CRC mismatched).
                 */
                struct DecodedItem {
                                /// Outcome of the decode (Ok / CorruptData / etc.).
                                Error error = Error::Ok;
                                /// Decoded 4-bit sync nibble.  @c 0xA on success.
                                uint8_t decodedSync = 0;
                                /// Decoded 8-bit CRC value.
                                uint8_t decodedCrc = 0;
                                /// CRC value the decoder computed from the recovered payload.
                                uint8_t expectedCrc = 0;
                                /// Decoded 64-bit payload (MSB-first).
                                uint64_t payload = 0;
                                /// Sub-sample-accurate samples-per-bit measured from the sync nibble.
                                double samplesPerBit = 0.0;
                                /// Sample index (within the Band, for the per-band
                                /// @ref decode entry points) of the first positive
                                /// sync sample.  Always non-negative.
                                uint64_t syncStartSample = 0;
                                /// Stream-absolute sample position of the first
                                /// positive sync sample, measured from sample 0
                                /// of the @ref StreamState's first @ref decodeAll
                                /// call.  Populated only by @ref decodeAll;
                                /// @c -1 for items returned by the per-band
                                /// @ref decode entry points.  Signed because
                                /// downstream consumers may rebase the anchor
                                /// against an external reference (e.g. an audio
                                /// MediaTimeStamp), at which point packets that
                                /// straddle the rebase point land before the
                                /// origin.
                                int64_t streamSampleStart = -1;
                                /// Number of samples this packet occupied,
                                /// computed from the measured pitch as
                                /// @c round(samplesPerBit * BitsPerPacket).
                                /// Reflects the actual on-wire span the
                                /// codeword took — a number lower than
                                /// @c BitsPerPacket * AudioDataEncoder::DefaultSamplesPerBit
                                /// indicates the packet was compressed in
                                /// time (downstream rate change), higher
                                /// indicates expansion.  Zero when no sync
                                /// was found (the @c samplesPerBit
                                /// measurement isn't available).
                                int64_t packetSampleCount = 0;
                };

                /** @brief Per-Band results list. */
                using DecodedList = List<DecodedItem>;

                /** @brief Constructs an invalid decoder. */
                AudioDataDecoder() = default;

                /**
                 * @brief Constructs a decoder for the given audio descriptor.
                 *
                 * @param desc                  Audio descriptor.  Must
                 *                              be valid PCM (not compressed).
                 * @param expectedSamplesPerBit Encoder samples-per-bit
                 *                              used to drive the ±50 %
                 *                              acceptance band.  Pass
                 *                              the same value the
                 *                              encoder used.  Defaults
                 *                              to @ref AudioDataEncoder::DefaultSamplesPerBit.
                 */
                explicit AudioDataDecoder(const AudioDesc &desc,
                                          uint32_t expectedSamplesPerBit = AudioDataEncoder::DefaultSamplesPerBit);

                /** @brief Returns @c true if the decoder is ready to use. */
                bool isValid() const { return _valid; }

                /** @brief Returns the configured expected samples-per-bit. */
                uint32_t expectedSamplesPerBit() const { return _expectedSamplesPerBit; }

                /** @brief Returns the audio descriptor the decoder was built for. */
                const AudioDesc &desc() const { return _desc; }

                /**
                 * @brief Decodes a list of Bands.
                 *
                 * @param payload PCM payload to read from.
                 * @param bands   Bands to decode.
                 * @param out     Filled with one @ref DecodedItem per band.
                 * @return @c Error::Ok on success, or an error code if
                 *         the decoder is invalid or the payload
                 *         descriptor doesn't match.  Per-band errors
                 *         are reported in the corresponding
                 *         @c DecodedItem::error.
                 */
                Error decode(const PcmAudioPayload &payload, const List<Band> &bands, DecodedList &out) const;

                /** @brief Convenience overload that decodes a single Band. */
                DecodedItem decode(const PcmAudioPayload &payload, const Band &band) const;

                /**
                 * @brief Decodes a codeword from a single-channel float buffer.
                 *
                 * Bypasses the per-format @ref AudioFormat::samplesToFloat
                 * extraction step the @ref PcmAudioPayload overloads
                 * perform — useful for one-shot decode of a known band
                 * already in normalized float form.  Decodes only the
                 * first complete codeword in the buffer; downstream
                 * callers that need to drain a streaming buffer should
                 * use @ref decodeAll instead.
                 *
                 * The buffer is treated as the entire search window:
                 * sample 0 is the start of the band, sample @c count-1
                 * is the last valid sample.  Sub-sample-accurate sync
                 * localisation works exactly as in the
                 * @ref PcmAudioPayload overloads.
                 *
                 * @param samples Pointer to a contiguous run of @c count
                 *                normalized float samples (one channel).
                 * @param count   Number of samples in @p samples.
                 * @return A populated @ref DecodedItem.  On failure
                 *         @c error names the failure mode and partial
                 *         fields may be populated for diagnostics.
                 */
                DecodedItem decode(const float *samples, size_t count) const;

                /**
                 * @brief Per-channel rolling state used by @ref decodeAll.
                 *
                 * Holds the audio samples that have arrived but
                 * haven't yet been consumed by a decoded packet, plus
                 * the absolute stream-sample anchor of the buffer's
                 * first sample.  One @c StreamState per concurrent
                 * channel — the decoder itself stays stateless so a
                 * single instance can serve any number of channels.
                 *
                 * The buffer is bounded at
                 * @ref kStreamBufferMaxSamples; on overflow the
                 * oldest half of the buffer is dropped and the
                 * anchor is advanced so subsequent
                 * @ref DecodedItem::streamSampleStart values stay
                 * consistent with the cumulative input stream.
                 */
                struct StreamState {
                                /// Samples that have been pushed via
                                /// @ref decodeAll but not yet consumed by a
                                /// successful decode.  Front of the list is
                                /// the oldest sample.
                                List<float> buffer;
                                /// Absolute sample position of @c buffer[0]
                                /// in the cumulative input stream — i.e. how
                                /// many samples have ever been pushed via
                                /// @ref decodeAll plus consumed since stream
                                /// inception.  Updated whenever samples are
                                /// dropped from the front of the buffer
                                /// (decoded or trimmed).
                                int64_t sampleAnchor = 0;
                };

                /**
                 * @brief Maximum samples held in a single @ref StreamState
                 *        before the oldest half is trimmed.
                 *
                 * Bounded so a sustained decode failure (silent
                 * channel, hostile input) can't grow memory without
                 * limit.  Sized to comfortably hold a few codewords
                 * plus one frame's worth of headroom.
                 */
                static constexpr size_t kStreamBufferMaxSamples = 8192;

                /**
                 * @brief Decodes every complete codeword reachable in the
                 *        rolling buffer of @p state, after appending @p count
                 *        new samples.
                 *
                 * Streaming entry point for callers that keep a
                 * cross-call rolling buffer (the inspector, network
                 * receivers, etc.) and need to decode every codeword
                 * the buffer contains, including ones that straddle
                 * the boundary between two consecutive @ref decodeAll
                 * calls.
                 *
                 * The append-then-decode loop:
                 *   1. Append @p newSamples to @c state.buffer.
                 *   2. Run sync localisation and bit demodulation on
                 *      the buffer.  If sync isn't found, leave the
                 *      buffer alone and return — more samples may
                 *      complete the codeword on the next call.
                 *   3. If sync is found but the buffer doesn't have
                 *      enough samples for the full 76-bit codeword,
                 *      same: leave the buffer and return.
                 *   4. Otherwise emit a @ref DecodedItem to @p out
                 *      (whether successful or not — CRC and sync-byte
                 *      failures are reported via
                 *      @ref DecodedItem::error so the caller can
                 *      distinguish), advance the buffer past the
                 *      packet, and loop.
                 *   5. After the loop, trim the buffer if it exceeds
                 *      @ref kStreamBufferMaxSamples (drops the
                 *      oldest samples, advances the anchor).
                 *
                 * Each emitted @ref DecodedItem carries a stream-
                 * absolute @ref DecodedItem::streamSampleStart
                 * (sample 0 = the very first sample ever pushed
                 * through this @ref StreamState).  Items remain in
                 * insertion order, so the last item is the
                 * most recently completed packet.
                 *
                 * @param state       Per-channel rolling state.
                 * @param newSamples  Pointer to @p count fresh samples
                 *                    to append.  May be @c nullptr
                 *                    when @p count is zero — useful
                 *                    for pumping decodes through a
                 *                    pure trim cycle.
                 * @param count       Number of samples in @p newSamples.
                 * @param out         Filled with one @ref DecodedItem
                 *                    per completed packet (success or
                 *                    failure).  Cleared on entry.
                 */
                void decodeAll(StreamState &state, const float *newSamples, size_t count, DecodedList &out) const;

        private:
                AudioDesc _desc;
                uint32_t  _expectedSamplesPerBit = 0;
                uint32_t  _samplesPerBitMin = 0;
                uint32_t  _samplesPerBitMax = 0;
                bool      _valid = false;

                DecodedItem decodeOne(const PcmAudioPayload &payload, const Band &band) const;
                DecodedItem decodeSamples(const float *samples, size_t count) const;
                // Shared bandwidth-check + demod helper.  Given a
                // pre-measured sync (@p syncStart sub-sample-accurate
                // codeword leading edge, @p samplesPerBit measured
                // pitch, @p syncStartSampleInt integer first-positive
                // sample), runs the 76-bit integrate-and-compare
                // demod across @p samples and verifies the sync
                // nibble + CRC.  Used by both the per-band decode
                // path and the streaming decodeAll loop.
                DecodedItem demodulate(const float *samples, size_t count, double syncStart, double samplesPerBit,
                                       uint64_t syncStartSampleInt) const;
};

PROMEKI_NAMESPACE_END
