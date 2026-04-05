/**
 * @file      rtpsession.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/rtpsession.h>
#include <promeki/udpsocket.h>
#include <promeki/rtppayload.h>
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
                CHECK(session.socket() == nullptr);
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

        SUBCASE("start and stop") {
                RtpSession session;
                Error err = session.start(SocketAddress::any(0));
                CHECK(err.isOk());
                CHECK(session.isRunning());
                CHECK(session.socket() != nullptr);
                session.stop();
                CHECK_FALSE(session.isRunning());
                CHECK(session.socket() == nullptr);
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

        SUBCASE("send packet not started fails") {
                RtpSession session;
                Buffer payload(10);
                payload.setSize(10);
                Error err = session.sendPacket(payload, 0, 96, SocketAddress::localhost(5004));
                CHECK(err == Error::NotOpen);
        }

        SUBCASE("send and receive single packet") {
                // Set up receiver
                UdpSocket receiver;
                receiver.open(IODevice::ReadWrite);
                receiver.setReceiveTimeout(2000);
                receiver.bind(SocketAddress::any(0));
                uint16_t recvPort = receiver.localAddress().port();
                SocketAddress dest(Ipv4Address::loopback(), recvPort);

                // Set up sender session
                RtpSession session;
                session.setSsrc(0xDEADBEEF);
                session.setPayloadType(97);
                Error err = session.start(SocketAddress::any(0));
                REQUIRE(err.isOk());

                // Send a packet
                const char *msg = "test payload";
                Buffer payload(std::strlen(msg));
                payload.setSize(std::strlen(msg));
                std::memcpy(payload.data(), msg, std::strlen(msg));

                err = session.sendPacket(payload, 1000, 97, dest, true);
                CHECK(err.isOk());

                // Receive and verify RTP header
                uint8_t buf[256];
                ssize_t n = receiver.readDatagram(buf, sizeof(buf));
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
                CHECK(buf[8]  == 0xDE);
                CHECK(buf[9]  == 0xAD);
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
                uint16_t recvPort = receiver.localAddress().port();
                SocketAddress dest(Ipv4Address::loopback(), recvPort);

                RtpSession session;
                session.start(SocketAddress::any(0));

                Buffer payload(4);
                payload.setSize(4);
                std::memset(payload.data(), 0, 4);

                // Send 3 packets
                for(int i = 0; i < 3; i++) {
                        session.sendPacket(payload, i * 100, 96, dest);
                }
                CHECK(session.sequenceNumber() == 3);

                // Verify sequence numbers in received packets
                for(int i = 0; i < 3; i++) {
                        uint8_t buf[256];
                        ssize_t n = receiver.readDatagram(buf, sizeof(buf));
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
                uint16_t recvPort = receiver.localAddress().port();
                SocketAddress dest(Ipv4Address::loopback(), recvPort);

                RtpSession session;
                session.setPayloadType(97);
                session.setSsrc(0x11223344);
                session.start(SocketAddress::any(0));

                // Create pre-packed packets (simulating payload handler output)
                RtpPayloadL24 payload(48000, 2);
                const size_t dataSize = 288;
                uint8_t data[288];
                for(size_t i = 0; i < dataSize; i++) data[i] = static_cast<uint8_t>(i & 0xFF);

                auto packets = payload.pack(data, dataSize);
                REQUIRE(packets.size() > 0);

                Error err = session.sendPackets(packets, 5000, dest, true);
                CHECK(err.isOk());

                // Receive and verify
                uint8_t buf[2048];
                ssize_t n = receiver.readDatagram(buf, sizeof(buf));
                REQUIRE(n > 12);

                // Check RTP header was filled in
                CHECK((buf[0] & 0xC0) == 0x80);
                CHECK((buf[1] & 0x7F) == 97); // PT
                CHECK((buf[1] & 0x80) == 0x80); // Marker (last packet)
                // SSRC
                CHECK(buf[8]  == 0x11);
                CHECK(buf[9]  == 0x22);
                CHECK(buf[10] == 0x33);
                CHECK(buf[11] == 0x44);

                session.stop();
        }

        SUBCASE("sendPackets marker only on last") {
                UdpSocket receiver;
                receiver.open(IODevice::ReadWrite);
                receiver.setReceiveTimeout(2000);
                receiver.bind(SocketAddress::any(0));
                uint16_t recvPort = receiver.localAddress().port();
                SocketAddress dest(Ipv4Address::loopback(), recvPort);

                RtpSession session;
                session.start(SocketAddress::any(0));

                // Create multiple pre-packed packets
                RtpPayloadL24 payload(48000, 2);
                const size_t dataSize = 3000;
                std::vector<uint8_t> data(dataSize);
                for(size_t i = 0; i < dataSize; i++) data[i] = static_cast<uint8_t>(i);

                auto packets = payload.pack(data.data(), dataSize);
                REQUIRE(packets.size() > 1);

                Error err = session.sendPackets(packets, 10000, dest, true);
                CHECK(err.isOk());

                // Receive all packets and check marker bits
                for(size_t i = 0; i < packets.size(); i++) {
                        uint8_t buf[2048];
                        ssize_t n = receiver.readDatagram(buf, sizeof(buf));
                        REQUIRE(n > 12);
                        bool marker = (buf[1] & 0x80) != 0;
                        if(i == packets.size() - 1) {
                                CHECK(marker); // Last packet has marker
                        } else {
                                CHECK_FALSE(marker); // Others don't
                        }
                }
                session.stop();
        }

        SUBCASE("sendPackets not started fails") {
                RtpSession session;
                auto pkts = RtpPacket::createList(3, 100);
                Error err = session.sendPackets(pkts, 0, SocketAddress::localhost(5004));
                CHECK(err == Error::NotOpen);
        }

        SUBCASE("destructor stops session") {
                UdpSocket receiver;
                receiver.open(IODevice::ReadWrite);
                receiver.bind(SocketAddress::any(0));
                {
                        RtpSession session;
                        Error err = session.start(SocketAddress::any(0));
                        CHECK(err.isOk());
                        CHECK(session.isRunning());
                        // session destructor should call stop()
                }
                // If we get here without crash, destructor worked
                CHECK(true);
        }
}
