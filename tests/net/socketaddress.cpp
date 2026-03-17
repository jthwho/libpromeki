/**
 * @file      tests/net/socketaddress.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/network/socketaddress.h>
#include <promeki/core/textstream.h>

using namespace promeki;

TEST_CASE("SocketAddress") {

        SUBCASE("default construction is null") {
                SocketAddress addr;
                CHECK(addr.isNull());
                CHECK(addr.port() == 0);
                CHECK(addr.address().isNull());
        }

        SUBCASE("from IPv4 and port") {
                SocketAddress addr(Ipv4Address(192, 168, 1, 1), 5004);
                CHECK_FALSE(addr.isNull());
                CHECK(addr.isIPv4());
                CHECK(addr.port() == 5004);
                CHECK(addr.address().toIpv4() == Ipv4Address(192, 168, 1, 1));
        }

        SUBCASE("from IPv6 and port") {
                SocketAddress addr(Ipv6Address::loopback(), 8080);
                CHECK(addr.isIPv6());
                CHECK(addr.port() == 8080);
        }

        SUBCASE("from NetworkAddress and port") {
                SocketAddress addr(NetworkAddress(String("example.com")), 443);
                CHECK_FALSE(addr.isNull());
                CHECK(addr.port() == 443);
                CHECK(addr.address().isHostname());
        }

        SUBCASE("setAddress and setPort") {
                SocketAddress addr;
                addr.setAddress(NetworkAddress(Ipv4Address(10, 0, 0, 1)));
                addr.setPort(9000);
                CHECK(addr.isIPv4());
                CHECK(addr.port() == 9000);
        }

        SUBCASE("any") {
                SocketAddress addr = SocketAddress::any(5004);
                CHECK(addr.isIPv4());
                CHECK(addr.port() == 5004);
                CHECK(addr.address().toIpv4() == Ipv4Address::any());
        }

        SUBCASE("localhost") {
                SocketAddress addr = SocketAddress::localhost(8080);
                CHECK(addr.isLoopback());
                CHECK(addr.port() == 8080);
        }

        SUBCASE("isMulticast") {
                SocketAddress addr(Ipv4Address(239, 0, 0, 1), 5004);
                CHECK(addr.isMulticast());
                SocketAddress addr2(Ipv4Address(10, 0, 0, 1), 5004);
                CHECK_FALSE(addr2.isMulticast());
        }

        SUBCASE("fromString IPv4:port") {
                auto [addr, err] = SocketAddress::fromString("192.168.1.1:5004");
                CHECK(err.isOk());
                CHECK(addr.isIPv4());
                CHECK(addr.address().toIpv4() == Ipv4Address(192, 168, 1, 1));
                CHECK(addr.port() == 5004);
        }

        SUBCASE("fromString IPv6 bracketed:port") {
                auto [addr, err] = SocketAddress::fromString("[::1]:8080");
                CHECK(err.isOk());
                CHECK(addr.isIPv6());
                CHECK(addr.address().toIpv6().isLoopback());
                CHECK(addr.port() == 8080);
        }

        SUBCASE("fromString hostname:port") {
                auto [addr, err] = SocketAddress::fromString("example.com:443");
                CHECK(err.isOk());
                CHECK(addr.address().isHostname());
                CHECK(addr.address().hostname() == "example.com");
                CHECK(addr.port() == 443);
        }

        SUBCASE("fromString empty is error") {
                auto [addr, err] = SocketAddress::fromString("");
                CHECK(err.isError());
        }

        SUBCASE("fromString missing port is error") {
                auto [addr, err] = SocketAddress::fromString("192.168.1.1");
                CHECK(err.isError());
        }

        SUBCASE("fromString invalid port is error") {
                auto [addr, err] = SocketAddress::fromString("192.168.1.1:99999");
                CHECK(err.isError());
        }

        SUBCASE("fromString non-numeric port is error") {
                auto [addr, err] = SocketAddress::fromString("192.168.1.1:abc");
                CHECK(err.isError());
        }

        SUBCASE("fromString IPv6 missing bracket is error") {
                auto [addr, err] = SocketAddress::fromString("[::1:8080");
                CHECK(err.isError());
        }

        SUBCASE("toString IPv4") {
                SocketAddress addr(Ipv4Address(10, 0, 0, 1), 5004);
                CHECK(addr.toString() == "10.0.0.1:5004");
        }

        SUBCASE("toString IPv6") {
                SocketAddress addr(Ipv6Address::loopback(), 8080);
                CHECK(addr.toString() == "[::1]:8080");
        }

        SUBCASE("toString null is empty") {
                SocketAddress addr;
                CHECK(addr.toString().isEmpty());
        }

        SUBCASE("fromString round-trip IPv4") {
                SocketAddress orig(Ipv4Address(172, 16, 0, 1), 1234);
                auto [parsed, err] = SocketAddress::fromString(orig.toString());
                CHECK(err.isOk());
                CHECK(parsed == orig);
        }

        SUBCASE("fromString round-trip IPv6") {
                SocketAddress orig(Ipv6Address::loopback(), 5678);
                auto [parsed, err] = SocketAddress::fromString(orig.toString());
                CHECK(err.isOk());
                CHECK(parsed == orig);
        }

        SUBCASE("equality") {
                SocketAddress a(Ipv4Address(1, 2, 3, 4), 100);
                SocketAddress b(Ipv4Address(1, 2, 3, 4), 100);
                SocketAddress c(Ipv4Address(1, 2, 3, 4), 200);
                SocketAddress d(Ipv4Address(5, 6, 7, 8), 100);
                CHECK(a == b);
                CHECK(a != c);
                CHECK(a != d);
        }

        SUBCASE("port 0") {
                auto [addr, err] = SocketAddress::fromString("127.0.0.1:0");
                CHECK(err.isOk());
                CHECK(addr.port() == 0);
        }

        SUBCASE("port 65535") {
                auto [addr, err] = SocketAddress::fromString("127.0.0.1:65535");
                CHECK(err.isOk());
                CHECK(addr.port() == 65535);
        }

        SUBCASE("TextStream operator<<") {
                SocketAddress addr(Ipv4Address(192, 168, 1, 1), 5004);
                String str;
                {
                        TextStream ts(&str);
                        ts << addr;
                }
                CHECK(str == "192.168.1.1:5004");
        }

#if !defined(PROMEKI_PLATFORM_EMSCRIPTEN)
        SUBCASE("toSockAddr IPv4") {
                SocketAddress addr(Ipv4Address(127, 0, 0, 1), 5004);
                struct sockaddr_storage storage;
                size_t len = addr.toSockAddr(&storage);
                CHECK(len == sizeof(struct sockaddr_in));
                auto *sa4 = reinterpret_cast<struct sockaddr_in *>(&storage);
                CHECK(sa4->sin_family == AF_INET);
                CHECK(ntohs(sa4->sin_port) == 5004);
        }

        SUBCASE("toSockAddr IPv6") {
                SocketAddress addr(Ipv6Address::loopback(), 8080);
                struct sockaddr_storage storage;
                size_t len = addr.toSockAddr(&storage);
                CHECK(len == sizeof(struct sockaddr_in6));
                auto *sa6 = reinterpret_cast<struct sockaddr_in6 *>(&storage);
                CHECK(sa6->sin6_family == AF_INET6);
                CHECK(ntohs(sa6->sin6_port) == 8080);
        }

        SUBCASE("toSockAddr null returns 0") {
                SocketAddress addr;
                struct sockaddr_storage storage;
                CHECK(addr.toSockAddr(&storage) == 0);
        }

        SUBCASE("fromSockAddr IPv4 round-trip") {
                SocketAddress orig(Ipv4Address(192, 168, 1, 100), 5004);
                struct sockaddr_storage storage;
                size_t len = orig.toSockAddr(&storage);
                REQUIRE(len > 0);
                auto [restored, err] = SocketAddress::fromSockAddr(
                        reinterpret_cast<struct sockaddr *>(&storage), len);
                CHECK(err.isOk());
                CHECK(restored == orig);
        }

        SUBCASE("fromSockAddr IPv6 round-trip") {
                SocketAddress orig(Ipv6Address::loopback(), 8080);
                struct sockaddr_storage storage;
                size_t len = orig.toSockAddr(&storage);
                REQUIRE(len > 0);
                auto [restored, err] = SocketAddress::fromSockAddr(
                        reinterpret_cast<struct sockaddr *>(&storage), len);
                CHECK(err.isOk());
                CHECK(restored == orig);
        }

        SUBCASE("fromSockAddr null") {
                auto [addr, err] = SocketAddress::fromSockAddr(nullptr, 0);
                CHECK(err.isError());
        }

        SUBCASE("toSockAddr null storage returns 0") {
                SocketAddress addr(Ipv4Address(127, 0, 0, 1), 5004);
                CHECK(addr.toSockAddr(nullptr) == 0);
        }
#endif

        SUBCASE("fromString IPv6 bracket bad separator") {
                auto [addr, err] = SocketAddress::fromString("[::1]x5004");
                CHECK(err.isError());
        }

        SUBCASE("fromString IPv6 bracket no port") {
                auto [addr, err] = SocketAddress::fromString("[::1]");
                CHECK(err.isOk());
                CHECK(addr.isIPv6());
                CHECK(addr.port() == 0);
        }

        SUBCASE("fromString negative port is error") {
                auto [addr, err] = SocketAddress::fromString("127.0.0.1:-1");
                CHECK(err.isError());
        }
}
