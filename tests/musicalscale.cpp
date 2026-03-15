/**
 * @file      musicalscale.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cmath>
#include <doctest/doctest.h>
#include <promeki/music/musicalscale.h>

using namespace promeki;

TEST_CASE("MusicalScale") {
        SUBCASE("default construction") {
                MusicalScale scale;
                CHECK(scale.rootPitchClass() == 0);
                CHECK(scale.mode() == MusicalScale::Chromatic);
                CHECK(scale.degreesPerOctave() == 12);
        }

        SUBCASE("construction with root and mode") {
                MusicalScale scale(0, MusicalScale::Major);
                CHECK(scale.rootPitchClass() == 0);
                CHECK(scale.mode() == MusicalScale::Major);
                CHECK(scale.degreesPerOctave() == 7);
        }

        SUBCASE("fromName") {
                auto [scale, err] = MusicalScale::fromName("C Major");
                CHECK(err.isOk());
                CHECK(scale.rootPitchClass() == 0);
                CHECK(scale.mode() == MusicalScale::Major);

                auto [scale2, err2] = MusicalScale::fromName("Eb Blues");
                CHECK(err2.isOk());
                CHECK(scale2.rootPitchClass() == 3);
                CHECK(scale2.mode() == MusicalScale::Blues);

                auto [scale3, err3] = MusicalScale::fromName("F# Minor");
                CHECK(err3.isOk());
                CHECK(scale3.rootPitchClass() == 6);
                CHECK(scale3.mode() == MusicalScale::NaturalMinor);

                auto [bad, badErr] = MusicalScale::fromName("");
                CHECK(badErr.isError());
        }

        SUBCASE("pitchClassFromName") {
                CHECK(MusicalScale::pitchClassFromName("C") == 0);
                CHECK(MusicalScale::pitchClassFromName("C#") == 1);
                CHECK(MusicalScale::pitchClassFromName("Db") == 1);
                CHECK(MusicalScale::pitchClassFromName("D") == 2);
                CHECK(MusicalScale::pitchClassFromName("A") == 9);
                CHECK(MusicalScale::pitchClassFromName("B") == 11);
                CHECK(MusicalScale::pitchClassFromName("Bb") == 10);
                CHECK(MusicalScale::pitchClassFromName("") == -1);
                CHECK(MusicalScale::pitchClassFromName("X") == -1);
        }

        SUBCASE("midiNoteForDegree C Major") {
                MusicalScale scale(0, MusicalScale::Major);
                // C4 = 60, D4 = 62, E4 = 64, F4 = 65, G4 = 67, A4 = 69, B4 = 71
                CHECK(scale.midiNoteForDegree(0, 4) == doctest::Approx(60.0f));
                CHECK(scale.midiNoteForDegree(1, 4) == doctest::Approx(62.0f));
                CHECK(scale.midiNoteForDegree(2, 4) == doctest::Approx(64.0f));
                CHECK(scale.midiNoteForDegree(3, 4) == doctest::Approx(65.0f));
                CHECK(scale.midiNoteForDegree(4, 4) == doctest::Approx(67.0f));
                CHECK(scale.midiNoteForDegree(5, 4) == doctest::Approx(69.0f));
                CHECK(scale.midiNoteForDegree(6, 4) == doctest::Approx(71.0f));
                // Octave wrap.
                CHECK(scale.midiNoteForDegree(7, 4) == doctest::Approx(72.0f));
        }

        SUBCASE("midiNoteForDegree negative degrees") {
                MusicalScale scale(0, MusicalScale::Major);
                CHECK(scale.midiNoteForDegree(-1, 4) == doctest::Approx(59.0f));
        }

        SUBCASE("containsNote") {
                MusicalScale cMajor(0, MusicalScale::Major);
                CHECK(cMajor.containsNote(60));  // C
                CHECK(cMajor.containsNote(62));  // D
                CHECK_FALSE(cMajor.containsNote(61));  // C#
                CHECK_FALSE(cMajor.containsNote(63));  // Eb
        }

        SUBCASE("constrainNote") {
                MusicalScale cMajor(0, MusicalScale::Major);
                // C# should snap to C or D.
                float constrained = cMajor.constrainNote(61.0f, 1.0f);
                // Should be an integer (snapped fully).
                CHECK(constrained == doctest::Approx(std::round(constrained)));
                // Should be in C Major.
                CHECK(cMajor.containsNote(static_cast<int>(constrained)));
        }

        SUBCASE("constrainNote with zero strength") {
                MusicalScale cMajor(0, MusicalScale::Major);
                CHECK(cMajor.constrainNote(61.5f, 0.0f) == doctest::Approx(61.5f));
        }

        SUBCASE("modeName") {
                CHECK(std::string(MusicalScale::modeName(MusicalScale::Major)) == "Major");
                CHECK(std::string(MusicalScale::modeName(MusicalScale::Blues)) == "Blues");
                CHECK(std::string(MusicalScale::modeName(MusicalScale::NaturalMinor)) == "Natural Minor");
        }

        SUBCASE("modeFromName") {
                auto [mode, err] = MusicalScale::modeFromName("major");
                CHECK(err.isOk());
                CHECK(mode == MusicalScale::Major);

                auto [mode2, err2] = MusicalScale::modeFromName("Aeolian");
                CHECK(err2.isOk());
                CHECK(mode2 == MusicalScale::NaturalMinor);

                auto [mode3, err3] = MusicalScale::modeFromName("nonsense");
                CHECK(err3.isError());
        }

        SUBCASE("membershipMaskForMode") {
                auto mask = MusicalScale::membershipMaskForMode(MusicalScale::Pentatonic);
                // Pentatonic: C D E G A = positions 0, 2, 4, 7, 9
                CHECK(mask[0] == 1);
                CHECK(mask[1] == 0);
                CHECK(mask[2] == 1);
                CHECK(mask[4] == 1);
                CHECK(mask[7] == 1);
                CHECK(mask[9] == 1);
        }
}
