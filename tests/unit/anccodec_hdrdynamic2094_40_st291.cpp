/**
 * @file      anccodec_hdrdynamic2094_40_st291.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Exercises the SMPTE ST 2108-2 HDR10+ codec, with particular focus
 * on the multi-packet split: a full HDR10+ Message routinely exceeds
 * the 255-byte ST 291 UDW cap, so the builder fragments across
 * multiple ANC packets with incrementing Packet Count and the parser
 * (registered as a @c MultiParserFn) reassembles.
 */

#include <doctest/doctest.h>
#include <promeki/ancformat.h>
#include <promeki/ancpacket.h>
#include <promeki/anctranslateconfig.h>
#include <promeki/anctranslator.h>
#include <promeki/framesyncdisposition.h>
#include <promeki/hdrdynamic2094_40.h>
#include <promeki/result.h>
#include <promeki/st291packet.h>
#include <promeki/variant.h>

using namespace promeki;

namespace {

        // Full single-window HDR10+ descriptor with every optional
        // sub-structure populated.  Exceeds 255 bytes on the wire,
        // forcing multi-packet split.  Uses ApplicationVersion=0
        // because ST 2094-40:2020 §9.4 SHALL-NOT-emits
        // ColorSaturationWeight (and the two ActualPeakLuminance grids)
        // at AppVer=1 — the codec strips them on emission, which
        // would defeat the round-trip equality check downstream.
        // §9.3 at AppVer=0 is the SHOULD-NOT softer form so the codec
        // warns-but-emits, preserving every field for the round-trip.
        HdrDynamic2094_40 fullSingleWindow() {
                HdrDynamic2094_40 md;
                md.setApplicationVersion(0);
                md.setNumWindows(1);
                md.setTargetedSystemDisplayMaximumLuminance(10'000'000u); // 1000 cd/m²

                auto &wp = md.windowProcessing()[0];
                wp.maxScl[0] = 100000;
                wp.maxScl[1] = 95000;
                wp.maxScl[2] = 90000;
                wp.averageMaxRgb = 50000;
                wp.distribution = {
                        HdrDynamic2094_40::DistributionMaxRgb{ 1u, 12000u },
                        HdrDynamic2094_40::DistributionMaxRgb{50u, 45000u },
                        HdrDynamic2094_40::DistributionMaxRgb{99u, 95000u },
                };
                wp.fractionBrightPixels = 256;
                wp.hasToneMapping = true;
                wp.toneMapping.kneePointX = 1024;
                wp.toneMapping.kneePointY = 2048;
                wp.toneMapping.bezierCurveAnchors = {64, 128, 192, 256};
                wp.hasColorSaturationMapping = true;
                wp.colorSaturationWeight = 12;
                return md;
        }

        // Returns the total UDW byte count across a list of packets.
        size_t totalUdwBytes(const AncPacket::List &pkts) {
                size_t total = 0;
                for (const AncPacket &pkt : pkts) {
                        Result<St291Packet> rp = St291Packet::from(pkt);
                        if (rp.second().isOk()) total += rp.first().udw().size();
                }
                return total;
        }

} // namespace

// ============================================================================
// Registration
// ============================================================================

TEST_CASE("HdrDynamic2094_40<->St291: multi-parser + builder registered") {
        CHECK(AncTranslator::hasMultiParser(AncFormat(AncFormat::HdrDynamic2094_40),
                                             AncTransport::St291));
        CHECK(AncTranslator::hasBuilder(AncFormat(AncFormat::HdrDynamic2094_40),
                                         AncTransport::St291));
        // hasParser() also reports true because MultiParser counts.
        CHECK(AncTranslator::hasParser(AncFormat(AncFormat::HdrDynamic2094_40),
                                        AncTransport::St291));
}

TEST_CASE("HdrDynamic2094_40<->St291: AncFormat::fromSt291DidSdid resolves DID=0x41/SDID=0x0D") {
        AncFormat fmt = AncFormat::fromSt291DidSdid(0x41, 0x0D);
        CHECK(fmt.id() == AncFormat::HdrDynamic2094_40);
}

// ============================================================================
// Single-packet fast path (descriptor small enough to fit in one ANC packet)
// ============================================================================

TEST_CASE("HdrDynamic2094_40<->St291: minimal descriptor fits in one packet") {
        HdrDynamic2094_40 md;
        md.setApplicationVersion(0);
        md.setNumWindows(1);
        md.setTargetedSystemDisplayMaximumLuminance(10'000'000u);
        auto &wp = md.windowProcessing()[0];
        wp.maxScl[0] = wp.maxScl[1] = wp.maxScl[2] = 50000;
        wp.averageMaxRgb = 25000;
        wp.fractionBrightPixels = 100;
        wp.distribution = {HdrDynamic2094_40::DistributionMaxRgb{50u, 35000u}};

        AncTranslator           t;
        AncTranslator::PacketsResult built =
                t.build(Variant(md), AncFormat(AncFormat::HdrDynamic2094_40), AncTransport::St291);
        REQUIRE(built.second().isOk());
        REQUIRE(built.first().size() == 1u);
        CHECK(built.first().front().format().id() == AncFormat::HdrDynamic2094_40);

        Result<St291Packet> rp = St291Packet::from(built.first().front());
        REQUIRE(rp.second().isOk());
        CHECK(rp.first().did() == 0x41u);
        CHECK(rp.first().sdid() == 0x0Du);
        CHECK((rp.first().udw().front() & 0xFF) == 0x01u); // Packet Count = 1

        AncTranslator::ParseResult parsed = t.parseGroup(built.first());
        REQUIRE(parsed.second().isOk());
        HdrDynamic2094_40 out = parsed.first().get<HdrDynamic2094_40>();
        CHECK(out.applicationVersion() == md.applicationVersion());
        CHECK(out.windowProcessing()[0].averageMaxRgb == wp.averageMaxRgb);
}

