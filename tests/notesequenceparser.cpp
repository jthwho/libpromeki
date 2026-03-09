/**
 * @file      notesequenceparser.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/notesequenceparser.h>

using namespace promeki;

TEST_CASE("NoteSequenceParser") {
        NoteSequenceParser parser;

        SUBCASE("simple letter notes") {
                auto notes = parser.parse("T(120) N(1/4) O(4) C D E");
                CHECK(notes.size() == 3);
                CHECK(notes[0].midiNote() == doctest::Approx(60.0f));
                CHECK(notes[1].midiNote() == doctest::Approx(62.0f));
                CHECK(notes[2].midiNote() == doctest::Approx(64.0f));
                CHECK_FALSE(notes[0].isRest());
        }

        SUBCASE("sharps and flats") {
                auto notes = parser.parse("O(4) C# Db");
                CHECK(notes.size() == 2);
                CHECK(notes[0].midiNote() == doctest::Approx(61.0f));
                CHECK(notes[1].midiNote() == doctest::Approx(61.0f));
        }

        SUBCASE("rests") {
                auto notes = parser.parse("T(120) N(1/4) C _ E");
                CHECK(notes.size() == 3);
                CHECK_FALSE(notes[0].isRest());
                CHECK(notes[1].isRest());
                CHECK_FALSE(notes[2].isRest());
                CHECK(notes[1].midiNote() < 0.0f);
        }

        SUBCASE("scale degrees") {
                auto notes = parser.parse("S(C Major) O(4) 0 1 2 3 4");
                CHECK(notes.size() == 5);
                CHECK(notes[0].midiNote() == doctest::Approx(60.0f)); // C
                CHECK(notes[1].midiNote() == doctest::Approx(62.0f)); // D
                CHECK(notes[2].midiNote() == doctest::Approx(64.0f)); // E
                CHECK(notes[3].midiNote() == doctest::Approx(65.0f)); // F
                CHECK(notes[4].midiNote() == doctest::Approx(67.0f)); // G
        }

        SUBCASE("timing") {
                auto notes = parser.parse("T(120) N(1/4) C D");
                CHECK(notes.size() == 2);
                CHECK(notes[0].startTime() == doctest::Approx(0.0));
                CHECK(notes[0].fullDuration() == doctest::Approx(0.5)); // 60/120 = 0.5s per beat
                CHECK(notes[1].startTime() == doctest::Approx(0.5));
        }

        SUBCASE("amplitude") {
                auto notes = parser.parse("A(0.8) C");
                CHECK(notes.size() == 1);
                CHECK(notes[0].amplitude() == doctest::Approx(0.8f));
        }

        SUBCASE("legato") {
                auto notes = parser.parse("T(120) N(1/4) L(0.9) C");
                CHECK(notes.size() == 1);
                CHECK(notes[0].legato() == doctest::Approx(0.9f));
                CHECK(notes[0].duration() == doctest::Approx(notes[0].fullDuration() * 0.9));
        }

        SUBCASE("vibrato") {
                auto notes = parser.parse("V(0.5,6.0) C");
                CHECK(notes.size() == 1);
                CHECK(notes[0].vibrato() == doctest::Approx(0.5f));
                CHECK(notes[0].vibratoRate() == doctest::Approx(6.0f));
        }

        SUBCASE("tremolo") {
                auto notes = parser.parse("TR(0.3,4.0) C");
                CHECK(notes.size() == 1);
                CHECK(notes[0].tremolo() == doctest::Approx(0.3f));
                CHECK(notes[0].tremoloRate() == doctest::Approx(4.0f));
        }

        SUBCASE("octave shift") {
                auto notes = parser.parse("O(4) C C+ C-");
                CHECK(notes.size() == 3);
                CHECK(notes[0].midiNote() == doctest::Approx(60.0f));
                CHECK(notes[1].midiNote() == doctest::Approx(72.0f));
                CHECK(notes[2].midiNote() == doctest::Approx(48.0f));
        }

        SUBCASE("dotted notes") {
                auto notes = parser.parse("T(120) N(1/4) C C.");
                CHECK(notes.size() == 2);
                // Dotted quarter = 1.5x quarter duration.
                CHECK(notes[1].fullDuration() == doctest::Approx(notes[0].fullDuration() * 1.5));
        }

        SUBCASE("duration modifier") {
                auto notes = parser.parse("T(120) N(1/4) C C*2");
                CHECK(notes.size() == 2);
                CHECK(notes[1].fullDuration() == doctest::Approx(notes[0].fullDuration() * 2.0));
        }

        SUBCASE("parameter stack") {
                auto notes = parser.parse("A(0.5) NP A(0.8) C PP C");
                CHECK(notes.size() == 2);
                CHECK(notes[0].amplitude() == doctest::Approx(0.8f));
                CHECK(notes[1].amplitude() == doctest::Approx(0.5f));
        }

        SUBCASE("no errors on valid input") {
                parser.parse("T(120) S(C Major) O(4) N(1/4) C D E F G A B");
                CHECK(parser.errors().isEmpty());
        }

        SUBCASE("errors on invalid input") {
                parser.parse("T(abc)");
                CHECK_FALSE(parser.errors().isEmpty());
        }

        SUBCASE("comments") {
                auto notes = parser.parse("C // this is a comment\nD");
                CHECK(notes.size() == 2);
        }
}
