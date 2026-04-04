/**
 * @file      macaddress.h
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

PROMEKI_NAMESPACE_BEGIN

class Ipv4Address;
class Ipv6Address;
class TextStream;

/**
 * @brief IEEE 802 MAC (Ethernet hardware) address.
 * @ingroup network
 *
 * Simple value type representing a 48-bit MAC address. Provides
 * parsing, formatting, and classification (broadcast, multicast,
 * locally-administered).
 *
 * This class is purely computational and requires no platform-specific
 * headers or system calls.
 *
 * This class is not thread-safe. Concurrent access to a single
 * instance requires external synchronization.
 *
 * @par Example
 * @code
 * auto [mac, err] = MacAddress::fromString("aa:bb:cc:dd:ee:ff");
 * if(mac.isMulticast()) { ... }
 * String str = mac.toString();  // "aa:bb:cc:dd:ee:ff"
 * @endcode
 */
class MacAddress {
        public:
                /** @brief Raw 6-byte storage format for a MAC address. */
                using DataFormat = Array<uint8_t, 6>;

                /**
                 * @brief Parses a MAC address from a string.
                 *
                 * Accepts colon-separated ("aa:bb:cc:dd:ee:ff") or
                 * hyphen-separated ("aa-bb-cc-dd-ee-ff") hexadecimal notation.
                 * Case-insensitive.
                 *
                 * @param str The string to parse.
                 * @return A Result containing the parsed address and Error::Ok,
                 *         or a null address and Error::Invalid on parse failure.
                 */
                static Result<MacAddress> fromString(const String &str);

                /** @brief Returns the Ethernet broadcast address (ff:ff:ff:ff:ff:ff). */
                static MacAddress broadcast();

                /**
                 * @brief Computes the multicast MAC address for an IPv4 multicast address.
                 *
                 * Maps an IPv4 multicast address (224.0.0.0/4) to its corresponding
                 * Ethernet multicast MAC address per IANA (RFC 1112). The mapping uses
                 * the OUI 01:00:5e and the low-order 23 bits of the IPv4 address,
                 * meaning that 32 different multicast IP addresses map to each MAC.
                 *
                 * @param addr An IPv4 multicast address.
                 * @return The corresponding MAC address, or a null MAC if addr is not multicast.
                 */
                static MacAddress fromIpv4Multicast(const Ipv4Address &addr);

                /**
                 * @brief Computes the multicast MAC address for an IPv6 multicast address.
                 *
                 * Maps an IPv6 multicast address (ff00::/8) to its corresponding
                 * Ethernet multicast MAC address per RFC 2464. The mapping uses
                 * the prefix 33:33 and the low-order 32 bits of the IPv6 address.
                 *
                 * @param addr An IPv6 multicast address.
                 * @return The corresponding MAC address, or a null MAC if addr is not multicast.
                 */
                static MacAddress fromIpv6Multicast(const Ipv6Address &addr);

                /** @brief Default constructor. Creates a null (00:00:00:00:00:00) address. */
                MacAddress() : _addr{} { }

                /**
                 * @brief Constructs from raw 6-byte data.
                 * @param bytes The 6 bytes of the MAC address.
                 */
                explicit MacAddress(const DataFormat &bytes) : _addr(bytes) { }

