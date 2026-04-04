/**
 * @file      ipv6address.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/ipv6address.h>
#include <promeki/ipv4address.h>
#include <promeki/macaddress.h>
#include <promeki/textstream.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

int hexDigit(char c) {
        if(c >= '0' && c <= '9') return c - '0';
        if(c >= 'a' && c <= 'f') return c - 'a' + 10;
        if(c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
}

// Parse a single 16-bit hex group. Returns -1 on failure.
int parseGroup(const String &str, size_t start, size_t end) {
        if(start == end) return -1;
        if(end - start > 4) return -1;
        int val = 0;
        for(size_t i = start; i < end; ++i) {
                int d = hexDigit(str[i]);
                if(d < 0) return -1;
                val = (val << 4) | d;
        }
        return val;
}

} // anonymous namespace

Result<Ipv6Address> Ipv6Address::fromString(const String &str) {
        if(str.isEmpty()) return makeError<Ipv6Address>(Error::Invalid);

        String addrPart = str;
        uint32_t scopeId = 0;

        // Strip scope ID suffix (%...)
        size_t pctPos = str.find('%');
        if(pctPos != String::npos) {
                addrPart = str.left(pctPos);
                Error err;
                scopeId = static_cast<uint32_t>(str.mid(pctPos + 1).toUInt(&err));
                if(err.isError()) return makeError<Ipv6Address>(Error::Invalid);
        }

        // Check for IPv4-mapped suffix (e.g. "::ffff:192.168.1.1")
        // Find last colon, then check if the part after it contains a dot
        size_t lastColon = addrPart.rfind(':');
        if(lastColon != String::npos) {
                String tail = addrPart.mid(lastColon + 1);
                if(tail.contains('.')) {
                        // Parse the IPv4 part
                        auto [v4, v4err] = Ipv4Address::fromString(tail);
                        if(v4err.isError()) return makeError<Ipv6Address>(Error::Invalid);

                        // Parse the prefix (everything up to and including the last colon)
                        // Replace the IPv4 part with two hex groups
                        String prefix = addrPart.left(lastColon + 1);
                        uint32_t v4n = v4.toUint32();
                        String hexSuffix = String::number(static_cast<uint16_t>(v4n >> 16), 16) + ":" +
                                           String::number(static_cast<uint16_t>(v4n & 0xFFFF), 16);
                        addrPart = prefix + hexSuffix;
                }
        }

        // Now parse standard colon-hex with :: compression
        uint16_t groups[8] = {};
        int groupCount = 0;
        int doubleColonPos = -1;

        size_t pos = 0;
        size_t len = addrPart.size();

        // Handle leading ::
        if(len >= 2 && addrPart[0] == ':' && addrPart[1] == ':') {
                doubleColonPos = 0;
                pos = 2;
                if(pos == len) {
                        // "::" alone = all zeros
                        Ipv6Address result;
                        result.setScopeId(scopeId);
                        return makeResult(result);
                }
        }

        while(pos < len && groupCount < 8) {
                // Find the end of this group
                size_t groupStart = pos;
                while(pos < len && addrPart[pos] != ':') ++pos;

                int val = parseGroup(addrPart, groupStart, pos);
                if(val < 0) return makeError<Ipv6Address>(Error::Invalid);
                groups[groupCount++] = static_cast<uint16_t>(val);

                if(pos < len) {
                        // We're at a ':'
                        ++pos;
                        if(pos < len && addrPart[pos] == ':') {
                                // Double colon
                                if(doubleColonPos >= 0) {
                                        // Only one :: allowed
                                        return makeError<Ipv6Address>(Error::Invalid);
                                }
                                doubleColonPos = groupCount;
                                ++pos;
                        }
                }
        }

        // Expand :: into the correct number of zero groups
        DataFormat bytes{};
        if(doubleColonPos >= 0) {
                int zerosNeeded = 8 - groupCount;
                if(zerosNeeded < 0) return makeError<Ipv6Address>(Error::Invalid);

                int outIndex = 0;
                for(int i = 0; i < doubleColonPos; ++i) {
                        bytes[outIndex * 2]     = static_cast<uint8_t>(groups[i] >> 8);
                        bytes[outIndex * 2 + 1] = static_cast<uint8_t>(groups[i] & 0xFF);
                        ++outIndex;
                }
                outIndex += zerosNeeded; // skip zero groups (already zero-initialized)
                for(int i = doubleColonPos; i < groupCount; ++i) {
                        bytes[outIndex * 2]     = static_cast<uint8_t>(groups[i] >> 8);
                        bytes[outIndex * 2 + 1] = static_cast<uint8_t>(groups[i] & 0xFF);
                        ++outIndex;
                }
        } else {
                if(groupCount != 8) return makeError<Ipv6Address>(Error::Invalid);
                for(int i = 0; i < 8; ++i) {
                        bytes[i * 2]     = static_cast<uint8_t>(groups[i] >> 8);
                        bytes[i * 2 + 1] = static_cast<uint8_t>(groups[i] & 0xFF);
                }
        }

        Ipv6Address result(bytes);
        result.setScopeId(scopeId);
        return makeResult(result);
}

Ipv6Address Ipv6Address::loopback() {
        DataFormat bytes{};
        bytes[15] = 1;
        return Ipv6Address(bytes);
}

bool Ipv6Address::isLoopback() const {
        for(int i = 0; i < 15; ++i) {
                if(_addr[i] != 0) return false;
        }
        return _addr[15] == 1;
}

bool Ipv6Address::isV4Mapped() const {
        for(int i = 0; i < 10; ++i) {
                if(_addr[i] != 0) return false;
        }
        return _addr[10] == 0xFF && _addr[11] == 0xFF;
}

String Ipv6Address::toString() const {
        // Convert to 8 groups
        uint16_t groups[8];
        for(int i = 0; i < 8; ++i) {
                groups[i] = static_cast<uint16_t>(
                        (static_cast<uint16_t>(_addr[i * 2]) << 8) | _addr[i * 2 + 1]
                );
        }

        // Find the longest run of consecutive zero groups for :: compression
        int bestStart = -1, bestLen = 0;
        int curStart = -1, curLen = 0;
        for(int i = 0; i < 8; ++i) {
                if(groups[i] == 0) {
                        if(curStart < 0) curStart = i;
                        ++curLen;
                } else {
                        if(curLen > bestLen) {
                                bestStart = curStart;
                                bestLen = curLen;
                        }
                        curStart = -1;
                        curLen = 0;
                }
        }
        if(curLen > bestLen) {
                bestStart = curStart;
                bestLen = curLen;
        }
        // Per RFC 5952: only use :: for runs of 2+ zero groups
        if(bestLen < 2) bestStart = -1;

        String ret;
        for(int i = 0; i < 8; ++i) {
                if(i == bestStart) {
                        ret += "::";
                        i += bestLen - 1;
                        continue;
                }
                if(i > 0 && !(i == bestStart + bestLen && bestStart >= 0)) {
                        // Add colon if not right after ::
                        if(ret.isEmpty() || ret[ret.size() - 1] != ':') {
                                ret += ":";
                        }
                }
                ret += String::number(groups[i], 16).toLower();
        }

        // Edge case: if we end with ::, the string already has it
        if(ret.isEmpty()) ret = "::";

        return ret;
}

MacAddress Ipv6Address::multicastMac() const {
        return MacAddress::fromIpv6Multicast(*this);
}

Ipv4Address Ipv6Address::toIpv4() const {
        if(!isV4Mapped()) return Ipv4Address();
        return Ipv4Address(_addr[12], _addr[13], _addr[14], _addr[15]);
}

#if !defined(PROMEKI_PLATFORM_EMSCRIPTEN)
Result<Ipv6Address> Ipv6Address::fromSockAddr(const struct sockaddr_in6 *sa) {
        if(sa == nullptr) return makeError<Ipv6Address>(Error::Invalid);
        Ipv6Address addr(sa->sin6_addr.s6_addr);
        addr.setScopeId(sa->sin6_scope_id);
        return makeResult(addr);
}

Error Ipv6Address::toSockAddr(struct sockaddr_in6 *sa) const {
        if(sa == nullptr) return Error::Invalid;
        std::memset(sa, 0, sizeof(struct sockaddr_in6));
        sa->sin6_family = AF_INET6;
        std::memcpy(&sa->sin6_addr, _addr.data(), 16);
        sa->sin6_scope_id = _scopeId;
        return Error();
}
#endif

TextStream &operator<<(TextStream &stream, const Ipv6Address &addr) {
        stream << addr.toString();
        return stream;
}

PROMEKI_NAMESPACE_END
