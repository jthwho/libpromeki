/**
 * @file      ancpayload.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/ancpayload.h>
#include <promeki/st291packet.h>
#include <promeki/ancmeta.h>
#include <promeki/datastream.h>
#include <promeki/bufferiodevice.h>

using namespace promeki;

// Helpers to make small St291 packets for the tests.
static St291Packet makeCea708Packet(uint16_t line) {
        List<uint16_t> udw;
        udw.pushToBack(uint16_t(0x10));
        udw.pushToBack(uint16_t(0x20));
        return St291Packet::build(AncFormat(AncFormat::Cea708), udw, line);
}

static St291Packet makeAtcPacket(uint16_t line) {
        List<uint16_t> udw;
        for (uint8_t b = 0; b < 8; ++b) udw.pushToBack(static_cast<uint16_t>(b));
        return St291Packet::build(AncFormat(AncFormat::AtcLtc), udw, line);
}

static St291Packet makeAfdPacket(uint16_t line) {
        List<uint16_t> udw;
        udw.pushToBack(uint16_t(0x08)); // pretend AFD code 8 (16:9 full frame)
        return St291Packet::build(AncFormat(AncFormat::Afd), udw, line);
}

// ============================================================================
// Construction
// ============================================================================

TEST_CASE("AncPayload: default constructor is empty") {
        AncPayload p;
        CHECK(p.packets().isEmpty());
        CHECK_FALSE(p.desc().isValid());
        CHECK(p.kind() == MediaPayloadKind::AncillaryData);
        CHECK_FALSE(p.isCompressed());
        CHECK(p.hasDuration());
}

TEST_CASE("AncPayload: descriptor-only constructor") {
        AncDesc desc(Size2Du32(1920, 1080), VideoScanMode::Progressive, FrameRate::FPS_60);
        AncPayload p(desc);
        CHECK(p.desc().isValid());
        CHECK(p.packets().isEmpty());
}

TEST_CASE("AncPayload: descriptor + packet list constructor") {
        AncDesc desc(Size2Du32(1920, 1080), VideoScanMode::Progressive, FrameRate::FPS_60);
        AncPacket::List packets;
        packets.pushToBack(makeCea708Packet(11));
        packets.pushToBack(makeAtcPacket(13));
        AncPayload p(desc, packets);
        CHECK(p.packets().size() == 2);
}

// ============================================================================
// Packet list mutation
// ============================================================================

TEST_CASE("AncPayload: addPacket appends; clearPackets empties") {
        AncPayload p;
        p.addPacket(makeCea708Packet(11));
        p.addPacket(makeAtcPacket(13));
        CHECK(p.packets().size() == 2);

        p.clearPackets();
        CHECK(p.packets().isEmpty());
}

// ============================================================================
// Filter helpers
// ============================================================================

TEST_CASE("AncPayload: packetsOfFormat returns matching packets") {
        AncPayload p;
        p.addPacket(makeCea708Packet(11));
        p.addPacket(makeAtcPacket(13));
        p.addPacket(makeCea708Packet(15));
        p.addPacket(makeAfdPacket(9));

        AncPacket::List cdp = p.packetsOfFormat(AncFormat(AncFormat::Cea708));
        CHECK(cdp.size() == 2);
        CHECK(cdp.at(0).format().id() == AncFormat::Cea708);
        CHECK(cdp.at(1).format().id() == AncFormat::Cea708);
}

TEST_CASE("AncPayload: packetsOfCategory groups by category") {
        AncPayload p;
        p.addPacket(makeCea708Packet(11));
        p.addPacket(makeAtcPacket(13));
        p.addPacket(makeAfdPacket(9));

        AncPacket::List captions = p.packetsOfCategory(AncCategory::Captions);
        CHECK(captions.size() == 1);
        AncPacket::List timecode = p.packetsOfCategory(AncCategory::Timecode);
        CHECK(timecode.size() == 1);
        AncPacket::List aspect = p.packetsOfCategory(AncCategory::Aspect);
        CHECK(aspect.size() == 1);
        AncPacket::List hdr = p.packetsOfCategory(AncCategory::Hdr);
        CHECK(hdr.isEmpty());
}

TEST_CASE("AncPayload: packetsOfTransport filters by transport") {
        AncPayload p;
        p.addPacket(makeCea708Packet(11));
        p.addPacket(makeAtcPacket(13));

        AncPacket::List st291 = p.packetsOfTransport(AncTransport::St291);
        CHECK(st291.size() == 2);
        AncPacket::List ndi = p.packetsOfTransport(AncTransport::NdiXml);
        CHECK(ndi.isEmpty());
}

TEST_CASE("AncPayload: hasFormat / hasCategory predicates") {
        AncPayload p;
        p.addPacket(makeCea708Packet(11));

        CHECK(p.hasFormat(AncFormat(AncFormat::Cea708)));
        CHECK_FALSE(p.hasFormat(AncFormat(AncFormat::AtcLtc)));
        CHECK(p.hasCategory(AncCategory::Captions));
        CHECK_FALSE(p.hasCategory(AncCategory::Timecode));
}

// ============================================================================
// Duration
// ============================================================================

TEST_CASE("AncPayload: setDuration / duration round-trips") {
        AncPayload p;
        CHECK(p.duration().nanoseconds() == 0);
        p.setDuration(Duration::fromNanoseconds(16666666));
        CHECK(p.duration().nanoseconds() == 16666666);
}

// ============================================================================
// Metadata forwarding
// ============================================================================

TEST_CASE("AncPayload: metadata() forwards to desc().metadata()") {
        AncPayload p(AncDesc(Size2Du32(1920, 1080), VideoScanMode::Progressive, FrameRate::FPS_60));
        p.metadata().set(Metadata::Title, String("ANC stream metadata test"));
        CHECK(p.desc().metadata().get(Metadata::Title).get<String>() == "ANC stream metadata test");
}

// ============================================================================
// MediaPayload polymorphic dispatch
// ============================================================================

TEST_CASE("AncPayload: subclassFourCC matches the registered FourCC") {
        AncPayload p;
        CHECK(p.subclassFourCC() == AncPayload::kSubclassFourCC.value());
}

TEST_CASE("AncPayload: clone yields an equivalent AncPayload") {
        AncPayload original;
        original.setDesc(AncDesc(Size2Du32(1920, 1080), VideoScanMode::Progressive, FrameRate::FPS_60));
        original.addPacket(makeCea708Packet(11));

        AncPayload *cloned = original._promeki_clone();
        REQUIRE(cloned != nullptr);
        CHECK(cloned->packets().size() == 1);
        CHECK(cloned->desc().sourceRaster() == Size2Du32(1920, 1080));
        delete cloned;
}

// ============================================================================
// DataStream round-trip via serialisePayload / deserialisePayload
// ============================================================================

TEST_CASE("AncPayload: serialisePayload / deserialisePayload preserve state") {
        AncPayload original;
        original.setDesc(AncDesc(Size2Du32(1920, 1080), VideoScanMode::Progressive, FrameRate::FPS_60));
        original.setDuration(Duration::fromNanoseconds(16666666));
        original.addPacket(makeCea708Packet(11));
        original.addPacket(makeAtcPacket(13));

        Buffer         storage(16384);
        BufferIODevice dev(&storage);
        dev.open(IODevice::ReadWrite);

        {
                DataStream writer = DataStream::createWriter(&dev);
                original.serialisePayload(writer);
                REQUIRE(writer.status() == DataStream::Ok);
        }
        dev.seek(0);
        AncPayload round;
        {
                DataStream reader = DataStream::createReader(&dev);
                round.deserialisePayload(reader);
                REQUIRE(reader.status() == DataStream::Ok);
        }
        CHECK(round.packets().size() == 2);
        CHECK(round.duration().nanoseconds() == 16666666);
        CHECK(round.desc().sourceRaster() == Size2Du32(1920, 1080));
}
