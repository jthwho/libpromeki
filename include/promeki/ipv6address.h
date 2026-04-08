/**
 * @file      ipv6address.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstdint>
#include <cstring>
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/result.h>
#include <promeki/array.h>
#include <promeki/platform.h>

#if defined(PROMEKI_PLATFORM_WINDOWS)
#       include <winsock2.h>
#       include <ws2tcpip.h>
#elif !defined(PROMEKI_PLATFORM_EMSCRIPTEN)
#       include <netinet/in.h>
#endif

PROMEKI_NAMESPACE_BEGIN

class Ipv4Address;
class MacAddress;
class TextStream;

/**
 * @brief IPv6 network address.
 * @ingroup network
 *
 * Simple value type representing a 128-bit IPv6 address stored in
 * network byte order. Provides parsing (RFC 5952), formatting,
 * scope ID support, and classification (loopback, multicast, etc.).
 *
 * This class is purely computational and requires no platform-specific
 * headers or system calls.
 *
 * This class is not thread-safe. Concurrent access to a single
 * instance requires external synchronization.
 *
 * @par Example
 * @code
 * auto [addr, err] = Ipv6Address::fromString("fe80::1%eth0");
 * if(addr.isLinkLocal()) { ... }
 * String str = addr.toString();  // "fe80::1"
 * @endcode
 */
class Ipv6Address {
        public:
                /** @brief Raw 16-byte storage format for an IPv6 address. */
                using DataFormat = Array<uint8_t, 16>;

                /**
                 * @brief Parses an IPv6 address from colon-hex notation.
                 *
                 * Accepts standard colon-hex formats including "::" compression
                 * and optional "%scope" suffix. Also accepts IPv4-mapped notation
                 * (e.g. "::%%ffff:192.168.1.1").
                 *
                 * @param str The string to parse.
                 * @return A Result containing the parsed address and Error::Ok,
                 *         or a null address and Error::Invalid on parse failure.
                 */
                static Result<Ipv6Address> fromString(const String &str);

                /** @brief Returns the any address (::). */
                static Ipv6Address any() { return Ipv6Address(); }

                /** @brief Returns the loopback address (::1). */
                static Ipv6Address loopback();

                /** @brief Default constructor. Creates a null (all-zero) address. */
                Ipv6Address() : _addr{}, _scopeId(0) { }

                /**
                 * @brief Constructs from raw 16-byte data.
                 * @param bytes The 16 bytes of the IPv6 address in network byte order.
                 */
                explicit Ipv6Address(const DataFormat &bytes) : _addr(bytes), _scopeId(0) { }

                /**
                 * @brief Constructs from a raw byte pointer (copies 16 bytes).
                 * @param bytes Pointer to 16 bytes in network byte order.
                 */
                explicit Ipv6Address(const uint8_t *bytes) : _scopeId(0) {
                        for(size_t i = 0; i < 16; ++i) _addr[i] = bytes[i];
                }

                /** @brief Returns true if all bytes are zero and scope ID is zero. */
                bool isNull() const { return _addr.isZero() && _scopeId == 0; }

                /** @brief Returns true if this is the loopback address (::1). */
                bool isLoopback() const;

                /** @brief Returns true if this is a multicast address (ff00::/8). */
                bool isMulticast() const { return _addr[0] == 0xFF; }

                /** @brief Returns true if this is a link-local address (fe80::/10). */
                bool isLinkLocal() const { return _addr[0] == 0xFE && (_addr[1] & 0xC0) == 0x80; }

                /** @brief Returns true if this is an IPv4-mapped address (::%%ffff:0:0/96). */
                bool isV4Mapped() const;

                /** @brief Returns true if this is a site-local address (fec0::/10, deprecated). */
                bool isSiteLocal() const { return _addr[0] == 0xFE && (_addr[1] & 0xC0) == 0xC0; }

                /**
                 * @brief Returns the scope ID.
                 * @return The numeric scope ID, or 0 if not set.
                 */
                uint32_t scopeId() const { return _scopeId; }

                /**
                 * @brief Sets the scope ID.
                 * @param id The numeric scope ID.
                 */
                void setScopeId(uint32_t id) { _scopeId = id; }

                /**
                 * @brief Returns a const reference to the raw 16-byte data.
                 * @return The underlying DataFormat array.
                 */
                const DataFormat &data() const { return _addr; }

                /**
                 * @brief Returns a pointer to the raw byte data.
                 * @return A pointer to the first byte.
                 */
                const uint8_t *raw() const { return _addr.data(); }

                /**
                 * @brief Returns a canonical string representation.
                 *
                 * Uses "::" compression for the longest run of zero groups
                 * per RFC 5952. Does not include the scope ID suffix.
                 * @return A String like "fe80::1".
                 */
                String toString() const;

                /**
                 * @brief Extracts the IPv4 address from an IPv4-mapped IPv6 address.
                 *
                 * Only valid when isV4Mapped() is true. Returns a null
                 * Ipv4Address otherwise.
                 * @return The extracted Ipv4Address.
                 */
                Ipv4Address toIpv4() const;

                /**
                 * @brief Returns the Ethernet multicast MAC address for this IPv6 multicast address.
                 *
                 * Maps this address to its multicast MAC (33:33 + low 32 bits)
                 * per RFC 2464.
                 *
                 * @return The corresponding MacAddress, or a null MacAddress if this
                 *         address is not multicast.
                 */
                MacAddress multicastMac() const;

#if !defined(PROMEKI_PLATFORM_EMSCRIPTEN)
                /**
                 * @brief Constructs from a sockaddr_in6 structure.
                 * @param sa Pointer to a sockaddr_in6. Must have sin6_family == AF_INET6.
                 * @return A Result containing the address and Error::Ok, or
                 *         Error::Invalid if the pointer is null.
                 */
                static Result<Ipv6Address> fromSockAddr(const struct sockaddr_in6 *sa);

                /**
                 * @brief Fills a sockaddr_in6 with this address.
                 *
                 * Sets sin6_family, sin6_addr, and sin6_scope_id. The port
                 * field (sin6_port) is set to zero; callers should set it
                 * separately.
                 *
                 * @param[out] sa The structure to fill.
                 * @return Error::Ok on success, Error::Invalid if sa is null.
                 */
                Error toSockAddr(struct sockaddr_in6 *sa) const;
#endif

                /** @brief Returns true if both addresses are equal (includes scope ID). */
                bool operator==(const Ipv6Address &other) const {
                        return _addr == other._addr && _scopeId == other._scopeId;
                }
                /** @brief Returns true if the addresses are not equal. */
                bool operator!=(const Ipv6Address &other) const { return !(*this == other); }
                /** @brief Less-than comparison for ordering (lexicographic, then scope). */
                bool operator<(const Ipv6Address &other) const {
                        int cmp = std::memcmp(_addr.data(), other._addr.data(), 16);
                        if(cmp != 0) return cmp < 0;
                        return _scopeId < other._scopeId;
                }

        private:
                DataFormat      _addr;          ///< Address stored in network byte order.
                uint32_t        _scopeId;       ///< Numeric scope ID (0 if not set).
};

/** @brief Writes a colon-hex IPv6 address to the stream. */
TextStream &operator<<(TextStream &stream, const Ipv6Address &addr);

PROMEKI_NAMESPACE_END

PROMEKI_FORMAT_VIA_TOSTRING(promeki::Ipv6Address);
