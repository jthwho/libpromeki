/**
 * @file      midinote.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cmath>
#include <promeki/midinote.h>

PROMEKI_NAMESPACE_BEGIN

namespace {
        const char *kSharpNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        const char *kFlatNames[] = {"C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"};
} // namespace

// --- Instance methods ---

const char *MidiNote::pitchClassName() const {
        return pitchClassName(static_cast<int>(_value));
}

const char *MidiNote::pitchClassNameFlat() const {
        return pitchClassNameFlat(static_cast<int>(_value));
}

int MidiNote::octave() const {
        return octave(static_cast<int>(_value));
}

int MidiNote::pitchClass() const {
        return pitchClass(static_cast<int>(_value));
}

String MidiNote::name() const {
        return nameFromMidiNote(static_cast<int>(_value));
}

double MidiNote::frequency(double a4Hz) const {
        return frequencyFromMidiNote(static_cast<double>(_value), a4Hz);
}

// --- Static methods ---

const char *MidiNote::pitchClassName(int midiNote) {
        return kSharpNames[((midiNote % 12) + 12) % 12];
}

const char *MidiNote::pitchClassNameFlat(int midiNote) {
        return kFlatNames[((midiNote % 12) + 12) % 12];
}

int MidiNote::octave(int midiNote) {
        return midiNote / 12 - 1;
}

int MidiNote::pitchClass(int midiNote) {
        int pc = midiNote % 12;
        return pc < 0 ? pc + 12 : pc;
}

String MidiNote::nameFromMidiNote(int midiNote) {
        return String(pitchClassName(midiNote)) + String::number(octave(midiNote));
}

MidiNote MidiNote::fromName(const String &name) {
        if (name.length() < 2) return MidiNote();

        int pc = -1;
        int nameLen = 0;

        // Try two-char pitch names first (e.g. "C#", "Db").
        if (name.length() >= 3 && (name.charAt(1) == '#' || name.charAt(1) == 'b')) {
                String prefix = name.substr(0, 2);
                for (int i = 0; i < 12; ++i) {
                        if (prefix == kSharpNames[i] || prefix == kFlatNames[i]) {
                                pc = i;
                                nameLen = 2;
                                break;
                        }
                }
        }

        // Try single-char pitch name.
        if (pc < 0) {
                String prefix = name.substr(0, 1);
                for (int i = 0; i < 12; ++i) {
                        if (prefix == kSharpNames[i]) {
                                pc = i;
                                nameLen = 1;
                                break;
                        }
                }
        }

        if (pc < 0) return MidiNote();

        // Parse the octave number.
        String octStr = name.substr(static_cast<size_t>(nameLen));
        Error  err;
        int    oct = octStr.toInt(&err);
        if (err.isError()) return MidiNote();
        int note = (oct + 1) * 12 + pc;
        if (note < 0 || note > 127) return MidiNote();
        return MidiNote(static_cast<uint8_t>(note));
}

double MidiNote::frequencyFromMidiNote(double midiNote, double a4Hz) {
        return a4Hz * std::pow(2.0, (midiNote - 69.0) / 12.0);
}

double MidiNote::midiNoteFromFrequency(double frequencyHz, double a4Hz) {
        return 69.0 + 12.0 * std::log2(frequencyHz / a4Hz);
}

PROMEKI_NAMESPACE_END
