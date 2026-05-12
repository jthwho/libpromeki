/**
 * @file      st291packet.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/st291packet.h>
#include <promeki/ancmeta.h>

using namespace promeki;

// ============================================================================
// Construction via build()
// ============================================================================

TEST_CASE("St291Packet: build a CEA-708 packet on VANC line 11") {
        // Synthesize 8 data bytes — content is arbitrary; we just want
        // the build/parse round-trip to match.
        List<uint16_t> udw;
        for (uint8_t v = 0x10; v < 0x18; ++v) udw.pushToBack(v);

        St291Packet p =
                St291Packet::build(AncFormat(AncFormat::Cea708), udw, /*line*/ 11, /*hOffset*/ 0xFFF,
                                   /*fieldB*/ false, /*cBit*/ false, /*streamNum*/ 0);

        CHECK(p.isValid());
        CHECK(p.line() == 11);
        CHECK(p.hOffset() == 0xFFF);
        CHECK(p.fieldB() == false);
        CHECK(p.cBit() == false);
        CHECK(p.streamNum() == 0);

        CHECK(p.did() == 0x61);    // CEA-708 CDP DID
        CHECK(p.sdid() == 0x01);   // CEA-708 CDP SDID
        CHECK(p.dataCount() == 8); // 8 UDWs
        CHECK(p.checksumValid());

        List<uint16_t> roundtrip = p.udw();
        REQUIRE(roundtrip.size() == 8);
        for (size_t i = 0; i < udw.size(); ++i) {
                // The low 8 bits must match the supplied data byte.
                CHECK((roundtrip.at(i) & 0xFF) == udw.at(i));
        }
}

TEST_CASE("St291Packet: build for every well-known St291 format") {
        struct Case {
                        AncFormat::ID id;
                        uint8_t       expectedDid;
                        uint8_t       expectedSdid;
        };
        Case cases[] = {
                {AncFormat::Cea708, 0x61, 0x01}, {AncFormat::Cea608, 0x61, 0x02}, {AncFormat::Afd, 0x41, 0x05},
                {AncFormat::BarData, 0x41, 0x06}, {AncFormat::Scte104, 0x41, 0x07}, {AncFormat::AtcLtc, 0x60, 0x60},
                {AncFormat::AtcVitc1, 0x60, 0x61}, {AncFormat::AtcVitc2, 0x60, 0x62}, {AncFormat::Klv0601, 0x44, 0x04},
        };

        List<uint16_t> udw;
        udw.pushToBack(uint16_t(0xAB));
        udw.pushToBack(uint16_t(0xCD));

        for (const Case &c : cases) {
                St291Packet p = St291Packet::build(AncFormat(c.id), udw, /*line*/ 9);
                CHECK(p.did() == c.expectedDid);
                CHECK(p.sdid() == c.expectedSdid);
                CHECK(p.dataCount() == 2);
                CHECK(p.checksumValid());
                CHECK(p.packet().format().id() == c.id);
                CHECK(p.packet().transport() == AncTransport::St291);
        }
}

TEST_CASE("St291Packet: build records fieldB/cBit/streamNum/hOffset in meta") {
        List<uint16_t> udw;
        udw.pushToBack(uint16_t(0x42));

        St291Packet p =
                St291Packet::build(AncFormat(AncFormat::Cea708), udw, /*line*/ 21, /*hOffset*/ 0x123,
                                   /*fieldB*/ true, /*cBit*/ true, /*streamNum*/ 5);
        CHECK(p.line() == 21);
        CHECK(p.hOffset() == 0x123);
        CHECK(p.fieldB() == true);
        CHECK(p.cBit() == true);
        CHECK(p.streamNum() == 5);
}

// ============================================================================
// buildRaw for unregistered / wildcard formats
// ============================================================================

TEST_CASE("St291Packet: buildRaw with unregistered DID/SDID still rides through") {
        List<uint16_t> udw;
        udw.pushToBack(uint16_t(0x77));
        // (0xAA, 0xBB) — not registered.
        St291Packet p = St291Packet::buildRaw(0xAA, 0xBB, udw, /*line*/ 9);
        CHECK(p.did() == 0xAA);
        CHECK(p.sdid() == 0xBB);
        CHECK(p.dataCount() == 1);
        CHECK(p.checksumValid());
        // The wrapper format is Invalid (the lookup failed) but the
        // wire bytes still round-trip.
        CHECK_FALSE(p.packet().format().isValid());
}

TEST_CASE("St291Packet: buildRaw with Smpte2020Audio sub-flavour SDID") {
        List<uint16_t> udw;
        udw.pushToBack(uint16_t(0x11));
        udw.pushToBack(uint16_t(0x22));
        // DID 0x45 + SDID 0x03 → Smpte2020Audio (wildcard registration).
        St291Packet p = St291Packet::buildRaw(0x45, 0x03, udw, /*line*/ 9);
        CHECK(p.did() == 0x45);
        CHECK(p.sdid() == 0x03);
        CHECK(p.packet().format().id() == AncFormat::Smpte2020Audio);
        CHECK(p.checksumValid());
}

// ============================================================================
// from()
// ============================================================================

TEST_CASE("St291Packet: from() succeeds for an St291 packet") {
        List<uint16_t> udw;
        udw.pushToBack(uint16_t(0xAB));
        AncPacket           pkt = St291Packet::build(AncFormat(AncFormat::Cea708), udw, 11);
        Result<St291Packet> r = St291Packet::from(pkt);
        REQUIRE(isOk(r));
        CHECK(value(r).did() == 0x61);
}

