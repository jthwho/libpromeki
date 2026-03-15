/**
 * @file      midinotenames.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/music/midinotenames.h>

using namespace promeki;

TEST_CASE("MidiNoteNames") {

        SUBCASE("Empty name set falls back to MidiNote::name") {
                MidiNoteNames names;
                CHECK(names.name(MidiNote(MidiNote::C4)) == "C4");
                CHECK(names.name(MidiNote(MidiNote::A4)) == "A4");
                CHECK(names.name(MidiNote(MidiNote::Cs4)) == "C#4");
        }

        SUBCASE("Custom name overrides default") {
                MidiNoteNames names;
                MidiNote c4(MidiNote::C4);
                names.setName(c4, "Middle C");
                CHECK(names.name(c4) == "Middle C");
        }

        SUBCASE("hasName") {
                MidiNoteNames names;
                MidiNote c4(MidiNote::C4);
                CHECK_FALSE(names.hasName(c4));
                names.setName(c4, "Middle C");
                CHECK(names.hasName(c4));
        }

        SUBCASE("clearName reverts to default") {
                MidiNoteNames names;
                MidiNote c4(MidiNote::C4);
                names.setName(c4, "Middle C");
                CHECK(names.name(c4) == "Middle C");
                names.clearName(c4);
                CHECK(names.name(c4) == "C4");
                CHECK_FALSE(names.hasName(c4));
        }

        SUBCASE("Invalid note returns empty string") {
                MidiNoteNames names;
                MidiNote invalid;
                CHECK(names.name(invalid).isEmpty());
        }

        SUBCASE("GM Percussion names") {
                auto perc = MidiNoteNames::gmPercussion();

                CHECK(perc.name(MidiNote(MidiNote::GMP_AcousticBassDrum)) == "Acoustic Bass Drum");
                CHECK(perc.name(MidiNote(MidiNote::GMP_BassDrum1)) == "Bass Drum 1");
                CHECK(perc.name(MidiNote(MidiNote::GMP_AcousticSnare)) == "Acoustic Snare");
                CHECK(perc.name(MidiNote(MidiNote::GMP_ClosedHiHat)) == "Closed Hi-Hat");
                CHECK(perc.name(MidiNote(MidiNote::GMP_OpenHiHat)) == "Open Hi-Hat");
                CHECK(perc.name(MidiNote(MidiNote::GMP_CrashCymbal1)) == "Crash Cymbal 1");
                CHECK(perc.name(MidiNote(MidiNote::GMP_RideCymbal1)) == "Ride Cymbal 1");
                CHECK(perc.name(MidiNote(MidiNote::GMP_Cowbell)) == "Cowbell");
                CHECK(perc.name(MidiNote(MidiNote::GMP_Tambourine)) == "Tambourine");
                CHECK(perc.name(MidiNote(MidiNote::GMP_OpenTriangle)) == "Open Triangle");
                CHECK(perc.name(MidiNote(MidiNote::GMP_OpenSurdo)) == "Open Surdo");
        }

        SUBCASE("GM Percussion falls back for non-percussion notes") {
                auto perc = MidiNoteNames::gmPercussion();

                // Notes outside GM percussion range should fall back
                CHECK(perc.name(MidiNote(MidiNote::C_1)) == "C-1");
                CHECK(perc.name(MidiNote(MidiNote::G9)) == "G9");
        }

        SUBCASE("Multiple name sets are independent") {
                MidiNoteNames a;
                MidiNoteNames b;
                MidiNote c4(MidiNote::C4);

                a.setName(c4, "Do");
                b.setName(c4, "Ut");

                CHECK(a.name(c4) == "Do");
                CHECK(b.name(c4) == "Ut");
        }
}
