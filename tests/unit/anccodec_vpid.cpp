/**
 * @file      anccodec_vpid.cpp
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
#include <promeki/sdivpid.h>
#include <promeki/st291packet.h>
#include <promeki/variant.h>

using namespace promeki;

namespace {

        // 1080p59.94 over 3G Level A, YCbCr 4:2:2, 10-bit, BT.709.
        // Matches the canonical VPID example used in sdivpid documentation
        // (toUint32BE → 0x89CA8001).
        SdiVpid makeReferenceVpid() {
                return SdiVpid(0x89, 0xCA, 0x80, 0x01);
        }

} // namespace

// ============================================================================
// Round-trip through the AncTranslator dispatch framework.
// ============================================================================

TEST_CASE("VPID<->St291: SdiVpid round-trips through AncTranslator") {
        AncTranslator t;
        SdiVpid       input = makeReferenceVpid();

        AncTranslator::PacketsResult built = t.build(Variant(input),
                                                AncFormat(AncFormat::Vpid),
                                                AncTransport::St291);
        REQUIRE(built.second().isOk());
        REQUIRE(built.first().size() == 1);
        CHECK(built.first().front().format().id() == AncFormat::Vpid);
        CHECK(built.first().front().transport() == AncTransport::St291);

        Result<St291Packet> rp = St291Packet::from(built.first().front());
        REQUIRE(rp.second().isOk());
        CHECK(rp.first().did() == 0x41);
        CHECK(rp.first().sdid() == 0x01);
        CHECK(rp.first().dataCount() == 0x04);
        CHECK(rp.first().checksumValid());

        // Wire bytes: the 4 VPID bytes appear in order as the 4 UDWs
        // (low 8 bits of each 10-bit word; parity in bits 8-9).
        List<uint16_t> udw = rp.first().udw();
        REQUIRE(udw.size() == 4);
        CHECK((udw[0] & 0xFF) == 0x89);
        CHECK((udw[1] & 0xFF) == 0xCA);
        CHECK((udw[2] & 0xFF) == 0x80);
        CHECK((udw[3] & 0xFF) == 0x01);

        AncTranslator::ParseResult parsed = t.parse(built.first().front());
        REQUIRE(parsed.second().isOk());
        CHECK(parsed.first().type() == DataTypeSdiVpid);
        SdiVpid back = parsed.first().get<SdiVpid>();
        CHECK(back == input);
}

TEST_CASE("VPID<->St291: zero-byte VPID round-trips (degenerate edge case)") {
        AncTranslator t;
        SdiVpid       input;   // default-constructed = 00:00:00:00

        AncTranslator::PacketsResult built = t.build(Variant(input),
                                                AncFormat(AncFormat::Vpid),
                                                AncTransport::St291);
        REQUIRE(built.second().isOk());
        AncTranslator::ParseResult parsed = t.parse(built.first().front());
        REQUIRE(parsed.second().isOk());
        CHECK(parsed.first().get<SdiVpid>() == input);
}

// ============================================================================
// Builder input validation.
// ============================================================================

TEST_CASE("VPID<->St291: builder rejects non-SdiVpid Variant payload") {
        AncTranslator t;
        AncTranslator::PacketsResult built = t.build(Variant(uint8_t(0x42)),
                                                AncFormat(AncFormat::Vpid),
                                                AncTransport::St291);
        CHECK(built.second().isError());
        CHECK(built.second().code() == Error::InvalidArgument);
}

// ============================================================================
// Capability / cfg threading.
// ============================================================================

TEST_CASE("VPID<->St291: capability queries report parser+builder registered") {
        CHECK(AncTranslator::hasParser(AncFormat(AncFormat::Vpid), AncTransport::St291));
        CHECK(AncTranslator::hasBuilder(AncFormat(AncFormat::Vpid), AncTransport::St291));
}

TEST_CASE("VPID<->St291: line / fieldB threaded from AncTranslateConfig") {
        AncTranslateConfig cfg;
        cfg.set(AncTranslateConfig::St291BuildLine, uint16_t(10));
        cfg.set(AncTranslateConfig::St291FieldB, true);
        AncTranslator           t(cfg);
        AncTranslator::PacketsResult built = t.build(Variant(makeReferenceVpid()),
                                                AncFormat(AncFormat::Vpid),
                                                AncTransport::St291);
        REQUIRE(built.second().isOk());
        Result<St291Packet> rp = St291Packet::from(built.first().front());
        REQUIRE(rp.second().isOk());
        CHECK(rp.first().line() == 10);
        CHECK(rp.first().fieldB() == true);
}

TEST_CASE("VPID<->St291: default line is the F3 spec-default (0x7FE)") {
        AncTranslator           t;
        AncTranslator::PacketsResult built = t.build(Variant(makeReferenceVpid()),
                                                AncFormat(AncFormat::Vpid),
                                                AncTransport::St291);
        REQUIRE(built.second().isOk());
        Result<St291Packet> rp = St291Packet::from(built.first().front());
        REQUIRE(rp.second().isOk());
        CHECK(rp.first().line() == St291Packet::UnspecifiedLine);
        CHECK(rp.first().fieldB() == false);
}

// ===========================================================================
// Frame-sync policy: VPID is sticky / idempotent — copy through on Play and
// Repeat, drop on Drop.  Mirrors the AFD policy.
// ===========================================================================

TEST_CASE("VPID sync policy: Play returns the packet unchanged") {
        AncTranslator           t;
        AncTranslator::PacketsResult built = t.build(Variant(makeReferenceVpid()),
                                                AncFormat(AncFormat::Vpid),
                                                AncTransport::St291);
        REQUIRE(built.second().isOk());
        const AncPacket &pkt = built.first().front();

        AncTranslator::PacketsResult res = t.applySyncPolicy(pkt, FrameSyncDisposition::play(), 0);
        REQUIRE(res.second().isOk());
        REQUIRE(res.first().size() == 1);
        CHECK(res.first().front().data().size() == pkt.data().size());
}

TEST_CASE("VPID sync policy: Drop returns an empty list") {
        AncTranslator           t;
        AncTranslator::PacketsResult built = t.build(Variant(makeReferenceVpid()),
                                                AncFormat(AncFormat::Vpid),
                                                AncTransport::St291);
        REQUIRE(built.second().isOk());
        const AncPacket &pkt = built.first().front();

        AncTranslator::PacketsResult res = t.applySyncPolicy(pkt, FrameSyncDisposition::drop(), 0);
        REQUIRE(res.second().isOk());
        CHECK(res.first().size() == 0);
}

TEST_CASE("VPID sync policy: Repeat copies the packet through at every index") {
        AncTranslator           t;
        SdiVpid                 src = makeReferenceVpid();
        AncTranslator::PacketsResult built = t.build(Variant(src),
                                                AncFormat(AncFormat::Vpid),
                                                AncTransport::St291);
        REQUIRE(built.second().isOk());
        const AncPacket &pkt = built.first().front();

        for (uint8_t i = 0; i < 4; ++i) {
                AncTranslator::PacketsResult res = t.applySyncPolicy(pkt, FrameSyncDisposition::repeat(4), i);
                REQUIRE(res.second().isOk());
                REQUIRE(res.first().size() == 1);
                AncTranslator::ParseResult parsed = t.parse(res.first().front());
                REQUIRE(parsed.second().isOk());
                CHECK(parsed.first().get<SdiVpid>() == src);
        }
}

TEST_CASE("VPID sync policy: hasSyncPolicy reflects registration") {
        CHECK(AncTranslator::hasSyncPolicy(AncFormat(AncFormat::Vpid)));
}

// ===========================================================================
// Registry verification.
// ===========================================================================

TEST_CASE("Vpid: registered at DID 0x41 / SDID 0x01 with ST 352 description") {
        AncFormat f(AncFormat::Vpid);
        CHECK(f.isValid());
        CHECK(f.name() == "Vpid");
        CHECK(f.st291Did() == 0x41);
        CHECK(f.st291Sdid() == 0x01);
        CHECK(f.category() == AncCategory::PayloadId);
        CHECK(f.canonicalTransport() == AncTransport::St291);
        CHECK(f.desc().contains("352"));
}
