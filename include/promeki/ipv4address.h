/**
 * @file      ipv4address.h
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
#include <promeki/platform.h>

#if defined(PROMEKI_PLATFORM_WINDOWS)
#       include <winsock2.h>
#       include <ws2tcpip.h>
#elif !defined(PROMEKI_PLATFORM_EMSCRIPTEN)
#       include <netinet/in.h>
#endif

PROMEKI_NAMESPACE_BEGIN

class Ipv6Address;
class MacAddress;
class TextStream;

/**
 * @brief IPv4 network address.
 * @ingroup network
 *
 * Simple value type representing a 32-bit IPv4 address stored in
 * network byte order (big-endian). Provides parsing, formatting,
 * subnet checks, and classification (loopback, multicast, private, etc.).
 *
 * This class is purely computational and requires no platform-specific
 * headers or system calls.
 *
 * This class is not thread-safe. Concurrent access to a single
 * instance requires external synchronization.
 *
 * @par Example
 * @code
 * auto [addr, err] = Ipv4Address::fromString("192.168.1.1");
 * if(addr.isPrivate()) { ... }
 * String str = addr.toString();  // "192.168.1.1"
 * @endcode
 */
class Ipv4Address {
        public:
                /**
                 * @brief Parses an IPv4 address from dotted-quad notation.
                 * @param str The string to parse (e.g. "192.168.1.1").
                 * @return A Result containing the parsed address and Error::Ok,
                 *         or a null address and Error::Invalid on parse failure.
                 */
                static Result<Ipv4Address> fromString(const String &str);

                /**
                 * @brief Creates an address from a 32-bit integer in network byte order.
                 * @param networkOrder The address in network byte order.
                 * @return The corresponding Ipv4Address.
                 */
                static Ipv4Address fromUint32(uint32_t networkOrder) {
                        return Ipv4Address(networkOrder);
                }

                /** @brief Returns the INADDR_ANY address (0.0.0.0). */
                static Ipv4Address any() { return Ipv4Address(); }

                /** @brief Returns the loopback address (127.0.0.1). */
                static Ipv4Address loopback() { return Ipv4Address(127, 0, 0, 1); }

                /** @brief Returns the broadcast address (255.255.255.255). */
                static Ipv4Address broadcast() { return Ipv4Address(255, 255, 255, 255); }

                /** @brief Default constructor. Creates a null (0.0.0.0) address. */
                Ipv4Address() : _addr(0) { }

                /**
                 * @brief Constructs from a 32-bit integer in network byte order.
                 * @param networkOrder The address in network byte order (big-endian).
                 */
                explicit Ipv4Address(uint32_t networkOrder) : _addr(networkOrder) { }

                /**
                 * @brief Constructs from four octets.
                 * @param a First octet (most significant).
                 * @param b Second octet.
                 * @param c Third octet.
                 * @param d Fourth octet (least significant).
                 */
                Ipv4Address(uint8_t a, uint8_t b, uint8_t c, uint8_t d) :
                        _addr(static_cast<uint32_t>(a) << 24 |
                              static_cast<uint32_t>(b) << 16 |
                              static_cast<uint32_t>(c) << 8  |
                              static_cast<uint32_t>(d))
                {
                }

                /** @brief Returns true if the address is 0.0.0.0. */
                bool isNull() const { return _addr == 0; }

                /** @brief Returns true if the address is in the 127.0.0.0/8 range. */
                bool isLoopback() const { return octet(0) == 127; }

                /** @brief Returns true if the address is in the 224.0.0.0/4 multicast range. */
                bool isMulticast() const { return (octet(0) & 0xF0) == 0xE0; }

                /** @brief Returns true if the address is in the 169.254.0.0/16 link-local range. */
                bool isLinkLocal() const { return octet(0) == 169 && octet(1) == 254; }

                /**
                 * @brief Returns true if the address is in a private range.
                 *
                 * Checks for 10.0.0.0/8, 172.16.0.0/12, and 192.168.0.0/16.
                 */
                bool isPrivate() const {
                        uint8_t a = octet(0);
                        uint8_t b = octet(1);
                        if(a == 10) return true;
                        if(a == 172 && (b & 0xF0) == 16) return true;
                        if(a == 192 && b == 168) return true;
                        return false;
                }

