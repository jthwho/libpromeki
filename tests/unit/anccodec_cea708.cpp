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
#include <promeki/framesyncdisposition.h>
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
        AncTranslator::PacketsResult built =
                t.build(Variant(cdp), AncFormat(AncFormat::Cea708), AncTransport::St291);
        REQUIRE(built.second().isOk());
        CHECK(built.first().front().format().id() == AncFormat::Cea708);
        CHECK(built.first().front().transport() == AncTransport::St291);

        Result<St291Packet> rp = St291Packet::from(built.first().front());
        REQUIRE(rp.second().isOk());
        CHECK(rp.first().did() == 0x61);
        CHECK(rp.first().sdid() == 0x01);
        CHECK(rp.first().checksumValid());
}

TEST_CASE("CEA-708<->St291: round-trip via AncTranslator parse + build") {
        AncTranslator     t;
        Cea708Cdp         src = makeSampleCdp(0x1234);
        AncTranslator::PacketsResult built =
                t.build(Variant(src), AncFormat(AncFormat::Cea708), AncTransport::St291);
        REQUIRE(built.second().isOk());

        AncTranslator::ParseResult parsed = t.parse(built.first().front());
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
        AncTranslator::PacketsResult built =
                t.build(Variant(src), AncFormat(AncFormat::Cea708), AncTransport::St291);
        REQUIRE(built.second().isOk());
        Result<St291Packet> rp = St291Packet::from(built.first().front());
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
        AncTranslator::ParseResult r = t.parse(p.packet());
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
        AncTranslator::PacketsResult r = t.build(Variant(big), AncFormat(AncFormat::Cea708), AncTransport::St291);
        CHECK(r.second().code() == Error::OutOfRange);
}

TEST_CASE("CEA-708<->St291: round-trip preserves sequence counter mirror") {
        AncTranslator t;
        for (uint16_t seq : {uint16_t(0), uint16_t(1), uint16_t(0xFFFF), uint16_t(0xABCD)}) {
                Cea708Cdp         src = makeSampleCdp(seq);
                AncTranslator::PacketsResult built =
                        t.build(Variant(src), AncFormat(AncFormat::Cea708), AncTransport::St291);
                REQUIRE(built.second().isOk());
                AncTranslator::ParseResult parsed = t.parse(built.first().front());
                REQUIRE(parsed.second().isOk());
                Cea708Cdp out = parsed.first().get<Cea708Cdp>();
                CHECK(out.sequenceCounter == seq);
        }
}

// ===========================================================================
// Frame-sync policy: framing-only CDP on Repeat[idx>0] — sequence_counter
// advances, every cc_data triple is invalidated, CDP envelope (cc_count,
// flags) preserved.  Drop loses the packet; Play / Repeat[idx=0] copy
// through unchanged.
// ===========================================================================

TEST_CASE("CEA-708 sync policy: hasSyncPolicy reflects registration") {
        CHECK(AncTranslator::hasSyncPolicy(AncFormat(AncFormat::Cea708)));
}

TEST_CASE("CEA-708 sync policy: Play returns the packet unchanged") {
        AncTranslator           t;
        AncTranslator::PacketsResult built =
                t.build(Variant(makeSampleCdp(100)), AncFormat(AncFormat::Cea708), AncTransport::St291);
        REQUIRE(built.second().isOk());
        auto res = t.applySyncPolicy(built.first().front(), FrameSyncDisposition::play(), 0);
        REQUIRE(res.second().isOk());
        REQUIRE(res.first().size() == 1);
        CHECK(res.first().front() == built.first().front());
}

TEST_CASE("CEA-708 sync policy: Drop returns an empty list") {
        AncTranslator           t;
        AncTranslator::PacketsResult built =
                t.build(Variant(makeSampleCdp(100)), AncFormat(AncFormat::Cea708), AncTransport::St291);
        REQUIRE(built.second().isOk());
        auto res = t.applySyncPolicy(built.first().front(), FrameSyncDisposition::drop(), 0);
        REQUIRE(res.second().isOk());
        CHECK(res.first().size() == 0);
}

