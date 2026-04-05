/**
 * @file      abstractsocket.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/udpsocket.h>
#include <promeki/tcpsocket.h>
#include <promeki/tcpserver.h>
#include <promeki/elapsedtimer.h>
#include <sys/socket.h>

using namespace promeki;

TEST_CASE("AbstractSocket") {

        SUBCASE("initial state") {
                UdpSocket sock;
                CHECK(sock.socketType() == AbstractSocket::UdpSocketType);
                CHECK(sock.state() == AbstractSocket::Unconnected);
                CHECK_FALSE(sock.isOpen());
                CHECK(sock.socketDescriptor() == -1);
                CHECK(sock.isSequential());
        }

        SUBCASE("open and close") {
                UdpSocket sock;
                Error err = sock.open(IODevice::ReadWrite);
                CHECK(err.isOk());
                CHECK(sock.isOpen());
                CHECK(sock.socketDescriptor() >= 0);
                CHECK(sock.state() == AbstractSocket::Unconnected);
                err = sock.close();
                CHECK(err.isOk());
                CHECK_FALSE(sock.isOpen());
                CHECK(sock.socketDescriptor() == -1);
        }

        SUBCASE("bind") {
                UdpSocket sock;
                sock.open(IODevice::ReadWrite);
                Error err = sock.bind(SocketAddress::any(0));
                CHECK(err.isOk());
                CHECK(sock.state() == AbstractSocket::Bound);
                CHECK(sock.localAddress().port() != 0);
        }

        SUBCASE("bind before open fails") {
                UdpSocket sock;
                Error err = sock.bind(SocketAddress::any(5004));
                CHECK(err.isError());
        }

        SUBCASE("setSocketOption and socketOption") {
                UdpSocket sock;
                sock.open(IODevice::ReadWrite);
                Error err = sock.setSocketOption(SOL_SOCKET, SO_RCVBUF, 65536);
                CHECK(err.isOk());
                auto [val, gerr] = sock.socketOption(SOL_SOCKET, SO_RCVBUF);
                CHECK(gerr.isOk());
                CHECK(val >= 65536);
        }

        SUBCASE("setSocketDescriptor") {
                // Create a raw fd, then adopt it
                int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
                REQUIRE(fd >= 0);
                UdpSocket sock;
                sock.setSocketDescriptor(fd);
                CHECK(sock.isOpen());
                CHECK(sock.socketDescriptor() == fd);
                // sock takes ownership and will close fd
        }

        SUBCASE("connectToHost and peerAddress") {
                UdpSocket receiver;
                receiver.open(IODevice::ReadWrite);
                receiver.bind(SocketAddress::any(0));
                uint16_t port = receiver.localAddress().port();

                UdpSocket sender;
                sender.open(IODevice::ReadWrite);
                Error err = sender.connectToHost(SocketAddress(Ipv4Address::loopback(), port));
                CHECK(err.isOk());
                CHECK(sender.state() == AbstractSocket::Connected);
                CHECK(sender.peerAddress().port() == port);
                CHECK(sender.peerAddress().isLoopback());
        }

        SUBCASE("disconnectFromHost UDP") {
                UdpSocket receiver;
                receiver.open(IODevice::ReadWrite);
                receiver.bind(SocketAddress::any(0));
                uint16_t port = receiver.localAddress().port();

                UdpSocket sender;
                sender.open(IODevice::ReadWrite);
                sender.bind(SocketAddress::any(0));
                sender.connectToHost(SocketAddress(Ipv4Address::loopback(), port));
                CHECK(sender.state() == AbstractSocket::Connected);

                sender.disconnectFromHost();
                CHECK(sender.state() == AbstractSocket::Bound);
                CHECK(sender.peerAddress().isNull());
        }

        SUBCASE("disconnectFromHost when not open is safe") {
                UdpSocket sock;
                sock.disconnectFromHost();
                CHECK(sock.state() == AbstractSocket::Unconnected);
        }

        SUBCASE("setSocketOption on closed socket fails") {
                UdpSocket sock;
                Error err = sock.setSocketOption(SOL_SOCKET, SO_RCVBUF, 65536);
                CHECK(err.isError());
        }

        SUBCASE("socketOption on closed socket fails") {
                UdpSocket sock;
                auto [val, err] = sock.socketOption(SOL_SOCKET, SO_RCVBUF);
                CHECK(err.isError());
        }

        SUBCASE("connectToHost on closed socket fails") {
                UdpSocket sock;
                Error err = sock.connectToHost(SocketAddress::localhost(5004));
                CHECK(err.isError());
        }

        SUBCASE("setReceiveTimeout on closed socket fails") {
                UdpSocket sock;
                Error err = sock.setReceiveTimeout(1000);
                CHECK(err.isError());
        }

        SUBCASE("setSendTimeout on closed socket fails") {
                UdpSocket sock;
                Error err = sock.setSendTimeout(1000);
                CHECK(err.isError());
        }

        SUBCASE("setReceiveTimeout success") {
                UdpSocket sock;
                sock.open(IODevice::ReadWrite);
                Error err = sock.setReceiveTimeout(500);
                CHECK(err.isOk());
        }

        SUBCASE("setSendTimeout success") {
                UdpSocket sock;
                sock.open(IODevice::ReadWrite);
                Error err = sock.setSendTimeout(500);
                CHECK(err.isOk());
        }

        SUBCASE("default receive timeout applied on open") {
                UdpSocket sock;
                sock.open(IODevice::ReadWrite);
                sock.bind(SocketAddress::any(0));

                // Read with no data — should time out at the default (5 s),
                // not block forever.  We verify it returns within a generous
                // window rather than testing the exact 5 s value.
                char buf[64];
                ElapsedTimer timer;
                int64_t ret = sock.read(buf, sizeof(buf));
                int64_t elapsed = timer.elapsed();

                CHECK(ret == -1);
                CHECK(elapsed >= 4500);
                CHECK(elapsed < 10000);
        }

        SUBCASE("receive timeout causes read to fail after correct duration") {
                UdpSocket sock;
                sock.open(IODevice::ReadWrite);
                sock.bind(SocketAddress::any(0));
                Error err = sock.setReceiveTimeout(200);
                REQUIRE(err.isOk());

                char buf[64];
                ElapsedTimer timer;
                int64_t ret = sock.read(buf, sizeof(buf));
                int64_t elapsed = timer.elapsed();

                CHECK(ret == -1);
                CHECK(elapsed >= 180);   // Allow 20 ms early jitter
                CHECK(elapsed < 1000);   // Must not hang
        }

        SUBCASE("receive timeout causes readDatagram to fail after correct duration") {
                UdpSocket sock;
                sock.open(IODevice::ReadWrite);
                sock.bind(SocketAddress::any(0));
                Error err = sock.setReceiveTimeout(200);
                REQUIRE(err.isOk());

                char buf[64];
                ElapsedTimer timer;
                ssize_t ret = sock.readDatagram(buf, sizeof(buf));
                int64_t elapsed = timer.elapsed();

                CHECK(ret == -1);
                CHECK(elapsed >= 180);
                CHECK(elapsed < 1000);
        }

        SUBCASE("receive timeout on TCP socket") {
                TcpServer server;
                Error err = server.listen(SocketAddress::localhost(0));
                REQUIRE(err.isOk());
                uint16_t port = server.serverAddress().port();

                TcpSocket client;
                client.open(IODevice::ReadWrite);
                err = client.connectToHost(SocketAddress::localhost(port));
                REQUIRE(err.isOk());

                err = server.waitForNewConnection(1000);
                REQUIRE(err.isOk());
                TcpSocket *accepted = server.nextPendingConnection();
                REQUIRE(accepted != nullptr);

                // Set timeout on server side, then read with no data arriving
                err = accepted->setReceiveTimeout(200);
                REQUIRE(err.isOk());

                char buf[64];
                ElapsedTimer timer;
                int64_t ret = accepted->read(buf, sizeof(buf));
                int64_t elapsed = timer.elapsed();

                CHECK(ret == -1);
                CHECK(elapsed >= 180);
                CHECK(elapsed < 1000);

                delete accepted;
        }

        SUBCASE("stateChanged signal emitted") {
                UdpSocket sock;
                sock.open(IODevice::ReadWrite);
                AbstractSocket::SocketState lastState = AbstractSocket::Unconnected;
                sock.stateChangedSignal.connect([&](AbstractSocket::SocketState s) {
                        lastState = s;
                });
                sock.bind(SocketAddress::any(0));
                CHECK(lastState == AbstractSocket::Bound);
        }
}
