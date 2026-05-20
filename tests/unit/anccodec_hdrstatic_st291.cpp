/**
 * @file      anccodec_hdrstatic_st291.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cmath>
#include <doctest/doctest.h>
#include <promeki/ancformat.h>
#include <promeki/ancpacket.h>
#include <promeki/anctranslateconfig.h>
#include <promeki/anctranslator.h>
#include <promeki/contentlightlevel.h>
#include <promeki/framesyncdisposition.h>
#include <promeki/hdrstaticmetadata.h>
#include <promeki/masteringdisplay.h>
#include <promeki/result.h>
#include <promeki/st291packet.h>
#include <promeki/variant.h>

using namespace promeki;

namespace {

        HdrStaticMetadata hdr10Sample() {
                MasteringDisplay md(CIEPoint(0.708, 0.292), CIEPoint(0.170, 0.797),
                                    CIEPoint(0.131, 0.046), CIEPoint(0.3127, 0.3290),
                                    0.005, 1000.0);
                return HdrStaticMetadata(TransferCharacteristics::SMPTE2084, std::move(md),
                                         ContentLightLevel(1000, 400));
        }

} // namespace

TEST_CASE("HdrStatic<->St291: parser + builder registered") {
        CHECK(AncTranslator::hasParser(AncFormat(AncFormat::HdrStatic2086), AncTransport::St291));
        CHECK(AncTranslator::hasBuilder(AncFormat(AncFormat::HdrStatic2086), AncTransport::St291));
}

TEST_CASE("HdrStatic<->St291: AncFormat::fromSt291DidSdid resolves DID=0x41/SDID=0x0C") {
        AncFormat fmt = AncFormat::fromSt291DidSdid(0x41, 0x0C);
        CHECK(fmt.id() == AncFormat::HdrStatic2086);
}

TEST_CASE("HdrStatic<->St291: build emits both MD and CLL frames in one packet") {
        AncTranslateConfig cfg;
        cfg.set(AncTranslateConfig::St291BuildLine, uint16_t(9));
        AncTranslator t(cfg);

        HdrStaticMetadata md = hdr10Sample();
        AncTranslator::PacketsResult built =
                t.build(Variant(md), AncFormat(AncFormat::HdrStatic2086), AncTransport::St291);
        REQUIRE(built.second().isOk());

        Result<St291Packet> rp = St291Packet::from(built.first().front());
        REQUIRE(rp.second().isOk());
        const St291Packet &pkt = rp.first();
        CHECK(pkt.did() == 0x41u);
        CHECK(pkt.sdid() == 0x0Cu);
        CHECK(pkt.line() == 9u);

        List<uint16_t> udw = pkt.udw();
        // Frame 0 (Mastering Display): Type=0, Length=26, then 26 data bytes.
        REQUIRE(udw.size() >= 2 + 26 + 2 + 6);
        CHECK((udw[0] & 0xFF) == 0x00u); // Type 0
        CHECK((udw[1] & 0xFF) == 0x1Au); // Length 26
        CHECK((udw[2] & 0xFF) == 0x89u); // SEI payloadType=137
        CHECK((udw[3] & 0xFF) == 0x18u); // SEI payloadSize=24
        // Frame 1 (Content Light Level): Type=1, Length=6, ...
        const size_t cllOff = 2 + 26;
        CHECK((udw[cllOff + 0] & 0xFF) == 0x01u); // Type 1
        CHECK((udw[cllOff + 1] & 0xFF) == 0x06u); // Length 6
        CHECK((udw[cllOff + 2] & 0xFF) == 0x90u); // SEI payloadType=144
        CHECK((udw[cllOff + 3] & 0xFF) == 0x04u); // SEI payloadSize=4
}

TEST_CASE("HdrStatic<->St291: mastering display + CLL round-trip through AncTranslator") {
        AncTranslator     t;
        HdrStaticMetadata src = hdr10Sample();
        AncTranslator::PacketsResult built =
                t.build(Variant(src), AncFormat(AncFormat::HdrStatic2086), AncTransport::St291);
        REQUIRE(built.second().isOk());

        AncTranslator::ParseResult parsed = t.parse(built.first().front());
        REQUIRE(parsed.second().isOk());
        REQUIRE(parsed.first().type() == DataTypeHdrStaticMetadata);
        HdrStaticMetadata out = parsed.first().get<HdrStaticMetadata>();

        // ST 2108-1 carries no EOTF — parse resets to Unspecified.
        CHECK(out.eotf() == TransferCharacteristics::Unspecified);

        // Mastering display chromaticities round-trip at 1/50000 precision.
        const double tol = 3.0e-5;
        CHECK(std::fabs(out.masteringDisplay().red().x() - 0.708) < tol);
        CHECK(std::fabs(out.masteringDisplay().red().y() - 0.292) < tol);
        CHECK(std::fabs(out.masteringDisplay().green().x() - 0.170) < tol);
        CHECK(std::fabs(out.masteringDisplay().green().y() - 0.797) < tol);
        CHECK(std::fabs(out.masteringDisplay().blue().x() - 0.131) < tol);
        CHECK(std::fabs(out.masteringDisplay().blue().y() - 0.046) < tol);
        CHECK(std::fabs(out.masteringDisplay().whitePoint().x() - 0.3127) < tol);
        CHECK(std::fabs(out.masteringDisplay().whitePoint().y() - 0.3290) < tol);

        // ST 2108-1 luminance uses 0.0001 cd/m² LSB so values round-trip exactly.
        CHECK(std::fabs(out.masteringDisplay().maxLuminance() - 1000.0) < 1e-4);
        CHECK(std::fabs(out.masteringDisplay().minLuminance() - 0.005) < 1e-4);

        CHECK(out.contentLightLevel().maxCLL() == 1000u);
        CHECK(out.contentLightLevel().maxFALL() == 400u);
}

TEST_CASE("HdrStatic<->St291: build with only CLL emits a single CLL frame") {
        HdrStaticMetadata md(TransferCharacteristics::SMPTE2084, MasteringDisplay(),
                             ContentLightLevel(1500, 600));
        AncTranslator     t;
        AncTranslator::PacketsResult built =
                t.build(Variant(md), AncFormat(AncFormat::HdrStatic2086), AncTransport::St291);
        REQUIRE(built.second().isOk());

        Result<St291Packet> rp = St291Packet::from(built.first().front());
        REQUIRE(rp.second().isOk());
        List<uint16_t> udw = rp.first().udw();
        REQUIRE(udw.size() == 2u + 6u);     // one CLL frame, no MD frame
        CHECK((udw[0] & 0xFF) == 0x01u);    // Type 1 = CLL
        CHECK((udw[1] & 0xFF) == 0x06u);    // Length 6

        AncTranslator::ParseResult parsed = t.parse(built.first().front());
        REQUIRE(parsed.second().isOk());
        HdrStaticMetadata out = parsed.first().get<HdrStaticMetadata>();
        CHECK_FALSE(out.masteringDisplay().isValid());
        CHECK(out.contentLightLevel().maxCLL() == 1500u);
        CHECK(out.contentLightLevel().maxFALL() == 600u);
}

TEST_CASE("HdrStatic<->St291: parser skips unknown frame types in the UDW") {
        // Hand-build a UDW with [unknown frame Type=99, Length=4, payload]
        // followed by a real Type=1 (CLL) frame.
        List<uint16_t> udw;
        // Unknown frame
        udw.pushToBack(99); udw.pushToBack(4);
        udw.pushToBack(0xAA); udw.pushToBack(0xBB); udw.pushToBack(0xCC); udw.pushToBack(0xDD);
        // CLL frame: Type=1, Length=6, payloadType=144, payloadSize=4, MaxCLL=0x0258 (600), MaxFALL=0x00C8 (200)
        udw.pushToBack(0x01); udw.pushToBack(0x06);
        udw.pushToBack(0x90); udw.pushToBack(0x04);
        udw.pushToBack(0x02); udw.pushToBack(0x58);
        udw.pushToBack(0x00); udw.pushToBack(0xC8);

        St291Packet     p = St291Packet::buildRaw(0x41, 0x0C, udw, 9);
        AncTranslator   t;
        AncTranslator::ParseResult parsed = t.parse(p.packet());
        REQUIRE(parsed.second().isOk());
        HdrStaticMetadata out = parsed.first().get<HdrStaticMetadata>();
        CHECK(out.contentLightLevel().maxCLL() == 600u);
        CHECK(out.contentLightLevel().maxFALL() == 200u);
}

TEST_CASE("HdrStatic<->St291: parser rejects truncated frame length") {
        // Type 1 (CLL), Length 6, but only 4 data bytes follow.
        List<uint16_t> udw;
        udw.pushToBack(0x01); udw.pushToBack(0x06);
        udw.pushToBack(0x90); udw.pushToBack(0x04);
        udw.pushToBack(0x00); udw.pushToBack(0x01);

        St291Packet     p = St291Packet::buildRaw(0x41, 0x0C, udw, 9);
        AncTranslator   t;
        AncTranslator::ParseResult parsed = t.parse(p.packet());
        CHECK(parsed.second().isError());
}

TEST_CASE("HdrStatic<->St291: builder rejects empty descriptor") {
        HdrStaticMetadata md; // default — MD invalid, CLL maxCLL=0 -> CLL invalid
        AncTranslator     t;
        AncTranslator::PacketsResult built =
                t.build(Variant(md), AncFormat(AncFormat::HdrStatic2086), AncTransport::St291);
        CHECK(built.second().isError());
}

// ===========================================================================
// Frame-sync policy: HDR static metadata is sticky — copy through on Repeat,
// drop on Drop, no per-frame state to advance.
// ===========================================================================

TEST_CASE("HdrStatic sync policy: hasSyncPolicy reflects registration") {
        CHECK(AncTranslator::hasSyncPolicy(AncFormat(AncFormat::HdrStatic2086)));
}

TEST_CASE("HdrStatic sync policy: Play returns the packet unchanged") {
        AncTranslator           t;
        AncTranslator::PacketsResult built =
                t.build(Variant(hdr10Sample()), AncFormat(AncFormat::HdrStatic2086), AncTransport::St291);
        REQUIRE(built.second().isOk());
        const AncPacket &pkt = built.first().front();

        AncTranslator::PacketsResult res = t.applySyncPolicy(pkt, FrameSyncDisposition::play(), 0);
        REQUIRE(res.second().isOk());
        REQUIRE(res.first().size() == 1);
        CHECK(res.first().front().data().size() == pkt.data().size());
}

TEST_CASE("HdrStatic sync policy: Drop returns an empty list") {
        AncTranslator           t;
        AncTranslator::PacketsResult built =
                t.build(Variant(hdr10Sample()), AncFormat(AncFormat::HdrStatic2086), AncTransport::St291);
        REQUIRE(built.second().isOk());
        AncTranslator::PacketsResult res = t.applySyncPolicy(built.first().front(),
                                                         FrameSyncDisposition::drop(), 0);
        REQUIRE(res.second().isOk());
        CHECK(res.first().size() == 0);
}

// ===========================================================================
// D4c — Per-frame-type byte-position audit against ST 2108-1 §5.3.2 / §5.3.3.
//
// These tests lock down the exact wire bytes produced for one fixed input
// (the BT.2020-mastered HDR10 sample defined by hdr10Sample()).  Any change
// to the SEI-body encoding (chromaticity scale 0.00002, luminance scale
// 0.0001 cd/m², big-endian byte order, RGB primary ordering) would flip
// these byte values and trip the asserts immediately.
// ===========================================================================

TEST_CASE("HdrStatic<->St291: ST 2108-1 §5.3.2 MD frame byte exactness") {
        AncTranslator           t;
        AncTranslator::PacketsResult built = t.build(Variant(hdr10Sample()),
                                                AncFormat(AncFormat::HdrStatic2086),
                                                AncTransport::St291);
        REQUIRE(built.second().isOk());
        Result<St291Packet> rp = St291Packet::from(built.first().front());
        REQUIRE(rp.second().isOk());
        List<uint16_t> udw = rp.first().udw();

        // MD frame occupies UDW [0..27]; the four header bytes plus 24-byte
        // body match ST 2108-1 Table 2 + §5.3.2 + H.265 Annex D.
        REQUIRE(udw.size() >= 28u);
        CHECK((udw[0] & 0xFF) == 0x00u);  // HDR/WCG Metadata Frame Type = 0
        CHECK((udw[1] & 0xFF) == 0x1Au);  // HDR/WCG Metadata Frame Length = 26
        CHECK((udw[2] & 0xFF) == 0x89u);  // Data Byte 1: SEI payloadType=137
        CHECK((udw[3] & 0xFF) == 0x18u);  // Data Byte 2: SEI payloadSize=24
        // mastering_display_colour_volume() body bytes (big-endian),
        // scale = 50000 for chromaticity, 10000 for luminance.
        const uint8_t kExpectedMdBody[24] = {
                0x8A, 0x48, 0x39, 0x08,  // red.x=35400, red.y=14600  (0.708, 0.292)
                0x21, 0x34, 0x9B, 0xAA,  // green.x=8500, green.y=39850 (0.170, 0.797)
                0x19, 0x96, 0x08, 0xFC,  // blue.x=6550,  blue.y=2300   (0.131, 0.046)
                0x3D, 0x13, 0x40, 0x42,  // wp.x=15635,   wp.y=16450    (0.3127, 0.3290)
                0x00, 0x98, 0x96, 0x80,  // maxL=10000000 (1000.0 cd/m² × 10000)
                0x00, 0x00, 0x00, 0x32,  // minL=50       (0.005 cd/m² × 10000)
        };
        for (size_t i = 0; i < 24; ++i) {
                CAPTURE(i);
                CHECK((udw[4 + i] & 0xFF) == kExpectedMdBody[i]);
        }
}

TEST_CASE("HdrStatic<->St291: ST 2108-1 §5.3.3 CLL frame byte exactness") {
        AncTranslator           t;
        AncTranslator::PacketsResult built = t.build(Variant(hdr10Sample()),
                                                AncFormat(AncFormat::HdrStatic2086),
                                                AncTransport::St291);
        REQUIRE(built.second().isOk());
        Result<St291Packet> rp = St291Packet::from(built.first().front());
        REQUIRE(rp.second().isOk());
        List<uint16_t> udw = rp.first().udw();

        // MD frame is 28 UDW (2 header + 26 data); CLL frame starts at 28
        // and occupies UDW [28..35]: 2 header + 6 data = 8 UDW.
        REQUIRE(udw.size() == 28u + 8u);
        const size_t off = 28u;
        CHECK((udw[off + 0] & 0xFF) == 0x01u);  // Frame Type = 1
        CHECK((udw[off + 1] & 0xFF) == 0x06u);  // Frame Length = 6
        CHECK((udw[off + 2] & 0xFF) == 0x90u);  // SEI payloadType = 144
        CHECK((udw[off + 3] & 0xFF) == 0x04u);  // SEI payloadSize = 4
        // content_light_level_info(): max_content_light_level=1000=0x03E8,
        // max_pic_average_light_level=400=0x0190, both u(16) big-endian.
        CHECK((udw[off + 4] & 0xFF) == 0x03u);
        CHECK((udw[off + 5] & 0xFF) == 0xE8u);
        CHECK((udw[off + 6] & 0xFF) == 0x01u);
        CHECK((udw[off + 7] & 0xFF) == 0x90u);
}

TEST_CASE("HdrStatic<->St291: parse from spec-exact wire bytes") {
        // Hand-construct a UDW per ST 2108-1 Table 2 with BT.709 primaries,
        // D65 white point, 4000 cd/m² peak, 0.01 cd/m² floor, MaxCLL=4000,
        // MaxFALL=2000 — and verify the parser recovers those values.
        //   red.x   = 0.64   × 50000 = 32000 = 0x7D00
        //   red.y   = 0.33   × 50000 = 16500 = 0x4074
        //   green.x = 0.30   × 50000 = 15000 = 0x3A98
        //   green.y = 0.60   × 50000 = 30000 = 0x7530
        //   blue.x  = 0.15   × 50000 =  7500 = 0x1D4C
        //   blue.y  = 0.06   × 50000 =  3000 = 0x0BB8
        //   wp.x    = 0.3127 × 50000 = 15635 = 0x3D13
        //   wp.y    = 0.3290 × 50000 = 16450 = 0x4042
        //   maxL    = 4000.0 × 10000 = 40000000 = 0x02625A00
        //   minL    = 0.01   × 10000 = 100      = 0x00000064
        const uint8_t kBytes[] = {
                // MD frame
                0x00, 0x1A,              // Type 0, Length 26
                0x89, 0x18,              // SEI payloadType=137, payloadSize=24
                0x7D, 0x00, 0x40, 0x74,  // red.x, red.y
                0x3A, 0x98, 0x75, 0x30,  // green.x, green.y
                0x1D, 0x4C, 0x0B, 0xB8,  // blue.x, blue.y
                0x3D, 0x13, 0x40, 0x42,  // wp.x, wp.y
                0x02, 0x62, 0x5A, 0x00,  // maxL = 0x02625A00
                0x00, 0x00, 0x00, 0x64,  // minL = 0x00000064
                // CLL frame
                0x01, 0x06,              // Type 1, Length 6
                0x90, 0x04,              // SEI payloadType=144, payloadSize=4
                0x0F, 0xA0,              // MaxCLL  = 4000
                0x07, 0xD0,              // MaxFALL = 2000
        };
        List<uint16_t> udw;
        for (size_t i = 0; i < sizeof(kBytes); ++i) udw.pushToBack(kBytes[i]);

        St291Packet     p = St291Packet::buildRaw(0x41, 0x0C, udw, 9);
        AncTranslator   t;
        AncTranslator::ParseResult parsed = t.parse(p.packet());
        REQUIRE(parsed.second().isOk());
        HdrStaticMetadata out = parsed.first().get<HdrStaticMetadata>();
        const double tol = 3.0e-5;
        CHECK(std::fabs(out.masteringDisplay().red().x() - 0.64) < tol);
        CHECK(std::fabs(out.masteringDisplay().red().y() - 0.33) < tol);
        CHECK(std::fabs(out.masteringDisplay().green().x() - 0.30) < tol);
        CHECK(std::fabs(out.masteringDisplay().green().y() - 0.60) < tol);
        CHECK(std::fabs(out.masteringDisplay().blue().x() - 0.15) < tol);
        CHECK(std::fabs(out.masteringDisplay().blue().y() - 0.06) < tol);
        CHECK(std::fabs(out.masteringDisplay().maxLuminance() - 4000.0) < 1e-4);
        CHECK(std::fabs(out.masteringDisplay().minLuminance() - 0.01) < 1e-4);
        CHECK(out.contentLightLevel().maxCLL() == 4000u);
        CHECK(out.contentLightLevel().maxFALL() == 2000u);
}

TEST_CASE("HdrStatic<->St291: forward-tolerant parse on oversized MD Frame Length") {
        // A hypothetical future spec revision could extend the SEI body.
        // Verify the parser still decodes the first 24 bytes correctly and
        // skips the extras, instead of erroring out.
        List<uint16_t> udw;
        // MD frame, Length = 28 (2 extra bytes past the 26-byte normative form).
        udw.pushToBack(0x00); udw.pushToBack(0x1C);
        udw.pushToBack(0x89); udw.pushToBack(0x18);
        // BT.709 body — same as the spec-exact test above.
        const uint8_t kBody[24] = {
                0x7D, 0x00, 0x40, 0x74, 0x3A, 0x98, 0x75, 0x30,
                0x1D, 0x4C, 0x0B, 0xB8, 0x3D, 0x13, 0x40, 0x42,
                0x02, 0x62, 0x5A, 0x00, 0x00, 0x00, 0x00, 0x64,
        };
        for (auto b : kBody) udw.pushToBack(b);
        // Two extra padding bytes that a future revision might define.
        udw.pushToBack(0xDE); udw.pushToBack(0xAD);
        // CLL frame.
        udw.pushToBack(0x01); udw.pushToBack(0x06);
        udw.pushToBack(0x90); udw.pushToBack(0x04);
        udw.pushToBack(0x0F); udw.pushToBack(0xA0);
        udw.pushToBack(0x07); udw.pushToBack(0xD0);

        St291Packet     p = St291Packet::buildRaw(0x41, 0x0C, udw, 9);
        AncTranslator   t;
        AncTranslator::ParseResult parsed = t.parse(p.packet());
        REQUIRE(parsed.second().isOk());
        HdrStaticMetadata out = parsed.first().get<HdrStaticMetadata>();
        CHECK(out.masteringDisplay().isValid());
        CHECK(out.contentLightLevel().maxCLL() == 4000u);
        CHECK(out.contentLightLevel().maxFALL() == 2000u);
}

TEST_CASE("HdrStatic<->St291: parser rejects sub-minimum MD Frame Length") {
        // ST 2108-1 §5.3.2: Frame Length shall be 0x1A (26).  A value
        // smaller than the spec minimum can't carry the full SEI body, so
        // CorruptData is the right answer.
        List<uint16_t> udw;
        udw.pushToBack(0x00); udw.pushToBack(0x10);  // Length 16 < 26
        for (int i = 0; i < 16; ++i) udw.pushToBack(0x00);
        St291Packet     p = St291Packet::buildRaw(0x41, 0x0C, udw, 9);
        AncTranslator   t;
        AncTranslator::ParseResult parsed = t.parse(p.packet());
        CHECK(parsed.second().isError());
}

TEST_CASE("HdrStatic<->St291: parser rejects wrong SEI payloadType/size on MD") {
        // ST 2108-1 §5.3.2: Data Byte 1 shall be 0x89, Data Byte 2 shall be
        // 0x18.  Anything else is malformed — fail rather than decode garbage.
        List<uint16_t> udw;
        udw.pushToBack(0x00); udw.pushToBack(0x1A);
        udw.pushToBack(0x88);                // payloadType = 136 (wrong)
        udw.pushToBack(0x18);
        for (int i = 0; i < 24; ++i) udw.pushToBack(0x00);
        St291Packet     p = St291Packet::buildRaw(0x41, 0x0C, udw, 9);
        AncTranslator   t;
        AncTranslator::ParseResult parsed = t.parse(p.packet());
        CHECK(parsed.second().isError());
}

TEST_CASE("HdrStatic<->St291: duplicate Type-0 frame — last wins (receiver tolerance)") {
        // ST 2108-1 §5.3.2 says no more than one Type-0 frame per packet.
        // A non-conformant sender that emits two is decoded tolerantly:
        // the last one wins, no error is surfaced.
        List<uint16_t> udw;
        auto pushMd = [&](double maxL) {
                udw.pushToBack(0x00); udw.pushToBack(0x1A);
                udw.pushToBack(0x89); udw.pushToBack(0x18);
                // Chromaticities — all zero for simplicity (degenerate).
                for (int i = 0; i < 16; ++i) udw.pushToBack(0x00);
                // maxL (u32 BE), minL=0.
                uint32_t v = static_cast<uint32_t>(maxL * 10000.0);
                udw.pushToBack((v >> 24) & 0xFF);
                udw.pushToBack((v >> 16) & 0xFF);
                udw.pushToBack((v >>  8) & 0xFF);
                udw.pushToBack( v        & 0xFF);
                udw.pushToBack(0x00); udw.pushToBack(0x00);
                udw.pushToBack(0x00); udw.pushToBack(0x00);
        };
        pushMd(1000.0);   // first
        pushMd(4000.0);   // second — should win

        St291Packet     p = St291Packet::buildRaw(0x41, 0x0C, udw, 9);
        AncTranslator   t;
        AncTranslator::ParseResult parsed = t.parse(p.packet());
        REQUIRE(parsed.second().isOk());
        HdrStaticMetadata out = parsed.first().get<HdrStaticMetadata>();
        CHECK(std::fabs(out.masteringDisplay().maxLuminance() - 4000.0) < 1e-4);
}

TEST_CASE("HdrStatic sync policy: Repeat copies the packet through at every index") {
        AncTranslator           t;
        AncTranslator::PacketsResult built =
                t.build(Variant(hdr10Sample()), AncFormat(AncFormat::HdrStatic2086), AncTransport::St291);
        REQUIRE(built.second().isOk());

        for (uint8_t i = 0; i < 4; ++i) {
                AncTranslator::PacketsResult res = t.applySyncPolicy(built.first().front(),
                                                                 FrameSyncDisposition::repeat(4), i);
                REQUIRE(res.second().isOk());
                REQUIRE(res.first().size() == 1);
                // Round-trip parse to confirm semantic identity at every index.
                AncTranslator::ParseResult parsed = t.parse(res.first().front());
                REQUIRE(parsed.second().isOk());
                HdrStaticMetadata out = parsed.first().get<HdrStaticMetadata>();
                CHECK(out.contentLightLevel().maxCLL() == 1000u);
                CHECK(out.contentLightLevel().maxFALL() == 400u);
        }
}
