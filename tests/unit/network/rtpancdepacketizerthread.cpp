/**
 * @file      rtpancdepacketizerthread.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>

#include <promeki/ancformat.h>
#include <promeki/ancmeta.h>
#include <promeki/ancpacket.h>
#include <promeki/atomic.h>
#include <promeki/clockdomain.h>
#include <promeki/queue.h>
#include <promeki/rtpancdepacketizerthread.h>
#include <promeki/rtppacket.h>
#include <promeki/rtppayloadanc.h>
#include <promeki/st291packet.h>
#include <promeki/timestamp.h>

using namespace promeki;

namespace {

constexpr uint32_t kAncClockHz = 90000u;

// Build a set of RTP packets carrying one ANC frame.  Stamps RTP
// header fields the depacketizer needs (PT / TS / seq / SSRC / marker
// on last / arrival time) so the unit test exercises the same code
// path the recv socket thread would.
RtpPacket::List buildRtpAnc(const AncPacket::List &ancPackets, uint32_t rtpTs,
                            uint16_t baseSeq,
                            const TimeStamp &arrival = TimeStamp::now()) {
        RtpPayloadAnc   payload;
        RtpPacket::List pkts = payload.packAncFrame(ancPackets, rtpTs);
        for (size_t i = 0; i < pkts.size(); ++i) {
                pkts[i].setSequenceNumber(static_cast<uint16_t>(baseSeq + i));
                pkts[i].setSsrc(0xC0FFEEu);
                pkts[i].arrivalSteady = arrival;
        }
        if (!pkts.isEmpty()) {
                pkts[pkts.size() - 1].setMarker(true);
        }
        return pkts;
}

// Builds a small ST 291 ANC packet (CEA-708, line 11) carrying @p
// bodyBytes as its UDW payload.
St291Packet makePacket(AncFormat::ID id, uint16_t line,
                       const List<uint8_t> &bodyBytes) {
        List<uint16_t> udw;
        udw.reserve(bodyBytes.size());
        for (uint8_t b : bodyBytes) udw.pushToBack(static_cast<uint16_t>(b));
        return St291Packet::build(AncFormat(id), udw, line);
}

struct Harness {
        Queue<RxAncFrame>  payloadQ;
        Atomic<uint32_t>   resetEpoch;
        bool               active = true;
        RtpPayloadAnc      payload;
        bool               hasSr = false;
        Atomic<int64_t>    packetsReceived;
        Atomic<int64_t>    bytesReceived;
        Atomic<int64_t>    lastPacketArrivalNs;
        Atomic<int64_t>    framesReassembled;
        Atomic<int64_t>    framesDroppedSsrcReset;
        int                noteFrameCalls = 0;
        AncDesc            desc;

        RtpAncDepacketizerContext makeCtx() {
                RtpAncDepacketizerContext ctx;
                ctx.payloadQueue = &payloadQ;
                ctx.resetEpoch = &resetEpoch;
                ctx.active = &active;
                ctx.payload = &payload;
                ctx.desc = desc;
                ctx.clockDomain = ClockDomain::SystemMonotonic;
                ctx.hasSr = &hasSr;
                ctx.packetsReceived = &packetsReceived;
                ctx.bytesReceived = &bytesReceived;
                ctx.lastPacketArrivalNs = &lastPacketArrivalNs;
                ctx.framesReassembled = &framesReassembled;
                ctx.framesDroppedSsrcReset = &framesDroppedSsrcReset;
                ctx.noteFrameReceived = [this]() { ++noteFrameCalls; };
                return ctx;
        }
};

} // namespace

TEST_CASE("RtpAncDepacketizerThread: marker-bit triggers unpack + emit") {
        Harness                   h;
        RtpAncDepacketizerThread depkt(h.makeCtx(), String("RtpAncDepkt"), kAncClockHz);

        AncPacket::List ancIn;
        ancIn.pushToBack(makePacket(AncFormat::Cea708, 11, {0x10, 0x20, 0x30}).packet());
        ancIn.pushToBack(makePacket(AncFormat::Afd, 12, {0x07}).packet());

        RtpPacket::List rtp = buildRtpAnc(ancIn, 12345u, 0u);
        REQUIRE(!rtp.isEmpty());
        for (size_t i = 0; i < rtp.size(); ++i) {
                depkt.handlePacketForTest(rtp[i]);
        }

        REQUIRE(h.payloadQ.size() == 1u);
        auto popped = h.payloadQ.tryPop();
        REQUIRE(popped.second().isOk());
        const RxAncFrame &f = popped.first();
        CHECK(f.rtpTimestamp == 12345u);
        REQUIRE(f.packets.size() == 2u);
        CHECK(f.packets[0].format().id() == AncFormat::Cea708);
        CHECK(f.packets[1].format().id() == AncFormat::Afd);
        CHECK(h.framesReassembled.value() == 1);
        CHECK(h.noteFrameCalls == 1);
}

TEST_CASE("RtpAncDepacketizerThread: timestamp-change flushes a stuck reassembly") {
        Harness                   h;
        RtpAncDepacketizerThread depkt(h.makeCtx(), String("RtpAncDepkt"), kAncClockHz);

        AncPacket::List ancIn;
        ancIn.pushToBack(makePacket(AncFormat::Cea708, 11, {0xAA}).packet());

        // First frame, but strip the marker — simulate a sender that
        // lost the trailing packet.
        RtpPacket::List p1 = buildRtpAnc(ancIn, 1000u, 0u);
        for (size_t i = 0; i < p1.size(); ++i) p1[i].setMarker(false);
        for (size_t i = 0; i < p1.size(); ++i) depkt.handlePacketForTest(p1[i]);
        CHECK(h.payloadQ.size() == 0u);

        // Second frame with a different RTP timestamp — flushes the
        // stuck reassembly and accepts the new one.
        AncPacket::List ancIn2;
        ancIn2.pushToBack(makePacket(AncFormat::Cea708, 11, {0xBB}).packet());
        RtpPacket::List p2 = buildRtpAnc(ancIn2, 2000u, 10u);
        for (size_t i = 0; i < p2.size(); ++i) depkt.handlePacketForTest(p2[i]);

        // We expect two emitted frames now — the first (stuck) and
        // the second (marker-driven).
        REQUIRE(h.payloadQ.size() == 2u);
        auto a = h.payloadQ.tryPop();
        REQUIRE(a.second().isOk());
        CHECK(a.first().rtpTimestamp == 1000u);
        auto b = h.payloadQ.tryPop();
        REQUIRE(b.second().isOk());
        CHECK(b.first().rtpTimestamp == 2000u);
}

TEST_CASE("RtpAncDepacketizerThread: SSRC reset drops in-flight reassembly") {
        Harness                   h;
        RtpAncDepacketizerThread depkt(h.makeCtx(), String("RtpAncDepkt"), kAncClockHz);

        // Push a non-marker packet so reassembly is in flight.
        AncPacket::List ancIn;
        ancIn.pushToBack(makePacket(AncFormat::Cea708, 11, {0x42}).packet());
        RtpPacket::List p = buildRtpAnc(ancIn, 1000u, 0u);
        for (size_t i = 0; i < p.size(); ++i) p[i].setMarker(false);
        for (size_t i = 0; i < p.size(); ++i) depkt.handlePacketForTest(p[i]);
        CHECK(h.payloadQ.size() == 0u);

        // Bump SSRC epoch.  Next packet on the new SSRC should not
        // resurrect the old reassembly.
        h.resetEpoch.setValue(h.resetEpoch.value() + 1);

        AncPacket::List ancIn2;
        ancIn2.pushToBack(makePacket(AncFormat::Cea708, 11, {0x77}).packet());
        RtpPacket::List p2 = buildRtpAnc(ancIn2, 5000u, 99u);
        for (size_t i = 0; i < p2.size(); ++i) depkt.handlePacketForTest(p2[i]);

        REQUIRE(h.payloadQ.size() == 1u);
        auto popped = h.payloadQ.tryPop();
        REQUIRE(popped.second().isOk());
        CHECK(popped.first().rtpTimestamp == 5000u);
        CHECK(h.framesDroppedSsrcReset.value() == 1);
}

TEST_CASE("RtpAncDepacketizerThread: inactive gate suppresses emit") {
        Harness                   h;
        h.active = false;
        RtpAncDepacketizerThread depkt(h.makeCtx(), String("RtpAncDepkt"), kAncClockHz);

        AncPacket::List ancIn;
        ancIn.pushToBack(makePacket(AncFormat::Cea708, 11, {0x10}).packet());
        RtpPacket::List p = buildRtpAnc(ancIn, 1000u, 0u);
        for (size_t i = 0; i < p.size(); ++i) depkt.handlePacketForTest(p[i]);

        CHECK(h.payloadQ.size() == 0u);
        CHECK(h.packetsReceived.value() == 0);
}