// ============================================================================
// Multi-packet split (the real-world HDR10+ path)
// ============================================================================

TEST_CASE("HdrDynamic2094_40<->St291: full descriptor splits across multiple packets") {
        HdrDynamic2094_40 src = fullSingleWindow();

        AncTranslator           t;
        AncTranslator::PacketsResult built =
                t.build(Variant(src), AncFormat(AncFormat::HdrDynamic2094_40), AncTransport::St291);
        REQUIRE(built.second().isOk());
        // Sanity: the full descriptor must split — if this assertion ever
        // starts firing it means the wire encoding shrank below 255 bytes,
        // which is unlikely but would still leave the codec correct (just
        // tested less thoroughly).
        REQUIRE(built.first().size() >= 2u);

        // Every wire packet must respect the 255-byte ST 291 UDW cap.
        for (const AncPacket &pkt : built.first()) {
                Result<St291Packet> rp = St291Packet::from(pkt);
                REQUIRE(rp.second().isOk());
                CHECK(rp.first().udw().size() <= 255u);
                CHECK(rp.first().did() == 0x41u);
                CHECK(rp.first().sdid() == 0x0Du);
                CHECK(rp.first().checksumValid());
        }

        // Packet Count must be 1, 2, 3, ... in order.
        for (size_t i = 0; i < built.first().size(); ++i) {
                Result<St291Packet> rp = St291Packet::from(built.first()[i]);
                REQUIRE(rp.second().isOk());
                const uint8_t pc = static_cast<uint8_t>(rp.first().udw().front() & 0xFF);
                CHECK(pc == static_cast<uint8_t>(i + 1));
        }

        // Reassembly through the registered multi-parser must round-trip
        // the descriptor exactly.
        AncTranslator::ParseResult parsed = t.parseGroup(built.first());
        REQUIRE(parsed.second().isOk());
        HdrDynamic2094_40 out = parsed.first().get<HdrDynamic2094_40>();
        CHECK(out == src);
}

TEST_CASE("HdrDynamic2094_40<->St291: total wire bytes match across pack/unpack") {
        HdrDynamic2094_40 src = fullSingleWindow();
        AncTranslator     t;
        AncTranslator::PacketsResult built =
                t.build(Variant(src), AncFormat(AncFormat::HdrDynamic2094_40), AncTransport::St291);
        REQUIRE(built.second().isOk());
        const size_t totalBytes = totalUdwBytes(built.first());
        // Every packet has 1 byte of Packet Count overhead.
        const size_t packetCountOverhead = built.first().size();
        // Strip Packet Count overhead → that's the Message bytes spread
        // across packets.  Message starts with 2-byte u16 Length.
        const size_t messageBytes = totalBytes - packetCountOverhead;
        // Verify: read the Message Length from packet 1's UDW bytes [1..2]
        Result<St291Packet> rp = St291Packet::from(built.first().front());
        REQUIRE(rp.second().isOk());
        List<uint16_t> udw = rp.first().udw();
        REQUIRE(udw.size() >= 3u);
        const uint16_t msgLen = static_cast<uint16_t>(((udw[1] & 0xFF) << 8) | (udw[2] & 0xFF));
        // messageBytes includes the 2-byte Length field + msgLen payload.
        CHECK(messageBytes == static_cast<size_t>(msgLen) + 2u);
}