TEST_CASE("St291Packet: from() rejects wrong transport") {
        // Hand-build a minimally valid AncPacket on the NdiXml transport.
        Buffer fakeData(5);
        Error  e = fakeData.copyFrom("hello", 5);
        REQUIRE(e.isOk());
        fakeData.setSize(5);
        AncPacket           pkt(AncFormat(AncFormat::Cea708), AncTransport::NdiXml, fakeData);
        Result<St291Packet> r = St291Packet::from(pkt);
        CHECK(isError(r));
        CHECK(error(r) == Error::InvalidArgument);
}

TEST_CASE("St291Packet: from() rejects too-short data") {
        Buffer tiny(2);
        Error  e = tiny.copyFrom("ab", 2);
        REQUIRE(e.isOk());
        tiny.setSize(2);
        AncPacket           pkt(AncFormat(AncFormat::Cea708), AncTransport::St291, tiny);
        Result<St291Packet> r = St291Packet::from(pkt);
        CHECK(isError(r));
        CHECK(error(r) == Error::InvalidArgument);
}

// ============================================================================
// Checksum behaviour
// ============================================================================

TEST_CASE("St291Packet: checksum() == computedChecksum() on freshly built packets") {
        List<uint16_t> udw;
        for (uint8_t v = 0; v < 16; ++v) udw.pushToBack(v);
        St291Packet p = St291Packet::build(AncFormat(AncFormat::Cea708), udw, 11);
        CHECK(p.checksum() == p.computedChecksum());
        CHECK(p.checksumValid());
}

// ============================================================================
// UDW byte-alignment edge cases
// ============================================================================

TEST_CASE("St291Packet: UDW round-trip at every byte boundary modulo") {
        // 10-bit packing wraps at 4 words = 40 bits = 5 bytes.  Test DC
        // values that cross every byte boundary inside the wrap:
        // DC = 1..8 covers two full wraps + every offset within them.
        for (uint8_t dc = 1; dc <= 8; ++dc) {
                List<uint16_t> udw;
                for (uint8_t v = 0; v < dc; ++v) udw.pushToBack(static_cast<uint16_t>(0x10 + v));

                St291Packet p = St291Packet::build(AncFormat(AncFormat::Cea708), udw, 9);
                CHECK(p.dataCount() == dc);
                CHECK(p.checksumValid());

                List<uint16_t> got = p.udw();
                REQUIRE(got.size() == dc);
                for (size_t i = 0; i < dc; ++i) {
                        CHECK((got.at(i) & 0xFFu) == (udw.at(i) & 0xFFu));
                }
        }
}

// ============================================================================
// Mutators (CoW under the hood)
// ============================================================================

TEST_CASE("St291Packet: setUdw replaces payload and recomputes checksum") {
        List<uint16_t> udw1, udw2;
        udw1.pushToBack(uint16_t(0xAB));
        udw1.pushToBack(uint16_t(0xCD));
        udw2.pushToBack(uint16_t(0x11));
        udw2.pushToBack(uint16_t(0x22));
        udw2.pushToBack(uint16_t(0x33));

        St291Packet p = St291Packet::build(AncFormat(AncFormat::Cea708), udw1, 9);
        p.setUdw(udw2);
        CHECK(p.dataCount() == 3);
        List<uint16_t> got = p.udw();
        REQUIRE(got.size() == 3);
        CHECK((got.at(0) & 0xFFu) == 0x11);
        CHECK((got.at(1) & 0xFFu) == 0x22);
        CHECK((got.at(2) & 0xFFu) == 0x33);
        CHECK(p.checksumValid());
}

TEST_CASE("St291Packet: setLine/setFieldB/setStreamNum update meta") {
        List<uint16_t> udw;
        udw.pushToBack(uint16_t(0xAB));
        St291Packet p = St291Packet::build(AncFormat(AncFormat::Cea708), udw, 9);

        p.setLine(13);
        p.setHOffset(0x456);
        p.setFieldB(true);
        p.setCBit(true);
        p.setStreamNum(2);

        CHECK(p.line() == 13);
        CHECK(p.hOffset() == 0x456);
        CHECK(p.fieldB() == true);
        CHECK(p.cBit() == true);
        CHECK(p.streamNum() == 2);
}

// ============================================================================
// Type-1 bit
// ============================================================================

TEST_CASE("St291Packet: isType1 reflects high bit of DID") {
        List<uint16_t> udw;
        udw.pushToBack(uint16_t(0));
        // 0xE1 = 0b11100001 — high bit set.
        St291Packet p1 = St291Packet::buildRaw(0xE1, 0x01, udw, 9);
        CHECK(p1.isType1());

        // 0x41 = 0b01000001 — high bit clear.
        St291Packet p2 = St291Packet::buildRaw(0x41, 0x05, udw, 9);
        CHECK_FALSE(p2.isType1());
}

// ============================================================================
// Implicit decay to const AncPacket&
// ============================================================================

TEST_CASE("St291Packet: implicit decay accepted by AncPacket-taking functions") {
        List<uint16_t> udw;
        udw.pushToBack(uint16_t(0xAB));
        St291Packet p = St291Packet::build(AncFormat(AncFormat::Cea708), udw, 11);

        // Take by const-ref to AncPacket — exercises the implicit
        // operator const AncPacket&().
        auto inspect = [](const AncPacket &pkt) {
                return pkt.transport() == AncTransport::St291 && pkt.format().id() == AncFormat::Cea708;
        };
        CHECK(inspect(p));

        // Add to a List<AncPacket>.
        AncPacket::List list;
        list.pushToBack(p);
        CHECK(list.size() == 1);
        CHECK(list.at(0).transport() == AncTransport::St291);
}
