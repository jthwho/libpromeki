/**
 * @file      networkaddress.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/networkaddress.h>
#include <promeki/textstream.h>

using namespace promeki;

TEST_CASE("NetworkAddress") {

        SUBCASE("default construction is null") {
                NetworkAddress addr;
                CHECK(addr.isNull());
                CHECK(addr.type() == NetworkAddress::None);
                CHECK_FALSE(addr.isResolved());
                CHECK(addr.toString().isEmpty());
        }

        SUBCASE("from Ipv4Address") {
                NetworkAddress addr(Ipv4Address(192, 168, 1, 1));
                CHECK(addr.isIPv4());
                CHECK(addr.isResolved());
                CHECK(addr.type() == NetworkAddress::IPv4);
                CHECK(addr.toIpv4() == Ipv4Address(192, 168, 1, 1));
                CHECK(addr.toString() == "192.168.1.1");
        }

        SUBCASE("from Ipv6Address") {
                NetworkAddress addr(Ipv6Address::loopback());
                CHECK(addr.isIPv6());
                CHECK(addr.isResolved());
                CHECK(addr.type() == NetworkAddress::IPv6);
                CHECK(addr.toIpv6() == Ipv6Address::loopback());
        }

        SUBCASE("from hostname") {
                NetworkAddress addr(String("example.com"));
                CHECK(addr.isHostname());
                CHECK_FALSE(addr.isResolved());
                CHECK(addr.type() == NetworkAddress::Hostname);
                CHECK(addr.hostname() == "example.com");
                CHECK(addr.toString() == "example.com");
        }

        SUBCASE("toIpv4 on non-IPv4 returns null") {
                NetworkAddress addr(Ipv6Address::loopback());
                CHECK(addr.toIpv4().isNull());
        }

        SUBCASE("toIpv6 on non-IPv6 returns null") {
                NetworkAddress addr(Ipv4Address::loopback());
                CHECK(addr.toIpv6().isNull());
        }

        SUBCASE("hostname on non-hostname returns empty") {
                NetworkAddress addr(Ipv4Address::loopback());
                CHECK(addr.hostname().isEmpty());
        }

        SUBCASE("fromString IPv4") {
                auto [addr, err] = NetworkAddress::fromString("10.0.0.1");
                CHECK(err.isOk());
                CHECK(addr.isIPv4());
                CHECK(addr.toIpv4() == Ipv4Address(10, 0, 0, 1));
        }

        SUBCASE("fromString IPv6") {
                auto [addr, err] = NetworkAddress::fromString("::1");
                CHECK(err.isOk());
                CHECK(addr.isIPv6());
                CHECK(addr.toIpv6().isLoopback());
        }

        SUBCASE("fromString IPv6 bracketed") {
                auto [addr, err] = NetworkAddress::fromString("[fe80::1]");
                CHECK(err.isOk());
                CHECK(addr.isIPv6());
                CHECK(addr.toIpv6().isLinkLocal());
        }

        SUBCASE("fromString hostname") {
                auto [addr, err] = NetworkAddress::fromString("ndi-source.local");
                CHECK(err.isOk());
                CHECK(addr.isHostname());
                CHECK(addr.hostname() == "ndi-source.local");
        }

        SUBCASE("fromString empty is error") {
                auto [addr, err] = NetworkAddress::fromString("");
                CHECK(err.isError());
        }

        SUBCASE("isLoopback") {
                CHECK(NetworkAddress(Ipv4Address::loopback()).isLoopback());
                CHECK(NetworkAddress(Ipv6Address::loopback()).isLoopback());
                CHECK(NetworkAddress(String("localhost")).isLoopback());
                CHECK_FALSE(NetworkAddress(Ipv4Address(8, 8, 8, 8)).isLoopback());
                CHECK_FALSE(NetworkAddress(String("example.com")).isLoopback());
        }

        SUBCASE("isMulticast") {
                CHECK(NetworkAddress(Ipv4Address(224, 0, 0, 1)).isMulticast());
                auto [mc6, err] = Ipv6Address::fromString("ff02::1");
                CHECK(NetworkAddress(mc6).isMulticast());
                CHECK_FALSE(NetworkAddress(Ipv4Address::loopback()).isMulticast());
                CHECK_FALSE(NetworkAddress(String("example.com")).isMulticast());
        }

        SUBCASE("isLinkLocal") {
                CHECK(NetworkAddress(Ipv4Address(169, 254, 1, 1)).isLinkLocal());
                auto [ll6, err] = Ipv6Address::fromString("fe80::1");
                CHECK(NetworkAddress(ll6).isLinkLocal());
                CHECK_FALSE(NetworkAddress(Ipv4Address::loopback()).isLinkLocal());
        }

        SUBCASE("equality") {
                NetworkAddress a(Ipv4Address(1, 2, 3, 4));
                NetworkAddress b(Ipv4Address(1, 2, 3, 4));
                NetworkAddress c(Ipv4Address(5, 6, 7, 8));
                NetworkAddress d(String("example.com"));
                CHECK(a == b);
                CHECK(a != c);
                CHECK(a != d);
        }

        SUBCASE("TextStream operator<< IPv4") {
                NetworkAddress addr(Ipv4Address(192, 168, 1, 1));
                String str;
                {
                        TextStream ts(&str);
                        ts << addr;
                }
                CHECK(str == "192.168.1.1");
        }

        SUBCASE("TextStream operator<< hostname") {
                NetworkAddress addr(String("example.com"));
                String str;
                {
                        TextStream ts(&str);
                        ts << addr;
                }
                CHECK(str == "example.com");
        }

#if !defined(PROMEKI_PLATFORM_EMSCRIPTEN)
        SUBCASE("toSockAddr IPv4") {
                NetworkAddress addr(Ipv4Address(127, 0, 0, 1));
                struct sockaddr_storage storage;
                size_t len = addr.toSockAddr(&storage);
                CHECK(len == sizeof(struct sockaddr_in));
                auto *sa4 = reinterpret_cast<struct sockaddr_in *>(&storage);
                CHECK(sa4->sin_family == AF_INET);
                CHECK(sa4->sin_port == 0);
        }

        SUBCASE("toSockAddr IPv6") {
                NetworkAddress addr(Ipv6Address::loopback());
                struct sockaddr_storage storage;
                size_t len = addr.toSockAddr(&storage);
                CHECK(len == sizeof(struct sockaddr_in6));
                auto *sa6 = reinterpret_cast<struct sockaddr_in6 *>(&storage);
                CHECK(sa6->sin6_family == AF_INET6);
        }

        SUBCASE("toSockAddr null returns 0") {
                NetworkAddress addr;
                struct sockaddr_storage storage;
                CHECK(addr.toSockAddr(&storage) == 0);
        }

        SUBCASE("toSockAddr hostname returns 0") {
                NetworkAddress addr(String("example.com"));
                struct sockaddr_storage storage;
                CHECK(addr.toSockAddr(&storage) == 0);
        }

        SUBCASE("fromSockAddr IPv4 round-trip") {
                NetworkAddress orig(Ipv4Address(192, 168, 1, 100));
                struct sockaddr_storage storage;
                size_t len = orig.toSockAddr(&storage);
                REQUIRE(len > 0);
                auto [restored, err] = NetworkAddress::fromSockAddr(
                        reinterpret_cast<struct sockaddr *>(&storage), len);
                CHECK(err.isOk());
                CHECK(restored == orig);
        }

        SUBCASE("fromSockAddr IPv6 round-trip") {
                NetworkAddress orig(Ipv6Address::loopback());
                struct sockaddr_storage storage;
                size_t len = orig.toSockAddr(&storage);
                REQUIRE(len > 0);
                auto [restored, err] = NetworkAddress::fromSockAddr(
                        reinterpret_cast<struct sockaddr *>(&storage), len);
                CHECK(err.isOk());
                CHECK(restored == orig);
        }

        SUBCASE("fromSockAddr null") {
                auto [addr, err] = NetworkAddress::fromSockAddr(nullptr, 0);
                CHECK(err.isError());
        }
#endif
}
