/**
 * @file      music/midinote.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstdint>
#include <promeki/core/string.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Represents a MIDI note number (0–127) with utility methods.
 * @ingroup music_theory
 *
 * MidiNote is a lightweight value type that wraps a single MIDI note
 * number.  It provides both instance methods and static methods for
 * converting between MIDI note numbers and human-readable names,
 * extracting pitch classes and octaves, and computing frequencies.
 *
 * A default-constructed MidiNote is invalid.  Use isValid() to check.
 */
class MidiNote {
        public:
                /**
                 * @brief Enumeration of all 128 MIDI note values with
                 *        sharp and flat aliases.
                 *
                 * Naming: sharps use 's' suffix (e.g. Cs4 = C#4),
                 * flats use 'b' suffix (e.g. Db4).  Octave -1 uses
                 * '_1' suffix (e.g. C_1).
                 */
                enum Value : uint8_t {
                        // Octave -1 (MIDI 0–11)
                        C_1  = 0,   Cs_1 = 1,   D_1  = 2,   Ds_1 = 3,
                        E_1  = 4,   F_1  = 5,   Fs_1 = 6,   G_1  = 7,
                        Gs_1 = 8,   A_1  = 9,   As_1 = 10,  B_1  = 11,
                        Db_1 = 1,   Eb_1 = 3,   Gb_1 = 6,   Ab_1 = 8,   Bb_1 = 10,

                        // Octave 0 (MIDI 12–23)
                        C0  = 12,  Cs0 = 13,  D0  = 14,  Ds0 = 15,
                        E0  = 16,  F0  = 17,  Fs0 = 18,  G0  = 19,
                        Gs0 = 20,  A0  = 21,  As0 = 22,  B0  = 23,
                        Db0 = 13,  Eb0 = 15,  Gb0 = 18,  Ab0 = 20,  Bb0 = 22,

                        // Octave 1 (MIDI 24–35)
                        C1  = 24,  Cs1 = 25,  D1  = 26,  Ds1 = 27,
                        E1  = 28,  F1  = 29,  Fs1 = 30,  G1  = 31,
                        Gs1 = 32,  A1  = 33,  As1 = 34,  B1  = 35,
                        Db1 = 25,  Eb1 = 27,  Gb1 = 30,  Ab1 = 32,  Bb1 = 34,

                        // Octave 2 (MIDI 36–47)
                        C2  = 36,  Cs2 = 37,  D2  = 38,  Ds2 = 39,
                        E2  = 40,  F2  = 41,  Fs2 = 42,  G2  = 43,
                        Gs2 = 44,  A2  = 45,  As2 = 46,  B2  = 47,
                        Db2 = 37,  Eb2 = 39,  Gb2 = 42,  Ab2 = 44,  Bb2 = 46,

                        // Octave 3 (MIDI 48–59)
                        C3  = 48,  Cs3 = 49,  D3  = 50,  Ds3 = 51,
                        E3  = 52,  F3  = 53,  Fs3 = 54,  G3  = 55,
                        Gs3 = 56,  A3  = 57,  As3 = 58,  B3  = 59,
                        Db3 = 49,  Eb3 = 51,  Gb3 = 54,  Ab3 = 56,  Bb3 = 58,

                        // Octave 4 (MIDI 60–71) — Middle C = C4
                        C4  = 60,  Cs4 = 61,  D4  = 62,  Ds4 = 63,
                        E4  = 64,  F4  = 65,  Fs4 = 66,  G4  = 67,
                        Gs4 = 68,  A4  = 69,  As4 = 70,  B4  = 71,
                        Db4 = 61,  Eb4 = 63,  Gb4 = 66,  Ab4 = 68,  Bb4 = 70,

                        // Octave 5 (MIDI 72–83)
                        C5  = 72,  Cs5 = 73,  D5  = 74,  Ds5 = 75,
                        E5  = 76,  F5  = 77,  Fs5 = 78,  G5  = 79,
                        Gs5 = 80,  A5  = 81,  As5 = 82,  B5  = 83,
                        Db5 = 73,  Eb5 = 75,  Gb5 = 78,  Ab5 = 80,  Bb5 = 82,

