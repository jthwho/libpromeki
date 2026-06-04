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
 * on DID 0x61 / SDID 0x02 and verify the Details path decodes them.
 *
 * Per ST 334-1 Annex B the CEA-608 ANC packet is fixed length with
 * DataCount = 3: a LINE byte (field + insertion line) followed by the
 * field's two line-21 caption bytes.
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

        // LINE byte for NTSC field 1, VBI line 21 (the standard caption
        // line): bit 7 = field 1, bits 4..0 = line offset 12 from the
        // field-1 base line 9 (9 + 12 = 21).  See ST 334-1 Annex B.
        constexpr uint8_t LineField1Line21 = 0x8C;

        // Builds a raw 334-1 packet (DID 0x61 / SDID 0x02) per Annex B: the
        // LINE byte followed by the two caption bytes, each stamped with odd
        // parity exactly as a real CEA-608 source would.  The LINE byte
        // carries no CEA-608 odd parity (it is a raw field/line value).
        AncPacket buildCea608(uint8_t lineByte, uint8_t capB1, uint8_t capB2) {
                List<uint16_t> udw;
                udw.pushToBack(lineByte);
                udw.pushToBack(Cea608::withOddParity(capB1));
                udw.pushToBack(Cea608::withOddParity(capB2));
                St291Packet p = St291Packet::build(AncFormat(AncFormat::Cea608), udw, 21);
                return p.packet();
        }

} // namespace

TEST_CASE("CEA-608 details: registered detailer reports DID 0x61 / SDID 0x02") {
        CHECK(AncTranslator::hasDetailer(AncFormat(AncFormat::Cea608), AncTransport::St291));

        AncTranslator t;
        // EOC control pair (0x14, 0x2F) on field 1, line 21.
        AncPacket  pkt = buildCea608(LineField1Line21, 0x14, 0x2F);
        AncDetails d   = t.details(pkt);

        CHECK(d.lines().contains(String("DID = 0x61")));
        CHECK(d.lines().contains(String("SDID = 0x02")));
        CHECK_FALSE(d.hasErrors());
        CHECK_FALSE(d.hasWarnings());
}

TEST_CASE("CEA-608 details: decodes the LINE byte field and insertion line") {
        AncTranslator t;
        AncDetails    d = t.details(buildCea608(LineField1Line21, 0x14, 0x2F));

        bool sawField = false, sawLine = false;
        for (const String &line : d.lines()) {
                if (line.contains("Field = 1")) sawField = true;
                if (line.contains("InsertLine = 21")) sawLine = true;
        }
        CHECK(sawField);
        CHECK(sawLine);
        CHECK_FALSE(d.hasErrors());

        // Field 2 carriage (line 272 base): offset 0 -> line 272.
        AncDetails d2      = t.details(buildCea608(0x00, 0x15, 0x2F));
        bool       sawF2   = false, sawL2 = false;
        for (const String &line : d2.lines()) {
                if (line.contains("Field = 2")) sawF2 = true;
                if (line.contains("InsertLine = 272")) sawL2 = true;
        }
        CHECK(sawF2);
        CHECK(sawL2);
}

TEST_CASE("CEA-608 details: decodes a misc control code by name with CC channel") {
        AncTranslator t;
        AncDetails    d = t.details(buildCea608(LineField1Line21, 0x14, 0x2F));  // EOC, CC1

        bool sawEoc = false;
        for (const String &line : d.lines()) {
                if (line.contains("CC1: EOC (End Of Caption)")) sawEoc = true;
        }
        CHECK(sawEoc);
        CHECK_FALSE(d.hasErrors());

        // Bit 3 of the control byte selects the second channel of the field.
        AncDetails d2     = t.details(buildCea608(LineField1Line21, 0x1C, 0x2F));  // CC2
        bool       sawCc2 = false;
        for (const String &line : d2.lines()) {
                if (line.contains("CC2: EOC")) sawCc2 = true;
        }
        CHECK(sawCc2);
}

TEST_CASE("CEA-608 details: renders printable G0 characters as text") {
        AncTranslator t;
        AncDetails    d = t.details(buildCea608(LineField1Line21, 'h', 'i'));

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
        // The PAC bytes already carry odd parity from encodePac, so feed the
        // stripped values through the parity-stamping builder.
        AncDetails d = t.details(
                buildCea608(LineField1Line21, Cea608::stripParity(b1), Cea608::stripParity(b2)));

        bool sawPac = false;
        for (const String &line : d.lines()) {
                if (line.contains("PAC (row 15")) sawPac = true;
        }
        CHECK(sawPac);
        CHECK_FALSE(d.hasErrors());
}

TEST_CASE("CEA-608 details: null padding pair is reported without a parity warning") {
        AncTranslator t;
        // Real-world zero-fill: caption bytes are literally 0x00 0x00, which
        // fail odd parity but are padding, not data.
        List<uint16_t> udw;
        udw.pushToBack(LineField1Line21);
        udw.pushToBack(0x00);
        udw.pushToBack(0x00);
        St291Packet p = St291Packet::build(AncFormat(AncFormat::Cea608), udw, 21);

        AncDetails d       = t.details(p.packet());
        bool       sawNull = false;
        for (const String &line : d.lines()) {
                if (line.contains("Null (padding)")) sawNull = true;
        }
        CHECK(sawNull);
        CHECK_FALSE(d.hasWarnings());
}

TEST_CASE("CEA-608 details: flags a parity error but still decodes best-effort") {
        // Hand-build the caption bytes WITHOUT parity stamping: 0x14 has even
        // parity, so the pair fails the odd-parity check.
        List<uint16_t> udw;
        udw.pushToBack(LineField1Line21);
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

TEST_CASE("CEA-608 details: warns on a truncated (non-Annex-B) packet") {
        // Only the LINE byte, no caption bytes: DataCount 1, not 3.
        List<uint16_t> udw;
        udw.pushToBack(LineField1Line21);
        St291Packet p = St291Packet::build(AncFormat(AncFormat::Cea608), udw, 21);

        AncTranslator t;
        AncDetails    d = t.details(p.packet());
        CHECK(d.hasWarnings());
        // Field/line still decode from the LINE byte.
        bool sawField = false;
        for (const String &line : d.lines()) {
                if (line.contains("Field = 1")) sawField = true;
        }
        CHECK(sawField);
}
