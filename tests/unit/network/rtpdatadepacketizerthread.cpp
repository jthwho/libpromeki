/**
 * @file      rtpdatadepacketizerthread.cpp
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
#include <promeki/queue.h>
#include <promeki/rtpdatadepacketizerthread.h>
#include <promeki/rtppacket.h>
#include <promeki/rtppayload.h>
#include <promeki/timestamp.h>

using namespace promeki;

namespace {

constexpr uint32_t kDataClockHz = 90000u;

// Builds an RtpPayloadJson-formatted set of packets carrying
// @p body as the metadata JSON.  Returns the list of packets;
// caller drives them through the depacketizer.
RtpPacket::List buildJsonPackets(const String &body, uint32_t rtpTs,
                                 uint16_t baseSeq,
                                 const TimeStamp &arrivalSteady = TimeStamp::now()) {
        RtpPayloadJson  payload;
        RtpPacket::List pkts =
                payload.pack(body.cstr(), body.length());
        for (size_t i = 0; i < pkts.size(); i++) {
                pkts[i].setPayloadType(98);
                pkts[i].setSequenceNumber(static_cast<uint16_t>(baseSeq + i));
                pkts[i].setTimestamp(rtpTs);
                pkts[i].setSsrc(0xCAFEBABEu);
                pkts[i].arrivalSteady = arrivalSteady;
        }
        // Pack already sets the marker on the last packet; double-
        // check defensively.
        if (!pkts.isEmpty()) {
                pkts[pkts.size() - 1].setMarker(true);
        }
        return pkts;
}

struct Harness {
        Queue<RxDataMessage> payloadQ;
        Atomic<uint32_t>     resetEpoch;
        bool                 active = true;
        RtpPayloadJson       payload;
        bool                 hasSr = false;
        Atomic<int64_t>      packetsReceived;
        Atomic<int64_t>      bytesReceived;
        Atomic<int64_t>      lastPacketArrivalNs;
        Atomic<int64_t>      framesReassembled;
        Atomic<int64_t>      framesDroppedSsrcReset;
        int                  noteFrameCalls = 0;

        RtpDataDepacketizerContext makeCtx() {
                RtpDataDepacketizerContext ctx;
                ctx.payloadQueue = &payloadQ;
                ctx.resetEpoch = &resetEpoch;
                ctx.active = &active;
                ctx.payload = &payload;
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

TEST_CASE("RtpDataDepacketizerThread: marker-bit triggers JSON parse + emit") {
        Harness h;
        RtpDataDepacketizerThread depkt(h.makeCtx(), String("RtpDatDepkt"),
                                        kDataClockHz);

        const String body("{\"Description\":\"hello\"}");
        RtpPacket::List pkts = buildJsonPackets(body, 1000u, 0u);
        REQUIRE(!pkts.isEmpty());
        for (size_t i = 0; i < pkts.size(); i++) {
                depkt.handlePacketForTest(pkts[i]);
        }

        REQUIRE(h.payloadQ.size() == 1u);
        Result<RxDataMessage> r = h.payloadQ.tryPop();
        REQUIRE(r.second().isOk());
        const RxDataMessage &m = r.first();
        CHECK(m.rtpTimestamp == 1000u);
        CHECK(m.packetCount == static_cast<int32_t>(pkts.size()));
        CHECK(m.metadata.contains(Metadata::Description));
        CHECK(m.metadata.get(Metadata::Description).get<String>() == "hello");
}

TEST_CASE("RtpDataDepacketizerThread: timestamp-change flush emits a stuck reassembly") {
        Harness h;
        RtpDataDepacketizerThread depkt(h.makeCtx(), String("RtpDatDepkt"),
                                        kDataClockHz);

        // First message — packet without a marker.
        const String body1("{\"Description\":\"first\"}");
        RtpPacket::List p1 = buildJsonPackets(body1, 1000u, 0u);
        REQUIRE(!p1.isEmpty());
        // Strip the marker — simulate a sender that lost the trailing
        // packet.  The depacketizer should hold the in-flight packets
        // until a different RTP timestamp arrives.
        for (size_t i = 0; i < p1.size(); i++) {
                p1[i].setMarker(false);
        }
        for (size_t i = 0; i < p1.size(); i++) {
                depkt.handlePacketForTest(p1[i]);
        }
        // No emit yet (no marker, no timestamp change).
        CHECK(h.payloadQ.size() == 0u);

        // Second message — different RTP timestamp.  The first
        // packet of the second message triggers a flush of the
        // stuck reassembly (which then emits the first message),
        // *then* the second message's packets accumulate.
        const String body2("{\"Description\":\"second\"}");
        RtpPacket::List p2 = buildJsonPackets(body2, 5000u, 100u);
        REQUIRE(!p2.isEmpty());
        for (size_t i = 0; i < p2.size(); i++) {
                depkt.handlePacketForTest(p2[i]);
        }

        // First message emitted via the timestamp-change flush;
        // second message emitted via its trailing marker.
        REQUIRE(h.payloadQ.size() == 2u);
        Result<RxDataMessage> r1 = h.payloadQ.tryPop();
        Result<RxDataMessage> r2 = h.payloadQ.tryPop();
        REQUIRE(r1.second().isOk());
        REQUIRE(r2.second().isOk());
        CHECK(r1.first().metadata.get(Metadata::Description).get<String>() == "first");
        CHECK(r2.first().metadata.get(Metadata::Description).get<String>() == "second");
}

TEST_CASE("RtpDataDepacketizerThread: SSRC reset epoch drops in-flight reassembly") {
        Harness h;
        RtpDataDepacketizerThread depkt(h.makeCtx(), String("RtpDatDepkt"),
                                        kDataClockHz);

        const String body("{\"Description\":\"never-emitted\"}");
        RtpPacket::List pkts = buildJsonPackets(body, 1000u, 0u);
        REQUIRE(!pkts.isEmpty());
        // Strip the marker so the message stays in flight.
        for (size_t i = 0; i < pkts.size(); i++) {
                pkts[i].setMarker(false);
        }
        for (size_t i = 0; i < pkts.size(); i++) {
                depkt.handlePacketForTest(pkts[i]);
        }
        CHECK(h.payloadQ.size() == 0u);

        // Bump the reset epoch.  Next packet must drop the in-
        // flight reassembly without emitting it.
        h.resetEpoch.setValue(1u);

        const String body2("{\"Description\":\"after-reset\"}");
        RtpPacket::List p2 = buildJsonPackets(body2, 5000u, 100u);
        for (size_t i = 0; i < p2.size(); i++) {
                depkt.handlePacketForTest(p2[i]);
        }

        // Only the second message emits; the in-flight first
        // reassembly was discarded by the reset epoch.
        REQUIRE(h.payloadQ.size() == 1u);
        Result<RxDataMessage> r = h.payloadQ.tryPop();
        REQUIRE(r.second().isOk());
        CHECK(r.first().metadata.get(Metadata::Description).get<String>() == "after-reset");

        // Drop counter must reflect the discarded reassembly.
        CHECK(h.framesDroppedSsrcReset.value() == 1);
}

TEST_CASE("RtpDataDepacketizerThread: stat counters advance on every packet") {
        Harness h;
        RtpDataDepacketizerThread depkt(h.makeCtx(), String("RtpDatDepkt"),
                                        kDataClockHz);

        const String body("{\"Description\":\"x\"}");
        RtpPacket::List pkts = buildJsonPackets(body, 1000u, 0u);
        for (size_t i = 0; i < pkts.size(); i++) {
                depkt.handlePacketForTest(pkts[i]);
        }
        CHECK(h.packetsReceived.value() == static_cast<int64_t>(pkts.size()));
        CHECK(h.bytesReceived.value() > 0);
        CHECK(h.lastPacketArrivalNs.value() != 0);
        CHECK(h.framesReassembled.value() == 1);
        CHECK(h.noteFrameCalls == 1);
}

TEST_CASE("RtpDataDepacketizerThread: malformed JSON is dropped without crashing") {
        Harness h;
        RtpDataDepacketizerThread depkt(h.makeCtx(), String("RtpDatDepkt"),
                                        kDataClockHz);

        // Manually craft a packet whose payload is not valid JSON.
        constexpr size_t headerBytes = 12;
        const char       junk[] = "{this is not valid JSON";
        const size_t     junkLen = sizeof(junk) - 1;
        RtpPacket        pkt(headerBytes + junkLen);
        pkt.setPayloadType(98);
        pkt.setSequenceNumber(0u);
        pkt.setTimestamp(1u);
        pkt.setSsrc(0xCAFEBABEu);
        pkt.setMarker(true);
        std::memcpy(pkt.payload(), junk, junkLen);
        pkt.arrivalSteady = TimeStamp::now();

        depkt.handlePacketForTest(pkt);

        // Counters tick (the packet was received), but no message
        // was emitted (parse failed).
        CHECK(h.packetsReceived.value() == 1);
        CHECK(h.payloadQ.size() == 0u);
        CHECK(h.framesReassembled.value() == 0);
}
