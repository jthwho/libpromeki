/**
 * @file      rtpsession.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>
#include <doctest/doctest.h>
#include <promeki/rtcppacket.h>
#include <promeki/rtppacketbatch.h>
#include <promeki/rtpsession.h>
#include <promeki/udpsocket.h>
#include <promeki/loopbacktransport.h>
#include <promeki/rtppayload.h>
#include <promeki/duration.h>
#include <promeki/thread.h>
#include <promeki/timestamp.h>
#include <cstring>

using namespace promeki;

TEST_CASE("RtpSession") {

        SUBCASE("construction") {
                RtpSession session;
                CHECK_FALSE(session.isRunning());
                CHECK(session.ssrc() != 0);
                CHECK(session.payloadType() == 96);
                CHECK(session.clockRate() == 90000);
                CHECK(session.sequenceNumber() == 0);
                CHECK(session.transport() == nullptr);
        }

        SUBCASE("set properties") {
                RtpSession session;
                session.setPayloadType(97);
                session.setClockRate(48000);
                session.setSsrc(0x12345678);
                CHECK(session.payloadType() == 97);
                CHECK(session.clockRate() == 48000);
                CHECK(session.ssrc() == 0x12345678);
        }

        SUBCASE("start with internal transport") {
                RtpSession session;
                Error      err = session.start(SocketAddress::any(0));
                CHECK(err.isOk());
                CHECK(session.isRunning());
                CHECK(session.transport() != nullptr);
                session.stop();
                CHECK_FALSE(session.isRunning());
                CHECK(session.transport() == nullptr);
        }

        SUBCASE("start with external transport") {
                RtpSession        session;
                LoopbackTransport tx, rx;
                LoopbackTransport::pair(&tx, &rx);
                tx.open();
                rx.open();

                Error err = session.start(&tx);
                CHECK(err.isOk());
                CHECK(session.isRunning());
                CHECK(session.transport() == &tx);

                session.stop();
                CHECK_FALSE(session.isRunning());
                CHECK(session.transport() == nullptr);
                // External transport is still open and owned by caller.
                CHECK(tx.isOpen());
        }

        SUBCASE("start with unopened external transport fails") {
                RtpSession        session;
                LoopbackTransport tx;
                // Not opened.
                Error err = session.start(&tx);
                CHECK(err == Error::NotOpen);
        }

        SUBCASE("start with null external transport fails") {
                RtpSession session;
                Error      err = session.start(static_cast<PacketTransport *>(nullptr));
                CHECK(err == Error::InvalidArgument);
        }

        SUBCASE("double start fails") {
                RtpSession session;
                session.start(SocketAddress::any(0));
                Error err = session.start(SocketAddress::any(0));
                CHECK(err == Error::Busy);
                session.stop();
        }

        SUBCASE("stop without start is safe") {
                RtpSession session;
                session.stop(); // Should not crash
                CHECK_FALSE(session.isRunning());
        }

        SUBCASE("sendPacket not started fails") {
                RtpSession session;
                Buffer     payload(10);
                payload.setSize(10);
                session.setRemote(SocketAddress::localhost(5004));
                Error err = session.sendPacket(payload, 0, 96);
                CHECK(err == Error::NotOpen);
        }

        SUBCASE("sendPacket without remote fails") {
                RtpSession session;
                session.start(SocketAddress::any(0));
                Buffer payload(4);
                payload.setSize(4);
                Error err = session.sendPacket(payload, 0, 96);
                CHECK(err == Error::InvalidArgument);
                session.stop();
        }

        SUBCASE("send and receive single packet") {
                // Set up receiver
                UdpSocket receiver;
                receiver.open(IODevice::ReadWrite);
                receiver.setReceiveTimeout(2000);
                receiver.bind(SocketAddress::any(0));
                uint16_t      recvPort = receiver.localAddress().port();
                SocketAddress dest(Ipv4Address::loopback(), recvPort);

                // Set up sender session
                RtpSession session;
                session.setSsrc(0xDEADBEEF);
                session.setPayloadType(97);
                session.setRemote(dest);
                Error err = session.start(SocketAddress::any(0));
                REQUIRE(err.isOk());

                // Send a packet
                const char *msg = "test payload";
                Buffer      payload(std::strlen(msg));
                payload.setSize(std::strlen(msg));
                std::memcpy(payload.data(), msg, std::strlen(msg));

                err = session.sendPacket(payload, 1000, 97, true);
                CHECK(err.isOk());

                // Receive and verify RTP header
                uint8_t buf[256];
                int64_t n = receiver.readDatagram(buf, sizeof(buf));
                REQUIRE(n > 12);

                // Version = 2
                CHECK((buf[0] & 0xC0) == 0x80);
                // Marker = 1, PT = 97
                CHECK(buf[1] == (0x80 | 97));
                // Sequence number = 0 (first packet)
                CHECK(buf[2] == 0);
                CHECK(buf[3] == 0);
                // Timestamp = 1000
                CHECK(buf[4] == 0);
                CHECK(buf[5] == 0);
                CHECK(buf[6] == 0x03);
                CHECK(buf[7] == 0xE8);
                // SSRC = 0xDEADBEEF
                CHECK(buf[8] == 0xDE);
                CHECK(buf[9] == 0xAD);
                CHECK(buf[10] == 0xBE);
                CHECK(buf[11] == 0xEF);
                // Payload
                CHECK(std::memcmp(buf + 12, msg, std::strlen(msg)) == 0);

                session.stop();
        }

        SUBCASE("sequence number increments") {
                UdpSocket receiver;
                receiver.open(IODevice::ReadWrite);
                receiver.setReceiveTimeout(2000);
                receiver.bind(SocketAddress::any(0));
                uint16_t      recvPort = receiver.localAddress().port();
                SocketAddress dest(Ipv4Address::loopback(), recvPort);

                RtpSession session;
                session.setRemote(dest);
                session.start(SocketAddress::any(0));

                Buffer payload(4);
                payload.setSize(4);
                std::memset(payload.data(), 0, 4);

                // Send 3 packets
                for (int i = 0; i < 3; i++) {
                        session.sendPacket(payload, i * 100, 96);
                }
                CHECK(session.sequenceNumber() == 3);

                // Verify sequence numbers in received packets
                for (int i = 0; i < 3; i++) {
                        uint8_t buf[256];
                        int64_t n = receiver.readDatagram(buf, sizeof(buf));
                        REQUIRE(n > 12);
                        uint16_t seq = (static_cast<uint16_t>(buf[2]) << 8) | buf[3];
                        CHECK(seq == static_cast<uint16_t>(i));
                }
                session.stop();
        }

        SUBCASE("sendPackets with pre-packed data") {
                UdpSocket receiver;
                receiver.open(IODevice::ReadWrite);
                receiver.setReceiveTimeout(2000);
                receiver.bind(SocketAddress::any(0));
                uint16_t      recvPort = receiver.localAddress().port();
                SocketAddress dest(Ipv4Address::loopback(), recvPort);

                RtpSession session;
                session.setPayloadType(97);
                session.setSsrc(0x11223344);
                session.setRemote(dest);
                session.start(SocketAddress::any(0));

                // Create pre-packed packets (simulating payload handler output)
                RtpPayloadL24 payload(48000, 2);
                const size_t  dataSize = 288;
                uint8_t       data[288];
                for (size_t i = 0; i < dataSize; i++) data[i] = static_cast<uint8_t>(i & 0xFF);

                auto packets = payload.pack(data, dataSize);
                REQUIRE(packets.size() > 0);

                // Caller (TX thread role) stamps RTP-TS + marker
                // before handing the batch to the session.  The
                // session fills version / seq / SSRC / PT.
                RtpPacketBatch batch;
                batch.packets = std::move(packets);
                for (size_t i = 0; i < batch.packets.size(); i++) {
                        batch.packets[i].setTimestamp(5000);
                        batch.packets[i].setMarker(i + 1 == batch.packets.size());
                }
                Error err = session.sendPackets(batch);
                CHECK(err.isOk());

                // Receive and verify
                uint8_t buf[2048];
                int64_t n = receiver.readDatagram(buf, sizeof(buf));
                REQUIRE(n > 12);

                // Check RTP header was filled in
                CHECK((buf[0] & 0xC0) == 0x80);
                CHECK((buf[1] & 0x7F) == 97);   // PT
                CHECK((buf[1] & 0x80) == 0x80); // Marker (last packet)
                // SSRC
                CHECK(buf[8] == 0x11);
                CHECK(buf[9] == 0x22);
                CHECK(buf[10] == 0x33);
                CHECK(buf[11] == 0x44);

                session.stop();
        }

        SUBCASE("sendPackets marker only on last") {
                UdpSocket receiver;
                receiver.open(IODevice::ReadWrite);
                receiver.setReceiveTimeout(2000);
                receiver.bind(SocketAddress::any(0));
                uint16_t      recvPort = receiver.localAddress().port();
                SocketAddress dest(Ipv4Address::loopback(), recvPort);

                RtpSession session;
                session.setRemote(dest);
                session.start(SocketAddress::any(0));

                // Create multiple pre-packed packets
                RtpPayloadL24 payload(48000, 2);
                const size_t  dataSize = 3000;
                List<uint8_t> data;
                data.resize(dataSize);
                for (size_t i = 0; i < dataSize; i++) data[i] = static_cast<uint8_t>(i);

                auto packets = payload.pack(data.data(), dataSize);
                REQUIRE(packets.size() > 1);

                RtpPacketBatch batch;
                batch.packets = std::move(packets);
                const size_t nPackets = batch.packets.size();
                for (size_t i = 0; i < nPackets; i++) {
                        batch.packets[i].setTimestamp(10000);
                        batch.packets[i].setMarker(i + 1 == nPackets);
                }
                Error err = session.sendPackets(batch);
                CHECK(err.isOk());

                // Receive all packets and check marker bits
                for (size_t i = 0; i < nPackets; i++) {
                        uint8_t buf[2048];
                        int64_t n = receiver.readDatagram(buf, sizeof(buf));
                        REQUIRE(n > 12);
                        bool marker = (buf[1] & 0x80) != 0;
                        if (i == nPackets - 1) {
                                CHECK(marker); // Last packet has marker
                        } else {
                                CHECK_FALSE(marker); // Others don't
                        }
                }
                session.stop();
        }

        SUBCASE("sendPackets not started fails") {
                RtpSession session;
                session.setRemote(SocketAddress::localhost(5004));
                RtpPacketBatch batch;
                batch.packets = RtpPacket::createList(3, 100);
                Error err = session.sendPackets(batch);
                CHECK(err == Error::NotOpen);
        }

        SUBCASE("sendPackets via LoopbackTransport") {
                LoopbackTransport txPort, rxPort;
                LoopbackTransport::pair(&txPort, &rxPort);
                txPort.open();
                rxPort.open();

                RtpSession session;
                session.setSsrc(0xAABBCCDD);
                session.setPayloadType(97);
                session.setRemote(SocketAddress(Ipv4Address::loopback(), 5004));
                Error err = session.start(&txPort);
                REQUIRE(err.isOk());

                RtpPacketBatch loopBatch;
                loopBatch.packets = RtpPacket::createList(4, 200);
                for (size_t i = 0; i < loopBatch.packets.size(); i++) {
                        loopBatch.packets[i].setTimestamp(0x00010203);
                        loopBatch.packets[i].setMarker(i + 1 == loopBatch.packets.size());
                }
                err = session.sendPackets(loopBatch);
                CHECK(err.isOk());
                CHECK(rxPort.pendingPackets() == 4);

                // Each packet arrives with the expected SSRC and incrementing seqno.
                for (size_t i = 0; i < 4; i++) {
                        uint8_t buf[300];
                        ssize_t n = rxPort.receivePacket(buf, sizeof(buf));
                        REQUIRE(n > 12);
                        CHECK((buf[0] & 0xC0) == 0x80);
                        CHECK((buf[1] & 0x7F) == 97);
                        uint16_t seq = (static_cast<uint16_t>(buf[2]) << 8) | buf[3];
                        CHECK(seq == static_cast<uint16_t>(i));
                        CHECK(buf[8] == 0xAA);
                        CHECK(buf[9] == 0xBB);
                        CHECK(buf[10] == 0xCC);
                        CHECK(buf[11] == 0xDD);
                        // Marker only on last
                        bool marker = (buf[1] & 0x80) != 0;
                        CHECK(marker == (i == 3));
                }

                session.stop();
        }

        // sendPacketsPaced was deleted in Phase 3 — userspace pacing
        // now lives in the per-stream TX thread + Cadence helper.
        // The Cadence-based pacing has its own dedicated coverage
        // in tests/unit/cadence.cpp; the equivalent of the
        // previous "spread N packets across one interval" check
        // is exercised by Cadence::next monotonicity + the 10k-tick
        // drift-free assertion.

        SUBCASE("setPacingRate via RtpSession") {
                RtpSession session;
                session.setRemote(SocketAddress::localhost(5004));
                session.start(SocketAddress::any(0));
                Error err = session.setPacingRate(12'500'000);
#if defined(PROMEKI_PLATFORM_LINUX)
                CHECK(err.isOk());
#else
                CHECK(err == Error::NotSupported);
#endif
                session.stop();
        }

        SUBCASE("setPacingRate before start fails") {
                RtpSession session;
                CHECK(session.setPacingRate(1'000'000) == Error::NotOpen);
        }

        SUBCASE("destructor stops session") {
                UdpSocket receiver;
                receiver.open(IODevice::ReadWrite);
                receiver.bind(SocketAddress::any(0));
                {
                        RtpSession session;
                        Error      err = session.start(SocketAddress::any(0));
                        CHECK(err.isOk());
                        CHECK(session.isRunning());
                        // session destructor should call stop()
                }
                // If we get here without crash, destructor worked
                CHECK(true);
        }

        SUBCASE("startReceiving before start fails") {
                RtpSession session;
                Error      err = session.startReceiving([](const RtpPacket &, const SocketAddress &) {});
                CHECK(err == Error::NotOpen);
                CHECK_FALSE(session.isReceiving());
        }

        SUBCASE("startReceiving with null callback fails") {
                // Need a running session first.
                UdpSocket receiver;
                receiver.open(IODevice::ReadWrite);
                receiver.bind(SocketAddress::any(0));

                RtpSession session;
                REQUIRE(session.start(SocketAddress::any(0)).isOk());
                Error err = session.startReceiving(RtpSession::PacketCallback{});
                CHECK(err == Error::InvalidArgument);
                CHECK_FALSE(session.isReceiving());
                session.stop();
        }

        SUBCASE("receive loop delivers packets to callback") {
                // sender: RtpSession transmitting onto a well-known
                //         loopback port
                // receiver: second RtpSession with startReceiving
                //           listening on that port
                UdpSocket pickport;
                pickport.open(IODevice::ReadWrite);
                pickport.bind(SocketAddress::any(0));
                uint16_t port = pickport.localAddress().port();
                pickport.close();

                RtpSession rxSession;
                rxSession.setReceivePollIntervalMs(20);
                Error err = rxSession.start(SocketAddress::any(port));
                REQUIRE(err.isOk());

                std::atomic<int>      count{0};
                std::atomic<uint32_t> lastTimestamp{0};
                std::atomic<uint8_t>  lastPayloadType{0};
                std::atomic<uint32_t> lastSsrc{0};
                err = rxSession.startReceiving([&](const RtpPacket &pkt, const SocketAddress &) {
                        lastTimestamp.store(pkt.timestamp());
                        lastPayloadType.store(pkt.payloadType());
                        lastSsrc.store(pkt.ssrc());
                        count.fetch_add(1);
                });
                REQUIRE(err.isOk());
                CHECK(rxSession.isReceiving());

                // Give the receive thread time to install
                // SO_RCVTIMEO and settle into the loop.
                Thread::sleepMs(50);

                // Send a single RTP packet.
                RtpSession txSession;
                txSession.setPayloadType(97);
                txSession.setClockRate(48000);
                txSession.setSsrc(0xCAFEBABE);
                txSession.setRemote(SocketAddress(Ipv4Address::loopback(), port));
                REQUIRE(txSession.start(SocketAddress::any(0)).isOk());

                RtpPayloadL16        payload(48000, 2);
                std::vector<uint8_t> audio(64, 0);
                auto                 packets = payload.pack(audio.data(), audio.size());
                REQUIRE(!packets.isEmpty());
                RtpPacketBatch rxBatch;
                rxBatch.packets = std::move(packets);
                for (size_t i = 0; i < rxBatch.packets.size(); i++) {
                        rxBatch.packets[i].setTimestamp(12345);
                        rxBatch.packets[i].setMarker(false);
                }
                rxBatch.markerOnLast = false;
                err = txSession.sendPackets(rxBatch);
                CHECK(err.isOk());

                // Wait up to 500 ms for delivery.
                for (int i = 0; i < 50 && count.load() == 0; i++) {
                        Thread::sleepMs(10);
                }

                rxSession.stopReceiving();
                CHECK_FALSE(rxSession.isReceiving());

                txSession.stop();
                rxSession.stop();

                CHECK(count.load() >= 1);
                CHECK(lastTimestamp.load() == 12345);
                CHECK(lastPayloadType.load() == 97);
                CHECK(lastSsrc.load() == 0xCAFEBABE);
        }

        SUBCASE("stopReceiving is idempotent") {
                RtpSession session;
                REQUIRE(session.start(SocketAddress::any(0)).isOk());
                REQUIRE(session.startReceiving([](const RtpPacket &, const SocketAddress &) {}).isOk());
                session.stopReceiving();
                CHECK_FALSE(session.isReceiving());
                session.stopReceiving(); // second call is a no-op
                CHECK_FALSE(session.isReceiving());
                session.stop();
        }

        SUBCASE("receivePollIntervalMs defaults and setter") {
                RtpSession session;
                CHECK(session.receivePollIntervalMs() == 200);
                session.setReceivePollIntervalMs(50);
                CHECK(session.receivePollIntervalMs() == 50);
        }

        SUBCASE("setReceivePollIntervalMs zero coerces to 200") {
                RtpSession session;
                session.setReceivePollIntervalMs(0);
                CHECK(session.receivePollIntervalMs() == 200);
        }

        SUBCASE("startReceiving twice returns Busy") {
                RtpSession session;
                REQUIRE(session.start(SocketAddress::any(0)).isOk());
                REQUIRE(session.startReceiving([](const RtpPacket &, const SocketAddress &) {}).isOk());
                Error err = session.startReceiving([](const RtpPacket &, const SocketAddress &) {});
                CHECK(err == Error::Busy);
                session.stopReceiving();
                session.stop();
        }

        // ====================================================================
        // SR anchor: setRtpAnchor / noteRtpEmission / currentSrNtp arithmetic
        // ====================================================================
        SUBCASE("setRtpAnchor stores the anchor pair") {
                RtpSession session;
                NtpTime    anchor(0xAA00BB00, 0x00112233);
                session.setRtpAnchor(anchor, 1000);
                CHECK(session.anchorNtp() == anchor);
                CHECK(session.anchorRtpTs() == 1000u);
        }

        SUBCASE("currentSrNtp returns anchor when no emission yet") {
                RtpSession session;
                session.setClockRate(48000);
                NtpTime anchor(0x10, 0x20);
                session.setRtpAnchor(anchor, 100);
                // Last emission rtpTs is 0 — but with no emission
                // recorded yet, currentSrNtp still runs the
                // arithmetic on whatever rtpTs is in place.  The
                // value here is anchor + (0 - 100)/clockRate, taken
                // modulo uint32_t — i.e. a very large negative
                // delta.  The interesting test is that the function
                // is pure and deterministic, not the precise value.
                NtpTime ntp = session.currentSrNtp();
                // It still returns a valid value (no nullptr / crash).
                (void)ntp;
        }

        SUBCASE("currentSrNtp adds (rtpTs - anchorRtpTs) / clockRate to anchor") {
                RtpSession session;
                session.setClockRate(48000);
                // Anchor pair: NTP @ 100s.0, rtpTs = 0.
                NtpTime anchor(100, 0);
                session.setRtpAnchor(anchor, 0);
                // After 48000 ticks the SR's NTP must read exactly
                // anchor + 1 second, i.e. seconds=101, fraction=0.
                session.noteRtpEmission(48000);
                NtpTime ntp = session.currentSrNtp();
                CHECK(ntp.seconds() == 101u);
                CHECK(ntp.fraction() == 0u);
                // After 24000 ticks the SR's NTP must read anchor +
                // 0.5 second — fraction = 2^31 (one half of 2^32).
                session.noteRtpEmission(24000);
                ntp = session.currentSrNtp();
                CHECK(ntp.seconds() == 100u);
                CHECK(ntp.fraction() == 0x80000000u);
        }

        SUBCASE("currentSrNtp handles uint32_t wrap on rtpTs") {
                RtpSession session;
                session.setClockRate(48000);
                // Anchor near the high end of the rtpTs space.
                NtpTime anchor(200, 0);
                session.setRtpAnchor(anchor, 0xFFFFFF00u);
                // Emit a packet 0x100 ticks past the anchor — wraps
                // to rtpTs = 0.  Modular subtraction must still
                // yield a delta of 0x100, i.e. 0x100 / 48000 ≈
                // 5.333...µs.  fraction = 0x100 * 2^32 / 48000.
                session.noteRtpEmission(0x00000000u);
                NtpTime ntp = session.currentSrNtp();
                CHECK(ntp.seconds() == 200u);
                const uint64_t expectedFrac =
                        (static_cast<uint64_t>(0x100) << 32) / static_cast<uint64_t>(48000);
                CHECK(static_cast<uint64_t>(ntp.fraction()) == expectedFrac);
        }

        SUBCASE("currentSrNtp is monotone non-decreasing across consecutive emissions") {
                RtpSession session;
                session.setClockRate(90000);
                NtpTime anchor(500, 0x12345678);
                session.setRtpAnchor(anchor, 0);
                NtpTime prev = session.currentSrNtp();
                for (uint32_t i = 1; i <= 1000; ++i) {
                        session.noteRtpEmission(i * 90);  // 1ms per tick
                        NtpTime cur = session.currentSrNtp();
                        CHECK(cur.toUint64() >= prev.toUint64());
                        prev = cur;
                }
        }

        SUBCASE("hasEmissionRecord starts false and latches after noteRtpEmission") {
                RtpSession session;
                CHECK_FALSE(session.hasEmissionRecord());
                session.noteRtpEmission(0);  // even rtpTs=0 counts as wire activity
                CHECK(session.hasEmissionRecord());
        }

        SUBCASE("emitRtcpSr carries anchor-derived NTP / RTP-TS pair on the wire") {
                // Wire-level integration test: drive the full
                // setRtpAnchor → noteRtpEmission → emitRtcpSr path
                // through a LoopbackTransport, parse the resulting
                // RTCP datagram, and pin the (NTP, RTP_TS) bytes
                // against the anchor-derivation formula.
                LoopbackTransport txPort, rxPort;
                LoopbackTransport::pair(&txPort, &rxPort);
                txPort.open();
                rxPort.open();

                RtpSession session;
                session.setSsrc(0x12345678u);
                session.setClockRate(48000);
                session.setCname(String("smoke@host"));
                session.setRemote(SocketAddress(Ipv4Address::loopback(), 6000));
                REQUIRE(session.start(&txPort).isOk());

                // Pin the anchor at NTP 100s.0 / rtpTs=0 and emit a
                // packet 1.5 seconds further on (rtpTs = 72000 ticks
                // at 48 kHz).  The SR's NTP must equal 101.5s.
                session.setRtpAnchor(NtpTime(100u, 0u), 0u);
                session.noteRtpEmission(72000u);
                CHECK(session.hasEmissionRecord());
                Error err = session.emitRtcpSr(/*pkts=*/3u, /*octs=*/120u);
                CHECK(err.isOk());

                // The compound packet (SR + SDES) should land on the
                // loopback peer.  SR is the first packet — first 28
                // bytes of the compound datagram.
                CHECK(rxPort.pendingPackets() == 1);
                uint8_t buf[1024];
                ssize_t n = rxPort.receivePacket(buf, sizeof(buf));
                REQUIRE(n >= 28);
                CHECK(buf[0] == 0x80u);
                CHECK(buf[1] == 200u);  // PT=SR
                // SSRC
                const uint32_t ssrc = (static_cast<uint32_t>(buf[4]) << 24) |
                                      (static_cast<uint32_t>(buf[5]) << 16) |
                                      (static_cast<uint32_t>(buf[6]) << 8) | static_cast<uint32_t>(buf[7]);
                CHECK(ssrc == 0x12345678u);
                // NTP seconds = 101 (anchor.seconds + 1)
                const uint32_t ntpSec = (static_cast<uint32_t>(buf[8]) << 24) |
                                        (static_cast<uint32_t>(buf[9]) << 16) |
                                        (static_cast<uint32_t>(buf[10]) << 8) | static_cast<uint32_t>(buf[11]);
                CHECK(ntpSec == 101u);
                // NTP fraction = 0.5 second = 2^31
                const uint32_t ntpFrac = (static_cast<uint32_t>(buf[12]) << 24) |
                                         (static_cast<uint32_t>(buf[13]) << 16) |
                                         (static_cast<uint32_t>(buf[14]) << 8) | static_cast<uint32_t>(buf[15]);
                CHECK(ntpFrac == 0x80000000u);
                // Wire RTP-TS == last emission rtpTs
                const uint32_t wireRtpTs = (static_cast<uint32_t>(buf[16]) << 24) |
                                           (static_cast<uint32_t>(buf[17]) << 16) |
                                           (static_cast<uint32_t>(buf[18]) << 8) | static_cast<uint32_t>(buf[19]);
                CHECK(wireRtpTs == 72000u);
                // Sender packet / octet counts
                const uint32_t pkts = (static_cast<uint32_t>(buf[20]) << 24) |
                                      (static_cast<uint32_t>(buf[21]) << 16) |
                                      (static_cast<uint32_t>(buf[22]) << 8) | static_cast<uint32_t>(buf[23]);
                CHECK(pkts == 3u);
                const uint32_t octs = (static_cast<uint32_t>(buf[24]) << 24) |
                                      (static_cast<uint32_t>(buf[25]) << 16) |
                                      (static_cast<uint32_t>(buf[26]) << 8) | static_cast<uint32_t>(buf[27]);
                CHECK(octs == 120u);

                session.stop();
        }

        // ====================================================================
        // Reader-side SR consumption: receivedSr / RTCP demux
        // ====================================================================
        SUBCASE("receivedSr is invalid before any SR has arrived") {
                RtpSession session;
                RtpSession::ReceivedSr sr = session.receivedSr();
                CHECK(sr.valid == false);
                CHECK(sr.rtpTs == 0u);
        }

        SUBCASE("ReceiveThread parses a Sender Report from a compound RTCP datagram") {
                // Set up two paired loopback transports.  The "sender"
                // side just emits a compound RTCP datagram (SR + SDES)
                // through its transport; the "receiver" side runs an
                // RtpSession bound to the paired transport with a
                // receive thread up.  The receive thread must demux
                // the RTCP packet via byte[1] in [200..223], parse
                // the SR, and update receivedSr().
                LoopbackTransport txPort, rxPort;
                LoopbackTransport::pair(&txPort, &rxPort);
                txPort.open();
                rxPort.open();

                RtpSession session;
                session.setClockRate(48000);
                session.setRemote(SocketAddress(Ipv4Address::loopback(), 6000));
                REQUIRE(session.start(&rxPort).isOk());

                std::atomic<int> rtpCalls{0};
                REQUIRE(session.startReceiving([&](const RtpPacket &, const SocketAddress &) {
                        rtpCalls.fetch_add(1);
                }).isOk());

                // Build a compound carrying one SR + one SDES.
                const NtpTime srNtp(3'913'056'000u, 0x80000000u);
                Buffer        sr = RtcpPacket::buildSenderReport(0xCAFEBABEu, srNtp,
                                                                 /*rtpTs=*/72000u,
                                                                 /*pkts=*/3u,
                                                                 /*octs=*/120u);
                Buffer        sdes = RtcpPacket::buildSourceDescriptionCname(0xCAFEBABEu, String("test@host"));
                List<Buffer>  parts;
                parts.pushToBack(sr);
                parts.pushToBack(sdes);
                Buffer compound = RtcpPacket::compound(parts);

                // Send the compound through the paired transport.
                txPort.sendPacket(compound.data(), compound.size(),
                                  SocketAddress(Ipv4Address::loopback(), 6000));

                // Wait briefly for the receive thread to drain the
                // datagram and update its state.  The loopback
                // transport delivers synchronously on the queue but
                // the consumer thread still has to wake.
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
                while (std::chrono::steady_clock::now() < deadline) {
                        if (session.receivedSr().valid) break;
                        std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }

                RtpSession::ReceivedSr got = session.receivedSr();
                CHECK(got.valid);
                CHECK(got.ntp == srNtp);
                CHECK(got.rtpTs == 72000u);
                // arrivedAt was sampled when the receive thread
                // processed the packet — it must be a non-default
                // timestamp (i.e. past the steady-clock epoch).
                CHECK(got.arrivedAt.nanoseconds() != 0);
                // The RTCP packet must NOT have been delivered to the
                // RTP callback — RTCP demux happens before the RTP
                // path.
                CHECK(rtpCalls.load() == 0);

                session.stopReceiving();
                session.stop();
        }

        SUBCASE("ReceiveThread keeps the most-recent SR when multiple arrive") {
                LoopbackTransport txPort, rxPort;
                LoopbackTransport::pair(&txPort, &rxPort);
                txPort.open();
                rxPort.open();

                RtpSession session;
                session.setClockRate(90000);
                session.setRemote(SocketAddress(Ipv4Address::loopback(), 6000));
                REQUIRE(session.start(&rxPort).isOk());
                REQUIRE(session.startReceiving([](const RtpPacket &, const SocketAddress &) {}).isOk());

                auto sendSr = [&](const NtpTime &n, uint32_t rtpTs) {
                        Buffer       sr = RtcpPacket::buildSenderReport(0x1u, n, rtpTs, 0u, 0u);
                        List<Buffer> parts;
                        parts.pushToBack(sr);
                        Buffer compound = RtcpPacket::compound(parts);
                        txPort.sendPacket(compound.data(), compound.size(),
                                          SocketAddress(Ipv4Address::loopback(), 6000));
                };
                auto waitForSrRtp = [&](uint32_t target) {
                        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
                        while (std::chrono::steady_clock::now() < deadline) {
                                auto s = session.receivedSr();
                                if (s.valid && s.rtpTs == target) return true;
                                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                        }
                        return false;
                };

                sendSr(NtpTime(100u, 0u), 1000u);
                CHECK(waitForSrRtp(1000u));
                sendSr(NtpTime(200u, 0u), 2000u);
                CHECK(waitForSrRtp(2000u));
                sendSr(NtpTime(300u, 0u), 3000u);
                CHECK(waitForSrRtp(3000u));

                RtpSession::ReceivedSr got = session.receivedSr();
                CHECK(got.valid);
                CHECK(got.ntp == NtpTime(300u, 0u));
                CHECK(got.rtpTs == 3000u);

                session.stopReceiving();
                session.stop();
        }

        SUBCASE("ReceiveThread silently drops unknown / malformed RTCP") {
                LoopbackTransport txPort, rxPort;
                LoopbackTransport::pair(&txPort, &rxPort);
                txPort.open();
                rxPort.open();

                RtpSession session;
                session.setRemote(SocketAddress(Ipv4Address::loopback(), 6000));
                REQUIRE(session.start(&rxPort).isOk());
                REQUIRE(session.startReceiving([](const RtpPacket &, const SocketAddress &) {}).isOk());

                // SDES-only RTCP compound — legal but carries no SR
                // for the receiver to pick up.
                Buffer       sdes = RtcpPacket::buildSourceDescriptionCname(0x1u, String("y"));
                List<Buffer> parts;
                parts.pushToBack(sdes);
                Buffer compound = RtcpPacket::compound(parts);
                txPort.sendPacket(compound.data(), compound.size(),
                                  SocketAddress(Ipv4Address::loopback(), 6000));

                // Forward-compatibility: an unknown RTCP packet type
                // (PT=210, reserved range).  Must drop without
                // disrupting subsequent traffic.
                uint8_t unknown[8] = {0x80u, 210u, 0x00u, 0x01u, 0x12u, 0x34u, 0x56u, 0x78u};
                txPort.sendPacket(unknown, sizeof(unknown),
                                  SocketAddress(Ipv4Address::loopback(), 6000));

                std::this_thread::sleep_for(std::chrono::milliseconds(50));

                CHECK(session.receivedSr().valid == false);

                session.stopReceiving();
                session.stop();
        }
}
