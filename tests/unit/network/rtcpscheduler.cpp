/**
 * @file      rtcpscheduler.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <thread>

#include <promeki/atomic.h>
#include <promeki/error.h>
#include <promeki/ipv4address.h>
#include <promeki/list.h>
#include <promeki/rtcppacket.h>
#include <promeki/rtcpscheduler.h>
#include <promeki/rtpseqtracker.h>
#include <promeki/rtpsession.h>
#include <promeki/socketaddress.h>
#include <promeki/timestamp.h>
#include <promeki/udpsocket.h>

using namespace promeki;

namespace {

// Stands up a real RtpSession bound to an ephemeral loopback port,
// pointing at a separately-bound UdpSocket so the test can sniff
// the RTCP datagrams the scheduler emits.  Exposes the receiving
// socket so tests can read sent packets.
//
// The receiver socket has a short timeout so unattempted reads
// fail fast (used by the "no SR before hasEmissionRecord" check).
struct RtcpHarness {
                UdpSocket  receiver;
                RtpSession session;

                RtcpHarness() {
                        receiver.open(IODevice::ReadWrite);
                        receiver.setReceiveTimeout(200);
                        REQUIRE(receiver.bind(SocketAddress::any(0)).isOk());
                        const SocketAddress dest(Ipv4Address::loopback(),
                                                 receiver.localAddress().port());
                        session.setRemote(dest);
                        // Stable SSRC so test assertions are predictable.
                        session.setSsrc(0xDEADBEEF);
                        REQUIRE(session.start(SocketAddress::any(0)).isOk());
                }

                ~RtcpHarness() {
                        session.stop();
                        receiver.close();
                }

                int64_t recv(uint8_t *buf, size_t cap) {
                        return receiver.readDatagram(buf, cap);
                }

                bool hasNoDatagram() {
                        // 50 ms wait — enough for any in-flight datagram
                        // to land but tight enough to keep the test fast.
                        receiver.setReceiveTimeout(50);
                        uint8_t buf[16];
                        const int64_t n = receiver.readDatagram(buf, sizeof(buf));
                        receiver.setReceiveTimeout(200);
                        return n <= 0;
                }
};

uint8_t rtcpPacketType(const uint8_t *buf) {
        // Compound RTCP header: byte 0 = V|P|RC, byte 1 = PT.
        return buf[1];
}

} // namespace

TEST_CASE("RtcpScheduler: writer SR gated on hasEmissionRecord") {
        RtcpHarness h;

        RtcpSchedulerWriterStream w;
        w.active = true;
        w.mediaType = "video";
        w.session = &h.session;
        Atomic<int64_t> packetsSent(0);
        Atomic<int64_t> senderOctets(0);
        w.packetsSent = &packetsSent;
        w.senderOctets = &senderOctets;

        RtcpSchedulerContext ctx;
        ctx.intervalMs = 5000;
        ctx.writers.pushToBack(w);

        RtcpScheduler scheduler(std::move(ctx));

        // No emission record yet → runOnce should NOT send an SR.
        scheduler.runOnce();
        CHECK(h.hasNoDatagram());

        // Flip emission record true and bump counters.
        h.session.noteRtpEmission(12345);
        packetsSent.setValue(7);
        senderOctets.setValue(900);

        scheduler.runOnce();

        uint8_t buf[1500];
        int64_t n = h.recv(buf, sizeof(buf));
        REQUIRE(n > 0);
        // SR compound starts with PT=200 (SenderReport).
        CHECK(rtcpPacketType(buf) == 200);
        // findSenderReports should extract one SR with our SSRC.
        const auto srs = RtcpPacket::findSenderReports(buf, static_cast<size_t>(n));
        REQUIRE(srs.size() == 1);
        CHECK(srs.front().ssrc == 0xDEADBEEFu);
        // Sender info packet count + octet count round-tripped.
        CHECK(srs.front().senderPacketCount == 7u);
        CHECK(srs.front().senderOctetCount == 900u);
}

TEST_CASE("RtcpScheduler: reader RR gated on seqTracker.receivedPackets > 0") {
        RtcpHarness h;

        RtpSeqTracker tracker;
        tracker.setClockRateHz(90000);

        bool                       wireSilenceLatch = false;
        Atomic<int64_t>            lastArrivalNs(0);
        RtcpSchedulerReaderStream r;
        r.active = true;
        r.mediaType = "video";
        r.session = &h.session;
        r.seqTracker = &tracker;
        r.lastPacketArrivalNs = &lastArrivalNs;
        r.wireSilenceEosSignaled = &wireSilenceLatch;

        RtcpSchedulerContext ctx;
        ctx.intervalMs = 5000;
        ctx.readers.pushToBack(r);

        RtcpScheduler scheduler(std::move(ctx));

        // Empty tracker → no RR.
        scheduler.runOnce();
        CHECK(h.hasNoDatagram());

        // RFC 3550 §A.1 probation requires MinSequential consecutive
        // packets before receivedPackets goes from zero to one.  Two
        // sequential observes clear probation and unlock RR emission.
        tracker.observe(100, 1000, TimeStamp::now());
        tracker.observe(101, 2000, TimeStamp::now());

        scheduler.runOnce();
        uint8_t buf[1500];
        int64_t n = h.recv(buf, sizeof(buf));
        REQUIRE(n > 0);
        CHECK(rtcpPacketType(buf) == 201); // RR
}

TEST_CASE("RtcpScheduler: emitByeForAll sends BYE for every active stream") {
        RtcpHarness writerH;
        RtcpHarness readerH;

        RtcpSchedulerWriterStream w;
        w.active = true;
        w.mediaType = "video";
        w.session = &writerH.session;

        bool                      wireSilenceLatch = false;
        Atomic<int64_t>           lastArrivalNs(0);
        RtcpSchedulerReaderStream r;
        r.active = true;
        r.mediaType = "audio";
        r.session = &readerH.session;
        r.lastPacketArrivalNs = &lastArrivalNs;
        r.wireSilenceEosSignaled = &wireSilenceLatch;

        RtcpSchedulerContext ctx;
        ctx.intervalMs = 5000;
        ctx.writers.pushToBack(w);
        ctx.readers.pushToBack(r);

        RtcpScheduler scheduler(std::move(ctx));
        scheduler.emitByeForAll();

        uint8_t        buf[1500];
        int64_t        n = writerH.recv(buf, sizeof(buf));
        REQUIRE(n > 0);
        const auto byes1 = RtcpPacket::findByeSources(buf, static_cast<size_t>(n));
        REQUIRE(byes1.size() == 1);
        CHECK(byes1.front() == 0xDEADBEEFu);

        n = readerH.recv(buf, sizeof(buf));
        REQUIRE(n > 0);
        const auto byes2 = RtcpPacket::findByeSources(buf, static_cast<size_t>(n));
        REQUIRE(byes2.size() == 1);
}

TEST_CASE("RtcpScheduler: skips inactive / null-session entries safely") {
        RtcpHarness h; // not used as recipient but harness creates a real session
        (void)h;

        // Writer entry with active=false and session=nullptr.
        RtcpSchedulerWriterStream w;
        w.active = false;
        w.mediaType = "data";
        w.session = nullptr;

        // Reader entry with active=true but session=nullptr (not yet
        // wired up — covers the pre-startReceiving phase).
        bool                       wireSilenceLatch = false;
        Atomic<int64_t>            lastArrivalNs(0);
        RtcpSchedulerReaderStream r;
        r.active = true;
        r.mediaType = "video";
        r.session = nullptr;
        r.seqTracker = nullptr;
        r.lastPacketArrivalNs = &lastArrivalNs;
        r.wireSilenceEosSignaled = &wireSilenceLatch;

        RtcpSchedulerContext ctx;
        ctx.intervalMs = 5000;
        ctx.writers.pushToBack(w);
        ctx.readers.pushToBack(r);

        RtcpScheduler scheduler(std::move(ctx));
        // Must not crash, must not throw.
        scheduler.runOnce();
        scheduler.emitByeForAll();
        CHECK_FALSE(wireSilenceLatch);
}

TEST_CASE("RtcpScheduler: wire-silence callback fires on gap exceeding threshold and is idempotent") {
        bool      wireSilenceLatch = false;
        Atomic<int64_t> lastArrivalNs(0);

        // Simulate a packet that arrived 5 seconds ago.
        const int64_t fiveSecondsAgoNs =
                TimeStamp::now().nanoseconds() - 5'000'000'000LL;
        lastArrivalNs.setValue(fiveSecondsAgoNs);

        RtcpSchedulerReaderStream r;
        r.active = true;
        r.mediaType = "video";
        r.session = nullptr; // RR / BYE skipped — only wire-silence path exercised
        r.seqTracker = nullptr;
        r.lastPacketArrivalNs = &lastArrivalNs;
        r.wireSilenceEosSignaled = &wireSilenceLatch;

        std::atomic<int>  callbackCount{0};
        std::atomic<int64_t> lastGapNs{0};
        RtcpSchedulerContext ctx;
        ctx.intervalMs = 100;
        ctx.wireSilenceTimeoutMs = 200; // 5 s gap >> 200 ms threshold
        ctx.readers.pushToBack(r);
        ctx.onWireSilenceEos =
                [&callbackCount, &lastGapNs](RtcpSchedulerReaderStream &, int64_t gapNs) {
                        callbackCount.fetch_add(1);
                        lastGapNs.store(gapNs);
                };

        RtcpScheduler scheduler(std::move(ctx));

        scheduler.runOnce();
        CHECK(wireSilenceLatch);
        CHECK(callbackCount.load() == 1);
        CHECK(lastGapNs.load() >= 5'000'000'000LL - 100'000'000LL);

        // Second pass: latch is set → callback must NOT fire again.
        scheduler.runOnce();
        CHECK(callbackCount.load() == 1);
}

TEST_CASE("RtcpScheduler: wire-silence skipped before any packet has arrived") {
        bool                      wireSilenceLatch = false;
        Atomic<int64_t>           lastArrivalNs(0); // never updated → 0
        RtcpSchedulerReaderStream r;
        r.active = true;
        r.mediaType = "video";
        r.session = nullptr;
        r.seqTracker = nullptr;
        r.lastPacketArrivalNs = &lastArrivalNs;
        r.wireSilenceEosSignaled = &wireSilenceLatch;

        std::atomic<int>     callbackCount{0};
        RtcpSchedulerContext ctx;
        ctx.intervalMs = 100;
        ctx.wireSilenceTimeoutMs = 50;
        ctx.readers.pushToBack(r);
        ctx.onWireSilenceEos =
                [&callbackCount](RtcpSchedulerReaderStream &, int64_t) {
                        callbackCount.fetch_add(1);
                };

        RtcpScheduler scheduler(std::move(ctx));

        scheduler.runOnce();
        CHECK_FALSE(wireSilenceLatch);
        CHECK(callbackCount.load() == 0);
}

TEST_CASE("RtcpScheduler: wire-silence threshold defaults to 10 × interval") {
        bool            wireSilenceLatch = false;
        Atomic<int64_t> lastArrivalNs(0);

        // Set lastArrivalNs to 1.5 seconds ago — under 10 × 200ms = 2s
        // default, so no trigger.  Then to 3 seconds ago — over.
        const int64_t nowNs = TimeStamp::now().nanoseconds();
        lastArrivalNs.setValue(nowNs - 1'500'000'000LL);

        RtcpSchedulerReaderStream r;
        r.active = true;
        r.mediaType = "video";
        r.session = nullptr;
        r.seqTracker = nullptr;
        r.lastPacketArrivalNs = &lastArrivalNs;
        r.wireSilenceEosSignaled = &wireSilenceLatch;

        std::atomic<int>     callbackCount{0};
        RtcpSchedulerContext ctx;
        ctx.intervalMs = 200;
        ctx.wireSilenceTimeoutMs = 0; // derive
        ctx.readers.pushToBack(r);
        ctx.onWireSilenceEos =
                [&callbackCount](RtcpSchedulerReaderStream &, int64_t) {
                        callbackCount.fetch_add(1);
                };

        RtcpScheduler scheduler(std::move(ctx));

        scheduler.runOnce();
        CHECK_FALSE(wireSilenceLatch);
        CHECK(callbackCount.load() == 0);

        // Now backdate further — past the 2 s default threshold.
        lastArrivalNs.setValue(TimeStamp::now().nanoseconds() - 3'000'000'000LL);
        scheduler.runOnce();
        CHECK(wireSilenceLatch);
        CHECK(callbackCount.load() == 1);
}

TEST_CASE("RtcpScheduler: thread runs early-emit phase and exits cleanly on requestStop") {
        // Verifies the early-emit phase actually runs through the
        // worker thread.  Spawn a scheduler with one writer that has
        // already emitted; expect at least one SR datagram within
        // ~kStartupPollMs.
        RtcpHarness h;
        h.session.noteRtpEmission(99);

        RtcpSchedulerWriterStream w;
        w.active = true;
        w.mediaType = "video";
        w.session = &h.session;
        Atomic<int64_t> packetsSent(1);
        Atomic<int64_t> senderOctets(100);
        w.packetsSent = &packetsSent;
        w.senderOctets = &senderOctets;

        RtcpSchedulerContext ctx;
        ctx.intervalMs = 60'000; // long steady-state interval — only the early-emit phase runs
        ctx.writers.pushToBack(w);

        RtcpScheduler scheduler(std::move(ctx));
        scheduler.start();

        // Read a datagram with the receiver's 200ms timeout.
        uint8_t buf[1500];
        int64_t n = h.recv(buf, sizeof(buf));
        REQUIRE(n > 0);
        CHECK(rtcpPacketType(buf) == 200);

        scheduler.requestStop();
        scheduler.wait();
        CHECK(scheduler.isStopRequested());
}
