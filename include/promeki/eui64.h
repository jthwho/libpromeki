/**
 * @file      eui64.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstdint>
#include <cstring>
#include <format>
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/result.h>
#include <promeki/array.h>
#include <promeki/enums.h>

PROMEKI_NAMESPACE_BEGIN

class MacAddress;
class TextStream;

/**
 * @brief IEEE EUI-64 extended unique identifier.
 * @ingroup network
 *
 * Simple value type representing a 64-bit (8-byte) EUI-64 address.
 * Used for PTP grandmaster clock identities, IPv6 interface IDs,
 * and other IEEE 64-bit identifiers.
 *
 * Provides parsing and formatting in multiple notations
 * (see @ref EUI64Format), as well as conversion to/from 48-bit
 * MacAddress via the modified EUI-64 transform (FF:FE insertion
 * with U/L bit inversion) defined by IEEE and used in IPv6 / PTP.
 *
 * @par Thread Safety
 * Distinct instances may be used concurrently.  A single instance is
 * conditionally thread-safe: const operations may be called from
 * multiple threads, but any mutation must be externally synchronized.
 *
 * @par Example
 * @code
 * auto [eui, err] = EUI64::fromString("02-1a-2b-ff-fe-3c-4d-5e");
 * assert(eui.hasMacAddress());
 * MacAddress mac = eui.toMacAddress();  // 00:1a:2b:3c:4d:5e
 *
 * EUI64 back = EUI64::fromMacAddress(mac);
 * assert(back == eui);
 *
 * // IPv6 format
 * String ipv6 = eui.toString(EUI64Format::IPv6);  // "021a:2bff:fe3c:4d5e"
 * @endcode
 */
class EUI64 {
        public:
                /** @brief Raw 8-byte storage format. */
                using DataFormat = Array<uint8_t, 8>;

                /**
                 * @brief Parses an EUI-64 from a string.
                 *
                 * Accepts three formats:
                 * - Octet hyphen: `"aa-bb-cc-dd-ee-ff-00-11"` (23 chars)
                 * - Octet colon:  `"aa:bb:cc:dd:ee:ff:00:11"` (23 chars)
                 * - IPv6 groups:  `"aabb:ccdd:eeff:0011"`     (19 chars)
                 *
                 * Case-insensitive.
                 *
                 * @param str The string to parse.
                 * @return The parsed EUI-64 and an error code.
                 */
                static Result<EUI64> fromString(const String &str);

                /**
                 * @brief Converts a 48-bit MAC address to modified EUI-64.
                 *
                 * Inserts FF:FE between the OUI (bytes 0-2) and the
                 * NIC-specific part (bytes 3-5), then inverts the U/L
                 * bit (bit 1 of byte 0) per IEEE modified EUI-64.
                 *
                 * @param mac The MAC address to convert.
                 * @return The corresponding modified EUI-64.
                 */
                static EUI64 fromMacAddress(const MacAddress &mac);

                /** @brief Default constructor.  Creates a null (all-zero) identifier. */
                EUI64() : _addr{} { }

                /**
                 * @brief Constructs from raw 8-byte data.
                 * @param bytes The 8 bytes of the identifier.
                 */
                explicit EUI64(const DataFormat &bytes) : _addr(bytes) { }

                /**
                 * @brief Constructs from eight individual octets.
                 */
                EUI64(uint8_t a, uint8_t b, uint8_t c, uint8_t d,
                      uint8_t e, uint8_t f, uint8_t g, uint8_t h) :
                        _addr{DataFormat{std::array<uint8_t, 8>{a, b, c, d, e, f, g, h}}}
                {
                }

                /** @brief Returns true if all bytes are zero. */
                bool isNull() const { return _addr.isZero(); }

                /**
                 * @brief Returns a const reference to the raw 8-byte data.
                 * @return The underlying DataFormat array.
                 */
                const DataFormat &data() const { return _addr; }

                /**
                 * @brief Returns a pointer to the raw byte data.
                 * @return A pointer to the first byte.
                 */
                const uint8_t *raw() const { return _addr.data(); }

