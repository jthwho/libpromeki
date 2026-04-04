/**
 * @file      tcpserver.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/tcpserver.h>
#include <promeki/tcpsocket.h>

using namespace promeki;

TEST_CASE("TcpServer") {

        SUBCASE("construction") {
                TcpServer server;
                CHECK_FALSE(server.isListening());
        }

        SUBCASE("listen on any port") {
                TcpServer server;
                Error err = server.listen(SocketAddress::any(0));
                CHECK(err.isOk());
                CHECK(server.isListening());
                CHECK(server.serverAddress().port() != 0);
        }

        SUBCASE("listen on localhost") {
                TcpServer server;
                Error err = server.listen(SocketAddress::localhost(0));
                CHECK(err.isOk());
                CHECK(server.isListening());
                CHECK(server.serverAddress().isLoopback());
        }

        SUBCASE("close") {
                TcpServer server;
                server.listen(SocketAddress::any(0));
                server.close();
                CHECK_FALSE(server.isListening());
        }

        SUBCASE("double listen fails") {
                TcpServer server;
                server.listen(SocketAddress::any(0));
                Error err = server.listen(SocketAddress::any(0));
                CHECK(err.isError());
        }

        SUBCASE("hasPendingConnections initially false") {
                TcpServer server;
                server.listen(SocketAddress::any(0));
                CHECK_FALSE(server.hasPendingConnections());
        }

        SUBCASE("waitForNewConnection timeout") {
                TcpServer server;
                server.listen(SocketAddress::any(0));
                Error err = server.waitForNewConnection(10);
                CHECK(err == Error::Timeout);
        }

        SUBCASE("accept connection") {
                TcpServer server;
                server.listen(SocketAddress::localhost(0));
                uint16_t port = server.serverAddress().port();

                // Connect a client
                TcpSocket client;
                client.open(IODevice::ReadWrite);
                Error err = client.connectToHost(SocketAddress::localhost(port));
                REQUIRE(err.isOk());

                // Wait and accept
                err = server.waitForNewConnection(1000);
                REQUIRE(err.isOk());
                CHECK(server.hasPendingConnections());

                TcpSocket *accepted = server.nextPendingConnection();
                REQUIRE(accepted != nullptr);
                CHECK(accepted->isOpen());
                delete accepted;
        }

        SUBCASE("setMaxPendingConnections") {
                TcpServer server;
                server.setMaxPendingConnections(10);
                Error err = server.listen(SocketAddress::localhost(0));
                CHECK(err.isOk());
                CHECK(server.isListening());
        }

        SUBCASE("waitForNewConnection when not listening") {
                TcpServer server;
                Error err = server.waitForNewConnection(10);
                CHECK(err.isError());
        }

        SUBCASE("nextPendingConnection when not listening") {
                TcpServer server;
                CHECK(server.nextPendingConnection() == nullptr);
        }

        SUBCASE("hasPendingConnections when not listening") {
                TcpServer server;
                CHECK_FALSE(server.hasPendingConnections());
        }

        SUBCASE("close when not listening is safe") {
                TcpServer server;
                server.close();
                CHECK_FALSE(server.isListening());
        }

        SUBCASE("server address reflects bound address") {
                TcpServer server;
                server.listen(SocketAddress::any(0));
                CHECK_FALSE(server.serverAddress().isNull());
                CHECK(server.serverAddress().port() != 0);
                server.close();
                CHECK(server.serverAddress().isNull());
        }
}