                        // Octave 6 (MIDI 84–95)
                        C6  = 84,  Cs6 = 85,  D6  = 86,  Ds6 = 87,
                        E6  = 88,  F6  = 89,  Fs6 = 90,  G6  = 91,
                        Gs6 = 92,  A6  = 93,  As6 = 94,  B6  = 95,
                        Db6 = 85,  Eb6 = 87,  Gb6 = 90,  Ab6 = 92,  Bb6 = 94,

                        // Octave 7 (MIDI 96–107)
                        C7  = 96,  Cs7 = 97,  D7  = 98,  Ds7 = 99,
                        E7  = 100, F7  = 101, Fs7 = 102, G7  = 103,
                        Gs7 = 104, A7  = 105, As7 = 106, B7  = 107,
                        Db7 = 97,  Eb7 = 99,  Gb7 = 102, Ab7 = 104, Bb7 = 106,

                        // Octave 8 (MIDI 108–119)
                        C8  = 108, Cs8 = 109, D8  = 110, Ds8 = 111,
                        E8  = 112, F8  = 113, Fs8 = 114, G8  = 115,
                        Gs8 = 116, A8  = 117, As8 = 118, B8  = 119,
                        Db8 = 109, Eb8 = 111, Gb8 = 114, Ab8 = 116, Bb8 = 118,

                        // Octave 9 (MIDI 120–127, partial)
                        C9  = 120, Cs9 = 121, D9  = 122, Ds9 = 123,
                        E9  = 124, F9  = 125, Fs9 = 126, G9  = 127,
                        Db9 = 121, Eb9 = 123, Gb9 = 126,

                        // General MIDI Percussion (channel 10, notes 27–87)
                        GMP_HighQ            = 27,
                        GMP_Slap             = 28,
                        GMP_ScratchPush      = 29,
                        GMP_ScratchPull      = 30,
                        GMP_Sticks           = 31,
                        GMP_SquareClick      = 32,
                        GMP_MetronomeClick   = 33,
                        GMP_MetronomeBell    = 34,
                        GMP_AcousticBassDrum = 35,
                        GMP_BassDrum1        = 36,
                        GMP_SideStick        = 37,
                        GMP_AcousticSnare    = 38,
                        GMP_HandClap         = 39,
                        GMP_ElectricSnare    = 40,
                        GMP_LowFloorTom      = 41,
                        GMP_ClosedHiHat      = 42,
                        GMP_HighFloorTom     = 43,
                        GMP_PedalHiHat       = 44,
                        GMP_LowTom           = 45,
                        GMP_OpenHiHat        = 46,
                        GMP_LowMidTom        = 47,
                        GMP_HiMidTom         = 48,
                        GMP_CrashCymbal1     = 49,
                        GMP_HighTom          = 50,
                        GMP_RideCymbal1      = 51,
                        GMP_ChineseCymbal    = 52,
                        GMP_RideBell         = 53,
                        GMP_Tambourine       = 54,
                        GMP_SplashCymbal     = 55,
                        GMP_Cowbell          = 56,
                        GMP_CrashCymbal2     = 57,
                        GMP_Vibraslap        = 58,
                        GMP_RideCymbal2      = 59,
                        GMP_HiBongo          = 60,
                        GMP_LowBongo         = 61,
                        GMP_MuteHiConga      = 62,
                        GMP_OpenHiConga      = 63,
                        GMP_LowConga         = 64,
                        GMP_HighTimbale      = 65,
                        GMP_LowTimbale       = 66,
                        GMP_HighAgogo        = 67,
                        GMP_LowAgogo         = 68,
                        GMP_Cabasa           = 69,
                        GMP_Maracas          = 70,
                        GMP_ShortWhistle     = 71,
                        GMP_LongWhistle      = 72,
                        GMP_ShortGuiro       = 73,
                        GMP_LongGuiro        = 74,
                        GMP_Claves           = 75,
                        GMP_HiWoodBlock      = 76,
                        GMP_LowWoodBlock     = 77,
                        GMP_MuteCuica        = 78,
                        GMP_OpenCuica        = 79,
                        GMP_MuteTriangle     = 80,
                        GMP_OpenTriangle     = 81,
                        GMP_Shaker           = 82,
                        GMP_JingleBell       = 83,
                        GMP_Belltree         = 84,
                        GMP_Castanets        = 85,
                        GMP_MuteSurdo        = 86,
                        GMP_OpenSurdo        = 87,

