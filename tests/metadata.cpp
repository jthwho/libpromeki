/**
 * @file      metadata.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/core/metadata.h>
#include <promeki/core/stringlist.h>

using namespace promeki;

// ============================================================================
// Basic operations
// ============================================================================

TEST_CASE("Metadata_Basic") {
    Metadata m;
    CHECK(m.isEmpty());
    CHECK(m.size() == 0);

    m.set(Metadata::Title, String("Test Title"));
    CHECK(!m.isEmpty());
    CHECK(m.size() == 1);
    CHECK(m.contains(Metadata::Title));
    CHECK(!m.contains(Metadata::Artist));
    CHECK(m.get(Metadata::Title).get<String>() == "Test Title");
}

TEST_CASE("Metadata_SetMultiple") {
    Metadata m;
    m.set(Metadata::Title, String("Title"));
    m.set(Metadata::Artist, String("Artist"));
    m.set(Metadata::Gamma, 2.2);
    m.set(Metadata::TrackNumber, 5);
    m.set(Metadata::EnableBWF, true);

    CHECK(m.size() == 5);
    CHECK(m.get(Metadata::Title).get<String>() == "Title");
    CHECK(m.get(Metadata::Artist).get<String>() == "Artist");
    CHECK(m.get(Metadata::Gamma).get<double>() > 2.1);
    CHECK(m.get(Metadata::Gamma).get<double>() < 2.3);
    CHECK(m.get(Metadata::TrackNumber).get<int32_t>() == 5);
    CHECK(m.get(Metadata::EnableBWF).get<bool>() == true);
}

// ============================================================================
// Remove and clear
// ============================================================================

TEST_CASE("Metadata_RemoveAndClear") {
    Metadata m;
    m.set(Metadata::Title, String("Title"));
    m.set(Metadata::Artist, String("Artist"));
    CHECK(m.size() == 2);

    m.remove(Metadata::Title);
    CHECK(m.size() == 1);
    CHECK(!m.contains(Metadata::Title));
    CHECK(m.contains(Metadata::Artist));

    m.clear();
    CHECK(m.isEmpty());
    CHECK(m.size() == 0);
}

// ============================================================================
// Copy semantics (plain value, no internal COW)
// ============================================================================

TEST_CASE("Metadata_CopyIsIndependent") {
    Metadata m1;
    m1.set(Metadata::Title, String("Original"));

    Metadata m2 = m1;
    CHECK(m2.get(Metadata::Title).get<String>() == "Original");

    // Mutating m2 does not affect m1
    m2.set(Metadata::Title, String("Modified"));
    CHECK(m1.get(Metadata::Title).get<String>() == "Original");
    CHECK(m2.get(Metadata::Title).get<String>() == "Modified");
}

TEST_CASE("Metadata_CopyRemoveIsIndependent") {
    Metadata m1;
    m1.set(Metadata::Title, String("Title"));
    m1.set(Metadata::Artist, String("Artist"));

    Metadata m2 = m1;

    m2.remove(Metadata::Title);
    CHECK(m1.size() == 2);
    CHECK(m2.size() == 1);
}

// ============================================================================
// forEach
// ============================================================================

TEST_CASE("Metadata_ForEach") {
    Metadata m;
    m.set(Metadata::Title, String("Title"));
    m.set(Metadata::Artist, String("Artist"));

    int count = 0;
    m.forEach([&count](Metadata::ID id, const Variant &val) {
        count++;
    });
    CHECK(count == 2);
}

// ============================================================================
// ID string conversion
// ============================================================================

TEST_CASE("Metadata_IDConversion") {
    CHECK(Metadata::idToString(Metadata::Title) == "Title");
    CHECK(Metadata::idToString(Metadata::Artist) == "Artist");
    CHECK(Metadata::stringToID("Title") == Metadata::Title);
    CHECK(Metadata::stringToID("Artist") == Metadata::Artist);
}

// ============================================================================
// JSON round-trip
// ============================================================================

TEST_CASE("Metadata_JsonRoundTrip") {
    Metadata m1;
    m1.set(Metadata::Title, String("Test"));
    m1.set(Metadata::Gamma, 2.2);
    m1.set(Metadata::TrackNumber, 7);

    JsonObject json = m1.toJson();
    CHECK(json.contains("Title"));
    CHECK(json.contains("Gamma"));
    CHECK(json.contains("TrackNumber"));

    Error err;
    Metadata m2 = Metadata::fromJson(json, &err);
    CHECK(err.isOk());
    CHECK(m2.size() == 3);
    CHECK(m2.get(Metadata::Title).get<String>() == "Test");
    CHECK(m2.get(Metadata::TrackNumber).get<int32_t>() == 7);
}

// ============================================================================
// Equality
// ============================================================================

TEST_CASE("Metadata_EqualityEmpty") {
    Metadata a;
    Metadata b;
    CHECK(a == b);
}

TEST_CASE("Metadata_EqualityMatching") {
    Metadata a;
    a.set(Metadata::Title, String("Test"));
    a.set(Metadata::Gamma, 2.2);
    Metadata b;
    b.set(Metadata::Title, String("Test"));
    b.set(Metadata::Gamma, 2.2);
    CHECK(a == b);
}

TEST_CASE("Metadata_EqualityDifferentValues") {
    Metadata a;
    a.set(Metadata::Title, String("Foo"));
    Metadata b;
    b.set(Metadata::Title, String("Bar"));
    CHECK_FALSE(a == b);
}

TEST_CASE("Metadata_EqualityDifferentKeys") {
    Metadata a;
    a.set(Metadata::Title, String("Test"));
    Metadata b;
    b.set(Metadata::Artist, String("Test"));
    CHECK_FALSE(a == b);
}

TEST_CASE("Metadata_EqualityDifferentSize") {
    Metadata a;
    a.set(Metadata::Title, String("Test"));
    Metadata b;
    a.set(Metadata::Title, String("Test"));
    b.set(Metadata::Artist, String("Extra"));
    CHECK_FALSE(a == b);
}

// ============================================================================
// Dump
// ============================================================================

TEST_CASE("Metadata_Dump") {
    Metadata m;
    m.set(Metadata::Title, String("Test Title"));

    StringList dumped = m.dump();
    CHECK(dumped.size() == 1);
}
