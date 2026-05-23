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
#include <promeki/duration.h>
#include <promeki/frame.h>
#include <promeki/framenumber.h>
#include <promeki/framerate.h>
#include <promeki/phcclock.h>
#include <promeki/queue.h>
#include <promeki/rtpancpacketizerthread.h>
#include <promeki/rtpmediaclock.h>
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

TEST_CASE("RtpAncPacketizerThread: empty AncPayload produces a §5.5 keep-alive batch") {
        Queue<RtpPacketBatch> txQ;
        RtpPayloadAnc         payload;
        RtpAncPacketizerContext ctx{
                /*streamIdx=*/0, /*clockRateHz=*/90000u, &payload, &txQ};
        RtpAncPacketizerThread pkt(ctx);

        RtpFrameWork work;
        work.frame = buildFrameWithAnc(AncPacket::List());
        work.frameIndex = FrameNumber(0);
        pkt.packetizeForTest(work);
        // ST 2110-40 §5.5: an empty ANC frame still produces one
        // keep-alive RTP packet (ANC_Count=0, Length=0, Marker=1).
        REQUIRE(txQ.size() == 1u);
        Result<RtpPacketBatch> popped = txQ.tryPop();
        REQUIRE(popped.second().isOk());
        const RtpPacketBatch &batch = popped.first();
        REQUIRE(batch.packets.size() == 1u);
        CHECK(batch.packets[0].marker());
        CHECK(batch.packets[0].payloadSize() == RtpPayloadAnc::PayloadHeaderSize);
}

TEST_CASE("RtpAncPacketizerThread: no LLTM context → batch.deadlineTaiNs is zero") {
        Queue<RtpPacketBatch>   txQ;
        RtpPayloadAnc           payload;
        RtpAncPacketizerContext ctx;
        ctx.streamIdx = 0;
        ctx.clockRateHz = 90000;
        ctx.payload = &payload;
        ctx.txPacketQueue = &txQ;
        // lltmEnabled stays false; mediaClock stays null → no deadline.
        RtpAncPacketizerThread pkt(ctx);

        AncPacket::List anc;
        anc.pushToBack(makePacket(AncFormat::Cea708, 11, {0x01}).packet());
        RtpFrameWork work;
        work.frame = buildFrameWithAnc(anc);
        work.frameIndex = FrameNumber(7);
        pkt.packetizeForTest(work);

        REQUIRE(txQ.size() == 1u);
        Result<RtpPacketBatch> popped = txQ.tryPop();
        REQUIRE(popped.second().isOk());
        CHECK(popped.first().deadlineTaiNs == 0u);
}

