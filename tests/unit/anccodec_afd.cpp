/**
 * @file      anccodec_afd.cpp
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
#include <promeki/variant.h>

using namespace promeki;

namespace {

        // AFD packing: bit 7 = AR flag, bits 3..6 = AFD code (4 bits).
        constexpr uint8_t pack(uint8_t afdCode, bool ar) {
                uint8_t v = static_cast<uint8_t>((afdCode & 0x0F) << 3);
                if (ar) v = static_cast<uint8_t>(v | 0x80);
                return v;
        }

} // namespace

TEST_CASE("AFD<->St291: round-trip every AFD code with AR=1") {
        AncTranslator t;
        for (uint8_t code = 0; code < 16; ++code) {
                uint8_t           input = pack(code, true);
                Result<AncPacket> built = t.build(Variant(input), AncFormat(AncFormat::Afd), AncTransport::St291);
                REQUIRE(built.second().isOk());
                CHECK(built.first().format().id() == AncFormat::Afd);
                CHECK(built.first().transport() == AncTransport::St291);

                Result<St291Packet> rp = St291Packet::from(built.first());
                REQUIRE(rp.second().isOk());
                CHECK(rp.first().did() == 0x41);
                CHECK(rp.first().sdid() == 0x05);
                CHECK(rp.first().checksumValid());

                Result<Variant> parsed = t.parse(built.first());
                REQUIRE(parsed.second().isOk());
                CHECK(parsed.first().get<uint8_t>() == input);
        }
}

TEST_CASE("AFD<->St291: round-trip AR=0 path") {
        AncTranslator     t;
        uint8_t           input = pack(0x09, false);
        Result<AncPacket> built = t.build(Variant(input), AncFormat(AncFormat::Afd), AncTransport::St291);
        REQUIRE(built.second().isOk());
        Result<Variant> parsed = t.parse(built.first());
        REQUIRE(parsed.second().isOk());
        CHECK(parsed.first().get<uint8_t>() == input);
}

TEST_CASE("AFD<->St291: built packet has 8 UDWs (bar-data slots zeroed)") {
        AncTranslator     t;
        Result<AncPacket> built = t.build(Variant(pack(0x0A, true)), AncFormat(AncFormat::Afd), AncTransport::St291);
        REQUIRE(built.second().isOk());
        Result<St291Packet> rp = St291Packet::from(built.first());
        REQUIRE(rp.second().isOk());
        CHECK(rp.first().dataCount() == 8);
        List<uint16_t> udw = rp.first().udw();
        for (size_t i = 1; i < udw.size(); ++i) {
                CHECK((udw[i] & 0xFF) == 0);
        }
}

TEST_CASE("AFD<->St291: capability queries report parser+builder registered") {
        CHECK(AncTranslator::hasParser(AncFormat(AncFormat::Afd), AncTransport::St291));
        CHECK(AncTranslator::hasBuilder(AncFormat(AncFormat::Afd), AncTransport::St291));
}

TEST_CASE("AFD<->St291: line / fieldB threaded from AncTranslateConfig") {
        AncTranslateConfig cfg;
        cfg.set(AncTranslateConfig::St291BuildLine, uint16_t(13));
        cfg.set(AncTranslateConfig::St291FieldB, true);
        AncTranslator     t(cfg);
        Result<AncPacket> built = t.build(Variant(pack(0x0A, true)), AncFormat(AncFormat::Afd), AncTransport::St291);
        REQUIRE(built.second().isOk());
        Result<St291Packet> rp = St291Packet::from(built.first());
        REQUIRE(rp.second().isOk());
        CHECK(rp.first().line() == 13);
        CHECK(rp.first().fieldB() == true);
}
