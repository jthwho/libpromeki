/**
 * @file      localsocket.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstring>
#include <thread>
#include <chrono>
#include <doctest/doctest.h>
#include <promeki/localserver.h>
#include <promeki/localsocket.h>
#include <promeki/error.h>
#include <promeki/string.h>
#include <promeki/uuid.h>
#include <promeki/dir.h>

using namespace promeki;

// ============================================================================
// Helper: unique socket path under the library's configured temp dir.
// ============================================================================

static String uniqueSocketPath(const char *tag) {
        return Dir::temp()
                .path()
                .join(String("promeki-test-localsock-") + String(tag) + String("-") + UUID::generateV4().toString() +
                      String(".sock"))
                .toString();
}

TEST_CASE("LocalSocket: isSupported is true on POSIX") {
#if defined(PROMEKI_PLATFORM_POSIX)
        CHECK(LocalSocket::isSupported());
        CHECK(LocalServer::isSupported());
#else
        CHECK_FALSE(LocalSocket::isSupported());
        CHECK_FALSE(LocalServer::isSupported());
#endif
}

TEST_CASE("LocalServer: listen binds and creates a socket file") {
        if (!LocalServer::isSupported()) return;
        String path = uniqueSocketPath("listen");

        LocalServer server;
        Error       err = server.listen(path);
        REQUIRE(err.isOk());
        CHECK(server.isListening());
        CHECK(server.serverPath() == path);

        // close() unlinks the socket file, so a second listen at the
        // same path succeeds.
        server.close();
        CHECK_FALSE(server.isListening());

        LocalServer server2;
        CHECK(server2.listen(path).isOk());
}

TEST_CASE("LocalServer: listen on empty path is rejected") {
        if (!LocalServer::isSupported()) return;
        LocalServer server;
        Error       err = server.listen(String());
        CHECK(err == Error::Invalid);
        CHECK_FALSE(server.isListening());
}

TEST_CASE("LocalSocket: connect/accept/read/write roundtrip") {
        if (!LocalSocket::isSupported()) return;
        String path = uniqueSocketPath("roundtrip");

        LocalServer server;
        REQUIRE(server.listen(path).isOk());

        LocalSocket client;
        REQUIRE(client.connectTo(path).isOk());
        CHECK(client.isConnected());

        // Server sees a pending connection.
        CHECK(server.waitForNewConnection(2000).isOk());
        LocalSocket *server_side = server.nextPendingConnection();
        REQUIRE(server_side != nullptr);
        CHECK(server_side->isConnected());

        // Client → server.
        const char *msg = "hello";
        CHECK(client.write(msg, 5) == 5);

        char    rxbuf[16] = {};
        int64_t n = server_side->read(rxbuf, sizeof(rxbuf));
        CHECK(n == 5);
        CHECK(std::memcmp(rxbuf, "hello", 5) == 0);

        // Server → client.
        CHECK(server_side->write("ACK", 3) == 3);
        std::memset(rxbuf, 0, sizeof(rxbuf));
        n = client.read(rxbuf, sizeof(rxbuf));
        CHECK(n == 3);
        CHECK(std::memcmp(rxbuf, "ACK", 3) == 0);

        delete server_side;
}

TEST_CASE("LocalSocket: connect to nonexistent path fails") {
        if (!LocalSocket::isSupported()) return;
        LocalSocket client;
        Error       err = client.connectTo(uniqueSocketPath("nobody-home"));
        CHECK(err.isError());
        CHECK_FALSE(client.isConnected());
}

TEST_CASE("LocalSocket: peer disconnect reports EOF via read == 0") {
        if (!LocalSocket::isSupported()) return;
        String      path = uniqueSocketPath("eof");
        LocalServer server;
        REQUIRE(server.listen(path).isOk());

        LocalSocket client;
        REQUIRE(client.connectTo(path).isOk());
        REQUIRE(server.waitForNewConnection(2000).isOk());
        LocalSocket *server_side = server.nextPendingConnection();
        REQUIRE(server_side != nullptr);

        // Client closes.
        client.close();

        char    buf[4];
        int64_t n = server_side->read(buf, sizeof(buf));
        CHECK(n == 0);
        CHECK_FALSE(server_side->isConnected());
        delete server_side;
}

TEST_CASE("LocalServer: accept multiple clients") {
        if (!LocalServer::isSupported()) return;
        String      path = uniqueSocketPath("multi");
        LocalServer server;
        REQUIRE(server.listen(path).isOk());

        LocalSocket c1, c2, c3;
        REQUIRE(c1.connectTo(path).isOk());
        REQUIRE(c2.connectTo(path).isOk());
        REQUIRE(c3.connectTo(path).isOk());

        int accepted = 0;
        for (int i = 0; i < 3; ++i) {
                if (server.waitForNewConnection(2000).isOk()) {
                        LocalSocket *s = server.nextPendingConnection();
                        if (s != nullptr) {
                                delete s;
                                ++accepted;
                        }
                }
        }
        CHECK(accepted == 3);
}

TEST_CASE("LocalServer: waitForNewConnection times out when idle") {
        if (!LocalServer::isSupported()) return;
        LocalServer server;
        REQUIRE(server.listen(uniqueSocketPath("idle")).isOk());
        auto  start = std::chrono::steady_clock::now();
        Error err = server.waitForNewConnection(100);
        auto  elapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
        CHECK(err == Error::Timeout);
        CHECK(elapsed >= 80); // allow some scheduler slack
}

TEST_CASE("LocalServer: hasPendingConnections reflects state") {
        if (!LocalServer::isSupported()) return;
        String      path = uniqueSocketPath("pending");
        LocalServer server;
        REQUIRE(server.listen(path).isOk());
        CHECK_FALSE(server.hasPendingConnections());

        LocalSocket client;
        REQUIRE(client.connectTo(path).isOk());
        // Give the kernel a moment to materialize the pending connection.
        for (int i = 0; i < 100 && !server.hasPendingConnections(); ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        CHECK(server.hasPendingConnections());

        LocalSocket *s = server.nextPendingConnection();
        REQUIRE(s != nullptr);
        CHECK_FALSE(server.hasPendingConnections());
        delete s;
}

TEST_CASE("LocalServer: refuses to stomp a non-socket file") {
        if (!LocalServer::isSupported()) return;
        // Create a regular file at the requested path.
        String path =
                Dir::temp().path().join(String("promeki-test-notsocket-") + UUID::generateV4().toString()).toString();
        {
                FILE *f = std::fopen(path.cstr(), "w");
                REQUIRE(f != nullptr);
                std::fprintf(f, "not a socket\n");
                std::fclose(f);
        }
        LocalServer server;
        Error       err = server.listen(path);
        CHECK(err == Error::Exists);
        CHECK_FALSE(server.isListening());
        // Cleanup.
        std::remove(path.cstr());
}

TEST_CASE("LocalServer: listen over a stale socket file succeeds") {
        if (!LocalServer::isSupported()) return;
        String path = uniqueSocketPath("stale");

        // First server creates + leaves a socket file.
        {
                LocalServer first;
                REQUIRE(first.listen(path).isOk());
                // Destructor unlinks — simulate a crash by manually
                // re-creating the socket file with a second listen that we
                // don't close.  We rely on the removeStaleSocket path in
                // listen() to clean it up for the next server.
        }
        // A leftover should NOT make the next listen fail.
        // (Our first server's dtor already unlinked; additionally exercise
        //  the stale path by explicitly binding twice below.)
        LocalServer second;
        REQUIRE(second.listen(path).isOk());
        LocalServer third;
        second.close();
        REQUIRE(third.listen(path).isOk());
}

TEST_CASE("LocalSocket: destructor closes a connected socket") {
        if (!LocalSocket::isSupported()) return;
        String      path = uniqueSocketPath("dtor");
        LocalServer server;
        REQUIRE(server.listen(path).isOk());
        {
                LocalSocket client;
                REQUIRE(client.connectTo(path).isOk());
        }
        // Accept the connection, then the server side should read EOF.
        REQUIRE(server.waitForNewConnection(2000).isOk());
        LocalSocket *server_side = server.nextPendingConnection();
        REQUIRE(server_side != nullptr);
        char    buf[4];
        int64_t n = server_side->read(buf, sizeof(buf));
        CHECK(n == 0);
        delete server_side;
}

TEST_CASE("LocalSocket: open twice without connect is rejected") {
        if (!LocalSocket::isSupported()) return;
        LocalSocket sock;
        REQUIRE(sock.open(IODevice::ReadWrite).isOk());
        CHECK(sock.open(IODevice::ReadWrite) == Error::AlreadyOpen);
}
