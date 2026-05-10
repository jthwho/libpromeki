/**
 * @file      ipv6subnet.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/ipv6subnet.h>

#include <cstring>

PROMEKI_NAMESPACE_BEGIN

Result<Ipv6Subnet> Ipv6Subnet::fromString(const String &str) {
        if (str.isEmpty()) return makeError<Ipv6Subnet>(Error::Invalid);
        // The slash is unambiguous as the prefix separator because
        // colon-hex IPv6 syntax never contains a forward slash; the
        // optional @c %scope suffix lives before any /prefix.
        size_t slash = str.find('/');
        String addrPart = (slash == String::npos) ? str : str.left(slash);
        auto [addr, addrErr] = Ipv6Address::fromString(addrPart);
        if (addrErr.isError()) return makeError<Ipv6Subnet>(addrErr);
        int prefix = 128;
        if (slash != String::npos) {
                Error pe;
                int   p = str.mid(slash + 1).toInt(&pe);
                if (pe.isError() || p < 0 || p > 128) return makeError<Ipv6Subnet>(Error::Invalid);
                prefix = p;
        }
        return makeResult(Ipv6Subnet(addr, prefix));
}

bool Ipv6Subnet::isValid() const {
        for (int i = 0; i < 16; ++i) {
                if (_addr.data()[i] != 0) return true;
        }
        return false;
}

bool Ipv6Subnet::contains(const Ipv6Address &addr) const {
        if (_prefixLen <= 0) return true;
        const uint8_t *a = addr.data().data();
        const uint8_t *b = _addr.data().data();
        int            fullBytes = _prefixLen / 8;
        int            tailBits = _prefixLen % 8;
        if (std::memcmp(a, b, static_cast<size_t>(fullBytes)) != 0) return false;
        if (tailBits == 0) return true;
        uint8_t mask = static_cast<uint8_t>(0xFFu << (8 - tailBits));
        return (a[fullBytes] & mask) == (b[fullBytes] & mask);
}

String Ipv6Subnet::toString() const {
        return _addr.toString() + "/" + String::number(_prefixLen);
}

PROMEKI_NAMESPACE_END