                        Invalid = 0xFF
                };

                /** @brief Default-constructs an invalid MidiNote. */
                MidiNote() : _value(Invalid) {}

                /** @brief Constructs a MidiNote from a Value enum. */
                MidiNote(Value v) : _value(v) {}

                /**
                 * @brief Constructs a MidiNote from a raw byte value.
                 * @param v  Raw MIDI note number; values > 127 become Invalid.
                 */
                MidiNote(uint8_t v) : _value(v > 127 ? Invalid : static_cast<Value>(v)) {}

                /** @brief Returns true if this note is valid (0–127). */
                bool isValid() const { return _value != Invalid; }

                /** @brief Returns the Value enum. */
                Value value() const { return _value; }

                /** @brief Returns the raw MIDI note number as uint8_t, or 0xFF if invalid. */
                uint8_t rawValue() const { return static_cast<uint8_t>(_value); }

                /** @brief Implicit conversion to uint8_t. */
                operator uint8_t() const { return static_cast<uint8_t>(_value); }

                // --- Instance methods (return values for this note) ---

                /** @brief Returns the pitch class name (e.g. "C", "C#"). */
                const char *pitchClassName() const;

                /** @brief Returns the pitch class name using flats (e.g. "Db"). */
                const char *pitchClassNameFlat() const;

                /** @brief Returns the octave number. */
                int octave() const;

                /** @brief Returns the pitch class (0–11). */
                int pitchClass() const;

                /** @brief Returns the human-readable name (e.g. "C4"). */
                String name() const;

                /**
                 * @brief Returns the frequency in Hz.
                 * @param a4Hz Reference frequency for A4 (default 440.0).
                 */
                double frequency(double a4Hz = 440.0) const;

                // --- Static methods (operate on raw values) ---

                /** @brief Returns the pitch class name for a MIDI note number. */
                static const char *pitchClassName(int midiNote);

                /** @brief Returns the pitch class name using flats. */
                static const char *pitchClassNameFlat(int midiNote);

                /** @brief Returns the octave number for a MIDI note number. */
                static int octave(int midiNote);

                /** @brief Returns the pitch class (0–11) for a MIDI note number. */
                static int pitchClass(int midiNote);

                /** @brief Converts a MIDI note number to a human-readable name. */
                static String nameFromMidiNote(int midiNote);

                /**
                 * @brief Parses a note name string to a MidiNote.
                 * @param name Note name (e.g. "C4", "Bb3", "F#5").
                 * @return A valid MidiNote, or an invalid MidiNote if not recognized.
                 */
                static MidiNote fromName(const String &name);

                /**
                 * @brief Computes the frequency in Hz for a MIDI note number.
                 * @param midiNote Fractional MIDI note number.
                 * @param a4Hz     Reference frequency for A4 (default 440.0).
                 */
                static double frequencyFromMidiNote(double midiNote, double a4Hz = 440.0);

                /**
                 * @brief Computes the MIDI note number for a frequency in Hz.
                 * @param frequencyHz Frequency in Hz.
                 * @param a4Hz        Reference frequency for A4 (default 440.0).
                 */
                static double midiNoteFromFrequency(double frequencyHz, double a4Hz = 440.0);

                // --- Comparison operators ---

                bool operator==(const MidiNote &other) const { return _value == other._value; }
                bool operator!=(const MidiNote &other) const { return _value != other._value; }
                bool operator<(const MidiNote &other) const  { return _value < other._value; }
                bool operator<=(const MidiNote &other) const { return _value <= other._value; }
                bool operator>(const MidiNote &other) const  { return _value > other._value; }
                bool operator>=(const MidiNote &other) const { return _value >= other._value; }

        private:
                Value _value;
};

PROMEKI_NAMESPACE_END
