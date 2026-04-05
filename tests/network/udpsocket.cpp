/**
 * @file      udpsocket.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/udpsocket.h>
#include <promeki/buffer.h>
#include <cstring>
#include <unistd.h>

using namespace promeki;

TEST_CASE("UdpSocket") {

        SUBCASE("construction") {
                UdpSocket sock;
                CHECK(sock.socketType() == AbstractSocket::UdpSocketType);
                CHECK_FALSE(sock.isOpen());
        }

        SUBCASE("open IPv4") {
                UdpSocket sock;
                Error err = sock.open(IODevice::ReadWrite);
                CHECK(err.isOk());
                CHECK(sock.isOpen());
                sock.close();
        }

        SUBCASE("open IPv6") {
                UdpSocket sock;
                Error err = sock.openIpv6(IODevice::ReadWrite);
                CHECK(err.isOk());
                CHECK(sock.isOpen());
                sock.close();
        }

        SUBCASE("bind to any port") {
                UdpSocket sock;
                sock.open(IODevice::ReadWrite);
                Error err = sock.bind(SocketAddress::any(0));
                CHECK(err.isOk());
                CHECK(sock.localAddress().port() != 0);
        }

        SUBCASE("loopback send and receive") {
                UdpSocket sender;
                UdpSocket receiver;

                sender.open(IODevice::ReadWrite);
                receiver.open(IODevice::ReadWrite);
                receiver.setReceiveTimeout(2000);

                // Bind receiver to a known port
                Error err = receiver.bind(SocketAddress::any(0));
                REQUIRE(err.isOk());
                uint16_t port = receiver.localAddress().port();

                // Send data
                const char *msg = "hello UDP";
                SocketAddress dest(Ipv4Address::loopback(), port);
                ssize_t sent = sender.writeDatagram(msg, std::strlen(msg), dest);
                CHECK(sent == static_cast<ssize_t>(std::strlen(msg)));

                // Receive data
                char buf[256];
                SocketAddress from;
                ssize_t received = receiver.readDatagram(buf, sizeof(buf), &from);
                REQUIRE(received > 0);
                CHECK(received == static_cast<ssize_t>(std::strlen(msg)));
                CHECK(std::memcmp(buf, msg, received) == 0);
                CHECK(from.isLoopback());
        }

        SUBCASE("connected mode send and receive") {
                UdpSocket sender;
                UdpSocket receiver;

                sender.open(IODevice::ReadWrite);
                receiver.open(IODevice::ReadWrite);
                receiver.setReceiveTimeout(2000);

                Error err = receiver.bind(SocketAddress::any(0));
                REQUIRE(err.isOk());
                uint16_t port = receiver.localAddress().port();

                // Connect sender to receiver
                err = sender.connectToHost(SocketAddress(Ipv4Address::loopback(), port));
                CHECK(err.isOk());

                // Use IODevice write/read
                const char *msg = "connected UDP";
                int64_t sent = sender.write(msg, std::strlen(msg));
                CHECK(sent == static_cast<int64_t>(std::strlen(msg)));

                char buf[256];
                int64_t received = receiver.read(buf, sizeof(buf));
                REQUIRE(received > 0);
                CHECK(std::memcmp(buf, msg, received) == 0);
        }

        SUBCASE("multiple datagrams") {
                UdpSocket sender;
                UdpSocket receiver;

                sender.open(IODevice::ReadWrite);
                receiver.open(IODevice::ReadWrite);
                receiver.setReceiveTimeout(2000);
                receiver.bind(SocketAddress::any(0));
                uint16_t port = receiver.localAddress().port();
                SocketAddress dest(Ipv4Address::loopback(), port);

                // Send several datagrams
                for(int i = 0; i < 5; i++) {
                        char msg[32];
                        int len = std::snprintf(msg, sizeof(msg), "packet %d", i);
                        sender.writeDatagram(msg, len, dest);
                }

                // Receive all
                for(int i = 0; i < 5; i++) {
                        char buf[256];
                        ssize_t n = receiver.readDatagram(buf, sizeof(buf));
                        REQUIRE(n > 0);
                        char expected[32];
                        int elen = std::snprintf(expected, sizeof(expected), "packet %d", i);
                        CHECK(n == elen);
                        CHECK(std::memcmp(buf, expected, n) == 0);
                }
        }

        SUBCASE("hasPendingDatagrams") {
                UdpSocket sender;
                UdpSocket receiver;

                sender.open(IODevice::ReadWrite);
                receiver.open(IODevice::ReadWrite);
                receiver.bind(SocketAddress::any(0));
                uint16_t port = receiver.localAddress().port();

                CHECK_FALSE(receiver.hasPendingDatagrams());

                sender.writeDatagram("test", 4,
                        SocketAddress(Ipv4Address::loopback(), port));

                // Small delay to let the kernel deliver the packet
                // hasPendingDatagrams should find it
                CHECK(receiver.hasPendingDatagrams());
        }

        SUBCASE("setReuseAddress") {
                UdpSocket sock;
                sock.open(IODevice::ReadWrite);
                Error err = sock.setReuseAddress(true);
                CHECK(err.isOk());
        }

        SUBCASE("multicast loopback") {
                UdpSocket sender;
                UdpSocket receiver;

                sender.open(IODevice::ReadWrite);
                receiver.open(IODevice::ReadWrite);
                receiver.setReuseAddress(true);
                receiver.setReceiveTimeout(2000);

                SocketAddress mcAddr(Ipv4Address(239, 255, 0, 1), 0);

                // Bind receiver to any port, join multicast
                Error err = receiver.bind(SocketAddress::any(0));
                REQUIRE(err.isOk());
                uint16_t port = receiver.localAddress().port();

                SocketAddress mcGroup(Ipv4Address(239, 255, 0, 1), port);
                err = receiver.joinMulticastGroup(mcGroup);
                CHECK(err.isOk());

                // Enable multicast loopback so we can receive our own packets
                sender.setMulticastLoopback(true);

                // Send to multicast group
                const char *msg = "multicast test";
                sender.writeDatagram(msg, std::strlen(msg),
                        SocketAddress(Ipv4Address(239, 255, 0, 1), port));

                // Receive
                char buf[256];
                ssize_t n = receiver.readDatagram(buf, sizeof(buf));
                CHECK(n > 0);
                if(n > 0) {
                        CHECK(std::memcmp(buf, msg, n) == 0);
                }

                // Leave group
                err = receiver.leaveMulticastGroup(mcGroup);
                CHECK(err.isOk());
        }

        SUBCASE("setMulticastTTL") {
                UdpSocket sock;
                sock.open(IODevice::ReadWrite);
                Error err = sock.setMulticastTTL(4);
                CHECK(err.isOk());
        }

        SUBCASE("setDscp") {
                UdpSocket sock;
                sock.open(IODevice::ReadWrite);
                // AF41 = 0x22 (34)
                Error err = sock.setDscp(0x22);
                CHECK(err.isOk());
        }

        SUBCASE("bytesAvailable before any data") {
                UdpSocket sock;
                sock.open(IODevice::ReadWrite);
                CHECK(sock.bytesAvailable() == 0);
        }

        SUBCASE("double close is safe") {
                UdpSocket sock;
                sock.open(IODevice::ReadWrite);
                sock.close();
                Error err = sock.close();
                CHECK(err.isOk());
        }

        SUBCASE("writeDatagram with Buffer") {
                UdpSocket sender;
                UdpSocket receiver;

                sender.open(IODevice::ReadWrite);
                receiver.open(IODevice::ReadWrite);
                receiver.setReceiveTimeout(2000);
                receiver.bind(SocketAddress::any(0));
                uint16_t port = receiver.localAddress().port();

                const char *msg = "buffer datagram";
                size_t msgLen = std::strlen(msg);
                Buffer buf(msgLen);
                buf.setSize(msgLen);
                std::memcpy(buf.data(), msg, msgLen);
                SocketAddress dest(Ipv4Address::loopback(), port);
                ssize_t sent = sender.writeDatagram(buf, dest);
                CHECK(sent == static_cast<ssize_t>(std::strlen(msg)));

                char rbuf[256];
                ssize_t received = receiver.readDatagram(rbuf, sizeof(rbuf));
                REQUIRE(received > 0);
                CHECK(std::memcmp(rbuf, msg, received) == 0);
        }

        SUBCASE("pendingDatagramSize") {
                UdpSocket sender;
                UdpSocket receiver;

                sender.open(IODevice::ReadWrite);
                receiver.open(IODevice::ReadWrite);
                receiver.setReceiveTimeout(2000);
                receiver.bind(SocketAddress::any(0));
                uint16_t port = receiver.localAddress().port();

                const char *msg = "size check";
                sender.writeDatagram(msg, std::strlen(msg),
                        SocketAddress(Ipv4Address::loopback(), port));

                // Give kernel a moment to deliver
                usleep(10000);
                ssize_t sz = receiver.pendingDatagramSize();
                CHECK(sz == static_cast<ssize_t>(std::strlen(msg)));

                // Now drain the datagram
                char buf[256];
                receiver.readDatagram(buf, sizeof(buf));
        }

        SUBCASE("pendingDatagramSize on closed socket") {
                UdpSocket sock;
                CHECK(sock.pendingDatagramSize() == -1);
        }

        SUBCASE("read and write on closed socket") {
                UdpSocket sock;
                char buf[16];
                CHECK(sock.read(buf, sizeof(buf)) == -1);
                CHECK(sock.write("test", 4) == -1);
        }

        SUBCASE("writeDatagram on closed socket") {
                UdpSocket sock;
                CHECK(sock.writeDatagram("test", 4, SocketAddress::localhost(5004)) == -1);
        }

        SUBCASE("readDatagram on closed socket") {
                UdpSocket sock;
                char buf[16];
                CHECK(sock.readDatagram(buf, sizeof(buf)) == -1);
        }

        SUBCASE("bytesAvailable on closed socket") {
                UdpSocket sock;
                CHECK(sock.bytesAvailable() == 0);
        }

        SUBCASE("setReuseAddress on closed socket fails") {
                UdpSocket sock;
                CHECK(sock.setReuseAddress(true).isError());
        }

        SUBCASE("setDscp on closed socket fails") {
                UdpSocket sock;
                CHECK(sock.setDscp(0x22).isError());
        }

        SUBCASE("setMulticastTTL on closed socket fails") {
                UdpSocket sock;
                CHECK(sock.setMulticastTTL(4).isError());
        }

        SUBCASE("setMulticastLoopback on closed socket fails") {
                UdpSocket sock;
                CHECK(sock.setMulticastLoopback(true).isError());
        }

        SUBCASE("joinMulticastGroup on closed socket fails") {
                UdpSocket sock;
                SocketAddress mc(Ipv4Address(239, 0, 0, 1), 5004);
                CHECK(sock.joinMulticastGroup(mc).isError());
        }

        SUBCASE("joinMulticastGroup non-multicast address fails") {
                UdpSocket sock;
                sock.open(IODevice::ReadWrite);
                SocketAddress unicast(Ipv4Address(10, 0, 0, 1), 5004);
                CHECK(sock.joinMulticastGroup(unicast) == Error::Invalid);
        }

        SUBCASE("leaveMulticastGroup on closed socket fails") {
                UdpSocket sock;
                SocketAddress mc(Ipv4Address(239, 0, 0, 1), 5004);
                CHECK(sock.leaveMulticastGroup(mc).isError());
        }

        SUBCASE("leaveMulticastGroup non-multicast address fails") {
                UdpSocket sock;
                sock.open(IODevice::ReadWrite);
                SocketAddress unicast(Ipv4Address(10, 0, 0, 1), 5004);
                CHECK(sock.leaveMulticastGroup(unicast) == Error::Invalid);
        }

        SUBCASE("setDscp IPv6") {
                UdpSocket sock;
                sock.openIpv6(IODevice::ReadWrite);
                Error err = sock.setDscp(0x22);
                CHECK(err.isOk());
        }

        SUBCASE("setMulticastTTL IPv6") {
                UdpSocket sock;
                sock.openIpv6(IODevice::ReadWrite);
                Error err = sock.setMulticastTTL(4);
                CHECK(err.isOk());
        }

        SUBCASE("setMulticastLoopback IPv6") {
                UdpSocket sock;
                sock.openIpv6(IODevice::ReadWrite);
                Error err = sock.setMulticastLoopback(true);
                CHECK(err.isOk());
        }
}
