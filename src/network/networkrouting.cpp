/**
 * @file      networkrouting.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/networkrouting.h>
#include <promeki/mutex.h>

PROMEKI_NAMESPACE_BEGIN

namespace {
        struct BackendSlot {
                Mutex                     mutex;
                NetworkRouting::Backend *backend = nullptr; // owned
        };

        BackendSlot &slot() {
                static BackendSlot s;
                return s;
        }
}

void NetworkRouting::setBackend(Backend *backend) {
        BackendSlot  &s = slot();
        Mutex::Locker lock(s.mutex);
        delete s.backend;
        s.backend = backend;
}

NetworkRouting::Backend *NetworkRouting::backend() {
        BackendSlot  &s = slot();
        Mutex::Locker lock(s.mutex);
        return s.backend;
}

NetworkAddress NetworkRouting::defaultGatewayIpv4() {
        Backend *b = backend();
        return b == nullptr ? NetworkAddress() : b->defaultGatewayIpv4();
}

NetworkAddress NetworkRouting::defaultGatewayIpv6() {
        Backend *b = backend();
        return b == nullptr ? NetworkAddress() : b->defaultGatewayIpv6();
}

NetworkInterface NetworkRouting::defaultRouteInterfaceIpv4() {
        Backend *b = backend();
        return b == nullptr ? NetworkInterface() : b->defaultRouteInterfaceIpv4();
}

NetworkInterface NetworkRouting::defaultRouteInterfaceIpv6() {
        Backend *b = backend();
        return b == nullptr ? NetworkInterface() : b->defaultRouteInterfaceIpv6();
}

List<NetworkAddress> NetworkRouting::dnsServers() {
        Backend *b = backend();
        return b == nullptr ? List<NetworkAddress>() : b->dnsServers();
}

List<NetworkRouting::Route> NetworkRouting::routesFor(const Ipv4Address &dest) {
        Backend *b = backend();
        return b == nullptr ? List<Route>() : b->routesForIpv4(dest);
}

List<NetworkRouting::Route> NetworkRouting::routesFor(const Ipv6Address &dest) {
        Backend *b = backend();
        return b == nullptr ? List<Route>() : b->routesForIpv6(dest);
}

Ipv4Address NetworkRouting::sourceAddressFor(const Ipv4Address &dest) {
        if (dest.isNull()) return Ipv4Address();
        // Loopback: trivially answers from 127.0.0.1.
        if (dest.isLoopback()) return Ipv4Address(127, 0, 0, 1);
        // Multicast: no per-subnet binding, so use the default-route
        // iface's primary address.
        if (dest.isMulticast()) {
                NetworkInterface gw = defaultRouteInterfaceIpv4();
                if (!gw.isValid()) return Ipv4Address();
                Ipv4Address::List addrs = gw.ipv4Addresses();
                return addrs.isEmpty() ? Ipv4Address() : addrs[0];
        }
        // Direct: walk every interface that covers the dest's subnet
        // and return its primary IPv4 address.
        for (const auto &iface : NetworkInterface::findRoutesTo(dest)) {
                for (const auto &subnet : iface.ipv4Subnets()) {
                        if (subnet.contains(dest)) return subnet.address();
                }
        }
        // Fall back to the default-route iface's primary address.
        NetworkInterface gw = defaultRouteInterfaceIpv4();
        if (!gw.isValid()) return Ipv4Address();
        Ipv4Address::List addrs = gw.ipv4Addresses();
        return addrs.isEmpty() ? Ipv4Address() : addrs[0];
}

Ipv6Address NetworkRouting::sourceAddressFor(const Ipv6Address &dest) {
        if (dest.isNull()) return Ipv6Address();
        // fe80::/10 link-local requires an explicit interface; the
        // single-arg form can't pick one.
        const auto &b = dest.data();
        if (b[0] == 0xFE && (b[1] & 0xC0) == 0x80) return Ipv6Address();
        // Loopback ::1 → answer from itself.
        if (dest.isLoopback()) return dest;
        // Multicast (ff00::/8): default-route iface's primary address.
        if (b[0] == 0xFF) {
                NetworkInterface gw = defaultRouteInterfaceIpv6();
                if (!gw.isValid()) return Ipv6Address();
                Ipv6Address::List addrs = gw.ipv6Addresses();
                return addrs.isEmpty() ? Ipv6Address() : addrs[0];
        }
        for (const auto &iface : NetworkInterface::findRoutesTo(dest)) {
                for (const auto &subnet : iface.ipv6Subnets()) {
                        if (subnet.contains(dest)) return subnet.address();
                }
        }
        NetworkInterface gw = defaultRouteInterfaceIpv6();
        if (!gw.isValid()) return Ipv6Address();
        Ipv6Address::List addrs = gw.ipv6Addresses();
        return addrs.isEmpty() ? Ipv6Address() : addrs[0];
}

PROMEKI_NAMESPACE_END
