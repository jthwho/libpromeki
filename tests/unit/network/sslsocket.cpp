/**
 * @file      sslsocket.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/sslsocket.h>

using namespace promeki;

TEST_CASE("SslSocket - default state") {
        SslSocket sock;
        CHECK_FALSE(sock.isEncrypted());
        CHECK(sock.peerCertificateSubject().isEmpty());
}

TEST_CASE("SslSocket - startEncryption without TCP connection fails") {
        // SslContext is now a value-type handle that lazy-allocates
        // its mbedTLS state on first use, so "no context attached"
        // is no longer a distinct error path — handshake initiation
        // still fails (the underlying TcpSocket has no fd), but the
        // failure surfaces as a transport-level error rather than
        // Error::Invalid.
        SslSocket sock;
        Error     e = sock.startEncryption("example.com");
        CHECK(e.isError());
        CHECK_FALSE(sock.isEncrypted());
}

TEST_CASE("SslSocket - startServerEncryption without certificate fails") {
        // A default-constructed SslContext has no server cert, so
        // server-side startup short-circuits with Error::Invalid
        // before we ever reach the handshake — same outcome as the
        // pre-refactor "no context attached" path.
        SslSocket sock;
        Error     e = sock.startServerEncryption();
        CHECK(e == Error::Invalid);
        CHECK_FALSE(sock.isEncrypted());
}

TEST_CASE("SslSocket - setSslContext attaches the context") {
        // Any SslContext is shareable; the socket stores a refcounted
        // handle to the same impl as the caller's instance.
        SslSocket  sock;
        SslContext ctx;
        sock.setSslContext(ctx);
        CHECK(sock.sslContext() == ctx);
}
