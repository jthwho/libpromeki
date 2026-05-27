/**
 * @file      ipv6subnet.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_NETWORK
#include <promeki/namespace.h>
#include <promeki/ipv6address.h>
#include <promeki/list.h>
#include <promeki/string.h>
#include <promeki/result.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief IPv6 address paired with its CIDR prefix length.
 * @ingroup network
 *
 * Captures one of the @c (address, prefix) tuples a network
 * interface advertises.  IPv6 prefixes are always contiguous, so a
 * plain @c prefixLen suffices — there is no IPv4-style legacy mask
 * to round-trip.
 *
 * @par Example
 * @code
 * Ipv6Subnet s = Ipv6Subnet::fromString("2001:db8::1/64").first();
 * assert(s.prefixLen() == 64);
 * assert(s.contains(Ipv6Subnet::fromString("2001:db8::abcd").first().address()));
 * @endcode
 */
class Ipv6Subnet {
        public:
                /** @brief List of IPv6 subnets. */
                using List = ::promeki::List<Ipv6Subnet>;

                /**
                 * @brief Parses a CIDR-formatted subnet string.
                 *
                 * Accepts @c "2001:db8::1/64" or a plain
                 * @c "2001:db8::1" (treated as @c /128).
                 *
                 * @param str The string to parse.
                 * @return A Result holding the parsed subnet and
                 *         Error::Ok, or an empty subnet and
                 *         Error::Invalid on parse failure.
                 */
                static Result<Ipv6Subnet> fromString(const String &str);

                /** @brief Constructs an invalid (null) subnet. */
                Ipv6Subnet() = default;

                /**
                 * @brief Constructs a subnet from address and prefix length.
                 * @param addr The interface address.
                 * @param prefixLen Prefix length in bits, 0–128.
                 */
                Ipv6Subnet(const Ipv6Address &addr, int prefixLen) : _addr(addr), _prefixLen(prefixLen) {
                        if (_prefixLen < 0) _prefixLen = 0;
                        if (_prefixLen > 128) _prefixLen = 128;
                }

                /** @brief Returns true if the address bytes are not all zero. */
                bool isValid() const;

                /** @brief Returns the interface address. */
                const Ipv6Address &address() const { return _addr; }

                /** @brief Returns the prefix length in bits (0–128). */
                int prefixLen() const { return _prefixLen; }

                /** @brief Returns true if @p addr lies inside this subnet. */
                bool contains(const Ipv6Address &addr) const;

                /** @brief Returns @c "address/prefix" CIDR notation. */
                String toString() const;

                /** @brief Equality compares both address and prefix. */
                bool operator==(const Ipv6Subnet &o) const { return _addr == o._addr && _prefixLen == o._prefixLen; }
                /** @brief Inequality. */
                bool operator!=(const Ipv6Subnet &o) const { return !(*this == o); }

        private:
                Ipv6Address _addr;
                int         _prefixLen = 0;
};

PROMEKI_NAMESPACE_END

PROMEKI_FORMAT_VIA_TOSTRING(promeki::Ipv6Subnet);

#endif // PROMEKI_ENABLE_NETWORK
