/**
 * @file      ntv2anc.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/config.h>

#if PROMEKI_ENABLE_NTV2

#include <cstdint>
#include <doctest/doctest.h>
#include <promeki/ancdesc.h>
#include <promeki/ancformat.h>
#include <promeki/ancmeta.h>
#include <promeki/ancpacket.h>
#include <promeki/ancpayload.h>
#include <promeki/buffer.h>
#include <promeki/enums_video.h>
#include <promeki/framerate.h>
#include <promeki/list.h>
#include <promeki/ntv2anc.h>
#include <promeki/size2d.h>
#include <promeki/st291packet.h>

using namespace promeki;

namespace {

        // Find a packet on a payload that matches @p did / @p sdid.
        // Returns an invalid St291Packet when no match exists.
        St291Packet findByDid(const AncPayload &payload, uint8_t did, uint8_t sdid) {
                const AncPacket::List &pkts = payload.packets();
                for (size_t i = 0; i < pkts.size(); ++i) {
                        Result<St291Packet> rs = St291Packet::from(pkts[i]);
                        if (rs.second().isError()) continue;
                        const St291Packet &sp = rs.first();
                        if (sp.did() == did && sp.sdid() == sdid) return sp;
                }
                return St291Packet{};
        }

} // namespace

TEST_CASE("Ntv2Anc: GUMP encode + decode round-trips a CEA-708 packet") {
        // Build a single CEA-708 packet on line 11, F1.
        List<uint16_t> udw;
        for (uint8_t b : {uint8_t(0x96), uint8_t(0x69), uint8_t(0x07), uint8_t(0x80), uint8_t(0xA1),
                          uint8_t(0xA2), uint8_t(0xA3)}) {
                udw.pushToBack(b);
        }
        St291Packet original = St291Packet::build(AncFormat(AncFormat::Cea708), udw, /*line=*/11);
        REQUIRE(original.isValid());

        AncDesc desc(Size2Du32(1920, 1080), VideoScanMode::Progressive, FrameRate(FrameRate::FPS_60));
        AncPayload payload(desc);
        payload.addPacket(original);

        // Encode to GUMP buffers.
        Buffer f1Buf(Ntv2Anc::kPreferredBufBytes);
        Buffer f2Buf(Ntv2Anc::kPreferredBufBytes);
        REQUIRE(f1Buf.isValid());
        REQUIRE(f2Buf.isValid());
        // Buffer(N) reserves N bytes but leaves size() at zero;
        // setSize promotes the live-byte count so size() matches
        // the buffer capacity we're about to hand to AJA.
        f1Buf.setSize(Ntv2Anc::kPreferredBufBytes);
        f2Buf.setSize(Ntv2Anc::kPreferredBufBytes);
        Error encErr = Ntv2Anc::packetsToNtv2Anc(payload, static_cast<uint8_t *>(f1Buf.data()),
                                                 f1Buf.size(), static_cast<uint8_t *>(f2Buf.data()),
                                                 f2Buf.size(), /*isProgressive=*/true,
                                                 /*f2StartLine=*/0);
        REQUIRE(encErr.isOk());

        // Decode back.
        AncPayload::Ptr decoded = Ntv2Anc::ntv2AncToPackets(
                static_cast<const uint8_t *>(f1Buf.data()), f1Buf.size(), nullptr, 0,
                Size2Du32(1920, 1080), VideoScanMode::Progressive, FrameRate(FrameRate::FPS_60));
        REQUIRE(decoded.isValid());
        CHECK(decoded->packets().size() == 1);

        St291Packet round = findByDid(*decoded, 0x61, 0x01);
        REQUIRE(round.isValid());
        CHECK(round.did() == 0x61);
        CHECK(round.sdid() == 0x01);
        CHECK(round.line() == 11);
        CHECK(round.fieldB() == false);

        // 8-bit payload data round-trips exactly (parity bits in the
        // upper two bits of each UDW are recomputed by AJA on encode
        // and decode, so we mask before compare).
        List<uint16_t> decUdw = round.udw();
        REQUIRE(decUdw.size() == udw.size());
        for (size_t i = 0; i < udw.size(); ++i) {
                CHECK((decUdw[i] & 0xFF) == (udw[i] & 0xFF));
        }
}

