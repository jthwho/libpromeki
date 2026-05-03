/**
 * @file      audiomarker.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>

#include <promeki/audiomarker.h>
#include <promeki/buffer.h>
#include <promeki/bufferiodevice.h>
#include <promeki/datastream.h>
#include <promeki/enums.h>
#include <promeki/metadata.h>
#include <promeki/variant.h>

using namespace promeki;

TEST_CASE("AudioMarker: default state") {
        AudioMarker m;
        CHECK(m.offset() == 0);
        CHECK(m.length() == 0);
        CHECK(m.type() == AudioMarkerType::Unknown);
}

TEST_CASE("AudioMarker: explicit construction") {
        AudioMarker m(128, 256, AudioMarkerType::SilenceFill);
        CHECK(m.offset() == 128);
        CHECK(m.length() == 256);
        CHECK(m.type() == AudioMarkerType::SilenceFill);
}

TEST_CASE("AudioMarker: equality is field-wise") {
        AudioMarker a(10, 20, AudioMarkerType::Glitch);
        AudioMarker b(10, 20, AudioMarkerType::Glitch);
        AudioMarker c(10, 20, AudioMarkerType::SilenceFill); // different type
        AudioMarker d(11, 20, AudioMarkerType::Glitch);     // different offset
        CHECK(a == b);
        CHECK(a != c);
        CHECK(a != d);
}

TEST_CASE("AudioMarker: toString / fromString round-trip with length") {
        AudioMarker m(64, 128, AudioMarkerType::ConcealedLoss);
        String      s = m.toString();
        CHECK(s == "ConcealedLoss@64+128");

        auto parsed = AudioMarker::fromString(s);
        REQUIRE(parsed.second().isOk());
        CHECK(parsed.first() == m);
}

TEST_CASE("AudioMarker: toString omits +length when zero") {
        AudioMarker m(42, 0, AudioMarkerType::Discontinuity);
        CHECK(m.toString() == "Discontinuity@42");

        auto parsed = AudioMarker::fromString("Discontinuity@42");
        REQUIRE(parsed.second().isOk());
        CHECK(parsed.first() == m);
}

TEST_CASE("AudioMarker: fromString rejects malformed input") {
        // Missing @
        CHECK(AudioMarker::fromString("SilenceFill 128").second().isError());
        // Bad type
        CHECK(AudioMarker::fromString("BogusType@1+2").second().isError());
        // Non-integer offset
        CHECK(AudioMarker::fromString("SilenceFill@abc+10").second().isError());
        // Non-integer length
        CHECK(AudioMarker::fromString("SilenceFill@10+xyz").second().isError());
}

TEST_CASE("AudioMarkerList: empty by default") {
        AudioMarkerList list;
        CHECK(list.isEmpty());
        CHECK(list.size() == 0);
        CHECK(list.toString() == String());
}

TEST_CASE("AudioMarkerList: append and aggregate accessors") {
        AudioMarkerList list;
        list.append(0, 100, AudioMarkerType::SilenceFill);
        list.append(200, 50, AudioMarkerType::SilenceFill);
        list.append(300, 0, AudioMarkerType::Discontinuity);

        CHECK(list.size() == 3);
        CHECK_FALSE(list.isEmpty());
        CHECK(list.totalLengthFor(AudioMarkerType::SilenceFill) == 150);
        CHECK(list.countFor(AudioMarkerType::SilenceFill) == 2);
        CHECK(list.totalLengthFor(AudioMarkerType::Discontinuity) == 0);
        CHECK(list.countFor(AudioMarkerType::Discontinuity) == 1);
        CHECK(list.totalLengthFor(AudioMarkerType::Glitch) == 0);
}

TEST_CASE("AudioMarkerList: clear() empties the list") {
        AudioMarkerList list({AudioMarker(0, 10, AudioMarkerType::SilenceFill)});
        REQUIRE(list.size() == 1);
        list.clear();
        CHECK(list.isEmpty());
}

TEST_CASE("AudioMarkerList: toString / fromString round-trip") {
        AudioMarkerList list;
        list.append(0, 32, AudioMarkerType::SilenceFill);
        list.append(64, 0, AudioMarkerType::Discontinuity);
        list.append(128, 256, AudioMarkerType::ConcealedLoss);

        String text = list.toString();
        CHECK(text == "SilenceFill@0+32, Discontinuity@64, ConcealedLoss@128+256");

        auto parsed = AudioMarkerList::fromString(text);
        REQUIRE(parsed.second().isOk());
        CHECK(parsed.first() == list);
}

TEST_CASE("AudioMarkerList: empty string parses to empty list") {
        auto parsed = AudioMarkerList::fromString(String());
        REQUIRE(parsed.second().isOk());
        CHECK(parsed.first().isEmpty());
}

TEST_CASE("AudioMarkerList: malformed entry causes whole-list parse failure") {
        auto parsed = AudioMarkerList::fromString("SilenceFill@0+10, garbage, Discontinuity@100");
        CHECK(parsed.second().isError());
}

TEST_CASE("AudioMarkerList: DataStream round-trip preserves all entries") {
        AudioMarkerList list;
        list.append(0, 32, AudioMarkerType::SilenceFill);
        list.append(64, 0, AudioMarkerType::Discontinuity);
        list.append(128, 256, AudioMarkerType::ConcealedLoss);

        Buffer         buf(4096);
        BufferIODevice dev(&buf);
        REQUIRE(dev.open(IODevice::ReadWrite).isOk());
        {
                DataStream ws = DataStream::createWriter(&dev);
                ws << list;
        }
        dev.seek(0);
        DataStream      rs = DataStream::createReader(&dev);
        AudioMarkerList back;
        rs >> back;
        CHECK(back == list);
}

TEST_CASE("AudioMarkerList: round-trips through Variant + Metadata") {
        AudioMarkerList list;
        list.append(48, 96, AudioMarkerType::SilenceFill);

        Metadata md;
        md.set(Metadata::AudioMarkers, list);

        AudioMarkerList back = md.getAs<AudioMarkerList>(Metadata::AudioMarkers);
        CHECK(back == list);
}
