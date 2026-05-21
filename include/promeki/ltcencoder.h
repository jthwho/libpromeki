/**
 * @file      ltcencoder.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_PROAV
#include <promeki/framerate.h>
#include <promeki/list.h>
#include <promeki/namespace.h>
#include <promeki/timecode.h>
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
 * LtcEncoder enc(48000, FrameRate(FrameRate::FPS_24), 0.5f);
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
                 * @brief Constructs an LTC encoder bound to a specific video frame rate.
                 *
                 * The @p frameRate parameter is the *video* frame rate at which the
                 * caller drives @ref encode.  At rates ≤ 30 fps each call emits one
                 * full 80-bit LTC codeword (≈ @c sampleRate/frameRate samples).  At
                 * HFR rates (≥48 fps) each ST 12-3 LTC codeword spans
                 * <tt>frameRate / tc_fps</tt> video frames; the encoder buffers the
                 * codeword internally and slices it across calls so each call emits
                 * exactly one video frame's worth of LTC audio
                 * (= @c sampleRate / frameRate samples).
                 *
                 * Pass a default-constructed @ref FrameRate to fall back to legacy
                 * one-codeword-per-call behavior, in which case @ref encode emits a
                 * full LTC frame (@c sampleRate/tc_fps samples) per call using the
                 * @ref Timecode's own format pointer.
                 *
                 * @param sampleRate Audio sample rate (e.g. 48000).
                 * @param frameRate  Video wall-clock rate driving @ref encode.
                 *                   Pass @c FrameRate() to disable per-video-frame
                 *                   slicing.
                 * @param level      Output amplitude 0.0-1.0 (default: 0.5).
                 */
                LtcEncoder(int sampleRate, const FrameRate &frameRate, float level = 0.5f);

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
                 * @brief Returns the configured wall-clock frame rate.
                 *
                 * Returns a default-constructed (invalid) @ref FrameRate when
                 * the encoder was constructed without an explicit rate, in
                 * which case @ref encode picks the libvtc format from the
                 * @ref Timecode itself.
                 *
                 * @return The frame rate set at construction.
                 */
                FrameRate frameRate() const { return _frameRate; }

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
                 * @brief Encodes one video frame's worth of LTC audio.
                 *
                 * At video rates ≤ tc_fps (i.e. non-HFR rates) this emits one
                 * full 80-bit LTC codeword as @c sampleRate/videoFps samples.
                 *
                 * At HFR rates the codeword duration (1/tc_fps) is longer than
                 * one video frame (1/videoFps).  The encoder buffers the
                 * codeword internally and returns just the next video-frame
                 * slice on each call (= @c sampleRate/videoFps samples per
                 * call).  When the buffer is exhausted the next call pulls a
                 * fresh codeword from libvtc using the supplied @p tc, which
                 * lets the caller advance Timecode every video frame as usual.
                 *
                 * For fractional rates (NTSC 29.97 / 59.94 / 119.88, 23.98,
                 * 47.95) the per-call sample count alternates by one to keep
                 * long-term totals exact.
                 *
                 * At HFR rates the encoder only latches a new codeword when
                 * @p tc lands on a super-frame boundary
                 * (@ref Timecode::isSuperFrameBoundary).  If the first call
                 * arrives mid-super-frame — or the caller skips ahead such
                 * that the next call would land mid-super-frame — the encoder
                 * emits silence (zero-valued PCM bytes) for that video frame
                 * and waits for the next boundary.  This keeps downstream LTC
                 * decoders from latching onto a stale codeword fragment.
                 *
                 * @param tc The timecode value to encode.  At HFR the encoder
                 *           latches the codeword only at super-frame boundaries
                 *           (every @c videoFps/tc_fps calls); the Timecode is
                 *           consulted at each such boundary.
                 * @return A list of mono PCMI_S8 samples.  Empty on failure.
                 */
                List<int8_t> encode(const Timecode &tc);

                /**
                 * @brief Resets the chunked-emission cursor so the next call
                 *        starts a fresh LTC codeword.
                 *
                 * No-op when the encoder was constructed without an explicit
                 * video FrameRate (legacy one-codeword-per-call mode).
                 */
                void resetSlicing();

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
                FrameRate     _frameRate;

                // Chunked-emission state (active only when _frameRate.isValid()).
                // _codewordBuf holds the most recent full libvtc codeword's audio;
                // _codewordCursor counts samples already emitted from it.  The
                // sample-count counters track exact long-term emission to avoid
                // rounding drift at NTSC fractional rates.
                List<int8_t> _codewordBuf;
                size_t       _codewordCursor = 0;
                int64_t      _videoFramesEmitted = 0;
                int64_t      _samplesEmittedTotal = 0;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_PROAV