TEST_CASE("RtpAncPacketizerThread: LLTM enabled but no PTP anchor → no deadline") {
        Queue<RtpPacketBatch>   txQ;
        RtpPayloadAnc           payload;
        // Frame-zero-anchored media clock has no PTP wallclock.
        RtpMediaClock           mediaClock = RtpMediaClock::frameZeroAnchored(
                90000u, FrameRate(FrameRate::FPS_60));
        RtpAncPacketizerContext ctx;
        ctx.streamIdx = 0;
        ctx.clockRateHz = 90000;
        ctx.payload = &payload;
        ctx.txPacketQueue = &txQ;
        ctx.lltmEnabled = true;
        ctx.mediaClock = &mediaClock;
        ctx.tD = Duration::fromNanoseconds(59'259); // ≈ 8 / (60 × 2250)
        RtpAncPacketizerThread pkt(ctx);

        RtpFrameWork work;
        work.frame = buildFrameWithAnc(AncPacket::List());
        work.frameIndex = FrameNumber(0);
        pkt.packetizeForTest(work);
        REQUIRE(txQ.size() == 1u);
        // hasPtpAnchor() returns false on a frame-zero-anchored clock;
        // the packetizer silently degrades to no deadline.
        CHECK(txQ.tryPop().first().deadlineTaiNs == 0u);
}

TEST_CASE("RtpAncPacketizerThread: LLTM PTP-anchored → deadline = T_FST + T_EPO + T_D") {
        Queue<RtpPacketBatch>   txQ;
        RtpPayloadAnc           payload;
        // Pick a deterministic UTC anchor a few seconds past the Unix
        // epoch so the math is easy to verify by hand.  Frame 0 maps
        // to anchorUtcNs; frame 1 advances by exactly one frame
        // duration at 60 fps integer (16'666'667 ns).
        const int64_t anchorUtcNs = 1'000'000'000'000LL; // 2001-09-09 ~ish
        RtpMediaClock mediaClock = RtpMediaClock::ptpAnchored(
                90000u, FrameRate(FrameRate::FPS_60), anchorUtcNs);

        const Duration tEpo = Duration::fromMicroseconds(125); // arbitrary
        const Duration tD = Duration::fromNanoseconds(59'259); // ≈ 8/(60×2250)
        RtpAncPacketizerContext ctx;
        ctx.streamIdx = 0;
        ctx.clockRateHz = 90000;
        ctx.payload = &payload;
        ctx.txPacketQueue = &txQ;
        ctx.lltmEnabled = true;
        ctx.mediaClock = &mediaClock;
        ctx.trOffset = tEpo;
        ctx.tD = tD;
        RtpAncPacketizerThread pkt(ctx);

        // Frame 0 — T_FST = anchorUtcNs.  Deadline (UTC) =
        // anchorUtcNs + 125 µs + ≈59 µs.  TAI version adds the system
        // TAI-UTC offset (typically 37 s).  We re-derive the expected
        // value using the same helper the packetizer calls.
        RtpFrameWork work;
        work.frame = buildFrameWithAnc(AncPacket::List());
        work.frameIndex = FrameNumber(0);
        pkt.packetizeForTest(work);
        REQUIRE(txQ.size() == 1u);
        Result<RtpPacketBatch> popped = txQ.tryPop();
        REQUIRE(popped.second().isOk());
        const int64_t tFst = mediaClock.tvdUtcNs(0);
        const int64_t deadlineUtc = tFst + tEpo.nanoseconds() + tD.nanoseconds();
        const uint64_t expectedTai = PhcClock::utcNsToTaiNs(deadlineUtc);
        CHECK(popped.first().deadlineTaiNs == expectedTai);
        CHECK(popped.first().deadlineTaiNs > 0u);
}

TEST_CASE("RtpAncPacketizerThread: LLTM frame advances deadline by exactly one frame interval") {
        Queue<RtpPacketBatch>   txQ;
        RtpPayloadAnc           payload;
        const int64_t           anchorUtcNs = 1'000'000'000'000LL;
        RtpMediaClock           mediaClock = RtpMediaClock::ptpAnchored(
                90000u, FrameRate(FrameRate::FPS_60), anchorUtcNs);
        RtpAncPacketizerContext ctx;
        ctx.streamIdx = 0;
        ctx.clockRateHz = 90000;
        ctx.payload = &payload;
        ctx.txPacketQueue = &txQ;
        ctx.lltmEnabled = true;
        ctx.mediaClock = &mediaClock;
        ctx.tD = Duration::fromNanoseconds(59'259);
        RtpAncPacketizerThread pkt(ctx);

        RtpFrameWork w0;
        w0.frame = buildFrameWithAnc(AncPacket::List());
        w0.frameIndex = FrameNumber(0);
        pkt.packetizeForTest(w0);
        RtpFrameWork w1;
        w1.frame = buildFrameWithAnc(AncPacket::List());
        w1.frameIndex = FrameNumber(1);
        pkt.packetizeForTest(w1);

        REQUIRE(txQ.size() == 2u);
        auto b0 = txQ.tryPop().first();
        auto b1 = txQ.tryPop().first();
        REQUIRE(b0.deadlineTaiNs > 0u);
        REQUIRE(b1.deadlineTaiNs > 0u);
        // 60 fps integer → exactly 16'666'666 or 16'666'667 ns per
        // frame.  Allow ±1 ns floor-division slack.
        const int64_t diff = static_cast<int64_t>(b1.deadlineTaiNs) -
                             static_cast<int64_t>(b0.deadlineTaiNs);
        CHECK(diff >= 16'666'665LL);
        CHECK(diff <= 16'666'668LL);
}

TEST_CASE("RtpAncPacketizerThread: missing AncPayload at streamIdx emits a keep-alive") {
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
        // streamIdx 5 has no AncPayload — emit a keep-alive instead
        // of dropping silently, per ST 2110-40 §5.5.
        REQUIRE(txQ.size() == 1u);
        Result<RtpPacketBatch> popped = txQ.tryPop();
        REQUIRE(popped.second().isOk());
        const RtpPacketBatch &batch = popped.first();
        REQUIRE(batch.packets.size() == 1u);
        CHECK(batch.packets[0].marker());
        CHECK(batch.packets[0].payloadSize() == RtpPayloadAnc::PayloadHeaderSize);
}
