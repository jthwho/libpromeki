/**
 * @file      rtmphandshake.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>

#include <cstring>

#include <promeki/buffer.h>
#include <promeki/bufferview.h>
#include <promeki/list.h>
#include <promeki/rtmphandshake.h>

using namespace promeki;

namespace {

/**
 * @brief Builds a BufferView over a contiguous byte buffer.
 *
 * Wraps the bytes in a non-owning host Buffer so the state-machine
 * @c feed path sees a single-slice view.  The buffer must outlive
 * the returned view.
 */
BufferView wrapBytes(Buffer &storage, const uint8_t *data, size_t size) {
        storage = Buffer::wrapHost(const_cast<uint8_t *>(data), size);
        storage.setSize(size);
        return BufferView(storage, 0, size);
}

/**
 * @brief Drives a pair of handshakes against each other to completion.
 *
 * Models the wire: every byte produced by one side is delivered to the
 * other on the next loop pass.  Mirrors how an in-process pipe pair
 * would behave (plus a deterministic transcript order so failures
 * point at exact bytes).
 *
 * @return @c true when both sides reach @c Done within the iteration
 *         budget.
 */
bool drainPair(RtmpHandshake &client, RtmpHandshake &server, int maxIters = 32) {
        for (int i = 0; i < maxIters; i++) {
                Buffer txC = client.pendingOutput();
                Buffer txS = server.pendingOutput();
                if (txC.size() > 0) {
                        Buffer storage;
                        server.feed(wrapBytes(storage,
                                              static_cast<const uint8_t *>(txC.data()),
                                              txC.size()));
                }
                if (txS.size() > 0) {
                        Buffer storage;
                        client.feed(wrapBytes(storage,
                                              static_cast<const uint8_t *>(txS.data()),
                                              txS.size()));
                }
                if (client.state() == RtmpHandshake::Done
                    && server.state() == RtmpHandshake::Done) {
                        return true;
                }
                if (client.state() == RtmpHandshake::Failed
                    || server.state() == RtmpHandshake::Failed) {
                        return false;
                }
                if (txC.size() == 0 && txS.size() == 0) {
                        return false;  // stalled — neither side produced bytes
                }
        }
        return false;
}

}  // namespace

// ---------------------------------------------------------------------------
// Simple handshake
// ---------------------------------------------------------------------------

TEST_CASE("RtmpHandshake: simple-mode happy path (client + server)") {
        RtmpHandshake client(RtmpRole::Client);
        RtmpHandshake server(RtmpRole::Server);
        REQUIRE(client.setMode(RtmpHandshakeMode::Simple) == Error::Ok);
        REQUIRE(server.setMode(RtmpHandshakeMode::Simple) == Error::Ok);

        CHECK(drainPair(client, server));
        CHECK(client.state() == RtmpHandshake::Done);
        CHECK(server.state() == RtmpHandshake::Done);
        CHECK(client.negotiatedMode() == RtmpHandshakeMode::Simple);
        CHECK(server.negotiatedMode() == RtmpHandshakeMode::Simple);
}

TEST_CASE("RtmpHandshake: simple-mode local epoch is delivered to peer") {
        RtmpHandshake client(RtmpRole::Client);
        RtmpHandshake server(RtmpRole::Server);
        REQUIRE(client.setMode(RtmpHandshakeMode::Simple) == Error::Ok);
        REQUIRE(server.setMode(RtmpHandshakeMode::Simple) == Error::Ok);
        client.setLocalEpoch(0x12345678u);
        server.setLocalEpoch(0xAABBCCDDu);

        CHECK(drainPair(client, server));
        CHECK(client.peerEpoch() == 0xAABBCCDDu);
        CHECK(server.peerEpoch() == 0x12345678u);
}

TEST_CASE("RtmpHandshake: simple-mode rejects wrong version byte") {
        RtmpHandshake server(RtmpRole::Server);
        REQUIRE(server.setMode(RtmpHandshakeMode::Simple) == Error::Ok);

        uint8_t junk[1 + 1536];
        junk[0] = 0x06;  // RTMPE version — we don't accept it.
        std::memset(junk + 1, 0, 1536);
        Buffer storage;
        Error err = server.feed(wrapBytes(storage, junk, sizeof(junk)));
        CHECK(err == Error::CorruptData);
        CHECK(server.state() == RtmpHandshake::Failed);
}

// ---------------------------------------------------------------------------
// Complex handshake — both schemes
// ---------------------------------------------------------------------------

TEST_CASE("RtmpHandshake: complex-mode happy path (Auto/Complex pair)") {
        RtmpHandshake client(RtmpRole::Client);
        RtmpHandshake server(RtmpRole::Server);
        REQUIRE(client.setMode(RtmpHandshakeMode::Complex) == Error::Ok);
        // Server stays in Auto so it autodetects Complex from the C1.

        CHECK(drainPair(client, server));
        CHECK(client.state() == RtmpHandshake::Done);
        CHECK(server.state() == RtmpHandshake::Done);
        CHECK(client.negotiatedMode() == RtmpHandshakeMode::Complex);
        CHECK(server.negotiatedMode() == RtmpHandshakeMode::Complex);
}

