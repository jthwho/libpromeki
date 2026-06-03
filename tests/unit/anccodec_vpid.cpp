/**
 * @file      anccodec_vpid.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/ancdetails.h>
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

// ===========================================================================
// Details path — full human-readable analysis with enum stringification.
// ===========================================================================

TEST_CASE("VPID details: decodes link standard, rate, scan, and sampling") {
        AncTranslator                t;
        AncTranslator::PacketsResult built =
                t.build(Variant(makeReferenceVpid()), AncFormat(AncFormat::Vpid), AncTransport::St291);
        REQUIRE(built.second().isOk());
        AncPacket pkt = built.first().front();

        AncDetails d = t.details(pkt);

        // The reference VPID is 1080p59.94 / 3G Level A / 4:2:2 / 10-bit.
        CHECK(d.lines().contains(String("DID = 0x41")));
        CHECK(d.lines().contains(String("SDID = 0x01")));
        CHECK(d.lines().contains(String("DataCount = 4")));
        CHECK(d.lines().contains(String("Bytes = 89:ca:80:01")));
        // Instrument-style packed word (big-endian byte 1 in MSB).
        CHECK(d.lines().contains(String("PackedWord = 0x89CA8001")));
        CHECK(d.lines().contains(String("Version = ST 352:2013 (current)")));
        CHECK(d.lines().contains(String("Byte1 = 0x89")));
        CHECK(d.lines().contains(String("Standard = SMPTE ST 425-1 - 1080-line on Level A 3 Gb/s SDI")));
        CHECK(d.lines().contains(String("ScanMode = ") +
                                 makeReferenceVpid().videoScanMode().valueName()));
        CHECK(d.lines().contains(String("LinkStandard = ") +
                                 makeReferenceVpid().linkStandard().valueName()));
        // 0x89/0xCA: progressive transport + progressive picture.
        CHECK(d.lines().contains(String("Transport = Progressive")));
        CHECK(d.lines().contains(String("Picture = Progressive")));
        CHECK(d.lines().contains(String("Sampling = Y'CbCr 4:2:2")));
        CHECK(d.lines().contains(String("BitDepth = 10-bit")));
        CHECK(d.lines().contains(String("AspectRatio = 16:9")));
        CHECK(d.lines().contains(String("Channel = 1 (single-link or ch1 of multi-channel)")));
        CHECK_FALSE(d.hasErrors());
}

TEST_CASE("VPID details: full Table 3 sampling codes are all named") {
        struct Case {
                uint8_t code;
                const char *name;
        };
        // Every defined ST 352:2013 Table 3 code, including the
        // data-channel ("D") variants, ST 2048-2 FS, and X'Y'Z'.
        const Case cases[] = {
                {0x0, "Y'CbCr 4:2:2"},
                {0x1, "Y'CbCr 4:4:4"},
                {0x2, "R'G'B' 4:4:4"},
                {0x3, "Y'CbCr 4:2:0"},
                {0x4, "Y'CbCr+A 4:2:2:4"},
                {0x7, "SMPTE ST 2048-2 FS"},
                {0x8, "Y'CbCr+D 4:2:2:4"},
                {0xA, "R'G'B'+D 4:4:4:4"},
                {0xE, "X'Y'Z' 4:4:4"},
                {0xB, "Reserved (0xB)"},
        };
        AncTranslator t;
        for (const Case &c : cases) {
                // 1080-line 3G Level A, byte 3 = sampling code (aspect 4:3),
                // 10-bit so the rest of the packet is well-formed.
                SdiVpid v(0x89, 0xCA, c.code, 0x01);
                AncTranslator::PacketsResult built =
                        t.build(Variant(v), AncFormat(AncFormat::Vpid), AncTransport::St291);
                REQUIRE(built.second().isOk());
                AncDetails d = t.details(built.first().front());
                CHECK(d.lines().contains(String("Sampling = ") + String(c.name)));
        }
}

TEST_CASE("VPID details: SD (Annex B.1) surfaces horizontal luma sample count") {
        // Byte 1 = 0x81 SD; byte 2 = 0x05 (25 Hz); byte 3 b6 = 1 (960 samples),
        // 16:9, 4:2:2; byte 4 = 0x01 (10-bit).
        SdiVpid                      v(0x81, 0x05, 0xC0, 0x01);
        REQUIRE(v.isSdSchema());
        AncTranslator                t;
        AncTranslator::PacketsResult built =
                t.build(Variant(v), AncFormat(AncFormat::Vpid), AncTransport::St291);
        REQUIRE(built.second().isOk());
        AncDetails d = t.details(built.first().front());
        CHECK(d.lines().contains(String("HorizontalLumaSamples = 960")));
        // The SD schema does not carry a meaningful transport bit.
        CHECK_FALSE(d.lines().contains(String("Transport = Progressive")));
        CHECK_FALSE(d.lines().contains(String("Transport = Interlaced")));
}

TEST_CASE("VPID details: extended schema surfaces sub-image width and range") {
        // Byte 1 = 0xC0 (6G 2160-line); byte 3 b6 = 1 → 2048-wide sub-image;
        // byte 4 [1:0] = 0 → 10-bit Full Range under the extended schema.
        SdiVpid                      v(SdiVpid::Byte1_SL_6G_2160, 0x00, 0x40, 0x00);
        REQUIRE(v.isExtendedSchema());
        AncTranslator                t;
        AncTranslator::PacketsResult built =
                t.build(Variant(v), AncFormat(AncFormat::Vpid), AncTransport::St291);
        REQUIRE(built.second().isOk());
        AncDetails d = t.details(built.first().front());
        CHECK(d.lines().contains(String("SubImageWidth = 2048")));
        CHECK(d.lines().contains(String("QuantizationRange = Full")));
}

TEST_CASE("VPID details: pre-2008 historical code warns about Annex C layout") {
        // Byte 1 = 0x04 (Annex C.4, ST 274) — version bit 7 = 0.
        SdiVpid                      v(SdiVpid::Byte1_AnnexC_ST274, 0x00, 0x00, 0x00);
        REQUIRE_FALSE(v.isCurrentVersion());
        AncTranslator                t;
        AncTranslator::PacketsResult built =
                t.build(Variant(v), AncFormat(AncFormat::Vpid), AncTransport::St291);
        REQUIRE(built.second().isOk());
        AncDetails d = t.details(built.first().front());
        CHECK(d.lines().contains(String("Version = Pre-2008 (Annex C)")));
        CHECK(d.hasWarnings());
}

TEST_CASE("VPID details: 6G/12G extended schema surfaces HDR / colorimetry fields") {
        // Byte 1 = 0xCE (12G 2160-line) selects the extended schema; byte 2
        // [5:4] = PQ transfer, byte 3 [5:4] = UHDTV colorimetry.
        SdiVpid v(SdiVpid::Byte1_SL_12G_2160, 0x00, 0x00, 0x00);
        v.setTransferCode(SdiVpid::Transfer_PQ);
        v.setColorimetryCode(SdiVpid::Colorimetry_UHDTV);
        REQUIRE(v.isExtendedSchema());

        AncTranslator                t;
        AncTranslator::PacketsResult built =
                t.build(Variant(v), AncFormat(AncFormat::Vpid), AncTransport::St291);
        REQUIRE(built.second().isOk());

        AncDetails d = t.details(built.first().front());
        CHECK(d.lines().contains(String("Transfer = ") +
                                 v.transferCharacteristic().valueName()));
        CHECK(d.lines().contains(String("Colorimetry = ") + v.colorimetry().valueName()));
        CHECK(d.lines().contains(String("SignalType = Y'CbCr")));
}
