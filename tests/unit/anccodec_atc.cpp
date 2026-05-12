/**
 * @file      anccodec_atc.cpp
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
#include <promeki/result.h>
#include <promeki/st291packet.h>
#include <promeki/timecode.h>
#include <promeki/variant.h>

using namespace promeki;

namespace {

        Result<Variant> parseVia(const AncPacket &pkt, const AncTranslateConfig &cfg = {}) {
                AncTranslator t(cfg);
                return t.parse(pkt);
        }

        Result<AncPacket> buildVia(const Timecode &tc, AncFormat::ID id, const AncTranslateConfig &cfg = {}) {
                AncTranslator t(cfg);
                return t.build(Variant(tc), AncFormat(id), AncTransport::St291);
        }

} // namespace

TEST_CASE("ATC<->St291: round-trip 01:23:45:14 NDF30 on AtcLtc") {
        Timecode src(Timecode::Mode(Timecode::NDF30), 1, 23, 45, 14);
        AncTranslateConfig cfg;
        cfg.set(AncTranslateConfig::St291BuildLine, uint16_t(9));

        Result<AncPacket> built = buildVia(src, AncFormat::AtcLtc, cfg);
        REQUIRE(built.second().isOk());
        CHECK(built.first().format().id() == AncFormat::AtcLtc);
        CHECK(built.first().transport() == AncTransport::St291);

        Result<St291Packet> rp = St291Packet::from(built.first());
        REQUIRE(rp.second().isOk());
        CHECK(rp.first().did() == 0x60);
        CHECK(rp.first().sdid() == 0x60);
        CHECK(rp.first().line() == 9);
        CHECK(rp.first().checksumValid());

        Result<Variant> parsed = parseVia(built.first());
        REQUIRE(parsed.second().isOk());
        Timecode out = parsed.first().get<Timecode>();
        CHECK(out.hour() == 1);
        CHECK(out.min() == 23);
        CHECK(out.sec() == 45);
        CHECK(out.frame() == 14);
        CHECK_FALSE(out.isDropFrame());
}

TEST_CASE("ATC<->St291: round-trip 00:00:00:00 NDF30 (boundary)") {
        Timecode           src(Timecode::Mode(Timecode::NDF30), 0, 0, 0, 0);
        Result<AncPacket>  built = buildVia(src, AncFormat::AtcVitc1);
        REQUIRE(built.second().isOk());
        Result<St291Packet> rp = St291Packet::from(built.first());
        REQUIRE(rp.second().isOk());
        CHECK(rp.first().sdid() == 0x61);

        Result<Variant> parsed = parseVia(built.first());
        REQUIRE(parsed.second().isOk());
        Timecode out = parsed.first().get<Timecode>();
        CHECK(out.hour() == 0);
        CHECK(out.min() == 0);
        CHECK(out.sec() == 0);
        CHECK(out.frame() == 0);
}

TEST_CASE("ATC<->St291: round-trip 23:59:59:29 NDF30 (max digits)") {
        Timecode          src(Timecode::Mode(Timecode::NDF30), 23, 59, 59, 29);
        Result<AncPacket> built = buildVia(src, AncFormat::AtcVitc2);
        REQUIRE(built.second().isOk());
        Result<St291Packet> rp = St291Packet::from(built.first());
        REQUIRE(rp.second().isOk());
        CHECK(rp.first().sdid() == 0x62);

        Result<Variant> parsed = parseVia(built.first());
        REQUIRE(parsed.second().isOk());
        Timecode out = parsed.first().get<Timecode>();
        CHECK(out.hour() == 23);
        CHECK(out.min() == 59);
        CHECK(out.sec() == 59);
        CHECK(out.frame() == 29);
}

TEST_CASE("ATC<->St291: drop-frame bit round-trips") {
        Timecode          src(Timecode::Mode(Timecode::DF30), 1, 0, 0, 2);
        Result<AncPacket> built = buildVia(src, AncFormat::AtcLtc);
        REQUIRE(built.second().isOk());

        Result<Variant> parsed = parseVia(built.first());
        REQUIRE(parsed.second().isOk());
        Timecode out = parsed.first().get<Timecode>();
        CHECK(out.hour() == 1);
        CHECK(out.frame() == 2);
        CHECK(out.isDropFrame());
}

TEST_CASE("ATC<->St291: cfg-driven line + field-B propagate to meta") {
        Timecode           src(Timecode::Mode(Timecode::NDF25), 5, 10, 15, 20);
        AncTranslateConfig cfg;
        cfg.set(AncTranslateConfig::St291BuildLine, uint16_t(11));
        cfg.set(AncTranslateConfig::St291FieldB, true);
        Result<AncPacket> built = buildVia(src, AncFormat::AtcVitc1, cfg);
        REQUIRE(built.second().isOk());
        Result<St291Packet> rp = St291Packet::from(built.first());
        REQUIRE(rp.second().isOk());
        CHECK(rp.first().line() == 11);
        CHECK(rp.first().fieldB() == true);
}

TEST_CASE("ATC<->St291: parse rejects too-short UDW count") {
        // Hand-build a packet with only 3 UDWs (below the required 8).
        List<uint16_t>  udw;
        udw.pushToBack(0x00);
        udw.pushToBack(0x00);
        udw.pushToBack(0x00);
        St291Packet     p = St291Packet::build(AncFormat(AncFormat::AtcLtc), udw, 0);
        Result<Variant> parsed = parseVia(p.packet());
        CHECK(parsed.second().code() == Error::CorruptData);
}

TEST_CASE("ATC<->St291: capability queries report parser+builder registered") {
        CHECK(AncTranslator::hasParser(AncFormat(AncFormat::AtcLtc), AncTransport::St291));
        CHECK(AncTranslator::hasBuilder(AncFormat(AncFormat::AtcLtc), AncTransport::St291));
        CHECK(AncTranslator::hasParser(AncFormat(AncFormat::AtcVitc1), AncTransport::St291));
        CHECK(AncTranslator::hasBuilder(AncFormat(AncFormat::AtcVitc1), AncTransport::St291));
        CHECK(AncTranslator::hasParser(AncFormat(AncFormat::AtcVitc2), AncTransport::St291));
        CHECK(AncTranslator::hasBuilder(AncFormat(AncFormat::AtcVitc2), AncTransport::St291));
}

TEST_CASE("ATC<->St291: translate(pkt, St291) identity-short-circuits") {
        Timecode          src(Timecode::Mode(Timecode::NDF30), 12, 0, 0, 0);
        Result<AncPacket> built = buildVia(src, AncFormat::AtcLtc);
        REQUIRE(built.second().isOk());

        AncTranslator     t;
        Result<AncPacket> r = t.translate(built.first(), AncTransport::St291);
        CHECK(r.second().isOk());
        // Identity returns the same impl handle (cheap refcount bump).
        CHECK(r.first() == built.first());
}