TEST_CASE("CEA-708 sync policy: Repeat[1] idx=0 copies the packet unchanged") {
        AncTranslator           t;
        AncTranslator::PacketsResult built =
                t.build(Variant(makeSampleCdp(100)), AncFormat(AncFormat::Cea708), AncTransport::St291);
        REQUIRE(built.second().isOk());
        auto res = t.applySyncPolicy(built.first().front(), FrameSyncDisposition::repeat(1), 0);
        REQUIRE(res.second().isOk());
        REQUIRE(res.first().size() == 1);
        CHECK(res.first().front() == built.first().front());
}

TEST_CASE("CEA-708 sync policy: Repeat[3] advances sequence_counter by repeatIndex") {
        AncTranslator           t;
        AncTranslator::PacketsResult built =
                t.build(Variant(makeSampleCdp(100)), AncFormat(AncFormat::Cea708), AncTransport::St291);
        REQUIRE(built.second().isOk());

        // idx=0 keeps seq=100 (verbatim copy).  idx=1, 2 advance to 101, 102.
        const uint16_t expectedSeq[3] = {100, 101, 102};
        for (uint8_t i = 0; i < 3; ++i) {
                auto res = t.applySyncPolicy(built.first().front(), FrameSyncDisposition::repeat(3), i);
                REQUIRE(res.second().isOk());
                REQUIRE(res.first().size() == 1);
                AncTranslator::ParseResult parsed = t.parse(res.first().front());
                REQUIRE(parsed.second().isOk());
                Cea708Cdp out = parsed.first().get<Cea708Cdp>();
                CHECK(out.sequenceCounter == expectedSeq[i]);
        }
}

TEST_CASE("CEA-708 sync policy: Repeat[idx>0] invalidates every cc_data triple but preserves count") {
        AncTranslator           t;
        Cea708Cdp               src = makeSampleCdp(100);
        const size_t            srcCount = src.ccData.size();
        REQUIRE(srcCount > 0);
        AncTranslator::PacketsResult built =
                t.build(Variant(src), AncFormat(AncFormat::Cea708), AncTransport::St291);
        REQUIRE(built.second().isOk());

        auto res = t.applySyncPolicy(built.first().front(), FrameSyncDisposition::repeat(2), 1);
        REQUIRE(res.second().isOk());
        REQUIRE(res.first().size() == 1);
        AncTranslator::ParseResult parsed = t.parse(res.first().front());
        REQUIRE(parsed.second().isOk());
        Cea708Cdp out = parsed.first().get<Cea708Cdp>();
        // CDP envelope intact: same number of triples, same frameRateCode,
        // ccDataPresent still true (so receivers see a normal CDP cadence).
        CHECK(out.ccData.size() == srcCount);
        CHECK(out.frameRateCode == src.frameRateCode);
        // Every triple is now invalid — no caption commands re-fire.
        for (const Cea708Cdp::CcData &cc : out.ccData) {
                CHECK_FALSE(cc.valid);
                CHECK(cc.type == 0);
                CHECK(cc.b1 == 0);
                CHECK(cc.b2 == 0);
        }
}

TEST_CASE("CEA-708 sync policy: Repeat[idx>0] preserves the source packet's line / fieldB") {
        AncTranslateConfig srcCfg;
        srcCfg.set(AncTranslateConfig::St291BuildLine, uint16_t(13));
        srcCfg.set(AncTranslateConfig::St291FieldB, true);
        AncTranslator           tSrc(srcCfg);
        AncTranslator::PacketsResult built =
                tSrc.build(Variant(makeSampleCdp(100)), AncFormat(AncFormat::Cea708), AncTransport::St291);
        REQUIRE(built.second().isOk());

        AncTranslator t;  // Default cfg — line=0, fieldB=false.
        auto          res = t.applySyncPolicy(built.first().front(), FrameSyncDisposition::repeat(2), 1);
        REQUIRE(res.second().isOk());
        REQUIRE(res.first().size() == 1);
        Result<St291Packet> rp = St291Packet::from(res.first().front());
        REQUIRE(rp.second().isOk());
        CHECK(rp.first().line() == 13);
        CHECK(rp.first().fieldB() == true);
}
