/**
 * @file      musicalnote.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/musicalnote.h>

using namespace promeki;

TEST_CASE("MusicalNote: default construction") {
        MusicalNote n;
        CHECK(n.midiNote() < 0.0f);
        CHECK(n.startTime() == 0.0);
        CHECK(n.duration() == 0.0);
        CHECK(n.amplitude() == doctest::Approx(0.5f));
        CHECK_FALSE(n.isRest());
}

TEST_CASE("MusicalNote: set and get midiNote") {
        MusicalNote n;
        n.setMidiNote(60.0f);
        CHECK(n.midiNote() == doctest::Approx(60.0f));
}

TEST_CASE("MusicalNote: set and get startTime") {
        MusicalNote n;
        n.setStartTime(1.5);
        CHECK(n.startTime() == doctest::Approx(1.5));
}

TEST_CASE("MusicalNote: set and get duration") {
        MusicalNote n;
        n.setDuration(0.25);
        CHECK(n.duration() == doctest::Approx(0.25));
}

TEST_CASE("MusicalNote: set and get fullDuration") {
        MusicalNote n;
        n.setFullDuration(0.5);
        CHECK(n.fullDuration() == doctest::Approx(0.5));
}

TEST_CASE("MusicalNote: set and get amplitude") {
        MusicalNote n;
        n.setAmplitude(0.8f);
        CHECK(n.amplitude() == doctest::Approx(0.8f));
}

TEST_CASE("MusicalNote: set and get legato") {
        MusicalNote n;
        n.setLegato(0.9f);
        CHECK(n.legato() == doctest::Approx(0.9f));
}

TEST_CASE("MusicalNote: vibrato settings") {
        MusicalNote n;
        n.setVibrato(0.3f);
        n.setVibratoRate(6.0f);
        CHECK(n.vibrato() == doctest::Approx(0.3f));
        CHECK(n.vibratoRate() == doctest::Approx(6.0f));
}

TEST_CASE("MusicalNote: tremolo settings") {
        MusicalNote n;
        n.setTremolo(0.4f);
        n.setTremoloRate(7.0f);
        CHECK(n.tremolo() == doctest::Approx(0.4f));
        CHECK(n.tremoloRate() == doctest::Approx(7.0f));
}

TEST_CASE("MusicalNote: rest flag") {
        MusicalNote n;
        CHECK_FALSE(n.isRest());
        n.setRest(true);
        CHECK(n.isRest());
}

TEST_CASE("MusicalNote: default vibrato rate") {
        MusicalNote n;
        CHECK(n.vibratoRate() == doctest::Approx(5.0f));
}

TEST_CASE("MusicalNote: default tremolo rate") {
        MusicalNote n;
        CHECK(n.tremoloRate() == doctest::Approx(5.0f));
}

TEST_CASE("MusicalNote: List type") {
        MusicalNote::List notes;
        MusicalNote n;
        n.setMidiNote(60.0f);
        notes.pushToBack(n);
        CHECK(notes.size() == 1);
}