                /**
                 * @brief Returns a single octet of the identifier.
                 * @param index Octet index (0-7).
                 * @return The octet value, or 0 if index is out of range.
                 */
                uint8_t octet(int index) const {
                        if(index < 0 || index > 7) return 0;
                        return _addr[index];
                }

                /**
                 * @brief Returns true if this EUI-64 was derived from a MAC address.
                 *
                 * A MAC-derived modified EUI-64 has bytes 3-4 set to FF:FE.
                 *
                 * @return True if convertible back to MacAddress.
                 */
                bool hasMacAddress() const {
                        return _addr[3] == 0xFF && _addr[4] == 0xFE;
                }

                /**
                 * @brief Extracts the original 48-bit MAC address.
                 *
                 * Reverses the modified EUI-64 transform: removes the
                 * FF:FE padding and inverts the U/L bit back.  Only
                 * valid when hasMacAddress() returns true; returns a
                 * null MacAddress otherwise.
                 *
                 * @return The extracted MacAddress.
                 */
                MacAddress toMacAddress() const;

                /**
                 * @brief Returns a string in the default format (OctetHyphen).
                 * @return A String like "aa-bb-cc-dd-ee-ff-00-11" (lowercase).
                 */
                String toString() const;

                /**
                 * @brief Returns a string in the specified format.
                 * @param fmt The format to use.
                 * @return The formatted string.
                 */
                String toString(const EUI64Format &fmt) const;

                /** @brief Returns true if both identifiers are equal. */
                bool operator==(const EUI64 &other) const { return _addr == other._addr; }
                /** @brief Returns true if the identifiers are not equal. */
                bool operator!=(const EUI64 &other) const { return _addr != other._addr; }
                /** @brief Less-than comparison for ordering (lexicographic). */
                bool operator<(const EUI64 &other) const {
                        return std::memcmp(_addr.data(), other._addr.data(), 8) < 0;
                }

        private:
                DataFormat _addr;
};

/** @brief Writes a hyphen-separated EUI-64 to the stream. */
TextStream &operator<<(TextStream &stream, const EUI64 &addr);

PROMEKI_NAMESPACE_END

/**
 * @brief @c std::formatter for @ref promeki::EUI64 with format selection.
 *
 * Format specifiers (placed after the colon in a format field):
 * - @c {} or @c {:h} — OctetHyphen: @c "aa-bb-cc-dd-ee-ff-00-11"
 * - @c {:o}          — OctetColon:  @c "aa:bb:cc:dd:ee:ff:00:11"
 * - @c {:v}          — IPv6:        @c "aabb:ccdd:eeff:0011"
 *
 * @code
 * EUI64 eui(0x02, 0x1A, 0x2B, 0xFF, 0xFE, 0x3C, 0x4D, 0x5E);
 * String::format("{}", eui);    // "02-1a-2b-ff-fe-3c-4d-5e"
 * String::format("{:o}", eui);  // "02:1a:2b:ff:fe:3c:4d:5e"
 * String::format("{:v}", eui);  // "021a:2bff:fe3c:4d5e"
 * @endcode
 */
template <>
struct std::formatter<promeki::EUI64> {
        int _spec = 0; // 0=hyphen, 1=colon, 2=ipv6

        constexpr auto parse(std::format_parse_context &ctx) {
                auto it = ctx.begin();
                if(it != ctx.end() && *it != '}') {
                        if(*it == 'h')      { _spec = 0; ++it; }
                        else if(*it == 'o') { _spec = 1; ++it; }
                        else if(*it == 'v') { _spec = 2; ++it; }
                }
                return it;
        }

        template <typename FormatContext>
        auto format(const promeki::EUI64 &v, FormatContext &ctx) const {
                promeki::EUI64Format fmt(_spec);
                promeki::String s = v.toString(fmt);
                return std::format_to(ctx.out(), "{}",
                        std::string_view(s.cstr(), s.byteCount()));
        }
};
