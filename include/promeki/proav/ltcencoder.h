/**
 * @file      proav/ltcencoder.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/core/namespace.h>
#include <promeki/core/timecode.h>
#include <promeki/proav/audio.h>
#include <vtc/ltc_audio.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Encodes timecode values into LTC (Linear Timecode) audio samples.
 * @ingroup proav_media
 *
 * Wraps libvtc's VtcLTCEncoder to produce biphase-mark-encoded audio
 * representing SMPTE timecode. The output is mono int8_t audio suitable
 * for mixing into an audio stream or output on a dedicated channel.
 *
 * Not copyable (owns encoder state). Movable.
 *
 * @par Example
 * @code
 * LtcEncoder enc(48000, 0.5f);
 * Timecode tc(Timecode::NDF24, 1, 0, 0, 0);
 * Audio audio = enc.encode(tc);
 * // audio contains ~2000 int8_t samples of LTC for this frame
 * @endcode
 */
class LtcEncoder {
        public:
                /**
                 * @brief Constructs an LTC encoder.
                 * @param sampleRate Audio sample rate (e.g. 48000).
                 * @param level Output amplitude 0.0-1.0 (default: 0.5).
                 */
                LtcEncoder(int sampleRate, float level = 0.5f);

                /** @brief Destructor. */
                ~LtcEncoder() = default;

                LtcEncoder(const LtcEncoder &) = delete;
                LtcEncoder &operator=(const LtcEncoder &) = delete;
                LtcEncoder(LtcEncoder &&) = default;
                LtcEncoder &operator=(LtcEncoder &&) = default;

                /**
                 * @brief Returns the configured sample rate.
                 * @return The sample rate in Hz.
                 */
                int sampleRate() const { return _encoder.sample_rate; }

                /**
                 * @brief Returns the output amplitude level.
                 * @return The level 0.0-1.0.
                 */
                float level() const { return _encoder.level; }

                /**
                 * @brief Sets the output amplitude.
                 * @param level Output amplitude 0.0-1.0.
                 */
                void setLevel(float level);

                /**
                 * @brief Encodes one timecode frame into mono int8_t audio.
                 * @param tc The timecode value to encode.
                 * @return An Audio object containing the LTC samples (mono, PCMI_S8).
                 */
                Audio encode(const Timecode &tc);

                /**
                 * @brief Returns the approximate number of samples per LTC frame.
                 *
                 * Useful for buffer pre-allocation. Actual encoded size may vary slightly.
                 *
                 * @param format The timecode format to estimate for.
                 * @return Approximate sample count.
                 */
                size_t frameSizeApprox(const VtcFormat *format) const;

        private:
                VtcLTCEncoder _encoder;
};

PROMEKI_NAMESPACE_END
