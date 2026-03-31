/**
 * @file      proav/audiotestpattern.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/core/namespace.h>
#include <promeki/core/string.h>
#include <promeki/core/error.h>
#include <promeki/core/result.h>
#include <promeki/core/audiolevel.h>
#include <promeki/proav/audiodesc.h>

PROMEKI_NAMESPACE_BEGIN

class Audio;
class AudioGen;
class LtcEncoder;
class Timecode;

/**
 * @brief Standalone audio test pattern generator.
 * @ingroup proav_media
 *
 * Generates audio test patterns including sine tones, silence, and LTC.
 * Supports dual-mode: create a new Audio buffer or render into an existing one.
 *
 * Call configure() after setting mode, frequency, and level parameters
 * to prepare the internal generators. Then call create() or render()
 * to produce audio data.
 *
 * This class is not thread-safe. External synchronization is required
 * for concurrent access.
 *
 * @par Example
 * @code
 * AudioTestPattern gen(AudioDesc(48000, 2));
 * gen.setMode(AudioTestPattern::Tone);
 * gen.setToneFrequency(1000.0);
 * gen.setToneLevel(AudioLevel::fromDbfs(-20.0));
 * gen.configure();
 *
 * Audio audio = gen.create(4800);
 * @endcode
 */
class AudioTestPattern {
        public:
                /** @brief Audio generation mode. */
                enum Mode {
                        Tone,           ///< @brief Sine tone (configurable frequency).
                        Silence,        ///< @brief Silence.
                        LTC             ///< @brief LTC timecode audio.
                };

                /**
                 * @brief Constructs an AudioTestPattern.
                 * @param desc Audio format descriptor.
                 */
                AudioTestPattern(const AudioDesc &desc);

                /** @brief Destructor. */
                ~AudioTestPattern();

                AudioTestPattern(const AudioTestPattern &) = delete;
                AudioTestPattern &operator=(const AudioTestPattern &) = delete;

                /** @brief Returns the audio descriptor. */
                const AudioDesc &desc() const { return _desc; }

                /** @brief Returns the current audio mode. */
                Mode mode() const { return _mode; }

                /**
                 * @brief Sets the audio mode.
                 *
                 * @par Example
                 * @code
                 * gen.setMode(AudioTestPattern::Tone);    // sine wave
                 * gen.setMode(AudioTestPattern::Silence); // silence
                 * gen.setMode(AudioTestPattern::LTC);     // LTC timecode
                 * @endcode
                 */
                void setMode(Mode mode) { _mode = mode; }

                /** @brief Returns the tone frequency in Hz. */
                double toneFrequency() const { return _toneFreq; }

                /** @brief Sets the tone frequency in Hz. */
                void setToneFrequency(double freq) { _toneFreq = freq; }

                /** @brief Returns the tone level. */
                AudioLevel toneLevel() const { return _toneLevel; }

                /** @brief Sets the tone level. */
                void setToneLevel(AudioLevel level) { _toneLevel = level; }

                /** @brief Returns the LTC level. */
                AudioLevel ltcLevel() const { return _ltcLevel; }

                /** @brief Sets the LTC level. */
                void setLtcLevel(AudioLevel level) { _ltcLevel = level; }

                /** @brief Returns the LTC target channel (-1 = all). */
                int ltcChannel() const { return _ltcChannel; }

                /** @brief Sets the LTC target channel (-1 = all). */
                void setLtcChannel(int channel) { _ltcChannel = channel; }

                /**
                 * @brief Configures internal generators based on current settings.
                 *
                 * Must be called after changing mode, frequency, or level settings
                 * and before calling create() or render(). Sets up AudioGen or
                 * LtcEncoder as appropriate.
                 *
                 * @return Error::Ok on success.
                 *
                 * @par Example
                 * @code
                 * gen.setMode(AudioTestPattern::Tone);
                 * gen.setToneFrequency(440.0);
                 * gen.configure();           // must call before create()/render()
                 * Audio buf = gen.create(4800);
                 * @endcode
                 */
                Error configure();

                /**
                 * @brief Creates a new Audio buffer with the test pattern.
                 * @param samples Number of samples to generate.
                 * @param tc Timecode for LTC mode (ignored for other modes).
                 * @return A new Audio object containing the generated samples.
                 *
                 * @par Example
                 * @code
                 * // LTC mode: embed timecode into audio
                 * Audio buf = gen.create(samplesPerFrame, currentTimecode);
                 * @endcode
                 */
                Audio create(size_t samples, const Timecode &tc) const;

                /**
                 * @brief Creates a new Audio buffer with the test pattern (no timecode).
                 * @param samples Number of samples to generate.
                 * @return A new Audio object containing the generated samples.
                 *
                 * @par Example
                 * @code
                 * // Tone or silence mode
                 * Audio buf = gen.create(samplesPerFrame);
                 * @endcode
                 */
                Audio create(size_t samples) const;

                /**
                 * @brief Renders the test pattern into an existing Audio buffer.
                 * @param audio The target audio buffer.
                 * @param tc Timecode for LTC mode (ignored for other modes).
                 *
                 * @par Example
                 * @code
                 * Audio buf(desc, samplesPerFrame);
                 * gen.render(buf, currentTimecode);  // fill in-place (LTC)
                 * @endcode
                 */
                void render(Audio &audio, const Timecode &tc) const;

                /**
                 * @brief Renders the test pattern into an existing Audio buffer (no timecode).
                 * @param audio The target audio buffer.
                 *
                 * @par Example
                 * @code
                 * Audio buf(desc, samplesPerFrame);
                 * gen.render(buf);  // fill in-place (tone or silence)
                 * @endcode
                 */
                void render(Audio &audio) const;

                /**
                 * @brief Parses a mode name string.
                 * @param name Mode name ("tone", "silence", "ltc").
                 * @return Result containing Mode on success, or Error::Invalid.
                 *
                 * @par Example
                 * @code
                 * auto [mode, err] = AudioTestPattern::fromString("ltc");
                 * if(err.isOk()) gen.setMode(mode);
                 * @endcode
                 */
                static Result<Mode> fromString(const String &name);

                /**
                 * @brief Returns the name string for a Mode value.
                 * @param mode The mode value.
                 * @return The mode name string (lowercase).
                 *
                 * @par Example
                 * @code
                 * String name = AudioTestPattern::toString(AudioTestPattern::LTC);
                 * // name == "ltc"
                 * @endcode
                 */
                static String toString(Mode mode);

        private:
                AudioDesc       _desc;
                Mode            _mode = Tone;
                double          _toneFreq = 1000.0;
                AudioLevel      _toneLevel = AudioLevel::fromDbfs(-20.0);
                AudioLevel      _ltcLevel = AudioLevel::fromDbfs(-20.0);
                int             _ltcChannel = 0;
                AudioGen        *_audioGen = nullptr;
                LtcEncoder      *_ltcEncoder = nullptr;
};

PROMEKI_NAMESPACE_END
