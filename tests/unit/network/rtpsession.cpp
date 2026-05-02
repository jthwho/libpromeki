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

                Error err = session.sendPackets(packets, 5000, true);
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

                Error err = session.sendPackets(packets, 10000, true);
                CHECK(err.isOk());

                // Receive all packets and check marker bits
                for (size_t i = 0; i < packets.size(); i++) {
                        uint8_t buf[2048];
                        int64_t n = receiver.readDatagram(buf, sizeof(buf));
                        REQUIRE(n > 12);
                        bool marker = (buf[1] & 0x80) != 0;
                        if (i == packets.size() - 1) {
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
                auto  pkts = RtpPacket::createList(3, 100);
                Error err = session.sendPackets(pkts, 0);
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

                auto pkts = RtpPacket::createList(4, 200);
                err = session.sendPackets(pkts, 0x00010203, true);
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

        SUBCASE("sendPacketsPaced spreads multi-packet frame across interval") {
                // Verify that sendPacketsPaced holds the call open
                // for the requested spread interval when there are
                // many packets, so the writer can use it as a
                // frame-rate enforcer that bounds the per-frame
                // dispatch duration to one frame interval.
                LoopbackTransport txPort, rxPort;
                LoopbackTransport::pair(&txPort, &rxPort);
                txPort.open();
                rxPort.open();

                RtpSession session;
                session.setRemote(SocketAddress(Ipv4Address::loopback(), 5004));
                REQUIRE(session.start(&txPort).isOk());

                auto           pkts = RtpPacket::createList(8, 200);
                const Duration interval = Duration::fromMilliseconds(50);

                TimeStamp start = TimeStamp::now();
                Error     err = session.sendPacketsPaced(pkts, 0, interval, true);
                int64_t   elapsedMs = start.elapsedMilliseconds();

                CHECK(err.isOk());
                CHECK(rxPort.pendingPackets() == 8);
                // Allow a small jitter window — the call should
                // last at least the requested interval and not
                // wildly more (the loopback transport adds
                // microseconds at most).
                CHECK(elapsedMs >= 45);
                CHECK(elapsedMs <= 100);

                session.stop();
        }

        SUBCASE("sendPacketsPaced paces single-packet frames too") {
                // Single-packet frames previously bypassed pacing
                // entirely (the function returned immediately
                // because there was nothing to spread "between"
                // packets), which broke the writer's frame-rate
                // pacing for very small compressed frames.  The
                // call should now last spreadInterval regardless
                // of packet count.
                LoopbackTransport txPort, rxPort;
                LoopbackTransport::pair(&txPort, &rxPort);
                txPort.open();
                rxPort.open();

                RtpSession session;
                session.setRemote(SocketAddress(Ipv4Address::loopback(), 5004));
                REQUIRE(session.start(&txPort).isOk());

                auto           pkts = RtpPacket::createList(1, 200);
                const Duration interval = Duration::fromMilliseconds(40);

                TimeStamp start = TimeStamp::now();
                Error     err = session.sendPacketsPaced(pkts, 0, interval, true);
                int64_t   elapsedMs = start.elapsedMilliseconds();

                CHECK(err.isOk());
                CHECK(rxPort.pendingPackets() == 1);
                CHECK(elapsedMs >= 35);
                CHECK(elapsedMs <= 80);

                session.stop();
        }

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
                err = txSession.sendPackets(packets, 12345, false);
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
}
