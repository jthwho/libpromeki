/**
 * @file      ltcencoder.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/timecode.h>
#include <promeki/list.h>
#include <promeki/uniqueptr.h>
#include <vtc/ltc_audio.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Encodes timecode values into LTC (Linear Timecode) audio samples.
 * @ingroup proav
 *
 * Wraps libvtc's VtcLTCEncoder to produce biphase-mark-encoded audio
 * representing SMPTE timecode. The output is mono int8_t audio samples
 * suitable for mixing into an audio stream or output on a dedicated
 * channel.
 *
 * Not copyable (owns encoder state). Movable.
 *
 * @par Example
 * @code
 * LtcEncoder enc(48000, 0.5f);
 * Timecode tc(Timecode::NDF24, 1, 0, 0, 0);
 * List<int8_t> samples = enc.encode(tc);
 * // samples contains ~2000 int8_t samples of LTC for this frame
 * @endcode
 *
 * @par Thread Safety
 * Conditionally thread-safe.  Distinct instances may be used concurrently;
 * concurrent access to a single instance must be externally synchronized.
 */
class LtcEncoder {
        public:
                /** @brief Unique-ownership pointer to an LtcEncoder. */
                using UPtr = UniquePtr<LtcEncoder>;

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
                 * @brief Encodes one timecode frame into mono int8_t samples.
                 * @param tc The timecode value to encode.
                 * @return A list of mono PCMI_S8 samples (one per
                 *         sample in the frame).  Empty on failure.
                 */
                List<int8_t> encode(const Timecode &tc);

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
