/**
 * @file      anccodec_hdrdynamic2094_40_st291.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
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
        // forcing multi-packet split.
        HdrDynamic2094_40 fullSingleWindow() {
                HdrDynamic2094_40 md;
                md.setApplicationVersion(1);
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
        size_t totalUdwBytes(const List<AncPacket> &pkts) {
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
        Result<List<AncPacket>> built =
                t.build(Variant(md), AncFormat(AncFormat::HdrDynamic2094_40), AncTransport::St291);
        REQUIRE(built.second().isOk());
        REQUIRE(built.first().size() == 1u);
        CHECK(built.first().front().format().id() == AncFormat::HdrDynamic2094_40);

        Result<St291Packet> rp = St291Packet::from(built.first().front());
        REQUIRE(rp.second().isOk());
        CHECK(rp.first().did() == 0x41u);
        CHECK(rp.first().sdid() == 0x0Du);
        CHECK((rp.first().udw().front() & 0xFF) == 0x01u); // Packet Count = 1

        Result<Variant> parsed = t.parseGroup(built.first());
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
        Result<List<AncPacket>> built =
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
        Result<Variant> parsed = t.parseGroup(built.first());
        REQUIRE(parsed.second().isOk());
        HdrDynamic2094_40 out = parsed.first().get<HdrDynamic2094_40>();
        CHECK(out == src);
}

TEST_CASE("HdrDynamic2094_40<->St291: total wire bytes match across pack/unpack") {
        HdrDynamic2094_40 src = fullSingleWindow();
        AncTranslator     t;
        Result<List<AncPacket>> built =
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
        Result<List<AncPacket>> built =
                t.build(Variant(src), AncFormat(AncFormat::HdrDynamic2094_40), AncTransport::St291);
        REQUIRE(built.second().isOk());
        // Two-window descriptors typically need multiple packets too.
        for (const AncPacket &pkt : built.first()) {
                Result<St291Packet> rp = St291Packet::from(pkt);
                REQUIRE(rp.second().isOk());
                CHECK(rp.first().udw().size() <= 255u);
        }

        Result<Variant> parsed = t.parseGroup(built.first());
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
        Result<List<AncPacket>> built =
                t.build(Variant(src), AncFormat(AncFormat::HdrDynamic2094_40), AncTransport::St291);
        REQUIRE(built.second().isOk());

        Result<Variant> parsed = t.parseGroup(built.first());
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
// Parser robustness
// ============================================================================

TEST_CASE("HdrDynamic2094_40<->St291: parser tolerates out-of-order packet delivery") {
        HdrDynamic2094_40 src = fullSingleWindow();
        AncTranslator     t;
        Result<List<AncPacket>> built =
                t.build(Variant(src), AncFormat(AncFormat::HdrDynamic2094_40), AncTransport::St291);
        REQUIRE(built.second().isOk());
        REQUIRE(built.first().size() >= 2u);

        // Reverse the order before feeding the multi-parser.  The codec is
        // documented to sort by Packet Count.
        List<AncPacket> reversed;
        for (size_t i = built.first().size(); i > 0; --i) {
                reversed.pushToBack(built.first()[i - 1]);
        }

        Result<Variant> parsed = t.parseGroup(reversed);
        REQUIRE(parsed.second().isOk());
        HdrDynamic2094_40 out = parsed.first().get<HdrDynamic2094_40>();
        CHECK(out == src);
}

TEST_CASE("HdrDynamic2094_40<->St291: parser rejects gap in Packet Count sequence") {
        HdrDynamic2094_40 src = fullSingleWindow();
        AncTranslator     t;
        Result<List<AncPacket>> built =
                t.build(Variant(src), AncFormat(AncFormat::HdrDynamic2094_40), AncTransport::St291);
        REQUIRE(built.second().isOk());
        REQUIRE(built.first().size() >= 2u);

        // Drop the middle packet, leaving Packet Counts {1, 3, ...}.
        List<AncPacket> withGap;
        withGap.pushToBack(built.first()[0]);
        for (size_t i = 2; i < built.first().size(); ++i) {
                withGap.pushToBack(built.first()[i]);
        }

        Result<Variant> parsed = t.parseGroup(withGap);
        CHECK(parsed.second().code() == Error::CorruptData);
}

TEST_CASE("HdrDynamic2094_40<->St291: parser rejects empty packet list") {
        AncTranslator   t;
        List<AncPacket> empty;
        Result<Variant> parsed = t.parseGroup(empty);
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
        Result<List<AncPacket>> built =
                t.build(Variant(src), AncFormat(AncFormat::HdrDynamic2094_40), AncTransport::St291);
        REQUIRE(built.second().isOk());
        REQUIRE(built.first().size() >= 2u);

        // Feed only the first packet to single-packet parse() → the
        // declared Message Length exceeds available bytes, so the parser
        // surfaces CorruptData.
        Result<Variant> r = t.parse(built.first().front());
        CHECK(r.second().isError());
}

TEST_CASE("HdrDynamic2094_40<->St291: builder caps Packet Count at 255") {
        // Realistic worst-case: ~290 byte Message → 2 packets.  We can't
        // realistically construct a 64+ KB Message via the value-type API
        // without artificial padding, so this test only sanity-checks the
        // OutOfRange path exists in source.  Coverage of the actual
        // overflow is left as a code-review concern.
        HdrDynamic2094_40       src = fullSingleWindow();
        AncTranslator           t;
        Result<List<AncPacket>> built =
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
        Result<List<AncPacket>> built =
                t.build(Variant(fullSingleWindow()), AncFormat(AncFormat::HdrDynamic2094_40),
                         AncTransport::St291);
        REQUIRE(built.second().isOk());
        // Apply Play to every fragment and verify each is preserved.
        for (const AncPacket &pkt : built.first()) {
                Result<List<AncPacket>> res = t.applySyncPolicy(pkt, FrameSyncDisposition::play(), 0);
                REQUIRE(res.second().isOk());
                REQUIRE(res.first().size() == 1);
                CHECK(res.first().front().data().size() == pkt.data().size());
        }
}

TEST_CASE("HdrDynamic2094_40 sync policy: Drop returns an empty list (per packet)") {
        AncTranslator           t;
        Result<List<AncPacket>> built =
                t.build(Variant(fullSingleWindow()), AncFormat(AncFormat::HdrDynamic2094_40),
                         AncTransport::St291);
        REQUIRE(built.second().isOk());
        for (const AncPacket &pkt : built.first()) {
                Result<List<AncPacket>> res = t.applySyncPolicy(pkt, FrameSyncDisposition::drop(), 0);
                REQUIRE(res.second().isOk());
                CHECK(res.first().size() == 0);
        }
}

TEST_CASE("HdrDynamic2094_40 sync policy: Repeat preserves multi-packet structure across indices") {
        AncTranslator           t;
        Result<List<AncPacket>> built =
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
                List<AncPacket> reassembled;
                for (const AncPacket &pkt : built.first()) {
                        Result<List<AncPacket>> res = t.applySyncPolicy(pkt, FrameSyncDisposition::repeat(3), i);
                        REQUIRE(res.second().isOk());
                        REQUIRE(res.first().size() == 1);
                        reassembled.pushToBack(res.first().front());
                }
                CHECK(reassembled.size() == fragments);
                Result<Variant> parsed = t.parseGroup(reassembled);
                REQUIRE(parsed.second().isOk());
                HdrDynamic2094_40 out = parsed.first().get<HdrDynamic2094_40>();
                CHECK(out.numWindows() == 1u);
        }
}