                /**
                 * @brief Constructs from six individual octets.
                 * @param a First octet.
                 * @param b Second octet.
                 * @param c Third octet.
                 * @param d Fourth octet.
                 * @param e Fifth octet.
                 * @param f Sixth octet.
                 */
                MacAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d, uint8_t e, uint8_t f) :
                        _addr{DataFormat{std::array<uint8_t, 6>{a, b, c, d, e, f}}}
                {
                }

                /** @brief Returns true if all bytes are zero. */
                bool isNull() const { return _addr.isZero(); }

                /**
                 * @brief Returns true if this is the Ethernet broadcast address (ff:ff:ff:ff:ff:ff).
                 *
                 * This is the Layer 2 broadcast address used for both Ethernet broadcast
                 * frames and IP-level broadcast (255.255.255.255). Note that the broadcast
                 * address also has the group/multicast bit set, so isMulticast() returns
                 * true for the broadcast address.
                 */
                bool isBroadcast() const {
                        for(size_t i = 0; i < 6; ++i) {
                                if(_addr[i] != 0xFF) return false;
                        }
                        return true;
                }

                /**
                 * @brief Returns true if this is a group (multicast) address.
                 *
                 * A MAC address is a group address when the least-significant bit of
                 * the first octet is set (IEEE 802 I/G bit). This includes both
                 * multicast addresses and the broadcast address (ff:ff:ff:ff:ff:ff).
                 * Use isBroadcast() to distinguish, or isGroupMulticast() to exclude
                 * broadcast.
                 */
                bool isMulticast() const { return (_addr[0] & 0x01) != 0; }

                /**
                 * @brief Returns true if this is a group multicast address but NOT broadcast.
                 *
                 * Returns true when the I/G bit is set and the address is not
                 * ff:ff:ff:ff:ff:ff. This is the typical check for "is this a
                 * multicast destination" in the practical sense.
                 */
                bool isGroupMulticast() const { return isMulticast() && !isBroadcast(); }

                /**
                 * @brief Returns true if this is an IPv4 multicast MAC address.
                 *
                 * IPv4 multicast maps to the IANA OUI 01:00:5e with the high bit
                 * of the fourth octet clear, giving the range 01:00:5e:00:00:00
                 * through 01:00:5e:7f:ff:ff (RFC 1112).
                 */
                bool isIpv4Multicast() const {
                        return _addr[0] == 0x01 && _addr[1] == 0x00 &&
                               _addr[2] == 0x5E && (_addr[3] & 0x80) == 0;
                }

                /**
                 * @brief Returns true if this is an IPv6 multicast MAC address.
                 *
                 * IPv6 multicast maps to the prefix 33:33 followed by the low-order
                 * 32 bits of the IPv6 address (RFC 2464).
                 */
                bool isIpv6Multicast() const {
                        return _addr[0] == 0x33 && _addr[1] == 0x33;
                }

                /**
                 * @brief Returns true if this is a unicast address.
                 *
                 * A unicast address has the I/G bit clear (not multicast, not broadcast).
                 */
                bool isUnicast() const { return !isMulticast(); }

                /**
                 * @brief Returns true if this is a locally-administered address.
                 *
                 * The second-least-significant bit of the first octet indicates
                 * local administration.
                 */
                bool isLocallyAdministered() const { return (_addr[0] & 0x02) != 0; }

                /**
                 * @brief Returns a const reference to the raw 6-byte data.
                 * @return The underlying DataFormat array.
                 */
                const DataFormat &data() const { return _addr; }

                /**
                 * @brief Returns a pointer to the raw byte data.
                 * @return A pointer to the first byte.
                 */
                const uint8_t *raw() const { return _addr.data(); }

                /**
                 * @brief Returns a single octet of the address.
                 * @param index Octet index (0-5).
                 * @return The octet value, or 0 if index is out of range.
                 */
                uint8_t octet(int index) const {
                        if(index < 0 || index > 5) return 0;
                        return _addr[index];
                }

                /**
                 * @brief Returns a colon-separated hex string.
                 * @return A String like "aa:bb:cc:dd:ee:ff" (lowercase).
                 */
                String toString() const;

                /**
                 * @brief Returns a hex string with a custom separator.
                 * @param separator The character to place between octets.
                 * @return A String like "aa-bb-cc-dd-ee-ff".
                 */
                String toString(char separator) const;

                /** @brief Returns true if both addresses are equal. */
                bool operator==(const MacAddress &other) const { return _addr == other._addr; }
                /** @brief Returns true if the addresses are not equal. */
                bool operator!=(const MacAddress &other) const { return _addr != other._addr; }
                /** @brief Less-than comparison for ordering (lexicographic). */
                bool operator<(const MacAddress &other) const {
                        return std::memcmp(_addr.data(), other._addr.data(), 6) < 0;
                }

        private:
                DataFormat _addr;               ///< Raw 6-byte MAC address.
};

/** @brief Writes a colon-separated MAC address to the stream. */
TextStream &operator<<(TextStream &stream, const MacAddress &addr);

PROMEKI_NAMESPACE_END
