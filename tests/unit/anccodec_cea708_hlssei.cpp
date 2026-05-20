/**
 * @file      anccodec_cea708_hlssei.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Coverage for the @c Cea708 ← / → @c HlsSei codec registered in
 * @c anccodec_cea708_hlssei.cpp: round-trip via @c AncTranslator,
 * direct wire-byte inspection, and malformed-input rejection.
 */

#include <cstdint>
#include <doctest/doctest.h>
#include <promeki/ancformat.h>
#include <promeki/ancpacket.h>
#include <promeki/anctranslator.h>
#include <promeki/buffer.h>
#include <promeki/cea708cdp.h>
#include <promeki/error.h>
#include <promeki/metadata.h>
#include <promeki/variant.h>

using namespace promeki;

namespace {

        /// @brief Builds an @ref AncPacket wrapping the given raw bytes
        ///        on transport @c HlsSei + format @c Cea708.
        AncPacket makeSeiPacket(const std::vector<uint8_t> &bytes) {
                Buffer buf(bytes.size());
                buf.setSize(bytes.size());
                if (!bytes.empty()) buf.copyFrom(bytes.data(), bytes.size(), 0);
                return AncPacket(AncFormat(AncFormat::Cea708), AncTransport::HlsSei, std::move(buf),
                                 Metadata());
        }

} // namespace

// ============================================================================
// Capability queries
// ============================================================================

TEST_CASE("Cea708_HlsSei: parser + builder registered against transport HlsSei") {
        AncTranslator t;
        CHECK(t.hasParser(AncFormat(AncFormat::Cea708), AncTransport::HlsSei));
        CHECK(t.hasBuilder(AncFormat(AncFormat::Cea708), AncTransport::HlsSei));
}

// ============================================================================
// Builder — wire layout
// ============================================================================

TEST_CASE("Cea708_HlsSei builder: zero-triple CDP emits canonical 11-byte header + marker") {
        AncTranslator t;
        Cea708Cdp     cdp;
        // ccData empty by default
        AncTranslator::PacketsResult r = t.build(Variant(cdp), AncFormat(AncFormat::Cea708),
                                       AncTransport::HlsSei);
        REQUIRE(r.second().isOk());
        const Buffer &buf = r.first().front().data();
        REQUIRE(buf.size() == 11); // 10-byte ATSC header + 1-byte trailing marker
        const auto *p = static_cast<const uint8_t *>(buf.data());
        CHECK(p[0] == 0xB5);       // country code USA
        CHECK(p[1] == 0x00);       // provider code ATSC high
        CHECK(p[2] == 0x31);       // provider code ATSC low
        CHECK(p[3] == 'G');
        CHECK(p[4] == 'A');
        CHECK(p[5] == '9');
        CHECK(p[6] == '4');
        CHECK(p[7] == 0x03);       // user_data_type_code = cc_data
        CHECK(p[8] == 0xC0);       // flags: process=1, reserved=1, cc_count=0
        CHECK(p[9] == 0xFF);       // em_data
        CHECK(p[10] == 0xFF);      // trailing marker
}

TEST_CASE("Cea708_HlsSei builder: 2-triple CDP packs cc_count + per-triple bytes") {
        AncTranslator t;
        Cea708Cdp     cdp;
        cdp.ccData.pushToBack(Cea708Cdp::CcData{true, 0, 0x94, 0x20});
        cdp.ccData.pushToBack(Cea708Cdp::CcData{true, 1, 0xAA, 0xBB});
        cdp.ccDataPresent = true;
        AncTranslator::PacketsResult r = t.build(Variant(cdp), AncFormat(AncFormat::Cea708),
                                       AncTransport::HlsSei);
        REQUIRE(r.second().isOk());
        const Buffer &buf = r.first().front().data();
        REQUIRE(buf.size() == 11 + 2 * 3);
        const auto *p = static_cast<const uint8_t *>(buf.data());
        CHECK(p[8] == (0xC0 | 0x02)); // cc_count = 2
        // triple 0: cc_valid=1, cc_type=0 → b0 = 0xF8 | 0x04 | 0 = 0xFC
        CHECK(p[10] == 0xFC);
        CHECK(p[11] == 0x94);
        CHECK(p[12] == 0x20);
        // triple 1: cc_valid=1, cc_type=1 → b0 = 0xFD
        CHECK(p[13] == 0xFD);
        CHECK(p[14] == 0xAA);
        CHECK(p[15] == 0xBB);
        CHECK(p[16] == 0xFF); // trailing marker
}

