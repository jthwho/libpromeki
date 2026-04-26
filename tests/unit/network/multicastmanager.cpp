/**
 * @file      multicastmanager.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/multicastmanager.h>
#include <promeki/udpsocket.h>
#include <cstring>

using namespace promeki;

TEST_CASE("MulticastManager") {

        SUBCASE("construction") {
                MulticastManager mgr;
                CHECK(mgr.activeGroups().isEmpty());
                CHECK(mgr.defaultInterface().isEmpty());
        }

        SUBCASE("set default interface") {
                MulticastManager mgr;
                mgr.setDefaultInterface("eth0");
                CHECK(mgr.defaultInterface() == "eth0");
        }

        SUBCASE("join and leave group") {
                MulticastManager mgr;
                UdpSocket        sock;
                sock.open(IODevice::ReadWrite);
                sock.setReuseAddress(true);
                sock.bind(SocketAddress::any(0));

                SocketAddress group(Ipv4Address(239, 255, 0, 1), sock.localAddress().port());
                Error         err = mgr.joinGroup(group, &sock);
                CHECK(err.isOk());
                CHECK(mgr.isMemberOf(group));
                CHECK(mgr.activeGroups().size() == 1);

                err = mgr.leaveGroup(group, &sock);
                CHECK(err.isOk());
                CHECK_FALSE(mgr.isMemberOf(group));
                CHECK(mgr.activeGroups().isEmpty());
        }

        SUBCASE("join multiple groups") {
                MulticastManager mgr;
                UdpSocket        sock;
                sock.open(IODevice::ReadWrite);
                sock.setReuseAddress(true);
                sock.bind(SocketAddress::any(0));
                uint16_t port = sock.localAddress().port();

                SocketAddress group1(Ipv4Address(239, 255, 0, 1), port);
                SocketAddress group2(Ipv4Address(239, 255, 0, 2), port);

                mgr.joinGroup(group1, &sock);
                mgr.joinGroup(group2, &sock);

                CHECK(mgr.activeGroups().size() == 2);
                CHECK(mgr.isMemberOf(group1));
                CHECK(mgr.isMemberOf(group2));

                mgr.leaveAllGroups();
                CHECK(mgr.activeGroups().isEmpty());
        }

        SUBCASE("leaveAllGroups") {
                MulticastManager mgr;
                UdpSocket        sock;
                sock.open(IODevice::ReadWrite);
                sock.setReuseAddress(true);
                sock.bind(SocketAddress::any(0));
                uint16_t port = sock.localAddress().port();

                SocketAddress group1(Ipv4Address(239, 255, 0, 10), port);
                SocketAddress group2(Ipv4Address(239, 255, 0, 11), port);

                mgr.joinGroup(group1, &sock);
                mgr.joinGroup(group2, &sock);
                CHECK(mgr.activeGroups().size() == 2);

                mgr.leaveAllGroups();
                CHECK(mgr.activeGroups().isEmpty());
                CHECK_FALSE(mgr.isMemberOf(group1));
                CHECK_FALSE(mgr.isMemberOf(group2));
        }

        SUBCASE("isMemberOf false for non-member") {
                MulticastManager mgr;
                SocketAddress    group(Ipv4Address(239, 255, 0, 50), 5004);
                CHECK_FALSE(mgr.isMemberOf(group));
        }

        SUBCASE("groupJoined signal") {
                MulticastManager mgr;
                UdpSocket        sock;
                sock.open(IODevice::ReadWrite);
                sock.setReuseAddress(true);
                sock.bind(SocketAddress::any(0));

                bool          signalFired = false;
                SocketAddress receivedGroup;
                mgr.groupJoinedSignal.connect([&](const SocketAddress &g) {
                        signalFired = true;
                        receivedGroup = g;
                });

                SocketAddress group(Ipv4Address(239, 255, 0, 20), sock.localAddress().port());
                mgr.joinGroup(group, &sock);
                CHECK(signalFired);
                CHECK(receivedGroup == group);

                mgr.leaveAllGroups();
        }

        SUBCASE("groupLeft signal") {
                MulticastManager mgr;
                UdpSocket        sock;
                sock.open(IODevice::ReadWrite);
                sock.setReuseAddress(true);
                sock.bind(SocketAddress::any(0));

                SocketAddress group(Ipv4Address(239, 255, 0, 21), sock.localAddress().port());
                mgr.joinGroup(group, &sock);

                bool signalFired = false;
                mgr.groupLeftSignal.connect([&](const SocketAddress &g) { signalFired = true; });

                mgr.leaveGroup(group, &sock);
                CHECK(signalFired);
        }

        SUBCASE("multicast data flow through managed group") {
                MulticastManager mgr;
                UdpSocket        sender;
                UdpSocket        receiver;

                sender.open(IODevice::ReadWrite);
                sender.setMulticastLoopback(true);

                receiver.open(IODevice::ReadWrite);
                receiver.setReuseAddress(true);
                receiver.setReceiveTimeout(2000);
                receiver.bind(SocketAddress::any(0));
                uint16_t port = receiver.localAddress().port();

                SocketAddress group(Ipv4Address(239, 255, 0, 30), port);
                Error         err = mgr.joinGroup(group, &receiver);
                REQUIRE(err.isOk());

                const char *msg = "managed multicast";
                sender.writeDatagram(msg, std::strlen(msg), SocketAddress(Ipv4Address(239, 255, 0, 30), port));

                char    buf[256];
                int64_t n = receiver.readDatagram(buf, sizeof(buf));
                CHECK(n > 0);
                if (n > 0) {
                        CHECK(std::memcmp(buf, msg, n) == 0);
                }

                mgr.leaveAllGroups();
        }

        SUBCASE("destructor leaves all groups") {
                UdpSocket sock;
                sock.open(IODevice::ReadWrite);
                sock.setReuseAddress(true);
                sock.bind(SocketAddress::any(0));
                uint16_t port = sock.localAddress().port();

                {
                        MulticastManager mgr;
                        SocketAddress    group(Ipv4Address(239, 255, 0, 40), port);
                        mgr.joinGroup(group, &sock);
                        CHECK(mgr.isMemberOf(group));
                        // Destructor should call leaveAllGroups
                }
                // Socket should still be usable after manager is destroyed
                CHECK(sock.isOpen());
        }
}
