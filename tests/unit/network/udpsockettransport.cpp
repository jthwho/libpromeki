/**
 * @file      udpsockettransport.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/udpsockettransport.h>
#include <promeki/udpsocket.h>
#include <cstring>

using namespace promeki;

TEST_CASE("UdpSocketTransport") {

        SUBCASE("construction defaults") {
                UdpSocketTransport t;
                CHECK_FALSE(t.isOpen());
                CHECK(t.localAddress() == SocketAddress::any(0));
                CHECK(t.dscp() == 0);
                CHECK(t.multicastTTL() == 0);
                CHECK_FALSE(t.isIpv6());
                CHECK_FALSE(t.reuseAddress());
        }

        SUBCASE("open and close") {
                UdpSocketTransport t;
                Error err = t.open();
                CHECK(err.isOk());
                CHECK(t.isOpen());
                CHECK(t.socket() != nullptr);
                t.close();
                CHECK_FALSE(t.isOpen());
                CHECK(t.socket() == nullptr);
        }

        SUBCASE("open twice is Busy") {
                UdpSocketTransport t;
                CHECK(t.open().isOk());
                CHECK(t.open() == Error::Busy);
                t.close();
        }

        SUBCASE("double close is safe") {
                UdpSocketTransport t;
                t.open();
                t.close();
                t.close();
                CHECK_FALSE(t.isOpen());
        }

        SUBCASE("sendPacket loopback") {
                UdpSocketTransport sender;
                UdpSocket receiver;

                sender.open();
                receiver.open(IODevice::ReadWrite);
                receiver.setReceiveTimeout(2000);
                receiver.bind(SocketAddress::any(0));
                uint16_t port = receiver.localAddress().port();

                const char *msg = "transport test";
                ssize_t sent = sender.sendPacket(msg, std::strlen(msg),
                        SocketAddress(Ipv4Address::loopback(), port));
                CHECK(sent == static_cast<ssize_t>(std::strlen(msg)));

                char buf[64];
                int64_t n = receiver.readDatagram(buf, sizeof(buf));
                REQUIRE(n > 0);
                CHECK(std::memcmp(buf, msg, n) == 0);
        }

        SUBCASE("sendPackets batch loopback") {
                UdpSocketTransport sender;
                UdpSocket receiver;

                sender.open();
                receiver.open(IODevice::ReadWrite);
                receiver.setReceiveTimeout(2000);
                receiver.bind(SocketAddress::any(0));
                uint16_t port = receiver.localAddress().port();

                SocketAddress dest(Ipv4Address::loopback(), port);

                char msgs[3][32];
                for(int i = 0; i < 3; i++) {
                        std::snprintf(msgs[i], sizeof(msgs[i]), "ptbatch %d", i);
                }
                PacketTransport::DatagramList batch;
                for(int i = 0; i < 3; i++) {
                        PacketTransport::Datagram d;
                        d.data = msgs[i];
                        d.size = std::strlen(msgs[i]);
                        d.dest = dest;
                        batch.pushToBack(d);
                }
                int sent = sender.sendPackets(batch);
                CHECK(sent == 3);

                for(int i = 0; i < 3; i++) {
                        char buf[64];
                        int64_t n = receiver.readDatagram(buf, sizeof(buf));
                        REQUIRE(n > 0);
                        char expected[32];
                        std::snprintf(expected, sizeof(expected), "ptbatch %d", i);
                        CHECK(n == static_cast<ssize_t>(std::strlen(expected)));
                        CHECK(std::memcmp(buf, expected, n) == 0);
                }
        }

        SUBCASE("receivePacket loopback") {
                UdpSocketTransport a;
                UdpSocketTransport b;

                // Bind b to a known port, send from a to b.
                a.open();
                b.open();
                // We need b's actual bound port from the underlying socket.
                // Rebinding would be cleaner; for now re-open with a
                // specific bind address.
                b.close();
                b.setLocalAddress(SocketAddress::any(0));
                b.open();
                uint16_t port = b.socket()->localAddress().port();

                const char *msg = "rx path";
                ssize_t sent = a.sendPacket(msg, std::strlen(msg),
                        SocketAddress(Ipv4Address::loopback(), port));
                CHECK(sent == static_cast<ssize_t>(std::strlen(msg)));

                b.socket()->setReceiveTimeout(2000);
                char buf[64];
                SocketAddress sender;
                ssize_t n = b.receivePacket(buf, sizeof(buf), &sender);
                REQUIRE(n > 0);
                CHECK(std::memcmp(buf, msg, n) == 0);
                CHECK(sender.isLoopback());
        }

        SUBCASE("setDscp applied at open") {
                UdpSocketTransport t;
                t.setDscp(46); // EF
                Error err = t.open();
                CHECK(err.isOk());
                CHECK(t.isOpen());
                t.close();
        }

        SUBCASE("setMulticastTTL applied at open") {
                UdpSocketTransport t;
                t.setMulticastTTL(8);
                Error err = t.open();
                CHECK(err.isOk());
                t.close();
        }

        SUBCASE("setPacingRate on open socket") {
                UdpSocketTransport t;
                t.open();
                Error err = t.setPacingRate(12'500'000);
#if defined(PROMEKI_PLATFORM_LINUX)
                CHECK(err.isOk());
#else
                CHECK(err == Error::NotSupported);
#endif
                t.close();
        }

        SUBCASE("setPacingRate on closed transport fails") {
                UdpSocketTransport t;
                CHECK(t.setPacingRate(1'000'000) == Error::NotOpen);
        }

        SUBCASE("sendPacket on closed transport fails") {
                UdpSocketTransport t;
                CHECK(t.sendPacket("x", 1, SocketAddress::localhost(5004)) == -1);
        }

        SUBCASE("sendPackets on closed transport fails") {
                UdpSocketTransport t;
                PacketTransport::DatagramList batch;
                PacketTransport::Datagram d;
                d.data = "x"; d.size = 1;
                d.dest = SocketAddress::localhost(5004);
                batch.pushToBack(d);
                CHECK(t.sendPackets(batch) == -1);
        }
}
