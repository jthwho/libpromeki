/**
 * @file      anccodec_cea608.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Exercises the SMPTE 334-1 raw line-21 CEA-608 codec: the detailer
 * (Details path), plus the parser / builder / frame-sync policy that
 * round-trip the raw ST 291 framing through the typed @ref Cea608Packet
 * value.  Tests hand-build raw ST 291 packets on DID 0x61 / SDID 0x02.
 *
 * Per ST 334-1 Annex B the CEA-608 ANC packet is fixed length with
 * DataCount = 3: a LINE byte (field + insertion line) followed by the
 * field's two line-21 caption bytes.
 */

#include <doctest/doctest.h>
#include <promeki/ancdetails.h>
#include <promeki/ancformat.h>
#include <promeki/ancpacket.h>
#include <promeki/anctranslateconfig.h>
#include <promeki/anctranslator.h>
#include <promeki/cea608.h>
#include <promeki/cea608packet.h>
#include <promeki/framesyncdisposition.h>
#include <promeki/list.h>
#include <promeki/result.h>
#include <promeki/st291packet.h>
#include <promeki/variant.h>

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

// ============================================================================
// Parser / builder / sync policy — typed Cea608Packet round-trip.
// ============================================================================

namespace {

        // Builds a one-triple Cea608Packet for the given channel and a
        // parity-stamped caption pair (stripped values passed in).
        Cea608Packet makePacket(Cea608Packet::Channel ch, uint8_t b1Stripped, uint8_t b2Stripped) {
                Cea708Cdp::CcData cc;
                cc.valid = true;
                cc.type  = (ch == Cea608Packet::Channel::CC1 || ch == Cea608Packet::Channel::CC2) ? 0 : 1;
                cc.b1    = Cea608::withOddParity(b1Stripped);
                cc.b2    = Cea608::withOddParity(b2Stripped);
                Cea708Cdp::CcDataList l;
                l.pushToBack(cc);
                return Cea608Packet(ch, std::move(l));
        }

} // namespace

TEST_CASE("CEA-608 codec: parser / builder / sync policy are registered") {
        CHECK(AncTranslator::hasParser(AncFormat(AncFormat::Cea608), AncTransport::St291));
        CHECK(AncTranslator::hasBuilder(AncFormat(AncFormat::Cea608), AncTransport::St291));
        CHECK(AncTranslator::hasSyncPolicy(AncFormat(AncFormat::Cea608)));
}

TEST_CASE("CEA-608 builder: emits an Annex-B raw line-21 packet (field 1)") {
        AncTranslator t;
        // EOC control pair on CC1 (field 1).
        Cea608Packet                 in    = makePacket(Cea608Packet::Channel::CC1, 0x14, 0x2F);
        AncTranslator::PacketsResult built = t.build(Variant(in), AncFormat(AncFormat::Cea608),
                                                     AncTransport::St291);
        REQUIRE(built.second().isOk());
        REQUIRE(built.first().size() == 1);
        CHECK(built.first().front().format().id() == AncFormat::Cea608);
        CHECK(built.first().front().transport() == AncTransport::St291);

        Result<St291Packet> rp = St291Packet::from(built.first().front());
        REQUIRE(rp.second().isOk());
        CHECK(rp.first().did() == 0x61);
        CHECK(rp.first().sdid() == 0x02);
        CHECK(rp.first().dataCount() == 3);
        CHECK(rp.first().checksumValid());

        List<uint16_t> udw = rp.first().udw();
        REQUIRE(udw.size() == 3);
        // Field-1 line-21 LINE byte = 0x8C, then the verbatim caption pair.
        CHECK((udw[0] & 0xFF) == LineField1Line21);
        CHECK((udw[1] & 0xFF) == Cea608::withOddParity(0x14));
        CHECK((udw[2] & 0xFF) == Cea608::withOddParity(0x2F));
}

TEST_CASE("CEA-608 builder: field-2 channel emits the field-2 LINE byte") {
        AncTranslator t;
        // CC3 lives in field 2 -> LINE byte 0x0C (line 284 = base 272 + 12).
        Cea608Packet                 in    = makePacket(Cea608Packet::Channel::CC3, 0x1C, 0x2F);
        AncTranslator::PacketsResult built = t.build(Variant(in), AncFormat(AncFormat::Cea608),
                                                     AncTransport::St291);
        REQUIRE(built.second().isOk());
        Result<St291Packet> rp = St291Packet::from(built.first().front());
        REQUIRE(rp.second().isOk());
        List<uint16_t> udw = rp.first().udw();
        REQUIRE(udw.size() == 3);
        CHECK((udw[0] & 0xFF) == 0x0C);
}