TEST_CASE("RtmpHandshake: complex-mode happy path (both ends explicit)") {
        RtmpHandshake client(RtmpRole::Client);
        RtmpHandshake server(RtmpRole::Server);
        REQUIRE(client.setMode(RtmpHandshakeMode::Complex) == Error::Ok);
        REQUIRE(server.setMode(RtmpHandshakeMode::Complex) == Error::Ok);

        CHECK(drainPair(client, server));
        CHECK(client.state() == RtmpHandshake::Done);
        CHECK(server.state() == RtmpHandshake::Done);
        CHECK(client.negotiatedMode() == RtmpHandshakeMode::Complex);
        CHECK(server.negotiatedMode() == RtmpHandshakeMode::Complex);
}

// ---------------------------------------------------------------------------
// Auto fallback
// ---------------------------------------------------------------------------

TEST_CASE("RtmpHandshake: Auto client falls back to Simple against a Simple-only server") {
        RtmpHandshake client(RtmpRole::Client);  // Auto.
        RtmpHandshake server(RtmpRole::Server);
        REQUIRE(server.setMode(RtmpHandshakeMode::Simple) == Error::Ok);

        CHECK(drainPair(client, server));
        CHECK(client.state() == RtmpHandshake::Done);
        CHECK(server.state() == RtmpHandshake::Done);
        // Even though the client sent a Complex-shaped C1, the server
        // echoed it back without computing a Complex digest, so the
        // client falls back to Simple.
        CHECK(client.negotiatedMode() == RtmpHandshakeMode::Simple);
        CHECK(server.negotiatedMode() == RtmpHandshakeMode::Simple);
}

TEST_CASE("RtmpHandshake: Complex client rejects a Simple-only server") {
        RtmpHandshake client(RtmpRole::Client);
        RtmpHandshake server(RtmpRole::Server);
        REQUIRE(client.setMode(RtmpHandshakeMode::Complex) == Error::Ok);
        REQUIRE(server.setMode(RtmpHandshakeMode::Simple) == Error::Ok);

        // Drain — expect the client to refuse the Simple S1.
        for (int i = 0; i < 32; i++) {
                Buffer txC = client.pendingOutput();
                Buffer txS = server.pendingOutput();
                if (txC.size() > 0) {
                        Buffer storage;
                        server.feed(wrapBytes(storage,
                                              static_cast<const uint8_t *>(txC.data()),
                                              txC.size()));
                }
                if (txS.size() > 0) {
                        Buffer storage;
                        client.feed(wrapBytes(storage,
                                              static_cast<const uint8_t *>(txS.data()),
                                              txS.size()));
                }
                if (client.state() == RtmpHandshake::Failed) break;
                if (txC.size() == 0 && txS.size() == 0) break;
        }
        CHECK(client.state() == RtmpHandshake::Failed);
        CHECK(client.lastError() == Error::CorruptData);
}

// ---------------------------------------------------------------------------
// Failure paths
// ---------------------------------------------------------------------------

TEST_CASE("RtmpHandshake: peer disconnect mid-handshake transitions to Failed") {
        RtmpHandshake client(RtmpRole::Client);
        REQUIRE(client.setMode(RtmpHandshakeMode::Simple) == Error::Ok);

        // Drain C0+C1 but never feed S0+S1.
        Buffer out = client.pendingOutput();
        CHECK(out.size() == 1 + 1536);
        CHECK(client.state() == RtmpHandshake::ExchangingC0C1);

        client.markPeerClosed();
        CHECK(client.state() == RtmpHandshake::Failed);
        CHECK(client.lastError() == Error::Cancelled);
}

TEST_CASE("RtmpHandshake: feed on a Failed instance returns Cancelled") {
        RtmpHandshake client(RtmpRole::Client);
        REQUIRE(client.setMode(RtmpHandshakeMode::Simple) == Error::Ok);
        (void)client.pendingOutput();
        client.markPeerClosed();
        REQUIRE(client.state() == RtmpHandshake::Failed);

        uint8_t junk[8] = {};
        Buffer storage;
        CHECK(client.feed(wrapBytes(storage, junk, sizeof(junk))) == Error::Cancelled);
}

// ---------------------------------------------------------------------------
// Forged-digest detection
// ---------------------------------------------------------------------------

TEST_CASE("RtmpHandshake: Complex client rejects S1 with corrupted digest") {
        RtmpHandshake client(RtmpRole::Client);
        REQUIRE(client.setMode(RtmpHandshakeMode::Complex) == Error::Ok);

        // Drain the client's C0+C1.
        Buffer c0c1 = client.pendingOutput();
        REQUIRE(c0c1.size() == 1 + 1536);

        // Construct an "S1" with garbage random data — no valid digest
        // will be present at any scheme offset.  The client must refuse.
        uint8_t s0s1[1 + 1536];
        s0s1[0] = 0x03;
        s0s1[1] = 0x00;
        s0s1[2] = 0x00;
        s0s1[3] = 0x00;
        s0s1[4] = 0x00;
        // Non-zero "version" so the validator treats it as Complex.
        s0s1[5] = 0x08;
        s0s1[6] = 0x00;
        s0s1[7] = 0x00;
        s0s1[8] = 0x02;
        for (size_t i = 9; i < sizeof(s0s1); i++) {
                s0s1[i] = static_cast<uint8_t>(i & 0xff);
        }

        Buffer storage;
        Error  err = client.feed(wrapBytes(storage, s0s1, sizeof(s0s1)));
        CHECK(err == Error::CorruptData);
        CHECK(client.state() == RtmpHandshake::Failed);
}

