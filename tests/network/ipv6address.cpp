/**
 * @file      ipv6address.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/ipv6address.h>
#include <promeki/ipv4address.h>
#include <promeki/macaddress.h>
#include <promeki/textstream.h>

using namespace promeki;

TEST_CASE("Ipv6Address") {

        SUBCASE("default construction is null") {
                Ipv6Address addr;
                CHECK(addr.isNull());
                CHECK(addr.scopeId() == 0);
        }

        SUBCASE("loopback") {
                Ipv6Address lo = Ipv6Address::loopback();
                CHECK_FALSE(lo.isNull());
                CHECK(lo.isLoopback());
                CHECK(lo.raw()[15] == 1);
                for(int i = 0; i < 15; ++i) CHECK(lo.raw()[i] == 0);
        }

        SUBCASE("any") {
                Ipv6Address any = Ipv6Address::any();
                CHECK(any.isNull());
        }

        SUBCASE("fromString loopback") {
                auto [addr, err] = Ipv6Address::fromString("::1");
                CHECK(err.isOk());
                CHECK(addr.isLoopback());
        }

        SUBCASE("fromString all zeros") {
                auto [addr, err] = Ipv6Address::fromString("::");
                CHECK(err.isOk());
                CHECK(addr.isNull());
        }

        SUBCASE("fromString full address") {
                auto [addr, err] = Ipv6Address::fromString("2001:0db8:85a3:0000:0000:8a2e:0370:7334");
                CHECK(err.isOk());
                CHECK_FALSE(addr.isNull());
                // Check first group: 2001 = 0x20, 0x01
                CHECK(addr.raw()[0] == 0x20);
                CHECK(addr.raw()[1] == 0x01);
        }

        SUBCASE("fromString compressed") {
                auto [addr, err] = Ipv6Address::fromString("fe80::1");
                CHECK(err.isOk());
                CHECK(addr.isLinkLocal());
                CHECK(addr.raw()[0] == 0xFE);
                CHECK(addr.raw()[1] == 0x80);
                CHECK(addr.raw()[15] == 1);
        }

        SUBCASE("fromString with scope") {
                auto [addr, err] = Ipv6Address::fromString("fe80::1%3");
                CHECK(err.isOk());
                CHECK(addr.isLinkLocal());
                CHECK(addr.scopeId() == 3);
        }

        SUBCASE("fromString multicast") {
                auto [addr, err] = Ipv6Address::fromString("ff02::1");
                CHECK(err.isOk());
                CHECK(addr.isMulticast());
        }

        SUBCASE("fromString invalid") {
                CHECK(Ipv6Address::fromString("").second().isError());
                CHECK(Ipv6Address::fromString(":::1").second().isError());
                CHECK(Ipv6Address::fromString("1::2::3").second().isError()); // double :: not allowed
        }

        SUBCASE("fromString IPv4-mapped") {
                auto [addr, err] = Ipv6Address::fromString("::ffff:192.168.1.1");
                CHECK(err.isOk());
                CHECK(addr.isV4Mapped());
                Ipv4Address v4 = addr.toIpv4();
                CHECK(v4.toString() == "192.168.1.1");
        }

        SUBCASE("toString loopback") {
                Ipv6Address lo = Ipv6Address::loopback();
                CHECK(lo.toString() == "::1");
        }

        SUBCASE("toString all zeros") {
                Ipv6Address any;
                CHECK(any.toString() == "::");
        }

        SUBCASE("toString compressed") {
                auto [addr, err] = Ipv6Address::fromString("fe80::1");
                CHECK(err.isOk());
                CHECK(addr.toString() == "fe80::1");
        }

        SUBCASE("toString no compression for single zero group") {
                // 2001:db8:0:1:0:0:0:1 should compress the three zeros
                auto [addr, err] = Ipv6Address::fromString("2001:db8:0:1::1");
                CHECK(err.isOk());
                // The longest run of zeros is after 1, so :: should be there
                String str = addr.toString();
                CHECK(str.contains("::"));
        }

        SUBCASE("fromString round-trip") {
                String inputs[] = {"::1", "::", "fe80::1", "ff02::1"};
                for(const auto &input : inputs) {
                        auto [addr, err] = Ipv6Address::fromString(input);
                        CHECK(err.isOk());
                        auto [addr2, err2] = Ipv6Address::fromString(addr.toString());
                        CHECK(err2.isOk());
                        CHECK(addr == addr2);
                }
        }

        SUBCASE("isV4Mapped") {
                auto [addr, err] = Ipv6Address::fromString("::ffff:10.0.0.1");
                CHECK(err.isOk());
                CHECK(addr.isV4Mapped());
                CHECK_FALSE(addr.isLoopback());
        }

        SUBCASE("toIpv4 non-mapped returns null") {
                Ipv6Address lo = Ipv6Address::loopback();
                Ipv4Address v4 = lo.toIpv4();
                CHECK(v4.isNull());
        }

        SUBCASE("classification") {
                CHECK(Ipv6Address::loopback().isLoopback());
                CHECK_FALSE(Ipv6Address::loopback().isMulticast());
                CHECK_FALSE(Ipv6Address::loopback().isLinkLocal());

                auto [ll, err] = Ipv6Address::fromString("fe80::1");
                CHECK(ll.isLinkLocal());
                CHECK_FALSE(ll.isMulticast());

                auto [mc, err2] = Ipv6Address::fromString("ff02::1");
                CHECK(mc.isMulticast());
                CHECK_FALSE(mc.isLinkLocal());

                auto [sl, err3] = Ipv6Address::fromString("fec0::1");
                CHECK(sl.isSiteLocal());
        }

        SUBCASE("scope ID") {
                Ipv6Address addr = Ipv6Address::loopback();
                CHECK(addr.scopeId() == 0);
                addr.setScopeId(42);
                CHECK(addr.scopeId() == 42);
        }

        SUBCASE("equality includes scope") {
                Ipv6Address a = Ipv6Address::loopback();
                Ipv6Address b = Ipv6Address::loopback();
                CHECK(a == b);
                b.setScopeId(1);
                CHECK(a != b);
        }

        SUBCASE("ordering") {
                Ipv6Address a = Ipv6Address::loopback();
                auto [b, err] = Ipv6Address::fromString("::2");
                CHECK(a < b);
        }

        SUBCASE("DataFormat constructor") {
                Ipv6Address::DataFormat bytes{};
                bytes[15] = 1; // ::1
                Ipv6Address addr(bytes);
                CHECK(addr.isLoopback());
                CHECK(addr.data() == bytes);
        }

        SUBCASE("uint8_t pointer constructor") {
                uint8_t bytes[16] = {};
                bytes[15] = 1; // ::1
                Ipv6Address addr(bytes);
                CHECK(addr.isLoopback());
        }

        SUBCASE("data accessor") {
                Ipv6Address lo = Ipv6Address::loopback();
                const Ipv6Address::DataFormat &d = lo.data();
                CHECK(d.size() == 16);
                CHECK(d[15] == 1);
                for(int i = 0; i < 15; ++i) CHECK(d[i] == 0);
        }

        SUBCASE("multicastMac") {
                // ff02::1 → 33:33:00:00:00:01
                auto [mcast, err] = Ipv6Address::fromString("ff02::1");
                CHECK(err.isOk());
                MacAddress mac = mcast.multicastMac();
                CHECK_FALSE(mac.isNull());
                CHECK(mac == MacAddress(0x33, 0x33, 0x00, 0x00, 0x00, 0x01));

                // Non-multicast returns null
                CHECK(Ipv6Address::loopback().multicastMac().isNull());
        }

        SUBCASE("TextStream operator<<") {
                Ipv6Address lo = Ipv6Address::loopback();
                String str;
                {
                        TextStream ts(&str);
                        ts << lo;
                }
                CHECK(str == "::1");
        }

#if !defined(PROMEKI_PLATFORM_EMSCRIPTEN)
        SUBCASE("toSockAddr") {
                Ipv6Address addr = Ipv6Address::loopback();
                addr.setScopeId(5);
                struct sockaddr_in6 sa;
                Error err = addr.toSockAddr(&sa);
                CHECK(err.isOk());
                CHECK(sa.sin6_family == AF_INET6);
                CHECK(sa.sin6_port == 0);
                CHECK(sa.sin6_scope_id == 5);
        }

        SUBCASE("toSockAddr null") {
                Ipv6Address addr = Ipv6Address::loopback();
                CHECK(addr.toSockAddr(nullptr).isError());
        }

        SUBCASE("fromSockAddr round-trip") {
                Ipv6Address orig = Ipv6Address::loopback();
                orig.setScopeId(3);
                struct sockaddr_in6 sa;
                REQUIRE(orig.toSockAddr(&sa).isOk());
                auto [restored, err] = Ipv6Address::fromSockAddr(&sa);
                CHECK(err.isOk());
                CHECK(restored == orig);
        }

        SUBCASE("fromSockAddr null") {
                auto [addr, err] = Ipv6Address::fromSockAddr(nullptr);
                CHECK(err.isError());
        }
#endif
}
