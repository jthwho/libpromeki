/**
 * @file      midinote.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/midinote.h>
#include <cmath>

using namespace promeki;

TEST_CASE("MidiNote") {

        SUBCASE("Default constructor is invalid") {
                MidiNote note;
                CHECK_FALSE(note.isValid());
                CHECK(note.rawValue() == 0xFF);
                CHECK(note.value() == MidiNote::Invalid);
        }

        SUBCASE("Value enum constructor") {
                MidiNote note(MidiNote::C4);
                CHECK(note.isValid());
                CHECK(note.value() == MidiNote::C4);
                CHECK(note.rawValue() == 60);

                MidiNote a4(MidiNote::A4);
                CHECK(a4.value() == MidiNote::A4);
                CHECK(a4.rawValue() == 69);

                MidiNote invalid(MidiNote::Invalid);
                CHECK_FALSE(invalid.isValid());
        }

        SUBCASE("uint8_t constructor") {
                MidiNote note(uint8_t(60));
                CHECK(note.isValid());
                CHECK(note.rawValue() == 60);

                MidiNote zero(uint8_t(0));
                CHECK(zero.isValid());
                CHECK(zero.rawValue() == 0);

                MidiNote max(uint8_t(127));
                CHECK(max.isValid());
                CHECK(max.rawValue() == 127);

                MidiNote over(uint8_t(128));
                CHECK_FALSE(over.isValid());

                MidiNote way_over(uint8_t(255));
                CHECK_FALSE(way_over.isValid());
        }

        SUBCASE("Enum value aliases") {
                // Sharp and flat aliases should map to the same value
                CHECK(MidiNote::Cs4 == MidiNote::Db4);
                CHECK(MidiNote::Ds4 == MidiNote::Eb4);
                CHECK(MidiNote::Fs4 == MidiNote::Gb4);
                CHECK(MidiNote::Gs4 == MidiNote::Ab4);
                CHECK(MidiNote::As4 == MidiNote::Bb4);

                // Spot-check other octaves
                CHECK(MidiNote::Cs_1 == MidiNote::Db_1);
                CHECK(MidiNote::Bb0 == MidiNote::As0);
                CHECK(MidiNote::Gb9 == MidiNote::Fs9);
        }

        SUBCASE("Enum known values") {
                CHECK(static_cast<uint8_t>(MidiNote::C_1) == 0);
                CHECK(static_cast<uint8_t>(MidiNote::C0) == 12);
                CHECK(static_cast<uint8_t>(MidiNote::C4) == 60);
                CHECK(static_cast<uint8_t>(MidiNote::A4) == 69);
                CHECK(static_cast<uint8_t>(MidiNote::G9) == 127);
        }

        SUBCASE("Implicit conversion to uint8_t") {
                MidiNote note(MidiNote::C4);
                uint8_t  raw = note;
                CHECK(raw == 60);
        }

        SUBCASE("Comparison operators") {
                MidiNote c4(MidiNote::C4);
                MidiNote d4(MidiNote::D4);
                MidiNote c4b(MidiNote::C4);

                CHECK(c4 == c4b);
                CHECK(c4 != d4);
                CHECK(c4 < d4);
                CHECK(d4 > c4);
                CHECK(c4 <= c4b);
                CHECK(c4 <= d4);
                CHECK(d4 >= c4);
        }

        SUBCASE("Instance pitchClassName") {
                MidiNote note(MidiNote::C4);
                CHECK(std::string(note.pitchClassName()) == "C");

                MidiNote cs(MidiNote::Cs4);
                CHECK(std::string(cs.pitchClassName()) == "C#");
        }

        SUBCASE("Instance pitchClassNameFlat") {
                MidiNote note(MidiNote::Db4);
                CHECK(std::string(note.pitchClassNameFlat()) == "Db");
        }

        SUBCASE("Instance octave") {
                CHECK(MidiNote(MidiNote::C4).octave() == 4);
                CHECK(MidiNote(MidiNote::C_1).octave() == -1);
                CHECK(MidiNote(MidiNote::C0).octave() == 0);
                CHECK(MidiNote(MidiNote::G9).octave() == 9);
        }

        SUBCASE("Instance pitchClass") {
                CHECK(MidiNote(MidiNote::C4).pitchClass() == 0);
                CHECK(MidiNote(MidiNote::A4).pitchClass() == 9);
                CHECK(MidiNote(MidiNote::B4).pitchClass() == 11);
        }

        SUBCASE("Instance name") {
                CHECK(MidiNote(MidiNote::C4).name() == "C4");
                CHECK(MidiNote(MidiNote::A4).name() == "A4");
                CHECK(MidiNote(MidiNote::Cs4).name() == "C#4");
        }

        SUBCASE("Instance frequency") {
                CHECK(MidiNote(MidiNote::A4).frequency() == doctest::Approx(440.0));
                CHECK(MidiNote(MidiNote::C4).frequency() == doctest::Approx(261.6256).epsilon(0.001));
                CHECK(MidiNote(MidiNote::A4).frequency(432.0) == doctest::Approx(432.0));
        }

        SUBCASE("Static pitchClassName") {
                CHECK(std::string(MidiNote::pitchClassName(60)) == "C");
                CHECK(std::string(MidiNote::pitchClassName(61)) == "C#");
                CHECK(std::string(MidiNote::pitchClassName(69)) == "A");
                CHECK(std::string(MidiNote::pitchClassName(71)) == "B");
        }

        SUBCASE("Static pitchClassNameFlat") {
                CHECK(std::string(MidiNote::pitchClassNameFlat(61)) == "Db");
                CHECK(std::string(MidiNote::pitchClassNameFlat(63)) == "Eb");
                CHECK(std::string(MidiNote::pitchClassNameFlat(66)) == "Gb");
        }

        SUBCASE("Static octave") {
                CHECK(MidiNote::octave(60) == 4);
                CHECK(MidiNote::octave(0) == -1);
                CHECK(MidiNote::octave(12) == 0);
                CHECK(MidiNote::octave(127) == 9);
        }

        SUBCASE("Static pitchClass") {
                CHECK(MidiNote::pitchClass(60) == 0);
                CHECK(MidiNote::pitchClass(69) == 9);
                CHECK(MidiNote::pitchClass(71) == 11);
        }

        SUBCASE("Static nameFromMidiNote") {
                CHECK(MidiNote::nameFromMidiNote(60) == "C4");
                CHECK(MidiNote::nameFromMidiNote(69) == "A4");
                CHECK(MidiNote::nameFromMidiNote(61) == "C#4");
        }

        SUBCASE("fromName") {
                CHECK(MidiNote::fromName("C4").rawValue() == 60);
                CHECK(MidiNote::fromName("A4").rawValue() == 69);
                CHECK(MidiNote::fromName("C#4").rawValue() == 61);
                CHECK(MidiNote::fromName("Db4").rawValue() == 61);
                CHECK(MidiNote::fromName("Bb3").rawValue() == 58);
                CHECK_FALSE(MidiNote::fromName("").isValid());
                CHECK_FALSE(MidiNote::fromName("X4").isValid());
        }

        SUBCASE("Static frequencyFromMidiNote") {
                CHECK(MidiNote::frequencyFromMidiNote(69) == doctest::Approx(440.0));
                CHECK(MidiNote::frequencyFromMidiNote(60) == doctest::Approx(261.6256).epsilon(0.001));
                CHECK(MidiNote::frequencyFromMidiNote(69, 432.0) == doctest::Approx(432.0));
        }

        SUBCASE("Static midiNoteFromFrequency") {
                CHECK(MidiNote::midiNoteFromFrequency(440.0) == doctest::Approx(69.0));
                CHECK(MidiNote::midiNoteFromFrequency(261.6256) == doctest::Approx(60.0).epsilon(0.001));
        }

        SUBCASE("Roundtrip name conversion") {
                for (int note = 0; note < 128; ++note) {
                        MidiNote mn(static_cast<uint8_t>(note));
                        String   n = mn.name();
                        MidiNote result = MidiNote::fromName(n);
                        CHECK(result.rawValue() == note);
                }
        }

        SUBCASE("Roundtrip static name conversion") {
                for (int note = 0; note < 128; ++note) {
                        String   n = MidiNote::nameFromMidiNote(note);
                        MidiNote result = MidiNote::fromName(n);
                        CHECK(result.rawValue() == note);
                }
        }
}
