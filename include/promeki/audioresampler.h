/**
 * @file      audioresampler.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
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
 * (AudioFormat::NativeFloat).  Use
 * @ref PcmAudioPayload::convert or @ref AudioDesc::samplesToFloat
 * to convert before feeding data in.
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
 * std::vector<float> in(1024 * 2);
 * std::vector<float> out(2048 * 2);
 * long used = 0, gen = 0;
 * r.process(in.data(), 1024, out.data(), 2048, used, gen);
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
