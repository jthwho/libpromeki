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
        Result<List<AncPacket>> built =
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
        Result<List<AncPacket>> built =
                t.build(Variant(src), AncFormat(AncFormat::HdrStatic2086), AncTransport::St291);
        REQUIRE(built.second().isOk());

        Result<Variant> parsed = t.parse(built.first().front());
        REQUIRE(parsed.second().isOk());
        REQUIRE(parsed.first().type() == Variant::TypeHdrStaticMetadata);
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
        Result<List<AncPacket>> built =
                t.build(Variant(md), AncFormat(AncFormat::HdrStatic2086), AncTransport::St291);
        REQUIRE(built.second().isOk());

        Result<St291Packet> rp = St291Packet::from(built.first().front());
        REQUIRE(rp.second().isOk());
        List<uint16_t> udw = rp.first().udw();
        REQUIRE(udw.size() == 2u + 6u);     // one CLL frame, no MD frame
        CHECK((udw[0] & 0xFF) == 0x01u);    // Type 1 = CLL
        CHECK((udw[1] & 0xFF) == 0x06u);    // Length 6

        Result<Variant> parsed = t.parse(built.first().front());
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
        Result<Variant> parsed = t.parse(p.packet());
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
        Result<Variant> parsed = t.parse(p.packet());
        CHECK(parsed.second().isError());
}

TEST_CASE("HdrStatic<->St291: builder rejects empty descriptor") {
        HdrStaticMetadata md; // default — MD invalid, CLL maxCLL=0 -> CLL invalid
        AncTranslator     t;
        Result<List<AncPacket>> built =
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
        Result<List<AncPacket>> built =
                t.build(Variant(hdr10Sample()), AncFormat(AncFormat::HdrStatic2086), AncTransport::St291);
        REQUIRE(built.second().isOk());
        const AncPacket &pkt = built.first().front();

        Result<List<AncPacket>> res = t.applySyncPolicy(pkt, FrameSyncDisposition::play(), 0);
        REQUIRE(res.second().isOk());
        REQUIRE(res.first().size() == 1);
        CHECK(res.first().front().data().size() == pkt.data().size());
}

TEST_CASE("HdrStatic sync policy: Drop returns an empty list") {
        AncTranslator           t;
        Result<List<AncPacket>> built =
                t.build(Variant(hdr10Sample()), AncFormat(AncFormat::HdrStatic2086), AncTransport::St291);
        REQUIRE(built.second().isOk());
        Result<List<AncPacket>> res = t.applySyncPolicy(built.first().front(),
                                                         FrameSyncDisposition::drop(), 0);
        REQUIRE(res.second().isOk());
        CHECK(res.first().size() == 0);
}

TEST_CASE("HdrStatic sync policy: Repeat copies the packet through at every index") {
        AncTranslator           t;
        Result<List<AncPacket>> built =
                t.build(Variant(hdr10Sample()), AncFormat(AncFormat::HdrStatic2086), AncTransport::St291);
        REQUIRE(built.second().isOk());

        for (uint8_t i = 0; i < 4; ++i) {
                Result<List<AncPacket>> res = t.applySyncPolicy(built.first().front(),
                                                                 FrameSyncDisposition::repeat(4), i);
                REQUIRE(res.second().isOk());
                REQUIRE(res.first().size() == 1);
                // Round-trip parse to confirm semantic identity at every index.
                Result<Variant> parsed = t.parse(res.first().front());
                REQUIRE(parsed.second().isOk());
                HdrStaticMetadata out = parsed.first().get<HdrStaticMetadata>();
                CHECK(out.contentLightLevel().maxCLL() == 1000u);
                CHECK(out.contentLightLevel().maxFALL() == 400u);
        }
}
