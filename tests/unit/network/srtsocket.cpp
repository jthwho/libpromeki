/**
 * @file      srtsocket.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/srtsocket.h>
#include <promeki/srtserver.h>
#include <promeki/srtsockettransport.h>
#include <promeki/srtepoll.h>
#include <promeki/srtgroup.h>
#include <promeki/socketaddress.h>

#include <cstring>
#include <thread>
#include <atomic>
#include <chrono>

using namespace promeki;

namespace {

        // Reserve a free loopback port by transiently listening on 0
        // and recording the assigned port.  The server is closed
        // before the real test stands up its own listener.  The
        // tradeoff (theoretically the port could be reused before
        // the real listener binds) is acceptable for unit-test scope.
        static uint16_t reserveLoopbackPort() {
                SrtServer probe;
                Error     err = probe.listen(SocketAddress::localhost(0));
                if (err.isError()) return 0;
                const uint16_t port = probe.serverAddress().port();
                probe.close();
                return port;
        }

} // anonymous namespace

TEST_CASE("SrtSocket: initial state") {
        SrtSocket sock;
        CHECK_FALSE(sock.isOpen());
        CHECK(sock.handle() == SrtSocket::InvalidHandle);
        CHECK(sock.state() == SrtSocket::NonExist);
        CHECK(sock.isSequential());
        CHECK(sock.transportType() == SrtSocket::Live);
}

TEST_CASE("SrtSocket: open and close") {
        SrtSocket sock;
        Error     err = sock.open(IODevice::ReadWrite);
        REQUIRE(err.isOk());
        CHECK(sock.isOpen());
        CHECK(sock.handle() != SrtSocket::InvalidHandle);
        CHECK(sock.state() == SrtSocket::Init);
        err = sock.close();
        CHECK(err.isOk());
        CHECK_FALSE(sock.isOpen());
}

TEST_CASE("SrtSocket: option validators reject bad values") {
        SrtSocket sock;
        CHECK(sock.setLatency(-1).isError());
        CHECK(sock.setLatency(120).isOk());
        CHECK(sock.setEncryptionKeyLength(7).isError());
        CHECK(sock.setEncryptionKeyLength(16).isOk());
        // 5 bytes is too short, 80 too long; 10..79 is the SRT spec range.
        CHECK(sock.setPassphrase(String("short")).isError());
        CHECK(sock.setPassphrase(String("a-valid-passphrase-1234")).isOk());
        CHECK(sock.setPayloadSize(2000).isError());
        CHECK(sock.setPayloadSize(1316).isOk());
}

TEST_CASE("SrtSocket: groupHandle is invalid for a non-bonded socket") {
        SrtSocket sock;
        REQUIRE(sock.open(IODevice::ReadWrite).isOk());
        // No group — srt_groupof returns SRT_INVALID_SOCK, mapped to InvalidHandle.
        CHECK(sock.groupHandle() == SrtSocket::InvalidHandle);
}

TEST_CASE("SrtSocket: bind without open is an error") {
        SrtSocket sock;
        CHECK(sock.bind(SocketAddress::localhost(0)).isError());
}

TEST_CASE("SrtSocket: state on closed socket is NonExist") {
        SrtSocket sock;
        CHECK(sock.state() == SrtSocket::NonExist);
        sock.open(IODevice::ReadWrite);
        sock.close();
        CHECK(sock.state() == SrtSocket::NonExist);
}

TEST_CASE("SrtServer: listen on loopback port 0 picks a port") {
        SrtServer server;
        Error     err = server.listen(SocketAddress::localhost(0));
        REQUIRE(err.isOk());
        CHECK(server.isListening());
        CHECK(server.serverAddress().port() != 0);
        CHECK(server.handle() != SrtSocket::InvalidHandle);
        server.close();
        CHECK_FALSE(server.isListening());
}

// ============================================================
// Loopback handshake.  Anchors the whole vendoring + isolation
// story end-to-end without needing any real network: the client
// thread dials, the server accepts, both report Connected.  A
// failure here means either the SRT bundle is broken or the
// isolated mbedTLS-3.6 cannot complete an unauthenticated
// handshake.
//
// The test deliberately does *not* exercise data transfer — SRT
// live mode TSBPD timing makes message-loss tests racy at unit
// scope.  Data-path coverage belongs in functional tests under
// utils/promeki-test/, where wall-clock budgets are flexible.
// ============================================================
TEST_CASE("SrtSocket: loopback caller↔listener handshake completes") {
        const uint16_t port = reserveLoopbackPort();
        REQUIRE(port != 0);

        SrtServer server;
        server.setLatency(80);
        REQUIRE(server.listen(SocketAddress::localhost(port)).isOk());

        std::atomic<bool>  clientConnected{false};
        std::atomic<int>   clientErr{0};

        std::thread clientThread([&] {
                SrtSocket sock;
                sock.setLatency(80);
                Error e = sock.open(IODevice::ReadWrite);
                if (e.isError()) {
                        clientErr.store(static_cast<int>(e.code()));
                        return;
                }
                sock.setConnectTimeout(3000);
                e = sock.connectToHost(SocketAddress::localhost(port));
                if (e.isError()) {
                        clientErr.store(static_cast<int>(e.code()));
                        return;
                }
                clientConnected.store(true, std::memory_order_release);
                // Hold the connection open while the main thread checks
                // server-side state, then close cleanly.
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                sock.close();
        });

        SrtSocket::UPtr accepted = server.accept(5000);
        // Always join before any REQUIRE, so a failure does not leave
        // a live std::thread for unwinding to abort on.
        clientThread.join();

        REQUIRE(accepted);
        CHECK(accepted->isOpen());
        // accept() returning a non-null SrtSocket already means the
        // handshake succeeded.  The post-connect SRT state is racy
        // when both peers run in the same process — the client may
        // have already cycled to Closed by the time we look — so
        // we assert only on the things this test actually proves
        // (handshake completion + clean client exit).
        CHECK(clientConnected.load());
        CHECK(clientErr.load() == 0);

        accepted->close();
        server.close();
}

// SrtGroup: validate the API surface — bonding requires multi-path
// to fully exercise, so the unit test scope is the create/open/close
// + option-validator + empty-member-status round-trip.  Multi-path
// connect coverage belongs in a functional-test rig under
// utils/promeki-test/ where you can stage real interfaces.
TEST_CASE("SrtGroup: open / close / pre-connect options") {
        SrtGroup grp(SrtGroup::Broadcast);
        CHECK_FALSE(grp.isOpen());
        CHECK(grp.handle() == SrtGroup::InvalidHandle);
        CHECK(grp.type() == SrtGroup::Broadcast);

        REQUIRE(grp.open(IODevice::ReadWrite).isOk());
        CHECK(grp.isOpen());
        CHECK(grp.handle() != SrtGroup::InvalidHandle);

        // Group has no members before connect — status list is empty.
        CHECK(grp.memberStatus().size() == 0);

        // Option validators behave like SrtSocket's.
        CHECK(grp.setLatency(-1).isError());
        CHECK(grp.setLatency(80).isOk());
        CHECK(grp.setEncryptionKeyLength(7).isError());
        CHECK(grp.setEncryptionKeyLength(16).isOk());
        CHECK(grp.setPassphrase(String("short")).isError());
        CHECK(grp.setPassphrase(String("a-valid-passphrase-1234")).isOk());
        CHECK(grp.setPayloadSize(2000).isError());
        CHECK(grp.setPayloadSize(1316).isOk());
        CHECK(grp.setStreamId(String("group-stream-id")).isOk());
        CHECK(grp.setMaxBandwidth(1024 * 1024).isOk());

        // connect() with an empty member list is a programmer error.
        CHECK(grp.connect(SrtGroup::MemberList{}).isError());

        // connect() with a NULL peer address is also rejected without
        // hitting libsrt.
        SrtGroup::MemberList bad;
        bad.pushToBack(SrtGroup::Member{});
        CHECK(grp.connect(bad).isError());

        REQUIRE(grp.close().isOk());
        CHECK_FALSE(grp.isOpen());
        CHECK(grp.handle() == SrtGroup::InvalidHandle);
}

// Adopting ctor — uses an existing group SRTSOCKET (e.g. one
// surfaced via SrtSocket::groupHandle on a listener-side accept).
// The unit test exercises the create-then-adopt path: create a
// group with the normal ctor, fish out its handle, hand the handle
// to a second SrtGroup via the adopting ctor, verify it reports
// the same handle and is open.  We deliberately steal the handle
// out of the original SrtGroup (close() suppression) so only the
// adopted instance closes the underlying SRTSOCKET on destruction.
TEST_CASE("SrtGroup: adopting ctor wraps an existing handle") {
        SrtGroup donor(SrtGroup::Broadcast);
        REQUIRE(donor.open(IODevice::ReadWrite).isOk());
        const int handle = donor.handle();
        REQUIRE(handle != SrtGroup::InvalidHandle);
        {
                SrtGroup adopted(handle, SrtGroup::Broadcast);
                CHECK(adopted.handle() == handle);
                CHECK(adopted.isOpen());
                CHECK(adopted.type() == SrtGroup::Broadcast);
                // adopted destructor closes the SRTSOCKET.
        }
        // Stop donor from re-closing the now-invalid handle.
        // Using close() on a stale handle would be benign (libsrt
        // returns SRT_ERROR), but the cleaner story is that the
        // donor never owned it after adoption — we simulate that
        // by skipping its close().
        // We don't have a release()/detach() yet, so force-close
        // here would be harmless but ugly; suppress instead:
        // (donor's destructor will call srt_close(invalid) which
        // returns -1, which we ignore, so we just let it run.)
}

TEST_CASE("SrtGroup: type=Backup is honoured") {
        SrtGroup grp(SrtGroup::Backup);
        REQUIRE(grp.open(IODevice::ReadWrite).isOk());
        CHECK(grp.type() == SrtGroup::Backup);
        grp.close();
}

// SrtEpoll: verify accept-ready notification fires when a peer
// connects.  Avoids data-path timing by stopping at the handshake.
TEST_CASE("SrtEpoll: server reports ReadReady when a client connects") {
        const uint16_t port = reserveLoopbackPort();
        REQUIRE(port != 0);

        SrtServer server;
        REQUIRE(server.listen(SocketAddress::localhost(port)).isOk());

        SrtEpoll mux;
        REQUIRE(mux.add(server, SrtEpoll::ReadReady).isOk());

        // No client yet — should time out.
        SrtEpoll::ReadyList ready;
        const int           initial = mux.wait(ready, 100);
        CHECK(initial == 0);
        CHECK(ready.size() == 0);

        std::atomic<bool> clientConnected{false};
        std::thread       clientThread([&] {
                SrtSocket sock;
                if (sock.open(IODevice::ReadWrite).isError()) return;
                sock.setConnectTimeout(2000);
                if (sock.connectToHost(SocketAddress::localhost(port)).isOk()) {
                        clientConnected.store(true, std::memory_order_release);
                        std::this_thread::sleep_for(std::chrono::milliseconds(150));
                        sock.close();
                }
        });

        // Wait up to 5s for the accept-ready notification.
        const int n = mux.wait(ready, 5000);
        REQUIRE(n >= 1);
        CHECK(ready[0].handle == server.handle());
        CHECK((ready[0].events & SrtEpoll::ReadReady) != 0);

        // Drain the accept and let the client thread finish.
        SrtSocket::UPtr accepted = server.accept(2000);
        clientThread.join();
        CHECK(accepted);
        CHECK(clientConnected.load());

        REQUIRE(mux.remove(server).isOk());
        server.close();
        mux.close();
        CHECK_FALSE(mux.isOpen());
}

// End-to-end encryption smoke test.  Without this, the whole
// reason we vendored a private mbedTLS-3.6 is unverified — there
// would be no proof the isolated crypto path actually drives a
// working AES handshake.  Two scenarios:
//   1. Matching passphrase → handshake completes.
//   2. Wrong passphrase → SRT_ECONNREJ → Error::ConnectionRefused.
TEST_CASE("SrtSocket: encrypted handshake with shared passphrase") {
        const uint16_t port = reserveLoopbackPort();
        REQUIRE(port != 0);
        const String passphrase("a-good-srt-passphrase-1234");

        SrtServer server;
        server.setLatency(80);
        REQUIRE(server.setPassphrase(passphrase).isOk());
        REQUIRE(server.setEncryptionKeyLength(16).isOk());
        REQUIRE(server.listen(SocketAddress::localhost(port)).isOk());

        std::atomic<bool> clientConnected{false};
        std::atomic<int>  clientErr{0};
        std::thread       clientThread([&] {
                SrtSocket sock;
                sock.setLatency(80);
                sock.setPassphrase(passphrase);
                sock.setEncryptionKeyLength(16);
                Error e = sock.open(IODevice::ReadWrite);
                if (e.isError()) {
                        clientErr.store(static_cast<int>(e.code()));
                        return;
                }
                sock.setConnectTimeout(3000);
                e = sock.connectToHost(SocketAddress::localhost(port));
                if (e.isError()) {
                        clientErr.store(static_cast<int>(e.code()));
                        return;
                }
                clientConnected.store(true, std::memory_order_release);
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                sock.close();
        });

        SrtSocket::UPtr accepted = server.accept(5000);
        clientThread.join();
        REQUIRE(accepted);
        CHECK(clientConnected.load());
        CHECK(clientErr.load() == 0);
        accepted->close();
        server.close();
}

TEST_CASE("SrtSocket: mismatched passphrase is rejected") {
        const uint16_t port = reserveLoopbackPort();
        REQUIRE(port != 0);

        SrtServer server;
        REQUIRE(server.setPassphrase(String("server-side-passphrase-XX")).isOk());
        REQUIRE(server.setEncryptionKeyLength(16).isOk());
        REQUIRE(server.listen(SocketAddress::localhost(port)).isOk());

        std::atomic<int> clientErr{0};
        std::thread      clientThread([&] {
                SrtSocket sock;
                sock.setPassphrase(String("client-different-passphrase"));
                sock.setEncryptionKeyLength(16);
                if (sock.open(IODevice::ReadWrite).isError()) {
                        clientErr.store(-1);
                        return;
                }
                sock.setConnectTimeout(2000);
                Error e = sock.connectToHost(SocketAddress::localhost(port));
                clientErr.store(static_cast<int>(e.code()));
        });

        SrtSocket::UPtr accepted = server.accept(2000);
        clientThread.join();
        // Caller must have been refused — handshake fails when SRT
        // cannot derive a matching key from the differing passphrases.
        CHECK(accepted == nullptr);
        CHECK(clientErr.load() == static_cast<int>(Error::ConnectionRefused));
        server.close();
}

// SrtEpoll push-dispatch: register a callback on a listening server,
// run the dispatch loop on a worker thread, drive a client, then
// observe that the callback fired and stop() returns cleanly.
TEST_CASE("SrtEpoll: callback dispatch via run() / stop() worker") {
        const uint16_t port = reserveLoopbackPort();
        REQUIRE(port != 0);

        SrtServer server;
        REQUIRE(server.listen(SocketAddress::localhost(port)).isOk());

        SrtEpoll mux;
        REQUIRE(mux.add(server, SrtEpoll::ReadReady).isOk());

        std::atomic<int>  cbCount{0};
        std::atomic<int>  cbEvents{0};
        REQUIRE(mux.setCallback(server, [&](int events) {
                cbEvents.store(events, std::memory_order_release);
                cbCount.fetch_add(1, std::memory_order_relaxed);
                // Drain the accept queue so SRT clears its pending
                // state — without this, the level-triggered epoll
                // would hand us the same readiness on every loop.
                SrtSocket::UPtr accepted = server.accept(0);
                (void)accepted;
        }).isOk());

        std::thread worker([&] { mux.run(); });

        // Stand up a client.  The handshake should fire the server
        // callback on the worker thread.
        std::thread clientThread([&] {
                SrtSocket sock;
                if (sock.open(IODevice::ReadWrite).isError()) return;
                sock.setConnectTimeout(2000);
                sock.connectToHost(SocketAddress::localhost(port));
                std::this_thread::sleep_for(std::chrono::milliseconds(150));
                sock.close();
        });
        clientThread.join();

        // Give the worker a moment to dispatch, then ask it to stop.
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        mux.stop();
        worker.join();

        CHECK(cbCount.load() >= 1);
        CHECK((cbEvents.load() & SrtEpoll::ReadReady) != 0);

        REQUIRE(mux.remove(server).isOk());
        server.close();
}

// SrtEpoll dispatchOnce(): same surface, pulled from the test thread
// instead of run()'s worker.  Verifies the simpler integration path
// for callers who already own an event loop and want to tick SRT.
TEST_CASE("SrtEpoll: dispatchOnce drives callback synchronously") {
        const uint16_t port = reserveLoopbackPort();
        REQUIRE(port != 0);

        SrtServer server;
        REQUIRE(server.listen(SocketAddress::localhost(port)).isOk());

        SrtEpoll mux;
        REQUIRE(mux.add(server, SrtEpoll::ReadReady).isOk());

        bool             cbFired = false;
        SrtSocket::UPtr  accepted;
        REQUIRE(mux.setCallback(server, [&](int events) {
                if (events & SrtEpoll::ReadReady) {
                        accepted = server.accept(0);
                        cbFired  = true;
                }
        }).isOk());

        // Nothing pending — should return 0.
        CHECK(mux.dispatchOnce(50) == 0);
        CHECK_FALSE(cbFired);

        // Connect a client, then tick the multiplexer.
        std::thread clientThread([&] {
                SrtSocket sock;
                if (sock.open(IODevice::ReadWrite).isError()) return;
                sock.setConnectTimeout(2000);
                sock.connectToHost(SocketAddress::localhost(port));
                std::this_thread::sleep_for(std::chrono::milliseconds(150));
                sock.close();
        });

        // Up to 5 s for the handshake to complete and the multiplexer
        // to mark the listener readable.
        const int dispatched = mux.dispatchOnce(5000);
        clientThread.join();
        CHECK(dispatched >= 1);
        CHECK(cbFired);
        CHECK(accepted);

        server.close();
}

// Listen callback receives the streamid before handshake completes,
// so the server can route or reject by stream-name.  Test: client
// sets streamid="reject-me", server's callback returns false → client
// connect should fail with ConnectionRefused.
TEST_CASE("SrtServer: listen callback can reject by streamid") {
        const uint16_t port = reserveLoopbackPort();
        REQUIRE(port != 0);

        std::atomic<int>      callbackCalls{0};
        std::atomic<bool>     sawExpectedStreamId{false};
        SrtServer             server;
        server.setListenCallback([&](const String &streamId, const SocketAddress &) {
                callbackCalls.fetch_add(1, std::memory_order_relaxed);
                if (streamId == String("reject-me")) {
                        return false;
                }
                if (streamId == String("accept-me")) {
                        sawExpectedStreamId.store(true, std::memory_order_release);
                }
                return true;
        });
        REQUIRE(server.listen(SocketAddress::localhost(port)).isOk());

        std::atomic<int> clientErr{0};
        std::thread      clientThread([&] {
                SrtSocket sock;
                sock.setStreamId(String("reject-me"));
                sock.setConnectTimeout(2000);
                Error e = sock.open(IODevice::ReadWrite);
                if (e.isError()) {
                        clientErr.store(static_cast<int>(e.code()));
                        return;
                }
                e = sock.connectToHost(SocketAddress::localhost(port));
                clientErr.store(static_cast<int>(e.code()));
        });

        SrtSocket::UPtr accepted = server.accept(2000);
        clientThread.join();
        // Caller should have been refused by the listener callback.
        CHECK(accepted == nullptr);
        CHECK(callbackCalls.load() >= 1);
        // ConnectionRefused (mapped from SRT_ECONNREJ) is the expected
        // outcome on the caller side.
        CHECK(clientErr.load() == static_cast<int>(Error::ConnectionRefused));

        server.close();
}

// Rendezvous: both peers act symmetrically — bind a known local port
// and dial the other peer's known local port.  We model that with
// two threads, each running a Rendezvous-mode SrtSocketTransport.
TEST_CASE("SrtSocketTransport: rendezvous handshake completes via loopback") {
        const uint16_t portA = reserveLoopbackPort();
        const uint16_t portB = reserveLoopbackPort();
        REQUIRE(portA != 0);
        REQUIRE(portB != 0);
        REQUIRE(portA != portB);

        std::atomic<bool> aOpened{false};
        std::atomic<bool> bOpened{false};
        std::atomic<int>  aErr{0};
        std::atomic<int>  bErr{0};

        std::thread peerA([&] {
                SrtSocketTransport tx(SrtSocketTransport::Rendezvous);
                tx.setLocalAddress(SocketAddress::localhost(portA));
                tx.setPeerAddress(SocketAddress::localhost(portB));
                tx.setLatency(80);
                Error e = tx.open();
                if (e.isError()) {
                        aErr.store(static_cast<int>(e.code()));
                        return;
                }
                aOpened.store(true, std::memory_order_release);
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                tx.close();
        });

        std::thread peerB([&] {
                SrtSocketTransport tx(SrtSocketTransport::Rendezvous);
                tx.setLocalAddress(SocketAddress::localhost(portB));
                tx.setPeerAddress(SocketAddress::localhost(portA));
                tx.setLatency(80);
                Error e = tx.open();
                if (e.isError()) {
                        bErr.store(static_cast<int>(e.code()));
                        return;
                }
                bOpened.store(true, std::memory_order_release);
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                tx.close();
        });

        peerA.join();
        peerB.join();

        CHECK(aOpened.load());
        CHECK(bOpened.load());
        CHECK(aErr.load() == 0);
        CHECK(bErr.load() == 0);
}

TEST_CASE("SrtSocketTransport: caller→listener open completes via loopback") {
        const uint16_t port = reserveLoopbackPort();
        REQUIRE(port != 0);

        std::atomic<bool> listenerOpened{false};
        std::atomic<int>  listenerErr{0};

        std::thread listenerThread([&] {
                SrtSocketTransport tx(SrtSocketTransport::Listener);
                tx.setLocalAddress(SocketAddress::localhost(port));
                tx.setLatency(80);
                tx.setAcceptTimeoutMs(5000);
                Error e = tx.open();
                if (e.isError()) {
                        listenerErr.store(static_cast<int>(e.code()));
                        return;
                }
                listenerOpened.store(true, std::memory_order_release);
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                tx.close();
        });

        // Give the listener thread a moment to bind / start listening.
        // SRT will return SRT_ECONNREJ if the caller races ahead.
        std::this_thread::sleep_for(std::chrono::milliseconds(150));

        SrtSocketTransport caller(SrtSocketTransport::Caller);
        caller.setPeerAddress(SocketAddress::localhost(port));
        caller.setLatency(80);
        Error err = caller.open();

        listenerThread.join();

        CHECK(err.isOk());
        CHECK(caller.isOpen());
        CHECK(listenerOpened.load());
        CHECK(listenerErr.load() == 0);
        caller.close();
}
