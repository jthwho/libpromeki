/**
 * @file      rtppayloadanc.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>

#include <cstring>

#include <promeki/ancformat.h>
#include <promeki/ancmeta.h>
#include <promeki/ancpacket.h>
#include <promeki/buffer.h>
#include <promeki/list.h>
#include <promeki/rtppacket.h>
#include <promeki/rtppayloadanc.h>
#include <promeki/st291packet.h>

using namespace promeki;

namespace {

// Builds one well-known ST 291 packet stamped with the given line and
// UDW payload (one byte per word).
St291Packet makePacket(AncFormat::ID id, uint16_t line, const List<uint8_t> &bytes,
                       bool fieldB = false, uint16_t hOffset = St291Packet::UnspecifiedHOffset,
                       bool cBit = false, uint8_t streamNum = 0) {
        List<uint16_t> udw;
        udw.reserve(bytes.size());
        for (uint8_t b : bytes) udw.pushToBack(static_cast<uint16_t>(b));
        return St291Packet::build(AncFormat(id), udw, line, hOffset, fieldB, cBit, streamNum);
}

} // namespace

TEST_CASE("RtpPayloadAnc — construction") {

        SUBCASE("defaults") {
                RtpPayloadAnc payload;
                CHECK(payload.payloadType() == RtpPayloadAnc::DefaultPayloadType);
                CHECK(payload.clockRate() == 90000u);
                CHECK(payload.maxPayloadSize() == 1200u);
        }

        SUBCASE("explicit payload type") {
                RtpPayloadAnc payload(101);
                CHECK(payload.payloadType() == 101);
                payload.setPayloadType(123);
                CHECK(payload.payloadType() == 123);
        }
}

TEST_CASE("RtpPayloadAnc — bytewise pack/unpack return empty") {
        RtpPayloadAnc payload;
        uint8_t       junk[16] = {};
        auto          pl = payload.pack(junk, sizeof junk);
        CHECK(pl.isEmpty());
        RtpPacket::List in;
        auto            buf = payload.unpack(in);
        CHECK(buf.size() == 0);
}

TEST_CASE("RtpPayloadAnc — packAncFrame / unpackAncPackets round-trip") {

        SUBCASE("single CEA-708 packet — byte-exact round-trip") {
                List<uint8_t> bytes;
                for (int i = 0; i < 10; ++i) bytes.pushToBack(static_cast<uint8_t>(0x40 + i));
                St291Packet src = makePacket(AncFormat::Cea708, 11, bytes,
                                             /*fieldB=*/false,
                                             /*hOffset=*/0x123,
                                             /*cBit=*/true,
                                             /*streamNum=*/0);
                AncPacket::List packets;
                packets.pushToBack(src.packet());

                RtpPayloadAnc   payload;
                RtpPacket::List rtp = payload.packAncFrame(packets, 0xCAFEBABEu);
                REQUIRE(rtp.size() == 1u);
                CHECK(rtp[0].marker());
                CHECK(rtp[0].timestamp() == 0xCAFEBABEu);
                CHECK(rtp[0].payloadType() == RtpPayloadAnc::DefaultPayloadType);

                AncPacket::List out;
                Error           err = payload.unpackAncPackets(rtp, out);
                CHECK(!err.isError());
                REQUIRE(out.size() == 1u);

                CHECK(out[0].transport() == AncTransport::St291);
                CHECK(out[0].format().id() == AncFormat::Cea708);
                CHECK(out[0].data().size() == src.packet().data().size());
                CHECK(std::memcmp(out[0].data().data(),
                                  src.packet().data().data(),
                                  out[0].data().size()) == 0);

                Result<St291Packet> rp = St291Packet::from(out[0]);
                REQUIRE(isOk(rp));
                St291Packet ck = value(rp);
                CHECK(ck.did() == 0x61);
                CHECK(ck.sdid() == 0x01);
                CHECK(ck.dataCount() == 10);
                CHECK(ck.checksumValid());
                CHECK(ck.line() == 11);
                CHECK(ck.hOffset() == 0x123);
                CHECK(ck.cBit() == true);
                CHECK(ck.streamNum() == 0u);
                CHECK(ck.fieldB() == false);
        }

        SUBCASE("multiple packets in one RTP frame") {
                List<uint8_t> body{0x10, 0x20, 0x30, 0x40};
                AncPacket::List packets;
                packets.pushToBack(makePacket(AncFormat::Cea708, 10, body).packet());
                packets.pushToBack(makePacket(AncFormat::Afd, 11, {0x07}).packet());
                packets.pushToBack(makePacket(AncFormat::AtcLtc, 12, {0xAB, 0xCD, 0xEF}).packet());

                RtpPayloadAnc   payload;
                RtpPacket::List rtp = payload.packAncFrame(packets, 100u);
                REQUIRE(rtp.size() == 1u);
                CHECK(rtp[0].marker());

                AncPacket::List out;
                CHECK(!payload.unpackAncPackets(rtp, out).isError());
                REQUIRE(out.size() == 3u);
                CHECK(out[0].format().id() == AncFormat::Cea708);
                CHECK(out[1].format().id() == AncFormat::Afd);
                CHECK(out[2].format().id() == AncFormat::AtcLtc);
                CHECK(value(St291Packet::from(out[0])).line() == 10);
                CHECK(value(St291Packet::from(out[1])).line() == 11);
                CHECK(value(St291Packet::from(out[2])).line() == 12);
        }

        SUBCASE("field-B encoding round-trips as F=11") {
                List<uint8_t>   body{0xFE, 0xED};
                AncPacket::List packets;
                packets.pushToBack(makePacket(AncFormat::AtcVitc2, 9, body, /*fieldB=*/true).packet());

                RtpPayloadAnc   payload;
                RtpPacket::List rtp = payload.packAncFrame(packets, 1u);
                REQUIRE(rtp.size() == 1u);
                const uint8_t *pl = rtp[0].payload();
                REQUIRE(pl != nullptr);
                // F bits live in the high 2 bits of byte 5.
                CHECK(((pl[5] >> 6) & 0x03u) == 0x03u);

                AncPacket::List out;
                CHECK(!payload.unpackAncPackets(rtp, out).isError());
                REQUIRE(out.size() == 1u);
                CHECK(value(St291Packet::from(out[0])).fieldB() == true);
        }

        SUBCASE("progressive (all FieldB=false) encodes as F=00") {
                AncPacket::List packets;
                packets.pushToBack(makePacket(AncFormat::Cea708, 11, {0x01, 0x02}).packet());

                RtpPayloadAnc   payload;
                RtpPacket::List rtp = payload.packAncFrame(packets, 1u);
                REQUIRE(rtp.size() == 1u);
                const uint8_t *pl = rtp[0].payload();
                REQUIRE(pl != nullptr);
                CHECK(((pl[5] >> 6) & 0x03u) == 0x00u);

                AncPacket::List out;
                CHECK(!payload.unpackAncPackets(rtp, out).isError());
                CHECK(value(St291Packet::from(out[0])).fieldB() == false);
        }

        SUBCASE("multi-stream — StreamNum and S-bit") {
                AncPacket::List packets;
                packets.pushToBack(makePacket(AncFormat::Cea708, 11, {0x05},
                                              /*fieldB=*/false,
                                              /*hOffset=*/St291Packet::UnspecifiedHOffset,
                                              /*cBit=*/false,
                                              /*streamNum=*/5)
                                           .packet());
                RtpPayloadAnc payload;
                auto          rtp = payload.packAncFrame(packets, 1u);
                REQUIRE(rtp.size() == 1u);

                AncPacket::List out;
                CHECK(!payload.unpackAncPackets(rtp, out).isError());
                REQUIRE(out.size() == 1u);
                CHECK(value(St291Packet::from(out[0])).streamNum() == 5u);
        }

        SUBCASE("DataCount modulos — alignment across word boundaries") {
                // Sweep DC values 1..8 — covers two full 10-bit×4-word
                // wraps so every byte-alignment phase of the 10-bit
                // packing is exercised.
                for (uint8_t dc = 1; dc <= 8; ++dc) {
                        List<uint8_t> bytes;
                        for (uint8_t i = 0; i < dc; ++i) bytes.pushToBack(static_cast<uint8_t>(0x50 + i));
                        AncPacket::List in;
                        in.pushToBack(makePacket(AncFormat::Cea708, 11, bytes).packet());

                        RtpPayloadAnc   payload;
                        RtpPacket::List rtp = payload.packAncFrame(in, 0x1000u + dc);
                        REQUIRE(rtp.size() == 1u);

                        AncPacket::List out;
                        CHECK(!payload.unpackAncPackets(rtp, out).isError());
                        REQUIRE(out.size() == 1u);

                        REQUIRE(out[0].data().size() == in[0].data().size());
                        CHECK(std::memcmp(out[0].data().data(),
                                          in[0].data().data(),
                                          out[0].data().size()) == 0);
                        Result<St291Packet> rp = St291Packet::from(out[0]);
                        REQUIRE(isOk(rp));
                        CHECK(value(rp).dataCount() == dc);
                        CHECK(value(rp).checksumValid());
                }
        }

        SUBCASE("preserves stored checksum byte-for-byte") {
                List<uint8_t> bytes{0xAA, 0xBB, 0xCC};
                St291Packet   src = makePacket(AncFormat::Cea708, 11, bytes);
                const uint16_t srcCs = src.checksum();
                AncPacket::List packets;
                packets.pushToBack(src.packet());

                RtpPayloadAnc   payload;
                RtpPacket::List rtp = payload.packAncFrame(packets, 0u);

                AncPacket::List out;
                CHECK(!payload.unpackAncPackets(rtp, out).isError());
                REQUIRE(out.size() == 1u);
                Result<St291Packet> rp = St291Packet::from(out[0]);
                REQUIRE(isOk(rp));
                CHECK(value(rp).checksum() == srcCs);
        }
}

