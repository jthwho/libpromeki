/**
 * @file      enumlist.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/enum.h>
#include <promeki/enumlist.h>
#include <promeki/enums.h>
#include <promeki/variant.h>
#include <promeki/datastream.h>
#include <promeki/bufferiodevice.h>
#include <promeki/buffer.h>

using namespace promeki;

// ============================================================================
// Construction and state
// ============================================================================

TEST_CASE("EnumList default-constructed is invalid") {
        EnumList list;
        CHECK(!list.isValid());
        CHECK(list.isEmpty());
        CHECK(list.size() == 0);
        CHECK(list.toString().isEmpty());
}

TEST_CASE("EnumList bound to a type is valid and empty") {
        EnumList list(AudioPattern::Type);
        CHECK(list.isValid());
        CHECK(list.isEmpty());
        CHECK(list.elementType() == AudioPattern::Type);
}

TEST_CASE("EnumList::forType<T> binds via the CRTP class") {
        EnumList list = EnumList::forType<AudioPattern>();
        CHECK(list.isValid());
        CHECK(list.elementType() == AudioPattern::Type);
}

// ============================================================================
// Append
// ============================================================================

TEST_CASE("EnumList::append preserves order and duplicates") {
        EnumList list(AudioPattern::Type);
        CHECK(list.append(AudioPattern::Tone));
        CHECK(list.append(AudioPattern::Silence));
        CHECK(list.append(AudioPattern::Tone));

        CHECK(list.size() == 3);
        CHECK(list[0] == AudioPattern::Tone);
        CHECK(list[1] == AudioPattern::Silence);
        CHECK(list[2] == AudioPattern::Tone);
}

TEST_CASE("EnumList::append rejects wrong-type values") {
        EnumList list(AudioPattern::Type);
        Error    err;
        // Wrong type (VideoPattern) must be refused.
        bool ok = list.append(VideoPattern::ColorBars, &err);
        CHECK_FALSE(ok);
        CHECK(err.isError());
        CHECK(list.isEmpty());
}

TEST_CASE("EnumList::append rejects everything on an unbound list") {
        EnumList list;
        Error    err;
        CHECK_FALSE(list.append(AudioPattern::Tone, &err));
        CHECK(err.isError());
}

TEST_CASE("EnumList::append accepts a raw integer") {
        EnumList list(AudioPattern::Type);
        CHECK(list.append(0)); // Tone
        CHECK(list.size() == 1);
        CHECK(list[0] == AudioPattern::Tone);
}

TEST_CASE("EnumList::append accepts a value name") {
        EnumList list(AudioPattern::Type);
        Error    err;
        CHECK(list.append(String("Silence"), &err));
        CHECK(err.isOk());
        CHECK(list.size() == 1);
        CHECK(list[0] == AudioPattern::Silence);
}

TEST_CASE("EnumList::append by name rejects unknown names") {
        EnumList list(AudioPattern::Type);
        Error    err;
        CHECK_FALSE(list.append(String("NotARealPattern"), &err));
        CHECK(err.isError());
        CHECK(list.isEmpty());
}

// ============================================================================
// uniqueSorted
// ============================================================================

TEST_CASE("EnumList::uniqueSorted dedups and sorts by integer value") {
        EnumList list(AudioPattern::Type);
        list.append(AudioPattern::AvSync);  // 3
        list.append(AudioPattern::Tone);    // 0
        list.append(AudioPattern::Tone);    // 0 (dup)
        list.append(AudioPattern::LTC);     // 2
        list.append(AudioPattern::Silence); // 1
        list.append(AudioPattern::AvSync);  // 3 (dup)

        EnumList sorted = list.uniqueSorted();
        CHECK(sorted.size() == 4);
        CHECK(sorted[0] == AudioPattern::Tone);
        CHECK(sorted[1] == AudioPattern::Silence);
        CHECK(sorted[2] == AudioPattern::LTC);
        CHECK(sorted[3] == AudioPattern::AvSync);
        CHECK(sorted.elementType() == AudioPattern::Type);

        // Original list unchanged.
        CHECK(list.size() == 6);
}

TEST_CASE("EnumList::uniqueSorted on an empty list produces an empty list") {
        EnumList list(AudioPattern::Type);
        EnumList sorted = list.uniqueSorted();
        CHECK(sorted.isValid());
        CHECK(sorted.isEmpty());
        CHECK(sorted.elementType() == AudioPattern::Type);
}

// ============================================================================
// toString / fromString
// ============================================================================

TEST_CASE("EnumList::toString produces a comma-separated list of names") {
        EnumList list(AudioPattern::Type);
        list.append(AudioPattern::Tone);
        list.append(AudioPattern::Silence);
        list.append(AudioPattern::LTC);
        CHECK(list.toString() == "Tone,Silence,LTC");
}

TEST_CASE("EnumList::toString on empty returns an empty string") {
        EnumList list(AudioPattern::Type);
        CHECK(list.toString().isEmpty());
}

TEST_CASE("EnumList::fromString parses a comma list") {
        Error    err;
        EnumList parsed = EnumList::fromString(AudioPattern::Type, "Tone,Silence,LTC", &err);
        CHECK(err.isOk());
        CHECK(parsed.size() == 3);
        CHECK(parsed[0] == AudioPattern::Tone);
        CHECK(parsed[1] == AudioPattern::Silence);
        CHECK(parsed[2] == AudioPattern::LTC);
}

TEST_CASE("EnumList::fromString trims whitespace around entries") {
        Error    err;
        EnumList parsed = EnumList::fromString(AudioPattern::Type, "  Tone , Silence ", &err);
        CHECK(err.isOk());
        CHECK(parsed.size() == 2);
        CHECK(parsed[0] == AudioPattern::Tone);
        CHECK(parsed[1] == AudioPattern::Silence);
}

TEST_CASE("EnumList::fromString accepts empty input") {
        Error    err;
        EnumList parsed = EnumList::fromString(AudioPattern::Type, String(), &err);
        CHECK(err.isOk());
        CHECK(parsed.isValid());
        CHECK(parsed.isEmpty());
}

TEST_CASE("EnumList::fromString rejects an invalid name") {
        Error    err;
        EnumList parsed = EnumList::fromString(AudioPattern::Type, "Tone,NotARealPattern", &err);
        CHECK(err.isError());
        CHECK(!parsed.isValid());
}

TEST_CASE("EnumList::fromString silently drops empty entries between commas") {
        // String::split() treats empty tokens as absent, so "Tone,,Silence"
        // parses as two entries — that behavior is inherited here to stay
        // consistent with the rest of the library's comma-joined parsers.
        Error    err;
        EnumList parsed = EnumList::fromString(AudioPattern::Type, "Tone,,Silence", &err);
        CHECK(err.isOk());
        CHECK(parsed.size() == 2);
        CHECK(parsed[0] == AudioPattern::Tone);
        CHECK(parsed[1] == AudioPattern::Silence);
}

TEST_CASE("EnumList::fromString falls back to decimal for out-of-list values") {
        // 42 is not a registered value — decimal fallback should still
        // accept it so toString/fromString round-trips cleanly when a
        // list carries an out-of-list integer from another process.
        Error    err;
        EnumList parsed = EnumList::fromString(AudioPattern::Type, "Tone,42,LTC", &err);
        CHECK(err.isOk());
        CHECK(parsed.size() == 3);
        CHECK(parsed[1].value() == 42);
}

TEST_CASE("EnumList toString/fromString round-trips") {
        EnumList original(AudioPattern::Type);
        original.append(AudioPattern::AvSync);
        original.append(AudioPattern::Tone);
        original.append(AudioPattern::AvSync);

        Error    err;
        EnumList parsed = EnumList::fromString(AudioPattern::Type, original.toString(), &err);
        CHECK(err.isOk());
        CHECK(parsed == original);
}

// ============================================================================
// Variant integration
// ============================================================================

TEST_CASE("EnumList stores and extracts from a Variant") {
        EnumList list(AudioPattern::Type);
        list.append(AudioPattern::Tone);
        list.append(AudioPattern::Silence);

        Variant v(list);
        CHECK(v.type() == Variant::TypeEnumList);

        EnumList out = v.get<EnumList>();
        CHECK(out == list);
}

TEST_CASE("Variant::get<String>() on an EnumList returns the comma form") {
        EnumList list(AudioPattern::Type);
        list.append(AudioPattern::Tone);
        list.append(AudioPattern::Silence);
        Variant v(list);
        CHECK(v.get<String>() == "Tone,Silence");
}

// ============================================================================
// DataStream round-trip
// ============================================================================

TEST_CASE("EnumList round-trips through DataStream") {
        EnumList original(AudioPattern::Type);
        original.append(AudioPattern::Tone);
        original.append(AudioPattern::LTC);
        original.append(AudioPattern::Tone);
        original.append(AudioPattern::AvSync);

        // BufferIODevice does not grow the underlying Buffer; pre-size
        // it to something comfortably larger than the serialized payload.
        Buffer         buf(4096);
        BufferIODevice dev(&buf);
        dev.open(IODevice::ReadWrite);

        {
                DataStream ws = DataStream::createWriter(&dev);
                ws << original;
                CHECK(ws.status() == DataStream::Ok);
        }
        dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&dev);
                EnumList   roundtrip;
                rs >> roundtrip;
                CHECK(rs.status() == DataStream::Ok);
                CHECK(roundtrip == original);
        }
}

TEST_CASE("EnumList round-trips through a Variant DataStream dispatch") {
        EnumList original(VideoPattern::Type);
        original.append(VideoPattern::ColorBars);
        original.append(VideoPattern::Noise);

        Buffer         buf(4096);
        BufferIODevice dev(&buf);
        dev.open(IODevice::ReadWrite);

        {
                DataStream ws = DataStream::createWriter(&dev);
                Variant    v(original);
                ws << v;
                CHECK(ws.status() == DataStream::Ok);
        }
        dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&dev);
                Variant    out;
                rs >> out;
                CHECK(rs.status() == DataStream::Ok);
                CHECK(out.type() == Variant::TypeEnumList);
                CHECK(out.get<EnumList>() == original);
        }
}
