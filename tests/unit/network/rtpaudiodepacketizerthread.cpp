/**
 * @file      rtpaudiodepacketizerthread.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>

#include <cstring>

#include <promeki/atomic.h>
#include <promeki/audiodesc.h>
#include <promeki/audioformat.h>
#include <promeki/buffer.h>
#include <promeki/clockdomain.h>
#include <promeki/duration.h>
#include <promeki/queue.h>
#include <promeki/rtpaudiodepacketizerthread.h>
#include <promeki/rtppacket.h>
#include <promeki/rtpstreamclock.h>
#include <promeki/rxpayloadbundle.h>
#include <promeki/timestamp.h>

using namespace promeki;

namespace {

// Stuffs a synthetic RTP audio packet: 12-byte header + N stereo
// L16 samples (4 bytes per sample frame).  Each byte of the
// payload is set to a sentinel so a test can spot-check that the
// chunk's pcmBytes match.
RtpPacket makeAudioPacket(uint16_t seq, uint32_t rtpTs,
                          unsigned int channels, size_t samples,
                          uint8_t payloadByte = 0x00,
                          const TimeStamp &arrivalSteady = TimeStamp::now()) {
        constexpr size_t headerBytes = 12;
        const size_t     payloadBytes = samples * channels * 2;
        RtpPacket        pkt(headerBytes + payloadBytes);
        pkt.setPayloadType(96);
        pkt.setSequenceNumber(seq);
        pkt.setTimestamp(rtpTs);
        pkt.setSsrc(0xCAFEBABEu);
        pkt.setMarker(false);
        if (payloadBytes > 0) {
                std::memset(pkt.payload(), payloadByte, payloadBytes);
        }
        pkt.arrivalSteady = arrivalSteady;
        return pkt;
}

// Builds a context with payloadQueue / counters / state fields.
// Caller binds the AudioDesc / hasSr / streamClock pointers.
struct Harness {
        Queue<RxAudioChunk> payloadQ;
        Atomic<uint32_t>    resetEpoch;
        bool                active = true;
        AudioDesc           audioDesc;
        bool                hasSr = false;
        RtpStreamClock      streamClock;
        Atomic<int64_t>     packetsReceived;
        Atomic<int64_t>     bytesReceived;
        Atomic<int64_t>     lastPacketArrivalNs;
        Atomic<int64_t>     framesReassembled;
        int                 noteFrameCalls = 0;
        int                 refreshSrCalls = 0;
        int                 ntpToSteadyCalls = 0;

        RtpAudioDepacketizerContext makeCtx() {
                RtpAudioDepacketizerContext ctx;
                ctx.payloadQueue = &payloadQ;
                ctx.resetEpoch = &resetEpoch;
                ctx.active = &active;
                ctx.readerAudioDesc = &audioDesc;
                ctx.hasSr = &hasSr;
                ctx.streamClock = &streamClock;
                ctx.packetsReceived = &packetsReceived;
                ctx.bytesReceived = &bytesReceived;
                ctx.lastPacketArrivalNs = &lastPacketArrivalNs;
                ctx.framesReassembled = &framesReassembled;
                ctx.noteFrameReceived = [this]() { ++noteFrameCalls; };
                ctx.refreshStreamClock = [this]() { ++refreshSrCalls; };
                ctx.ntpToSteady = [this](const NtpTime &) {
                        ++ntpToSteadyCalls;
                        return TimeStamp();
                };
                return ctx;
        }
};

constexpr uint32_t kAudioClockHz = 48000u;

} // namespace

TEST_CASE("RtpAudioDepacketizerThread: first packet seeds the StreamAnchor") {
        Harness h;
        h.audioDesc = AudioDesc(AudioFormat::PCMI_S16BE, 48000.0f, 2);

        RtpAudioDepacketizerThread depkt(h.makeCtx(), String("RtpAudDepkt"),
                                         kAudioClockHz);
        const TimeStamp arrival = TimeStamp::now();
        depkt.handlePacketForTest(makeAudioPacket(100u, 12345u, 2, 96, 0xAB, arrival));

        const StreamAnchor &anchor = depkt.anchor();
        CHECK(anchor.isValid());
        CHECK(anchor.rtpTs0 == 12345u);
        CHECK(anchor.clockRate == kAudioClockHz);
        CHECK(anchor.arrivalT0 == arrival);
}

TEST_CASE("RtpAudioDepacketizerThread: per-packet sample count comes from payload size / frame size") {
        Harness h;
        h.audioDesc = AudioDesc(AudioFormat::PCMI_S16BE, 48000.0f, 2);

        RtpAudioDepacketizerThread depkt(h.makeCtx(), String("RtpAudDepkt"),
                                         kAudioClockHz);
        depkt.handlePacketForTest(makeAudioPacket(0u, 0u, 2, 192));

        REQUIRE(h.payloadQ.size() == 1u);
        Result<RxAudioChunk> r = h.payloadQ.tryPop();
        REQUIRE(r.second().isOk());
        const RxAudioChunk &c = r.first();
        CHECK(c.sampleCount == 192u);
        CHECK(c.rtpTimestamp == 0u);
        CHECK(c.wireDesc.channels() == 2u);
        CHECK(c.wireDesc.sampleRate() == doctest::Approx(48000.0f));
}

TEST_CASE("RtpAudioDepacketizerThread: captureTime interpolated via anchor when no SR yet") {
        Harness h;
        h.audioDesc = AudioDesc(AudioFormat::PCMI_S16BE, 48000.0f, 2);
        // hasSr stays false — depacketizer must use anchor.

        RtpAudioDepacketizerThread depkt(h.makeCtx(), String("RtpAudDepkt"),
                                         kAudioClockHz);
        const TimeStamp t0 = TimeStamp::now();
        // Packet 0 at rtpTs 1000 — this seeds the anchor.
        depkt.handlePacketForTest(makeAudioPacket(0u, 1000u, 2, 48, 0xAA, t0));
        // Packet 1 at rtpTs 1048 (one packet's worth = 1 ms at 48 kHz).
        depkt.handlePacketForTest(makeAudioPacket(1u, 1048u, 2, 48, 0xAA,
                                                  t0 + Duration::fromMilliseconds(1)));

        REQUIRE(h.payloadQ.size() == 2u);
        Result<RxAudioChunk> r0 = h.payloadQ.tryPop();
        Result<RxAudioChunk> r1 = h.payloadQ.tryPop();
        REQUIRE(r0.second().isOk());
        REQUIRE(r1.second().isOk());
        // First chunk's captureTime equals the anchor (rtpTs == rtpTs0).
        CHECK(r0.first().captureTime == t0);
        // Second chunk is one ms later in audio-clock terms.
        const Duration delta = r1.first().captureTime - r0.first().captureTime;
        CHECK(delta == Duration::fromMilliseconds(1));
        // ntpToSteady wasn't consulted (no SR).
        CHECK(h.ntpToSteadyCalls == 0);
}

TEST_CASE("RtpAudioDepacketizerThread: captureTime uses streamClock when hasSr is true") {
        Harness h;
        h.audioDesc = AudioDesc(AudioFormat::PCMI_S16BE, 48000.0f, 2);
        h.hasSr = true;
        // Anchor the streamClock at NTP=(100,0), rtpTs=10000, 48 kHz.
        h.streamClock = RtpStreamClock(NtpTime(100u, 0u), 10000u, kAudioClockHz);

        RtpAudioDepacketizerThread depkt(h.makeCtx(), String("RtpAudDepkt"),
                                         kAudioClockHz);
        depkt.handlePacketForTest(makeAudioPacket(0u, 10000u, 2, 48));

        REQUIRE(h.payloadQ.size() == 1u);
        Result<RxAudioChunk> r = h.payloadQ.tryPop();
        REQUIRE(r.second().isOk());
        // wallclockNtp must match the streamClock's mapping for
        // the packet's rtpTs.
        CHECK(r.first().wallclockNtp == NtpTime(100u, 0u));
        // ntpToSteady was consulted (returns default → fallback to anchor).
        CHECK(h.ntpToSteadyCalls == 1);
}

TEST_CASE("RtpAudioDepacketizerThread: SSRC reset epoch invalidates the anchor") {
        Harness h;
        h.audioDesc = AudioDesc(AudioFormat::PCMI_S16BE, 48000.0f, 2);

        RtpAudioDepacketizerThread depkt(h.makeCtx(), String("RtpAudDepkt"),
                                         kAudioClockHz);
        const TimeStamp t0 = TimeStamp::now();
        depkt.handlePacketForTest(makeAudioPacket(0u, 1000u, 2, 48, 0xAA, t0));
        REQUIRE(depkt.anchor().isValid());
        const uint32_t firstAnchorRtp = depkt.anchor().rtpTs0;

        // Bump the reset epoch — depacketizer must drop the
        // anchor and re-seed on the next packet.
        h.resetEpoch.setValue(1u);
        depkt.handlePacketForTest(makeAudioPacket(1u, 5000u, 2, 48, 0xAA,
                                                  t0 + Duration::fromMilliseconds(10)));
        // Anchor must have been re-seeded to rtpTs 5000, not the
        // original 1000.
        CHECK(depkt.anchor().rtpTs0 == 5000u);
        CHECK(depkt.anchor().rtpTs0 != firstAnchorRtp);
}

TEST_CASE("RtpAudioDepacketizerThread: stat counters advance with each accepted packet") {
        Harness h;
        h.audioDesc = AudioDesc(AudioFormat::PCMI_S16BE, 48000.0f, 2);

        RtpAudioDepacketizerThread depkt(h.makeCtx(), String("RtpAudDepkt"),
                                         kAudioClockHz);
        depkt.handlePacketForTest(makeAudioPacket(0u, 0u, 2, 48));
        depkt.handlePacketForTest(makeAudioPacket(1u, 48u, 2, 48));
        depkt.handlePacketForTest(makeAudioPacket(2u, 96u, 2, 48));

        CHECK(h.packetsReceived.value() == 3);
        CHECK(h.bytesReceived.value() > 0);
        CHECK(h.lastPacketArrivalNs.value() != 0);
        CHECK(h.framesReassembled.value() == 3);
        CHECK(h.noteFrameCalls == 3);
        CHECK(h.refreshSrCalls == 3);
}

TEST_CASE("RtpAudioDepacketizerThread: inactive stream drops packets without bumping counters") {
        Harness h;
        h.audioDesc = AudioDesc(AudioFormat::PCMI_S16BE, 48000.0f, 2);
        h.active = false;

        RtpAudioDepacketizerThread depkt(h.makeCtx(), String("RtpAudDepkt"),
                                         kAudioClockHz);
        depkt.handlePacketForTest(makeAudioPacket(0u, 0u, 2, 48));

        CHECK(h.payloadQ.size() == 0u);
        CHECK(h.packetsReceived.value() == 0);
        CHECK(h.framesReassembled.value() == 0);
}

TEST_CASE("RtpAudioDepacketizerThread: empty audio desc drops packets after counter bumps") {
        Harness h;
        // Leave audioDesc invalid (no format / channels).

        RtpAudioDepacketizerThread depkt(h.makeCtx(), String("RtpAudDepkt"),
                                         kAudioClockHz);
        depkt.handlePacketForTest(makeAudioPacket(0u, 0u, 2, 48));

        // Counters bumped (the packet was received over the wire,
        // a valid wire-side observation).
        CHECK(h.packetsReceived.value() == 1);
        // But no chunk emitted — depacketizer can't decode without
        // a valid desc.
        CHECK(h.payloadQ.size() == 0u);
        CHECK(h.framesReassembled.value() == 0);
}