TEST_CASE("Cea708_HlsSei builder: emits AncPacket with transport=HlsSei + format=Cea708") {
        AncTranslator t;
        Cea708Cdp     cdp;
        cdp.ccData.pushToBack(Cea708Cdp::CcData{true, 0, 0x94, 0x20});
        cdp.ccDataPresent = true;
        AncTranslator::PacketsResult r = t.build(Variant(cdp), AncFormat(AncFormat::Cea708),
                                       AncTransport::HlsSei);
        REQUIRE(r.second().isOk());
        CHECK(r.first().front().transport() == AncTransport::HlsSei);
        CHECK(r.first().front().format() == AncFormat(AncFormat::Cea708));
}

TEST_CASE("Cea708_HlsSei builder: > 31 cc_data triples -> OutOfRange") {
        AncTranslator t;
        Cea708Cdp     cdp;
        for (int i = 0; i < 32; ++i) {
                cdp.ccData.pushToBack(Cea708Cdp::CcData{true, 0, 0x80, 0x80});
        }
        cdp.ccDataPresent = true;
        AncTranslator::PacketsResult r = t.build(Variant(cdp), AncFormat(AncFormat::Cea708),
                                       AncTransport::HlsSei);
        CHECK(r.second().code() == Error::OutOfRange);
}

// ============================================================================
// Parser — round-trip
// ============================================================================

TEST_CASE("Cea708_HlsSei parser: full round-trip recovers cc_data triples") {
        AncTranslator t;
        Cea708Cdp     orig;
        orig.ccData.pushToBack(Cea708Cdp::CcData{true, 0, 0xC4, 0x45});  // 'D','E' parity-stamped
        orig.ccData.pushToBack(Cea708Cdp::CcData{true, 2, 0x80, 0x80});  // DTVCC triple
        orig.ccDataPresent = true;

        AncTranslator::PacketsResult built = t.build(Variant(orig), AncFormat(AncFormat::Cea708),
                                           AncTransport::HlsSei);
        REQUIRE(built.second().isOk());
        AncTranslator::ParseResult parsed = t.parse(built.first().front());
        REQUIRE(parsed.second().isOk());
        Cea708Cdp recovered = parsed.first().get<Cea708Cdp>();
        REQUIRE(recovered.ccData.size() == 2);
        CHECK(recovered.ccData[0].valid);
        CHECK(recovered.ccData[0].type == 0);
        CHECK(recovered.ccData[0].b1 == 0xC4);
        CHECK(recovered.ccData[0].b2 == 0x45);
        CHECK(recovered.ccData[1].valid);
        CHECK(recovered.ccData[1].type == 2);
        CHECK(recovered.ccData[1].b1 == 0x80);
        CHECK(recovered.ccData[1].b2 == 0x80);
}

// ============================================================================
// Parser — malformed inputs
// ============================================================================

TEST_CASE("Cea708_HlsSei parser: wrong country code -> CorruptData") {
        AncTranslator t;
        // Replace country byte with 0x00.
        std::vector<uint8_t> bytes = {
                0x00, 0x00, 0x31, 'G',  'A',  '9',  '4',  0x03,
                0xC0, 0xFF, 0xFF,
        };
        AncPacket       pkt = makeSeiPacket(bytes);
        AncTranslator::ParseResult r = t.parse(pkt);
        CHECK(r.second().code() == Error::CorruptData);
}

