/**
 * @file      networkinterface.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/networkinterface.h>
#include <promeki/networkinterfacebackend.h>
#include <promeki/textstream.h>

PROMEKI_NAMESPACE_BEGIN

namespace {
        bool isLinkLocalIpv6(const Ipv6Address &addr) {
                // fe80::/10 covers the IPv6 link-local prefix; the
                // top byte's high six bits are 0xFE and the next two
                // bits are 10.  Equivalent to addr.octet(0) == 0xFE
                // and (addr.octet(1) & 0xC0) == 0x80.
                const auto &b = addr.data();
                return b[0] == 0xFE && (b[1] & 0xC0) == 0x80;
        }
}

NetworkInterface::List NetworkInterface::enumerate() {
        List out;
        for (auto &impl : NetworkInterfaceBackend::enumerateAll()) {
                out.pushToBack(NetworkInterface(impl));
        }
        return out;
}

NetworkInterface NetworkInterface::findByName(const String &name) {
        if (name.isEmpty()) return NetworkInterface();
        for (const auto &iface : enumerate()) {
                if (iface.name() == name) return iface;
        }
        return NetworkInterface();
}

NetworkInterface NetworkInterface::findByIpv4Address(const Ipv4Address &addr) {
        if (addr.isNull()) return NetworkInterface();
        for (const auto &iface : enumerate()) {
                for (const auto &subnet : iface.ipv4Subnets()) {
                        if (subnet.address() == addr) return iface;
                }
        }
        return NetworkInterface();
}

NetworkInterface NetworkInterface::findByIpv6Address(const Ipv6Address &addr) {
        for (const auto &iface : enumerate()) {
                for (const auto &subnet : iface.ipv6Subnets()) {
                        if (subnet.address() == addr) return iface;
                }
        }
        return NetworkInterface();
}

NetworkInterface NetworkInterface::findByMacAddress(const MacAddress &mac) {
        if (mac.isNull()) return NetworkInterface();
        for (const auto &iface : enumerate()) {
                for (const auto &candidate : iface.allMacAddresses()) {
                        if (candidate == mac) return iface;
                }
        }
        return NetworkInterface();
}

NetworkInterface::List NetworkInterface::findRoutesTo(const Ipv4Address &dest) {
        List out;
        for (const auto &iface : enumerate()) {
                if (iface.canRoute(dest)) out.pushToBack(iface);
        }
        return out;
}

NetworkInterface::List NetworkInterface::findRoutesTo(const Ipv6Address &dest) {
        List out;
        for (const auto &iface : enumerate()) {
                if (iface.canRoute(dest)) out.pushToBack(iface);
        }
        return out;
}

NetworkInterface NetworkInterface::firstNonLoopback() {
        for (const auto &iface : enumerate()) {
                if (iface.isLoopback()) continue;
                if (!iface.isUp()) continue;
                if (iface.macAddress().isNull()) continue;
                return iface;
        }
        return NetworkInterface();
}

NetworkInterfaceData NetworkInterface::data() const {
        if (!_d.isValid()) return NetworkInterfaceData{};
        return _d->data();
}

String NetworkInterface::name() const {
        return data().name;
}

String NetworkInterface::friendlyName() const {
        return data().friendlyName;
}

uint32_t NetworkInterface::index() const {
        return data().index;
}

MacAddress NetworkInterface::macAddress() const {
        const auto macs = allMacAddresses();
        if (macs.isEmpty()) return MacAddress();
        return macs[0];
}

MacAddress::List NetworkInterface::allMacAddresses() const {
        return data().macAddresses;
}

Ipv4Subnet::List NetworkInterface::ipv4Subnets() const {
        return data().ipv4Subnets;
}

Ipv6Subnet::List NetworkInterface::ipv6Subnets() const {
        return data().ipv6Subnets;
}

Ipv4Address::List NetworkInterface::ipv4Addresses() const {
        Ipv4Address::List out;
        for (const auto &subnet : ipv4Subnets()) out.pushToBack(subnet.address());
        return out;
}

Ipv6Address::List NetworkInterface::ipv6Addresses() const {
        Ipv6Address::List out;
        for (const auto &subnet : ipv6Subnets()) out.pushToBack(subnet.address());
        return out;
}

uint32_t NetworkInterface::mtu() const {
        return data().mtu;
}

NetworkInterfaceKind NetworkInterface::kind() const {
        return data().kind;
}

uint64_t NetworkInterface::linkSpeedMbps() const {
        return data().linkSpeedMbps;
}

bool NetworkInterface::fullDuplex() const {
        return data().fullDuplex;
}

bool NetworkInterface::isUp() const {
        return data().isUp;
}

bool NetworkInterface::isRunning() const {
        return data().isRunning;
}

bool NetworkInterface::hasCarrier() const {
        return data().hasCarrier;
}

bool NetworkInterface::isLoopback() const {
        return data().isLoopback;
}

bool NetworkInterface::isMulticast() const {
        return data().isMulticast;
}

bool NetworkInterface::canRoute(const Ipv4Address &dest) const {
        if (!isValid()) return false;
        const auto snap = data();
        if (!snap.isUp) return false;
        // Multicast destinations have no per-subnet binding — every
        // up + multicast-capable interface is a valid egress.
        if (dest.isMulticast()) return snap.isMulticast;
        // Limited-broadcast 255.255.255.255 routes out every up
        // broadcast-capable (i.e. non-loopback, non-point-to-point)
        // interface; treat it like multicast for the canRoute check
        // since callers usually want "any up interface will do".
        if (dest.isBroadcast()) return !snap.isLoopback;
        // Loopback prefix is per-loopback-interface by definition.
        if (dest.isLoopback()) return snap.isLoopback;
        for (const auto &subnet : snap.ipv4Subnets) {
                if (subnet.contains(dest)) return true;
        }
        return false;
}

bool NetworkInterface::canRoute(const Ipv6Address &dest) const {
        if (!isValid()) return false;
        const auto snap = data();
        if (!snap.isUp) return false;
        if (dest.data()[0] == 0xFF) {
                // ff00::/8 — IPv6 multicast.  Every up + multicast-
                // capable interface is a valid egress; the receiver
                // chooses the scope (interface-, link-, site-, etc.).
                return snap.isMulticast;
        }
        if (isLinkLocalIpv6(dest)) {
                // Link-local always needs explicit interface
                // selection; canRoute returns true for every up
                // multicast-capable interface and the caller picks
                // by scope id or name.
                return snap.isMulticast;
        }
        for (const auto &subnet : snap.ipv6Subnets) {
                if (subnet.contains(dest)) return true;
        }
        return false;
}

NetworkInterfaceStats NetworkInterface::stats() const {
        if (!_d.isValid()) return NetworkInterfaceStats{};
        return _d->stats();
}

String NetworkInterface::toString() const {
        if (!_d.isValid()) return String();
        const auto snap = data();
        String     out  = snap.name;
        if (!snap.friendlyName.isEmpty() && snap.friendlyName != snap.name) {
                out += " (";
                out += snap.friendlyName;
                out += ")";
        }
        out += " [";
        out += snap.kind.valueName();
        out += "] ";
        out += snap.isUp ? "up" : "down";
        if (!snap.macAddresses.isEmpty()) {
                out += " MAC=";
                out += snap.macAddresses[0].toString();
        }
        out += " addrs=";
        out += String::number(static_cast<uint64_t>(snap.ipv4Subnets.size() + snap.ipv6Subnets.size()));
        return out;
}

TextStream &operator<<(TextStream &stream, const NetworkInterface &iface) {
        stream << iface.toString();
        return stream;
}

TextStream &operator<<(TextStream &stream, const NetworkInterfaceData &data) {
        stream << "NetworkInterfaceData{\n";
        stream << "  name=" << data.name << "\n";
        stream << "  friendlyName=" << data.friendlyName << "\n";
        stream << "  index=" << static_cast<uint64_t>(data.index) << "\n";
        stream << "  kind=" << data.kind.valueName() << "\n";
        stream << "  mtu=" << static_cast<uint64_t>(data.mtu) << "\n";
        stream << "  linkSpeedMbps=" << data.linkSpeedMbps << "\n";
        stream << "  fullDuplex=" << data.fullDuplex << "\n";
        stream << "  isUp=" << data.isUp << "\n";
        stream << "  isRunning=" << data.isRunning << "\n";
        stream << "  hasCarrier=" << data.hasCarrier << "\n";
        stream << "  isLoopback=" << data.isLoopback << "\n";
        stream << "  isMulticast=" << data.isMulticast << "\n";
        for (const auto &mac : data.macAddresses) stream << "  mac=" << mac << "\n";
        for (const auto &s : data.ipv4Subnets) stream << "  ipv4=" << s.toString() << "\n";
        for (const auto &s : data.ipv6Subnets) stream << "  ipv6=" << s.toString() << "\n";
        stream << "}";
        return stream;
}

TextStream &operator<<(TextStream &stream, const NetworkInterfaceStats &stats) {
        stream << "rx=" << stats.rxBytes << "/" << stats.rxPackets << "/" << stats.rxErrors << "/" << stats.rxDropped;
        stream << " tx=" << stats.txBytes << "/" << stats.txPackets << "/" << stats.txErrors << "/" << stats.txDropped;
        stream << (stats.valid ? " [valid]" : " [invalid]");
        return stream;
}

PROMEKI_NAMESPACE_END
