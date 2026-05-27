/**
 * @file      ipv4subnet.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/ipv4subnet.h>

PROMEKI_NAMESPACE_BEGIN

namespace {
        // Returns the CIDR prefix length encoded in @p mask if the
        // mask is contiguous high-bits-set, else -1.  Mirrors the
        // standard "count leading 1s, ensure the rest are zero" walk.
        int countContiguousPrefix(const Ipv4Address &mask) {
                uint32_t v = mask.toUint32();
                // The address stores the four octets with octet 0 in
                // the high byte.  Walk MSB to LSB.
                int count = 0;
                bool sawZero = false;
                for (int i = 31; i >= 0; --i) {
                        bool bit = ((v >> i) & 1U) != 0;
                        if (bit) {
                                if (sawZero) return -1;
                                ++count;
                        } else {
                                sawZero = true;
                        }
                }
                return count;
        }
}

Result<Ipv4Subnet> Ipv4Subnet::fromString(const String &str) {
        if (str.isEmpty()) return makeError<Ipv4Subnet>(Error::Invalid);
        size_t slash = str.find('/');
        String addrPart = (slash == String::npos) ? str : str.left(slash);
        auto [addr, addrErr] = Ipv4Address::fromString(addrPart);
        if (addrErr.isError()) return makeError<Ipv4Subnet>(addrErr);
        if (slash == String::npos) {
                return makeResult(Ipv4Subnet(addr, 32));
        }
        String maskPart = str.mid(slash + 1);
        // CIDR prefix length first (most common form), netmask
        // dotted-quad fallback for explicit non-contiguous shapes
        // and for compatibility with other tooling.
        Error pe;
        int   prefix = maskPart.toInt(&pe);
        if (pe.isOk() && prefix >= 0 && prefix <= 32) {
                return makeResult(Ipv4Subnet(addr, prefix));
        }
        auto [mask, mErr] = Ipv4Address::fromString(maskPart);
        if (mErr.isError()) return makeError<Ipv4Subnet>(mErr);
        return makeResult(Ipv4Subnet(addr, mask));
}

Ipv4Address Ipv4Subnet::netmaskForPrefix(int prefixLen) {
        if (prefixLen <= 0) return Ipv4Address();
        if (prefixLen >= 32) return Ipv4Address(0xFFFFFFFFu);
        uint32_t bits = 0xFFFFFFFFu << (32 - prefixLen);
        return Ipv4Address(bits);
}

Ipv4Address Ipv4Subnet::network() const {
        return Ipv4Address(_addr.toUint32() & _mask.toUint32());
}

Ipv4Address Ipv4Subnet::broadcast() const {
        // The directed broadcast address is the network address with
        // the host bits all set; for a /32 the netmask covers every
        // bit so this collapses back to the address itself, matching
        // the "loopback / point-to-point" expectation.
        return Ipv4Address(_addr.toUint32() | ~_mask.toUint32());
}

int Ipv4Subnet::prefixLen() const {
        return countContiguousPrefix(_mask);
}

bool Ipv4Subnet::contains(const Ipv4Address &addr) const {
        return (addr.toUint32() & _mask.toUint32()) == (_addr.toUint32() & _mask.toUint32());
}

String Ipv4Subnet::toString() const {
        int prefix = prefixLen();
        if (prefix >= 0) {
                return _addr.toString() + "/" + String::number(prefix);
        }
        return _addr.toString() + "/" + _mask.toString();
}

PROMEKI_NAMESPACE_END
