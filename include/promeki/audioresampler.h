/**
 * @file      audioresampler.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/audio.h>
#include <promeki/audiodesc.h>
#include <promeki/error.h>
#include <promeki/result.h>
#include <promeki/enums.h>
#include <promeki/uniqueptr.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Variable-ratio audio sample rate converter backed by libsamplerate.
 * @ingroup proav
 *
 * Wraps a libsamplerate SRC_STATE to resample interleaved float32 audio
 * with smooth ratio transitions.  Designed for real-time drift correction:
 * the ratio can be changed between process() calls and libsamplerate
 * smoothly interpolates between the old and new ratios.
 *
 * All input must be in the native interleaved float32 format
 * (AudioFormat::NativeFloat).  Use Audio::convert() or AudioDesc's
 * samplesToFloat() helper to convert before feeding data in.
 *
 * The resampler is not internally thread-safe; the caller must
 * synchronize if process() is called from multiple threads.
 *
 * @par Example
 * @code
 * AudioResampler r;
 * Error err = r.setup(2, SrcQuality::SincMedium);
 * r.setRatio(48000.0 / 44100.0);
 *
 * Audio in(AudioDesc(44100.0f, 2), 1024);
 * // ... fill in ...
 * Audio out(AudioDesc(48000.0f, 2), 2048);
 * auto [generated, processErr] = r.process(in, out);
 * @endcode
 */
class AudioResampler {
        public:
                /** @brief Unique-ownership pointer to an AudioResampler. */
                using UPtr = UniquePtr<AudioResampler>;

                /** @brief Result type for process(): {output samples generated, Error}. */
                using ProcessResult = Result<size_t>;

                /** @brief Default constructor.  Creates an uninitialized resampler. */
                AudioResampler();

                /** @brief Destructor.  Releases the libsamplerate state. */
                ~AudioResampler();

                AudioResampler(const AudioResampler &) = delete;
                AudioResampler &operator=(const AudioResampler &) = delete;
                AudioResampler(AudioResampler &&) = delete;
                AudioResampler &operator=(AudioResampler &&) = delete;

                /**
                 * @brief Returns true if the resampler has been successfully set up.
                 */
                bool isValid() const;

                /**
                 * @brief Initializes (or reinitializes) the resampler.
                 *
                 * Allocates a new libsamplerate converter with the given
                 * channel count and quality mode.  Any previous state is
                 * destroyed and the ratio is reset to 1.0.
                 *
                 * @param channels Number of interleaved audio channels.
                 * @param quality  Conversion quality (maps to libsamplerate
                 *                 converter types).
                 * @return Error::Ok on success, Error::LibraryFailure if
                 *         src_new() fails, Error::InvalidArgument if
                 *         channels is 0.
                 */
                Error setup(unsigned int channels, const SrcQuality &quality = SrcQuality::SincMedium);

                /**
                 * @brief Returns the channel count this resampler was set up for.
                 */
                unsigned int channels() const;

                /**
                 * @brief Returns the current quality mode.
                 */
                SrcQuality quality() const;

                /**
                 * @brief Returns the current conversion ratio (output_rate / input_rate).
                 */
                double ratio() const;

                /**
                 * @brief Sets the conversion ratio for subsequent process() calls.
                 *
                 * The ratio is output_rate / input_rate.  For example, converting
                 * 44100 Hz input to 48000 Hz output uses ratio 48000.0 / 44100.0.
                 *
                 * libsamplerate smoothly transitions to the new ratio, which is
                 * the mechanism for variable-ratio drift correction.
                 *
                 * @param ratio The new conversion ratio.  Must be positive.
                 * @return Error::Ok on success, Error::InvalidArgument if ratio
                 *         is non-positive, Error::NotSupported if the resampler
                 *         is not set up.
                 */
                Error setRatio(double ratio);

                /**
                 * @brief Sets the conversion ratio from input and output sample rates.
                 *
                 * Convenience wrapper: ratio = outputRate / inputRate.
                 *
                 * @param inputRate  Input sample rate in Hz.
                 * @param outputRate Output sample rate in Hz.
                 * @return Same as setRatio().
                 */
                Error setRatio(float inputRate, float outputRate);

                /**
                 * @brief Processes audio samples through the resampler.
                 *
                 * Reads from @p input and writes resampled data to @p output.
                 * Both must be in native float32 interleaved format and have
                 * the same channel count as the resampler.
                 *
                 * On success, @p output's sample count is set to the number
                 * of samples actually generated.  The number of input samples
                 * consumed may be less than input.samples() if the output
                 * buffer fills; the caller should advance and re-call.
                 *
                 * @param input  Source audio (not modified, but sample count
                 *               is read).
                 * @param output Destination audio; maxSamples() determines the
                 *               upper bound on output.  Resized to actual
                 *               output on return.
                 * @param endOfInput Set to true when this is the final chunk
                 *                   (flushes the resampler's internal state).
                 * @return {outputSamplesGenerated, Error::Ok} on success.
                 */
                ProcessResult process(const Audio &input, Audio &output, bool endOfInput = false);

                /**
                 * @brief Processes raw float buffers through the resampler.
                 *
                 * Lower-level interface for callers that manage their own buffers.
                 *
                 * @param dataIn         Pointer to input float samples (interleaved).
                 * @param inputFrames    Number of input frames (samples per channel).
                 * @param dataOut        Pointer to output float buffer.
                 * @param outputFrames   Maximum output frames the buffer can hold.
                 * @param inputUsed      [out] Number of input frames consumed.
                 * @param outputGen      [out] Number of output frames generated.
                 * @param endOfInput     True if this is the final chunk.
                 * @return Error::Ok on success, Error::LibraryFailure on
                 *         libsamplerate error.
                 */
                Error process(const float *dataIn, long inputFrames,
                              float *dataOut, long outputFrames,
                              long &inputUsed, long &outputGen,
                              bool endOfInput = false);

                /**
                 * @brief Resets the resampler to its initial state.
                 *
                 * Clears internal filter history.  The ratio and channel count
                 * are preserved.  Use this when seeking or switching streams.
                 *
                 * @return Error::Ok on success.
                 */
                Error reset();

        private:
                struct Impl;
                using ImplPtr = UniquePtr<Impl>;
                ImplPtr _impl;
};

PROMEKI_NAMESPACE_END
