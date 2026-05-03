/**
 * @file      musicalnote.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/list.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Represents a single musical note with timing and expression data.
 * @ingroup music
 *
 * MusicalNote is a lightweight value type that describes a note (or rest)
 * with its MIDI pitch, timing, dynamics, and expression parameters such
 *
 * @par Example
 * @code
 * MusicalNote note;
 * note.setMidiNote(60);     // Middle C
 * note.setDuration(1.0);    // quarter note
 * note.setVelocity(0.8);    // mezzo-forte
 * @endcode
 * as vibrato and tremolo.  Fractional MIDI note numbers are supported
 * for microtonal or pitch-bent notes.
 */
class MusicalNote {
        public:
                using List = ::promeki::List<MusicalNote>;

                /** @brief Default-constructs an invalid (rest) note. */
                MusicalNote() = default;

                /** @brief MIDI note number (fractional). Negative values indicate a rest. */
                float midiNote() const { return _midiNote; }
                /** @brief Sets the MIDI note number. */
                void setMidiNote(float v) { _midiNote = v; }

                /** @brief Start time of the note in seconds. */
                double startTime() const { return _startTime; }
                /** @brief Sets the start time of the note in seconds. */
                void setStartTime(double v) { _startTime = v; }

                /** @brief Sounding duration in seconds (after legato scaling). */
                double duration() const { return _duration; }
                /** @brief Sets the sounding duration in seconds. */
                void setDuration(double v) { _duration = v; }

                /** @brief Full rhythmic duration in seconds (before legato scaling). */
                double fullDuration() const { return _fullDuration; }
                /** @brief Sets the full rhythmic duration in seconds. */
                void setFullDuration(double v) { _fullDuration = v; }

                /** @brief Note amplitude in the range 0.0 to 1.0. */
                float amplitude() const { return _amplitude; }
                /** @brief Sets the note amplitude. */
                void setAmplitude(float v) { _amplitude = v; }

                /** @brief Legato factor in the range 0.0 to 1.0. */
                float legato() const { return _legato; }
                /** @brief Sets the legato factor. */
                void setLegato(float v) { _legato = v; }

                /** @brief Vibrato depth in semitones. */
                float vibrato() const { return _vibrato; }
                /** @brief Sets the vibrato depth in semitones. */
                void setVibrato(float v) { _vibrato = v; }

                /** @brief Vibrato rate in Hz. */
                float vibratoRate() const { return _vibratoRate; }
                /** @brief Sets the vibrato rate in Hz. */
                void setVibratoRate(float v) { _vibratoRate = v; }

                /** @brief Tremolo depth in the range 0.0 to 1.0. */
                float tremolo() const { return _tremolo; }
                /** @brief Sets the tremolo depth. */
                void setTremolo(float v) { _tremolo = v; }

                /** @brief Tremolo rate in Hz. */
                float tremoloRate() const { return _tremoloRate; }
                /** @brief Sets the tremolo rate in Hz. */
                void setTremoloRate(float v) { _tremoloRate = v; }

                /** @brief Returns true if this note is a rest (silence). */
                bool isRest() const { return _rest; }
                /** @brief Sets whether this note is a rest. */
                void setRest(bool v) { _rest = v; }

        private:
                float  _midiNote = -1.0f;
                double _startTime = 0.0;
                double _duration = 0.0;
                double _fullDuration = 0.0;
                float  _amplitude = 0.5f;
                float  _legato = 0.5f;
                float  _vibrato = 0.0f;
                float  _vibratoRate = 5.0f;
                float  _tremolo = 0.0f;
                float  _tremoloRate = 5.0f;
                bool   _rest = false;
};

PROMEKI_NAMESPACE_END
