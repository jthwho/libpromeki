/**
 * @file      macaddress.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/macaddress.h>
#include <promeki/ipv4address.h>
#include <promeki/ipv6address.h>
#include <promeki/textstream.h>

using namespace promeki;

TEST_CASE("MacAddress") {

        SUBCASE("default construction is null") {
                MacAddress mac;
                CHECK(mac.isNull());
                CHECK_FALSE(mac.isBroadcast());
                CHECK_FALSE(mac.isMulticast());
        }

        SUBCASE("octet construction") {
                MacAddress mac(0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF);
                CHECK_FALSE(mac.isNull());
                CHECK(mac.octet(0) == 0xAA);
                CHECK(mac.octet(1) == 0xBB);
                CHECK(mac.octet(2) == 0xCC);
                CHECK(mac.octet(3) == 0xDD);
                CHECK(mac.octet(4) == 0xEE);
                CHECK(mac.octet(5) == 0xFF);
        }

        SUBCASE("octet out of range") {
                MacAddress mac(1, 2, 3, 4, 5, 6);
                CHECK(mac.octet(-1) == 0);
                CHECK(mac.octet(6) == 0);
        }

        SUBCASE("fromString colon-separated") {
                auto [mac, err] = MacAddress::fromString("aa:bb:cc:dd:ee:ff");
                CHECK(err.isOk());
                CHECK(mac.octet(0) == 0xAA);
                CHECK(mac.octet(5) == 0xFF);
        }

        SUBCASE("fromString hyphen-separated") {
                auto [mac, err] = MacAddress::fromString("01-23-45-67-89-AB");
                CHECK(err.isOk());
                CHECK(mac.octet(0) == 0x01);
                CHECK(mac.octet(5) == 0xAB);
        }

        SUBCASE("fromString uppercase") {
                auto [mac, err] = MacAddress::fromString("AA:BB:CC:DD:EE:FF");
                CHECK(err.isOk());
                CHECK(mac.octet(0) == 0xAA);
        }

        SUBCASE("fromString invalid") {
                CHECK(MacAddress::fromString("").second().isError());
                CHECK(MacAddress::fromString("aa:bb:cc:dd:ee").second().isError());
                CHECK(MacAddress::fromString("aa:bb:cc:dd:ee:ff:00").second().isError());
                CHECK(MacAddress::fromString("gg:bb:cc:dd:ee:ff").second().isError());
                CHECK(MacAddress::fromString("aabbccddeeff").second().isError());
        }

        SUBCASE("toString") {
                MacAddress mac(0x01, 0x23, 0x45, 0x67, 0x89, 0xAB);
                CHECK(mac.toString() == "01:23:45:67:89:ab");
        }

        SUBCASE("toString with custom separator") {
                MacAddress mac(0x01, 0x23, 0x45, 0x67, 0x89, 0xAB);
                CHECK(mac.toString('-') == "01-23-45-67-89-ab");
        }

        SUBCASE("fromString round-trip") {
                String str = "de:ad:be:ef:ca:fe";
                auto [mac, err] = MacAddress::fromString(str);
                CHECK(err.isOk());
                CHECK(mac.toString() == str);
        }

        SUBCASE("broadcast") {
                MacAddress bcast = MacAddress::broadcast();
                CHECK(bcast.isBroadcast());
                CHECK(bcast.isMulticast()); // broadcast is also multicast (I/G bit set)
                CHECK_FALSE(bcast.isGroupMulticast()); // but not "group multicast" (excludes broadcast)
                CHECK_FALSE(bcast.isUnicast());
                CHECK_FALSE(bcast.isIpv4Multicast());
                CHECK_FALSE(bcast.isIpv6Multicast());
                CHECK(bcast.toString() == "ff:ff:ff:ff:ff:ff");
        }

        SUBCASE("isMulticast and isGroupMulticast") {
                // IPv4 multicast MAC
                MacAddress mcast(0x01, 0x00, 0x5E, 0x00, 0x00, 0x01);
                CHECK(mcast.isMulticast());
                CHECK(mcast.isGroupMulticast());
                CHECK_FALSE(mcast.isBroadcast());
                CHECK_FALSE(mcast.isUnicast());

                // Unicast address
                MacAddress unicast(0x00, 0x00, 0x00, 0x00, 0x00, 0x01);
                CHECK_FALSE(unicast.isMulticast());
                CHECK_FALSE(unicast.isGroupMulticast());
                CHECK(unicast.isUnicast());
        }

        SUBCASE("isIpv4Multicast") {
                // Valid IPv4 multicast MACs: 01:00:5e:00:00:00 to 01:00:5e:7f:ff:ff
                MacAddress valid1(0x01, 0x00, 0x5E, 0x00, 0x00, 0x00);
                CHECK(valid1.isIpv4Multicast());

                MacAddress valid2(0x01, 0x00, 0x5E, 0x7F, 0xFF, 0xFF);
                CHECK(valid2.isIpv4Multicast());

                // High bit of 4th octet set — outside IANA range
                MacAddress invalid(0x01, 0x00, 0x5E, 0x80, 0x00, 0x00);
                CHECK_FALSE(invalid.isIpv4Multicast());

                // Broadcast is not IPv4 multicast
                CHECK_FALSE(MacAddress::broadcast().isIpv4Multicast());

                // Unicast is not IPv4 multicast
                MacAddress unicast(0x00, 0x00, 0x5E, 0x00, 0x00, 0x01);
                CHECK_FALSE(unicast.isIpv4Multicast());
        }

        SUBCASE("isIpv6Multicast") {
                MacAddress valid(0x33, 0x33, 0x00, 0x00, 0x00, 0x01);
                CHECK(valid.isIpv6Multicast());

                MacAddress valid2(0x33, 0x33, 0xFF, 0x12, 0x34, 0x56);
                CHECK(valid2.isIpv6Multicast());

                MacAddress notV6(0x01, 0x00, 0x5E, 0x00, 0x00, 0x01);
                CHECK_FALSE(notV6.isIpv6Multicast());
        }

        SUBCASE("fromIpv4Multicast") {
                // 224.0.0.1 → 01:00:5e:00:00:01
                Ipv4Address mcast(224, 0, 0, 1);
                MacAddress mac = MacAddress::fromIpv4Multicast(mcast);
                CHECK(mac == MacAddress(0x01, 0x00, 0x5E, 0x00, 0x00, 0x01));
                CHECK(mac.isIpv4Multicast());

                // 239.255.255.250 (SSDP) → 01:00:5e:7f:ff:fa
                Ipv4Address ssdp(239, 255, 255, 250);
                MacAddress ssdpMac = MacAddress::fromIpv4Multicast(ssdp);
                CHECK(ssdpMac == MacAddress(0x01, 0x00, 0x5E, 0x7F, 0xFF, 0xFA));

                // Only low 23 bits used: 224.128.0.1 and 225.0.0.1 share the same MAC
                // because bits 24-23 are masked away (octet(1) & 0x7F)
                Ipv4Address a(224, 128, 0, 1);
                Ipv4Address b(225, 0, 0, 1);
                CHECK(MacAddress::fromIpv4Multicast(a) == MacAddress::fromIpv4Multicast(b));

                // Non-multicast returns null
                Ipv4Address unicast(192, 168, 1, 1);
                MacAddress nullMac = MacAddress::fromIpv4Multicast(unicast);
                CHECK(nullMac.isNull());
        }

        SUBCASE("fromIpv6Multicast") {
                // ff02::1 → 33:33:00:00:00:01
                auto [mcast, err] = Ipv6Address::fromString("ff02::1");
                CHECK(err.isOk());
                MacAddress mac = MacAddress::fromIpv6Multicast(mcast);
                CHECK(mac == MacAddress(0x33, 0x33, 0x00, 0x00, 0x00, 0x01));
                CHECK(mac.isIpv6Multicast());

                // ff02::1:ff12:3456 → 33:33:ff:12:34:56
                auto [sol, serr] = Ipv6Address::fromString("ff02::1:ff12:3456");
                CHECK(serr.isOk());
                MacAddress solMac = MacAddress::fromIpv6Multicast(sol);
                CHECK(solMac == MacAddress(0x33, 0x33, 0xFF, 0x12, 0x34, 0x56));

                // Non-multicast returns null
                auto [uni, uerr] = Ipv6Address::fromString("fe80::1");
                CHECK(uerr.isOk());
                MacAddress nullMac = MacAddress::fromIpv6Multicast(uni);
                CHECK(nullMac.isNull());
        }

        SUBCASE("IPv4 multicast MAC collision detection") {
                // These two multicast IPs differ only in bit 24 (which is masked out)
                // so they map to the same MAC address
                Ipv4Address a(224, 1, 2, 3);
                Ipv4Address b(225, 1, 2, 3);  // differs in top 5 bits, same low 23
                CHECK(a.multicastMac() == b.multicastMac());

                // These differ in the low 23 bits, so different MACs
                Ipv4Address c(224, 0, 0, 1);
                Ipv4Address d(224, 0, 0, 2);
                CHECK(c.multicastMac() != d.multicastMac());
        }

        SUBCASE("isLocallyAdministered") {
                // Bit 1 of first octet set
                MacAddress local(0x02, 0x00, 0x00, 0x00, 0x00, 0x01);
                CHECK(local.isLocallyAdministered());

                MacAddress global(0x00, 0x00, 0x00, 0x00, 0x00, 0x01);
                CHECK_FALSE(global.isLocallyAdministered());
        }

        SUBCASE("equality and ordering") {
                MacAddress a(1, 2, 3, 4, 5, 6);
                MacAddress b(1, 2, 3, 4, 5, 6);
                MacAddress c(1, 2, 3, 4, 5, 7);
                CHECK(a == b);
                CHECK(a != c);
                CHECK(a < c);
        }

        SUBCASE("data access") {
                MacAddress mac(0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE);
                const uint8_t *raw = mac.raw();
                CHECK(raw[0] == 0xDE);
                CHECK(raw[5] == 0xFE);
                CHECK(mac.data().size() == 6);
        }

        SUBCASE("DataFormat constructor") {
                MacAddress::DataFormat bytes{std::array<uint8_t, 6>{0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE}};
                MacAddress mac(bytes);
                CHECK_FALSE(mac.isNull());
                CHECK(mac.octet(0) == 0xDE);
                CHECK(mac.octet(5) == 0xFE);
                CHECK(mac.data() == bytes);
        }

        SUBCASE("TextStream operator<<") {
                MacAddress mac(0x01, 0x23, 0x45, 0x67, 0x89, 0xAB);
                String str;
                {
                        TextStream ts(&str);
                        ts << mac;
                }
                CHECK(str == "01:23:45:67:89:ab");
        }
}
