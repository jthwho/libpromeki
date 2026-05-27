/**
 * @file      anccodec_st2020audio.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/ancformat.h>
#include <promeki/ancpacket.h>
#include <promeki/ancst2020audio.h>
#include <promeki/anctranslateconfig.h>
#include <promeki/anctranslator.h>
#include <promeki/buffer.h>
#include <promeki/bufferiodevice.h>
#include <promeki/datastream.h>
#include <promeki/framesyncdisposition.h>
#include <promeki/result.h>
#include <promeki/st291packet.h>
#include <promeki/variant.h>

using namespace promeki;

namespace {

        Buffer makePayload(size_t n, uint8_t seed = 0) {
                Buffer b(n);
                b.setSize(n);
                uint8_t *p = static_cast<uint8_t *>(b.data());
                for (size_t i = 0; i < n; ++i) p[i] = static_cast<uint8_t>(seed + i);
                return b;
        }

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
// Single-packet build path — MDF <= 254 bytes.
// ============================================================================

TEST_CASE("St2020Audio<->St291: single-packet round-trip with channel pair 1/2") {
        AncTranslator  t;
        AncSt2020Audio input;
        input.setChannelPair(AncSt2020Audio::ChannelPair1_2);
        input.setMetadataFrame(makePayload(180, 0x10));

        AncTranslator::PacketsResult built = t.build(Variant(input),
                                                AncFormat(AncFormat::Smpte2020Audio),
                                                AncTransport::St291);
        REQUIRE(built.second().isOk());
        REQUIRE(built.first().size() == 1);

        const AncPacket &pkt = built.first().front();
        CHECK(pkt.format().id() == AncFormat::Smpte2020Audio);
        CHECK(pkt.transport() == AncTransport::St291);

        Result<St291Packet> rp = St291Packet::from(pkt);
        REQUIRE(rp.second().isOk());
        CHECK(rp.first().did() == 0x45);
        CHECK(rp.first().sdid() == AncSt2020Audio::ChannelPair1_2);
        CHECK(rp.first().dataCount() == 181); // 1 descriptor + 180 MDF

        AncTranslator::ParseResult parsed = t.parse(pkt);
        REQUIRE(parsed.second().isOk());
        AncSt2020Audio back = parsed.first().get<AncSt2020Audio>();
        CHECK(back == input);
        CHECK(back.channelPair() == AncSt2020Audio::ChannelPair1_2);
        CHECK(back.metadataFrame().size() == 180);
}

TEST_CASE("St2020Audio<->St291: empty metadata frame round-trips") {
        AncTranslator  t;
        AncSt2020Audio input;
        input.setChannelPair(AncSt2020Audio::NoAssociation);

        AncTranslator::PacketsResult built = t.build(Variant(input),
                                                AncFormat(AncFormat::Smpte2020Audio),
                                                AncTransport::St291);
        REQUIRE(built.second().isOk());
        Result<St291Packet> rp = St291Packet::from(built.first().front());
        REQUIRE(rp.second().isOk());
        CHECK(rp.first().sdid() == 0x01);
        CHECK(rp.first().dataCount() == 1); // descriptor only

        AncTranslator::ParseResult parsed = t.parse(built.first().front());
        REQUIRE(parsed.second().isOk());
        AncSt2020Audio back = parsed.first().get<AncSt2020Audio>();
        CHECK(back == input);
}

TEST_CASE("St2020Audio<->St291: maximum single-packet MDF (254 bytes)") {
        AncTranslator  t;
        AncSt2020Audio input;
        input.setChannelPair(AncSt2020Audio::ChannelPair3_4);
        input.setMetadataFrame(makePayload(AncSt2020Audio::MaxSinglePacketBytes, 0x20));

        AncTranslator::PacketsResult built = t.build(Variant(input),
                                                AncFormat(AncFormat::Smpte2020Audio),
                                                AncTransport::St291);
        REQUIRE(built.second().isOk());
        CHECK(built.first().size() == 1);
        Result<St291Packet> rp = St291Packet::from(built.first().front());
        REQUIRE(rp.second().isOk());
        CHECK(rp.first().dataCount() == 255); // descriptor + 254 MDF — max DC
        CHECK(rp.first().sdid() == AncSt2020Audio::ChannelPair3_4);

        AncTranslator::ParseResult parsed = t.parse(built.first().front());
        REQUIRE(parsed.second().isOk());
        CHECK(parsed.first().get<AncSt2020Audio>() == input);
}

// ============================================================================
// ST 2020-2 §5.4 Payload Descriptor — bit-position checks.
// ============================================================================

TEST_CASE("St2020Audio<->St291: payload descriptor version bits are 01b on emit") {
        AncTranslator  t;
        AncSt2020Audio input;
        input.setChannelPair(AncSt2020Audio::ChannelPair1_2);
        input.setMetadataFrame(makePayload(10, 0));

        AncTranslator::PacketsResult built = t.build(Variant(input),
                                                AncFormat(AncFormat::Smpte2020Audio),
                                                AncTransport::St291);
        REQUIRE(built.second().isOk());
        List<uint8_t> udw = udwBytesOf(built.first().front());
        // Payload descriptor at UDW[0]: bits 4..3 = 01b → value 0x08.
        CHECK((udw[0] & AncSt2020Audio::PayloadDescriptorVersionMask) ==
              AncSt2020Audio::PayloadDescriptorVersionV1);
        // bit 7 (COMPATIBILITY) and bits 5,6 (Reserved) must be 0.
        CHECK((udw[0] & 0xE0) == 0);
        // Single packet → DOUBLE_PKT (b2) and SECOND_PKT (b1) clear.
        CHECK((udw[0] & AncSt2020Audio::PayloadDescriptorDoubleBit) == 0);
        CHECK((udw[0] & AncSt2020Audio::PayloadDescriptorSecondBit) == 0);
}

TEST_CASE("St2020Audio<->St291: duplicate flag (descriptor b0) round-trips") {
        AncTranslator  t;
        AncSt2020Audio input;
        input.setChannelPair(AncSt2020Audio::ChannelPair5_6);
        input.setDuplicate(true);
        input.setMetadataFrame(makePayload(20, 0x30));

        AncTranslator::PacketsResult built = t.build(Variant(input),
                                                AncFormat(AncFormat::Smpte2020Audio),
                                                AncTransport::St291);
        REQUIRE(built.second().isOk());
        List<uint8_t> udw = udwBytesOf(built.first().front());
        CHECK((udw[0] & AncSt2020Audio::PayloadDescriptorDuplicateBit) != 0);

        AncTranslator::ParseResult parsed = t.parse(built.first().front());
        REQUIRE(parsed.second().isOk());
        AncSt2020Audio back = parsed.first().get<AncSt2020Audio>();
        CHECK(back.duplicate() == true);
}

// ============================================================================
// Two-packet split build path — MDF > 254 bytes.
// ============================================================================

TEST_CASE("St2020Audio<->St291: MDF=300 splits across two packets per §5.3") {
        AncTranslator  t;
        AncSt2020Audio input;
        input.setChannelPair(AncSt2020Audio::ChannelPair7_8);
        input.setMetadataFrame(makePayload(300, 0x40));

        AncTranslator::PacketsResult built = t.build(Variant(input),
                                                AncFormat(AncFormat::Smpte2020Audio),
                                                AncTransport::St291);
        REQUIRE(built.second().isOk());
        REQUIRE(built.first().size() == 2);

        // Pkt 1: 1 descriptor + 254 MDF = DC 255; descriptor has
        // DOUBLE=1, SECOND=0.
        List<uint8_t> u1 = udwBytesOf(built.first()[0]);
        CHECK(u1.size() == 255);
        CHECK((u1[0] & AncSt2020Audio::PayloadDescriptorDoubleBit) != 0);
        CHECK((u1[0] & AncSt2020Audio::PayloadDescriptorSecondBit) == 0);

        // Pkt 2: 1 descriptor + 46 MDF = DC 47; descriptor has
        // DOUBLE=1, SECOND=1.
        List<uint8_t> u2 = udwBytesOf(built.first()[1]);
        CHECK(u2.size() == 47);
        CHECK((u2[0] & AncSt2020Audio::PayloadDescriptorDoubleBit) != 0);
        CHECK((u2[0] & AncSt2020Audio::PayloadDescriptorSecondBit) != 0);

        // Both packets share the same SDID.
        Result<St291Packet> r1 = St291Packet::from(built.first()[0]);
        Result<St291Packet> r2 = St291Packet::from(built.first()[1]);
        REQUIRE(r1.second().isOk());
        REQUIRE(r2.second().isOk());
        CHECK(r1.first().sdid() == AncSt2020Audio::ChannelPair7_8);
        CHECK(r2.first().sdid() == AncSt2020Audio::ChannelPair7_8);

        AncTranslator::ParseResult parsed = t.parseGroup(built.first());
        REQUIRE(parsed.second().isOk());
        AncSt2020Audio back = parsed.first().get<AncSt2020Audio>();
        CHECK(back == input);
        CHECK(back.metadataFrame().size() == 300);
}

TEST_CASE("St2020Audio<->St291: single-packet parse rejects split packets") {
        AncTranslator  t;
        AncSt2020Audio input;
        input.setChannelPair(AncSt2020Audio::ChannelPair9_10);
        input.setMetadataFrame(makePayload(300, 0x50));

        AncTranslator::PacketsResult built = t.build(Variant(input),
                                                AncFormat(AncFormat::Smpte2020Audio),
                                                AncTransport::St291);
        REQUIRE(built.second().isOk());
        // Calling single-packet parse on a (DOUBLE=1, SECOND=0) packet
        // surfaces Error::InsufficientContext so the dispatcher knows
        // to route it through the multi-parser.
        AncTranslator::ParseResult parsed = t.parse(built.first()[0]);
        REQUIRE(parsed.second().isError());
        CHECK(parsed.second().code() == Error::InsufficientContext);
}

TEST_CASE("St2020Audio<->St291: maximum split MDF (508 bytes)") {
        AncTranslator  t;
        AncSt2020Audio input;
        input.setChannelPair(AncSt2020Audio::ChannelPair11_12);
        input.setMetadataFrame(makePayload(AncSt2020Audio::MaxMetadataFrameBytes, 0x60));

        AncTranslator::PacketsResult built = t.build(Variant(input),
                                                AncFormat(AncFormat::Smpte2020Audio),
                                                AncTransport::St291);
        REQUIRE(built.second().isOk());
        REQUIRE(built.first().size() == 2);
        Result<St291Packet> r1 = St291Packet::from(built.first()[0]);
        Result<St291Packet> r2 = St291Packet::from(built.first()[1]);
        REQUIRE(r1.second().isOk());
        REQUIRE(r2.second().isOk());
        CHECK(r1.first().dataCount() == 255);
        CHECK(r2.first().dataCount() == 255);

        AncTranslator::ParseResult parsed = t.parseGroup(built.first());
        REQUIRE(parsed.second().isOk());
        CHECK(parsed.first().get<AncSt2020Audio>() == input);
}

// ============================================================================
// Build path — reject invalid inputs.
// ============================================================================

TEST_CASE("St2020Audio<->St291: build rejects MDF larger than 508 bytes") {
        AncTranslator  t;
        AncSt2020Audio input;
        input.setChannelPair(AncSt2020Audio::ChannelPair1_2);
        input.setMetadataFrame(makePayload(AncSt2020Audio::MaxMetadataFrameBytes + 1));

        AncTranslator::PacketsResult built = t.build(Variant(input),
                                                AncFormat(AncFormat::Smpte2020Audio),
                                                AncTransport::St291);
        REQUIRE(built.second().isError());
        CHECK(built.second().code() == Error::InvalidArgument);
}

TEST_CASE("St2020Audio<->St291: build rejects SDID 0x00 (ST 2020-1 §7.1 reserved)") {
        AncTranslator  t;
        AncSt2020Audio input;
        input.setChannelPair(0x00);

        AncTranslator::PacketsResult built = t.build(Variant(input),
                                                AncFormat(AncFormat::Smpte2020Audio),
                                                AncTransport::St291);
        REQUIRE(built.second().isError());
        CHECK(built.second().code() == Error::InvalidArgument);
}

// ============================================================================
// Multi-parser — reject malformed split sequences.
// ============================================================================

TEST_CASE("St2020Audio<->St291: multi-parser rejects mismatched SDIDs") {
        AncTranslator  t;
        AncSt2020Audio input;
        input.setChannelPair(AncSt2020Audio::ChannelPair13_14);
        input.setMetadataFrame(makePayload(300, 0x70));

        AncTranslator::PacketsResult built = t.build(Variant(input),
                                                AncFormat(AncFormat::Smpte2020Audio),
                                                AncTransport::St291);
        REQUIRE(built.second().isOk());

        // Rebuild the second packet with a different SDID to simulate
        // an out-of-order cross-program collision.
        Result<St291Packet> r2 = St291Packet::from(built.first()[1]);
        REQUIRE(r2.second().isOk());
        St291Packet substitute = St291Packet::buildRaw(
                0x45, AncSt2020Audio::ChannelPair15_16, r2.first().udw(),
                r2.first().line(), r2.first().hOffset(), r2.first().fieldB());
        AncPacket::List mismatched;
        mismatched.pushToBack(built.first()[0]);
        mismatched.pushToBack(substitute.packet());

        AncTranslator::ParseResult parsed = t.parseGroup(mismatched);
        REQUIRE(parsed.second().isError());
        CHECK(parsed.second().code() == Error::CorruptData);
}

TEST_CASE("St2020Audio<->St291: multi-parser rejects (Double=0) pair") {
        AncTranslator  t;
        // Hand-craft two packets that pretend to be a split pair but
        // have DOUBLE_PKT cleared on both — violates §5.4.3.
        AncFormat fmt(AncFormat::Smpte2020Audio);
        List<uint16_t> udw1;
        udw1.pushToBack(AncSt2020Audio::PayloadDescriptorVersionV1); // single-pkt descriptor
        for (uint8_t b = 1; b < 50; ++b) udw1.pushToBack(b);
        List<uint16_t> udw2 = udw1;
        St291Packet     p1 = St291Packet::buildRaw(0x45, AncSt2020Audio::ChannelPair1_2, udw1,
                                                    St291Packet::UnspecifiedLine,
                                                    St291Packet::UnspecifiedHOffset, false);
        St291Packet     p2 = St291Packet::buildRaw(0x45, AncSt2020Audio::ChannelPair1_2, udw2,
                                                    St291Packet::UnspecifiedLine,
                                                    St291Packet::UnspecifiedHOffset, false);
        AncPacket::List pkts;
        pkts.pushToBack(p1.packet());
        pkts.pushToBack(p2.packet());

        AncTranslator::ParseResult parsed = t.parseGroup(pkts);
        REQUIRE(parsed.second().isError());
        CHECK(parsed.second().code() == Error::CorruptData);
}

TEST_CASE("St2020Audio<->St291: multi-parser rejects three-packet group") {
        AncTranslator  t;
        AncSt2020Audio input;
        input.setChannelPair(AncSt2020Audio::ChannelPair1_2);
        input.setMetadataFrame(makePayload(300, 0x70));

        AncTranslator::PacketsResult built = t.build(Variant(input),
                                                AncFormat(AncFormat::Smpte2020Audio),
                                                AncTransport::St291);
        REQUIRE(built.second().isOk());
        AncPacket::List three;
        three.pushToBack(built.first()[0]);
        three.pushToBack(built.first()[1]);
        three.pushToBack(built.first()[0]);

        AncTranslator::ParseResult parsed = t.parseGroup(three);
        REQUIRE(parsed.second().isError());
        CHECK(parsed.second().code() == Error::CorruptData);
}

// ============================================================================
// Capability queries — codec is registered through the AncTranslator registry.
// ============================================================================

TEST_CASE("St2020Audio<->St291: parser + builder + sync policy are registered") {
        CHECK(AncTranslator::hasParser(AncFormat(AncFormat::Smpte2020Audio), AncTransport::St291));
        CHECK(AncTranslator::hasBuilder(AncFormat(AncFormat::Smpte2020Audio), AncTransport::St291));
        CHECK(AncTranslator::hasMultiParser(AncFormat(AncFormat::Smpte2020Audio),
                                            AncTransport::St291));
        CHECK(AncTranslator::hasSyncPolicy(AncFormat(AncFormat::Smpte2020Audio)));
}

// ============================================================================
// AncTranslateConfig threading.
// ============================================================================

TEST_CASE("St2020Audio<->St291: build honours AncTranslateConfig St291BuildLine + St291FieldB") {
        AncTranslateConfig cfg;
        cfg.set(AncTranslateConfig::St291BuildLine, static_cast<uint16_t>(15));
        cfg.set(AncTranslateConfig::St291FieldB, true);
        AncTranslator t(cfg);

        AncSt2020Audio input;
        input.setChannelPair(AncSt2020Audio::ChannelPair1_2);
        input.setMetadataFrame(makePayload(40, 0x80));
        AncTranslator::PacketsResult built = t.build(Variant(input),
                                                AncFormat(AncFormat::Smpte2020Audio),
                                                AncTransport::St291);
        REQUIRE(built.second().isOk());
        CHECK(built.first().front().st291Line() == 15);
        CHECK(built.first().front().st291FieldB() == true);
}

// ============================================================================
// Frame-sync policy — Play/Repeat pass through, Drop discards.
// ============================================================================

TEST_CASE("St2020Audio: sync policy Play and Repeat pass the packet through") {
        AncTranslator  t;
        AncSt2020Audio input;
        input.setChannelPair(AncSt2020Audio::ChannelPair1_2);
        input.setMetadataFrame(makePayload(50, 0x90));
        AncTranslator::PacketsResult built = t.build(Variant(input),
                                                AncFormat(AncFormat::Smpte2020Audio),
                                                AncTransport::St291);
        REQUIRE(built.second().isOk());

        AncTranslator::PacketsResult playOut = t.applySyncPolicy(built.first().front(),
                                                             FrameSyncDisposition::play(), 0);
        REQUIRE(playOut.second().isOk());
        CHECK(playOut.first().size() == 1);

        AncTranslator::PacketsResult repeatOut = t.applySyncPolicy(built.first().front(),
                                                               FrameSyncDisposition::repeat(2), 1);
        REQUIRE(repeatOut.second().isOk());
        CHECK(repeatOut.first().size() == 1);
}

TEST_CASE("St2020Audio: sync policy Drop discards the packet") {
        AncTranslator  t;
        AncSt2020Audio input;
        input.setChannelPair(AncSt2020Audio::ChannelPair1_2);
        input.setMetadataFrame(makePayload(50, 0x90));
        AncTranslator::PacketsResult built = t.build(Variant(input),
                                                AncFormat(AncFormat::Smpte2020Audio),
                                                AncTransport::St291);
        REQUIRE(built.second().isOk());

        AncTranslator::PacketsResult out = t.applySyncPolicy(built.first().front(),
                                                        FrameSyncDisposition::drop(), 0);
        REQUIRE(out.second().isOk());
        CHECK(out.first().size() == 0);
}

// ============================================================================
// AncSt2020Audio value-type tests.
// ============================================================================

TEST_CASE("AncSt2020Audio: default-constructed defaults to NoAssociation, no payload") {
        AncSt2020Audio s;
        CHECK(s.channelPair() == AncSt2020Audio::NoAssociation);
        CHECK(s.duplicate() == false);
        CHECK(s.metadataFrame().size() == 0);
}

TEST_CASE("AncSt2020Audio: equality is field-wise") {
        AncSt2020Audio a;
        AncSt2020Audio b;
        CHECK(a == b);
        a.setChannelPair(AncSt2020Audio::ChannelPair1_2);
        CHECK(a != b);
        b.setChannelPair(AncSt2020Audio::ChannelPair1_2);
        CHECK(a == b);
        a.setDuplicate(true);
        CHECK(a != b);
        b.setDuplicate(true);
        CHECK(a == b);
        a.setMetadataFrame(makePayload(20, 0x11));
        CHECK(a != b);
        b.setMetadataFrame(makePayload(20, 0x11));
        CHECK(a == b);
}

TEST_CASE("AncSt2020Audio: Variant round-trip preserves every field") {
        AncSt2020Audio s;
        s.setChannelPair(AncSt2020Audio::ChannelPair3_4);
        s.setDuplicate(true);
        s.setMetadataFrame(makePayload(120, 0xAB));
        Variant        v(s);
        AncSt2020Audio back = v.get<AncSt2020Audio>();
        CHECK(back == s);
}

TEST_CASE("AncSt2020Audio: DataStream round-trip preserves every field") {
        AncSt2020Audio s;
        s.setChannelPair(AncSt2020Audio::ChannelPair5_6);
        s.setDuplicate(true);
        s.setMetadataFrame(makePayload(200, 0xCD));

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
                CHECK(v.type() == DataTypeSt2020Audio);
                CHECK(v.get<AncSt2020Audio>() == s);
        }
}

TEST_CASE("AncSt2020Audio: SDID values match ST 2020-1 §7.1 Table 1") {
        // Sanity-check that the named constants align with the
        // Table 1 SDID-to-channel-pair mapping.
        CHECK(AncSt2020Audio::NoAssociation == 0x01);
        CHECK(AncSt2020Audio::ChannelPair1_2 == 0x02);
        CHECK(AncSt2020Audio::ChannelPair3_4 == 0x03);
        CHECK(AncSt2020Audio::ChannelPair5_6 == 0x04);
        CHECK(AncSt2020Audio::ChannelPair7_8 == 0x05);
        CHECK(AncSt2020Audio::ChannelPair9_10 == 0x06);
        CHECK(AncSt2020Audio::ChannelPair11_12 == 0x07);
        CHECK(AncSt2020Audio::ChannelPair13_14 == 0x08);
        CHECK(AncSt2020Audio::ChannelPair15_16 == 0x09);
}
