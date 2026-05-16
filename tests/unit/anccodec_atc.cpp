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
#include <promeki/framesyncdisposition.h>
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

        Result<List<AncPacket>> buildVia(const Timecode &tc, AncFormat::ID id, const AncTranslateConfig &cfg = {}) {
                AncTranslator t(cfg);
                return t.build(Variant(tc), AncFormat(id), AncTransport::St291);
        }

} // namespace

TEST_CASE("ATC<->St291: round-trip 01:23:45:14 NDF30 on AtcLtc") {
        Timecode src(Timecode::Mode(Timecode::NDF30), 1, 23, 45, 14);
        AncTranslateConfig cfg;
        cfg.set(AncTranslateConfig::St291BuildLine, uint16_t(9));

        Result<List<AncPacket>> built = buildVia(src, AncFormat::AtcLtc, cfg);
        REQUIRE(built.second().isOk());
        CHECK(built.first().front().format().id() == AncFormat::AtcLtc);
        CHECK(built.first().front().transport() == AncTransport::St291);

        Result<St291Packet> rp = St291Packet::from(built.first().front());
        REQUIRE(rp.second().isOk());
        CHECK(rp.first().did() == 0x60);
        CHECK(rp.first().sdid() == 0x60);
        CHECK(rp.first().line() == 9);
        CHECK(rp.first().checksumValid());

        Result<Variant> parsed = parseVia(built.first().front());
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
        Result<List<AncPacket>>  built = buildVia(src, AncFormat::AtcVitc1);
        REQUIRE(built.second().isOk());
        Result<St291Packet> rp = St291Packet::from(built.first().front());
        REQUIRE(rp.second().isOk());
        CHECK(rp.first().sdid() == 0x61);

        Result<Variant> parsed = parseVia(built.first().front());
        REQUIRE(parsed.second().isOk());
        Timecode out = parsed.first().get<Timecode>();
        CHECK(out.hour() == 0);
        CHECK(out.min() == 0);
        CHECK(out.sec() == 0);
        CHECK(out.frame() == 0);
}

TEST_CASE("ATC<->St291: round-trip 23:59:59:29 NDF30 (max digits)") {
        Timecode          src(Timecode::Mode(Timecode::NDF30), 23, 59, 59, 29);
        Result<List<AncPacket>> built = buildVia(src, AncFormat::AtcVitc2);
        REQUIRE(built.second().isOk());
        Result<St291Packet> rp = St291Packet::from(built.first().front());
        REQUIRE(rp.second().isOk());
        CHECK(rp.first().sdid() == 0x62);

        Result<Variant> parsed = parseVia(built.first().front());
        REQUIRE(parsed.second().isOk());
        Timecode out = parsed.first().get<Timecode>();
        CHECK(out.hour() == 23);
        CHECK(out.min() == 59);
        CHECK(out.sec() == 59);
        CHECK(out.frame() == 29);
}

TEST_CASE("ATC<->St291: drop-frame bit round-trips") {
        Timecode          src(Timecode::Mode(Timecode::DF30), 1, 0, 0, 2);
        Result<List<AncPacket>> built = buildVia(src, AncFormat::AtcLtc);
        REQUIRE(built.second().isOk());

        Result<Variant> parsed = parseVia(built.first().front());
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
        Result<List<AncPacket>> built = buildVia(src, AncFormat::AtcVitc1, cfg);
        REQUIRE(built.second().isOk());
        Result<St291Packet> rp = St291Packet::from(built.first().front());
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
        Result<List<AncPacket>> built = buildVia(src, AncFormat::AtcLtc);
        REQUIRE(built.second().isOk());

        AncTranslator     t;
        Result<List<AncPacket>> r = t.translate(built.first().front(), AncTransport::St291);
        CHECK(r.second().isOk());
        REQUIRE_FALSE(r.first().isEmpty());
        // Identity returns the same impl handle (cheap refcount bump).
        CHECK(r.first().front() == built.first().front());
}

// ===========================================================================
// Frame-sync policy: ATC must keep advancing across a Repeat run, must drop
// on Drop, and must preserve the wire bytes on Play / Repeat[idx=0].
// ===========================================================================

TEST_CASE("ATC sync policy: hasSyncPolicy reflects registration on all three IDs") {
        CHECK(AncTranslator::hasSyncPolicy(AncFormat(AncFormat::AtcLtc)));
        CHECK(AncTranslator::hasSyncPolicy(AncFormat(AncFormat::AtcVitc1)));
        CHECK(AncTranslator::hasSyncPolicy(AncFormat(AncFormat::AtcVitc2)));
}

TEST_CASE("ATC sync policy: Play returns the packet unchanged") {
        Timecode                src(Timecode::Mode(Timecode::NDF30), 1, 0, 0, 0);
        Result<List<AncPacket>> built = buildVia(src, AncFormat::AtcLtc);
        REQUIRE(built.second().isOk());

        AncTranslator t;
        auto          res = t.applySyncPolicy(built.first().front(), FrameSyncDisposition::play(), 0);
        REQUIRE(res.second().isOk());
        REQUIRE(res.first().size() == 1);
        // Play returns the original packet — same wire bytes, same pkt identity.
        CHECK(res.first().front() == built.first().front());
}