TEST_CASE("RtpPayloadAnc — MTU splitting") {

        SUBCASE("frame split across multiple RTP packets") {
                AncPacket::List packets;
                // Each packet's body bytes = ceil((4+12)*10/8) = 20 →
                // 4 + 20 = 24 record bytes total.  At
                // maxPayloadSize=64 minus 8-byte ANC payload header =
                // 56 bytes of record room per RTP packet, so 2 records
                // fit per RTP (48 ≤ 56) but a third would not (72 > 56).
                // With 5 packets total we expect 3 RTP packets (2+2+1).
                for (int i = 0; i < 5; ++i) {
                        List<uint8_t> body;
                        for (int j = 0; j < 12; ++j) body.pushToBack(static_cast<uint8_t>(j + i));
                        packets.pushToBack(makePacket(AncFormat::Cea708,
                                                       static_cast<uint16_t>(10 + i),
                                                       body)
                                                   .packet());
                }

                RtpPayloadAnc payload;
                payload.setMaxPayloadSize(64);
                RtpPacket::List rtp = payload.packAncFrame(packets, 7u);
                REQUIRE(rtp.size() == 3u);
                // Only the last packet has the marker bit.
                CHECK_FALSE(rtp[0].marker());
                CHECK_FALSE(rtp[1].marker());
                CHECK(rtp[2].marker());
                CHECK(rtp[0].timestamp() == 7u);
                CHECK(rtp[1].timestamp() == 7u);
                CHECK(rtp[2].timestamp() == 7u);

                AncPacket::List out;
                CHECK(!payload.unpackAncPackets(rtp, out).isError());
                REQUIRE(out.size() == 5u);
                for (size_t i = 0; i < 5; ++i) {
                        Result<St291Packet> rp = St291Packet::from(out[i]);
                        REQUIRE(isOk(rp));
                        CHECK(value(rp).line() == static_cast<uint16_t>(10 + i));
                        CHECK(value(rp).checksumValid());
                }
        }
}

