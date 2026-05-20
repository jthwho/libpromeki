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
        // ST 12-2:2014 §5 collapses all three ATC flavours onto
        // (DID=0x60, SDID=0x60); the DBB1 byte in UDWs 1..8 b3 is the
        // discriminator.  @c St291Packet::build preserves the caller's
        // explicit @c AncFormat::ID on the packet so a builder-chosen
        // AtcVitc1 / AtcVitc2 does not get re-stamped as AtcLtc by the
        // (DID,SDID)→ID lookup.
        Case cases[] = {
                {AncFormat::Cea708, 0x61, 0x01}, {AncFormat::Cea608, 0x61, 0x02}, {AncFormat::Afd, 0x41, 0x05},
                {AncFormat::PanScan, 0x41, 0x06}, {AncFormat::Scte104, 0x41, 0x07}, {AncFormat::AtcLtc, 0x60, 0x60},
                {AncFormat::AtcVitc1, 0x60, 0x60}, {AncFormat::AtcVitc2, 0x60, 0x60}, {AncFormat::Klv0601, 0x44, 0x04},
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
        // (0x4A, 0xBB) — Type-2 DID, not registered.  Use a Type-2
        // DID here so sdid() reads back as the second-word byte; the
        // Type-1 behaviour (sdid() reports 0, dbn() reports the
        // byte) is exercised by the F6 Type-1 tests below.
        St291Packet p = St291Packet::buildRaw(0x4A, 0xBB, udw, /*line*/ 9);
        CHECK(p.did() == 0x4A);
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
// Reserved-input + protected-code validation (ST 291-1 §6.1 / §6.2 / §6.5 / §9.1)
// ============================================================================

TEST_CASE("St291Packet: buildRaw rejects reserved DID 0x00 (ST 291-1 §6.1)") {
        List<uint16_t> udw;
        udw.pushToBack(uint16_t(0x42));
        St291Packet p = St291Packet::buildRaw(/*did*/ 0x00, /*sdid*/ 0x05, udw, /*line*/ 9);
        CHECK_FALSE(p.isValid());
}

TEST_CASE("St291Packet: P2-17 buildRaw rejects Type-2 reserved DIDs (ST 291-1 §6.1 Figure 4b)") {
        List<uint16_t> udw;
        udw.pushToBack(uint16_t(0x42));
        // ST 291-1 §6.1 Figure 4b reserves 0x01-0x03.
        for (uint8_t did : {uint8_t(0x01), uint8_t(0x02), uint8_t(0x03)}) {
                INFO("did=0x", String::number(static_cast<int>(did), 16));
                CHECK_FALSE(St291Packet::buildRaw(did, 0x05, udw, /*line*/ 9).isValid());
        }
        // 0x04-0x0F are "Reserved for 8-bit Application"; only 0x04,
        // 0x08, 0x0C are valid per §6.1.
        for (uint8_t did : {uint8_t(0x05), uint8_t(0x06), uint8_t(0x07),
                            uint8_t(0x09), uint8_t(0x0A), uint8_t(0x0B),
                            uint8_t(0x0D), uint8_t(0x0E), uint8_t(0x0F)}) {
                INFO("did=0x", String::number(static_cast<int>(did), 16));
                CHECK_FALSE(St291Packet::buildRaw(did, 0x05, udw, /*line*/ 9).isValid());
        }
        // 0x04, 0x08, 0x0C are valid 8-bit-app DIDs.
        for (uint8_t did : {uint8_t(0x04), uint8_t(0x08), uint8_t(0x0C)}) {
                INFO("did=0x", String::number(static_cast<int>(did), 16));
                CHECK(St291Packet::buildRaw(did, 0x05, udw, /*line*/ 9).isValid());
        }
        // 0x10+ is SMPTE-registered or User-Application space — valid.
        CHECK(St291Packet::buildRaw(0x10, 0x05, udw, /*line*/ 9).isValid());
        CHECK(St291Packet::buildRaw(0x41, 0x05, udw, /*line*/ 9).isValid());
}

TEST_CASE("St291Packet: P2-17 buildRaw rejects Type-2 SDID 0x00 (ST 291-1 §6.2)") {
        // ST 291-1 §6.2: SDID 0x00 is reserved on Type-2 packets.
        // (Type-1 packets store a DBN in word 1 and 0x00 means "DBN
        // inactive" per §6.4 — that path goes through buildRawType1.)
        List<uint16_t> udw;
        udw.pushToBack(uint16_t(0x42));
        St291Packet p = St291Packet::buildRaw(/*did*/ 0x41, /*sdid*/ 0x00, udw, /*line*/ 9);
        CHECK_FALSE(p.isValid());
}

TEST_CASE("St291Packet: P2-17 buildRawType1 rejects Type-1 reserved DIDs (ST 291-1 §6.1 Figure 4a)") {
        List<uint16_t> udw;
        udw.pushToBack(uint16_t(0x42));
        // ST 291-1 §6.1 Figure 4a reserves Type-1 DIDs 0x81-0x9F.
        for (uint8_t did : {uint8_t(0x81), uint8_t(0x83), uint8_t(0x84),
                            uint8_t(0x88), uint8_t(0x8C), uint8_t(0x9F)}) {
                INFO("did=0x", String::number(static_cast<int>(did), 16));
                CHECK_FALSE(St291Packet::buildRawType1(did, /*dbn*/ 0x01, udw, /*line*/ 9).isValid());
        }
        // 0x80 (Packet Marked for Deletion) is valid; 0xA0+ ranges are
        // SMPTE-registered / User Application.
        CHECK(St291Packet::buildRawType1(0x80, /*dbn*/ 0x01, udw, /*line*/ 9).isValid());
        CHECK(St291Packet::buildRawType1(0xA0, /*dbn*/ 0x01, udw, /*line*/ 9).isValid());
        // DBN=0x00 ("DBN inactive" per §6.4) is legal on Type-1 packets.
        CHECK(St291Packet::buildRawType1(0x80, /*dbn*/ 0x00, udw, /*line*/ 9).isValid());
}

TEST_CASE("St291Packet: build rejects oversize UDW list (ST 291-1 §6.5)") {
        // ST 291-1 §6.5 caps DataCount at 255 UDWs.  An oversize list
        // would silently truncate DC; refuse the build instead.
        List<uint16_t> udw;
        udw.resize(256, uint16_t(0x55));
        St291Packet p = St291Packet::build(AncFormat(AncFormat::Cea708), udw, /*line*/ 9);
        CHECK_FALSE(p.isValid());

        // The 255-word case is the maximum permitted and must still
        // succeed.
        List<uint16_t> udwMax;
        udwMax.resize(255, uint16_t(0x55));
        St291Packet pMax = St291Packet::build(AncFormat(AncFormat::Cea708), udwMax, /*line*/ 9);
        CHECK(pMax.isValid());
        CHECK(pMax.dataCount() == 255);
        CHECK(pMax.checksumValid());
}

TEST_CASE("St291Packet: build rejects protected-code UDW values (ST 291-1 §9.1)") {
        // Inputs with bits 8-9 both set (0x300-0x3FF) are protected
        // codes or parity-violation values; the build refuses to
        // emit them.  Try one value from each protected range that's
        // reachable via the pass-through path (the §9.1 protected
        // codes 0x3FC-0x3FF) plus a parity-violation in 0x300-0x3FB.
        for (uint16_t protectedValue : {uint16_t(0x3FC), uint16_t(0x3FD), uint16_t(0x3FE), uint16_t(0x3FF),
                                         uint16_t(0x300), uint16_t(0x3AB)}) {
                List<uint16_t> udw;
                udw.pushToBack(uint16_t(0x42));       // valid pass-through (parity = 0x100 | 0x42 = 0x142)
                udw.pushToBack(protectedValue);       // forbidden
                St291Packet p = St291Packet::build(AncFormat(AncFormat::Cea708), udw, /*line*/ 9);
                CHECK_MESSAGE(!p.isValid(), "protected code value not rejected: ",
                              (unsigned)protectedValue);
        }
}

TEST_CASE("St291Packet: build accepts valid pass-through encodings") {
        // Pass-through encodings have upper-2-bit pattern 01 or 10
        // (bit 9 = NOT bit 8) per ST 291-1 §6.1.  Spec-correct parity
        // for data byte 0x43 (odd-parity) is 0x143; for 0x55
        // (even-parity) it is 0x255.  Both must sail through the
        // validator.
        List<uint16_t> udw;
        udw.pushToBack(uint16_t(0x143));  // 0x43 with odd-parity encoding (01)
        udw.pushToBack(uint16_t(0x255));  // 0x55 with even-parity encoding (10)
        St291Packet p = St291Packet::build(AncFormat(AncFormat::Cea708), udw, /*line*/ 9);
        CHECK(p.isValid());
        CHECK(p.dataCount() == 2);
        CHECK(p.checksumValid());
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

        // Add to a AncPacket::List.
        AncPacket::List list;
        list.pushToBack(p);
        CHECK(list.size() == 1);
        CHECK(list.at(0).transport() == AncTransport::St291);
}

// ============================================================================
// Type-1 packet support (ST 291-1 §5.1 / §6.3 — DBN in word 1)
// ============================================================================

TEST_CASE("St291Packet: buildRawType1 builds a Type-1 packet (DID 0x80, DBN word 1)") {
        // Packet-Marked-for-Deletion (ST 291-1 §6.3): DID 0x80, Type-1,
        // word 1 carries a Data Block Number rather than an SDID.
        List<uint16_t> udw;
        udw.pushToBack(uint16_t(0xAA));
        udw.pushToBack(uint16_t(0xBB));
        const uint8_t dbn = 0x42;

        St291Packet p = St291Packet::buildRawType1(/*did*/ 0x80, dbn, udw, /*line*/ 9);
        REQUIRE(p.isValid());
        CHECK(p.isType1());
        CHECK(p.did() == 0x80);
        // Word 1 holds DBN, not SDID — sdid() must report the
        // RFC 8331 §3.1 sentinel value 0x00 so callers never
        // mis-label DBN as SDID.
        CHECK(p.sdid() == 0x00);
        CHECK(p.dbn() == dbn);
        CHECK(p.dataCount() == 2);
        CHECK(p.checksumValid());
        CHECK(p.packet().format().id() == AncFormat::PacketForDeletion);
}

TEST_CASE("St291Packet: buildRawType1 rejects Type-2 DIDs") {
        // DID 0x41 (Type-2 by §5.1) must not be accepted by the
        // Type-1 build helper — callers should use buildRaw().
        List<uint16_t> udw;
        udw.pushToBack(uint16_t(0));
        St291Packet p = St291Packet::buildRawType1(/*did*/ 0x41, /*dbn*/ 1, udw, 9);
        CHECK_FALSE(p.isValid());
}

TEST_CASE("St291Packet: dbn() reports 0 on Type-2 packets") {
        List<uint16_t> udw;
        udw.pushToBack(uint16_t(0x12));
        St291Packet p = St291Packet::build(AncFormat(AncFormat::Cea708), udw, 11);
        REQUIRE(p.isValid());
        CHECK_FALSE(p.isType1());
        CHECK(p.sdid() == 0x01);
        // Type-2 packets have no DBN — dbn() returns 0 by contract.
        CHECK(p.dbn() == 0);
}

TEST_CASE("St291Packet: PacketForDeletion is the canonical Type-1 round-trip") {
        // Build a Packet-Marked-for-Deletion via the registered
        // AncFormat enum and confirm it round-trips through from():
        // wire bytes preserved, DBN preserved, format identified.
        AncFormat fmt(AncFormat::PacketForDeletion);
        REQUIRE(fmt.isValid());
        CHECK(fmt.st291Did() == 0x80);
        // Wildcard SDID — the (DID, anyDBN) lookup falls back here.
        CHECK(fmt.st291Sdid() == 0x00);
        // SDP fmtp emission expands the wildcard into {0x00} per
        // RFC 8331 §3.1.
        REQUIRE(fmt.st291ConcreteSdids().size() == 1);
        CHECK(fmt.st291ConcreteSdids().at(0) == 0x00);

        List<uint16_t> udw;
        for (uint8_t v = 0x10; v < 0x18; ++v) udw.pushToBack(v);
        const uint8_t dbn = 0x55;
        St291Packet   p = St291Packet::buildRawType1(0x80, dbn, udw, /*line*/ 9);
        REQUIRE(p.isValid());
        CHECK(p.packet().format().id() == AncFormat::PacketForDeletion);

        // Re-promote from the underlying AncPacket — exercises
        // from()'s tightened DC-aware length check on a real Type-1
        // payload.
        Result<St291Packet> r = St291Packet::from(p.packet());
        REQUIRE(isOk(r));
        const St291Packet &rp = value(r);
        CHECK(rp.isType1());
        CHECK(rp.did() == 0x80);
        CHECK(rp.dbn() == dbn);
        CHECK(rp.sdid() == 0);
        CHECK(rp.dataCount() == 8);
        CHECK(rp.checksumValid());

        // Verify the lookup direction: a freshly-received (DID, DBN)
        // pair from the wire matches PacketForDeletion via the
        // wildcard fallback regardless of DBN value.
        AncFormat lookedUp = AncFormat::fromSt291DidSdid(0x80, dbn);
        CHECK(lookedUp.id() == AncFormat::PacketForDeletion);
        AncFormat lookedUp2 = AncFormat::fromSt291DidSdid(0x80, 0);
        CHECK(lookedUp2.id() == AncFormat::PacketForDeletion);
}

TEST_CASE("St291Packet: setUdw on a Type-1 packet preserves DBN") {
        // Mutating the UDW list must not silently zero the DBN — the
        // re-pack reads the existing second-word byte and writes it
        // back as DBN for Type-1 packets (or SDID for Type-2).
        List<uint16_t> udw1, udw2;
        udw1.pushToBack(uint16_t(0xAA));
        udw2.pushToBack(uint16_t(0x11));
        udw2.pushToBack(uint16_t(0x22));
        udw2.pushToBack(uint16_t(0x33));

        const uint8_t dbn = 0x7E;
        St291Packet   p = St291Packet::buildRawType1(0x80, dbn, udw1, 9);
        REQUIRE(p.isValid());
        CHECK(p.dbn() == dbn);

        p.setUdw(udw2);
        CHECK(p.dataCount() == 3);
        CHECK(p.dbn() == dbn);   // DBN preserved across re-pack
        CHECK(p.sdid() == 0);    // still Type-1 → sdid() reports 0
        CHECK(p.checksumValid());
}

TEST_CASE("St291Packet: udwRaw() preserves parity bits, udw() strips them") {
        // Build via the pass-through path with explicit parity-bit
        // encodings.  udw() drops the parity bits (the codec-path
        // dominant case); udwRaw() preserves them for byte-exact
        // replay verification.
        List<uint16_t> udw;
        udw.pushToBack(uint16_t(0x143));  // 0x43 + odd-parity encoding (upper 01)
        udw.pushToBack(uint16_t(0x255));  // 0x55 + even-parity encoding (upper 10)
        St291Packet p = St291Packet::build(AncFormat(AncFormat::Cea708), udw, /*line*/ 9);
        REQUIRE(p.isValid());

        List<uint16_t> stripped = p.udw();
        REQUIRE(stripped.size() == 2);
        CHECK(stripped.at(0) == 0x43);
        CHECK(stripped.at(1) == 0x55);
        // Upper-two-bit pattern is gone in udw() output.
        CHECK((stripped.at(0) & 0x300u) == 0u);
        CHECK((stripped.at(1) & 0x300u) == 0u);

        List<uint16_t> raw = p.udwRaw();
        REQUIRE(raw.size() == 2);
        CHECK(raw.at(0) == 0x143);
        CHECK(raw.at(1) == 0x255);
}

TEST_CASE("St291Packet: from() rejects buffers shorter than DC implies") {
        // Build a real CEA-708 packet, then truncate its underlying
        // wire buffer by one byte.  from() should refuse to promote
        // it because the declared DataCount no longer fits.
        List<uint16_t> udw;
        for (uint8_t v = 0x10; v < 0x20; ++v) udw.pushToBack(v);  // DC = 16
        St291Packet good = St291Packet::build(AncFormat(AncFormat::Cea708), udw, /*line*/ 11);
        REQUIRE(good.isValid());
        const Buffer &fullWire = good.packet().data();
        REQUIRE(fullWire.size() > 5);

        // Trim one byte off the wire — DataCount still says 16 but
        // the buffer now under-runs the (4+16)*10/8 = 25-byte
        // requirement.
        const size_t shortSize = fullWire.size() - 1;
        Buffer       shortBuf(shortSize);
        Error        cpyErr = shortBuf.copyFrom(fullWire.data(), shortSize, 0);
        REQUIRE(cpyErr.isOk());
        shortBuf.setSize(shortSize);
        AncPacket truncated(AncFormat(AncFormat::Cea708), AncTransport::St291, std::move(shortBuf));

        Result<St291Packet> r = St291Packet::from(truncated);
        CHECK(isError(r));
        CHECK(error(r) == Error::InvalidArgument);

        // Sanity: the full-size buffer still promotes.
        Result<St291Packet> rGood = St291Packet::from(good.packet());
        CHECK(isOk(rGood));
}

// ============================================================================
// Checksum policy (RFC 8331 §7 / ST 291-1 §6.4)
// ============================================================================

namespace {

        // Builds a valid CEA-708 packet, then returns a copy whose stored
        // §6.4 Checksum_Word has been corrupted by flipping the low bit of
        // the wire buffer's last byte.  The 4-header-word + N-UDW + 1-CS
        // packing places the checksum's low bits at the very end of the
        // byte stream, so flipping bit 0 of the last byte is guaranteed to
        // mutate the stored checksum without disturbing any UDW.
        AncPacket buildPacketWithCorruptedChecksum() {
                List<uint16_t> udw;
                for (uint8_t v = 0x10; v < 0x18; ++v) udw.pushToBack(v);
                St291Packet good = St291Packet::build(AncFormat(AncFormat::Cea708), udw, 11);
                REQUIRE(good.isValid());
                REQUIRE(good.checksumValid());

                const Buffer &wire = good.packet().data();
                REQUIRE(wire.size() >= 5);
                Buffer flipped(wire.size());
                Error  cpyErr = flipped.copyFrom(wire.data(), wire.size(), 0);
                REQUIRE(cpyErr.isOk());
                flipped.setSize(wire.size());
                uint8_t *bytes = static_cast<uint8_t *>(flipped.data());
                bytes[wire.size() - 1] ^= 0x01u;  // mutate the stored CS LSB

                return AncPacket(good.packet().format(), AncTransport::St291, std::move(flipped),
                                 Metadata(good.packet().meta()));
        }

} // namespace

TEST_CASE("St291Packet: from() defaults to PreserveOrRecompute (tolerant of bad checksum)") {
        // Default policy is tolerant: a captured packet whose checksum
        // drifted on the wire still promotes — downstream codecs are
        // responsible for graceful tolerance under the byte-exact
        // replay contract.
        AncPacket           bad = buildPacketWithCorruptedChecksum();
        Result<St291Packet> r = St291Packet::from(bad);
        REQUIRE(isOk(r));
        CHECK_FALSE(value(r).checksumValid());  // proves the wire was actually corrupted
}

TEST_CASE("St291Packet: from() with AlwaysRecompute is tolerant on parse") {
        // AlwaysRecompute governs emission, not ingest; on the parse
        // path it behaves identically to PreserveOrRecompute.
        AncPacket           bad = buildPacketWithCorruptedChecksum();
        Result<St291Packet> r = St291Packet::from(bad, AncChecksumPolicy::AlwaysRecompute);
        REQUIRE(isOk(r));
        CHECK_FALSE(value(r).checksumValid());
}

TEST_CASE("St291Packet: from() with StrictValidate rejects a mismatched checksum") {
        AncPacket           bad = buildPacketWithCorruptedChecksum();
        Result<St291Packet> r = St291Packet::from(bad, AncChecksumPolicy::StrictValidate);
        CHECK(isError(r));
        CHECK(error(r) == Error::InvalidChecksum);
}

TEST_CASE("St291Packet: from() with StrictValidate accepts a clean packet") {
        List<uint16_t> udw;
        for (uint8_t v = 0x10; v < 0x18; ++v) udw.pushToBack(v);
        St291Packet         good = St291Packet::build(AncFormat(AncFormat::Cea708), udw, 11);
        Result<St291Packet> r = St291Packet::from(good.packet(), AncChecksumPolicy::StrictValidate);
        REQUIRE(isOk(r));
        CHECK(value(r).checksumValid());
}
