/**
 * @file      rtpancpacketizerthread.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>

#include <promeki/ancdesc.h>
#include <promeki/ancformat.h>
#include <promeki/ancpacket.h>
#include <promeki/ancpayload.h>
#include <promeki/frame.h>
#include <promeki/framenumber.h>
#include <promeki/queue.h>
#include <promeki/rtpancpacketizerthread.h>
#include <promeki/rtppacketbatch.h>
#include <promeki/rtppayloadanc.h>
#include <promeki/st291packet.h>

using namespace promeki;

namespace {

St291Packet makePacket(AncFormat::ID id, uint16_t line,
                       const List<uint8_t> &bodyBytes) {
        List<uint16_t> udw;
        udw.reserve(bodyBytes.size());
        for (uint8_t b : bodyBytes) udw.pushToBack(static_cast<uint16_t>(b));
        return St291Packet::build(AncFormat(id), udw, line);
}

Frame buildFrameWithAnc(const AncPacket::List &packets) {
        AncDesc         desc(Size2Du32(1920, 1080), VideoScanMode::Progressive, FrameRate::FPS_60);
        AncPayload::Ptr ap = AncPayload::Ptr::create(desc, packets);
        Frame           frame;
        frame.addPayload(ap);
        return frame;
}

} // namespace

TEST_CASE("RtpAncPacketizerThread: packs one AncPayload into one RtpPacketBatch") {
        Queue<RtpPacketBatch> txQ;
        RtpPayloadAnc         payload;

        RtpAncPacketizerContext ctx;
        ctx.streamIdx = 0;
        ctx.clockRateHz = 90000;
        ctx.payload = &payload;
        ctx.txPacketQueue = &txQ;

        RtpAncPacketizerThread pkt(ctx);

        AncPacket::List anc;
        anc.pushToBack(makePacket(AncFormat::Cea708, 11, {0x10, 0x20}).packet());
        anc.pushToBack(makePacket(AncFormat::AtcLtc, 12, {0xAA, 0xBB, 0xCC}).packet());

        RtpFrameWork work;
        work.frame = buildFrameWithAnc(anc);
        work.frameIndex = FrameNumber(42);

        pkt.packetizeForTest(work);

        REQUIRE(txQ.size() == 1u);
        auto popped = txQ.tryPop();
        REQUIRE(popped.second().isOk());
        const RtpPacketBatch &batch = popped.first();
        CHECK(batch.clockRate == 90000u);
        CHECK(batch.frameIndex == FrameNumber(42));
        CHECK(batch.markerOnLast == true);
        REQUIRE(batch.packets.size() >= 1u);
        // Marker on the final packet is sealed by packAncFrame; TX
        // thread re-stamps RTP TS, so we don't check the TS here.
        CHECK(batch.packets[batch.packets.size() - 1].marker());
}

TEST_CASE("RtpAncPacketizerThread: empty AncPayload produces no batch") {
        Queue<RtpPacketBatch> txQ;
        RtpPayloadAnc         payload;
        RtpAncPacketizerContext ctx{
                /*streamIdx=*/0, /*clockRateHz=*/90000u, &payload, &txQ};
        RtpAncPacketizerThread pkt(ctx);

        RtpFrameWork work;
        work.frame = buildFrameWithAnc(AncPacket::List());
        work.frameIndex = FrameNumber(0);
        pkt.packetizeForTest(work);
        CHECK(txQ.size() == 0u);
}

TEST_CASE("RtpAncPacketizerThread: missing AncPayload at streamIdx is a no-op") {
        Queue<RtpPacketBatch> txQ;
        RtpPayloadAnc         payload;
        RtpAncPacketizerContext ctx{
                /*streamIdx=*/5, /*clockRateHz=*/90000u, &payload, &txQ};
        RtpAncPacketizerThread pkt(ctx);

        AncPacket::List anc;
        anc.pushToBack(makePacket(AncFormat::Cea708, 11, {0x01}).packet());
        RtpFrameWork work;
        work.frame = buildFrameWithAnc(anc);
        work.frameIndex = FrameNumber(0);
        pkt.packetizeForTest(work);
        CHECK(txQ.size() == 0u);
}
