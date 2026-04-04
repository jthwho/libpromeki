/**
 * @file      tcpsocket.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/tcpsocket.h>
#include <promeki/tcpserver.h>
#include <cstring>
#include <unistd.h>

using namespace promeki;

TEST_CASE("TcpSocket") {

        SUBCASE("construction") {
                TcpSocket sock;
                CHECK(sock.socketType() == AbstractSocket::TcpSocketType);
                CHECK_FALSE(sock.isOpen());
        }

        SUBCASE("open and close") {
                TcpSocket sock;
                Error err = sock.open(IODevice::ReadWrite);
                CHECK(err.isOk());
                CHECK(sock.isOpen());
                err = sock.close();
                CHECK(err.isOk());
                CHECK_FALSE(sock.isOpen());
        }

        SUBCASE("open IPv6") {
                TcpSocket sock;
                Error err = sock.openIpv6(IODevice::ReadWrite);
                CHECK(err.isOk());
                CHECK(sock.isOpen());
                sock.close();
        }

        SUBCASE("setNoDelay") {
                TcpSocket sock;
                sock.open(IODevice::ReadWrite);
                Error err = sock.setNoDelay(true);
                CHECK(err.isOk());
        }

        SUBCASE("setKeepAlive") {
                TcpSocket sock;
                sock.open(IODevice::ReadWrite);
                Error err = sock.setKeepAlive(true);
                CHECK(err.isOk());
        }

        SUBCASE("loopback echo via TcpServer") {
                TcpServer server;
                Error err = server.listen(SocketAddress::localhost(0));
                REQUIRE(err.isOk());
                REQUIRE(server.isListening());
                uint16_t port = server.serverAddress().port();
                REQUIRE(port != 0);

                // Connect client
                TcpSocket client;
                client.open(IODevice::ReadWrite);
                err = client.connectToHost(SocketAddress::localhost(port));
                REQUIRE(err.isOk());

                // Accept connection
                err = server.waitForNewConnection(1000);
                REQUIRE(err.isOk());
                TcpSocket *accepted = server.nextPendingConnection();
                REQUIRE(accepted != nullptr);
                CHECK(accepted->isOpen());

                // Send from client, receive on server
                const char *msg = "hello TCP";
                int64_t sent = client.write(msg, std::strlen(msg));
                CHECK(sent == static_cast<int64_t>(std::strlen(msg)));

                char buf[256];
                int64_t received = accepted->read(buf, sizeof(buf));
                REQUIRE(received > 0);
                CHECK(received == static_cast<int64_t>(std::strlen(msg)));
                CHECK(std::memcmp(buf, msg, received) == 0);

                // Echo back
                accepted->write(buf, received);
                int64_t echoed = client.read(buf, sizeof(buf));
                CHECK(echoed == received);

                delete accepted;
                server.close();
        }

        SUBCASE("bytesAvailable") {
                TcpServer server;
                server.listen(SocketAddress::localhost(0));
                uint16_t port = server.serverAddress().port();

                TcpSocket client;
                client.open(IODevice::ReadWrite);
                client.connectToHost(SocketAddress::localhost(port));

                server.waitForNewConnection(1000);
                TcpSocket *accepted = server.nextPendingConnection();
                REQUIRE(accepted != nullptr);

                CHECK(accepted->bytesAvailable() == 0);

                const char *msg = "test bytes available";
                client.write(msg, std::strlen(msg));

                // Give kernel a moment to deliver
                usleep(10000);
                CHECK(accepted->bytesAvailable() == static_cast<int64_t>(std::strlen(msg)));

                delete accepted;
        }

        SUBCASE("read and write on closed socket") {
                TcpSocket sock;
                char buf[16];
                CHECK(sock.read(buf, sizeof(buf)) == -1);
                CHECK(sock.write("test", 4) == -1);
        }

        SUBCASE("bytesAvailable on closed socket") {
                TcpSocket sock;
                CHECK(sock.bytesAvailable() == 0);
        }

        SUBCASE("setNoDelay on closed socket fails") {
                TcpSocket sock;
                CHECK(sock.setNoDelay(true).isError());
        }

        SUBCASE("setKeepAlive on closed socket fails") {
                TcpSocket sock;
                CHECK(sock.setKeepAlive(true).isError());
        }

        SUBCASE("waitForConnected") {
                TcpServer server;
                server.listen(SocketAddress::localhost(0));
                uint16_t port = server.serverAddress().port();

                TcpSocket client;
                client.open(IODevice::ReadWrite);
                // TCP connect to loopback is immediate, so state will be Connected
                Error err = client.connectToHost(SocketAddress::localhost(port));
                CHECK(err.isOk());
                // Either already connected or connecting
                if(client.state() == AbstractSocket::Connecting) {
                        err = client.waitForConnected(1000);
                        CHECK(err.isOk());
                }
                CHECK(client.state() == AbstractSocket::Connected);

                // Clean up server side
                server.waitForNewConnection(1000);
                TcpSocket *accepted = server.nextPendingConnection();
                delete accepted;
        }

        SUBCASE("waitForConnected invalid state") {
                TcpSocket sock;
                sock.open(IODevice::ReadWrite);
                // Not in Connecting state
                Error err = sock.waitForConnected(100);
                CHECK(err.isError());
        }

        SUBCASE("disconnectFromHost TCP") {
                TcpServer server;
                server.listen(SocketAddress::localhost(0));
                uint16_t port = server.serverAddress().port();

                TcpSocket client;
                client.open(IODevice::ReadWrite);
                client.connectToHost(SocketAddress::localhost(port));

                server.waitForNewConnection(1000);
                TcpSocket *accepted = server.nextPendingConnection();
                REQUIRE(accepted != nullptr);

                client.disconnectFromHost();
                CHECK(client.state() == AbstractSocket::Unconnected);
                CHECK_FALSE(client.isOpen());

                delete accepted;
        }
}
