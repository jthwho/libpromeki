/**
 * @file      anccodec_cea708.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/ancformat.h>
#include <promeki/ancpacket.h>
#include <promeki/anctranslateconfig.h>
#include <promeki/anctranslator.h>
#include <promeki/cea708cdp.h>
#include <promeki/result.h>
#include <promeki/st291packet.h>
#include <promeki/variant.h>

using namespace promeki;

namespace {

        Cea708Cdp makeSampleCdp(uint16_t seq = 7) {
                Cea708Cdp::CcDataList triples;
                triples.pushToBack({true, 0, 0x94, 0x20});
                triples.pushToBack({true, 0, 'h' | 0x80, 'i' | 0x80});
                triples.pushToBack({true, 0, '!' | 0x80, 0x00 | 0x80});
                return Cea708Cdp(4, triples, seq);
        }

} // namespace

TEST_CASE("CEA-708<->St291: capability queries report parser+builder registered") {
        CHECK(AncTranslator::hasParser(AncFormat(AncFormat::Cea708), AncTransport::St291));
        CHECK(AncTranslator::hasBuilder(AncFormat(AncFormat::Cea708), AncTransport::St291));
}

TEST_CASE("CEA-708<->St291: build emits a CEA-708 ST 291 packet on DID 0x61 / SDID 0x01") {
        AncTranslator     t;
        Cea708Cdp         cdp = makeSampleCdp();
        Result<AncPacket> built =
                t.build(Variant(cdp), AncFormat(AncFormat::Cea708), AncTransport::St291);
        REQUIRE(built.second().isOk());
        CHECK(built.first().format().id() == AncFormat::Cea708);
        CHECK(built.first().transport() == AncTransport::St291);

        Result<St291Packet> rp = St291Packet::from(built.first());
        REQUIRE(rp.second().isOk());
        CHECK(rp.first().did() == 0x61);
        CHECK(rp.first().sdid() == 0x01);
        CHECK(rp.first().checksumValid());
}

TEST_CASE("CEA-708<->St291: round-trip via AncTranslator parse + build") {
        AncTranslator     t;
        Cea708Cdp         src = makeSampleCdp(0x1234);
        Result<AncPacket> built =
                t.build(Variant(src), AncFormat(AncFormat::Cea708), AncTransport::St291);
        REQUIRE(built.second().isOk());

        Result<Variant> parsed = t.parse(built.first());
        REQUIRE(parsed.second().isOk());
        Cea708Cdp restored = parsed.first().get<Cea708Cdp>();
        CHECK(restored == src);
}

TEST_CASE("CEA-708<->St291: cfg-driven line + field-B threaded to meta") {
        AncTranslateConfig cfg;
        cfg.set(AncTranslateConfig::St291BuildLine, uint16_t(11));
        cfg.set(AncTranslateConfig::St291FieldB, true);
        AncTranslator     t(cfg);
        Cea708Cdp         src = makeSampleCdp();
        Result<AncPacket> built =
                t.build(Variant(src), AncFormat(AncFormat::Cea708), AncTransport::St291);
        REQUIRE(built.second().isOk());
        Result<St291Packet> rp = St291Packet::from(built.first());
        REQUIRE(rp.second().isOk());
        CHECK(rp.first().line() == 11);
        CHECK(rp.first().fieldB() == true);
}

TEST_CASE("CEA-708<->St291: parse rejects too-short ST 291 payload") {
        AncTranslator t;
        // Hand-build an ST 291 packet with only 4 UDWs (below the 11-byte
        // minimum CDP size).
        List<uint16_t> udw;
        udw.pushToBack(0x96);
        udw.pushToBack(0x69);
        udw.pushToBack(0x04);
        udw.pushToBack(0x4F);
        St291Packet     p = St291Packet::build(AncFormat(AncFormat::Cea708), udw, 0);
        Result<Variant> r = t.parse(p.packet());
        CHECK(r.second().code() == Error::CorruptData);
}

TEST_CASE("CEA-708<->St291: oversize CDP returns OutOfRange") {
        AncTranslator         t;
        Cea708Cdp::CcDataList triples;
        // 80 triples × 3 bytes + 2 section header + 7 header + 4 footer
        // = 253 — under 255.  Push 85 triples to overflow.
        for (int i = 0; i < 85; ++i) {
                triples.pushToBack({true, 0, static_cast<uint8_t>(i), static_cast<uint8_t>(i + 1)});
        }
        Cea708Cdp         big(4, triples, 1);
        Result<AncPacket> r = t.build(Variant(big), AncFormat(AncFormat::Cea708), AncTransport::St291);
        CHECK(r.second().code() == Error::OutOfRange);
}

TEST_CASE("CEA-708<->St291: round-trip preserves sequence counter mirror") {
        AncTranslator t;
        for (uint16_t seq : {uint16_t(0), uint16_t(1), uint16_t(0xFFFF), uint16_t(0xABCD)}) {
                Cea708Cdp         src = makeSampleCdp(seq);
                Result<AncPacket> built =
                        t.build(Variant(src), AncFormat(AncFormat::Cea708), AncTransport::St291);
                REQUIRE(built.second().isOk());
                Result<Variant> parsed = t.parse(built.first());
                REQUIRE(parsed.second().isOk());
                Cea708Cdp out = parsed.first().get<Cea708Cdp>();
                CHECK(out.sequenceCounter == seq);
        }
}