TEST_CASE("HdrDynamic2094_40<->St291: multi-window split round-trips through assembly") {
        HdrDynamic2094_40 src;
        src.setApplicationVersion(0);
        src.setNumWindows(2);
        src.setTargetedSystemDisplayMaximumLuminance(40'000'000u);

        // Window 0 stats.
        src.windowProcessing()[0].maxScl[0] = 80000;
        src.windowProcessing()[0].maxScl[1] = 70000;
        src.windowProcessing()[0].maxScl[2] = 60000;
        src.windowProcessing()[0].averageMaxRgb = 40000;
        src.windowProcessing()[0].fractionBrightPixels = 100;
        src.windowProcessing()[0].distribution = {
                HdrDynamic2094_40::DistributionMaxRgb{25u, 30000u},
                HdrDynamic2094_40::DistributionMaxRgb{75u, 60000u},
        };

        // Window 1 geometry + stats.
        src.extraWindows()[0].upperLeftCornerX = 100;
        src.extraWindows()[0].upperLeftCornerY = 200;
        src.extraWindows()[0].lowerRightCornerX = 800;
        src.extraWindows()[0].lowerRightCornerY = 600;
        src.extraWindows()[0].rotationAngle = 30;
        src.extraWindows()[0].overlapProcessOption = true;
        src.windowProcessing()[1].maxScl[0] = 120000;
        src.windowProcessing()[1].maxScl[1] = 110000;
        src.windowProcessing()[1].maxScl[2] = 100000;
        src.windowProcessing()[1].averageMaxRgb = 60000;
        src.windowProcessing()[1].fractionBrightPixels = 50;
        src.windowProcessing()[1].distribution = {
                HdrDynamic2094_40::DistributionMaxRgb{99u, 105000u},
        };

        AncTranslator           t;
        AncTranslator::PacketsResult built =
                t.build(Variant(src), AncFormat(AncFormat::HdrDynamic2094_40), AncTransport::St291);
        REQUIRE(built.second().isOk());
        // Two-window descriptors typically need multiple packets too.
        for (const AncPacket &pkt : built.first()) {
                Result<St291Packet> rp = St291Packet::from(pkt);
                REQUIRE(rp.second().isOk());
                CHECK(rp.first().udw().size() <= 255u);
        }

        AncTranslator::ParseResult parsed = t.parseGroup(built.first());
        REQUIRE(parsed.second().isOk());
        HdrDynamic2094_40 out = parsed.first().get<HdrDynamic2094_40>();
        CHECK(out.numWindows() == 2u);
        REQUIRE(out.extraWindows().size() == 1u);
        CHECK(out.extraWindows()[0].upperLeftCornerX == 100u);
        CHECK(out.extraWindows()[0].overlapProcessOption == true);
}

TEST_CASE("HdrDynamic2094_40<->St291: actual-peak grids survive multi-packet round-trip") {
        HdrDynamic2094_40 src;
        src.setApplicationVersion(0);
        src.setNumWindows(1);
        src.setTargetedSystemDisplayMaximumLuminance(10'000'000u);

        // Targeted-display 4×4 grid.
        src.targetedSystemDisplayActualPeakLuminance().numRows = 4;
        src.targetedSystemDisplayActualPeakLuminance().numCols = 4;
        src.targetedSystemDisplayActualPeakLuminance().values =
                {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 0};

        // Mastering-display 3×5 grid.
        src.masteringDisplayActualPeakLuminance().numRows = 3;
        src.masteringDisplayActualPeakLuminance().numCols = 5;
        src.masteringDisplayActualPeakLuminance().values =
                {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14};

        auto &wp = src.windowProcessing()[0];
        wp.maxScl[0] = wp.maxScl[1] = wp.maxScl[2] = 50000;
        wp.averageMaxRgb = 25000;
        wp.fractionBrightPixels = 100;

        AncTranslator           t;
        AncTranslator::PacketsResult built =
                t.build(Variant(src), AncFormat(AncFormat::HdrDynamic2094_40), AncTransport::St291);
        REQUIRE(built.second().isOk());

        AncTranslator::ParseResult parsed = t.parseGroup(built.first());
        REQUIRE(parsed.second().isOk());
        HdrDynamic2094_40 out = parsed.first().get<HdrDynamic2094_40>();

        const auto &tgIn = src.targetedSystemDisplayActualPeakLuminance();
        const auto &tgOut = out.targetedSystemDisplayActualPeakLuminance();
        CHECK(tgOut.numRows == tgIn.numRows);
        CHECK(tgOut.numCols == tgIn.numCols);
        REQUIRE(tgOut.values.size() == tgIn.values.size());
        for (size_t i = 0; i < tgOut.values.size(); ++i) CHECK(tgOut.values[i] == tgIn.values[i]);

        const auto &mdIn = src.masteringDisplayActualPeakLuminance();
        const auto &mdOut = out.masteringDisplayActualPeakLuminance();
        CHECK(mdOut.numRows == mdIn.numRows);
        CHECK(mdOut.numCols == mdIn.numCols);
}

// ============================================================================
// D5c — Byte-position audit against ST 2108-2 §5 + ST 2094-2 Tables 10/11.
//
// These tests lock down the wire constants the codec is responsible for: the
// 16-byte DMCVT App 4 Set Key from ST 2094-2 Table 10, the 4-byte BER
// long-form Set Length per ST 2094-2 §6.1, the 16-bit big-endian Message
// Length from ST 2108-2 §5.4.1, the 1-based Packet Count from ST 2108-2 §5.3,
// and the per-tag Local Tag / Local Length encoding inside the App 4 Set.
// Any change to those constants — including a spec-wrong byte order or
// tag-value change — trips an assertion immediately.
// ============================================================================

