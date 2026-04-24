/**
 * @file      audiogen.h
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/audiolevel.h>
#include <promeki/audiodesc.h>
#include <promeki/list.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Audio signal generator for producing test tones and silence.
 * @ingroup proav
 *
 * Generates multi-channel audio data based on per-channel configuration.
 * Each channel can independently produce silence or a sine tone at a
 * configurable frequency and level.  Output is written directly into a
 * caller-supplied interleaved @c float buffer.
 *
 * @par Example
 * @code
 * AudioDesc desc(48000, 2);
 * AudioGen gen(desc);
 * gen.setConfig(0, { AudioGen::Sine, 1000.0f, AudioLevel::fromDbfs(-10.0), 0.0f, 0.0f });
 * float samples[4800 * 2];
 * gen.generate(samples, 4800);
 * @endcode
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
                        AudioLevel      level;          ///< Output level in dBFS.
                        float           phase;          ///< Phase offset in radians.
                        float           dutyCycle;      ///< Duty cycle (reserved for future waveform types).
                        float           linearGain;     ///< Cached linear gain from level. Set internally by setConfig().
                };

                /**
                 * @brief Constructs an audio generator with the given audio description.
                 * @param desc The audio format description (sample rate, channels).
                 */
                AudioGen(const AudioDesc &desc);

                /**
                 * @brief Generates the specified number of interleaved
                 *        float samples into @p out.
                 *
                 * The caller must provide a buffer sized at least
                 * @c samples * channels floats; the function writes
                 * exactly that many samples per channel and advances
                 * the internal sample cursor.
                 *
                 * @param out     Interleaved float output buffer.
                 * @param samples Number of samples per channel.
                 * @return @c true on success, @c false if @p out is
                 *         null.
                 */
                bool generate(float *out, size_t samples);

                /**
                 * @brief Returns the configuration for the given channel.
                 * @param chan Channel index.
                 * @return The channel's generator configuration.
                 */
                const Config &config(size_t chan) const {
                        return _chanConfig[chan];
                }

                /**
                 * @brief Sets the configuration for the given channel.
                 * @param chan Channel index.
                 * @param val The generator configuration for that channel.
                 */
                void setConfig(size_t chan, Config val);

        private:
                AudioDesc       _desc;
                List<Config>    _chanConfig;
                size_t          _sampleCount = 0;

                void genSilence(size_t chan, float *data, size_t samples) const;
                void genSine(size_t chan, float *data, size_t samples) const;
};

PROMEKI_NAMESPACE_END