                /** @brief Returns true if the address is 255.255.255.255. */
                bool isBroadcast() const { return _addr == 0xFFFFFFFF; }

                /**
                 * @brief Returns true if this address is within the given subnet.
                 * @param network The network address.
                 * @param mask The subnet mask.
                 * @return True if (this & mask) == (network & mask).
                 */
                bool isInSubnet(Ipv4Address network, Ipv4Address mask) const {
                        return (_addr & mask._addr) == (network._addr & mask._addr);
                }

                /**
                 * @brief Returns true if this address is within the given subnet.
                 * @param network The network address.
                 * @param prefixLen The prefix length (0-32).
                 * @return True if the address matches the network prefix.
                 */
                bool isInSubnet(Ipv4Address network, int prefixLen) const;

                /**
                 * @brief Returns the address as a 32-bit integer in network byte order.
                 * @return The raw 32-bit address (big-endian).
                 */
                uint32_t toUint32() const { return _addr; }

                /**
                 * @brief Returns a single octet of the address.
                 * @param index Octet index (0 = most significant, 3 = least significant).
                 * @return The octet value, or 0 if index is out of range.
                 */
                uint8_t octet(int index) const {
                        if(index < 0 || index > 3) return 0;
                        return static_cast<uint8_t>((_addr >> (24 - index * 8)) & 0xFF);
                }

                /**
                 * @brief Returns a dotted-quad string representation.
                 * @return A String like "192.168.1.1".
                 */
                String toString() const;

                /**
                 * @brief Converts to an IPv4-mapped IPv6 address (::%%ffff:a.b.c.d).
                 * @return The corresponding Ipv6Address.
                 */
                Ipv6Address toIpv6Mapped() const;

                /**
                 * @brief Returns the Ethernet multicast MAC address for this IPv4 multicast address.
                 *
                 * Maps this address to its IANA multicast MAC (01:00:5e + low 23 bits)
                 * per RFC 1112. Only 23 of the 28 multicast group bits are used, so
                 * 32 different multicast IPs share each MAC address. Use this to check
                 * for multicast MAC collisions:
                 * @code
                 * if(addrA.multicastMac() == addrB.multicastMac()) { ... }
                 * @endcode
                 *
                 * @return The corresponding MacAddress, or a null MacAddress if this
                 *         address is not multicast.
                 */
                MacAddress multicastMac() const;

#if !defined(PROMEKI_PLATFORM_EMSCRIPTEN)
                /**
                 * @brief Constructs from a sockaddr_in structure.
                 * @param sa Pointer to a sockaddr_in. Must have sin_family == AF_INET.
                 * @return A Result containing the address and Error::Ok, or
                 *         Error::Invalid if the pointer is null.
                 */
                static Result<Ipv4Address> fromSockAddr(const struct sockaddr_in *sa);

                /**
                 * @brief Fills a sockaddr_in with this address.
                 *
                 * Sets sin_family to AF_INET and sin_addr. The port field
                 * (sin_port) is set to zero; callers should set it separately.
                 *
                 * @param[out] sa The structure to fill.
                 * @return Error::Ok on success, Error::Invalid if sa is null.
                 */
                Error toSockAddr(struct sockaddr_in *sa) const;
#endif

                /** @brief Returns true if both addresses are equal. */
                bool operator==(const Ipv4Address &other) const { return _addr == other._addr; }
                /** @brief Returns true if the addresses are not equal. */
                bool operator!=(const Ipv4Address &other) const { return _addr != other._addr; }
                /** @brief Less-than comparison for ordering. */
                bool operator<(const Ipv4Address &other) const { return _addr < other._addr; }

        private:
                uint32_t _addr;         ///< Address stored in big-endian (network) byte order.
};

/** @brief Writes a dotted-quad IPv4 address to the stream. */
TextStream &operator<<(TextStream &stream, const Ipv4Address &addr);

PROMEKI_NAMESPACE_END