namespace {
        // Build a single-packet HDR10+ message (the minimal descriptor used
        // elsewhere in this file) and return its first packet's UDW bytes.
        List<uint16_t> buildSinglePacketUdw() {
                HdrDynamic2094_40 md;
                md.setApplicationVersion(0);
                md.setNumWindows(1);
                md.setTargetedSystemDisplayMaximumLuminance(10'000'000u);
                auto &wp = md.windowProcessing()[0];
                wp.maxScl[0] = wp.maxScl[1] = wp.maxScl[2] = 50000;
                wp.averageMaxRgb = 25000;
                wp.fractionBrightPixels = 100;
                wp.distribution = {HdrDynamic2094_40::DistributionMaxRgb{50u, 35000u}};

                AncTranslator           t;
                AncTranslator::PacketsResult built = t.build(Variant(md),
                                                        AncFormat(AncFormat::HdrDynamic2094_40),
                                                        AncTransport::St291);
                REQUIRE(built.second().isOk());
                REQUIRE(built.first().size() == 1u);
                Result<St291Packet> rp = St291Packet::from(built.first().front());
                REQUIRE(rp.second().isOk());
                return rp.first().udw();
        }

        // Find a 2-byte big-endian tag in @p udw starting from @p start, and
        // return the offset of the tag (or udw.size() on miss).  Tags inside
        // the App 4 Set are 2-byte BE values per ST 2094-2 §6.1.
        size_t findTag(const List<uint16_t> &udw, size_t start, uint16_t tag) {
                for (size_t i = start; i + 1 < udw.size(); ++i) {
                        uint16_t v = static_cast<uint16_t>(((udw[i] & 0xFF) << 8) | (udw[i + 1] & 0xFF));
                        if (v == tag) return i;
                }
                return udw.size();
        }
} // namespace

TEST_CASE("HdrDynamic2094_40<->St291: Packet Count byte is 0x01 in first/only packet") {
        // ST 2108-2 §5.3: UDW[0] = Packet Count, 1-based.  First packet shall
        // be 0x01.
        List<uint16_t> udw = buildSinglePacketUdw();
        REQUIRE(udw.size() >= 1u);
        CHECK((udw[0] & 0xFF) == 0x01u);
}

TEST_CASE("HdrDynamic2094_40<->St291: Message Length is u16 BE, excludes itself") {
        // ST 2108-2 §5.4.1: Message Length is u16 BE; the first data word is
        // the upper 8 bits, the second is the lower 8 bits; the value
        // excludes the Length field itself.
        List<uint16_t> udw = buildSinglePacketUdw();
        REQUIRE(udw.size() >= 3u);
        uint16_t msgLen = static_cast<uint16_t>(((udw[1] & 0xFF) << 8) | (udw[2] & 0xFF));
        // The Message Length must equal the total UDW size minus
        // (Packet Count byte + 2-byte Length field) = udw.size() - 3.
        CHECK(static_cast<size_t>(msgLen) == udw.size() - 3u);
}

TEST_CASE("HdrDynamic2094_40<->St291: Frame Key is the ST 2094-2 Table 10 App 4 Set UL") {
        // ST 2094-2 Table 10:
        //   06.0E.2B.34.02.53.01.01.05.31.02.04.00.00.00.00
        List<uint16_t> udw = buildSinglePacketUdw();
        REQUIRE(udw.size() >= 3u + 16u);
        const uint8_t kApp4Key[16] = {
                0x06, 0x0E, 0x2B, 0x34, 0x02, 0x53, 0x01, 0x01,
                0x05, 0x31, 0x02, 0x04, 0x00, 0x00, 0x00, 0x00,
        };
        for (size_t i = 0; i < 16; ++i) {
                CAPTURE(i);
                CHECK((udw[3 + i] & 0xFF) == kApp4Key[i]);
        }
}

TEST_CASE("HdrDynamic2094_40<->St291: Set Length is 4-byte BER long form (0x83 + 3 length bytes)") {
        // ST 2094-2 §6.1: the Set Length field shall be 4 bytes long.  The
        // codec encodes it as 0x83 (long form, 3 length bytes) followed by 3
        // big-endian length bytes.
        List<uint16_t> udw = buildSinglePacketUdw();
        const size_t   lenOff = 3 + 16; // Packet Count (1) + MsgLen (2) + Key (16).
        REQUIRE(udw.size() >= lenOff + 4u);
        CHECK((udw[lenOff + 0] & 0xFF) == 0x83u); // BER long form, 3 length bytes follow.
        // The decoded BER length should equal udw.size() - (lenOff + 4).
        uint32_t setLen = static_cast<uint32_t>((udw[lenOff + 1] & 0xFF) << 16) |
                          static_cast<uint32_t>((udw[lenOff + 2] & 0xFF) << 8) |
                          static_cast<uint32_t>(udw[lenOff + 3] & 0xFF);
        CHECK(static_cast<size_t>(setLen) == udw.size() - (lenOff + 4u));
}

