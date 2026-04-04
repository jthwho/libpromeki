/**
 * @file      notesequenceparser.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cctype>
#include <cmath>
#include <promeki/notesequenceparser.h>

PROMEKI_NAMESPACE_BEGIN

MusicalNote::List NoteSequenceParser::parse(const String &input) {
        _input       = input;
        _pos         = 0;
        _currentTime = 0.0;
        _params      = Params{};
        _paramStack.clear();
        _namedParams.clear();
        _notes.clear();
        _errors.clear();

        const std::string &s = _input.str();
        while(_pos < s.size()) {
                skipWhitespace();
                if(atEnd()) break;
                parseToken();
        }

        return _notes;
}

void NoteSequenceParser::parseToken() {
        char c  = current();
        char c1 = peek(1);
        char c2 = peek(2);

        // Comments.
        if(c == '/' && c1 == '/') { skipComment(); return; }

        // Two-char command prefixes (check before single-char).
        if(c == 'T' && c1 == 'R' && c2 == '(') { parseTremolo(); return; }
        if(c == 'N' && c1 == 'P') { _pos += 2; pushParams(); return; }
        if(c == 'P' && c1 == 'P') { _pos += 2; popParams(); return; }
        if(c == 'P' && c1 == 'S' && c2 == '(') { parseSaveParams(); return; }
        if(c == 'P' && c1 == 'R' && c2 == '(') { parseRecallParams(); return; }

        // Single-char commands followed by '('.
        if(c == 'T' && c1 == '(') { parseTempo(); return; }
        if(c == 'N' && c1 == '(') { parseNoteLengthParam(); return; }
        if(c == 'A' && c1 == '(') { parseAmplitude(); return; }
        if(c == 'O' && c1 == '(') { parseOctave(); return; }
        if(c == 'S' && c1 == '(') { parseScaleParam(); return; }
        if(c == 'L' && c1 == '(') { parseLegato(); return; }
        if(c == 'V' && c1 == '(') { parseVibrato(); return; }

        // Rests.
        if(c == '_') { parseRest(); return; }
        if(c == '|') { parseBarRest(); return; }

        // Letter notes (A–G).
        if(c >= 'A' && c <= 'G') { parseLetterNote(); return; }

        // Numeric notes (scale degrees), including negative.
        if(std::isdigit(static_cast<unsigned char>(c))) { parseNumericNote(); return; }
        if(c == '-' && std::isdigit(static_cast<unsigned char>(c1))) { parseNumericNote(); return; }

        addError(String("Unexpected character: '") + String(std::string(1, c)) + "'");
        _pos++;
        return;
}

// --- Parameter commands ---

void NoteSequenceParser::parseTempo() {
        _pos++; // skip 'T'
        auto arg = readParenArg();
        try {
                _params.tempo = std::stod(arg);
        } catch(...) {
                addError(String("Invalid tempo value: ") + arg);
        }
        return;
}

void NoteSequenceParser::parseNoteLengthParam() {
        _pos++; // skip 'N'
        auto arg = readParenArg();
        double val = parseNoteLengthValue(arg);
        if(val > 0.0)
                _params.noteLength = val;
        else
                addError(String("Invalid note length: ") + arg);
        return;
}

void NoteSequenceParser::parseAmplitude() {
        _pos++; // skip 'A'
        auto arg = readParenArg();
        try {
                _params.amplitude = std::stof(arg);
        } catch(...) {
                addError(String("Invalid amplitude value: ") + arg);
        }
        return;
}

void NoteSequenceParser::parseOctave() {
        _pos++; // skip 'O'
        auto arg = readParenArg();
        try {
                _params.octave = std::stoi(arg);
        } catch(...) {
                addError(String("Invalid octave value: ") + arg);
        }
        return;
}

void NoteSequenceParser::parseScaleParam() {
        _pos++; // skip 'S'
        auto arg = readParenArg();
        auto [scale, err] = MusicalScale::fromName(String(arg));
        if(err.isError()) {
                addError(String("Invalid scale: ") + arg);
        } else {
                _params.scale = scale;
        }
        return;
}

void NoteSequenceParser::parseLegato() {
        _pos++; // skip 'L'
        auto arg = readParenArg();
        try {
                _params.legato = std::stof(arg);
        } catch(...) {
                addError(String("Invalid legato value: ") + arg);
        }
        return;
}

void NoteSequenceParser::parseVibrato() {
        _pos++; // skip 'V'
        auto arg = readParenArg();

        // Parse "depth" or "depth, rate".
        auto comma = arg.find(',');
        try {
                if(comma != String::npos) {
                        _params.vibrato     = std::stof(arg.substr(0, comma));
                        _params.vibratoRate = std::stof(arg.substr(comma + 1));
                } else {
                        _params.vibrato = std::stof(arg);
                }
        } catch(...) {
                addError(String("Invalid vibrato value: ") + arg);
        }
        return;
}

void NoteSequenceParser::parseTremolo() {
        _pos += 2; // skip 'TR'
        auto arg = readParenArg();

        auto comma = arg.find(',');
        try {
                if(comma != String::npos) {
                        _params.tremolo     = std::stof(arg.substr(0, comma));
                        _params.tremoloRate = std::stof(arg.substr(comma + 1));
                } else {
                        _params.tremolo = std::stof(arg);
                }
        } catch(...) {
                addError(String("Invalid tremolo value: ") + arg);
        }
        return;
}

// --- Parameter stack ---

void NoteSequenceParser::pushParams() {
        _paramStack.pushToBack(_params);
        return;
}

void NoteSequenceParser::popParams() {
        if(_paramStack.isEmpty()) {
                addError("Parameter stack underflow (PP without matching NP)");
                return;
        }
        _params = _paramStack.back();
        _paramStack.popFromBack();
        return;
}

void NoteSequenceParser::parseSaveParams() {
        _pos += 2; // skip 'PS'
        auto name = readParenArg();
        _namedParams[name] = _params;
        return;
}

void NoteSequenceParser::parseRecallParams() {
        _pos += 2; // skip 'PR'
        auto name = readParenArg();
        auto it = _namedParams.find(name);
        if(it == _namedParams.end()) {
                addError(String("Unknown parameter set: ") + name);
                return;
        }
        _paramStack.pushToBack(_params);
        _params = it->second;
        return;
}

// --- Notes ---

void NoteSequenceParser::parseLetterNote() {
        const std::string &s = _input.str();
        char noteName = s[_pos++];

        // Map letter to pitch class.
        static const int kPitchClass[] = {9, 11, 0, 2, 4, 5, 7}; // A=9, B=11, C=0, ...
        int pitchClass = kPitchClass[noteName - 'A'];

        // Accidentals: # (sharp) and b (flat, lowercase only).
        int accidental = 0;
        while(!atEnd()) {
                if(s[_pos] == '#')      { accidental++; _pos++; }
                else if(s[_pos] == 'b') { accidental--; _pos++; }
                else break;
        }

        // Octave shifts: + and - (but '-' followed by a digit is a new negative note).
        int octaveShift = 0;
        while(!atEnd()) {
                if(s[_pos] == '+') {
                        octaveShift++;
                        _pos++;
                } else if(s[_pos] == '-'
                          && !std::isdigit(static_cast<unsigned char>(peek(1)))) {
                        octaveShift--;
                        _pos++;
                } else {
                        break;
                }
        }

        // Dots.
        int dots = 0;
        while(!atEnd() && s[_pos] == '.') { dots++; _pos++; }

        // Duration modifier.
        double lengthMod = parseDurationModifier();

        int octave = _params.octave + octaveShift;
        float midiNote = static_cast<float>((octave + 1) * 12 + pitchClass + accidental);

        emitNote(midiNote, dots, lengthMod);
        return;
}

void NoteSequenceParser::parseNumericNote() {
        const std::string &s = _input.str();
        size_t start = _pos;
        if(!atEnd() && s[_pos] == '-') _pos++; // allow leading minus
        while(!atEnd() && std::isdigit(static_cast<unsigned char>(s[_pos]))) _pos++;
        int degree = std::stoi(s.substr(start, _pos - start));

        // Accidentals.
        int accidental = 0;
        while(!atEnd()) {
                if(s[_pos] == '#')      { accidental++; _pos++; }
                else if(s[_pos] == 'b') { accidental--; _pos++; }
                else break;
        }

        // Octave shifts (but '-' followed by a digit is a new negative note).
        int octaveShift = 0;
        while(!atEnd()) {
                if(s[_pos] == '+') {
                        octaveShift++;
                        _pos++;
                } else if(s[_pos] == '-'
                          && !std::isdigit(static_cast<unsigned char>(peek(1)))) {
                        octaveShift--;
                        _pos++;
                } else {
                        break;
                }
        }

        // Dots.
        int dots = 0;
        while(!atEnd() && s[_pos] == '.') { dots++; _pos++; }

        // Duration modifier.
        double lengthMod = parseDurationModifier();

        float midiNote =
                _params.scale.midiNoteForDegree(degree, _params.octave + octaveShift) + accidental;

        emitNote(midiNote, dots, lengthMod);
        return;
}

void NoteSequenceParser::parseRest() {
        const std::string &s = _input.str();
        _pos++; // skip '_'

        int dots = 0;
        while(!atEnd() && s[_pos] == '.') { dots++; _pos++; }

        double lengthMod = parseDurationModifier();

        emitRest(dots, lengthMod);
        return;
}

void NoteSequenceParser::parseBarRest() {
        _pos++; // skip '|'

        // 4/4 time: one bar = 4 quarter notes = 1 whole note.
        double barDuration = 4.0 * 60.0 / _params.tempo;
        double posInBar    = std::fmod(_currentTime, barDuration);
        double restDur     = 0.0;

        if(posInBar > 1e-9) restDur = barDuration - posInBar;

        if(restDur > 1e-9) {
                MusicalNote note;
                note.setRest(true);
                note.setMidiNote(-1.0f);
                note.setStartTime(_currentTime);
                note.setDuration(0.0);
                note.setFullDuration(restDur);
                note.setAmplitude(0.0f);
                _notes.pushToBack(note);
                _currentTime += restDur;
        }
        return;
}

// --- Helpers ---

void NoteSequenceParser::emitNote(float midiNote, int dots, double lengthMod) {
        double noteLen      = _params.noteLength * lengthMod;
        noteLen             = applyDots(noteLen, dots);
        double fullDuration = noteLen * 4.0 * 60.0 / _params.tempo;
        double duration     = fullDuration * static_cast<double>(_params.legato);

        MusicalNote note;
        note.setMidiNote(midiNote);
        note.setStartTime(_currentTime);
        note.setDuration(duration);
        note.setFullDuration(fullDuration);
        note.setAmplitude(_params.amplitude);
        note.setLegato(_params.legato);
        note.setVibrato(_params.vibrato);
        note.setVibratoRate(_params.vibratoRate);
        note.setTremolo(_params.tremolo);
        note.setTremoloRate(_params.tremoloRate);
        note.setRest(false);

        _notes.pushToBack(note);
        _currentTime += fullDuration;
        return;
}

void NoteSequenceParser::emitRest(int dots, double lengthMod) {
        double noteLen      = _params.noteLength * lengthMod;
        noteLen             = applyDots(noteLen, dots);
        double fullDuration = noteLen * 4.0 * 60.0 / _params.tempo;

        MusicalNote note;
        note.setRest(true);
        note.setMidiNote(-1.0f);
        note.setStartTime(_currentTime);
        note.setDuration(0.0);
        note.setFullDuration(fullDuration);
        note.setAmplitude(0.0f);

        _notes.pushToBack(note);
        _currentTime += fullDuration;
        return;
}

void NoteSequenceParser::skipComment() {
        const std::string &s = _input.str();
        while(!atEnd() && s[_pos] != '\n') _pos++;
        return;
}

void NoteSequenceParser::skipWhitespace() {
        const std::string &s = _input.str();
        while(!atEnd() && std::isspace(static_cast<unsigned char>(s[_pos]))) _pos++;
        return;
}

char NoteSequenceParser::current() const {
        const std::string &s = _input.str();
        return _pos < s.size() ? s[_pos] : '\0';
}

char NoteSequenceParser::peek(int offset) const {
        const std::string &s = _input.str();
        size_t idx = _pos + static_cast<size_t>(offset);
        return idx < s.size() ? s[idx] : '\0';
}

bool NoteSequenceParser::atEnd() const {
        return _pos >= _input.str().size();
}

String NoteSequenceParser::readParenArg() {
        const std::string &s = _input.str();
        if(current() != '(') {
                addError("Expected '('");
                return "";
        }
        _pos++; // skip '('
        size_t start = _pos;
        while(!atEnd() && s[_pos] != ')') _pos++;
        auto arg = s.substr(start, _pos - start);
        if(!atEnd()) _pos++; // skip ')'
        return arg;
}

double NoteSequenceParser::parseDurationModifier() {
        const std::string &s = _input.str();
        if(atEnd()) return 1.0;
        if(s[_pos] == '/' || s[_pos] == '*') {
                bool isDivide = (s[_pos] == '/');
                _pos++;
                size_t start = _pos;
                while(!atEnd() && (std::isdigit(static_cast<unsigned char>(s[_pos]))
                                   || s[_pos] == '.'))
                        _pos++;
                if(_pos == start) {
                        addError(String("Expected number after '") +
                                 (isDivide ? "/" : "*") + "'");
                        return 1.0;
                }
                double val = std::stod(s.substr(start, _pos - start));
                if(val == 0.0) {
                        addError("Division by zero in duration modifier");
                        return 1.0;
                }
                return isDivide ? 1.0 / val : val;
        }
        return 1.0;
}

double NoteSequenceParser::parseNoteLengthValue(const String &s) {
        size_t pos = 0;
        auto skipSpaces = [&]() {
                while(pos < s.size() && s[pos] == ' ') pos++;
        };

        skipSpaces();
        if(pos >= s.size() || !std::isdigit(static_cast<unsigned char>(s[pos]))) return -1.0;

        size_t numStart = pos;
        while(pos < s.size() && std::isdigit(static_cast<unsigned char>(s[pos]))) pos++;
        double first = std::stod(s.substr(numStart, pos - numStart));

        // Check for fraction: "1/4".
        if(pos < s.size() && s[pos] == '/') {
                pos++;
                numStart = pos;
                while(pos < s.size() && std::isdigit(static_cast<unsigned char>(s[pos]))) pos++;
                if(pos == numStart) return -1.0;
                double den = std::stod(s.substr(numStart, pos - numStart));
                if(den == 0.0) return -1.0;
                return first / den;
        }

        // Check for mixed: "1 1/2".
        skipSpaces();
        if(pos < s.size() && std::isdigit(static_cast<unsigned char>(s[pos]))) {
                numStart = pos;
                while(pos < s.size() && std::isdigit(static_cast<unsigned char>(s[pos]))) pos++;
                double fracNum = std::stod(s.substr(numStart, pos - numStart));
                if(pos < s.size() && s[pos] == '/') {
                        pos++;
                        numStart = pos;
                        while(pos < s.size() && std::isdigit(static_cast<unsigned char>(s[pos]))) pos++;
                        if(pos == numStart) return first;
                        double den = std::stod(s.substr(numStart, pos - numStart));
                        if(den == 0.0) return first;
                        return first + fracNum / den;
                }
        }

        return first;
}

double NoteSequenceParser::applyDots(double length, int dots) {
        double total = length;
        double add   = length;
        for(int i = 0; i < dots; i++) {
                add /= 2.0;
                total += add;
        }
        return total;
}

void NoteSequenceParser::addError(const String &msg) {
        _errors.pushToBack(msg);
        return;
}

PROMEKI_NAMESPACE_END
