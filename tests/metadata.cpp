/**
 * @file      metadata.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information
 */

#include <promeki/unittest.h>
#include <promeki/metadata.h>
#include <promeki/stringlist.h>

using namespace promeki;

// ============================================================================
// Basic operations
// ============================================================================

PROMEKI_TEST_BEGIN(Metadata_Basic)
    Metadata m;
    PROMEKI_TEST(m.isEmpty());
    PROMEKI_TEST(m.size() == 0);
    PROMEKI_TEST(m.referenceCount() == 1);

    m.set(Metadata::Title, String("Test Title"));
    PROMEKI_TEST(!m.isEmpty());
    PROMEKI_TEST(m.size() == 1);
    PROMEKI_TEST(m.contains(Metadata::Title));
    PROMEKI_TEST(!m.contains(Metadata::Artist));
    PROMEKI_TEST(m.get(Metadata::Title).get<String>() == "Test Title");
PROMEKI_TEST_END()

PROMEKI_TEST_BEGIN(Metadata_SetMultiple)
    Metadata m;
    m.set(Metadata::Title, String("Title"));
    m.set(Metadata::Artist, String("Artist"));
    m.set(Metadata::Gamma, 2.2);
    m.set(Metadata::TrackNumber, 5);
    m.set(Metadata::EnableBWF, true);

    PROMEKI_TEST(m.size() == 5);
    PROMEKI_TEST(m.get(Metadata::Title).get<String>() == "Title");
    PROMEKI_TEST(m.get(Metadata::Artist).get<String>() == "Artist");
    PROMEKI_TEST(m.get(Metadata::Gamma).get<double>() > 2.1);
    PROMEKI_TEST(m.get(Metadata::Gamma).get<double>() < 2.3);
    PROMEKI_TEST(m.get(Metadata::TrackNumber).get<int32_t>() == 5);
    PROMEKI_TEST(m.get(Metadata::EnableBWF).get<bool>() == true);
PROMEKI_TEST_END()

// ============================================================================
// Remove and clear
// ============================================================================

PROMEKI_TEST_BEGIN(Metadata_RemoveAndClear)
    Metadata m;
    m.set(Metadata::Title, String("Title"));
    m.set(Metadata::Artist, String("Artist"));
    PROMEKI_TEST(m.size() == 2);

    m.remove(Metadata::Title);
    PROMEKI_TEST(m.size() == 1);
    PROMEKI_TEST(!m.contains(Metadata::Title));
    PROMEKI_TEST(m.contains(Metadata::Artist));

    m.clear();
    PROMEKI_TEST(m.isEmpty());
    PROMEKI_TEST(m.size() == 0);
PROMEKI_TEST_END()

// ============================================================================
// Copy-on-write
// ============================================================================

PROMEKI_TEST_BEGIN(Metadata_CopyOnWrite)
    Metadata m1;
    m1.set(Metadata::Title, String("Original"));

    Metadata m2 = m1;
    PROMEKI_TEST(m1.referenceCount() == 2);
    PROMEKI_TEST(m2.referenceCount() == 2);
    PROMEKI_TEST(m2.get(Metadata::Title).get<String>() == "Original");

    // Mutate m2 — should detach
    m2.set(Metadata::Title, String("Modified"));
    PROMEKI_TEST(m1.referenceCount() == 1);
    PROMEKI_TEST(m2.referenceCount() == 1);
    PROMEKI_TEST(m1.get(Metadata::Title).get<String>() == "Original");
    PROMEKI_TEST(m2.get(Metadata::Title).get<String>() == "Modified");
PROMEKI_TEST_END()

PROMEKI_TEST_BEGIN(Metadata_CopyOnWriteRemove)
    Metadata m1;
    m1.set(Metadata::Title, String("Title"));
    m1.set(Metadata::Artist, String("Artist"));

    Metadata m2 = m1;
    PROMEKI_TEST(m1.referenceCount() == 2);

    m2.remove(Metadata::Title);
    PROMEKI_TEST(m1.referenceCount() == 1);
    PROMEKI_TEST(m1.size() == 2);
    PROMEKI_TEST(m2.size() == 1);
PROMEKI_TEST_END()

// ============================================================================
// forEach
// ============================================================================

PROMEKI_TEST_BEGIN(Metadata_ForEach)
    Metadata m;
    m.set(Metadata::Title, String("Title"));
    m.set(Metadata::Artist, String("Artist"));

    int count = 0;
    m.forEach([&count](Metadata::ID id, const Variant &val) {
        count++;
    });
    PROMEKI_TEST(count == 2);
PROMEKI_TEST_END()

// ============================================================================
// ID string conversion
// ============================================================================

PROMEKI_TEST_BEGIN(Metadata_IDConversion)
    PROMEKI_TEST(Metadata::idToString(Metadata::Title) == "Title");
    PROMEKI_TEST(Metadata::idToString(Metadata::Artist) == "Artist");
    PROMEKI_TEST(Metadata::stringToID("Title") == Metadata::Title);
    PROMEKI_TEST(Metadata::stringToID("Artist") == Metadata::Artist);
PROMEKI_TEST_END()

// ============================================================================
// JSON round-trip
// ============================================================================

PROMEKI_TEST_BEGIN(Metadata_JsonRoundTrip)
    Metadata m1;
    m1.set(Metadata::Title, String("Test"));
    m1.set(Metadata::Gamma, 2.2);
    m1.set(Metadata::TrackNumber, 7);

    JsonObject json = m1.toJson();
    PROMEKI_TEST(json.contains("Title"));
    PROMEKI_TEST(json.contains("Gamma"));
    PROMEKI_TEST(json.contains("TrackNumber"));

    bool ok = false;
    Metadata m2 = Metadata::fromJson(json, &ok);
    PROMEKI_TEST(ok);
    PROMEKI_TEST(m2.size() == 3);
    PROMEKI_TEST(m2.get(Metadata::Title).get<String>() == "Test");
    PROMEKI_TEST(m2.get(Metadata::TrackNumber).get<int32_t>() == 7);
PROMEKI_TEST_END()

// ============================================================================
// Dump
// ============================================================================

PROMEKI_TEST_BEGIN(Metadata_Dump)
    Metadata m;
    m.set(Metadata::Title, String("Test Title"));

    StringList dumped = m.dump();
    PROMEKI_TEST(dumped.size() == 1);
PROMEKI_TEST_END()
