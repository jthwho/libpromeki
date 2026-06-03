/**
 * @file      anccodec_cea608.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Exercises the SMPTE 334-1 raw line-21 CEA-608 detailer.  ST 334-1 has
 * no registered parser / builder (modern captions ride in the CDP via
 * @c AncFormat::Cea708), so these tests hand-build raw ST 291 packets
 * on DID 0x61 / SDID 0x02 and verify the Details path decodes the
 * line-21 byte pairs.
 */

#include <doctest/doctest.h>
#include <promeki/ancdetails.h>
#include <promeki/ancformat.h>
#include <promeki/ancpacket.h>
#include <promeki/anctranslator.h>
#include <promeki/cea608.h>
#include <promeki/list.h>
#include <promeki/result.h>
#include <promeki/st291packet.h>

using namespace promeki;

namespace {

        // Builds a raw 334-1 packet (DID 0x61 / SDID 0x02) carrying the
        // supplied line-21 bytes as user-data words, each stamped with odd
        // parity exactly as a real CEA-608 source would.
        AncPacket buildCea608(std::initializer_list<uint8_t> preParityBytes) {
                List<uint16_t> udw;
                for (uint8_t b : preParityBytes) {
                        udw.pushToBack(Cea608::withOddParity(b));
                }
                St291Packet p = St291Packet::build(AncFormat(AncFormat::Cea608), udw, 21);
                return p.packet();
        }

} // namespace

TEST_CASE("CEA-608 details: registered detailer reports DID 0x61 / SDID 0x02") {
        CHECK(AncTranslator::hasDetailer(AncFormat(AncFormat::Cea608), AncTransport::St291));

        AncTranslator t;
        // EOC control pair (0x14, 0x2F) followed by printable "hi".
        AncPacket  pkt = buildCea608({0x14, 0x2F, 'h', 'i'});
        AncDetails d   = t.details(pkt);

        CHECK(d.lines().contains(String("DID = 0x61")));
        CHECK(d.lines().contains(String("SDID = 0x02")));
        CHECK(d.lines().contains(String("Line = 21")));
        CHECK_FALSE(d.hasErrors());
        CHECK_FALSE(d.hasWarnings());
}

TEST_CASE("CEA-608 details: decodes a misc control code by name") {
        AncTranslator t;
        AncPacket     pkt = buildCea608({0x14, 0x2F});  // EOC
        AncDetails    d   = t.details(pkt);

        bool sawEoc = false;
        for (const String &line : d.lines()) {
                if (line.contains("EOC (End Of Caption)")) sawEoc = true;
        }
        CHECK(sawEoc);
        CHECK_FALSE(d.hasErrors());
}

TEST_CASE("CEA-608 details: renders printable G0 characters as text") {
        AncTranslator t;
        AncPacket     pkt = buildCea608({'h', 'i'});
        AncDetails    d   = t.details(pkt);

        bool sawText = false;
        for (const String &line : d.lines()) {
                if (line.contains("Text \"hi\"")) sawText = true;
        }
        CHECK(sawText);
}

TEST_CASE("CEA-608 details: decodes a PAC with its row") {
        // Build a PAC for row 15 (the spec's row-15 / col-0 white PAC).
        Cea608::PacAttr attr;
        attr.row       = 15;
        attr.indentCol = 0;
        uint8_t b1 = 0, b2 = 0;
        Cea608::encodePac(attr, b1, b2);

        AncTranslator t;
        AncPacket     pkt = buildCea608({b1, b2});
        AncDetails    d   = t.details(pkt);

        bool sawPac = false;
        for (const String &line : d.lines()) {
                if (line.contains("PAC (row 15")) sawPac = true;
        }
        CHECK(sawPac);
        CHECK_FALSE(d.hasErrors());
}

TEST_CASE("CEA-608 details: flags a parity error but still decodes best-effort") {
        // Hand-build the bytes WITHOUT parity stamping: 0x14 has even
        // parity, so the pair fails the odd-parity check.
        List<uint16_t> udw;
        udw.pushToBack(0x14);
        udw.pushToBack(0x2F);
        St291Packet p = St291Packet::build(AncFormat(AncFormat::Cea608), udw, 21);

        AncTranslator t;
        AncDetails    d = t.details(p.packet());
        CHECK(d.hasWarnings());
        // The decode still surfaces the control code despite the bad parity.
        bool sawEoc = false;
        for (const String &line : d.lines()) {
                if (line.contains("EOC")) sawEoc = true;
        }
        CHECK(sawEoc);
}