TEST_CASE("CEA-608 builder: honours the St291BuildLine config key") {
        AncTranslateConfig cfg;
        cfg.set(AncTranslateConfig::St291BuildLine, static_cast<uint16_t>(13));
        AncTranslator                t(cfg);
        Cea608Packet                 in    = makePacket(Cea608Packet::Channel::CC1, 0x14, 0x2F);
        AncTranslator::PacketsResult built = t.build(Variant(in), AncFormat(AncFormat::Cea608),
                                                     AncTransport::St291);
        REQUIRE(built.second().isOk());
        Result<St291Packet> rp = St291Packet::from(built.first().front());
        REQUIRE(rp.second().isOk());
        CHECK(rp.first().line() == 13);
}

TEST_CASE("CEA-608 builder: rejects a DTVCC (cc_type 2/3) triple") {
        AncTranslator     t;
        Cea708Cdp::CcData cc;
        cc.valid = true;
        cc.type  = 2;  // DTVCC channel-packet data — not raw line-21.
        cc.b1    = 0x41;
        cc.b2    = 0x42;
        Cea708Cdp::CcDataList l;
        l.pushToBack(cc);
        Cea608Packet                 in    = Cea608Packet(Cea608Packet::Channel::CC1, std::move(l));
        AncTranslator::PacketsResult built = t.build(Variant(in), AncFormat(AncFormat::Cea608),
                                                     AncTransport::St291);
        CHECK(built.second().isError());
        CHECK(built.second().code() == Error::NotSupported);
}

TEST_CASE("CEA-608 builder: rejects an empty packet") {
        AncTranslator                t;
        Cea608Packet                 in;  // no cc_data triples
        AncTranslator::PacketsResult built = t.build(Variant(in), AncFormat(AncFormat::Cea608),
                                                     AncTransport::St291);
        CHECK(built.second().isError());
}

TEST_CASE("CEA-608 parser: decodes field + channel + verbatim caption bytes") {
        AncTranslator t;
        // EOC on CC1 (field 1).
        AncPacket                  pkt    = buildCea608(LineField1Line21, 0x14, 0x2F);
        AncTranslator::ParseResult parsed = t.parse(pkt);
        REQUIRE(parsed.second().isOk());
        Cea608Packet p = parsed.first().get<Cea608Packet>();
        CHECK(p.channel == Cea608Packet::Channel::CC1);
        REQUIRE(p.ccData.size() == 1);
        CHECK(p.ccData[0].valid);
        CHECK(p.ccData[0].type == 0);  // field 1
        CHECK(p.ccData[0].b1 == Cea608::withOddParity(0x14));
        CHECK(p.ccData[0].b2 == Cea608::withOddParity(0x2F));
}

TEST_CASE("CEA-608 parser: bit 3 of a control byte selects the second channel") {
        AncTranslator t;
        // CC2 (field 1, second channel): control byte 0x1C has bit 3 set.
        Cea608Packet p2 = t.parse(buildCea608(LineField1Line21, 0x1C, 0x2F)).first().get<Cea608Packet>();
        CHECK(p2.channel == Cea608Packet::Channel::CC2);

        // Field 2 (LINE byte 0x00) primary channel -> CC3.
        Cea608Packet p3 = t.parse(buildCea608(0x00, 0x15, 0x2F)).first().get<Cea608Packet>();
        CHECK(p3.channel == Cea608Packet::Channel::CC3);
        CHECK(p3.ccData[0].type == 1);  // field 2
}

TEST_CASE("CEA-608 parser: a text pair defaults to the field primary channel") {
        AncTranslator t;
        // 'h','i' are G0 text, not a control pair; channel falls back to CC1.
        Cea608Packet p = t.parse(buildCea608(LineField1Line21, 'h', 'i')).first().get<Cea608Packet>();
        CHECK(p.channel == Cea608Packet::Channel::CC1);
        CHECK(p.ccData[0].b1 == Cea608::withOddParity('h'));
        CHECK(p.ccData[0].b2 == Cea608::withOddParity('i'));
}