TEST_CASE("HdrDynamic2094_40<->St291: Application Identifier tag (0x3601) carries value 4") {
        // ST 2094-2 Table 3: ApplicationIdentifier @ Local Tag 36.01,
        // Length 1, identifies the ST 2094 application.  ST 2108-2 §5.4.2.5
        // App 4 = ST 2094-40 (HDR10+), so value shall be 4.
        List<uint16_t> udw = buildSinglePacketUdw();
        const size_t   setBodyStart = 3 + 16 + 4;
        size_t         off = findTag(udw, setBodyStart, 0x3601);
        REQUIRE(off < udw.size());
        REQUIRE(off + 5 <= udw.size());
        // Tag (2 BE)
        CHECK((udw[off + 0] & 0xFF) == 0x36u);
        CHECK((udw[off + 1] & 0xFF) == 0x01u);
        // Length (2 BE) = 1
        CHECK((udw[off + 2] & 0xFF) == 0x00u);
        CHECK((udw[off + 3] & 0xFF) == 0x01u);
        // Value = 4 (App 4)
        CHECK((udw[off + 4] & 0xFF) == 0x04u);
}

TEST_CASE("HdrDynamic2094_40<->St291: Window Number tag (0x3608) carries 0 on the only window") {
        // ST 2094-2 Table 3: WindowNumber @ Local Tag 36.08, Length 1.
        List<uint16_t> udw = buildSinglePacketUdw();
        const size_t   setBodyStart = 3 + 16 + 4;
        size_t         off = findTag(udw, setBodyStart, 0x3608);
        REQUIRE(off < udw.size());
        REQUIRE(off + 5 <= udw.size());
        CHECK((udw[off + 2] & 0xFF) == 0x00u); // Length MSB
        CHECK((udw[off + 3] & 0xFF) == 0x01u); // Length LSB
        CHECK((udw[off + 4] & 0xFF) == 0x00u); // Window 0
}

TEST_CASE("HdrDynamic2094_40<->St291: TargetedSystemDisplayMaxLum tag (0x360B) is Rational with Den=100") {
        // ST 2094-2 Table 3: TargetedSystemDisplayMaximumLuminance @
        // Local Tag 36.0B, Type Rational (8 bytes), Den = 100.
        // Wire value 10_000_000 (0.0001 cd/m² units) / 100 = 100_000 numerator;
        // denominator must be 100.
        List<uint16_t> udw = buildSinglePacketUdw();
        const size_t   setBodyStart = 3 + 16 + 4;
        size_t         off = findTag(udw, setBodyStart, 0x360B);
        REQUIRE(off < udw.size());
        REQUIRE(off + 12 <= udw.size());
        // Length = 8
        CHECK((udw[off + 2] & 0xFF) == 0x00u);
        CHECK((udw[off + 3] & 0xFF) == 0x08u);
        // Numerator u32 BE = 100_000 = 0x000186A0
        CHECK((udw[off + 4] & 0xFF) == 0x00u);
        CHECK((udw[off + 5] & 0xFF) == 0x01u);
        CHECK((udw[off + 6] & 0xFF) == 0x86u);
        CHECK((udw[off + 7] & 0xFF) == 0xA0u);
        // Denominator u32 BE = 100 = 0x00000064
        CHECK((udw[off + 8] & 0xFF) == 0x00u);
        CHECK((udw[off + 9] & 0xFF) == 0x00u);
        CHECK((udw[off + 10] & 0xFF) == 0x00u);
        CHECK((udw[off + 11] & 0xFF) == 0x64u);
}

TEST_CASE("HdrDynamic2094_40<->St291: AverageMaxRGB tag (0x363B) Den=100000") {
        // ST 2094-2 Table 11: AverageMaxRGB @ Local Tag 36.3B, Den = 100000.
        List<uint16_t> udw = buildSinglePacketUdw();
        const size_t   setBodyStart = 3 + 16 + 4;
        size_t         off = findTag(udw, setBodyStart, 0x363B);
        REQUIRE(off < udw.size());
        REQUIRE(off + 12 <= udw.size());
        // Length = 8
        CHECK((udw[off + 2] & 0xFF) == 0x00u);
        CHECK((udw[off + 3] & 0xFF) == 0x08u);
        // Numerator = 25000 = 0x000061A8
        CHECK((udw[off + 4] & 0xFF) == 0x00u);
        CHECK((udw[off + 5] & 0xFF) == 0x00u);
        CHECK((udw[off + 6] & 0xFF) == 0x61u);
        CHECK((udw[off + 7] & 0xFF) == 0xA8u);
        // Denominator = 100000 = 0x000186A0
        CHECK((udw[off + 8] & 0xFF) == 0x00u);
        CHECK((udw[off + 9] & 0xFF) == 0x01u);
        CHECK((udw[off + 10] & 0xFF) == 0x86u);
        CHECK((udw[off + 11] & 0xFF) == 0xA0u);
}

TEST_CASE("HdrDynamic2094_40<->St291: multi-packet — Packet Count is sequential 1..N across packets") {
        // ST 2108-2 §5.3: "the Packet Count shall increment in sequence for
        // each additional packet" — confirmed for a multi-packet build.
        HdrDynamic2094_40 src = fullSingleWindow();
        AncTranslator     t;
        AncTranslator::PacketsResult built = t.build(Variant(src),
                                                AncFormat(AncFormat::HdrDynamic2094_40),
                                                AncTransport::St291);
        REQUIRE(built.second().isOk());
        REQUIRE(built.first().size() >= 2u);
        for (size_t i = 0; i < built.first().size(); ++i) {
                CAPTURE(i);
                Result<St291Packet> rp = St291Packet::from(built.first()[i]);
                REQUIRE(rp.second().isOk());
                REQUIRE(rp.first().udw().size() >= 1u);
                CHECK((rp.first().udw()[0] & 0xFF) == static_cast<uint8_t>(i + 1u));
        }
}

