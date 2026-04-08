/**
 * @file      musicalscale.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <algorithm>
#include <cmath>
#include <promeki/musicalscale.h>

PROMEKI_NAMESPACE_BEGIN

namespace {
        // clang-format off
        constexpr MusicalScale::MembershipMask kChromatic     = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
        constexpr MusicalScale::MembershipMask kMajor         = {1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1};
        constexpr MusicalScale::MembershipMask kNaturalMinor  = {1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 1, 0};
        constexpr MusicalScale::MembershipMask kHarmonicMinor = {1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 0, 1};
        constexpr MusicalScale::MembershipMask kMelodicMinor  = {1, 0, 1, 1, 0, 1, 0, 1, 0, 1, 0, 1};
        constexpr MusicalScale::MembershipMask kPentatonic    = {1, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 0};
        constexpr MusicalScale::MembershipMask kBlues         = {1, 0, 0, 1, 0, 1, 1, 1, 0, 0, 1, 0};
        // clang-format on
} // namespace

MusicalScale::MusicalScale()
        : _rootPitchClass(0), _mode(Chromatic) {
        _intervals = intervalsForMode(_mode);
        return;
}

MusicalScale::MusicalScale(int rootPitchClass, Mode mode)
        : _rootPitchClass(rootPitchClass), _mode(mode) {
        _intervals = intervalsForMode(mode);
        return;
}

Result<MusicalScale> MusicalScale::fromName(const String &name) {
        if(name.isEmpty()) return makeError<MusicalScale>(Error::Invalid);

        const std::string &s = name.str();

        // Find where the root note ends and the mode name begins.
        // Root can be 1 char (C) or 2 chars (C#, Db, Ab).
        size_t modeStart = 1;
        if(s.size() >= 2 && (s[1] == '#' || s[1] == 'b')) modeStart = 2;

        String rootStr = String(s.substr(0, modeStart));
        int root = pitchClassFromName(rootStr);
        if(root < 0) return makeError<MusicalScale>(Error::Invalid);

        // Extract mode string, trimming leading whitespace.
        Mode mode = Chromatic;
        if(modeStart < s.size()) {
                std::string modeStr = s.substr(modeStart);
                size_t first = modeStr.find_first_not_of(' ');
                if(first != std::string::npos) {
                        modeStr = modeStr.substr(first);
                        auto [m, err] = modeFromName(String(modeStr));
                        if(err.isError()) return makeError<MusicalScale>(err);
                        mode = m;
                }
        }

        return makeResult(MusicalScale(root, mode));
}

float MusicalScale::midiNoteForDegree(int degree, int octave) const {
        int n = static_cast<int>(_intervals.size());
        if(n == 0) return static_cast<float>((octave + 1) * 12 + _rootPitchClass);

        // Floor division to handle negative degrees correctly.
        int octaveShift;
        int index;
        if(degree >= 0) {
                octaveShift = degree / n;
                index       = degree % n;
        } else {
                octaveShift = (degree - n + 1) / n;
                index       = degree - octaveShift * n;
        }

        int semitones = _intervals[static_cast<size_t>(index)];
        return static_cast<float>((octave + 1) * 12 + _rootPitchClass + octaveShift * 12 + semitones);
}

bool MusicalScale::containsNote(int midiNote) const {
        int noteInOctave = midiNote % 12;
        if(noteInOctave < 0) noteInOctave += 12;
        int offset = (noteInOctave - _rootPitchClass + 12) % 12;
        const auto &mask = membershipMaskForMode(_mode);
        return mask[static_cast<size_t>(offset)] == 1;
}

float MusicalScale::constrainNote(float midiNote, float strength) const {
        if(strength <= 0.0f || _mode == Chromatic) return midiNote;

        auto rounded = std::round(midiNote);
        auto noteInOctave = static_cast<int>(rounded) % 12;
        if(noteInOctave < 0) noteInOctave += 12;

        auto offset = (noteInOctave - _rootPitchClass + 12) % 12;
        const auto &mask = membershipMaskForMode(_mode);

        if(mask[static_cast<size_t>(offset)] == 1) return midiNote;

        int bestDistance = 12;
        int bestNote     = static_cast<int>(rounded);

        for(int delta = -6; delta <= 6; ++delta) {
                int candidate       = static_cast<int>(rounded) + delta;
                int candidateOffset = ((candidate % 12) - _rootPitchClass + 12) % 12;
                if(mask[static_cast<size_t>(candidateOffset)] == 1) {
                        if(std::abs(delta) < std::abs(bestDistance)) {
                                bestDistance = delta;
                                bestNote     = candidate;
                        }
                }
        }

        float snappedNote = static_cast<float>(bestNote);
        return midiNote + strength * (snappedNote - midiNote);
}

int MusicalScale::pitchClassFromName(const String &name) {
        if(name.isEmpty()) return -1;

        const std::string &s = name.str();

        int base = -1;
        switch(s[0]) {
                case 'C': base = 0;  break;
                case 'D': base = 2;  break;
                case 'E': base = 4;  break;
                case 'F': base = 5;  break;
                case 'G': base = 7;  break;
                case 'A': base = 9;  break;
                case 'B': base = 11; break;
                default:  return -1;
        }

        for(size_t i = 1; i < s.size(); i++) {
                if(s[i] == '#')      base++;
                else if(s[i] == 'b') base--;
        }

        // Wrap to 0–11.
        base = ((base % 12) + 12) % 12;
        return base;
}

const char *MusicalScale::pitchClassName(int pitchClass) {
        static const char *kNames[] = {
                "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
        };
        return kNames[((pitchClass % 12) + 12) % 12];
}

Result<MusicalScale::Mode> MusicalScale::modeFromName(const String &name) {
        std::string lower = name.str();
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

        if(lower == "chromatic")                                           return makeResult(Chromatic);
        if(lower == "major" || lower == "ionian")                          return makeResult(Major);
        if(lower == "minor" || lower == "natural minor" || lower == "aeolian") return makeResult(NaturalMinor);
        if(lower == "harmonic minor")                                      return makeResult(HarmonicMinor);
        if(lower == "melodic minor")                                       return makeResult(MelodicMinor);
        if(lower == "pentatonic" || lower == "major pentatonic")           return makeResult(Pentatonic);
        if(lower == "blues")                                               return makeResult(Blues);

        return makeError<Mode>(Error::Invalid);
}

const char *MusicalScale::modeName(Mode mode) {
        switch(mode) {
                case Chromatic:     return "Chromatic";
                case Major:         return "Major";
                case NaturalMinor:  return "Natural Minor";
                case HarmonicMinor: return "Harmonic Minor";
                case MelodicMinor:  return "Melodic Minor";
                case Pentatonic:    return "Pentatonic";
                case Blues:         return "Blues";
        }
        return "Unknown";
}

List<int> MusicalScale::intervalsForMode(Mode mode) {
        switch(mode) {
                case Chromatic:     return {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
                case Major:         return {0, 2, 4, 5, 7, 9, 11};
                case NaturalMinor:  return {0, 2, 3, 5, 7, 8, 10};
                case HarmonicMinor: return {0, 2, 3, 5, 7, 8, 11};
                case MelodicMinor:  return {0, 2, 3, 5, 7, 9, 11};
                case Pentatonic:    return {0, 2, 4, 7, 9};
                case Blues:         return {0, 3, 5, 6, 7, 10};
        }
        return {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
}

const MusicalScale::MembershipMask &MusicalScale::membershipMaskForMode(Mode mode) {
        switch(mode) {
                case Chromatic:     return kChromatic;
                case Major:         return kMajor;
                case NaturalMinor:  return kNaturalMinor;
                case HarmonicMinor: return kHarmonicMinor;
                case MelodicMinor:  return kMelodicMinor;
                case Pentatonic:    return kPentatonic;
                case Blues:         return kBlues;
        }
        return kChromatic;
}

PROMEKI_NAMESPACE_END
