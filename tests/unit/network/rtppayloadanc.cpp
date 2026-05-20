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
#include <promeki/metadata.h>
#include <promeki/rtppacket.h>
#include <promeki/rtppayloadanc.h>
#include <promeki/st291packet.h>
#include <promeki/variantspec.h>

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

        SUBCASE("empty list produces a ST 2110-40 §5.5 keep-alive RTP packet") {
                RtpPayloadAnc   payload;
                AncPacket::List in;
                RtpPacket::List rtp = payload.packAncFrame(in, 1u);
                REQUIRE(rtp.size() == 1u);
                CHECK(rtp[0].marker());
                // Payload is exactly the 8-byte RFC 8331 §2.1 header
                // (no records, no word_align padding per §2.2).
                REQUIRE(rtp[0].payloadSize() == RtpPayloadAnc::PayloadHeaderSize);
                const uint8_t *pl = rtp[0].payload();
                REQUIRE(pl != nullptr);
                CHECK(pl[2] == 0); // Length hi
                CHECK(pl[3] == 0); // Length lo
                CHECK(pl[4] == 0); // ANC_Count
                CHECK(((pl[5] >> 6) & 0x03) == 0x0); // F = Progressive (default)
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

        SUBCASE("validate gates short / malformed payloads; accepts keep-alive") {
                RtpPayloadAnc payload;
                Buffer        tiny(4);
                tiny.setSize(4);
                CHECK(payload.validate(tiny) == RtpPayload::ValidateResult::DropSilently);

                // 8-byte header with ANC_Count=0 + Length=0 is the
                // ST 2110-40 §5.5 keep-alive shape — Accept.
                Buffer    empty(8);
                empty.setSize(8);
                uint8_t  *eptr = static_cast<uint8_t *>(empty.data());
                std::memset(eptr, 0, 8);
                CHECK(payload.validate(empty) == RtpPayload::ValidateResult::Accept);

                // ANC_Count=0 with non-zero Length is malformed.
                uint8_t  malformed[8] = {0, 0, 0, 4, 0, 0, 0, 0};
                Buffer   bad(8);
                bad.setSize(8);
                std::memcpy(bad.data(), malformed, 8);
                CHECK(payload.validate(bad) == RtpPayload::ValidateResult::DropSilently);

                // 8-byte header with ANC_Count=1 but no body → drop
                // (Length implies more bytes than the payload holds).
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

// ============================================================================
// F3 — RFC 8331 / ST 2110-40 correctness
// ============================================================================

TEST_CASE("RtpPayloadAnc F3 — RFC 8331 §2.1 / ST 2110-40 §5.5") {

        SUBCASE("C1: unpack ignores F=0b01 packets") {
                // Build a valid single-packet RTP payload, then force
                // the F-bit to 0b01 (RFC 8331 §2.1 reserved).
                AncPacket::List packets;
                packets.pushToBack(makePacket(AncFormat::Cea708, 11, {0x01, 0x02, 0x03}).packet());
                RtpPayloadAnc   payload;
                RtpPacket::List rtp = payload.packAncFrame(packets, 1u);
                REQUIRE(rtp.size() == 1u);
                uint8_t *pl = rtp[0].payload();
                REQUIRE(pl != nullptr);
                pl[5] = static_cast<uint8_t>(
                        (static_cast<uint8_t>(RtpPayloadAnc::FieldIndication::Invalid) & 0x03u) << 6);

                AncPacket::List out;
                Error           err = payload.unpackAncPackets(rtp, out);
                CHECK(!err.isError());
                // Records skipped wholesale — the payload was tainted.
                CHECK(out.isEmpty());
        }

        SUBCASE("C2: keep-alive emit on empty input") {
                RtpPayloadAnc   payload;
                RtpPacket::List rtp = payload.packAncFrame(AncPacket::List(), 42u);
                REQUIRE(rtp.size() == 1u);
                CHECK(rtp[0].marker());
                CHECK(rtp[0].timestamp() == 42u);
                REQUIRE(rtp[0].payloadSize() == RtpPayloadAnc::PayloadHeaderSize);
                const uint8_t *pl = rtp[0].payload();
                REQUIRE(pl != nullptr);
                CHECK(pl[2] == 0); // Length hi
                CHECK(pl[3] == 0); // Length lo
                CHECK(pl[4] == 0); // ANC_Count
        }

        SUBCASE("C2: keep-alive accepted on receive") {
                // Hand-build a keep-alive datagram (ANC_Count=0,
                // Length=0, Marker=1, F=Progressive).
                RtpPayloadAnc   payload;
                RtpPacket::List ka = payload.packAncFrame(AncPacket::List(), 0u);
                REQUIRE(ka.size() == 1u);

                AncPacket::List out;
                Error           err = payload.unpackAncPackets(ka, out);
                CHECK(!err.isError());
                CHECK(out.isEmpty());
        }

        SUBCASE("C2: keep-alive F-bit honours setKeepAliveField") {
                RtpPayloadAnc payload;
                payload.setKeepAliveField(RtpPayloadAnc::FieldIndication::InterlacedField1);
                CHECK(payload.keepAliveField() ==
                      RtpPayloadAnc::FieldIndication::InterlacedField1);
                RtpPacket::List rtp = payload.packAncFrame(AncPacket::List(), 0u);
                REQUIRE(rtp.size() == 1u);
                const uint8_t *pl = rtp[0].payload();
                REQUIRE(pl != nullptr);
                CHECK(((pl[5] >> 6) & 0x03) ==
                      static_cast<uint8_t>(RtpPayloadAnc::FieldIndication::InterlacedField1));
        }

        SUBCASE("C2: setKeepAliveField rejects FieldIndication::Invalid") {
                RtpPayloadAnc payload;
                payload.setKeepAliveField(RtpPayloadAnc::FieldIndication::InterlacedField2);
                payload.setKeepAliveField(RtpPayloadAnc::FieldIndication::Invalid);
                // Last valid value sticks.
                CHECK(payload.keepAliveField() ==
                      RtpPayloadAnc::FieldIndication::InterlacedField2);
        }

        SUBCASE("C11: pack sorts records by ascending (Line, HOffset)") {
                // Three packets supplied in reverse line order.
                AncPacket::List packets;
                packets.pushToBack(makePacket(AncFormat::Cea708, 20, {0xAA},
                                              false, 0x100).packet());
                packets.pushToBack(makePacket(AncFormat::Afd, 10, {0xBB},
                                              false, 0x200).packet());
                packets.pushToBack(makePacket(AncFormat::AtcLtc, 10, {0xCC},
                                              false, 0x100).packet());

                RtpPayloadAnc   payload;
                RtpPacket::List rtp = payload.packAncFrame(packets, 1u);
                REQUIRE(rtp.size() == 1u);
                AncPacket::List out;
                REQUIRE(!payload.unpackAncPackets(rtp, out).isError());
                REQUIRE(out.size() == 3u);
                // Expected order: (10, 0x100), (10, 0x200), (20, 0x100).
                Result<St291Packet> r0 = St291Packet::from(out[0]);
                Result<St291Packet> r1 = St291Packet::from(out[1]);
                Result<St291Packet> r2 = St291Packet::from(out[2]);
                REQUIRE(isOk(r0));
                REQUIRE(isOk(r1));
                REQUIRE(isOk(r2));
                CHECK(value(r0).line() == 10);
                CHECK(value(r0).hOffset() == 0x100);
                CHECK(value(r1).line() == 10);
                CHECK(value(r1).hOffset() == 0x200);
                CHECK(value(r2).line() == 20);
                CHECK(value(r2).hOffset() == 0x100);
        }

        SUBCASE("C10: AudioMetadata-category packets are dropped on egress") {
                // Smpte2020Audio has category=AudioMetadata; build a
                // raw St291 packet against (DID 0x45, SDID 0x05) so it
                // resolves to that family via wildcard SDID lookup.
                List<uint16_t> udw;
                for (uint8_t b : {uint8_t{0x10}, uint8_t{0x20}, uint8_t{0x30}}) {
                        udw.pushToBack(b);
                }
                St291Packet audioPkt = St291Packet::buildRaw(
                        0x45, 0x05, udw, 11, St291Packet::UnspecifiedHOffset, false);
                AncPacket::List packets;
                packets.pushToBack(audioPkt.packet());
                packets.pushToBack(makePacket(AncFormat::Cea708, 11, {0x01}).packet());

                RtpPayloadAnc   payload;
                RtpPacket::List rtp = payload.packAncFrame(packets, 1u);
                REQUIRE(rtp.size() == 1u);
                AncPacket::List out;
                REQUIRE(!payload.unpackAncPackets(rtp, out).isError());
                REQUIRE(out.size() == 1u);
                CHECK(out[0].format().id() == AncFormat::Cea708);
        }

        SUBCASE("C7: AncPacket defaults ST 291 framing to the RFC sentinels") {
                // ST 2110-40 §5.2.2 / RP 168 recommend 0x7FE as the
                // "switching-point" sentinel when no exact line is
                // known.  AncPacket's Impl carries these defaults
                // directly (F9.1 conversion off the Metadata sidecar)
                // so a freshly built packet round-trips cleanly through
                // the carriage layer without the codec having to set
                // them.
                AncPacket fresh(AncFormat(AncFormat::Cea708), AncTransport::St291, Buffer());
                CHECK(fresh.st291Line() == 0x7FE);
                CHECK(fresh.st291HOffset() == 0xFFF);
                CHECK(fresh.st291FieldB() == false);
                CHECK(fresh.st291CBit() == false);
                CHECK(fresh.st291StreamNum() == 0);
        }

        SUBCASE("C7: St291Packet sentinel constants match the RFC") {
                CHECK(St291Packet::LineNoSpecific == 0x7FF);
                CHECK(St291Packet::LineSwitchingDefault == 0x7FE);
                CHECK(St291Packet::LineLargerThan11Bits == 0x7FD);
                CHECK(St291Packet::UnspecifiedLine == St291Packet::LineSwitchingDefault);
                CHECK(St291Packet::UnspecifiedHOffset == 0xFFF);
                CHECK(St291Packet::HOffsetInHanc == 0xFFE);
                CHECK(St291Packet::HOffsetInActiveVideo == 0xFFD);
                CHECK(St291Packet::HOffsetLargerThan12Bits == 0xFFC);
        }
}

// ============================================================================
// F3 — C7 end-to-end: codecs build packets with the 0x7FE sentinel by default.
// ============================================================================

#include <promeki/anctranslateconfig.h>
#include <promeki/anctranslator.h>
#include <promeki/cea708cdp.h>
#include <promeki/variant.h>

TEST_CASE("CEA-708 codec defaults Line to UnspecifiedLine when no cfg supplied") {
        AncTranslator     t;  // default cfg
        Cea708Cdp::CcDataList triples;
        triples.pushToBack({true, 0, 0x94, 0x20});
        Cea708Cdp                cdp(4, triples, 1);
        AncTranslator::PacketsResult  built =
                t.build(Variant(cdp), AncFormat(AncFormat::Cea708), AncTransport::St291);
        REQUIRE(built.second().isOk());
        Result<St291Packet> rp = St291Packet::from(built.first().front());
        REQUIRE(isOk(rp));
        CHECK(value(rp).line() == St291Packet::UnspecifiedLine); // 0x7FE
}

// ============================================================================
// F7 — AncChecksumPolicy wiring on the depacketizer (ancaudit.md F7 / B2)
// ============================================================================

namespace {

        // Constructs an AncPacket on the St291 transport with the same
        // header layout as a freshly-built CEA-708 packet, but whose
        // stored §6.4 Checksum_Word has been corrupted by flipping the
        // low bit of the wire buffer's last byte.  Used to drive the
        // unpacker through StrictValidate's rejection path without
        // having to surgically rewrite RTP bytes.
        AncPacket cea708PacketWithBadChecksum() {
                List<uint16_t> udw;
                for (uint8_t v = 0x10; v < 0x18; ++v) udw.pushToBack(v);
                St291Packet good = St291Packet::build(AncFormat(AncFormat::Cea708), udw, 11);
                REQUIRE(good.isValid());

                const Buffer &wire = good.packet().data();
                Buffer        flipped(wire.size());
                Error         cpyErr = flipped.copyFrom(wire.data(), wire.size(), 0);
                REQUIRE(cpyErr.isOk());
                flipped.setSize(wire.size());
                static_cast<uint8_t *>(flipped.data())[wire.size() - 1] ^= 0x01u;

                return AncPacket(good.packet().format(), AncTransport::St291, std::move(flipped),
                                 Metadata(good.packet().meta()));
        }

} // namespace

TEST_CASE("RtpPayloadAnc F7 — default policy preserves a captured bad-checksum record") {
        // Default PreserveOrRecompute mirrors ancaudit.md Q6's
        // byte-exact replay contract: the depacketizer emits the record
        // verbatim and leaves checksum verification to the codec layer.
        AncPacket::List packets;
        packets.pushToBack(cea708PacketWithBadChecksum());

        RtpPayloadAnc   payload;
        RtpPacket::List rtp = payload.packAncFrame(packets, 0xCAFEBABEu);
        REQUIRE(rtp.size() == 1u);

        AncPacket::List out;
        Error           err = payload.unpackAncPackets(rtp, out);
        CHECK(err == Error::Ok);
        REQUIRE(out.size() == 1u);

        Result<St291Packet> rp = St291Packet::from(out[0]);
        REQUIRE(isOk(rp));
        CHECK_FALSE(value(rp).checksumValid());  // bad CS rode through the depacketizer
}

TEST_CASE("RtpPayloadAnc F7 — StrictValidate rejects a record with a bad checksum") {
        AncPacket::List packets;
        packets.pushToBack(cea708PacketWithBadChecksum());

        RtpPayloadAnc   payload;
        RtpPacket::List rtp = payload.packAncFrame(packets, 0xCAFEBABEu);
        REQUIRE(rtp.size() == 1u);

        AncPacket::List out;
        Error           err =
                payload.unpackAncPackets(rtp, out, AncChecksumPolicy::StrictValidate);
        CHECK(err == Error::InvalidChecksum);
        // The failure happens during per-record promotion; no records
        // were appended for this single-record payload.
        CHECK(out.isEmpty());
}

TEST_CASE("RtpPayloadAnc F7 — StrictValidate accepts a clean record") {
        List<uint8_t> bytes;
        for (uint8_t v = 0x10; v < 0x18; ++v) bytes.pushToBack(v);
        St291Packet     src = makePacket(AncFormat::Cea708, 11, bytes);
        AncPacket::List packets;
        packets.pushToBack(src.packet());

        RtpPayloadAnc   payload;
        RtpPacket::List rtp = payload.packAncFrame(packets, 0xCAFEBABEu);
        REQUIRE(rtp.size() == 1u);

        AncPacket::List out;
        Error           err =
                payload.unpackAncPackets(rtp, out, AncChecksumPolicy::StrictValidate);
        CHECK(err == Error::Ok);
        REQUIRE(out.size() == 1u);
        Result<St291Packet> rp = St291Packet::from(out[0], AncChecksumPolicy::StrictValidate);
        REQUIRE(isOk(rp));
        CHECK(value(rp).checksumValid());
}