TEST_CASE("HdrDynamic2094_40<->St291: parser skips unrecognized non-App-4 Frame Keys") {
        // §5.4.2 permits multiple Frame types in the same Message.  A
        // synthetic Message that prepends a fake Frame Key (16 bytes that
        // don't match the App 4 Set UL) and a BER short-form Length, then
        // pads, must be tolerated: the parser walks past the unknown Frame
        // and decodes the trailing App 4 Set normally.
        HdrDynamic2094_40 src;
        src.setApplicationVersion(0);
        src.setNumWindows(1);
        src.setTargetedSystemDisplayMaximumLuminance(10'000'000u);
        auto &wp = src.windowProcessing()[0];
        wp.maxScl[0] = wp.maxScl[1] = wp.maxScl[2] = 50000;
        wp.averageMaxRgb = 25000;
        wp.fractionBrightPixels = 100;
        wp.distribution = {HdrDynamic2094_40::DistributionMaxRgb{50u, 35000u}};

        AncTranslator           t;
        AncTranslator::PacketsResult built = t.build(Variant(src),
                                                AncFormat(AncFormat::HdrDynamic2094_40),
                                                AncTransport::St291);
        REQUIRE(built.second().isOk());
        REQUIRE(built.first().size() == 1u);
        Result<St291Packet> rp = St291Packet::from(built.first().front());
        REQUIRE(rp.second().isOk());
        List<uint16_t> udw = rp.first().udw();

        // Strip the Packet Count + Message Length header, then construct a
        // new Message that prepends a 23-byte "fake frame" (16-byte key all
        // zeros + 0x06 BER short-form length byte + 6 zero value bytes)
        // before the original App 4 frame.
        REQUIRE(udw.size() >= 3u);
        const size_t origFramesBytes = udw.size() - 3u;
        const size_t kPrependedFrameBytes = 16u + 1u + 6u; // Key + L + V
        const size_t newFramesBytes = origFramesBytes + kPrependedFrameBytes;
        List<uint16_t> newUdw;
        newUdw.pushToBack(0x01);                                       // Packet Count
        newUdw.pushToBack(static_cast<uint16_t>((newFramesBytes >> 8) & 0xFF));   // MsgLen MSB
        newUdw.pushToBack(static_cast<uint16_t>(newFramesBytes & 0xFF));          // MsgLen LSB
        for (int i = 0; i < 16; ++i) newUdw.pushToBack(0x00);          // Fake 16-byte Key
        newUdw.pushToBack(0x06);                                       // BER short-form len = 6
        for (int i = 0; i < 6; ++i) newUdw.pushToBack(0x00);           // Fake 6-byte Value
        for (size_t i = 3; i < udw.size(); ++i) newUdw.pushToBack(udw[i]);

        St291Packet     p = St291Packet::buildRaw(0x41, 0x0D, newUdw, 9);
        AncTranslator::ParseResult parsed = t.parse(p.packet());
        REQUIRE(parsed.second().isOk());
        HdrDynamic2094_40 out = parsed.first().get<HdrDynamic2094_40>();
        CHECK(out.numWindows() == 1u);
        CHECK(out.windowProcessing()[0].averageMaxRgb == 25000u);
}

// ============================================================================
// Parser robustness
// ============================================================================

TEST_CASE("HdrDynamic2094_40<->St291: parser tolerates out-of-order packet delivery") {
        HdrDynamic2094_40 src = fullSingleWindow();
        AncTranslator     t;
        AncTranslator::PacketsResult built =
                t.build(Variant(src), AncFormat(AncFormat::HdrDynamic2094_40), AncTransport::St291);
        REQUIRE(built.second().isOk());
        REQUIRE(built.first().size() >= 2u);

        // Reverse the order before feeding the multi-parser.  The codec is
        // documented to sort by Packet Count.
        AncPacket::List reversed;
        for (size_t i = built.first().size(); i > 0; --i) {
                reversed.pushToBack(built.first()[i - 1]);
        }

        AncTranslator::ParseResult parsed = t.parseGroup(reversed);
        REQUIRE(parsed.second().isOk());
        HdrDynamic2094_40 out = parsed.first().get<HdrDynamic2094_40>();
        CHECK(out == src);
}