TEST_CASE("Ntv2Anc: round-trips AFD + ATC LTC + unregistered packet on F1 + F2") {
        // AFD on line 11 (DID 0x41 / SDID 0x05)
        List<uint16_t> afdUdw;
        afdUdw.pushToBack(uint8_t(0xCC)); // BAR data flags + AFD code
        afdUdw.pushToBack(uint8_t(0x10));
        afdUdw.pushToBack(uint8_t(0x00));
        afdUdw.pushToBack(uint8_t(0x00));
        St291Packet afd = St291Packet::build(AncFormat(AncFormat::Afd), afdUdw, /*line=*/11);
        REQUIRE(afd.isValid());

        // ATC LTC on line 13 (DID 0x60 / SDID 0x60).  Plausible 16-byte
        // packet shape — the bytes themselves aren't decoded, just
        // round-tripped.
        List<uint16_t> atcUdw;
        for (uint8_t b : {uint8_t(0x00), uint8_t(0x80), uint8_t(0x00), uint8_t(0x80), uint8_t(0x00),
                          uint8_t(0x80), uint8_t(0x00), uint8_t(0x80), uint8_t(0x00), uint8_t(0x80),
                          uint8_t(0x00), uint8_t(0x80), uint8_t(0x00), uint8_t(0x80), uint8_t(0x00),
                          uint8_t(0x80)}) {
                atcUdw.pushToBack(b);
        }
        // ATC LTC is sdid 0x60 per the registry table.
        St291Packet atc =
                St291Packet::buildRaw(0x60, 0x60, atcUdw, /*line=*/13);
        REQUIRE(atc.isValid());

        // Unregistered DID/SDID — proves we preserve wire data even
        // when AncFormat::fromSt291DidSdid returns Invalid.
        List<uint16_t> userUdw;
        for (uint8_t b : {uint8_t(0xDE), uint8_t(0xAD), uint8_t(0xBE), uint8_t(0xEF)}) {
                userUdw.pushToBack(b);
        }
        St291Packet user = St291Packet::buildRaw(0x55, 0x77, userUdw, /*line=*/570, /*hOffset=*/0xFFF,
                                                 /*fieldB=*/true);
        REQUIRE(user.isValid());

        AncDesc desc(Size2Du32(1920, 1080), VideoScanMode::Interlaced,
                     FrameRate(FrameRate::FPS_59_94));
        AncPayload payload(desc);
        payload.addPacket(afd);
        payload.addPacket(atc);
        payload.addPacket(user);

        Buffer f1Buf(Ntv2Anc::kPreferredBufBytes);
        Buffer f2Buf(Ntv2Anc::kPreferredBufBytes);
        REQUIRE(f1Buf.isValid());
        REQUIRE(f2Buf.isValid());
        // Buffer(N) reserves N bytes but leaves size() at zero;
        // setSize promotes the live-byte count so size() matches
        // the buffer capacity we're about to hand to AJA.
        f1Buf.setSize(Ntv2Anc::kPreferredBufBytes);
        f2Buf.setSize(Ntv2Anc::kPreferredBufBytes);

        // Interlaced 1080i, F2 starts at line 564 (SMPTE 274M).
        Error encErr = Ntv2Anc::packetsToNtv2Anc(payload, static_cast<uint8_t *>(f1Buf.data()),
                                                 f1Buf.size(), static_cast<uint8_t *>(f2Buf.data()),
                                                 f2Buf.size(), /*isProgressive=*/false,
                                                 /*f2StartLine=*/564);
        REQUIRE(encErr.isOk());

        AncPayload::Ptr decoded = Ntv2Anc::ntv2AncToPackets(
                static_cast<const uint8_t *>(f1Buf.data()), f1Buf.size(),
                static_cast<const uint8_t *>(f2Buf.data()), f2Buf.size(), Size2Du32(1920, 1080),
                VideoScanMode::Interlaced, FrameRate(FrameRate::FPS_59_94));
        REQUIRE(decoded.isValid());
        CHECK(decoded->packets().size() == 3);

        St291Packet decAfd = findByDid(*decoded, 0x41, 0x05);
        REQUIRE(decAfd.isValid());
        CHECK(decAfd.line() == 11);
        CHECK(decAfd.fieldB() == false);

        St291Packet decAtc = findByDid(*decoded, 0x60, 0x60);
        REQUIRE(decAtc.isValid());
        CHECK(decAtc.line() == 13);
        CHECK(decAtc.fieldB() == false);

        St291Packet decUser = findByDid(*decoded, 0x55, 0x77);
        REQUIRE(decUser.isValid());
        CHECK(decUser.line() == 570);
        CHECK(decUser.fieldB() == true);
        // Wire bytes preserved even though AncFormat lookup is Invalid.
        List<uint16_t> decUserUdw = decUser.udw();
        REQUIRE(decUserUdw.size() == userUdw.size());
        for (size_t i = 0; i < userUdw.size(); ++i) {
                CHECK((decUserUdw[i] & 0xFF) == (userUdw[i] & 0xFF));
        }
}

TEST_CASE("Ntv2Anc: empty payload encodes to a zeroed buffer that decodes to no packets") {
        AncDesc desc(Size2Du32(1920, 1080), VideoScanMode::Progressive, FrameRate(FrameRate::FPS_60));
        AncPayload empty(desc);

        Buffer f1Buf(Ntv2Anc::kPreferredBufBytes);
        REQUIRE(f1Buf.isValid());
        f1Buf.setSize(Ntv2Anc::kPreferredBufBytes);
        Error encErr = Ntv2Anc::packetsToNtv2Anc(empty, static_cast<uint8_t *>(f1Buf.data()),
                                                 f1Buf.size(), nullptr, 0,
                                                 /*isProgressive=*/true, /*f2StartLine=*/0);
        CHECK(encErr.isOk());

        AncPayload::Ptr decoded = Ntv2Anc::ntv2AncToPackets(
                static_cast<const uint8_t *>(f1Buf.data()), f1Buf.size(), nullptr, 0,
                Size2Du32(1920, 1080), VideoScanMode::Progressive, FrameRate(FrameRate::FPS_60));
        REQUIRE(decoded.isValid());
        CHECK(decoded->packets().isEmpty());
}

TEST_CASE("Ntv2Anc: packetsToNtv2Anc rejects a null F1 buffer") {
        AncDesc desc(Size2Du32(1920, 1080), VideoScanMode::Progressive, FrameRate(FrameRate::FPS_60));
        AncPayload empty(desc);
        Error      err = Ntv2Anc::packetsToNtv2Anc(empty, nullptr, 0, nullptr, 0,
                                              /*isProgressive=*/true, /*f2StartLine=*/0);
        CHECK(err.isError());
        CHECK(err == Error::InvalidArgument);
}

#endif // PROMEKI_ENABLE_NTV2