// ---------------------------------------------------------------------------
// Drip-feeding bytes (parser must reassemble across calls)
// ---------------------------------------------------------------------------

TEST_CASE("RtmpHandshake: simple-mode reassembles across fragmented feeds") {
        RtmpHandshake client(RtmpRole::Client);
        RtmpHandshake server(RtmpRole::Server);
        REQUIRE(client.setMode(RtmpHandshakeMode::Simple) == Error::Ok);
        REQUIRE(server.setMode(RtmpHandshakeMode::Simple) == Error::Ok);

        // Client emits C0+C1.
        Buffer c0c1 = client.pendingOutput();
        REQUIRE(c0c1.size() == 1 + 1536);

        // Drip-feed the server one byte at a time.
        auto *bytes = static_cast<const uint8_t *>(c0c1.data());
        for (size_t i = 0; i < c0c1.size(); i++) {
                Buffer storage;
                CHECK(server.feed(wrapBytes(storage, bytes + i, 1)) == Error::Ok);
        }
        CHECK(server.state() == RtmpHandshake::ExchangingC2S2);

        // Server now drains S0+S1+S2.
        Buffer s0s1s2 = server.pendingOutput();
        REQUIRE(s0s1s2.size() == 1 + 1536 + 1536);

        // Feed client S0+S1 + S2 in three jagged chunks.  The state
        // machine advances through StepRecvS0S1 → StepSendC2 →
        // StepRecvS2 as bytes arrive, so by the time the final chunk
        // lands the client is in Done — the C2 it owes the server is
        // queued in pendingOutput() awaiting drain.
        auto *sb = static_cast<const uint8_t *>(s0s1s2.data());
        size_t splits[3] = { 800, 1700, s0s1s2.size() };
        size_t prev = 0;
        for (size_t s : splits) {
                Buffer storage;
                CHECK(client.feed(wrapBytes(storage, sb + prev, s - prev)) == Error::Ok);
                prev = s;
        }
        CHECK(client.state() == RtmpHandshake::Done);

        // Drain C2 and deliver it to the server.
        Buffer c2 = client.pendingOutput();
        REQUIRE(c2.size() == 1536);

        Buffer storage;
        CHECK(server.feed(wrapBytes(storage,
                                    static_cast<const uint8_t *>(c2.data()),
                                    c2.size()))
              == Error::Ok);
        CHECK(server.state() == RtmpHandshake::Done);
}

// ---------------------------------------------------------------------------
// Override APIs (deterministic test fixtures)
// ---------------------------------------------------------------------------

TEST_CASE("RtmpHandshake: setLocalNonce rejects wrong-size buffers") {
        RtmpHandshake client(RtmpRole::Client);
        REQUIRE(client.setMode(RtmpHandshakeMode::Simple) == Error::Ok);

        uint8_t shortNonce[16] = {};
        Buffer  storage;
        CHECK(client.setLocalNonce(wrapBytes(storage, shortNonce, sizeof(shortNonce)))
              == Error::InvalidArgument);
}

TEST_CASE("RtmpHandshake: setLocalNonce is deterministic across the wire") {
        RtmpHandshake client(RtmpRole::Client);
        RtmpHandshake server(RtmpRole::Server);
        REQUIRE(client.setMode(RtmpHandshakeMode::Simple) == Error::Ok);
        REQUIRE(server.setMode(RtmpHandshakeMode::Simple) == Error::Ok);

        uint8_t nonce[1528];
        for (size_t i = 0; i < sizeof(nonce); i++) {
                nonce[i] = static_cast<uint8_t>(i * 7 + 13);
        }
        Buffer cn, sn;
        CHECK(client.setLocalNonce(wrapBytes(cn, nonce, sizeof(nonce))) == Error::Ok);
        CHECK(server.setLocalNonce(wrapBytes(sn, nonce, sizeof(nonce))) == Error::Ok);

        CHECK(drainPair(client, server));
        CHECK(client.state() == RtmpHandshake::Done);
        CHECK(server.state() == RtmpHandshake::Done);
}

TEST_CASE("RtmpHandshake: setMode after first emission returns Error::Invalid") {
        RtmpHandshake client(RtmpRole::Client);
        REQUIRE(client.setMode(RtmpHandshakeMode::Simple) == Error::Ok);
        (void)client.pendingOutput();
        CHECK(client.setMode(RtmpHandshakeMode::Complex) == Error::Invalid);
}
