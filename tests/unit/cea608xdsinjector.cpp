/**
 * @file      cea608xdsinjector.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdint>
#include <doctest/doctest.h>
#include <promeki/cea608.h>
#include <promeki/cea608xds.h>
#include <promeki/cea608xdsinjector.h>
#include <promeki/list.h>

using namespace promeki;

namespace {

        /// @brief Builds a Program Name packet for @p title.
        Cea608XdsPacket programNamePacket(const char *title) {
                Cea608XdsPacket p;
                p.class_ = Cea608XdsClass::Current;
                p.type = 0x03;
                for (size_t i = 0; i < std::char_traits<char>::length(title); ++i) {
                        p.payload.pushToBack(static_cast<uint8_t>(title[i]));
                }
                return p;
        }

        /// @brief Drives the injector for @p frames frames and feeds
        ///        each emitted pair into a fresh extractor.  Returns
        ///        the drained packet list.
        List<Cea608XdsPacket> runInjectorAndExtract(Cea608XdsInjector &inj, int frames) {
                Cea608XdsExtractor ext;
                for (int f = 0; f < frames; ++f) {
                        Cea708Cdp::CcData p = inj.nextPair();
                        if (!p.valid) continue;
                        ext.processPair(p.b1, p.b2);
                }
                return ext.drain();
        }

} // namespace

// ============================================================================
// Source registration
// ============================================================================

TEST_CASE("Cea608XdsInjector: setPacket adds a source") {
        Cea608XdsInjector inj;
        CHECK(inj.sourceCount() == 0);
        inj.setPacket(programNamePacket("HI"));
        CHECK(inj.sourceCount() == 1);
        // Re-setting the same (class, type) replaces — count stays at 1.
        inj.setPacket(programNamePacket("HELLO"));
        CHECK(inj.sourceCount() == 1);
}

TEST_CASE("Cea608XdsInjector: removePacket removes the source") {
        Cea608XdsInjector inj;
        inj.setPacket(programNamePacket("HI"));
        CHECK(inj.sourceCount() == 1);
        inj.removePacket(Cea608XdsClass::Current, 0x03);
        CHECK(inj.sourceCount() == 0);
}

TEST_CASE("Cea608XdsInjector: reset clears all sources + scheduler state") {
        Cea608XdsInjector inj;
        inj.setPacket(programNamePacket("HI"));
        inj.setPacket(programNamePacket("BYE")); // same (class, type) — replaces
        CHECK(inj.sourceCount() == 1);
        inj.reset();
        CHECK(inj.sourceCount() == 0);
}

// ============================================================================
// Emission + repetition
// ============================================================================

TEST_CASE("Cea608XdsInjector: a single registered packet emits its byte pairs") {
        Cea608XdsInjector inj;
        inj.setPacket(programNamePacket("AB"));
        // Program Name "AB" encodes as: Start(0x01), Type(0x03), 'A','B',
        // End(0x0F), Checksum.  Four byte pairs total.
        // Drive 4 frames and feed them to a fresh extractor.
        const auto packets = runInjectorAndExtract(inj, 4);
        REQUIRE(packets.size() == 1);
        CHECK(packets[0].class_ == Cea608XdsClass::Current);
        CHECK(packets[0].type == 0x03);
        CHECK(packets[0].programName() == "AB");
}

TEST_CASE("Cea608XdsInjector: idle frames after a one-shot emit return invalid pairs") {
        Cea608XdsInjector inj;
        // repetition <= 0 → emit once, then go idle.
        inj.setPacket(programNamePacket("AB"), 0);
        // Drive past the packet's 4-pair length.  After completion, the
        // scheduler should report idle (valid == false).
        bool sawValid = false, sawIdle = false;
        for (int f = 0; f < 10; ++f) {
                Cea708Cdp::CcData p = inj.nextPair();
                if (p.valid) sawValid = true;
                else if (sawValid) sawIdle = true; // saw idle AFTER the emission
        }
        CHECK(sawValid);
        CHECK(sawIdle);
}

TEST_CASE("Cea608XdsInjector: repetition period repeats the packet on schedule") {
        Cea608XdsInjector inj;
        // Repeat every 5 frames.
        inj.setPacket(programNamePacket("AB"), 5);
        // Drive 30 frames; each emission is 4 pairs long.  With a 5-frame
        // repetition gap, we expect emissions starting at frames 0..3,
        // idle 4..8, 9..12, idle 13..17, 18..21, ...
        // At least 2 complete packets should round-trip.
        Cea608XdsExtractor ext;
        int validPairs = 0;
        for (int f = 0; f < 30; ++f) {
                Cea708Cdp::CcData p = inj.nextPair();
                if (p.valid) {
                        ++validPairs;
                        ext.processPair(p.b1, p.b2);
                }
        }
        const auto pkts = ext.drain();
        CHECK(pkts.size() >= 2);
        for (size_t i = 0; i < pkts.size(); ++i) {
                CHECK(pkts[i].programName() == "AB");
        }
}

TEST_CASE("Cea608XdsInjector: multiple sources interleave by overdue-ness") {
        Cea608XdsInjector inj;
        inj.setPacket(programNamePacket("AA"), 10);
        Cea608XdsPacket networkName;
        networkName.class_ = Cea608XdsClass::Channel;
        networkName.type = 0x01;
        networkName.payload.pushToBack('B');
        networkName.payload.pushToBack('B');
        inj.setPacket(networkName, 10);

        // Drive enough frames for both to emit at least once.
        Cea608XdsExtractor ext;
        for (int f = 0; f < 40; ++f) {
                Cea708Cdp::CcData p = inj.nextPair();
                if (p.valid) ext.processPair(p.b1, p.b2);
        }
        const auto pkts = ext.drain();
        // We expect at least one of each (and possibly more).
        bool sawProgName = false;
        bool sawNetName = false;
        for (size_t i = 0; i < pkts.size(); ++i) {
                if (pkts[i].class_ == Cea608XdsClass::Current && pkts[i].type == 0x03) {
                        sawProgName = true;
                        CHECK(pkts[i].programName() == "AA");
                }
                if (pkts[i].class_ == Cea608XdsClass::Channel && pkts[i].type == 0x01) {
                        sawNetName = true;
                        CHECK(pkts[i].networkName() == "BB");
                }
        }
        CHECK(sawProgName);
        CHECK(sawNetName);
        CHECK(ext.checksumFailures() == 0);
}

TEST_CASE("Cea608XdsInjector::nextPairWithParity stamps odd parity") {
        Cea608XdsInjector inj;
        inj.setPacket(programNamePacket("AB"));
        bool sawValid = false;
        for (int f = 0; f < 4; ++f) {
                Cea708Cdp::CcData p = inj.nextPairWithParity();
                if (!p.valid) continue;
                sawValid = true;
                CHECK(Cea608::checkOddParity(p.b1));
                CHECK(Cea608::checkOddParity(p.b2));
                CHECK(p.type == 1); // field 2
        }
        CHECK(sawValid);
}

TEST_CASE("Cea608XdsInjector: §E.10 priority — Time of Day beats Network Name") {
        // Two sources, both registered at the same instant, identical
        // repetition periods.  Time of Day (Misc / 0x01) lives at the
        // top of the §E.10 priority tree; Network Name (Channel /
        // 0x01) sits much lower.  When both are simultaneously
        // eligible (which happens on every frame after the
        // repetition window elapses), the priority arbitration
        // chooses Time of Day on each fresh dispatch.
        Cea608XdsInjector inj;

        // Build a Time of Day packet (Misc / 0x01) with arbitrary
        // payload — wire content doesn't matter for the arbitration
        // test, only the (class, type).
        Cea608XdsPacket tod;
        tod.class_ = Cea608XdsClass::Misc;
        tod.type = 0x01;
        tod.payload.pushToBack(0x00);
        tod.payload.pushToBack(0x00);
        tod.payload.pushToBack(0x00);
        tod.payload.pushToBack(0x00);
        tod.payload.pushToBack(0x00);
        tod.payload.pushToBack(0x00);

        Cea608XdsPacket netName;
        netName.class_ = Cea608XdsClass::Channel;
        netName.type = 0x01;
        netName.payload.pushToBack('A');
        netName.payload.pushToBack('B');

        // Both registered at the same time, same repetition.  ToD
        // (higher priority) should fire first; net-name only fills
        // the slots where ToD isn't eligible.
        inj.setPacket(tod, 10);
        inj.setPacket(netName, 10);

        // Drive ~80 frames and tally which class emits first.
        Cea708Cdp::CcData first;
        first.valid = false;
        for (int f = 0; f < 80; ++f) {
                Cea708Cdp::CcData p = inj.nextPair();
                if (p.valid && !first.valid) first = p;
        }
        REQUIRE(first.valid);
        // First emission's b1 should be the Misc Start byte (0x07)
        // per §9.5.  Channel Start is 0x05.  Priority steered the
        // injector to emit ToD before Net Name.
        const uint8_t firstStart = first.b1;
        CHECK(firstStart == 0x07);
}

TEST_CASE("Cea608XdsInjector: §E.10 priority — Content Advisory beats Program Name") {
        // Within the Current Class, Content Advisory (type 0x05)
        // outranks Program Name (type 0x03).  Same trick as above:
        // register both at the same time with the same repetition
        // window; observe that Content Advisory emits first.
        Cea608XdsInjector inj;

        Cea608XdsPacket adv;
        adv.class_ = Cea608XdsClass::Current;
        adv.type = 0x05;
        adv.payload.pushToBack(0x40); // arbitrary 2-byte payload
        adv.payload.pushToBack(0x40);

        inj.setPacket(adv, 10);
        inj.setPacket(programNamePacket("HI"), 10);

        // Both Current-Class so the first-emission delta is the
        // packet's Type byte (second wire byte after the Start byte).
        Cea708Cdp::CcData first;
        first.valid = false;
        for (int f = 0; f < 80; ++f) {
                Cea708Cdp::CcData p = inj.nextPair();
                if (p.valid && !first.valid) first = p;
        }
        REQUIRE(first.valid);
        // First emission carries Current Start (0x01) as b1; the
        // first byte pair's b2 is the packet's Type byte — should
        // be 0x05 (Content Advisory), not 0x03 (Program Name).
        CHECK(first.b1 == 0x01);
        CHECK(first.b2 == 0x05);
}

TEST_CASE("Cea608XdsInjector: removing an in-flight source aborts the emission cleanly") {
        Cea608XdsInjector inj;
        inj.setPacket(programNamePacket("ABCD"));
        // Pull one pair so the source becomes in-flight.
        Cea708Cdp::CcData first = inj.nextPair();
        REQUIRE(first.valid);
        // Remove the source mid-emission.
        inj.removePacket(Cea608XdsClass::Current, 0x03);
        CHECK(inj.sourceCount() == 0);
        // Next pair should be idle.
        Cea708Cdp::CcData second = inj.nextPair();
        CHECK_FALSE(second.valid);
}

// ============================================================================
// X7 — nextPair(hasCaptionPair) yields F2 to captions per §E.10
// ============================================================================

TEST_CASE("Cea608XdsInjector: nextPair(hasCaptionPair=true) holds the in-flight cursor") {
        // Start an emission, claim a few frames for captioning, then
        // let the injector resume — it must not lose any bytes.
        Cea608XdsInjector inj;
        inj.setPacket(programNamePacket("AB"));
        // Pull frame 0 — fetches Start/Type pair.
        Cea708Cdp::CcData f0 = inj.nextPair(false);
        REQUIRE(f0.valid);
        CHECK(f0.b1 == 0x01); // Current Start
        CHECK(f0.b2 == 0x03); // Type Name
        // Frames 1..3 — captions claim F2.  Injector returns invalid
        // pairs and keeps the cursor at the next informational byte.
        for (int f = 0; f < 3; ++f) {
                Cea708Cdp::CcData p = inj.nextPair(true);
                CHECK_FALSE(p.valid);
        }
        // Frame 4 — captions release F2; injector emits 'A','B'.
        Cea708Cdp::CcData f4 = inj.nextPair(false);
        REQUIRE(f4.valid);
        CHECK(f4.b1 == 'A');
        CHECK(f4.b2 == 'B');
}

// ============================================================================
// X8 — setPacket on an in-flight source aborts the in-flight emission
// ============================================================================

TEST_CASE("Cea608XdsInjector: setPacket replace mid-emission aborts in-flight bytes") {
        Cea608XdsInjector inj;
        inj.setPacket(programNamePacket("ABCD"));
        // Pull one pair so the source becomes in-flight.
        Cea708Cdp::CcData first = inj.nextPair();
        REQUIRE(first.valid);
        CHECK(first.b1 == 0x01); // Start Current
        // Replace the payload mid-emission with a different title.
        inj.setPacket(programNamePacket("EFGH"));
        CHECK(inj.sourceCount() == 1);
        // Next emission should start over with a fresh Start/Type
        // pair from the NEW payload — never mix the stale "ABCD"
        // tail with the new "EFGH" bytes.
        Cea608XdsExtractor ext;
        for (int f = 0; f < 8; ++f) {
                Cea708Cdp::CcData p = inj.nextPair();
                if (p.valid) ext.processPair(p.b1, p.b2);
        }
        const auto pkts = ext.drain();
        REQUIRE(pkts.size() >= 1);
        CHECK(pkts[0].programName() == "EFGH");
        CHECK(ext.checksumFailures() == 0);
}

// ============================================================================
// X13 — Injector priority: NWS WRSAME beats CGMS-A per §9.6.2.13
// ============================================================================

TEST_CASE("Cea608XdsInjector: §9.6.2.13 priority — NWS WRSAME beats CGMS-A") {
        Cea608XdsInjector inj;

        // Build a CGMS-A packet (Current / 0x08).
        Cea608XdsPacket cgms;
        cgms.class_ = Cea608XdsClass::Current;
        cgms.type = 0x08;
        cgms.payload.pushToBack(0x40);
        cgms.payload.pushToBack(0x40);

        // Build a WRSAME packet (PublicSvc / 0x01).  The injector
        // doesn't validate payload semantics — it just emits bytes.
        Cea608XdsPacket wrsame;
        wrsame.class_ = Cea608XdsClass::PublicSvc;
        wrsame.type = 0x01;
        const uint8_t wrsameBytes[16] = {
                'T', 'O', 'R', '-',
                '1', '0', '6', '0',
                '3', '7', '-', 0x00,
                '+', '0', '4', '-',
        };
        for (uint8_t b : wrsameBytes) wrsame.payload.pushToBack(b);

        // Both registered simultaneously, same repetition window —
        // WRSAME (emergency) must outrank CGMS-A on first dispatch.
        inj.setPacket(cgms, 10);
        inj.setPacket(wrsame, 10);

        Cea708Cdp::CcData first;
        first.valid = false;
        for (int f = 0; f < 80; ++f) {
                Cea708Cdp::CcData p = inj.nextPair();
                if (p.valid && !first.valid) first = p;
        }
        REQUIRE(first.valid);
        // PublicSvc Start byte is 0x09; CGMS-A would emit 0x01.
        CHECK(first.b1 == 0x09);
        CHECK(first.b2 == 0x01); // Type WRSAME
}
