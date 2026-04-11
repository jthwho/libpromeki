/**
 * @file      ltcdecoder.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <vector>
#include <promeki/namespace.h>
#include <promeki/timecode.h>
#include <promeki/list.h>
#include <promeki/audio.h>
#include <vtc/ltc_audio.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Decodes LTC (Linear Timecode) audio samples into timecode values.
 * @ingroup proav
 *
 * Wraps libvtc's VtcLTCDecoder to extract SMPTE timecode from
 * biphase-mark-encoded audio. Supports incremental decoding (audio
 * can be fed in chunks) and detects forward/reverse playback direction.
 *
 * Not copyable (owns decoder state). Movable.
 *
 * @par Example
 * @code
 * LtcDecoder dec(48000);
 *
 * // Low-level: hand-prepared int8 mono samples.
 * auto results = dec.decode(audioSamples, sampleCount);
 *
 * // High-level: any-format Audio, picking the LTC channel.
 * auto results = dec.decode(audio, ltcChannelIndex);
 *
 * for(auto &r : results) {
 *     // r.timecode is the decoded value
 * }
 * @endcode
 */
class LtcDecoder {
        public:
                /**
                 * @brief Result of decoding a single LTC frame.
                 */
                struct DecodedTimecode {
                        Timecode timecode;      ///< @brief The decoded timecode value.
                        int64_t sampleStart;    ///< @brief Sample position where this frame began.
                        int64_t sampleLength;   ///< @brief Number of samples in this frame.
                };

                /** @brief List of decoded timecode results. */
                using DecodedList = List<DecodedTimecode>;

                /**
                 * @brief Constructs an LTC decoder.
                 * @param sampleRate Audio sample rate (e.g. 48000).
                 */
                LtcDecoder(int sampleRate);

                /** @brief Destructor. */
                ~LtcDecoder() = default;

                LtcDecoder(const LtcDecoder &) = delete;
                LtcDecoder &operator=(const LtcDecoder &) = delete;
                LtcDecoder(LtcDecoder &&) = default;
                LtcDecoder &operator=(LtcDecoder &&) = default;

                /**
                 * @brief Returns the configured sample rate.
                 * @return The sample rate in Hz.
                 */
                int sampleRate() const { return _decoder.sample_rate; }

                /**
                 * @brief Sets the hysteresis thresholds for edge detection.
                 * @param lower Lower threshold (typically negative, e.g. -3).
                 * @param upper Upper threshold (typically positive, e.g. 3).
                 */
                void setThresholds(int8_t lower, int8_t upper);

                /**
                 * @brief Sets the timing tolerance in samples.
                 * @param fuzz Fuzz tolerance (default: 3).
                 */
                void setFuzz(int fuzz);

                /**
                 * @brief Feeds raw int8_t audio samples to the decoder.
                 * @param samples Pointer to int8_t audio data.
                 * @param count Number of samples.
                 * @return List of decoded timecodes found in this chunk.
                 */
                DecodedList decode(const int8_t *samples, size_t count);

                /**
                 * @brief Feeds an Audio object's selected channel to the decoder.
                 *
                 * Format-agnostic — any sample format @ref AudioDesc supports
                 * is accepted.  The named channel is converted to int8 mono
                 * via @ref AudioDesc::Format::samplesToFloat (the same per-format
                 * helper @ref AudioBuffer uses) followed by a normalised
                 * float-to-int8 quantisation.  The audio's sample rate must
                 * match the decoder's configured rate, otherwise an empty
                 * list is returned.
                 *
                 * The fast path for @c PCMI_S8 mono audio (when
                 * @p channelIndex is 0) skips the conversion entirely and
                 * feeds the raw bytes straight to libvtc.
                 *
                 * @param audio        The audio chunk.
                 * @param channelIndex Zero-based channel index carrying LTC.
                 *                     Defaults to channel 0.
                 * @return List of decoded timecodes found in this chunk.
                 */
                DecodedList decode(const Audio &audio, int channelIndex = 0);

                /**
                 * @brief Clears the decoder state.
                 *
                 * Use after seeking or switching sources.
                 */
                void reset();

        private:
                VtcLTCDecoder _decoder;
                DecodedList _results;

                /// Reusable scratch buffers for the format-agnostic decode
                /// path; held as members so per-call allocation is amortised
                /// across the inspector / monitor use case.
                std::vector<float>  _floatScratch;
                std::vector<int8_t> _int8Scratch;

                static void decoderCallback(const VtcTimecode *tc,
                        int64_t sampleStart, int64_t sampleLength, void *userData);
};

PROMEKI_NAMESPACE_END