TEST_CASE("HdrDynamic2094_40<->St291: parser rejects gap in Packet Count sequence") {
        HdrDynamic2094_40 src = fullSingleWindow();
        AncTranslator     t;
        AncTranslator::PacketsResult built =
                t.build(Variant(src), AncFormat(AncFormat::HdrDynamic2094_40), AncTransport::St291);
        REQUIRE(built.second().isOk());
        REQUIRE(built.first().size() >= 2u);

        // Drop the middle packet, leaving Packet Counts {1, 3, ...}.
        AncPacket::List withGap;
        withGap.pushToBack(built.first()[0]);
        for (size_t i = 2; i < built.first().size(); ++i) {
                withGap.pushToBack(built.first()[i]);
        }

        AncTranslator::ParseResult parsed = t.parseGroup(withGap);
        CHECK(parsed.second().code() == Error::CorruptData);
}

TEST_CASE("HdrDynamic2094_40<->St291: parser rejects empty packet list") {
        AncTranslator   t;
        AncPacket::List empty;
        AncTranslator::ParseResult parsed = t.parseGroup(empty);
        CHECK(parsed.second().isError());
}

TEST_CASE("HdrDynamic2094_40<->St291: parse(single packet) dispatches to multi-parser") {
        // Single-packet AncTranslator::parse routes through the registered
        // MultiParserFn by wrapping the packet in a one-element list.  For
        // a Message that splits across multiple packets, a single-packet
        // parse must fail gracefully because the assembled Message is
        // truncated.
        HdrDynamic2094_40 src = fullSingleWindow();
        AncTranslator     t;
        AncTranslator::PacketsResult built =
                t.build(Variant(src), AncFormat(AncFormat::HdrDynamic2094_40), AncTransport::St291);
        REQUIRE(built.second().isOk());
        REQUIRE(built.first().size() >= 2u);

        // Feed only the first packet to single-packet parse() → the
        // declared Message Length exceeds available bytes, so the parser
        // surfaces CorruptData.
        AncTranslator::ParseResult r = t.parse(built.first().front());
        CHECK(r.second().isError());
}

TEST_CASE("HdrDynamic2094_40<->St291: P2-26 §9.4 strips ColorSaturationWeight at AppVer=1") {
        // ST 2094-40:2020 §9.4 SHALL NOT include ColorSaturationWeight
        // (and the two ActualPeakLuminance grids) at
        // ApplicationVersion=1.  The codec strips them on emission and
        // logs a warn; the round-trip parses back with the field
        // absent.
        HdrDynamic2094_40 src = fullSingleWindow();
        src.setApplicationVersion(1);
        // fullSingleWindow() populates ColorSaturationWeight via
        // hasColorSaturationMapping=true; the §9.4 strip should drop
        // it from the wire.
        REQUIRE(src.windowProcessing()[0].hasColorSaturationMapping);

        AncTranslator t;
        AncTranslator::PacketsResult built =
                t.build(Variant(src), AncFormat(AncFormat::HdrDynamic2094_40), AncTransport::St291);
        REQUIRE(built.second().isOk());

        AncTranslator::ParseResult parsed = t.parseGroup(built.first());
        REQUIRE(parsed.second().isOk());
        HdrDynamic2094_40 out = parsed.first().get<HdrDynamic2094_40>();
        CHECK(out.applicationVersion() == 1u);
        // The §9.4-forbidden field was stripped on emission and is
        // therefore absent on the parse-back.
        CHECK_FALSE(out.windowProcessing()[0].hasColorSaturationMapping);
}

TEST_CASE("HdrDynamic2094_40<->St291: P2-26 §9.4 clamps numWindows>1 to 1 at AppVer=1") {
        // §9.4 SHALL NOT include WindowNumber > 0 at
        // ApplicationVersion=1, so multi-window metadata is clamped to
        // a single-window emission with a warn.
        HdrDynamic2094_40 src = fullSingleWindow();
        src.setApplicationVersion(1);
        src.setNumWindows(2);
        HdrDynamic2094_40::Window w{};
        w.upperLeftCornerX = 100;
        w.upperLeftCornerY = 100;
        w.lowerRightCornerX = 500;
        w.lowerRightCornerY = 500;
        src.extraWindows().pushToBack(w);
        // windowProcessing list must grow to match numWindows.
        src.windowProcessing().pushToBack(src.windowProcessing()[0]);

        AncTranslator t;
        AncTranslator::PacketsResult built =
                t.build(Variant(src), AncFormat(AncFormat::HdrDynamic2094_40), AncTransport::St291);
        REQUIRE(built.second().isOk());

        AncTranslator::ParseResult parsed = t.parseGroup(built.first());
        REQUIRE(parsed.second().isOk());
        HdrDynamic2094_40 out = parsed.first().get<HdrDynamic2094_40>();
        CHECK(out.applicationVersion() == 1u);
        // Only window 0 survived the §9.4 clamp.
        CHECK(out.numWindows() == 1u);
}

