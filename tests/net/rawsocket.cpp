/**
 * @file      tests/net/rawsocket.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/network/rawsocket.h>

using namespace promeki;

TEST_CASE("RawSocket") {

        SUBCASE("construction") {
                RawSocket sock;
                CHECK(sock.socketType() == AbstractSocket::RawSocketType);
                CHECK_FALSE(sock.isOpen());
                CHECK(sock.isSequential());
        }

        SUBCASE("interface and protocol configuration") {
                RawSocket sock;
                sock.setInterface("eth0");
                CHECK(sock.interface() == "eth0");
                sock.setProtocol(0x0800);
                CHECK(sock.protocol() == 0x0800);
        }

        SUBCASE("open without privileges") {
                // Raw sockets typically require root/CAP_NET_RAW.
                // In a normal test environment, open() should fail
                // with PermissionDenied (unless running as root).
                RawSocket sock;
                sock.setProtocol(0x0800);
                Error err = sock.open(IODevice::ReadWrite);
                // Either it succeeds (root) or fails with PermissionDenied
                if(err.isError()) {
                        CHECK(err == Error::PermissionDenied);
                } else {
                        CHECK(sock.isOpen());
                        sock.close();
                }
        }

        SUBCASE("read and write on closed socket") {
                RawSocket sock;
                char buf[16];
                CHECK(sock.read(buf, sizeof(buf)) == -1);
                CHECK(sock.write("test", 4) == -1);
        }

        SUBCASE("setPromiscuous on closed socket fails") {
                RawSocket sock;
                sock.setInterface("lo");
                CHECK(sock.setPromiscuous(true).isError());
        }

        SUBCASE("setPromiscuous without interface fails") {
                RawSocket sock;
                // Even if we could open, no interface is set
                // Test the path where _interface.isEmpty()
                // Since we can't open without root, just verify
                // the closed-socket path returns error
                CHECK(sock.setPromiscuous(true).isError());
        }

        SUBCASE("close on already closed socket is safe") {
                RawSocket sock;
                Error err = sock.close();
                CHECK(err.isOk());
        }

        SUBCASE("default protocol is 0") {
                RawSocket sock;
                CHECK(sock.protocol() == 0);
        }

        SUBCASE("default interface is empty") {
                RawSocket sock;
                CHECK(sock.interface().isEmpty());
        }
}
