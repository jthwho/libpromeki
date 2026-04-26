/**
 * @file      prioritysocket.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/prioritysocket.h>
#include <cstring>

using namespace promeki;

TEST_CASE("PrioritySocket") {

        SUBCASE("construction") {
                PrioritySocket sock;
                CHECK(sock.socketType() == AbstractSocket::UdpSocketType);
                CHECK_FALSE(sock.isOpen());
                CHECK(sock.priority() == PrioritySocket::BestEffort);
        }

        SUBCASE("setPriority Video") {
                PrioritySocket sock;
                sock.open(IODevice::ReadWrite);
                Error err = sock.setPriority(PrioritySocket::Video);
                CHECK(err.isOk());
                CHECK(sock.priority() == PrioritySocket::Video);
        }

        SUBCASE("setPriority Voice") {
                PrioritySocket sock;
                sock.open(IODevice::ReadWrite);
                Error err = sock.setPriority(PrioritySocket::Voice);
                CHECK(err.isOk());
                CHECK(sock.priority() == PrioritySocket::Voice);
        }

        SUBCASE("setPriority NetworkControl") {
                PrioritySocket sock;
                sock.open(IODevice::ReadWrite);
                Error err = sock.setPriority(PrioritySocket::NetworkControl);
                CHECK(err.isOk());
                CHECK(sock.priority() == PrioritySocket::NetworkControl);
        }

        SUBCASE("setPriority BestEffort") {
                PrioritySocket sock;
                sock.open(IODevice::ReadWrite);
                // Set to something else first
                sock.setPriority(PrioritySocket::Video);
                // Then back to BestEffort
                Error err = sock.setPriority(PrioritySocket::BestEffort);
                CHECK(err.isOk());
                CHECK(sock.priority() == PrioritySocket::BestEffort);
        }

        SUBCASE("setPriority Background") {
                PrioritySocket sock;
                sock.open(IODevice::ReadWrite);
                Error err = sock.setPriority(PrioritySocket::Background);
                CHECK(err.isOk());
                CHECK(sock.priority() == PrioritySocket::Background);
        }

        SUBCASE("setPriority on closed socket fails") {
                PrioritySocket sock;
                Error          err = sock.setPriority(PrioritySocket::Video);
                CHECK(err.isError());
                CHECK(sock.priority() == PrioritySocket::BestEffort);
        }

        SUBCASE("inherits UdpSocket functionality") {
                PrioritySocket sender;
                PrioritySocket receiver;

                sender.open(IODevice::ReadWrite);
                receiver.open(IODevice::ReadWrite);
                receiver.setReceiveTimeout(2000);

                sender.setPriority(PrioritySocket::Video);

                Error err = receiver.bind(SocketAddress::any(0));
                REQUIRE(err.isOk());
                uint16_t port = receiver.localAddress().port();

                const char   *msg = "priority test";
                SocketAddress dest(Ipv4Address::loopback(), port);
                int64_t       sent = sender.writeDatagram(msg, std::strlen(msg), dest);
                CHECK(sent == static_cast<int64_t>(std::strlen(msg)));

                char    buf[256];
                int64_t received = receiver.readDatagram(buf, sizeof(buf));
                REQUIRE(received > 0);
                CHECK(std::memcmp(buf, msg, received) == 0);
        }

        SUBCASE("direct DSCP access still works") {
                PrioritySocket sock;
                sock.open(IODevice::ReadWrite);
                // Use inherited setDscp directly
                Error err = sock.setDscp(0x22); // AF41 = 34
                CHECK(err.isOk());
                // Priority tracking won't update from direct DSCP calls
                // (tracked separately)
        }
}