TEST_CASE("Cea708_HlsSei parser: wrong provider code -> CorruptData") {
        AncTranslator t;
        std::vector<uint8_t> bytes = {
                0xB5, 0x00, 0x00, 'G',  'A',  '9',  '4',  0x03,
                0xC0, 0xFF, 0xFF,
        };
        AncPacket       pkt = makeSeiPacket(bytes);
        AncTranslator::ParseResult r = t.parse(pkt);
        CHECK(r.second().code() == Error::CorruptData);
}

TEST_CASE("Cea708_HlsSei parser: wrong user_identifier -> CorruptData") {
        AncTranslator t;
        std::vector<uint8_t> bytes = {
                0xB5, 0x00, 0x31, 'B',  'A',  'D',  '!',  0x03,
                0xC0, 0xFF, 0xFF,
        };
        AncPacket       pkt = makeSeiPacket(bytes);
        AncTranslator::ParseResult r = t.parse(pkt);
        CHECK(r.second().code() == Error::CorruptData);
}

TEST_CASE("Cea708_HlsSei parser: wrong user_data_type_code -> CorruptData") {
        AncTranslator t;
        std::vector<uint8_t> bytes = {
                0xB5, 0x00, 0x31, 'G',  'A',  '9',  '4',  0xFF, // not cc_data
                0xC0, 0xFF, 0xFF,
        };
        AncPacket       pkt = makeSeiPacket(bytes);
        AncTranslator::ParseResult r = t.parse(pkt);
        CHECK(r.second().code() == Error::CorruptData);
}

TEST_CASE("Cea708_HlsSei parser: truncated cc_data -> CorruptData") {
        AncTranslator t;
        // cc_count = 2 (needs 6 bytes of triples), but only 3 follow.
        std::vector<uint8_t> bytes = {
                0xB5, 0x00, 0x31, 'G',  'A',  '9',  '4',  0x03,
                static_cast<uint8_t>(0xC0 | 0x02), // cc_count = 2
                0xFF,
                0xFC, 0x94, 0x20, // only 1 triple available
        };
        AncPacket       pkt = makeSeiPacket(bytes);
        AncTranslator::ParseResult r = t.parse(pkt);
        CHECK(r.second().code() == Error::CorruptData);
}

TEST_CASE("Cea708_HlsSei parser: too-short packet -> CorruptData") {
        AncTranslator        t;
        std::vector<uint8_t> bytes = {0xB5, 0x00, 0x31}; // way too short
        AncPacket       pkt = makeSeiPacket(bytes);
        AncTranslator::ParseResult r = t.parse(pkt);
        CHECK(r.second().code() == Error::CorruptData);
}

// ============================================================================
// Translate — fallback via parse + build
// ============================================================================

TEST_CASE("AncTranslator::translate: St291 Cea708 -> HlsSei goes through parse+build") {
        AncTranslator t;
        // Build a CDP, wrap it on St291.
        Cea708Cdp cdp;
        cdp.ccData.pushToBack(Cea708Cdp::CcData{true, 0, 0xC4, 0x45});
        cdp.ccDataPresent = true;
        AncTranslator::PacketsResult st291 = t.build(Variant(cdp), AncFormat(AncFormat::Cea708),
                                          AncTransport::St291);
        REQUIRE(st291.second().isOk());
        // Translate to HlsSei.
        AncTranslator::PacketsResult sei = t.translate(st291.first().front(), AncTransport::HlsSei);
        REQUIRE(sei.second().isOk());
        CHECK(sei.first().front().transport() == AncTransport::HlsSei);
        // Reverse: HlsSei → St291 (caption bytes round-trip).
        AncTranslator::PacketsResult back = t.translate(sei.first().front(), AncTransport::St291);
        REQUIRE(back.second().isOk());
        CHECK(back.first().front().transport() == AncTransport::St291);
        AncTranslator::ParseResult parsed = t.parse(back.first().front());
        REQUIRE(parsed.second().isOk());
        Cea708Cdp recovered = parsed.first().get<Cea708Cdp>();
        REQUIRE(recovered.ccData.size() == 1);
        CHECK(recovered.ccData[0].b1 == 0xC4);
        CHECK(recovered.ccData[0].b2 == 0x45);
}