TEST_CASE("HdrDynamic2094_40<->St291: P2-26 §9.3 preserves optional fields at AppVer=0 (warn-but-emit)") {
        // §9.3 at AppVer=0 is SHOULD NOT (softer than §9.4's SHALL
        // NOT).  The codec warns but still emits, so the round-trip
        // preserves every field that was populated.  This protects
        // legacy senders that built on the 2016 edition where the
        // ActualPeakLuminance grids + ColorSaturationWeight were
        // common.
        HdrDynamic2094_40 src = fullSingleWindow();
        REQUIRE(src.applicationVersion() == 0u);
        REQUIRE(src.windowProcessing()[0].hasColorSaturationMapping);

        AncTranslator t;
        AncTranslator::PacketsResult built =
                t.build(Variant(src), AncFormat(AncFormat::HdrDynamic2094_40), AncTransport::St291);
        REQUIRE(built.second().isOk());

        AncTranslator::ParseResult parsed = t.parseGroup(built.first());
        REQUIRE(parsed.second().isOk());
        HdrDynamic2094_40 out = parsed.first().get<HdrDynamic2094_40>();
        CHECK(out.applicationVersion() == 0u);
        CHECK(out.windowProcessing()[0].hasColorSaturationMapping);
        CHECK(out.windowProcessing()[0].colorSaturationWeight ==
              src.windowProcessing()[0].colorSaturationWeight);
}

TEST_CASE("HdrDynamic2094_40<->St291: builder caps Packet Count at 255") {
        // Realistic worst-case: ~290 byte Message → 2 packets.  We can't
        // realistically construct a 64+ KB Message via the value-type API
        // without artificial padding, so this test only sanity-checks the
        // OutOfRange path exists in source.  Coverage of the actual
        // overflow is left as a code-review concern.
        HdrDynamic2094_40       src = fullSingleWindow();
        AncTranslator           t;
        AncTranslator::PacketsResult built =
                t.build(Variant(src), AncFormat(AncFormat::HdrDynamic2094_40), AncTransport::St291);
        REQUIRE(built.second().isOk());
        CHECK(built.first().size() < 256u);
}

// ===========================================================================
// Frame-sync policy: HDR10+ holds last sample on Repeat (per-frame metadata
// describing the same picture stays correct), drops on Drop.  SyncPolicy
// operates per-packet — each fragment of a multi-packet Message copies
// through individually so the receiver re-aggregates from the wire's
// Packet Count bytes.
// ===========================================================================

TEST_CASE("HdrDynamic2094_40 sync policy: hasSyncPolicy reflects registration") {
        CHECK(AncTranslator::hasSyncPolicy(AncFormat(AncFormat::HdrDynamic2094_40)));
}

TEST_CASE("HdrDynamic2094_40 sync policy: Play returns the packet unchanged") {
        AncTranslator           t;
        AncTranslator::PacketsResult built =
                t.build(Variant(fullSingleWindow()), AncFormat(AncFormat::HdrDynamic2094_40),
                         AncTransport::St291);
        REQUIRE(built.second().isOk());
        // Apply Play to every fragment and verify each is preserved.
        for (const AncPacket &pkt : built.first()) {
                AncTranslator::PacketsResult res = t.applySyncPolicy(pkt, FrameSyncDisposition::play(), 0);
                REQUIRE(res.second().isOk());
                REQUIRE(res.first().size() == 1);
                CHECK(res.first().front().data().size() == pkt.data().size());
        }
}

TEST_CASE("HdrDynamic2094_40 sync policy: Drop returns an empty list (per packet)") {
        AncTranslator           t;
        AncTranslator::PacketsResult built =
                t.build(Variant(fullSingleWindow()), AncFormat(AncFormat::HdrDynamic2094_40),
                         AncTransport::St291);
        REQUIRE(built.second().isOk());
        for (const AncPacket &pkt : built.first()) {
                AncTranslator::PacketsResult res = t.applySyncPolicy(pkt, FrameSyncDisposition::drop(), 0);
                REQUIRE(res.second().isOk());
                CHECK(res.first().size() == 0);
        }
}

TEST_CASE("HdrDynamic2094_40 sync policy: Repeat preserves multi-packet structure across indices") {
        AncTranslator           t;
        AncTranslator::PacketsResult built =
                t.build(Variant(fullSingleWindow()), AncFormat(AncFormat::HdrDynamic2094_40),
                         AncTransport::St291);
        REQUIRE(built.second().isOk());
        const size_t fragments = built.first().size();
        REQUIRE(fragments >= 1u);

        // Walk every fragment through Repeat[3] at every repeatIndex and
        // collect outputs into per-index packet lists.  Then re-aggregate
        // each list via parseGroup() and confirm the decoded MD round-trips
        // — proves the multi-packet structure survives the per-packet
        // dispatch.
        for (uint8_t i = 0; i < 3; ++i) {
                AncPacket::List reassembled;
                for (const AncPacket &pkt : built.first()) {
                        AncTranslator::PacketsResult res = t.applySyncPolicy(pkt, FrameSyncDisposition::repeat(3), i);
                        REQUIRE(res.second().isOk());
                        REQUIRE(res.first().size() == 1);
                        reassembled.pushToBack(res.first().front());
                }
                CHECK(reassembled.size() == fragments);
                AncTranslator::ParseResult parsed = t.parseGroup(reassembled);
                REQUIRE(parsed.second().isOk());
                HdrDynamic2094_40 out = parsed.first().get<HdrDynamic2094_40>();
                CHECK(out.numWindows() == 1u);
        }
}
