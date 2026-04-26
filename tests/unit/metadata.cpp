/**
 * @file      metadata.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/metadata.h>
#include <promeki/fnv1a.h>
#include <promeki/stringlist.h>
#include <promeki/application.h>
#include <promeki/umid.h>
#include <promeki/datastream.h>
#include <promeki/bufferiodevice.h>
#include <promeki/buffer.h>

using namespace promeki;

// Compile-time sanity: ID::literal agrees with the raw FNV-1a hash
// for a real in-tree well-known name.  If this ever fails to compile,
// Item::literal has drifted from the registry's hashing.
static_assert(Metadata::ID::literal("Title").id() == fnv1a("Title"));

// With PROMEKI_DECLARE_ID, Title itself is constexpr, so we can lock
// the declareID path and the literal path together at compile time.
static_assert(Metadata::Title.id() == Metadata::ID::literal("Title").id());
static_assert(Metadata::Title.id() == fnv1a("Title"));

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
        m.forEach([&count](Metadata::ID id, const Variant &val) { count++; });
        CHECK(count == 2);
}

// ============================================================================
// ID string conversion
// ============================================================================

TEST_CASE("Metadata_IDConversion") {
        // Well-known IDs are valid and round-trip through name
        CHECK(Metadata::Title.isValid());
        CHECK(Metadata::Artist.isValid());
        CHECK(Metadata::idToString(Metadata::Title) == "Title");
        CHECK(Metadata::idToString(Metadata::Artist) == "Artist");
        CHECK(Metadata::stringToID("Title") == Metadata::Title);
        CHECK(Metadata::stringToID("Artist") == Metadata::Artist);

        // All well-known IDs are distinct
        CHECK(Metadata::Title != Metadata::Artist);
        CHECK(Metadata::Timecode != Metadata::FrameRate);
        CHECK(Metadata::EnableBWF != Metadata::EnableVBR);

        // Default-constructed ID is invalid
        Metadata::ID invalid;
        CHECK(!invalid.isValid());
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

        Error    err;
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

// ============================================================================
// applyMediaIOWriteDefaults
// ============================================================================

TEST_CASE("Metadata_WriteDefaults_PopulatesAllFields") {
        Application::setAppName(String()); // no application name
        Metadata m;
        m.applyMediaIOWriteDefaults();

        CHECK(m.contains(Metadata::Date));
        CHECK(m.contains(Metadata::OriginationDateTime));
        CHECK(m.contains(Metadata::Software));
        CHECK(m.contains(Metadata::Originator));
        CHECK(m.contains(Metadata::OriginatorReference));
        CHECK(m.contains(Metadata::UMID));

        // Software falls back to the libpromeki tag when appName is empty.
        String sw = m.get(Metadata::Software).get<String>();
        CHECK(sw == "libpromeki (https://howardlogic.com)");

        // Originator is always the libpromeki signature (fits in BWF's 32-char limit).
        String orig = m.get(Metadata::Originator).get<String>();
        CHECK(orig == "libpromeki howardlogic.com");
        CHECK(orig.size() <= 32);

        // UMID is stored as a fresh Extended UMID.
        UMID umid = m.get(Metadata::UMID).get<UMID>();
        CHECK(umid.isValid());
        CHECK(umid.length() == UMID::Extended);
}

TEST_CASE("Metadata_WriteDefaults_UsesApplicationAppName") {
        Application::setAppName(String("myapp"));
        Metadata m;
        m.applyMediaIOWriteDefaults();
        CHECK(m.get(Metadata::Software).get<String>() == "myapp");
        Application::setAppName(String()); // restore for other tests
}

TEST_CASE("Metadata_WriteDefaults_PreservesExistingValues") {
        Metadata m;
        m.set(Metadata::Software, String("CallerSetSoftware"));
        m.set(Metadata::Date, String("1999-01-01"));
        m.set(Metadata::Originator, String("CallerSetOriginator"));
        m.applyMediaIOWriteDefaults();

        CHECK(m.get(Metadata::Software).get<String>() == "CallerSetSoftware");
        CHECK(m.get(Metadata::Date).get<String>() == "1999-01-01");
        CHECK(m.get(Metadata::Originator).get<String>() == "CallerSetOriginator");
        // Missing fields are still populated.
        CHECK(m.contains(Metadata::OriginationDateTime));
        CHECK(m.contains(Metadata::UMID));
}

TEST_CASE("Metadata_WriteDefaults_FreshUMIDEachCall") {
        Metadata a;
        Metadata b;
        a.applyMediaIOWriteDefaults();
        b.applyMediaIOWriteDefaults();
        UMID ua = a.get(Metadata::UMID).get<UMID>();
        UMID ub = b.get(Metadata::UMID).get<UMID>();
        CHECK(ua.isValid());
        CHECK(ub.isValid());
        CHECK(ua != ub);
}

TEST_CASE("Metadata_WriteDefaults_OriginationDateTimeIsIso8601") {
        Metadata m;
        m.applyMediaIOWriteDefaults();
        String s = m.get(Metadata::OriginationDateTime).get<String>();
        // Expect "YYYY-MM-DDTHH:MM:SS" — 19 characters with a 'T' at offset 10.
        CHECK(s.size() == 19);
        CHECK(s[4] == '-');
        CHECK(s[7] == '-');
        CHECK(s[10] == 'T');
        CHECK(s[13] == ':');
        CHECK(s[16] == ':');
}

// ============================================================================
// DataStream round-trip
// ============================================================================
// A Metadata carrying a UMID Variant is the common case after
// MediaIO::open() has applied write defaults.  VariantDatabase
// serialization goes through DataStream::operator<<(const Variant&),
// which must recognize TypeUMID.  Without the dedicated tag this
// test would fail with a "Variant::write: unknown type" error.

TEST_CASE("Metadata_Json_UMIDRoundTripsViaString") {
        // JSON can't distinguish a String from a UMID, so UMID → JSON → UMID
        // goes through its hex string representation.  The Variant's
        // String↔UMID conversion lets the caller recover a typed UMID via
        // `metadata.get(Metadata::UMID).get<UMID>()`.
        Metadata m;
        UMID     inUmid = UMID::generate(UMID::Extended);
        m.set(Metadata::UMID, inUmid);
        m.set(Metadata::Title, String("json umid test"));

        JsonObject j = m.toJson();
        Error      err;
        Metadata   m2 = Metadata::fromJson(j, &err);
        CHECK(err.isOk());
        REQUIRE(m2.contains(Metadata::UMID));

        UMID outUmid = m2.get(Metadata::UMID).get<UMID>();
        CHECK(outUmid.isValid());
        CHECK(outUmid == inUmid);
}

TEST_CASE("Metadata_DataStream_RoundTripWithUMID") {
        Metadata m;
        m.applyMediaIOWriteDefaults();
        m.set(Metadata::Title, String("DataStream test"));

        Buffer         buf(8192);
        BufferIODevice dev(&buf);
        dev.open(IODevice::ReadWrite);

        {
                DataStream writer = DataStream::createWriter(&dev);
                writer << m;
                CHECK(writer.status() == DataStream::Ok);
        }

        dev.seek(0);
        Metadata m2;
        {
                DataStream reader = DataStream::createReader(&dev);
                reader >> m2;
                CHECK(reader.status() == DataStream::Ok);
        }

        CHECK(m2 == m);
        CHECK(m2.contains(Metadata::UMID));
        UMID original = m.get(Metadata::UMID).get<UMID>();
        UMID roundTripped = m2.get(Metadata::UMID).get<UMID>();
        CHECK(original == roundTripped);
        CHECK(roundTripped.isValid());
        CHECK(roundTripped.length() == UMID::Extended);
        CHECK(m2.get(Metadata::Title).get<String>() == "DataStream test");
        CHECK(m2.get(Metadata::Originator).get<String>() == "libpromeki howardlogic.com");
}
