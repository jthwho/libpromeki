/**
 * @file      ipv4address.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/datastream.h>
#include <promeki/ipv4address.h>
#include <promeki/ipv6address.h>
#include <promeki/macaddress.h>
#include <promeki/textstream.h>

PROMEKI_NAMESPACE_BEGIN

Result<Ipv4Address> Ipv4Address::fromString(const String &str) {
        if (str.isEmpty()) return makeError<Ipv4Address>(Error::Invalid);

        uint8_t      octets[4] = {};
        int          octetIndex = 0;
        unsigned int value = 0;
        bool         hasDigit = false;

        for (size_t i = 0; i < str.size(); ++i) {
                char c = str[i];
                if (c >= '0' && c <= '9') {
                        value = value * 10 + static_cast<unsigned int>(c - '0');
                        if (value > 255) return makeError<Ipv4Address>(Error::Invalid);
                        hasDigit = true;
                } else if (c == '.') {
                        if (!hasDigit || octetIndex >= 3) {
                                return makeError<Ipv4Address>(Error::Invalid);
                        }
                        octets[octetIndex++] = static_cast<uint8_t>(value);
                        value = 0;
                        hasDigit = false;
                } else {
                        return makeError<Ipv4Address>(Error::Invalid);
                }
        }

        if (!hasDigit || octetIndex != 3) return makeError<Ipv4Address>(Error::Invalid);
        octets[3] = static_cast<uint8_t>(value);

        return makeResult(Ipv4Address(octets[0], octets[1], octets[2], octets[3]));
}

bool Ipv4Address::isInSubnet(Ipv4Address network, int prefixLen) const {
        if (prefixLen <= 0) return true;
        if (prefixLen >= 32) return _addr == network._addr;
        uint32_t mask = 0xFFFFFFFF << (32 - prefixLen);
        return (_addr & mask) == (network._addr & mask);
}

String Ipv4Address::toString() const {
        return String::number(octet(0)) + "." + String::number(octet(1)) + "." + String::number(octet(2)) + "." +
               String::number(octet(3));
}

MacAddress Ipv4Address::multicastMac() const {
        return MacAddress::fromIpv4Multicast(*this);
}

Ipv6Address Ipv4Address::toIpv6Mapped() const {
        Ipv6Address::DataFormat bytes{};
        bytes[10] = 0xFF;
        bytes[11] = 0xFF;
        bytes[12] = octet(0);
        bytes[13] = octet(1);
        bytes[14] = octet(2);
        bytes[15] = octet(3);
        return Ipv6Address(bytes);
}

#if !defined(PROMEKI_PLATFORM_EMSCRIPTEN)
Result<Ipv4Address> Ipv4Address::fromSockAddr(const struct sockaddr_in *sa) {
        if (sa == nullptr) return makeError<Ipv4Address>(Error::Invalid);
        return makeResult(Ipv4Address(ntohl(sa->sin_addr.s_addr)));
}

Error Ipv4Address::toSockAddr(struct sockaddr_in *sa) const {
        if (sa == nullptr) return Error::Invalid;
        std::memset(sa, 0, sizeof(struct sockaddr_in));
        sa->sin_family = AF_INET;
        sa->sin_addr.s_addr = htonl(_addr);
        return Error();
}
#endif

TextStream &operator<<(TextStream &stream, const Ipv4Address &addr) {
        stream << addr.toString();
        return stream;
}

// ============================================================================
// DataStream wire format (v1: 4 bytes, network byte order).
//
// @c _addr is stored in host-endian form with the MSB carrying the
// network-most-significant octet — emitting it through DataStream's
// big-endian @c uint32_t operator yields the canonical four octets in
// network byte order regardless of host endianness.
// ============================================================================

Error Ipv4Address::writeToStream(DataStream &s) const {
        s << static_cast<uint32_t>(_addr);
        return s.status() == DataStream::Ok ? Error::Ok : s.toError();
}

template <> Result<Ipv4Address> Ipv4Address::readFromStream<1>(DataStream &s) {
        uint32_t addr = 0;
        s >> addr;
        if (s.status() != DataStream::Ok) return makeError<Ipv4Address>(s.toError());
        return makeResult(Ipv4Address::fromUint32(addr));
}

PROMEKI_NAMESPACE_END
