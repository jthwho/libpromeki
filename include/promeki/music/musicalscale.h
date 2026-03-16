/**
 * @file      music/musicalscale.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <array>
#include <promeki/core/string.h>
#include <promeki/core/list.h>
#include <promeki/core/error.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Represents a musical scale with a root pitch class and mode.
 * @ingroup music_theory
 *
 * MusicalScale combines a root note (0–11 pitch class) with a mode
 * (Major, NaturalMinor, etc.) and provides operations for mapping
 * scale degrees to MIDI note numbers, testing scale membership,
 * and constraining arbitrary MIDI notes to the nearest scale tone.
 */
class MusicalScale {
        public:
                /** @brief Scale mode. */
                enum Mode {
                        Chromatic,
                        Major,
                        NaturalMinor,
                        HarmonicMinor,
                        MelodicMinor,
                        Pentatonic,
                        Blues
                };

                /**
                 * @brief Chromatic membership mask for a scale.
                 *
                 * Each element is 1 if the semitone offset from the root
                 * is a member of the scale, 0 otherwise.
                 */
                using MembershipMask = std::array<int, 12>;

                /** @brief Default-constructs a C Chromatic scale. */
                MusicalScale();

                /**
                 * @brief Constructs a scale with the given root pitch class and mode.
                 * @param rootPitchClass Pitch class of the root (0 = C, 1 = C#, ..., 11 = B).
                 * @param mode           Scale mode.
                 */
                MusicalScale(int rootPitchClass, Mode mode);

                /**
                 * @brief Parses a scale name such as "C Major" or "Eb Blues".
                 * @param name Scale name string.
                 * @return A pair of the parsed scale and an error code.
                 *         Returns Error::Invalid if the name cannot be parsed.
                 */
                static std::pair<MusicalScale, Error> fromName(const String &name);

                /** @brief Returns the root pitch class (0–11). */
                int rootPitchClass() const { return _rootPitchClass; }

                /** @brief Returns the scale mode. */
                Mode mode() const { return _mode; }

                /** @brief Returns the number of degrees per octave in this scale. */
                int degreesPerOctave() const { return static_cast<int>(_intervals.size()); }

                /**
                 * @brief Returns the MIDI note number for a given scale degree and octave.
                 * @param degree Scale degree (0-based, may be negative).
                 * @param octave MIDI octave number.
                 * @return Fractional MIDI note number.
                 */
                float midiNoteForDegree(int degree, int octave) const;

                /**
                 * @brief Tests whether a MIDI note number belongs to this scale.
                 * @param midiNote Integer MIDI note number.
                 * @return True if the note is a member of the scale.
                 */
                bool containsNote(int midiNote) const;

                /**
                 * @brief Constrains a fractional MIDI note to the nearest scale tone.
                 * @param midiNote  The input MIDI note (fractional).
                 * @param strength  Snap strength from 0.0 (no snap) to 1.0 (full snap).
                 * @return The constrained MIDI note.
                 */
                float constrainNote(float midiNote, float strength = 1.0f) const;

                /**
                 * @brief Parses a pitch class name ("C", "F#", "Bb") to a pitch class number.
                 * @param name Pitch class name string.
                 * @return Pitch class (0–11), or -1 if the name is not recognized.
                 */
                static int pitchClassFromName(const String &name);

                /**
                 * @brief Returns the human-readable name for a pitch class.
                 * @param pitchClass Pitch class (0–11).
                 * @return Name string (e.g. "C", "C#", "D").
                 */
                static const char *pitchClassName(int pitchClass);

                /**
                 * @brief Parses a mode name to the Mode enum.
                 * @param name Mode name (case-insensitive).
                 * @return A pair of the parsed mode and an error code.
                 */
                static std::pair<Mode, Error> modeFromName(const String &name);

                /**
                 * @brief Returns the human-readable name for a mode.
                 * @param mode Scale mode.
                 * @return Name string (e.g. "Major", "Natural Minor").
                 */
                static const char *modeName(Mode mode);

                /**
                 * @brief Returns the interval list (cumulative semitone offsets) for a mode.
                 * @param mode Scale mode.
                 * @return List of semitone offsets from the root for each scale degree.
                 */
                static List<int> intervalsForMode(Mode mode);

                /**
                 * @brief Returns the chromatic membership mask for a mode.
                 * @param mode Scale mode.
                 * @return A 12-element array where 1 indicates a scale member.
                 */
                static const MembershipMask &membershipMaskForMode(Mode mode);

        private:
                int       _rootPitchClass = 0;
                Mode      _mode           = Chromatic;
                List<int> _intervals;
};

PROMEKI_NAMESPACE_END
