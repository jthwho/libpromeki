/**
 * @file      proav/audiogen.h
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/core/namespace.h>
#include <promeki/proav/audiodesc.h>
#include <promeki/core/list.h>

PROMEKI_NAMESPACE_BEGIN

class Audio;

/**
 * @brief Audio signal generator for producing test tones and silence.
 * @ingroup proav_media
 */
class AudioGen {
	public:
                /** @brief Type of audio signal to generate. */
                enum Type {
                        Silence = 0,   ///< Generate silence (all zeros).
                        Sine           ///< Generate a sine wave.
                };

                /** @brief Per-channel configuration for the audio generator. */
                struct Config {
                        Type            type;           ///< Signal type to generate.
                        float           freq;           ///< Frequency in Hz.
                        float           amplitude;      ///< Amplitude (0.0 to 1.0).
                        float           phase;          ///< Phase offset in radians.
                        float           dutyCycle;      ///< Duty cycle (reserved for future waveform types).
                };

                /** @brief Constructs an audio generator with the given audio description. */
		AudioGen(const AudioDesc &desc);

                /** @brief Generates the specified number of audio samples. */
		Audio generate(size_t samples);

                /** @brief Returns the configuration for the given channel. */
                const Config &config(size_t chan) const {
                        return _chanConfig[chan];
                }

                /** @brief Sets the configuration for the given channel. */
                void setConfig(size_t chan, Config val);

	private:
                AudioDesc       _desc;
                List<Config>    _chanConfig;
                size_t          _sampleCount = 0;

                void genSilence(size_t chan, float *data, size_t samples) const;
                void genSine(size_t chan, float *data, size_t samples) const;
};

PROMEKI_NAMESPACE_END
