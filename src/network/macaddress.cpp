/**
 * @file      macaddress.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/macaddress.h>
#include <promeki/ipv4address.h>
#include <promeki/ipv6address.h>
#include <promeki/textstream.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

int hexDigit(char c) {
        if(c >= '0' && c <= '9') return c - '0';
        if(c >= 'a' && c <= 'f') return c - 'a' + 10;
        if(c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
}

} // anonymous namespace

Result<MacAddress> MacAddress::fromString(const String &str) {
        // Expected format: "aa:bb:cc:dd:ee:ff" or "aa-bb-cc-dd-ee-ff" (17 chars)
        if(str.size() != 17) return makeError<MacAddress>(Error::Invalid);

        MacAddress::DataFormat bytes{};
        for(int i = 0; i < 6; ++i) {
                size_t pos = static_cast<size_t>(i) * 3;
                int hi = hexDigit(str[pos]);
                int lo = hexDigit(str[pos + 1]);
                if(hi < 0 || lo < 0) return makeError<MacAddress>(Error::Invalid);
                bytes[i] = static_cast<uint8_t>((hi << 4) | lo);
                if(i < 5) {
                        char sep = str[pos + 2];
                        if(sep != ':' && sep != '-') {
                                return makeError<MacAddress>(Error::Invalid);
                        }
                }
        }

        return makeResult(MacAddress(bytes));
}

MacAddress MacAddress::broadcast() {
        return MacAddress(0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF);
}

MacAddress MacAddress::fromIpv4Multicast(const Ipv4Address &addr) {
        if(!addr.isMulticast()) return MacAddress();
        // IANA mapping: 01:00:5e + low 23 bits of IPv4 address
        // Octet 1 (index 1) of the IP contributes bits 22-16 (masked to 7 bits)
        // Octet 2 (index 2) of the IP contributes bits 15-8
        // Octet 3 (index 3) of the IP contributes bits 7-0
        return MacAddress(
                0x01, 0x00, 0x5E,
                static_cast<uint8_t>(addr.octet(1) & 0x7F),
                addr.octet(2),
                addr.octet(3)
        );
}

MacAddress MacAddress::fromIpv6Multicast(const Ipv6Address &addr) {
        if(!addr.isMulticast()) return MacAddress();
        // RFC 2464: 33:33 + low 32 bits of IPv6 address
        const uint8_t *raw = addr.raw();
        return MacAddress(
                0x33, 0x33,
                raw[12], raw[13], raw[14], raw[15]
        );
}

String MacAddress::toString() const {
        return toString(':');
}

String MacAddress::toString(char separator) const {
        String ret;
        for(int i = 0; i < 6; ++i) {
                if(i > 0) ret += separator;
                ret += String::number(_addr[i], 16, 2, '0').toLower();
        }
        return ret;
}

TextStream &operator<<(TextStream &stream, const MacAddress &addr) {
        stream << addr.toString();
        return stream;
}

PROMEKI_NAMESPACE_END
