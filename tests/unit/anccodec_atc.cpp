/**
 * @file      anccodec_atc.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/ancatc.h>
#include <promeki/ancformat.h>
#include <promeki/ancmeta.h>
#include <promeki/ancpacket.h>
#include <promeki/anctranslateconfig.h>
#include <promeki/anctranslator.h>
#include <promeki/bufferiodevice.h>
#include <promeki/datastream.h>
#include <promeki/framesyncdisposition.h>
#include <promeki/metadata.h>
#include <promeki/result.h>
#include <promeki/st291packet.h>
#include <promeki/timecode.h>
#include <promeki/variant.h>

using namespace promeki;

namespace {

        // Inserts an ATC parse rate hint into @p cfg when one is not
        // already set.  Most pre-F5 tests built and parsed at a single
        // implied rate (30 fps NDF / DF, sometimes 25 NDF); after the
        // F5 D1b decision (Q4) the parser hard-errors on missing rate
        // context, so each test path supplies one via the cfg.
        AncTranslateConfig withDefaultHint(AncTranslateConfig cfg, uint32_t fps = 30) {
                Error err;
                cfg.getAs<uint32_t>(AncTranslateConfig::AtcParseRateHint, uint32_t(0), &err);
                if (err.isError()) {
                        cfg.set(AncTranslateConfig::AtcParseRateHint, fps);
                }
                return cfg;
        }

        AncTranslator::ParseResult parseVia(const AncPacket &pkt, const AncTranslateConfig &cfg = {}) {
                AncTranslator t(withDefaultHint(cfg));
                return t.parse(pkt);
        }

        AncTranslator::PacketsResult buildVia(const Timecode &tc, AncFormat::ID id, const AncTranslateConfig &cfg = {}) {
                AncTranslator t(cfg);
                return t.build(Variant(tc), AncFormat(id), AncTransport::St291);
        }

        // Pulls the wall-clock Timecode out of an AncAtc-carrying
        // Variant, mirroring the pre-F5 `parsed.get<Timecode>()`
        // ergonomics in test bodies.
        Timecode atcTimecode(const Variant &v) {
                return v.get<AncAtc>().timecode();
        }

} // namespace

TEST_CASE("ATC<->St291: round-trip 01:23:45:14 NDF30 on AtcLtc") {
        Timecode src(Timecode::Mode(Timecode::NDF30), 1, 23, 45, 14);
        AncTranslateConfig cfg;
        cfg.set(AncTranslateConfig::St291BuildLine, uint16_t(9));

        AncTranslator::PacketsResult built = buildVia(src, AncFormat::AtcLtc, cfg);
        REQUIRE(built.second().isOk());
        CHECK(built.first().front().format().id() == AncFormat::AtcLtc);
        CHECK(built.first().front().transport() == AncTransport::St291);

        Result<St291Packet> rp = St291Packet::from(built.first().front());
        REQUIRE(rp.second().isOk());
        CHECK(rp.first().did() == 0x60);
        CHECK(rp.first().sdid() == 0x60);
        CHECK(rp.first().line() == 9);
        CHECK(rp.first().checksumValid());

        AncTranslator::ParseResult parsed = parseVia(built.first().front());
        REQUIRE(parsed.second().isOk());
        Timecode out = atcTimecode(parsed.first());
        CHECK(out.hour() == 1);
        CHECK(out.min() == 23);
        CHECK(out.sec() == 45);
        CHECK(out.frame() == 14);
        CHECK_FALSE(out.isDropFrame());
}

TEST_CASE("ATC<->St291: round-trip 00:00:00:00 NDF30 on AtcVitc1") {
        // ST 12-2:2014 §5 collapses LTC/VITC1/VITC2 onto (DID=0x60,
        // SDID=0x60); the DBB1 byte discriminates.  AncAtc::payloadType
        // round-trips through parse → build.
        Timecode           src(Timecode::Mode(Timecode::NDF30), 0, 0, 0, 0);
        AncTranslator::PacketsResult  built = buildVia(src, AncFormat::AtcVitc1);
        REQUIRE(built.second().isOk());
        Result<St291Packet> rp = St291Packet::from(built.first().front());
        REQUIRE(rp.second().isOk());
        CHECK(rp.first().sdid() == 0x60);

        AncTranslator::ParseResult parsed = parseVia(built.first().front());
        REQUIRE(parsed.second().isOk());
        AncAtc atc = parsed.first().get<AncAtc>();
        Timecode out = atc.timecode();
        CHECK(out.hour() == 0);
        CHECK(out.min() == 0);
        CHECK(out.sec() == 0);
        CHECK(out.frame() == 0);
        CHECK(atc.payloadType() == AncAtc::Vitc1);
}

TEST_CASE("ATC<->St291: round-trip 23:59:59:29 NDF30 on AtcVitc2") {
        Timecode          src(Timecode::Mode(Timecode::NDF30), 23, 59, 59, 29);
        AncTranslator::PacketsResult built = buildVia(src, AncFormat::AtcVitc2);
        REQUIRE(built.second().isOk());
        Result<St291Packet> rp = St291Packet::from(built.first().front());
        REQUIRE(rp.second().isOk());
        CHECK(rp.first().sdid() == 0x60);

        AncTranslator::ParseResult parsed = parseVia(built.first().front());
        REQUIRE(parsed.second().isOk());
        AncAtc atc = parsed.first().get<AncAtc>();
        Timecode out = atc.timecode();
        CHECK(out.hour() == 23);
        CHECK(out.min() == 59);
        CHECK(out.sec() == 59);
        CHECK(out.frame() == 29);
        CHECK(atc.payloadType() == AncAtc::Vitc2);
}

TEST_CASE("ATC<->St291: drop-frame bit round-trips") {
        Timecode          src(Timecode::Mode(Timecode::DF30), 1, 0, 0, 2);
        AncTranslator::PacketsResult built = buildVia(src, AncFormat::AtcLtc);
        REQUIRE(built.second().isOk());

        AncTranslator::ParseResult parsed = parseVia(built.first().front());
        REQUIRE(parsed.second().isOk());
        Timecode out = atcTimecode(parsed.first());
        CHECK(out.hour() == 1);
        CHECK(out.frame() == 2);
        CHECK(out.isDropFrame());
}

TEST_CASE("ATC<->St291: cfg-driven line + field-B propagate to meta") {
        Timecode           src(Timecode::Mode(Timecode::NDF25), 5, 10, 15, 20);
        AncTranslateConfig cfg;
        cfg.set(AncTranslateConfig::St291BuildLine, uint16_t(11));
        cfg.set(AncTranslateConfig::St291FieldB, true);
        AncTranslator::PacketsResult built = buildVia(src, AncFormat::AtcVitc1, cfg);
        REQUIRE(built.second().isOk());
        Result<St291Packet> rp = St291Packet::from(built.first().front());
        REQUIRE(rp.second().isOk());
        CHECK(rp.first().line() == 11);
        CHECK(rp.first().fieldB() == true);
}

TEST_CASE("ATC<->St291: parse rejects too-short UDW count (ST 12-2 §5 requires DC=0x10)") {
        // Hand-build a packet with only 3 UDWs (well below the required 16).
        List<uint16_t>  udw;
        udw.pushToBack(0x00);
        udw.pushToBack(0x00);
        udw.pushToBack(0x00);
        St291Packet     p = St291Packet::build(AncFormat(AncFormat::AtcLtc), udw, 0);
        AncTranslator::ParseResult parsed = parseVia(p.packet());
        CHECK(parsed.second().code() == Error::CorruptData);
}

TEST_CASE("ATC<->St291: parse rejects DC=15 (one short of the spec-required 16)") {
        // Boundary case: 15 UDWs is one less than the mandatory DC=0x10.
        List<uint16_t> udw;
        udw.resize(15);
        for (size_t i = 0; i < udw.size(); ++i) udw[i] = 0x00;
        St291Packet     p = St291Packet::build(AncFormat(AncFormat::AtcLtc), udw, 0);
        AncTranslator::ParseResult parsed = parseVia(p.packet());
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
        AncTranslator::PacketsResult built = buildVia(src, AncFormat::AtcLtc);
        REQUIRE(built.second().isOk());

        AncTranslator     t;
        AncTranslator::PacketsResult r = t.translate(built.first().front(), AncTransport::St291);
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
        AncTranslator::PacketsResult built = buildVia(src, AncFormat::AtcLtc);
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
        AncTranslator::PacketsResult built = buildVia(src, AncFormat::AtcLtc);
        REQUIRE(built.second().isOk());
        AncTranslator t;
        auto          res = t.applySyncPolicy(built.first().front(), FrameSyncDisposition::drop(), 0);
        REQUIRE(res.second().isOk());
        CHECK(res.first().size() == 0);
}

TEST_CASE("ATC sync policy: Repeat[1] idx=0 copies the packet through unchanged") {
        Timecode                src(Timecode::Mode(Timecode::NDF30), 1, 0, 0, 0);
        AncTranslator::PacketsResult built = buildVia(src, AncFormat::AtcLtc);
        REQUIRE(built.second().isOk());
        AncTranslator t;
        auto          res = t.applySyncPolicy(built.first().front(), FrameSyncDisposition::repeat(1), 0);
        REQUIRE(res.second().isOk());
        REQUIRE(res.first().size() == 1);
        CHECK(res.first().front() == built.first().front());
}

TEST_CASE("ATC sync policy: Repeat[3] increments timecode by repeatIndex (NDF30)") {
        Timecode                src(Timecode::Mode(Timecode::NDF30), 1, 0, 0, 0);
        AncTranslator::PacketsResult built = buildVia(src, AncFormat::AtcLtc);
        REQUIRE(built.second().isOk());
        // The sync policy parses internally on Repeat[idx>0], so the
        // translator's held cfg must supply the rate hint per F5 D1b.
        AncTranslateConfig tcfg;
        tcfg.set(AncTranslateConfig::AtcParseRateHint, uint32_t(30));
        AncTranslator t(tcfg);

        // Expected output digits: idx=0→01:00:00:00, idx=1→01:00:00:01, idx=2→01:00:00:02.
        const Timecode::DigitType expectedFrames[3] = {0, 1, 2};
        for (uint8_t i = 0; i < 3; ++i) {
                auto res = t.applySyncPolicy(built.first().front(), FrameSyncDisposition::repeat(3), i);
                REQUIRE(res.second().isOk());
                REQUIRE(res.first().size() == 1);
                AncTranslator::ParseResult parsed = parseVia(res.first().front());
                REQUIRE(parsed.second().isOk());
                Timecode out = atcTimecode(parsed.first());
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
        AncTranslator::PacketsResult built = buildVia(src, AncFormat::AtcLtc);
        REQUIRE(built.second().isOk());
        AncTranslateConfig tcfg;
        tcfg.set(AncTranslateConfig::AtcParseRateHint, uint32_t(30));
        AncTranslator t(tcfg);

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
                AncTranslator::ParseResult parsed = parseVia(res.first().front());
                REQUIRE(parsed.second().isOk());
                Timecode out = atcTimecode(parsed.first());
                CHECK(out.hour() == 0);
                CHECK(out.min() == expected[i].m);
                CHECK(out.sec() == expected[i].s);
                CHECK(out.frame() == expected[i].f);
                CHECK(out.isDropFrame());
        }
}

// ===========================================================================
// SMPTE ST 12-2:2014 reference-packet tests.  These exercise the actual wire
// bytes the codec emits against hand-computed expected bytes derived from
// the spec — round-trips with the codec's own output cannot catch bit-
// position bugs because both sides share the same code.
//
// Wire format (10-bit UDW, parity stripped — i.e. udw[i] & 0xFF):
//   bits 0-2  zero per §6.1.4
//   bit  3    DBB (UDW 1-8 b3 = DBB1; UDW 9-16 b3 = DBB2, here zero)
//   bits 4-7  time-code nibble (b4 = LSB)
//
// Table 6 (1-indexed UDW):
//   UDW 1  frame units            UDW 2  binary group 1
//   UDW 3  frame tens|DF|CF       UDW 4  binary group 2
//   UDW 5  sec units              UDW 6  binary group 3
//   UDW 7  sec tens|polarity      UDW 8  binary group 4
//   UDW 9  min units              UDW 10 binary group 5
//   UDW 11 min tens|BGF0          UDW 12 binary group 6
//   UDW 13 hour units             UDW 14 binary group 7
//   UDW 15 hour tens|BGF1|BGF2    UDW 16 binary group 8
// ===========================================================================

namespace {

        // Pull the 8-bit data byte out of a 10-bit UDW (parity in b8/b9).
        uint8_t udwByte(uint16_t w) { return static_cast<uint8_t>(w & 0xFFu); }

        // Verify a built packet against the canonical ATC framing: DID=0x60,
        // SDID per format, DC=0x10, the expected 16 data bytes, and a valid
        // checksum.  Returned for use as a sub-check in each reference test.
        void checkAtcReferencePacket(const AncPacket &pkt, uint8_t expectedSdid,
                                     const uint8_t (&expected)[16]) {
                Result<St291Packet> rp = St291Packet::from(pkt);
                REQUIRE(rp.second().isOk());
                const St291Packet &p = rp.first();
                CHECK(p.did() == 0x60);
                CHECK(p.sdid() == expectedSdid);
                CHECK(p.dataCount() == 0x10);
                CHECK(p.checksumValid());
                List<uint16_t> wire = p.udw();
                REQUIRE(wire.size() == 16);
                for (size_t i = 0; i < 16; ++i) {
                        INFO("UDW ", (i + 1));
                        CHECK(udwByte(wire[i]) == expected[i]);
                        // Bits 0-2 are "shall be zero" per §6.1.4 — verify
                        // explicitly so a future bug that writes into the
                        // reserved low bits is caught even if the
                        // higher-nibble assertion happens to match.
                        CHECK((udwByte(wire[i]) & 0x07) == 0);
                }
        }

} // namespace

TEST_CASE("ATC reference: AtcLtc 01:23:45:14 NDF30 matches ST 12-2 Table 6 byte-for-byte") {
        Timecode src(Timecode::Mode(Timecode::NDF30), 1, 23, 45, 14);
        AncTranslator::PacketsResult built = buildVia(src, AncFormat::AtcLtc);
        REQUIRE(built.second().isOk());

        // Hand-computed per Table 6.  DBB1=0x00 (LTC) → all UDW 1-8 b3=0;
        // DBB2=0x00 (HD digital) → all UDW 9-16 b3=0.
        //                       frame=14    sec=45    min=23    hour=01
        const uint8_t expected[16] = {
                0x40, 0x00,  // UDW 1-2:   frame units 4              | BG1=0
                0x10, 0x00,  // UDW 3-4:   frame tens 1 (DF=0,CF=0)   | BG2=0
                0x50, 0x00,  // UDW 5-6:   sec units 5                | BG3=0
                0x40, 0x00,  // UDW 7-8:   sec tens 4 (polarity=0)    | BG4=0
                0x30, 0x00,  // UDW 9-10:  min units 3                | BG5=0
                0x20, 0x00,  // UDW 11-12: min tens 2 (BGF0=0)        | BG6=0
                0x10, 0x00,  // UDW 13-14: hour units 1               | BG7=0
                0x00, 0x00   // UDW 15-16: hour tens 0 (BGF1=BGF2=0)  | BG8=0
        };
        checkAtcReferencePacket(built.first().front(), 0x60, expected);
}

TEST_CASE("ATC reference: AtcLtc 01:00:00:02 DF30 sets DF bit at UDW 3 bit 6") {
        Timecode src(Timecode::Mode(Timecode::DF30), 1, 0, 0, 2);
        AncTranslator::PacketsResult built = buildVia(src, AncFormat::AtcLtc);
        REQUIRE(built.second().isOk());

        // Frame tens 0 + DF=1 → bits 4-7 of UDW 3 = 0b0100 → byte 0x40.
        const uint8_t expected[16] = {
                0x20, 0x00,  // UDW 1-2:   frame units 2              | BG1=0
                0x40, 0x00,  // UDW 3-4:   frame tens 0 + DF=1        | BG2=0
                0x00, 0x00,  // UDW 5-6:   sec units 0                | BG3=0
                0x00, 0x00,  // UDW 7-8:   sec tens 0                 | BG4=0
                0x00, 0x00,  // UDW 9-10:  min units 0                | BG5=0
                0x00, 0x00,  // UDW 11-12: min tens 0                 | BG6=0
                0x10, 0x00,  // UDW 13-14: hour units 1               | BG7=0
                0x00, 0x00   // UDW 15-16: hour tens 0                | BG8=0
        };
        checkAtcReferencePacket(built.first().front(), 0x60, expected);
}

TEST_CASE("ATC reference: AtcVitc1 00:00:00:00 sets DBB1 bit 0 (UDW 1 b3 = 1)") {
        Timecode                src(Timecode::Mode(Timecode::NDF30), 0, 0, 0, 0);
        AncTranslator::PacketsResult built = buildVia(src, AncFormat::AtcVitc1);
        REQUIRE(built.second().isOk());

        // DBB1=0x01 (VITC1) → UDW 1 b3 = 1, all other DBB bits zero.
        // SDID=0x60 per ST 12-2:2014 §5 (every ATC flavour shares
        // DID=0x60/SDID=0x60; DBB1 discriminates).
        const uint8_t expected[16] = {
                0x08, 0x00,  // UDW 1: DBB1 LSB=1, nibble=0           | UDW 2 BG1=0
                0x00, 0x00,  // UDW 3: nibble=0, DBB1 bit 2=0         | UDW 4 BG2=0
                0x00, 0x00,  //
                0x00, 0x00,  //
                0x00, 0x00,  //
                0x00, 0x00,  //
                0x00, 0x00,  //
                0x00, 0x00   //
        };
        checkAtcReferencePacket(built.first().front(), 0x60, expected);
}

TEST_CASE("ATC reference: AtcVitc2 23:59:59:29 NDF30 sets DBB1 bit 1 (UDW 2 b3 = 1)") {
        Timecode                src(Timecode::Mode(Timecode::NDF30), 23, 59, 59, 29);
        AncTranslator::PacketsResult built = buildVia(src, AncFormat::AtcVitc2);
        REQUIRE(built.second().isOk());

        // DBB1=0x02 (VITC2) → UDW 2 b3 = 1, others zero.  SDID=0x60
        // per ST 12-2:2014 §5 (shared with LTC + VITC1).
        //                       frame=29    sec=59    min=59    hour=23
        const uint8_t expected[16] = {
                0x90, 0x08,  // UDW 1-2:   frame units 9              | BG1 + DBB1 bit 1=1
                0x20, 0x00,  // UDW 3-4:   frame tens 2               | BG2=0
                0x90, 0x00,  // UDW 5-6:   sec units 9                | BG3=0
                0x50, 0x00,  // UDW 7-8:   sec tens 5                 | BG4=0
                0x90, 0x00,  // UDW 9-10:  min units 9                | BG5=0
                0x50, 0x00,  // UDW 11-12: min tens 5                 | BG6=0
                0x30, 0x00,  // UDW 13-14: hour units 3               | BG7=0
                0x20, 0x00   // UDW 15-16: hour tens 2                | BG8=0
        };
        checkAtcReferencePacket(built.first().front(), 0x60, expected);
}

TEST_CASE("ATC reference: parser decodes a hand-built ST 12-2 packet (12:34:56:21 DF30)") {
        // Construct the 16 UDW data bytes directly per the spec, then feed
        // them through the parser.  Failing this proves the parser is the
        // wrong side of the build/parse pair (the build tests above could
        // pass while the parser still got the bits wrong).
        //                       frame=21 → units=1, tens=2 + DF=1
        //                       sec=56  → units=6, tens=5
        //                       min=34  → units=4, tens=3
        //                       hour=12 → units=2, tens=1
        List<uint16_t> udw;
        udw.resize(16);
        udw[0]  = 0x10;  // UDW 1   frame units 1
        udw[1]  = 0x00;  // UDW 2   BG1
        udw[2]  = 0x60;  // UDW 3   frame tens 2 + DF=1 → 0b0110 << 4
        udw[3]  = 0x00;  // UDW 4   BG2
        udw[4]  = 0x60;  // UDW 5   sec units 6
        udw[5]  = 0x00;  // UDW 6   BG3
        udw[6]  = 0x50;  // UDW 7   sec tens 5
        udw[7]  = 0x00;  // UDW 8   BG4
        udw[8]  = 0x40;  // UDW 9   min units 4
        udw[9]  = 0x00;  // UDW 10  BG5
        udw[10] = 0x30;  // UDW 11  min tens 3
        udw[11] = 0x00;  // UDW 12  BG6
        udw[12] = 0x20;  // UDW 13  hour units 2
        udw[13] = 0x00;  // UDW 14  BG7
        udw[14] = 0x10;  // UDW 15  hour tens 1
        udw[15] = 0x00;  // UDW 16  BG8

        St291Packet     p      = St291Packet::build(AncFormat(AncFormat::AtcLtc), udw, 10);
        AncTranslator::ParseResult parsed = parseVia(p.packet());
        REQUIRE(parsed.second().isOk());
        Timecode out = atcTimecode(parsed.first());
        CHECK(out.hour() == 12);
        CHECK(out.min() == 34);
        CHECK(out.sec() == 56);
        CHECK(out.frame() == 21);
        CHECK(out.isDropFrame());
}

TEST_CASE("ATC reference: parser tolerates non-zero binary-group nibbles (BGs are user data)") {
        // ST 12-1 §9.2.2: binary-group nibbles are user data the codec
        // does not interpret.  Stuff them with garbage and verify the
        // time address still parses correctly.
        List<uint16_t> udw;
        udw.resize(16);
        udw[0]  = 0x40;  // UDW 1   frame units 4
        udw[1]  = 0xA0;  // UDW 2   BG1 = 0xA (garbage)
        udw[2]  = 0x10;  // UDW 3   frame tens 1
        udw[3]  = 0xF0;  // UDW 4   BG2 = 0xF
        udw[4]  = 0x50;  // UDW 5   sec units 5
        udw[5]  = 0x50;  // UDW 6   BG3 = 0x5
        udw[6]  = 0x40;  // UDW 7   sec tens 4
        udw[7]  = 0xC0;  // UDW 8   BG4
        udw[8]  = 0x30;  // UDW 9   min units 3
        udw[9]  = 0x70;  // UDW 10  BG5
        udw[10] = 0x20;  // UDW 11  min tens 2
        udw[11] = 0x10;  // UDW 12  BG6
        udw[12] = 0x10;  // UDW 13  hour units 1
        udw[13] = 0x80;  // UDW 14  BG7
        udw[14] = 0x00;  // UDW 15  hour tens 0
        udw[15] = 0xE0;  // UDW 16  BG8

        St291Packet     p      = St291Packet::build(AncFormat(AncFormat::AtcLtc), udw, 9);
        AncTranslator::ParseResult parsed = parseVia(p.packet());
        REQUIRE(parsed.second().isOk());
        Timecode out = atcTimecode(parsed.first());
        CHECK(out.hour() == 1);
        CHECK(out.min() == 23);
        CHECK(out.sec() == 45);
        CHECK(out.frame() == 14);
        CHECK_FALSE(out.isDropFrame());
}

TEST_CASE("ATC reference: parser ignores BGF / polarity / CF flag bits in the time-code UDWs") {
        // ST 12-1 §9.2.2: "Unused flag bits ... shall be ignored by
        // receiving equipment."  Stuff every flag bit position with 1
        // and verify the time address still decodes cleanly.
        List<uint16_t> udw;
        udw.resize(16);
        // 00:00:00:00 with every flag bit set:
        //   UDW 3 b7=CF, b6=DF — set both
        //   UDW 7 b7=polarity / 25Hz BGF0 — set
        //   UDW 11 b7=BGF0 / 25Hz BGF2 — set
        //   UDW 15 b6=BGF1, b7=BGF2 — set both
        udw[0]  = 0x00;
        udw[1]  = 0x00;
        udw[2]  = 0xC0;  // CF + DF set, nibble (frame tens) = 0
        udw[3]  = 0x00;
        udw[4]  = 0x00;
        udw[5]  = 0x00;
        udw[6]  = 0x80;  // polarity set, nibble (sec tens) = 0
        udw[7]  = 0x00;
        udw[8]  = 0x00;
        udw[9]  = 0x00;
        udw[10] = 0x80;  // BGF0 set, nibble (min tens) = 0
        udw[11] = 0x00;
        udw[12] = 0x00;
        udw[13] = 0x00;
        udw[14] = 0xC0;  // BGF1+BGF2 set, nibble (hour tens) = 0
        udw[15] = 0x00;

        St291Packet     p      = St291Packet::build(AncFormat(AncFormat::AtcLtc), udw, 9);
        AncTranslator::ParseResult parsed = parseVia(p.packet());
        REQUIRE(parsed.second().isOk());
        Timecode out = atcTimecode(parsed.first());
        CHECK(out.hour() == 0);
        CHECK(out.min() == 0);
        CHECK(out.sec() == 0);
        CHECK(out.frame() == 0);
        // DF is the one flag we *do* surface — and it's set on the wire.
        CHECK(out.isDropFrame());
}

TEST_CASE("ATC parse hint: default (no hint) yields NDF30 / DF30 by the DF wire bit") {
        // No hint set — historical behaviour: non-DF wire → NDF30,
        // DF wire → DF30.
        {
                Timecode src(Timecode::Mode(Timecode::NDF30), 1, 2, 3, 4);
                AncTranslator::PacketsResult built = buildVia(src, AncFormat::AtcLtc);
                REQUIRE(built.second().isOk());
                AncTranslator::ParseResult parsed = parseVia(built.first().front());
                REQUIRE(parsed.second().isOk());
                Timecode out = atcTimecode(parsed.first());
                CHECK_FALSE(out.isDropFrame());
        }
        {
                Timecode src(Timecode::Mode(Timecode::DF30), 1, 2, 3, 4);
                AncTranslator::PacketsResult built = buildVia(src, AncFormat::AtcLtc);
                REQUIRE(built.second().isOk());
                AncTranslator::ParseResult parsed = parseVia(built.first().front());
                REQUIRE(parsed.second().isOk());
                Timecode out = atcTimecode(parsed.first());
                CHECK(out.isDropFrame());
        }
}

TEST_CASE("ATC parse hint: AtcParseRateHint=25 stamps NDF25 on the result") {
        Timecode src(Timecode::Mode(Timecode::NDF25), 5, 10, 15, 20);
        AncTranslator::PacketsResult built = buildVia(src, AncFormat::AtcLtc);
        REQUIRE(built.second().isOk());

        AncTranslateConfig cfg;
        cfg.set(AncTranslateConfig::AtcParseRateHint, uint32_t(25));
        AncTranslator::ParseResult parsed = parseVia(built.first().front(), cfg);
        REQUIRE(parsed.second().isOk());
        Timecode out = atcTimecode(parsed.first());
        CHECK(out.hour() == 5);
        CHECK(out.min() == 10);
        CHECK(out.sec() == 15);
        CHECK(out.frame() == 20);
        CHECK_FALSE(out.isDropFrame());
        // The mode actually got stamped — verify by formatting:
        // NDF25 toString uses ':' (NDF separator) and round-trips
        // through the libvtc 25-fps format.
        CHECK(out.toString() == String("05:10:15:20"));
}

TEST_CASE("ATC parse hint: AtcParseRateHint=24 stamps NDF24 on the result") {
        Timecode src(Timecode::Mode(Timecode::NDF24), 2, 0, 0, 23);
        AncTranslator::PacketsResult built = buildVia(src, AncFormat::AtcLtc);
        REQUIRE(built.second().isOk());

        AncTranslateConfig cfg;
        cfg.set(AncTranslateConfig::AtcParseRateHint, uint32_t(24));
        AncTranslator::ParseResult parsed = parseVia(built.first().front(), cfg);
        REQUIRE(parsed.second().isOk());
        Timecode out = atcTimecode(parsed.first());
        CHECK(out.hour() == 2);
        CHECK(out.frame() == 23);
        CHECK_FALSE(out.isDropFrame());
        CHECK(out.toString() == String("02:00:00:23"));
}

TEST_CASE("ATC parse hint: AtcParseRateHint=30 + wire DF bit yields DF30") {
        Timecode src(Timecode::Mode(Timecode::DF30), 1, 0, 0, 2);
        AncTranslator::PacketsResult built = buildVia(src, AncFormat::AtcLtc);
        REQUIRE(built.second().isOk());

        AncTranslateConfig cfg;
        cfg.set(AncTranslateConfig::AtcParseRateHint, uint32_t(30));
        AncTranslator::ParseResult parsed = parseVia(built.first().front(), cfg);
        REQUIRE(parsed.second().isOk());
        Timecode out = atcTimecode(parsed.first());
        CHECK(out.isDropFrame());
}

TEST_CASE("ATC parse hint: non-standard rate (e.g. 99 fps) yields a custom-format Timecode") {
        // Post-F5 D1b: an explicit non-standard hint is taken at face
        // value via vtc_format_find_or_create rather than silently
        // demoted to NDF30; the digits round-trip correctly with the
        // caller's unusual rate stamped onto the result.
        Timecode src(Timecode::Mode(Timecode::NDF30), 1, 0, 0, 0);
        AncTranslator::PacketsResult built = buildVia(src, AncFormat::AtcLtc);
        REQUIRE(built.second().isOk());

        AncTranslateConfig cfg;
        cfg.set(AncTranslateConfig::AtcParseRateHint, uint32_t(99));
        AncTranslator::ParseResult parsed = parseVia(built.first().front(), cfg);
        REQUIRE(parsed.second().isOk());
        Timecode out = atcTimecode(parsed.first());
        CHECK(out.hour() == 1);
        CHECK(out.fps() == 99u);
        CHECK_FALSE(out.isDropFrame());
}

TEST_CASE("ATC reference: every built packet has DC=0x10 and 16 UDWs") {
        // Spot-check across all three formats: every codec output must
        // carry the spec-mandated DC=0x10.
        const AncFormat::ID ids[] = {AncFormat::AtcLtc, AncFormat::AtcVitc1, AncFormat::AtcVitc2};
        for (auto id : ids) {
                Timecode                src(Timecode::Mode(Timecode::NDF30), 4, 5, 6, 7);
                AncTranslator::PacketsResult built = buildVia(src, id);
                REQUIRE(built.second().isOk());
                Result<St291Packet> rp = St291Packet::from(built.first().front());
                REQUIRE(rp.second().isOk());
                CHECK(rp.first().dataCount() == 0x10);
                CHECK(rp.first().udw().size() == 16);
                CHECK(rp.first().checksumValid());
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
        AncTranslator::PacketsResult built = buildVia(src, AncFormat::AtcLtc, srcCfg);
        REQUIRE(built.second().isOk());

        // Sync policy parses + re-encodes on Repeat[idx>0], so it
        // needs a rate hint just like a plain parse does.
        AncTranslateConfig tcfg;
        tcfg.set(AncTranslateConfig::AtcParseRateHint, uint32_t(30));
        AncTranslator t(tcfg);
        auto          res = t.applySyncPolicy(built.first().front(), FrameSyncDisposition::repeat(2), 1);
        REQUIRE(res.second().isOk());
        REQUIRE(res.first().size() == 1);
        Result<St291Packet> rp = St291Packet::from(res.first().front());
        REQUIRE(rp.second().isOk());
        // Source-packet framing is preserved across the re-encode.
        CHECK(rp.first().line() == 11);
        CHECK(rp.first().fieldB() == true);
}

// ============================================================================
// F5 — AncAtc value type, user bits / flags / DBB2 round-trip, ST 12-3 HFR
// ============================================================================

namespace {

        AncTranslator::PacketsResult buildAtcVia(const AncAtc &atc, AncFormat::ID id,
                                            const AncTranslateConfig &cfg = {}) {
                AncTranslator t(cfg);
                return t.build(Variant(atc), AncFormat(id), AncTransport::St291);
        }

} // namespace

TEST_CASE("AncAtc F5 — round-trip preserves user bits across all eight nibbles") {
        // Phase 4: user bits ride on the Timecode now.  Synthesize a pattern
        // with every nibble distinct so a mis-routed slot would corrupt
        // the test.
        TimecodeUserbits::Nibbles ub = {0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7};
        Timecode tc(Timecode::Mode(Timecode::NDF30), 1, 2, 3, 4);
        tc.setUserbits(TimecodeUserbits::fromNibbles(ub, TimecodeUserbits::Unspecified));
        AncAtc src(tc);

        AncTranslator::PacketsResult built = buildAtcVia(src, AncFormat::AtcLtc);
        REQUIRE(built.second().isOk());
        AncTranslator::ParseResult parsed = parseVia(built.first().front());
        REQUIRE(parsed.second().isOk());
        AncAtc out = parsed.first().get<AncAtc>();
        for (size_t i = 0; i < TimecodeUserbits::NibbleCount; ++i) {
                CHECK(out.timecode().userbits().nibbles()[i] == ub[i]);
        }
        CHECK(out.timecode().hour() == 1);
        CHECK(out.timecode().frame() == 4);
}

TEST_CASE("AncAtc F5 — round-trip preserves user bits with high nibble values") {
        // The codec masks each user-bit nibble to its low 4 bits; verify
        // by feeding it bytes whose high bits would leak if the mask
        // wasn't applied.
        TimecodeUserbits::Nibbles ub = {0x8, 0x9, 0xA, 0xB, 0xC, 0xD, 0xE, 0xF};
        Timecode tc(Timecode::Mode(Timecode::NDF30), 0, 0, 0, 0);
        tc.setUserbits(TimecodeUserbits::fromNibbles(ub, TimecodeUserbits::Unspecified));
        AncAtc src(tc);

        AncTranslator::PacketsResult built = buildAtcVia(src, AncFormat::AtcLtc);
        REQUIRE(built.second().isOk());
        AncTranslator::ParseResult parsed = parseVia(built.first().front());
        REQUIRE(parsed.second().isOk());
        AncAtc out = parsed.first().get<AncAtc>();
        for (size_t i = 0; i < TimecodeUserbits::NibbleCount; ++i) {
                CHECK(out.timecode().userbits().nibbles()[i] == ub[i]);
        }
}

TEST_CASE("AncAtc Phase 4 — color-frame and BGF mode round-trip via Timecode") {
        // Color frame + the eight BGF mode combinations exercise the four
        // flag-bit slots that survived the Phase 4 cull (color-frame at
        // UDW 3 b7, BGF0/1/2 at UDW 11 b7 / 15 b6 / 15 b7).  The polarity
        // slot is intentionally not surfaced post-Phase 4.
        for (uint8_t mode = 0; mode < 8; ++mode) {
                for (int cf = 0; cf < 2; ++cf) {
                        Timecode tc(Timecode::Mode(Timecode::NDF30), 1, 2, 3, 4);
                        tc.setColorFrame(cf != 0);
                        tc.setUserbits(TimecodeUserbits::fromRawBits(
                                0u, static_cast<TimecodeUserbits::Mode>(mode)));
                        AncAtc src(tc);
                        AncTranslator::PacketsResult built = buildAtcVia(src, AncFormat::AtcLtc);
                        REQUIRE(built.second().isOk());
                        AncTranslator::ParseResult parsed = parseVia(built.first().front());
                        REQUIRE(parsed.second().isOk());
                        AncAtc out = parsed.first().get<AncAtc>();
                        CAPTURE(mode);
                        CAPTURE(cf);
                        CHECK(out.timecode().colorFrame() == (cf != 0));
                        CHECK(static_cast<int>(out.timecode().userbits().mode()) == mode);
                }
        }
}

TEST_CASE("AncAtc P2-1 — DBB1 payload type round-trips through UDW 1..8 b3 LSB-first") {
        // ST 12-2:2014 §6.2.1: DBB1 is UDW 1..8 b3, LSB-first.  The
        // codec must round-trip the byte byte-exact so a captured
        // VITC2 packet re-emits as VITC2 even though the captured
        // AncPacket's @c format() resolves to AtcLtc (lowest-ID match
        // for the shared (0x60,0x60) slot).
        for (uint8_t dbb1 : {AncAtc::Ltc, AncAtc::Vitc1, AncAtc::Vitc2}) {
                AncFormat::ID fmtId = dbb1 == AncAtc::Vitc1 ? AncFormat::AtcVitc1
                                    : dbb1 == AncAtc::Vitc2 ? AncFormat::AtcVitc2
                                                            : AncFormat::AtcLtc;
                Timecode tc(Timecode::Mode(Timecode::NDF30), 1, 2, 3, 4);
                AncTranslator::PacketsResult built = buildVia(tc, fmtId);
                REQUIRE(built.second().isOk());
                AncTranslator::ParseResult parsed = parseVia(built.first().front());
                REQUIRE(parsed.second().isOk());
                CHECK(parsed.first().get<AncAtc>().payloadType() == dbb1);
        }
}

TEST_CASE("AncAtc F5 — DBB2 byte round-trips through UDW 9..16 b3 LSB-first") {
        // Bit-sweep DBB2 to confirm each bit lands in the correct UDW.
        for (unsigned bit = 0; bit < 8; ++bit) {
                AncAtc src(Timecode(Timecode::Mode(Timecode::NDF30), 0, 0, 0, 0));
                src.setDbb2(static_cast<uint8_t>(1u << bit));
                AncTranslator::PacketsResult built = buildAtcVia(src, AncFormat::AtcLtc);
                REQUIRE(built.second().isOk());
                AncTranslator::ParseResult parsed = parseVia(built.first().front());
                REQUIRE(parsed.second().isOk());
                AncAtc out = parsed.first().get<AncAtc>();
                CHECK(out.dbb2() == static_cast<uint8_t>(1u << bit));
        }
}

TEST_CASE("AncAtc F5 — Timecode-only Variant build promotes to default AncAtc") {
        // Pre-F5 callers passed Variant(Timecode); the codec should
        // still accept that shape and produce a packet whose parse
        // returns an AncAtc with the requested timecode and zero
        // user-bits / colorFrame / DBB2.
        Timecode tc(Timecode::Mode(Timecode::NDF30), 7, 7, 7, 7);
        AncTranslator t;
        AncTranslator::PacketsResult built =
                t.build(Variant(tc), AncFormat(AncFormat::AtcLtc), AncTransport::St291);
        REQUIRE(built.second().isOk());
        AncTranslator::ParseResult parsed = parseVia(built.first().front());
        REQUIRE(parsed.second().isOk());
        AncAtc out = parsed.first().get<AncAtc>();
        CHECK(out.timecode().hour() == 7);
        CHECK(out.timecode().frame() == 7);
        CHECK_FALSE(out.timecode().colorFrame());
        CHECK(out.timecode().userbits().toUint32() == 0u);
        CHECK(out.timecode().userbits().mode() == TimecodeUserbits::Unspecified);
        CHECK(out.dbb2() == 0);
}

TEST_CASE("AncAtc F5 D1b — parse without rate context returns InsufficientContext") {
        // Build a normal packet, then parse it with no rate hint on
        // the meta and no AtcParseRateHint on the cfg.  Per audit Q4
        // the parser must fail rather than silently default to 30 fps.
        Timecode tc(Timecode::Mode(Timecode::NDF30), 1, 0, 0, 0);
        AncTranslator::PacketsResult built = buildVia(tc, AncFormat::AtcLtc);
        REQUIRE(built.second().isOk());
        AncTranslator t;  // default cfg: no AtcParseRateHint set.
        AncTranslator::ParseResult parsed = t.parse(built.first().front());
        REQUIRE(parsed.second().isError());
        CHECK(parsed.second().code() == Error::InsufficientContext);
}

TEST_CASE("AncAtc F5 D1b — AncMeta::Atc::Rate on the packet outweighs cfg") {
        Timecode tc(Timecode::Mode(Timecode::NDF25), 5, 10, 15, 20);
        AncTranslator::PacketsResult built = buildVia(tc, AncFormat::AtcLtc);
        REQUIRE(built.second().isOk());

        // Stamp the meta key with 25 fps; cfg says 24 fps.  Meta wins.
        AncPacket pkt = built.first().front();
        Metadata  m   = pkt.meta();
        m.set(AncMeta::Atc::Rate, uint32_t(25));
        AncPacket stamped(pkt.format(), pkt.transport(), pkt.data(), m);

        AncTranslateConfig cfg;
        cfg.set(AncTranslateConfig::AtcParseRateHint, uint32_t(24));
        AncTranslator t(cfg);
        AncTranslator::ParseResult parsed = t.parse(stamped);
        REQUIRE(parsed.second().isOk());
        AncAtc out = parsed.first().get<AncAtc>();
        CHECK(out.timecode().fps() == 25u);
}

TEST_CASE("AncAtc F5 D1f — atcVitcFormatForFrame picks the right ID per ST 12-3 §6") {
        // ≤30 fps: every frame uses AtcVitc1.
        CHECK(AncAtc::atcVitcFormatForFrame(24, 0) == AncFormat::AtcVitc1);
        CHECK(AncAtc::atcVitcFormatForFrame(24, 1) == AncFormat::AtcVitc1);
        CHECK(AncAtc::atcVitcFormatForFrame(25, 5) == AncFormat::AtcVitc1);
        CHECK(AncAtc::atcVitcFormatForFrame(30, 100) == AncFormat::AtcVitc1);
        // HFR (>30 fps): alternate VITC1 / VITC2 per physical frame.
        CHECK(AncAtc::atcVitcFormatForFrame(50, 0) == AncFormat::AtcVitc1);
        CHECK(AncAtc::atcVitcFormatForFrame(50, 1) == AncFormat::AtcVitc2);
        CHECK(AncAtc::atcVitcFormatForFrame(60, 0) == AncFormat::AtcVitc1);
        CHECK(AncAtc::atcVitcFormatForFrame(60, 1) == AncFormat::AtcVitc2);
        CHECK(AncAtc::atcVitcFormatForFrame(60, 2) == AncFormat::AtcVitc1);
        CHECK(AncAtc::atcVitcFormatForFrame(60, 3) == AncFormat::AtcVitc2);
        CHECK(AncAtc::atcVitcFormatForFrame(120, 99) == AncFormat::AtcVitc2);
        CHECK(AncAtc::atcVitcFormatForFrame(120, 100) == AncFormat::AtcVitc1);
}

TEST_CASE("AncAtc F5 — DataStream round-trip preserves every field") {
        // Phase 4: color-frame + BGF mode + userbits live on the embedded
        // Timecode now.  This test verifies the v2 wire body carries them
        // (the v1 → v2 forward-compat reader is exercised separately by
        // the @c AncAtc::readFromStream<1> path).
        Timecode tc(Timecode::Mode(Timecode::NDF30), 1, 2, 3, 4);
        tc.setColorFrame(true);
        tc.setUserbits(TimecodeUserbits::fromNibbles(
                TimecodeUserbits::Nibbles{0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8},
                TimecodeUserbits::ClockTime));
        AncAtc src(tc);
        src.setPayloadType(AncAtc::Vitc2);
        src.setDbb2(0x42);

        Variant v(src);
        Variant restored;
        Buffer         buf(256);
        BufferIODevice dev(&buf);
        dev.open(IODevice::ReadWrite);
        {
                DataStream w = DataStream::createWriter(&dev);
                w << v;
        }
        dev.seek(0);
        {
                DataStream r = DataStream::createReader(&dev);
                r >> restored;
        }
        REQUIRE(restored.type() == DataTypeAncAtc);
        AncAtc out = restored.get<AncAtc>();
        CHECK(out == src);
}

TEST_CASE("AncAtc Phase 4 — DBB2 helpers round-trip ST 12-2 and ST 12-3 layouts") {
        // ST 12-2 VITC DBB2: line + duplicate + validity + processed.
        for (uint8_t line = 0; line < 32; ++line) {
                for (int dup = 0; dup < 2; ++dup) {
                        for (int valid = 0; valid < 2; ++valid) {
                                for (int processed = 0; processed < 2; ++processed) {
                                        uint8_t b = AncAtc::dbb2EncodeVitc(line, dup != 0,
                                                                            valid != 0,
                                                                            processed != 0);
                                        auto d = AncAtc::dbb2DecodeVitc(b);
                                        CHECK(d.line == line);
                                        CHECK(d.duplicate == (dup != 0));
                                        CHECK(d.valid == (valid != 0));
                                        CHECK(d.processed == (processed != 0));
                                }
                        }
                }
        }

        // ST 12-3 HFRTC DBB2: super-frame count (0..2) + N (3..5).
        for (uint8_t sfCount = 0; sfCount <= 2; ++sfCount) {
                for (uint8_t n = 3; n <= 5; ++n) {
                        uint8_t b = AncAtc::dbb2EncodeHfrtc(sfCount, n);
                        auto d = AncAtc::dbb2DecodeHfrtc(b);
                        CHECK(d.superFrameCount == sfCount);
                        CHECK(d.n == n);
                }
        }
}

TEST_CASE("AncAtc Phase 4 — payloadType isHfrtcPayload + hfrtcBitstream") {
        AncAtc atc;
        atc.setPayloadType(AncAtc::Ltc);
        CHECK_FALSE(atc.isHfrtcPayload());
        CHECK(atc.hfrtcBitstream() == 0);

        atc.setPayloadType(AncAtc::Vitc1);
        CHECK_FALSE(atc.isHfrtcPayload());

        // Range 0x80..0x8F
        for (uint8_t i = 0; i <= 0x0F; ++i) {
                atc.setPayloadType(static_cast<uint8_t>(0x80u | i));
                CHECK(atc.isHfrtcPayload());
                CHECK(atc.hfrtcBitstream() == i);
        }

        atc.setPayloadType(0x90u);
        CHECK_FALSE(atc.isHfrtcPayload());
}

TEST_CASE("AncAtc Phase 4 — v1 DataStream wire body is read-and-promoted") {
        // Synthesize a v1 wire body by hand: the v1 layout was
        //   Timecode + 8 user-bit bytes + flags + payloadType + dbb2
        // where flags bit 0 = ColorFrame, bit 2 = BGF0, bit 3 = BGF1, bit 4 = BGF2.
        // The v2 reader should not see this body — we exercise the v1
        // reader through readFromStream<1> directly.
        Buffer         buf(256);
        BufferIODevice dev(&buf);
        dev.open(IODevice::ReadWrite);
        {
                DataStream w = DataStream::createWriter(&dev);
                Timecode tc(Timecode::Mode(Timecode::NDF30), 9, 8, 7, 6);
                w << tc;
                // Eight user-bit nibbles.
                w << uint8_t(0x1) << uint8_t(0x2) << uint8_t(0x3) << uint8_t(0x4);
                w << uint8_t(0x5) << uint8_t(0x6) << uint8_t(0x7) << uint8_t(0x8);
                // v1 flags byte: ColorFrame (0x01) + BGF1 (0x08) + BGF2 (0x10) = 0x19.
                w << uint8_t(0x19);
                w << uint8_t(AncAtc::Vitc2);
                w << uint8_t(0x42);
        }
        dev.seek(0);
        DataStream r = DataStream::createReader(&dev);
        auto rr = AncAtc::readFromStream<1>(r);
        REQUIRE(rr.second().isOk());
        AncAtc out = rr.first();
        // Color frame promoted onto Timecode.
        CHECK(out.timecode().colorFrame());
        // BGF1 + BGF2 (bits 0x08 + 0x10) → BGF mode bits 0b110 (mode 6 = DateTimeZoneClock).
        CHECK(static_cast<int>(out.timecode().userbits().mode()) == 6);
        // User-bit nibbles preserved.
        CHECK(out.timecode().userbits().nibbles()[0] == 0x1);
        CHECK(out.timecode().userbits().nibbles()[7] == 0x8);
        // PayloadType + DBB2 preserved.
        CHECK(out.payloadType() == AncAtc::Vitc2);
        CHECK(out.dbb2() == 0x42);
}

// ============================================================================
// Phase 5 — ST 12-1 §12 / ST 12-2 §9.2 Am1 pair-rate HFR carriage
// ============================================================================

TEST_CASE("ancAtcIsPairHfrRate / ancAtcIsHfrtcRate split per ST 12-2 vs ST 12-3") {
        // Pair-rate HFR (ST 12-2 Am1 §9.2): 48/50/60.
        CHECK(ancAtcIsPairHfrRate(48));
        CHECK(ancAtcIsPairHfrRate(50));
        CHECK(ancAtcIsPairHfrRate(60));   // covers both 60p and 59.94p (digit family)
        CHECK_FALSE(ancAtcIsPairHfrRate(24));
        CHECK_FALSE(ancAtcIsPairHfrRate(30));
        CHECK_FALSE(ancAtcIsPairHfrRate(72));
        CHECK_FALSE(ancAtcIsPairHfrRate(120));

        // HFRTC rates (ST 12-3): 72/96/100/120.
        CHECK(ancAtcIsHfrtcRate(72));
        CHECK(ancAtcIsHfrtcRate(96));
        CHECK(ancAtcIsHfrtcRate(100));
        CHECK(ancAtcIsHfrtcRate(120));    // covers both 120p and 119.88p
        CHECK_FALSE(ancAtcIsHfrtcRate(24));
        CHECK_FALSE(ancAtcIsHfrtcRate(48));
        CHECK_FALSE(ancAtcIsHfrtcRate(60));

        // Disjunction: ancAtcIsHfrRate covers both.
        CHECK(ancAtcIsHfrRate(50));
        CHECK(ancAtcIsHfrRate(100));
        CHECK_FALSE(ancAtcIsHfrRate(30));
}

TEST_CASE("ATC Phase 5 — NDF60 pair-rate physical-frame round-trip") {
        // At 60p the physical frame walks 0..59 (Phase 2: physical-frame
        // counting on Timecode).  The wire only has 2 bits for frame
        // tens (max 39 raw), so the codec must encode super-frame
        // index (= pair_index, 0..29) into the FF wire slots and the
        // field-mark bit (= sub-frame index parity) into the polarity
        // slot.  This verifies the round-trip across every physical
        // frame in one second.
        AncTranslateConfig cfg;
        cfg.set(AncTranslateConfig::AtcParseRateHint, uint32_t(60));
        for (uint32_t physFrame = 0; physFrame < 60; ++physFrame) {
                CAPTURE(physFrame);
                Timecode src(Timecode::Mode(Timecode::NDF60), 0, 0, 0,
                             static_cast<Timecode::DigitType>(physFrame));
                REQUIRE(src.frame() == physFrame);
                REQUIRE(src.superFrameIndex() == physFrame / 2u);
                REQUIRE(src.subFrameIndex() == physFrame % 2u);

                AncTranslator::PacketsResult built = buildVia(src, AncFormat::AtcLtc, cfg);
                REQUIRE(built.second().isOk());
                AncTranslator::ParseResult parsed = parseVia(built.first().front(), cfg);
                REQUIRE(parsed.second().isOk());
                Timecode out = atcTimecode(parsed.first());
                CHECK(out.fps() == 60u);
                CHECK(out.frame() == physFrame);
                CHECK_FALSE(out.isDropFrame());
        }
}

TEST_CASE("ATC Phase 5 — DF60 pair-rate physical-frame round-trip (59.94 DF)") {
        // 59.94 DF: digit family 60, drop-frame compensation per ST 12-3.
        // Pick a frame index well inside the second so DF rollover doesn't
        // confound the round-trip.
        AncTranslateConfig cfg;
        cfg.set(AncTranslateConfig::AtcParseRateHint, uint32_t(60));
        for (uint32_t physFrame : {0u, 1u, 2u, 3u, 30u, 31u, 58u, 59u}) {
                CAPTURE(physFrame);
                Timecode src(Timecode::Mode(Timecode::DF60), 1, 0, 0,
                             static_cast<Timecode::DigitType>(physFrame));
                AncTranslator::PacketsResult built = buildVia(src, AncFormat::AtcLtc, cfg);
                REQUIRE(built.second().isOk());
                AncTranslator::ParseResult parsed = parseVia(built.first().front(), cfg);
                REQUIRE(parsed.second().isOk());
                Timecode out = atcTimecode(parsed.first());
                CHECK(out.frame() == physFrame);
                CHECK(out.isDropFrame());
                CHECK(out.fps() == 60u);
        }
}

TEST_CASE("ATC Phase 5 — NDF50 pair-rate physical-frame round-trip") {
        AncTranslateConfig cfg;
        cfg.set(AncTranslateConfig::AtcParseRateHint, uint32_t(50));
        for (uint32_t physFrame : {0u, 1u, 24u, 25u, 48u, 49u}) {
                CAPTURE(physFrame);
                Timecode src(Timecode::Mode(Timecode::NDF50), 0, 0, 0,
                             static_cast<Timecode::DigitType>(physFrame));
                AncTranslator::PacketsResult built = buildVia(src, AncFormat::AtcLtc, cfg);
                REQUIRE(built.second().isOk());
                AncTranslator::ParseResult parsed = parseVia(built.first().front(), cfg);
                REQUIRE(parsed.second().isOk());
                Timecode out = atcTimecode(parsed.first());
                CHECK(out.frame() == physFrame);
                CHECK(out.fps() == 50u);
        }
}

TEST_CASE("ATC Phase 5 — NDF48 pair-rate physical-frame round-trip") {
        AncTranslateConfig cfg;
        cfg.set(AncTranslateConfig::AtcParseRateHint, uint32_t(48));
        for (uint32_t physFrame : {0u, 1u, 22u, 23u, 46u, 47u}) {
                CAPTURE(physFrame);
                Timecode src(Timecode::Mode(Timecode::NDF48), 0, 0, 0,
                             static_cast<Timecode::DigitType>(physFrame));
                AncTranslator::PacketsResult built = buildVia(src, AncFormat::AtcLtc, cfg);
                REQUIRE(built.second().isOk());
                AncTranslator::ParseResult parsed = parseVia(built.first().front(), cfg);
                REQUIRE(parsed.second().isOk());
                Timecode out = atcTimecode(parsed.first());
                CHECK(out.frame() == physFrame);
                CHECK(out.fps() == 48u);
        }
}

TEST_CASE("ATC Phase 5 — field-mark bit lands at UDW 7 b7 on the wire") {
        // Physical frame 1 at 60p: super_frame=0, sub_frame=1.
        // Wire encoding: frame_units=0, frame_tens=0, polarity bit set
        // = UDW 7 high nibble bit 3 → UDW 7 data byte = 0x80.
        AncTranslateConfig cfg;
        cfg.set(AncTranslateConfig::AtcParseRateHint, uint32_t(60));
        Timecode src(Timecode::Mode(Timecode::NDF60), 0, 0, 0, 1);
        AncTranslator::PacketsResult built = buildVia(src, AncFormat::AtcLtc, cfg);
        REQUIRE(built.second().isOk());
        Result<St291Packet> rp = St291Packet::from(built.first().front());
        REQUIRE(rp.second().isOk());
        List<uint16_t> wire = rp.first().udw();
        REQUIRE(wire.size() == 16);
        // UDW 1 frame_units = 0, UDW 3 frame_tens = 0 — pair_index=0.
        CHECK((wire[0] & 0xFFu) == 0x00);
        CHECK((wire[2] & 0xFFu) == 0x00);
        // UDW 7 polarity bit (b7) set → field-mark = 1.
        CHECK((wire[6] & 0x80u) == 0x80);

        // Physical frame 0 at 60p: super_frame=0, sub_frame=0 → field-mark cleared.
        Timecode srcZero(Timecode::Mode(Timecode::NDF60), 0, 0, 0, 0);
        AncTranslator::PacketsResult built0 = buildVia(srcZero, AncFormat::AtcLtc, cfg);
        REQUIRE(built0.second().isOk());
        Result<St291Packet> rp0 = St291Packet::from(built0.first().front());
        REQUIRE(rp0.second().isOk());
        List<uint16_t> wire0 = rp0.first().udw();
        CHECK((wire0[6] & 0x80u) == 0x00);
}

TEST_CASE("ATC Phase 5 — AtcVitcLegacyFieldMark forces field-mark to 0") {
        // Pre-Am1 receivers reject Am1-conformant streams; the
        // legacy-mark cfg lets the encoder downgrade for them.
        // Physical frame 3 at 60p would normally produce field-mark=1;
        // verify the cfg flag clears it.
        AncTranslateConfig cfg;
        cfg.set(AncTranslateConfig::AtcParseRateHint, uint32_t(60));
        cfg.set(AncTranslateConfig::AtcVitcLegacyFieldMark, true);
        Timecode src(Timecode::Mode(Timecode::NDF60), 0, 0, 0, 3);
        AncTranslator::PacketsResult built = buildVia(src, AncFormat::AtcLtc, cfg);
        REQUIRE(built.second().isOk());
        Result<St291Packet> rp = St291Packet::from(built.first().front());
        REQUIRE(rp.second().isOk());
        List<uint16_t> wire = rp.first().udw();
        // UDW 7 polarity bit must be clear.
        CHECK((wire[6] & 0x80u) == 0x00);
        // pair_index = 1 → frame units=1, tens=0.
        CHECK((wire[0] & 0xFFu) == 0x10);
        CHECK((wire[2] & 0xFFu) == 0x00);

        // The parser interprets a clear field-mark as sub_frame=0, so
        // the recovered physical frame is pair_index*2 = 2 (not 3).
        // This is a lossy downgrade; the test documents that the
        // receiver-side recovery follows the wire.
        AncTranslator::ParseResult parsed = parseVia(built.first().front(), cfg);
        REQUIRE(parsed.second().isOk());
        Timecode out = atcTimecode(parsed.first());
        CHECK(out.frame() == 2u);
}

TEST_CASE("ATC Phase 5 — atcVitcFormatForFrame retains alternation at pair-rates") {
        // 48/50/60 pair-rates → even frames are VITC1, odd frames are VITC2.
        CHECK(AncAtc::atcVitcFormatForFrame(48, 0) == AncFormat::AtcVitc1);
        CHECK(AncAtc::atcVitcFormatForFrame(48, 1) == AncFormat::AtcVitc2);
        CHECK(AncAtc::atcVitcFormatForFrame(48, 46) == AncFormat::AtcVitc1);
        CHECK(AncAtc::atcVitcFormatForFrame(48, 47) == AncFormat::AtcVitc2);
}

TEST_CASE("ATC Phase 5 — modeForRate stamps the right libvtc format for each pair-rate") {
        // The build-then-parse round-trip already exercises modeForRate
        // indirectly; verify the resulting Timecode reports the expected
        // VTC format properties so a future refactor that breaks the
        // mapping fails loudly here.
        struct Case {
                        uint32_t fps;
                        bool     df;
                        uint32_t expectedFps;
                        bool     expectedDf;
        };
        const Case cases[] = {
                {48, false, 48, false},
                {50, false, 50, false},
                {60, false, 60, false},
                {60, true, 60, true}, // 59.94 DF
        };
        for (const Case &c : cases) {
                CAPTURE(c.fps);
                CAPTURE(c.df);
                Timecode src(c.df ? Timecode::Mode(Timecode::DF60)
                                  : c.fps == 48 ? Timecode::Mode(Timecode::NDF48)
                                  : c.fps == 50 ? Timecode::Mode(Timecode::NDF50)
                                                : Timecode::Mode(Timecode::NDF60),
                             0, 0, 0, 0);
                AncTranslateConfig cfg;
                cfg.set(AncTranslateConfig::AtcParseRateHint, c.fps);
                AncTranslator::PacketsResult built = buildVia(src, AncFormat::AtcLtc, cfg);
                REQUIRE(built.second().isOk());
                AncTranslator::ParseResult parsed = parseVia(built.first().front(), cfg);
                REQUIRE(parsed.second().isOk());
                Timecode out = atcTimecode(parsed.first());
                CHECK(out.fps() == c.expectedFps);
                CHECK(out.isDropFrame() == c.expectedDf);
                CHECK(out.mode().framesPerSuperFrame() == 2u);
        }
}

TEST_CASE("ATC Phase 5 — pair-rate alternation drives VITC1/VITC2 builder selection") {
        // End-to-end check: pick the format per AncAtc::atcVitcFormatForFrame,
        // build the packet, and verify the resulting payloadType matches
        // and the round-trip recovers the original physical frame.
        AncTranslateConfig cfg;
        cfg.set(AncTranslateConfig::AtcParseRateHint, uint32_t(60));
        for (uint32_t physFrame : {0u, 1u, 2u, 3u}) {
                CAPTURE(physFrame);
                Timecode src(Timecode::Mode(Timecode::NDF60), 0, 0, 1,
                             static_cast<Timecode::DigitType>(physFrame));
                int fmtIdRaw = AncAtc::atcVitcFormatForFrame(60, physFrame);
                AncFormat::ID fmtId = static_cast<AncFormat::ID>(fmtIdRaw);

                AncTranslator::PacketsResult built = buildVia(src, fmtId, cfg);
                REQUIRE(built.second().isOk());
                AncTranslator::ParseResult parsed = parseVia(built.first().front(), cfg);
                REQUIRE(parsed.second().isOk());
                AncAtc out = parsed.first().get<AncAtc>();
                CHECK(out.timecode().frame() == physFrame);
                CHECK(out.payloadType() == (physFrame % 2u == 0u
                                                    ? AncAtc::Vitc1
                                                    : AncAtc::Vitc2));
        }
}

// ============================================================================
// Phase 7 — extended pair-rate frame-mark coverage at 48p / 50p / 59.94p
// ============================================================================
//
// Phase 5 covered the alternation rule at 60p; the test below extends
// that to the three remaining pair-rate variants and verifies a tight
// (VITC1, fm=0) → (VITC2, fm=1) wire shape across the full pair-index
// space for each rate.

TEST_CASE("ATC Phase 7 — pair-rate frame-mark round-trip at 48p / 50p / 59.94p") {
        struct Case {
                        const char            *name;
                        Timecode::TimecodeType type;
                        uint32_t               fps;
                        bool                   df;
        };
        const Case cases[] = {
                {"NDF48", Timecode::NDF48, 48, false},
                {"NDF50", Timecode::NDF50, 50, false},
                {"DF60 (59.94)", Timecode::DF60, 60, true},
        };
        for (const Case &c : cases) {
                CAPTURE(c.name);
                AncTranslateConfig cfg;
                cfg.set(AncTranslateConfig::AtcParseRateHint, c.fps);
                const uint32_t pairCount = c.fps / 2u;
                for (uint32_t pairIdx = 0; pairIdx < pairCount; ++pairIdx) {
                        CAPTURE(pairIdx);
                        // Build the two consecutive physical-frame packets
                        // making up one pair (VITC1 then VITC2).
                        for (uint32_t sub = 0; sub < 2; ++sub) {
                                CAPTURE(sub);
                                const uint32_t physFrame = pairIdx * 2u + sub;
                                Timecode src(Timecode::Mode(c.type), 0, 0, 0,
                                             static_cast<Timecode::DigitType>(physFrame));
                                AncFormat::ID fmtId = static_cast<AncFormat::ID>(
                                        AncAtc::atcVitcFormatForFrame(c.fps, physFrame));
                                CHECK(fmtId == (sub == 0 ? AncFormat::AtcVitc1
                                                         : AncFormat::AtcVitc2));

                                AncTranslator::PacketsResult built =
                                        buildVia(src, fmtId, cfg);
                                REQUIRE(built.second().isOk());
                                Result<St291Packet> rp = St291Packet::from(
                                        built.first().front());
                                REQUIRE(rp.second().isOk());
                                List<uint16_t> wire = rp.first().udw();
                                REQUIRE(wire.size() == 16);
                                // UDW 7 polarity bit (b7) encodes the
                                // field-mark — must equal sub-frame index.
                                const uint8_t fmBit = static_cast<uint8_t>(wire[6] & 0x80u);
                                CHECK(fmBit == (sub == 1 ? 0x80u : 0x00u));
                                // FF wire (units + tens) carries the
                                // pair_index, never the raw physical frame.
                                const uint8_t units = static_cast<uint8_t>(
                                        (wire[0] >> 4) & 0x0Fu);
                                const uint8_t tens = static_cast<uint8_t>(
                                        (wire[2] >> 4) & 0x03u);
                                CHECK(static_cast<uint32_t>(tens * 10u + units)
                                              == pairIdx);

                                // Parse back to a physical-frame Timecode.
                                AncTranslator::ParseResult parsed =
                                        parseVia(built.first().front(), cfg);
                                REQUIRE(parsed.second().isOk());
                                Timecode out = atcTimecode(parsed.first());
                                CHECK(out.frame() == physFrame);
                                CHECK(out.fps() == c.fps);
                                CHECK(out.isDropFrame() == c.df);
                        }
                }
        }
}

// ============================================================================
// Phase 7 — cartesian payload × rate parse / build round-trip
// ============================================================================
//
// Phase 5/6 covered each carriage path at its own rate set; the test
// below sweeps the full cartesian product of payload type × base /
// pair rate to catch any unintended interaction between the DBB1
// flavour byte and the rate-driven wire layout.  HFRTC is exercised
// separately by tests/unit/anccodec_atc_hfrtc.cpp (the dispatch
// rules require AtcHfrtc to flow through that codec, not the
// ST 12-2 codec under test here).
//
// Round-trip shape: build → parse → rebuild → byte-for-byte equality
// against the first build's wire UDWs.  Exercises both build paths
// (initial + sync-policy re-encode) and the parse path in one go.

namespace {

        struct CartesianRate {
                        const char            *name;
                        Timecode::TimecodeType type;
                        uint32_t               fps;
        };

        const CartesianRate cartesianRates[] = {
                {"NDF24", Timecode::NDF24, 24},
                {"NDF25", Timecode::NDF25, 25},
                {"NDF30", Timecode::NDF30, 30},
                {"DF30",  Timecode::DF30,  30},
                {"NDF48", Timecode::NDF48, 48},
                {"NDF50", Timecode::NDF50, 50},
                {"NDF60", Timecode::NDF60, 60},
                {"DF60",  Timecode::DF60,  60},
        };

        const AncFormat::ID cartesianPayloads[] = {
                AncFormat::AtcLtc,
                AncFormat::AtcVitc1,
                AncFormat::AtcVitc2,
        };

        // Pick a physical-frame index that's safely inside the rate's
        // frame range and (for pair-rate carriage) lands on a non-zero
        // pair_index + non-zero field_mark so both wire-encoded fields
        // get exercised.
        uint32_t cartesianPickFrame(uint32_t fps) {
                if (fps <= 30u) return 7u; // 0..29
                return 9u;                  // pair_index=4, field_mark=1 at all pair-rates
        }

} // namespace

TEST_CASE("ATC Phase 7 — cartesian payload x rate parse / build round-trip") {
        for (const CartesianRate &r : cartesianRates) {
                CAPTURE(r.name);
                for (AncFormat::ID payload : cartesianPayloads) {
                        CAPTURE(static_cast<int>(payload));
                        AncTranslateConfig cfg;
                        cfg.set(AncTranslateConfig::AtcParseRateHint, r.fps);
                        Timecode src(Timecode::Mode(r.type), 1, 2, 3,
                                     static_cast<Timecode::DigitType>(
                                             cartesianPickFrame(r.fps)));
                        AncTranslator::PacketsResult first = buildVia(src, payload, cfg);
                        REQUIRE(first.second().isOk());

                        AncTranslator::ParseResult parsed =
                                parseVia(first.first().front(), cfg);
                        REQUIRE(parsed.second().isOk());
                        AncAtc parsedAtc = parsed.first().get<AncAtc>();

                        // Round-trip the digits + payload-type discriminator.
                        CHECK(parsedAtc.timecode().frame() == src.frame());
                        CHECK(parsedAtc.timecode().fps() == r.fps);
                        const uint8_t expectedDbb1 =
                                payload == AncFormat::AtcLtc   ? AncAtc::Ltc :
                                payload == AncFormat::AtcVitc1 ? AncAtc::Vitc1 :
                                                                 AncAtc::Vitc2;
                        CHECK(parsedAtc.payloadType() == expectedDbb1);

                        // Rebuild from the parsed AncAtc, with the same
                        // cfg, and compare the wire bytes against the
                        // first build.  Any drift here proves a
                        // parse-then-encode asymmetry.
                        AncTranslator t(cfg);
                        AncTranslator::PacketsResult second = t.build(
                                Variant(parsedAtc), AncFormat(payload),
                                AncTransport::St291);
                        REQUIRE(second.second().isOk());
                        Result<St291Packet> rpA = St291Packet::from(first.first().front());
                        Result<St291Packet> rpB = St291Packet::from(second.first().front());
                        REQUIRE(rpA.second().isOk());
                        REQUIRE(rpB.second().isOk());
                        List<uint16_t> wireA = rpA.first().udw();
                        List<uint16_t> wireB = rpB.first().udw();
                        REQUIRE(wireA.size() == wireB.size());
                        for (size_t i = 0; i < wireA.size(); ++i) {
                                CAPTURE(i);
                                CHECK((wireA[i] & 0xFFu) == (wireB[i] & 0xFFu));
                        }
                }
        }
}

TEST_CASE("ATC Phase 7 — DBB2 byte round-trips on VITC under every rate") {
        // ST 12-2 DBB2 = (line, duplicate, valid, processed).  Set a
        // recognisable non-default DBB2 and verify it survives a
        // parse → build cycle at every rate.
        const uint8_t dbb2 = AncAtc::dbb2EncodeVitc(11, /*duplicate*/ true,
                                                    /*valid*/ true,
                                                    /*processed*/ false);
        for (const CartesianRate &r : cartesianRates) {
                CAPTURE(r.name);
                AncTranslateConfig cfg;
                cfg.set(AncTranslateConfig::AtcParseRateHint, r.fps);
                Timecode tc(Timecode::Mode(r.type), 1, 2, 3,
                            static_cast<Timecode::DigitType>(cartesianPickFrame(r.fps)));
                AncAtc src(tc);
                src.setDbb2(dbb2);
                AncTranslator t(cfg);
                AncTranslator::PacketsResult built = t.build(
                        Variant(src), AncFormat(AncFormat::AtcVitc1), AncTransport::St291);
                REQUIRE(built.second().isOk());
                AncTranslator::ParseResult parsed = parseVia(built.first().front(), cfg);
                REQUIRE(parsed.second().isOk());
                AncAtc out = parsed.first().get<AncAtc>();
                CHECK(out.dbb2() == dbb2);
                AncAtc::VitcDbb2 d = AncAtc::dbb2DecodeVitc(out.dbb2());
                CHECK(d.line == 11);
                CHECK(d.duplicate == true);
                CHECK(d.valid == true);
                CHECK(d.processed == false);
        }
}
