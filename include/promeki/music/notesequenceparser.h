/**
 * @file      music/notesequenceparser.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/core/string.h>
#include <promeki/core/map.h>
#include <promeki/core/stringlist.h>
#include <promeki/music/musicalnote.h>
#include <promeki/music/musicalscale.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Parses a domain-specific text notation into a sequence of MusicalNote objects.
 *
 * NoteSequenceParser interprets a compact text language for describing
 * musical sequences.  The language supports:
 *
 * - **Letter notes**: A–G with optional sharps (#) and flats (b),
 *   octave shifts (+/-), dots, and duration modifiers (*N, /N).
 * - **Numeric scale degrees**: Integer scale degrees (0-based) resolved
 *   against the current MusicalScale.
 * - **Rests**: Underscore (_) for a rest, pipe (|) to rest until the
 *   next bar boundary.
 * - **Parameter commands**: T(bpm), N(length), A(amplitude), O(octave),
 *   S(scale), L(legato), V(depth[,rate]), TR(depth[,rate]).
 * - **Parameter stack**: NP (push), PP (pop), PS(name) (save),
 *   PR(name) (recall).
 * - **Comments**: // to end of line.
 *
 * @code
 * NoteSequenceParser parser;
 * auto notes = parser.parse("T(120) S(C Major) O(4) N(1/4) C D E F G");
 * @endcode
 */
class NoteSequenceParser {
        public:
                /**
                 * @brief Parses the input string and returns a list of notes.
                 * @param input The notation string.
                 * @return The resulting note sequence.
                 */
                MusicalNote::List parse(const String &input);

                /**
                 * @brief Returns any errors encountered during the last parse.
                 * @return List of error message strings.
                 */
                const StringList &errors() const { return _errors; }

        private:
                struct Params {
                        double       tempo       = 100.0;
                        double       noteLength  = 0.25;
                        float        amplitude   = 0.5f;
                        int          octave      = 4;
                        MusicalScale scale;
                        float        legato      = 0.5f;
                        float        vibrato     = 0.0f;
                        float        vibratoRate = 5.0f;
                        float        tremolo     = 0.0f;
                        float        tremoloRate = 5.0f;
                };

                String                        _input;
                size_t                        _pos         = 0;
                double                        _currentTime = 0.0;
                Params                        _params;
                List<Params>                  _paramStack;
                Map<String, Params> _namedParams;
                MusicalNote::List             _notes;
                StringList                    _errors;

                void parseToken();
                void parseTempo();
                void parseNoteLengthParam();
                void parseAmplitude();
                void parseOctave();
                void parseScaleParam();
                void parseLegato();
                void parseVibrato();
                void parseTremolo();
                void parseLetterNote();
                void parseNumericNote();
                void parseRest();
                void parseBarRest();
                void parseSaveParams();
                void parseRecallParams();
                void pushParams();
                void popParams();
                void skipComment();
                void skipWhitespace();

                char        current() const;
                char        peek(int offset) const;
                bool        atEnd() const;
                String readParenArg();
                double      parseDurationModifier();
                void        emitNote(float midiNote, int dots, double lengthMod);
                void        emitRest(int dots, double lengthMod);
                void        addError(const String &msg);

                static double parseNoteLengthValue(const String &s);
                static double applyDots(double length, int dots);
};

PROMEKI_NAMESPACE_END