TEST_CASE("ATC sync policy: Drop returns an empty list") {
        Timecode                src(Timecode::Mode(Timecode::NDF30), 1, 0, 0, 0);
        Result<List<AncPacket>> built = buildVia(src, AncFormat::AtcLtc);
        REQUIRE(built.second().isOk());
        AncTranslator t;
        auto          res = t.applySyncPolicy(built.first().front(), FrameSyncDisposition::drop(), 0);
        REQUIRE(res.second().isOk());
        CHECK(res.first().size() == 0);
}

TEST_CASE("ATC sync policy: Repeat[1] idx=0 copies the packet through unchanged") {
        Timecode                src(Timecode::Mode(Timecode::NDF30), 1, 0, 0, 0);
        Result<List<AncPacket>> built = buildVia(src, AncFormat::AtcLtc);
        REQUIRE(built.second().isOk());
        AncTranslator t;
        auto          res = t.applySyncPolicy(built.first().front(), FrameSyncDisposition::repeat(1), 0);
        REQUIRE(res.second().isOk());
        REQUIRE(res.first().size() == 1);
        CHECK(res.first().front() == built.first().front());
}

TEST_CASE("ATC sync policy: Repeat[3] increments timecode by repeatIndex (NDF30)") {
        Timecode                src(Timecode::Mode(Timecode::NDF30), 1, 0, 0, 0);
        Result<List<AncPacket>> built = buildVia(src, AncFormat::AtcLtc);
        REQUIRE(built.second().isOk());
        AncTranslator t;

        // Expected output digits: idx=0→01:00:00:00, idx=1→01:00:00:01, idx=2→01:00:00:02.
        const Timecode::DigitType expectedFrames[3] = {0, 1, 2};
        for (uint8_t i = 0; i < 3; ++i) {
                auto res = t.applySyncPolicy(built.first().front(), FrameSyncDisposition::repeat(3), i);
                REQUIRE(res.second().isOk());
                REQUIRE(res.first().size() == 1);
                Result<Variant> parsed = parseVia(res.first().front());
                REQUIRE(parsed.second().isOk());
                Timecode out = parsed.first().get<Timecode>();
                CHECK(out.hour() == 1);
                CHECK(out.min() == 0);
                CHECK(out.sec() == 0);
                CHECK(out.frame() == expectedFrames[i]);
                CHECK_FALSE(out.isDropFrame());
        }
}

TEST_CASE("ATC sync policy: Repeat across the DF30 minute-rollover boundary") {
        // Load-bearing case: at 00:00:59:29 in DF30 the next frame is
        // 00:01:00:02 (frames 00 and 01 of every minute except every 10th
        // are dropped).  Verify the libvtc-backed Timecode::operator++
        // handles that correctly under our sync-policy increment.
        Timecode src(Timecode::Mode(Timecode::DF30), 0, 0, 59, 29);
        Result<List<AncPacket>> built = buildVia(src, AncFormat::AtcLtc);
        REQUIRE(built.second().isOk());
        AncTranslator t;

        struct Expect {
                        Timecode::DigitType m, s, f;
        };
        // idx=0 → 00:00:59:29 (unchanged)
        // idx=1 → 00:01:00:02 (DF rollover skips :00 and :01)
        // idx=2 → 00:01:00:03
        // idx=3 → 00:01:00:04
        const Expect expected[4] = {{0, 59, 29}, {1, 0, 2}, {1, 0, 3}, {1, 0, 4}};

        for (uint8_t i = 0; i < 4; ++i) {
                auto res = t.applySyncPolicy(built.first().front(), FrameSyncDisposition::repeat(4), i);
                REQUIRE(res.second().isOk());
                REQUIRE(res.first().size() == 1);
                Result<Variant> parsed = parseVia(res.first().front());
                REQUIRE(parsed.second().isOk());
                Timecode out = parsed.first().get<Timecode>();
                CHECK(out.hour() == 0);
                CHECK(out.min() == expected[i].m);
                CHECK(out.sec() == expected[i].s);
                CHECK(out.frame() == expected[i].f);
                CHECK(out.isDropFrame());
        }
}

TEST_CASE("ATC sync policy: Repeat preserves the original packet's line / fieldB") {
        // Build with line=11, fieldB=true; verify that after a Repeat[idx>0]
        // re-encode the output still carries those framing values rather
        // than the held translator's defaults (line=0, fieldB=false).
        Timecode           src(Timecode::Mode(Timecode::NDF30), 2, 0, 0, 0);
        AncTranslateConfig srcCfg;
        srcCfg.set(AncTranslateConfig::St291BuildLine, uint16_t(11));
        srcCfg.set(AncTranslateConfig::St291FieldB, true);
        Result<List<AncPacket>> built = buildVia(src, AncFormat::AtcLtc, srcCfg);
        REQUIRE(built.second().isOk());

        AncTranslator t;  // Default cfg — line=0, fieldB=false.
        auto          res = t.applySyncPolicy(built.first().front(), FrameSyncDisposition::repeat(2), 1);
        REQUIRE(res.second().isOk());
        REQUIRE(res.first().size() == 1);
        Result<St291Packet> rp = St291Packet::from(res.first().front());
        REQUIRE(rp.second().isOk());
        // Source-packet framing is preserved across the re-encode.
        CHECK(rp.first().line() == 11);
        CHECK(rp.first().fieldB() == true);
}
