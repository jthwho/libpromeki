/**
 * @file      anccodec_op47sdp.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/ancformat.h>
#include <promeki/ancop47sdp.h>
#include <promeki/ancpacket.h>
#include <promeki/anctranslateconfig.h>
#include <promeki/anctranslator.h>
#include <promeki/bufferiodevice.h>
#include <promeki/datastream.h>
#include <promeki/framesyncdisposition.h>
#include <promeki/result.h>
#include <promeki/st291packet.h>
#include <promeki/variant.h>

using namespace promeki;

namespace {

        // Builds a fully populated VbiPacket with deterministic but
        // recognisable WST bytes so a byte-exact compare proves the
        // 45-byte Structure B survives a round-trip.
        AncOp47Sdp::VbiPacket makeVbiPacket(uint8_t line, bool fieldOne, uint8_t seed) {
                AncOp47Sdp::VbiPacket p;
                p.lineNumber = line;
                p.fieldOne = fieldOne;
                p.wstData[0] = AncOp47Sdp::RunInCode;
                p.wstData[1] = AncOp47Sdp::RunInCode;
                p.wstData[2] = AncOp47Sdp::FramingCode;
                for (size_t i = 3; i < AncOp47Sdp::WstPacketSize; ++i) {
                        p.wstData[i] = static_cast<uint8_t>(seed + i);
                }
                return p;
        }

        // Decodes the UDW byte stream from an emitted SDP packet so
        // tests can probe wire bytes without re-implementing the
        // ST 291 layer's 10-bit packing.
        List<uint8_t> udwBytesOf(const AncPacket &pkt) {
                List<uint8_t>       out;
                Result<St291Packet> rp = St291Packet::from(pkt);
                REQUIRE(rp.second().isOk());
                for (uint16_t w : rp.first().udw()) {
                        out.pushToBack(static_cast<uint8_t>(w & 0xFFu));
                }
                return out;
        }

} // namespace

// ============================================================================
// Empty SDP (no carried packets) — smallest legal shape.
// ============================================================================

TEST_CASE("Op47Sdp<->St291: empty SDP round-trip preserves FSC and produces 13-byte UDW") {
        AncTranslator t;
        AncOp47Sdp    input;
        input.setFooterSequenceCounter(0xABCD);

        AncTranslator::PacketsResult built = t.build(Variant(input), AncFormat(AncFormat::Op47Sdp),
                                                AncTransport::St291);
        REQUIRE(built.second().isOk());
        REQUIRE(built.first().size() == 1);
        CHECK(built.first().front().format().id() == AncFormat::Op47Sdp);
        CHECK(built.first().front().transport() == AncTransport::St291);

        Result<St291Packet> rp = St291Packet::from(built.first().front());
        REQUIRE(rp.second().isOk());
        CHECK(rp.first().did() == 0x43);
        CHECK(rp.first().sdid() == 0x02);
        CHECK(rp.first().dataCount() == 13);
        CHECK(rp.first().checksumValid());

        AncTranslator::ParseResult parsed = t.parse(built.first().front());
        REQUIRE(parsed.second().isOk());
        AncOp47Sdp back = parsed.first().get<AncOp47Sdp>();
        CHECK(back == input);
        CHECK(back.packets().size() == 0);
        CHECK(back.footerSequenceCounter() == 0xABCD);
}

// ============================================================================
// Single carried packet — verify the LENGTH field equals 13 + 45.
// ============================================================================

TEST_CASE("Op47Sdp<->St291: one carried VBI packet round-trips byte-exact") {
        AncTranslator t;
        AncOp47Sdp    input;
        input.setFooterSequenceCounter(0x0001);
        input.addPacket(makeVbiPacket(9, true, 0x10));

        AncTranslator::PacketsResult built = t.build(Variant(input), AncFormat(AncFormat::Op47Sdp),
                                                AncTransport::St291);
        REQUIRE(built.second().isOk());

        Result<St291Packet> rp = St291Packet::from(built.first().front());
        REQUIRE(rp.second().isOk());
        CHECK(rp.first().dataCount() == 13 + 45);

        AncTranslator::ParseResult parsed = t.parse(built.first().front());
        REQUIRE(parsed.second().isOk());
        AncOp47Sdp back = parsed.first().get<AncOp47Sdp>();
        CHECK(back == input);
        REQUIRE(back.packets().size() == 1);
        CHECK(back.packets()[0].lineNumber == 9);
        CHECK(back.packets()[0].fieldOne == true);
        CHECK(back.packets()[0].wstData == input.packets()[0].wstData);
}

// ============================================================================
// Maximum (5) carried packets — verify the LENGTH field equals 13 + 5*45.
// ============================================================================

TEST_CASE("Op47Sdp<->St291: five carried VBI packets round-trip and consume 238 UDW bytes") {
        AncTranslator t;
        AncOp47Sdp    input;
        input.setFooterSequenceCounter(0x1234);
        for (uint8_t i = 0; i < AncOp47Sdp::MaxVbiPackets; ++i) {
                input.addPacket(
                        makeVbiPacket(static_cast<uint8_t>(7 + i), (i & 1) == 0, static_cast<uint8_t>(i * 17)));
        }

        AncTranslator::PacketsResult built = t.build(Variant(input), AncFormat(AncFormat::Op47Sdp),
                                                AncTransport::St291);
        REQUIRE(built.second().isOk());

        Result<St291Packet> rp = St291Packet::from(built.first().front());
        REQUIRE(rp.second().isOk());
        CHECK(rp.first().dataCount() == 13 + 5 * 45);

        AncTranslator::ParseResult parsed = t.parse(built.first().front());
        REQUIRE(parsed.second().isOk());
        AncOp47Sdp back = parsed.first().get<AncOp47Sdp>();
        CHECK(back == input);
        CHECK(back.packets().size() == 5);
}

// ============================================================================
// Field bit + reserved bits — descriptor byte b5..b7 round-trip.
// ============================================================================

TEST_CASE("Op47Sdp<->St291: field-one bit (descriptor b7) round-trips") {
        AncTranslator t;
        AncOp47Sdp    input;
        AncOp47Sdp::VbiPacket pOdd = makeVbiPacket(11, true, 0x20);   // field 1 / odd
        AncOp47Sdp::VbiPacket pEven = makeVbiPacket(11, false, 0x20); // field 2 / even
        input.addPacket(pOdd);
        input.addPacket(pEven);

        AncTranslator::PacketsResult built = t.build(Variant(input), AncFormat(AncFormat::Op47Sdp),
                                                AncTransport::St291);
        REQUIRE(built.second().isOk());

        // Descriptors live at UDW indices 4..8.  The first carries
        // field-one (b7 set, line 11), the second carries field-two
        // (b7 clear, line 11).
        List<uint8_t> udw = udwBytesOf(built.first().front());
        CHECK(udw[4] == static_cast<uint8_t>(AncOp47Sdp::FieldOneBit | 11));
        CHECK(udw[5] == static_cast<uint8_t>(11));

        AncTranslator::ParseResult parsed = t.parse(built.first().front());
        REQUIRE(parsed.second().isOk());
        AncOp47Sdp back = parsed.first().get<AncOp47Sdp>();
        CHECK(back == input);
}

TEST_CASE("Op47Sdp<->St291: reserved descriptor bits (b5, b6) round-trip") {
        AncTranslator         t;
        AncOp47Sdp            input;
        AncOp47Sdp::VbiPacket p = makeVbiPacket(15, false, 0x30);
        // bit 0 of reservedBits → descriptor b5, bit 1 → b6
        p.reservedBits = 0x03;
        input.addPacket(p);

        AncTranslator::PacketsResult built = t.build(Variant(input), AncFormat(AncFormat::Op47Sdp),
                                                AncTransport::St291);
        REQUIRE(built.second().isOk());
        List<uint8_t> udw = udwBytesOf(built.first().front());
        // Descriptor byte at UDW[4]: line 15 + reserved b5=1, b6=1.
        CHECK(udw[4] == static_cast<uint8_t>(15 | 0x60));

        AncTranslator::ParseResult parsed = t.parse(built.first().front());
        REQUIRE(parsed.second().isOk());
        AncOp47Sdp back = parsed.first().get<AncOp47Sdp>();
        CHECK(back == input);
        CHECK(back.packets()[0].reservedBits == 0x03);
}

// ============================================================================
// RDD 8 §5.1 byte-position checks — header / descriptors / footer.
// ============================================================================

TEST_CASE("Op47Sdp<->St291: wire bytes match RDD 8 §5.1 fixed positions") {
        AncTranslator t;
        AncOp47Sdp    input;
        input.setFooterSequenceCounter(0xBEEF);
        input.addPacket(makeVbiPacket(10, true, 0x40));

        AncTranslator::PacketsResult built = t.build(Variant(input), AncFormat(AncFormat::Op47Sdp),
                                                AncTransport::St291);
        REQUIRE(built.second().isOk());
        List<uint8_t> udw = udwBytesOf(built.first().front());

        // Identifiers + LENGTH + FORMAT CODE.
        CHECK(udw[0] == AncOp47Sdp::Identifier1);
        CHECK(udw[1] == AncOp47Sdp::Identifier2);
        CHECK(udw[2] == static_cast<uint8_t>(udw.size())); // LENGTH = full byte count
        CHECK(udw[3] == AncOp47Sdp::FormatCodeWstTeletext);

        // Descriptors: first non-zero, remaining four = 0.
        CHECK(udw[4] != 0);
        CHECK(udw[5] == 0);
        CHECK(udw[6] == 0);
        CHECK(udw[7] == 0);
        CHECK(udw[8] == 0);

        // Footer ID at offset 9 + 45*N - here N=1 so offset = 54.
        const size_t footerOffset = 9 + 45;
        CHECK(udw[footerOffset] == AncOp47Sdp::FooterId);
        CHECK(udw[footerOffset + 1] == 0xBE); // FSC MSB
        CHECK(udw[footerOffset + 2] == 0xEF); // FSC LSB

        // SDP CHECKSUM — full-UDW arithmetic sum mod 256 must be zero.
        uint32_t sum = 0;
        for (uint8_t b : udw) sum += b;
        CHECK((sum & 0xFF) == 0);
}

// ============================================================================
// RDD 8 §5.3 SDP CHECKSUM — recompute should land exactly on the wire byte.
// ============================================================================

TEST_CASE("Op47Sdp<->St291: SDP CHECKSUM is stamped per RDD 8 §5.3") {
        AncTranslator t;
        AncOp47Sdp    input;
        input.setFooterSequenceCounter(0x0042);
        input.addPacket(makeVbiPacket(22, false, 0x50));

        AncTranslator::PacketsResult built = t.build(Variant(input), AncFormat(AncFormat::Op47Sdp),
                                                AncTransport::St291);
        REQUIRE(built.second().isOk());
        List<uint8_t> udw = udwBytesOf(built.first().front());

        // Re-derive what the wire's SDP CHECKSUM byte should be from
        // the prefix (everything but the last byte) and compare.
        uint32_t prefixSum = 0;
        for (size_t i = 0; i + 1 < udw.size(); ++i) prefixSum += udw[i];
        uint8_t expected = static_cast<uint8_t>((0u - prefixSum) & 0xFFu);
        CHECK(udw.back() == expected);
}

// ============================================================================
// RDD 8 §5.2 FSC — Footer Sequence Counter big-endian on the wire.
// ============================================================================

TEST_CASE("Op47Sdp<->St291: FSC is written MSB-first per RDD 8 §5.2") {
        AncTranslator t;
        AncOp47Sdp    input;
        input.setFooterSequenceCounter(0x1234);

        AncTranslator::PacketsResult built = t.build(Variant(input), AncFormat(AncFormat::Op47Sdp),
                                                AncTransport::St291);
        REQUIRE(built.second().isOk());
        List<uint8_t> udw = udwBytesOf(built.first().front());

        // No packets → footer starts at offset 9.
        CHECK(udw[9] == AncOp47Sdp::FooterId);
        CHECK(udw[10] == 0x12); // MSB
        CHECK(udw[11] == 0x34); // LSB
}

// ============================================================================
// Wire rejection — corrupted identifier / format code / length / footer.
// ============================================================================

TEST_CASE("Op47Sdp<->St291: parse rejects wrong identifiers") {
        AncTranslator t;
        AncOp47Sdp    input;
        input.addPacket(makeVbiPacket(13, true, 0x60));
        AncTranslator::PacketsResult built = t.build(Variant(input), AncFormat(AncFormat::Op47Sdp),
                                                AncTransport::St291);
        REQUIRE(built.second().isOk());

        List<uint8_t>     udw = udwBytesOf(built.first().front());
        // Flip IDENTIFIER 1 to a junk byte; re-stamp checksum to keep
        // the sum-mod-256-zero invariant so the parse failure is
        // genuinely the identifier check rather than the checksum.
        udw[0] = 0x00;
        uint32_t sum = 0;
        for (size_t i = 0; i + 1 < udw.size(); ++i) sum += udw[i];
        udw[udw.size() - 1] = static_cast<uint8_t>((0u - sum) & 0xFFu);

        List<uint16_t> udw16;
        for (uint8_t b : udw) udw16.pushToBack(b);
        St291Packet      rebuilt = St291Packet::build(AncFormat(AncFormat::Op47Sdp), udw16, 0x7FE,
                                                       St291Packet::UnspecifiedHOffset, false);
        AncTranslator::ParseResult  parsed = t.parse(rebuilt.packet());
        REQUIRE(parsed.second().isError());
        CHECK(parsed.second().code() == Error::CorruptData);
}

TEST_CASE("Op47Sdp<->St291: parse rejects wrong FORMAT CODE") {
        AncTranslator t;
        AncOp47Sdp    input;
        AncTranslator::PacketsResult built = t.build(Variant(input), AncFormat(AncFormat::Op47Sdp),
                                                AncTransport::St291);
        REQUIRE(built.second().isOk());
        List<uint8_t> udw = udwBytesOf(built.first().front());
        udw[3] = 0x55; // non-WST format code
        uint32_t sum = 0;
        for (size_t i = 0; i + 1 < udw.size(); ++i) sum += udw[i];
        udw[udw.size() - 1] = static_cast<uint8_t>((0u - sum) & 0xFFu);

        List<uint16_t> udw16;
        for (uint8_t b : udw) udw16.pushToBack(b);
        St291Packet      rebuilt = St291Packet::build(AncFormat(AncFormat::Op47Sdp), udw16, 0x7FE,
                                                       St291Packet::UnspecifiedHOffset, false);
        AncTranslator::ParseResult  parsed = t.parse(rebuilt.packet());
        REQUIRE(parsed.second().isError());
        CHECK(parsed.second().code() == Error::CorruptData);
}

TEST_CASE("Op47Sdp<->St291: parse rejects LENGTH that disagrees with DC") {
        AncTranslator t;
        AncOp47Sdp    input;
        AncTranslator::PacketsResult built = t.build(Variant(input), AncFormat(AncFormat::Op47Sdp),
                                                AncTransport::St291);
        REQUIRE(built.second().isOk());
        List<uint8_t> udw = udwBytesOf(built.first().front());
        udw[2] = static_cast<uint8_t>(udw.size() + 1); // wrong declared LENGTH
        uint32_t sum = 0;
        for (size_t i = 0; i + 1 < udw.size(); ++i) sum += udw[i];
        udw[udw.size() - 1] = static_cast<uint8_t>((0u - sum) & 0xFFu);

        List<uint16_t> udw16;
        for (uint8_t b : udw) udw16.pushToBack(b);
        St291Packet      rebuilt = St291Packet::build(AncFormat(AncFormat::Op47Sdp), udw16, 0x7FE,
                                                       St291Packet::UnspecifiedHOffset, false);
        AncTranslator::ParseResult  parsed = t.parse(rebuilt.packet());
        REQUIRE(parsed.second().isError());
        CHECK(parsed.second().code() == Error::CorruptData);
}

TEST_CASE("Op47Sdp<->St291: parse rejects wrong FOOTER ID") {
        AncTranslator t;
        AncOp47Sdp    input;
        AncTranslator::PacketsResult built = t.build(Variant(input), AncFormat(AncFormat::Op47Sdp),
                                                AncTransport::St291);
        REQUIRE(built.second().isOk());
        List<uint8_t> udw = udwBytesOf(built.first().front());
        udw[9] = 0x73; // wrong footer id (right ID is 0x74)
        uint32_t sum = 0;
        for (size_t i = 0; i + 1 < udw.size(); ++i) sum += udw[i];
        udw[udw.size() - 1] = static_cast<uint8_t>((0u - sum) & 0xFFu);

        List<uint16_t> udw16;
        for (uint8_t b : udw) udw16.pushToBack(b);
        St291Packet      rebuilt = St291Packet::build(AncFormat(AncFormat::Op47Sdp), udw16, 0x7FE,
                                                       St291Packet::UnspecifiedHOffset, false);
        AncTranslator::ParseResult  parsed = t.parse(rebuilt.packet());
        REQUIRE(parsed.second().isError());
        CHECK(parsed.second().code() == Error::CorruptData);
}

TEST_CASE("Op47Sdp<->St291: parse rejects corrupted SDP CHECKSUM") {
        AncTranslator t;
        AncOp47Sdp    input;
        AncTranslator::PacketsResult built = t.build(Variant(input), AncFormat(AncFormat::Op47Sdp),
                                                AncTransport::St291);
        REQUIRE(built.second().isOk());
        List<uint8_t> udw = udwBytesOf(built.first().front());
        udw[udw.size() - 1] = static_cast<uint8_t>(udw.back() ^ 0xFF); // flip every bit

        List<uint16_t> udw16;
        for (uint8_t b : udw) udw16.pushToBack(b);
        St291Packet      rebuilt = St291Packet::build(AncFormat(AncFormat::Op47Sdp), udw16, 0x7FE,
                                                       St291Packet::UnspecifiedHOffset, false);
        AncTranslator::ParseResult  parsed = t.parse(rebuilt.packet());
        REQUIRE(parsed.second().isError());
        CHECK(parsed.second().code() == Error::CorruptData);
}

TEST_CASE("Op47Sdp<->St291: parse rejects non-zero descriptor following a zero descriptor") {
        AncTranslator         t;
        AncOp47Sdp            input;
        input.setFooterSequenceCounter(0x0009);
        input.addPacket(makeVbiPacket(9, true, 0x70));
        input.addPacket(makeVbiPacket(10, false, 0x71));
        AncTranslator::PacketsResult built = t.build(Variant(input), AncFormat(AncFormat::Op47Sdp),
                                                AncTransport::St291);
        REQUIRE(built.second().isOk());
        List<uint8_t> udw = udwBytesOf(built.first().front());

        // Zero out the first descriptor (UDW[4]) so the wire stream
        // claims "no packet 1" but still carries a Structure B for it,
        // and leave the second descriptor (UDW[5]) intact — violates
        // §5.4.2 prefix rule.
        udw[4] = 0x00;
        uint32_t sum = 0;
        for (size_t i = 0; i + 1 < udw.size(); ++i) sum += udw[i];
        udw[udw.size() - 1] = static_cast<uint8_t>((0u - sum) & 0xFFu);

        List<uint16_t> udw16;
        for (uint8_t b : udw) udw16.pushToBack(b);
        St291Packet      rebuilt = St291Packet::build(AncFormat(AncFormat::Op47Sdp), udw16, 0x7FE,
                                                       St291Packet::UnspecifiedHOffset, false);
        AncTranslator::ParseResult  parsed = t.parse(rebuilt.packet());
        REQUIRE(parsed.second().isError());
        CHECK(parsed.second().code() == Error::CorruptData);
}

// ============================================================================
// Build path — reject lists exceeding the §5.1 five-packet cap.
// ============================================================================

TEST_CASE("Op47Sdp<->St291: build rejects more than five carried packets") {
        AncTranslator t;
        AncOp47Sdp    input;
        for (size_t i = 0; i < AncOp47Sdp::MaxVbiPackets + 1; ++i) {
                input.addPacket(makeVbiPacket(static_cast<uint8_t>(8 + i), true, static_cast<uint8_t>(i)));
        }
        AncTranslator::PacketsResult built = t.build(Variant(input), AncFormat(AncFormat::Op47Sdp),
                                                AncTransport::St291);
        REQUIRE(built.second().isError());
        CHECK(built.second().code() == Error::InvalidArgument);
}

// ============================================================================
// Capability queries — codec is wired through the AncTranslator registry.
// ============================================================================

TEST_CASE("Op47Sdp<->St291: parser + builder are registered through AncTranslator") {
        CHECK(AncTranslator::hasParser(AncFormat(AncFormat::Op47Sdp), AncTransport::St291));
        CHECK(AncTranslator::hasBuilder(AncFormat(AncFormat::Op47Sdp), AncTransport::St291));
        CHECK(AncTranslator::hasSyncPolicy(AncFormat(AncFormat::Op47Sdp)));
}

// ============================================================================
// Line / FieldB threading via AncTranslateConfig — codec respects defaults.
// ============================================================================

TEST_CASE("Op47Sdp<->St291: build honours AncTranslateConfig St291BuildLine + St291FieldB") {
        AncTranslateConfig cfg;
        cfg.set(AncTranslateConfig::St291BuildLine, static_cast<uint16_t>(9));
        cfg.set(AncTranslateConfig::St291FieldB, true);
        AncTranslator t(cfg);

        AncOp47Sdp input;
        input.addPacket(makeVbiPacket(11, true, 0x80));
        AncTranslator::PacketsResult built = t.build(Variant(input), AncFormat(AncFormat::Op47Sdp),
                                                AncTransport::St291);
        REQUIRE(built.second().isOk());
        const AncPacket &pkt = built.first().front();
        CHECK(pkt.st291Line() == 9);
        CHECK(pkt.st291FieldB() == true);
}

// ============================================================================
// Frame-sync policy — Play passes through, Drop/Repeat drop.
// ============================================================================

TEST_CASE("Op47Sdp: sync policy Play passes the packet through unchanged") {
        AncTranslator t;
        AncOp47Sdp    input;
        input.addPacket(makeVbiPacket(12, true, 0x90));
        AncTranslator::PacketsResult built = t.build(Variant(input), AncFormat(AncFormat::Op47Sdp),
                                                AncTransport::St291);
        REQUIRE(built.second().isOk());

        AncTranslator::PacketsResult out = t.applySyncPolicy(built.first().front(),
                                                       FrameSyncDisposition::play(), 0);
        REQUIRE(out.second().isOk());
        CHECK(out.first().size() == 1);
}

TEST_CASE("Op47Sdp: sync policy Drop discards the packet") {
        AncTranslator t;
        AncOp47Sdp    input;
        input.addPacket(makeVbiPacket(12, true, 0x90));
        AncTranslator::PacketsResult built = t.build(Variant(input), AncFormat(AncFormat::Op47Sdp),
                                                AncTransport::St291);
        REQUIRE(built.second().isOk());

        AncTranslator::PacketsResult out = t.applySyncPolicy(built.first().front(),
                                                       FrameSyncDisposition::drop(), 0);
        REQUIRE(out.second().isOk());
        CHECK(out.first().size() == 0);
}

TEST_CASE("Op47Sdp: sync policy Repeat drops to avoid duplicate FSC") {
        AncTranslator t;
        AncOp47Sdp    input;
        input.setFooterSequenceCounter(0x0099);
        input.addPacket(makeVbiPacket(12, true, 0x90));
        AncTranslator::PacketsResult built = t.build(Variant(input), AncFormat(AncFormat::Op47Sdp),
                                                AncTransport::St291);
        REQUIRE(built.second().isOk());

        AncTranslator::PacketsResult out = t.applySyncPolicy(built.first().front(),
                                                       FrameSyncDisposition::repeat(2), 1);
        REQUIRE(out.second().isOk());
        CHECK(out.first().size() == 0);
}

// ============================================================================
// AncOp47Sdp value-type tests.
// ============================================================================

TEST_CASE("AncOp47Sdp: default-constructed is empty") {
        AncOp47Sdp s;
        CHECK(s.footerSequenceCounter() == 0);
        CHECK(s.packets().size() == 0);
}

TEST_CASE("AncOp47Sdp: equality is field-wise") {
        AncOp47Sdp a;
        AncOp47Sdp b;
        CHECK(a == b);
        a.setFooterSequenceCounter(1);
        CHECK(a != b);
        b.setFooterSequenceCounter(1);
        CHECK(a == b);
        a.addPacket(makeVbiPacket(9, true, 0x10));
        CHECK(a != b);
        b.addPacket(makeVbiPacket(9, true, 0x10));
        CHECK(a == b);
}

TEST_CASE("AncOp47Sdp: VbiPacket equality is field-wise") {
        AncOp47Sdp::VbiPacket a = makeVbiPacket(9, true, 0x10);
        AncOp47Sdp::VbiPacket b = makeVbiPacket(9, true, 0x10);
        CHECK(a == b);
        b.lineNumber = 10;
        CHECK(a != b);
        b.lineNumber = 9;
        b.fieldOne = false;
        CHECK(a != b);
        b.fieldOne = true;
        b.reservedBits = 0x01;
        CHECK(a != b);
        b.reservedBits = 0;
        b.wstData[3] ^= 0x01;
        CHECK(a != b);
}

TEST_CASE("AncOp47Sdp: Variant round-trip preserves every field") {
        AncOp47Sdp s;
        s.setFooterSequenceCounter(0xABCD);
        s.addPacket(makeVbiPacket(9, true, 0x10));
        s.addPacket(makeVbiPacket(11, false, 0x20));
        Variant    v(s);
        AncOp47Sdp back = v.get<AncOp47Sdp>();
        CHECK(back == s);
}

TEST_CASE("AncOp47Sdp: DataStream round-trip preserves every field") {
        AncOp47Sdp s;
        s.setFooterSequenceCounter(0x1357);
        s.addPacket(makeVbiPacket(9, true, 0x10));
        AncOp47Sdp::VbiPacket reserved = makeVbiPacket(15, false, 0x30);
        reserved.reservedBits = 0x02;
        s.addPacket(reserved);

        Buffer         buf(4096);
        BufferIODevice dev(&buf);
        dev.open(IODevice::ReadWrite);
        {
                DataStream w = DataStream::createWriter(&dev);
                w << Variant(s);
                CHECK(w.status() == DataStream::Ok);
        }
        dev.seek(0);
        {
                DataStream r = DataStream::createReader(&dev);
                Variant    v;
                r >> v;
                REQUIRE(r.status() == DataStream::Ok);
                CHECK(v.type() == DataTypeAncOp47Sdp);
                CHECK(v.get<AncOp47Sdp>() == s);
        }
}
