/**
 * @file      eui64.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/eui64.h>
#include <promeki/macaddress.h>
#include <promeki/textstream.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

        int hexDigit(char c) {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
        }

        // Parse 8 hex-separated octets (23 chars): "aa-bb-cc-dd-ee-ff-00-11"
        Result<EUI64> parseOctet(const String &str) {
                if (str.size() != 23) return makeError<EUI64>(Error::Invalid);
                EUI64::DataFormat bytes{};
                for (int i = 0; i < 8; ++i) {
                        size_t pos = static_cast<size_t>(i) * 3;
                        int    hi = hexDigit(str[pos]);
                        int    lo = hexDigit(str[pos + 1]);
                        if (hi < 0 || lo < 0) return makeError<EUI64>(Error::Invalid);
                        bytes[i] = static_cast<uint8_t>((hi << 4) | lo);
                        if (i < 7) {
                                char sep = str[pos + 2];
                                if (sep != ':' && sep != '-') {
                                        return makeError<EUI64>(Error::Invalid);
                                }
                        }
                }
                return makeResult(EUI64(bytes));
        }

        // Parse 4 colon-separated 16-bit groups (19 chars): "aabb:ccdd:eeff:0011"
        Result<EUI64> parseIPv6(const String &str) {
                if (str.size() != 19) return makeError<EUI64>(Error::Invalid);
                EUI64::DataFormat bytes{};
                for (int g = 0; g < 4; ++g) {
                        size_t pos = static_cast<size_t>(g) * 5;
                        int    d0 = hexDigit(str[pos]);
                        int    d1 = hexDigit(str[pos + 1]);
                        int    d2 = hexDigit(str[pos + 2]);
                        int    d3 = hexDigit(str[pos + 3]);
                        if (d0 < 0 || d1 < 0 || d2 < 0 || d3 < 0) {
                                return makeError<EUI64>(Error::Invalid);
                        }
                        bytes[g * 2] = static_cast<uint8_t>((d0 << 4) | d1);
                        bytes[g * 2 + 1] = static_cast<uint8_t>((d2 << 4) | d3);
                        if (g < 3) {
                                if (str[pos + 4] != ':') {
                                        return makeError<EUI64>(Error::Invalid);
                                }
                        }
                }
                return makeResult(EUI64(bytes));
        }

} // anonymous namespace

Result<EUI64> EUI64::fromString(const String &str) {
        if (str.size() == 23) return parseOctet(str);
        if (str.size() == 19) return parseIPv6(str);
        return makeError<EUI64>(Error::Invalid);
}

EUI64 EUI64::fromMacAddress(const MacAddress &mac) {
        // Modified EUI-64: insert FF:FE and invert U/L bit (bit 1 of byte 0)
        const MacAddress::DataFormat &m = mac.data();
        return EUI64(static_cast<uint8_t>(m[0] ^ 0x02), m[1], m[2], 0xFF, 0xFE, m[3], m[4], m[5]);
}

MacAddress EUI64::toMacAddress() const {
        if (!hasMacAddress()) return MacAddress();
        // Reverse modified EUI-64: remove FF:FE and invert U/L bit back
        return MacAddress(static_cast<uint8_t>(_addr[0] ^ 0x02), _addr[1], _addr[2], _addr[5], _addr[6], _addr[7]);
}

String EUI64::toString() const {
        return toString(EUI64Format::OctetHyphen);
}

String EUI64::toString(const EUI64Format &fmt) const {
        if (fmt == EUI64Format::IPv6) {
                // "aabb:ccdd:eeff:0011"
                return String::sprintf("%02x%02x:%02x%02x:%02x%02x:%02x%02x", _addr[0], _addr[1], _addr[2], _addr[3],
                                       _addr[4], _addr[5], _addr[6], _addr[7]);
        }
        char   sep = (fmt == EUI64Format::OctetColon) ? ':' : '-';
        String ret;
        for (int i = 0; i < 8; ++i) {
                if (i > 0) ret += sep;
                ret += String::number(_addr[i], 16, 2, '0').toLower();
        }
        return ret;
}

TextStream &operator<<(TextStream &stream, const EUI64 &addr) {
        stream << addr.toString();
        return stream;
}

PROMEKI_NAMESPACE_END