TEST_CASE("RtpPayloadAnc — degenerate / error cases") {

        SUBCASE("empty list produces no RTP packets") {
                RtpPayloadAnc   payload;
                AncPacket::List in;
                RtpPacket::List rtp = payload.packAncFrame(in, 1u);
                CHECK(rtp.isEmpty());
        }

        SUBCASE("non-St291 entries are skipped") {
                AncPacket::List packets;
                Buffer          dummy(4);
                dummy.setSize(4);
                Metadata m;
                m.set(AncMeta::NdiXml::ElementName, String("X"));
                AncPacket ndiPkt(AncFormat(), AncTransport::NdiXml, dummy, m);
                packets.pushToBack(ndiPkt);
                packets.pushToBack(makePacket(AncFormat::Cea708, 11, {0x01}).packet());

                RtpPayloadAnc   payload;
                RtpPacket::List rtp = payload.packAncFrame(packets, 1u);
                REQUIRE(rtp.size() == 1u);
                AncPacket::List out;
                CHECK(!payload.unpackAncPackets(rtp, out).isError());
                REQUIRE(out.size() == 1u);  // The NdiXml packet was skipped.
                CHECK(out[0].format().id() == AncFormat::Cea708);
        }

        SUBCASE("validate gates short / zero-ANC_Count payloads") {
                RtpPayloadAnc payload;
                Buffer        tiny(4);
                tiny.setSize(4);
                CHECK(payload.validate(tiny) == RtpPayload::ValidateResult::DropSilently);

                // 8-byte header with ANC_Count=0 → drop.
                Buffer    empty(8);
                empty.setSize(8);
                uint8_t  *eptr = static_cast<uint8_t *>(empty.data());
                std::memset(eptr, 0, 8);
                CHECK(payload.validate(empty) == RtpPayload::ValidateResult::DropSilently);

                // 8-byte header with ANC_Count=1 but no body → drop
                // (Length=0 but ANC_Count=1; we only check Length here,
                // a real decode would also fail).
                uint8_t hdrOnly[8] = {0, 0, 0, 16, 1, 0, 0, 0};
                Buffer  short8(8);
                short8.setSize(8);
                std::memcpy(short8.data(), hdrOnly, 8);
                CHECK(payload.validate(short8) == RtpPayload::ValidateResult::DropSilently);
        }

        SUBCASE("unpack rejects truncated payload header") {
                // Build a single ANC RTP packet, then truncate its
                // payload so the declared Length overruns the packet.
                AncPacket::List packets;
                packets.pushToBack(makePacket(AncFormat::Cea708, 11, {0x01, 0x02, 0x03}).packet());
                RtpPayloadAnc   payload;
                RtpPacket::List rtp = payload.packAncFrame(packets, 1u);
                REQUIRE(rtp.size() == 1u);
                // Build a truncated copy: keep only 4 payload bytes.
                Buffer  truncBuf(RtpPacket::HeaderSize + 4);
                truncBuf.setSize(RtpPacket::HeaderSize + 4);
                std::memcpy(truncBuf.data(), rtp[0].data(), RtpPacket::HeaderSize + 4);
                RtpPacket truncRtp(truncBuf, 0, RtpPacket::HeaderSize + 4);
                RtpPacket::List trunc;
                trunc.pushToBack(truncRtp);

                AncPacket::List out;
                Error           err = payload.unpackAncPackets(trunc, out);
                CHECK(err.isError());
                CHECK(err.code() == Error::OutOfRange);
        }
}
