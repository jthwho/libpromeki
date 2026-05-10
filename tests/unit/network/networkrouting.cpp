/**
 * @file      networkrouting.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/networkrouting.h>
#include <promeki/networkinterface.h>

using namespace promeki;

TEST_CASE("NetworkRouting::defaultGatewayIpv4 is null or owned by an up iface") {
        NetworkAddress gw = NetworkRouting::defaultGatewayIpv4();
        if (gw.isNull()) {
                MESSAGE("No IPv4 default gateway configured (CI containers often lack one)");
                return;
        }
        REQUIRE(gw.isIPv4());
        // The owning interface (default route) should be enumerable
        // and administratively up.
        NetworkInterface owner = NetworkRouting::defaultRouteInterfaceIpv4();
        REQUIRE(owner.isValid());
        CHECK(owner.isUp());
}

TEST_CASE("NetworkRouting::sourceAddressFor(127.0.0.1) returns 127.0.0.1") {
        Ipv4Address src = NetworkRouting::sourceAddressFor(Ipv4Address(127, 0, 0, 1));
        CHECK(src == Ipv4Address(127, 0, 0, 1));
}

TEST_CASE("NetworkRouting::sourceAddressFor returns a covered address for a directly-attached subnet") {
        // Walk every up non-loopback iface; pick one with at least one
        // IPv4 subnet larger than /32 and check that an address inside
        // that subnet selects the iface's own primary address.
        for (const auto &iface : NetworkInterface::enumerate()) {
                if (!iface.isUp() || iface.isLoopback()) continue;
                for (const auto &subnet : iface.ipv4Subnets()) {
                        if (subnet.prefixLen() <= 0 || subnet.prefixLen() == 32) continue;
                        Ipv4Address candidate = subnet.network();
                        // Pick a different host address inside the
                        // subnet so we don't trivially match the
                        // iface's own .1 address.  Network + 2 lands
                        // inside any prefix shorter than /31.
                        if (subnet.prefixLen() < 31) {
                                candidate = Ipv4Address(candidate.octet(0), candidate.octet(1), candidate.octet(2),
                                                        static_cast<uint8_t>(candidate.octet(3) + 2));
                        }
                        Ipv4Address src = NetworkRouting::sourceAddressFor(candidate);
                        CHECK(src == subnet.address());
                        return; // one verified iface is enough
                }
        }
        MESSAGE("No directly-attached non-loopback IPv4 subnet to test source-address selection against");
}

TEST_CASE("NetworkRouting::dnsServers returns resolved addresses or an empty list") {
        auto servers = NetworkRouting::dnsServers();
        for (const auto &dns : servers) {
                CHECK(dns.isResolved());
        }
}

TEST_CASE("NetworkRouting::routesFor returns routes sorted by metric") {
        // Pick a destination most hosts can route — the IPv4 default
        // is the right thing for dev machines, fall back to a public
        // IP when no default route exists.
        Ipv4Address probe(8, 8, 8, 8);
        auto        routes = NetworkRouting::routesFor(probe);
        if (routes.isEmpty()) {
                MESSAGE("No IPv4 routes to 8.8.8.8 (CI containers without a default route); skipping");
                return;
        }
        // First entry is the kernel's actual choice — its iface
        // should be enumerable and up.
        const auto &best = routes.front();
        REQUIRE(best.iface.isValid());
        CHECK(best.iface.isUp());
        // Metric is monotonically non-decreasing through the list.
        for (size_t i = 1; i < routes.size(); ++i) {
                CHECK(routes[i].metric >= routes[i - 1].metric);
        }
}

TEST_CASE("NetworkRouting::routesFor returns covering routes (default fallback)") {
        // Pick a destination unlikely to be in any specific subnet
        // on a developer machine — the default route should be the
        // covering match.  Use a public address rather than
        // 127.0.0.1 because Linux puts loopback in the per-host
        // local table, not /proc/net/route's main table.
        Ipv4Address publicAddr(1, 1, 1, 1);
        auto        routes = NetworkRouting::routesFor(publicAddr);
        if (routes.isEmpty()) {
                MESSAGE("No routes from /proc/net/route's main table cover 1.1.1.1; skipping");
                return;
        }
        for (const auto &r : routes) {
                CAPTURE(r.metric);
                CAPTURE(r.prefixLen);
                if (r.prefixLen == 0) continue;
                // Non-default routes should literally cover the dest.
                Ipv4Subnet subnet(r.destination.toIpv4(),
                                  Ipv4Subnet::netmaskForPrefix(r.prefixLen));
                CHECK(subnet.contains(publicAddr));
        }
}

TEST_CASE("NetworkInterface::stats() on the loopback returns valid + monotonic counters") {
        NetworkInterface lo;
        for (const auto &iface : NetworkInterface::enumerate()) {
                if (iface.isLoopback()) {
                        lo = iface;
                        break;
                }
        }
        if (!lo.isValid()) {
                MESSAGE("No loopback iface; skipping stats test");
                return;
        }
        NetworkInterfaceStats first = lo.stats();
        if (!first.valid) {
                MESSAGE("/sys/class/net/<lo>/statistics not present; skipping");
                return;
        }
        // Generate a tiny bit of loopback traffic so the second
        // sample shouldn't collapse to identical values — a UDP
        // socket bound to lo would do it, but a sleep and re-read is
        // sufficient for the monotonicity check (loopback always has
        // background chatter on a Linux desktop).
        NetworkInterfaceStats second = lo.stats();
        CHECK(second.valid);
        CHECK(second.rxBytes >= first.rxBytes);
        CHECK(second.rxPackets >= first.rxPackets);
        CHECK(second.txBytes >= first.txBytes);
        CHECK(second.txPackets >= first.txPackets);
}
