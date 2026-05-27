/**
 * @file      rtpvideodepacketizerthread.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>

#include <cstring>

#include <promeki/atomic.h>
#include <promeki/buffer.h>
#include <promeki/clockdomain.h>
#include <promeki/duration.h>
#include <promeki/imagedesc.h>
#include <promeki/pixelformat.h>
#include <promeki/queue.h>
#include <promeki/rtppacket.h>
#include <promeki/rtppayload.h>
#include <promeki/rtppayloadrawvideo.h>
#include <promeki/rtpvideodepacketizerthread.h>
#include <promeki/timestamp.h>

using namespace promeki;

namespace {

constexpr uint32_t kVideoClockHz = 90000u;

// Builds a small RFC 4175 raw-video frame's worth of packets.
// @p width / @p height / @p bpp determine the payload sizes; the
// test passes a non-trivial pixel count so the packet list ends
// up with at least 2 packets at the default packetSize.
RtpPacket::List buildRawVideoPackets(int width, int height, int bpp,
                                     uint32_t rtpTs, uint16_t baseSeq,
                                     const TimeStamp &arrivalSteady = TimeStamp::now()) {
        RtpPayloadRawVideo payload(width, height, bpp);
        // Build a synthetic frame: one byte per pixel * channels.
        const size_t bytes = static_cast<size_t>(width) * height *
                            (static_cast<size_t>(bpp) / 8);
        Buffer frame(bytes);
        std::memset(frame.data(), 0xAA, bytes);
        frame.setSize(bytes);
        RtpPacket::List pkts = payload.pack(frame.data(), bytes);
        for (size_t i = 0; i < pkts.size(); i++) {
                pkts[i].setPayloadType(96);
                pkts[i].setSequenceNumber(static_cast<uint16_t>(baseSeq + i));
                pkts[i].setTimestamp(rtpTs);
                pkts[i].setSsrc(0xCAFEBABEu);
                pkts[i].arrivalSteady = arrivalSteady;
        }
        if (!pkts.isEmpty()) {
                pkts[pkts.size() - 1].setMarker(true);
        }
        return pkts;
}

struct Harness {
        Queue<RxVideoFrame> payloadQ;
        Atomic<uint32_t>    resetEpoch;
        bool                active = true;
        // payload + readerImageDesc are owned by the test; the
        // depacketizer reads/writes through pointers in the ctx.
        bool                hasSr = false;
        Atomic<int64_t>     packetsReceived;
        Atomic<int64_t>     bytesReceived;
        Atomic<int64_t>     lastPacketArrivalNs;
        Atomic<int64_t>     framesReassembled;
        Atomic<int64_t>     framesDroppedValidate;
        Atomic<int64_t>     framesWaitingParamSets;
        Atomic<int64_t>     framesDroppedSsrcReset;
        int                 noteFrameCalls = 0;

        RtpVideoDepacketizerContext makeCtx(RtpPayload *payload,
                                            ImageDesc *idesc) {
                RtpVideoDepacketizerContext ctx;
                ctx.payloadQueue = &payloadQ;
                ctx.resetEpoch = &resetEpoch;
                ctx.active = &active;
                ctx.payload = payload;
                ctx.readerImageDesc = idesc;
                ctx.clockDomain = ClockDomain::SystemMonotonic;
                ctx.hasSr = &hasSr;
                ctx.packetsReceived = &packetsReceived;
                ctx.bytesReceived = &bytesReceived;
                ctx.lastPacketArrivalNs = &lastPacketArrivalNs;
                ctx.framesReassembled = &framesReassembled;
                ctx.framesDroppedValidate = &framesDroppedValidate;
                ctx.framesWaitingParamSets = &framesWaitingParamSets;
                ctx.framesDroppedSsrcReset = &framesDroppedSsrcReset;
                ctx.noteFrameReceived = [this]() { ++noteFrameCalls; };
                return ctx;
        }
};

} // namespace

TEST_CASE("RtpVideoDepacketizerThread: marker-bit assembles + emits one RxVideoFrame") {
        Harness            h;
        RtpPayloadRawVideo payload(64, 36, 24);
        ImageDesc          idesc;
        idesc.setSize(Size2Du32(64, 36));
        idesc.setPixelFormat(PixelFormat::RGB8_sRGB);

        RtpVideoDepacketizerThread depkt(h.makeCtx(&payload, &idesc),
                                         String("RtpVidDepkt"), kVideoClockHz);
        RtpPacket::List pkts = buildRawVideoPackets(64, 36, 24, 1000u, 0u);
        REQUIRE(!pkts.isEmpty());
        for (size_t i = 0; i < pkts.size(); i++) {
                depkt.handlePacketForTest(pkts[i]);
        }

        REQUIRE(h.payloadQ.size() == 1u);
        Result<RxVideoFrame> r = h.payloadQ.tryPop();
        REQUIRE(r.second().isOk());
        const RxVideoFrame &v = r.first();
        CHECK(v.rtpTimestamp == 1000u);
        CHECK(v.packetCount == static_cast<int32_t>(pkts.size()));
        CHECK(v.payload.isValid());
        CHECK(v.imageDesc.isValid());
        CHECK(v.streamFrameIndex.value() == 0u);
}

TEST_CASE("RtpVideoDepacketizerThread: streamFrameIndex advances per emitted frame") {
        Harness            h;
        RtpPayloadRawVideo payload(64, 36, 24);
        ImageDesc          idesc;
        idesc.setSize(Size2Du32(64, 36));
        idesc.setPixelFormat(PixelFormat::RGB8_sRGB);

        RtpVideoDepacketizerThread depkt(h.makeCtx(&payload, &idesc),
                                         String("RtpVidDepkt"), kVideoClockHz);
        const TimeStamp t0 = TimeStamp::now();
        for (int i = 0; i < 3; i++) {
                RtpPacket::List pkts = buildRawVideoPackets(
                        64, 36, 24, 1000u + static_cast<uint32_t>(i * 3000),
                        static_cast<uint16_t>(i * 100),
                        t0 + Duration::fromMilliseconds(i * 33));
                for (size_t k = 0; k < pkts.size(); k++) {
                        depkt.handlePacketForTest(pkts[k]);
                }
        }

        REQUIRE(h.payloadQ.size() == 3u);
        for (uint64_t i = 0; i < 3; i++) {
                Result<RxVideoFrame> r = h.payloadQ.tryPop();
                REQUIRE(r.second().isOk());
                CHECK(r.first().streamFrameIndex.value() == i);
        }
}

TEST_CASE("RtpVideoDepacketizerThread: timestamp-change flush emits a stuck frame") {
        Harness            h;
        RtpPayloadRawVideo payload(64, 36, 24);
        ImageDesc          idesc;
        idesc.setSize(Size2Du32(64, 36));
        idesc.setPixelFormat(PixelFormat::RGB8_sRGB);

        RtpVideoDepacketizerThread depkt(h.makeCtx(&payload, &idesc),
                                         String("RtpVidDepkt"), kVideoClockHz);

        // First frame — strip the marker bit so the depacketizer
        // doesn't emit on the marker.  The next frame's first
        // packet (different timestamp) must trigger an emit.
        RtpPacket::List p1 = buildRawVideoPackets(64, 36, 24, 1000u, 0u);
        for (size_t i = 0; i < p1.size(); i++) p1[i].setMarker(false);
        for (size_t i = 0; i < p1.size(); i++) depkt.handlePacketForTest(p1[i]);
        CHECK(h.payloadQ.size() == 0u);

        RtpPacket::List p2 = buildRawVideoPackets(64, 36, 24, 5000u, 100u);
        for (size_t i = 0; i < p2.size(); i++) depkt.handlePacketForTest(p2[i]);

        // Two emissions: first via timestamp-change flush, second
        // via the marker on the trailing packet of frame 2.
        REQUIRE(h.payloadQ.size() == 2u);
        Result<RxVideoFrame> r1 = h.payloadQ.tryPop();
        Result<RxVideoFrame> r2 = h.payloadQ.tryPop();
        REQUIRE(r1.second().isOk());
        REQUIRE(r2.second().isOk());
        CHECK(r1.first().rtpTimestamp == 1000u);
        CHECK(r2.first().rtpTimestamp == 5000u);
}

TEST_CASE("RtpVideoDepacketizerThread: SSRC reset drops reassembly but preserves config-derived image desc") {
        Harness            h;
        RtpPayloadRawVideo payload(64, 36, 24);
        ImageDesc          idesc;
        idesc.setSize(Size2Du32(64, 36));
        idesc.setPixelFormat(PixelFormat::RGB8_sRGB);

        RtpVideoDepacketizerThread depkt(h.makeCtx(&payload, &idesc),
                                         String("RtpVidDepkt"), kVideoClockHz);

        RtpPacket::List p1 = buildRawVideoPackets(64, 36, 24, 1000u, 0u);
        for (size_t i = 0; i < p1.size(); i++) p1[i].setMarker(false);
        for (size_t i = 0; i < p1.size(); i++) depkt.handlePacketForTest(p1[i]);
        CHECK(h.payloadQ.size() == 0u);

        // Reset epoch.  The reset path drops the in-flight
        // reassembly state but must NOT clear the
        // @c readerImageDesc — for raw RFC 4175 the geometry was
        // pinned at @c configureVideoStream time from
        // @c VideoSize + @c VideoPixelFormat and never arrives over
        // the wire.  Clearing it would permanently disable the
        // raw receiver because @c emitFrame's
        // @c !idesc.isValid() guard would drop every post-reset
        // frame.
        h.resetEpoch.setValue(1u);

        RtpPacket::List p2 = buildRawVideoPackets(64, 36, 24, 5000u, 100u);
        REQUIRE(!p2.isEmpty());
        for (size_t i = 0; i < p2.size(); i++) depkt.handlePacketForTest(p2[i]);

        REQUIRE(h.payloadQ.size() == 1u);
        Result<RxVideoFrame> r = h.payloadQ.tryPop();
        REQUIRE(r.second().isOk());
        CHECK(r.first().rtpTimestamp == 5000u);
        CHECK(h.framesDroppedSsrcReset.value() == 1);
        // streamFrameIndex was reset to 0 by the SSRC reset path.
        CHECK(r.first().streamFrameIndex.value() == 0u);
        // readerImageDesc survives the reset.
        CHECK(idesc.isValid());
        CHECK(idesc.size() == Size2Du32(64, 36));
        CHECK(idesc.pixelFormat() == PixelFormat(PixelFormat::RGB8_sRGB));
}

TEST_CASE("RtpVideoDepacketizerThread: stat counters advance on accepted packets") {
        Harness            h;
        RtpPayloadRawVideo payload(64, 36, 24);
        ImageDesc          idesc;
        idesc.setSize(Size2Du32(64, 36));
        idesc.setPixelFormat(PixelFormat::RGB8_sRGB);

        RtpVideoDepacketizerThread depkt(h.makeCtx(&payload, &idesc),
                                         String("RtpVidDepkt"), kVideoClockHz);
        RtpPacket::List pkts = buildRawVideoPackets(64, 36, 24, 1000u, 0u);
        for (size_t i = 0; i < pkts.size(); i++) {
                depkt.handlePacketForTest(pkts[i]);
        }
        CHECK(h.packetsReceived.value() == static_cast<int64_t>(pkts.size()));
        CHECK(h.bytesReceived.value() > 0);
        CHECK(h.lastPacketArrivalNs.value() != 0);
        CHECK(h.framesReassembled.value() == 1);
        CHECK(h.noteFrameCalls == 1);
        CHECK(h.framesDroppedValidate.value() == 0);
        CHECK(h.framesWaitingParamSets.value() == 0);
}

TEST_CASE("RtpVideoDepacketizerThread: inactive stream drops packets without emitting") {
        Harness            h;
        h.active = false;
        RtpPayloadRawVideo payload(64, 36, 24);
        ImageDesc          idesc;
        idesc.setSize(Size2Du32(64, 36));
        idesc.setPixelFormat(PixelFormat::RGB8_sRGB);

        RtpVideoDepacketizerThread depkt(h.makeCtx(&payload, &idesc),
                                         String("RtpVidDepkt"), kVideoClockHz);
        RtpPacket::List pkts = buildRawVideoPackets(64, 36, 24, 1000u, 0u);
        for (size_t i = 0; i < pkts.size(); i++) {
                depkt.handlePacketForTest(pkts[i]);
        }
        CHECK(h.payloadQ.size() == 0u);
        CHECK(h.packetsReceived.value() == 0);
}
