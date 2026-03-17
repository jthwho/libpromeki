/**
 * @file      proav/ltcdecoder.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/core/namespace.h>
#include <promeki/core/timecode.h>
#include <promeki/core/list.h>
#include <promeki/proav/audio.h>
#include <vtc/ltc_audio.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Decodes LTC (Linear Timecode) audio samples into timecode values.
 * @ingroup proav_media
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
 * auto results = dec.decode(audioSamples, sampleCount);
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
                 * @brief Feeds an Audio object to the decoder.
                 *
                 * The Audio must be mono PCMI_S8. Other formats are not supported.
                 *
                 * @param audio The audio to decode.
                 * @return List of decoded timecodes found in this chunk.
                 */
                DecodedList decode(const Audio &audio);

                /**
                 * @brief Clears the decoder state.
                 *
                 * Use after seeking or switching sources.
                 */
                void reset();

        private:
                VtcLTCDecoder _decoder;
                DecodedList _results;

                static void decoderCallback(const VtcTimecode *tc,
                        int64_t sampleStart, int64_t sampleLength, void *userData);
};

PROMEKI_NAMESPACE_END
