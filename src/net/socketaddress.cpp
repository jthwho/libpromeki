/**
 * @file      net/socketaddress.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/network/socketaddress.h>
#include <promeki/core/textstream.h>

PROMEKI_NAMESPACE_BEGIN

Result<SocketAddress> SocketAddress::fromString(const String &hostPort) {
        if(hostPort.isEmpty()) {
                return makeError<SocketAddress>(Error::Invalid);
        }

        String host;
        uint16_t port = 0;

        // Check for IPv6 bracketed notation: [addr]:port
        if(hostPort.startsWith("[")) {
                size_t closeBracket = hostPort.find(']');
                if(closeBracket == String::npos) {
                        return makeError<SocketAddress>(Error::Invalid);
                }
                host = hostPort.mid(1, closeBracket - 1);
                // After closing bracket, expect ":port"
                if(closeBracket + 1 < hostPort.size()) {
                        if(hostPort.charAt(closeBracket + 1) != ':') {
                                return makeError<SocketAddress>(Error::Invalid);
                        }
                        String portStr = hostPort.mid(closeBracket + 2);
                        Error portErr;
                        int portVal = portStr.toInt(&portErr);
                        if(portErr.isError() || portVal < 0 || portVal > 65535) {
                                return makeError<SocketAddress>(Error::Invalid);
                        }
                        port = static_cast<uint16_t>(portVal);
                }
        } else {
                // Find the last colon for host:port separation.
                // For IPv6 without brackets, there would be multiple colons —
                // but that format is not supported (brackets are required for IPv6).
                size_t lastColon = hostPort.rfind(':');
                if(lastColon == String::npos) {
                        return makeError<SocketAddress>(Error::Invalid);
                }
                host = hostPort.left(lastColon);
                String portStr = hostPort.mid(lastColon + 1);
                Error portErr;
                int portVal = portStr.toInt(&portErr);
                if(portErr.isError() || portVal < 0 || portVal > 65535) {
                        return makeError<SocketAddress>(Error::Invalid);
                }
                port = static_cast<uint16_t>(portVal);
        }

        auto [addr, err] = NetworkAddress::fromString(host);
        if(err.isError()) {
                return makeError<SocketAddress>(Error::Invalid);
        }

        return makeResult(SocketAddress(addr, port));
}

String SocketAddress::toString() const {
        if(isNull()) return String();
        String addrStr = _address.toString();
        if(_address.isIPv6()) {
                return String("[") + addrStr + "]:" + String::number(_port);
        }
        return addrStr + ":" + String::number(_port);
}

#if !defined(PROMEKI_PLATFORM_EMSCRIPTEN)
Result<SocketAddress> SocketAddress::fromSockAddr(const struct sockaddr *addr, size_t len) {
        if(addr == nullptr) {
                return makeError<SocketAddress>(Error::Invalid);
        }
        auto [netAddr, err] = NetworkAddress::fromSockAddr(addr, len);
        if(err.isError()) {
                return makeError<SocketAddress>(Error::Invalid);
        }
        uint16_t port = 0;
        if(addr->sa_family == AF_INET) {
                auto *sa4 = reinterpret_cast<const struct sockaddr_in *>(addr);
                port = ntohs(sa4->sin_port);
        } else if(addr->sa_family == AF_INET6) {
                auto *sa6 = reinterpret_cast<const struct sockaddr_in6 *>(addr);
                port = ntohs(sa6->sin6_port);
        }
        return makeResult(SocketAddress(netAddr, port));
}

size_t SocketAddress::toSockAddr(struct sockaddr_storage *storage) const {
        if(storage == nullptr) return 0;
        size_t len = _address.toSockAddr(storage);
        if(len == 0) return 0;
        if(storage->ss_family == AF_INET) {
                auto *sa4 = reinterpret_cast<struct sockaddr_in *>(storage);
                sa4->sin_port = htons(_port);
        } else if(storage->ss_family == AF_INET6) {
                auto *sa6 = reinterpret_cast<struct sockaddr_in6 *>(storage);
                sa6->sin6_port = htons(_port);
        }
        return len;
}
#endif

TextStream &operator<<(TextStream &stream, const SocketAddress &addr) {
        stream << addr.toString();
        return stream;
}

PROMEKI_NAMESPACE_END
