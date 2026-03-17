/**
 * @file      net/networkaddress.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/network/networkaddress.h>
#include <promeki/core/textstream.h>

PROMEKI_NAMESPACE_BEGIN

Result<NetworkAddress> NetworkAddress::fromString(const String &str) {
        if(str.isEmpty()) return makeError<NetworkAddress>(Error::Invalid);

        // Try IPv4 first
        auto [v4, v4err] = Ipv4Address::fromString(str);
        if(v4err.isOk()) return makeResult(NetworkAddress(v4));

        // Try IPv6 (strip brackets if present)
        String v6str = str;
        if(v6str.startsWith("[") && v6str.endsWith("]")) {
                v6str = v6str.mid(1, v6str.size() - 2);
        }
        auto [v6, v6err] = Ipv6Address::fromString(v6str);
        if(v6err.isOk()) return makeResult(NetworkAddress(v6));

        // Store as unresolved hostname
        return makeResult(NetworkAddress(str));
}

NetworkAddress::Type NetworkAddress::type() const {
        if(std::holds_alternative<Ipv4Address>(_data)) return IPv4;
        if(std::holds_alternative<Ipv6Address>(_data)) return IPv6;
        if(std::holds_alternative<String>(_data)) return Hostname;
        return None;
}

Ipv4Address NetworkAddress::toIpv4() const {
        if(auto *p = std::get_if<Ipv4Address>(&_data)) return *p;
        return Ipv4Address();
}

Ipv6Address NetworkAddress::toIpv6() const {
        if(auto *p = std::get_if<Ipv6Address>(&_data)) return *p;
        return Ipv6Address();
}

String NetworkAddress::hostname() const {
        if(auto *p = std::get_if<String>(&_data)) return *p;
        return String();
}

bool NetworkAddress::isLoopback() const {
        if(auto *p = std::get_if<Ipv4Address>(&_data)) return p->isLoopback();
        if(auto *p = std::get_if<Ipv6Address>(&_data)) return p->isLoopback();
        if(auto *p = std::get_if<String>(&_data)) return *p == "localhost";
        return false;
}

bool NetworkAddress::isMulticast() const {
        if(auto *p = std::get_if<Ipv4Address>(&_data)) return p->isMulticast();
        if(auto *p = std::get_if<Ipv6Address>(&_data)) return p->isMulticast();
        return false;
}

bool NetworkAddress::isLinkLocal() const {
        if(auto *p = std::get_if<Ipv4Address>(&_data)) return p->isLinkLocal();
        if(auto *p = std::get_if<Ipv6Address>(&_data)) return p->isLinkLocal();
        return false;
}

String NetworkAddress::toString() const {
        if(auto *p = std::get_if<Ipv4Address>(&_data)) return p->toString();
        if(auto *p = std::get_if<Ipv6Address>(&_data)) return p->toString();
        if(auto *p = std::get_if<String>(&_data)) return *p;
        return String();
}

#if !defined(PROMEKI_PLATFORM_EMSCRIPTEN)
Result<NetworkAddress> NetworkAddress::fromSockAddr(const struct sockaddr *addr, size_t len) {
        if(addr == nullptr || len == 0) return makeError<NetworkAddress>(Error::Invalid);
        if(addr->sa_family == AF_INET && len >= sizeof(struct sockaddr_in)) {
                auto [v4, err] = Ipv4Address::fromSockAddr(
                        reinterpret_cast<const struct sockaddr_in *>(addr));
                if(err.isError()) return makeError<NetworkAddress>(err.code());
                return makeResult(NetworkAddress(v4));
        }
        if(addr->sa_family == AF_INET6 && len >= sizeof(struct sockaddr_in6)) {
                auto [v6, err] = Ipv6Address::fromSockAddr(
                        reinterpret_cast<const struct sockaddr_in6 *>(addr));
                if(err.isError()) return makeError<NetworkAddress>(err.code());
                return makeResult(NetworkAddress(v6));
        }
        return makeError<NetworkAddress>(Error::Invalid);
}

size_t NetworkAddress::toSockAddr(struct sockaddr_storage *storage) const {
        if(storage == nullptr || !isResolved()) return 0;
        if(isIPv4()) {
                auto *sa4 = reinterpret_cast<struct sockaddr_in *>(storage);
                Error err = toIpv4().toSockAddr(sa4);
                if(err.isError()) return 0;
                return sizeof(struct sockaddr_in);
        }
        if(isIPv6()) {
                auto *sa6 = reinterpret_cast<struct sockaddr_in6 *>(storage);
                Error err = toIpv6().toSockAddr(sa6);
                if(err.isError()) return 0;
                return sizeof(struct sockaddr_in6);
        }
        return 0;
}
#endif

TextStream &operator<<(TextStream &stream, const NetworkAddress &addr) {
        stream << addr.toString();
        return stream;
}

PROMEKI_NAMESPACE_END