TEST_CASE("CEA-608 parser: truncated (< 3 word) packet is rejected") {
        AncTranslator  t;
        List<uint16_t> udw;
        udw.pushToBack(LineField1Line21);
        St291Packet p = St291Packet::build(AncFormat(AncFormat::Cea608), udw, 21);
        CHECK(t.parse(p.packet()).second().isError());
}

TEST_CASE("CEA-608 codec: build -> parse round-trips a control pair") {
        AncTranslator t;
        for (Cea608Packet::Channel ch : {Cea608Packet::Channel::CC1, Cea608Packet::Channel::CC2,
                                         Cea608Packet::Channel::CC3, Cea608Packet::Channel::CC4}) {
                // EOC: control byte 0x14 (CC1/CC3) or 0x1C (CC2/CC4) per channel bit.
                bool         second = (ch == Cea608Packet::Channel::CC2 || ch == Cea608Packet::Channel::CC4);
                Cea608Packet in     = makePacket(ch, second ? 0x1C : 0x14, 0x2F);
                AncTranslator::PacketsResult built = t.build(Variant(in), AncFormat(AncFormat::Cea608),
                                                             AncTransport::St291);
                REQUIRE(built.second().isOk());
                AncTranslator::ParseResult parsed = t.parse(built.first().front());
                REQUIRE(parsed.second().isOk());
                CHECK(parsed.first().get<Cea608Packet>() == in);
        }
}

TEST_CASE("CEA-608 codec: parse -> build round-trips the wire caption bytes") {
        AncTranslator              t;
        AncPacket                  pkt    = buildCea608(LineField1Line21, 0x14, 0x2F);
        AncTranslator::ParseResult parsed = t.parse(pkt);
        REQUIRE(parsed.second().isOk());
        AncTranslator::PacketsResult rebuilt =
                t.build(parsed.first(), AncFormat(AncFormat::Cea608), AncTransport::St291);
        REQUIRE(rebuilt.second().isOk());
        Result<St291Packet> a = St291Packet::from(pkt);
        Result<St291Packet> b = St291Packet::from(rebuilt.first().front());
        REQUIRE(a.second().isOk());
        REQUIRE(b.second().isOk());
        // LINE byte + both caption bytes survive the round trip byte-for-byte.
        CHECK(a.first().udw() == b.first().udw());
}

TEST_CASE("CEA-608 sync policy: Play copies through, Drop emits nothing") {
        AncTranslator t;
        AncPacket     pkt = buildCea608(LineField1Line21, 0x14, 0x2F);

        auto play = t.applySyncPolicy(pkt, FrameSyncDisposition::play(), 0);
        REQUIRE(play.second().isOk());
        REQUIRE(play.first().size() == 1);

        auto drop = t.applySyncPolicy(pkt, FrameSyncDisposition::drop(), 0);
        REQUIRE(drop.second().isOk());
        CHECK(drop.first().isEmpty());
}

TEST_CASE("CEA-608 sync policy: Repeat[idx>0] nulls the caption pair, keeps framing") {
        AncTranslator t;
        AncPacket     pkt = buildCea608(LineField1Line21, 0x14, 0x2F);

        // idx 0 copies through verbatim.
        auto first = t.applySyncPolicy(pkt, FrameSyncDisposition::repeat(2), 0);
        REQUIRE(first.second().isOk());
        REQUIRE(first.first().size() == 1);
        CHECK(St291Packet::from(first.first().front()).first().udw() ==
              St291Packet::from(pkt).first().udw());

        // idx 1 keeps the LINE byte but blanks the caption pair to a parity
        // null so the held control code does not re-fire.
        auto held = t.applySyncPolicy(pkt, FrameSyncDisposition::repeat(2), 1);
        REQUIRE(held.second().isOk());
        REQUIRE(held.first().size() == 1);
        List<uint16_t> udw = St291Packet::from(held.first().front()).first().udw();
        REQUIRE(udw.size() == 3);
        CHECK((udw[0] & 0xFF) == LineField1Line21);
        CHECK((udw[1] & 0xFF) == Cea608::withOddParity(0x00));
        CHECK((udw[2] & 0xFF) == Cea608::withOddParity(0x00));
}
