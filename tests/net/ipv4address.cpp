/**
 * @file      tests/net/ipv4address.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/network/ipv4address.h>
#include <promeki/network/ipv6address.h>
#include <promeki/network/macaddress.h>
#include <promeki/core/textstream.h>

using namespace promeki;

TEST_CASE("Ipv4Address") {

        SUBCASE("default construction is null") {
                Ipv4Address addr;
                CHECK(addr.isNull());
                CHECK(addr.toUint32() == 0);
        }

        SUBCASE("octet construction") {
                Ipv4Address addr(192, 168, 1, 100);
                CHECK_FALSE(addr.isNull());
                CHECK(addr.octet(0) == 192);
                CHECK(addr.octet(1) == 168);
                CHECK(addr.octet(2) == 1);
                CHECK(addr.octet(3) == 100);
        }

        SUBCASE("octet out of range") {
                Ipv4Address addr(10, 20, 30, 40);
                CHECK(addr.octet(-1) == 0);
                CHECK(addr.octet(4) == 0);
        }

        SUBCASE("fromString valid") {
                auto [addr, err] = Ipv4Address::fromString("10.0.0.1");
                CHECK(err.isOk());
                CHECK(addr.octet(0) == 10);
                CHECK(addr.octet(1) == 0);
                CHECK(addr.octet(2) == 0);
                CHECK(addr.octet(3) == 1);
        }

        SUBCASE("fromString boundary values") {
                auto [addr, err] = Ipv4Address::fromString("0.0.0.0");
                CHECK(err.isOk());
                CHECK(addr.isNull());

                auto [addr2, err2] = Ipv4Address::fromString("255.255.255.255");
                CHECK(err2.isOk());
                CHECK(addr2.isBroadcast());
        }

        SUBCASE("fromString invalid") {
                CHECK(Ipv4Address::fromString("").second().isError());
                CHECK(Ipv4Address::fromString("256.0.0.1").second().isError());
                CHECK(Ipv4Address::fromString("1.2.3").second().isError());
                CHECK(Ipv4Address::fromString("1.2.3.4.5").second().isError());
                CHECK(Ipv4Address::fromString("abc").second().isError());
                CHECK(Ipv4Address::fromString("1..2.3").second().isError());
                CHECK(Ipv4Address::fromString(".1.2.3").second().isError());
        }

        SUBCASE("toString") {
                CHECK(Ipv4Address(192, 168, 1, 1).toString() == "192.168.1.1");
                CHECK(Ipv4Address(0, 0, 0, 0).toString() == "0.0.0.0");
                CHECK(Ipv4Address(255, 255, 255, 255).toString() == "255.255.255.255");
        }

        SUBCASE("fromString round-trip") {
                String str = "172.16.32.64";
                auto [addr, err] = Ipv4Address::fromString(str);
                CHECK(err.isOk());
                CHECK(addr.toString() == str);
        }

        SUBCASE("isLoopback") {
                CHECK(Ipv4Address(127, 0, 0, 1).isLoopback());
                CHECK(Ipv4Address(127, 255, 255, 255).isLoopback());
                CHECK_FALSE(Ipv4Address(128, 0, 0, 1).isLoopback());
        }

        SUBCASE("isMulticast") {
                CHECK(Ipv4Address(224, 0, 0, 1).isMulticast());
                CHECK(Ipv4Address(239, 255, 255, 255).isMulticast());
                CHECK_FALSE(Ipv4Address(223, 255, 255, 255).isMulticast());
                CHECK_FALSE(Ipv4Address(240, 0, 0, 0).isMulticast());
        }

        SUBCASE("isLinkLocal") {
                CHECK(Ipv4Address(169, 254, 0, 1).isLinkLocal());
                CHECK(Ipv4Address(169, 254, 255, 255).isLinkLocal());
                CHECK_FALSE(Ipv4Address(169, 253, 0, 1).isLinkLocal());
        }

        SUBCASE("isPrivate") {
                CHECK(Ipv4Address(10, 0, 0, 1).isPrivate());
                CHECK(Ipv4Address(10, 255, 255, 255).isPrivate());
                CHECK(Ipv4Address(172, 16, 0, 1).isPrivate());
                CHECK(Ipv4Address(172, 31, 255, 255).isPrivate());
                CHECK_FALSE(Ipv4Address(172, 32, 0, 0).isPrivate());
                CHECK(Ipv4Address(192, 168, 0, 1).isPrivate());
                CHECK_FALSE(Ipv4Address(8, 8, 8, 8).isPrivate());
        }

        SUBCASE("isBroadcast") {
                CHECK(Ipv4Address(255, 255, 255, 255).isBroadcast());
                CHECK_FALSE(Ipv4Address(255, 255, 255, 254).isBroadcast());
        }

        SUBCASE("isInSubnet with mask") {
                Ipv4Address addr(192, 168, 1, 100);
                CHECK(addr.isInSubnet(Ipv4Address(192, 168, 1, 0), Ipv4Address(255, 255, 255, 0)));
                CHECK_FALSE(addr.isInSubnet(Ipv4Address(192, 168, 2, 0), Ipv4Address(255, 255, 255, 0)));
        }

        SUBCASE("isInSubnet with prefix length") {
                Ipv4Address addr(192, 168, 1, 100);
                CHECK(addr.isInSubnet(Ipv4Address(192, 168, 0, 0), 16));
                CHECK(addr.isInSubnet(Ipv4Address(192, 168, 1, 0), 24));
                CHECK_FALSE(addr.isInSubnet(Ipv4Address(192, 168, 2, 0), 24));
                CHECK(addr.isInSubnet(Ipv4Address(0, 0, 0, 0), 0));
                CHECK(addr.isInSubnet(addr, 32));
        }

        SUBCASE("static factories") {
                CHECK(Ipv4Address::any().isNull());
                CHECK(Ipv4Address::loopback().isLoopback());
                CHECK(Ipv4Address::loopback().toString() == "127.0.0.1");
                CHECK(Ipv4Address::broadcast().isBroadcast());
        }

        SUBCASE("equality and ordering") {
                Ipv4Address a(1, 2, 3, 4);
                Ipv4Address b(1, 2, 3, 4);
                Ipv4Address c(1, 2, 3, 5);
                CHECK(a == b);
                CHECK(a != c);
                CHECK(a < c);
        }

        SUBCASE("toIpv6Mapped") {
                Ipv4Address v4(192, 168, 1, 1);
                Ipv6Address v6 = v4.toIpv6Mapped();
                CHECK(v6.isV4Mapped());
                Ipv4Address back = v6.toIpv4();
                CHECK(back == v4);
        }

        SUBCASE("uint32 round-trip") {
                Ipv4Address addr(10, 20, 30, 40);
                uint32_t val = addr.toUint32();
                Ipv4Address restored = Ipv4Address::fromUint32(val);
                CHECK(restored == addr);
        }

        SUBCASE("multicastMac") {
                // Valid multicast address
                Ipv4Address mcast(224, 0, 0, 1);
                MacAddress mac = mcast.multicastMac();
                CHECK_FALSE(mac.isNull());
                CHECK(mac == MacAddress(0x01, 0x00, 0x5E, 0x00, 0x00, 0x01));

                // Non-multicast returns null
                Ipv4Address unicast(192, 168, 1, 1);
                CHECK(unicast.multicastMac().isNull());
        }

        SUBCASE("TextStream operator<<") {
                Ipv4Address addr(10, 20, 30, 40);
                String str;
                {
                        TextStream ts(&str);
                        ts << addr;
                }
                CHECK(str == "10.20.30.40");
        }

#if !defined(PROMEKI_PLATFORM_EMSCRIPTEN)
        SUBCASE("toSockAddr") {
                Ipv4Address addr(192, 168, 1, 1);
                struct sockaddr_in sa;
                Error err = addr.toSockAddr(&sa);
                CHECK(err.isOk());
                CHECK(sa.sin_family == AF_INET);
                CHECK(sa.sin_port == 0);
        }

        SUBCASE("toSockAddr null") {
                Ipv4Address addr(10, 0, 0, 1);
                CHECK(addr.toSockAddr(nullptr).isError());
        }

        SUBCASE("fromSockAddr round-trip") {
                Ipv4Address orig(172, 16, 0, 1);
                struct sockaddr_in sa;
                REQUIRE(orig.toSockAddr(&sa).isOk());
                auto [restored, err] = Ipv4Address::fromSockAddr(&sa);
                CHECK(err.isOk());
                CHECK(restored == orig);
        }

        SUBCASE("fromSockAddr null") {
                auto [addr, err] = Ipv4Address::fromSockAddr(nullptr);
                CHECK(err.isError());
        }
#endif
}
