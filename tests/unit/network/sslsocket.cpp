/**
 * @file      sslsocket.cpp
 * @copyright Howard Logic. All rights reserved.
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

TEST_CASE("SslSocket - startEncryption without context fails") {
        SslSocket sock;
        Error e = sock.startEncryption("example.com");
        CHECK(e == Error::Invalid);
        CHECK_FALSE(sock.isEncrypted());
}

TEST_CASE("SslSocket - startServerEncryption without context fails") {
        SslSocket sock;
        Error e = sock.startServerEncryption();
        CHECK(e == Error::Invalid);
        CHECK_FALSE(sock.isEncrypted());
}

TEST_CASE("SslSocket - setSslContext attaches the context") {
        SslSocket sock;
        SslContext::Ptr ctx = SslContext::Ptr::takeOwnership(new SslContext());
        sock.setSslContext(ctx);
        CHECK(sock.sslContext().isValid());
}
