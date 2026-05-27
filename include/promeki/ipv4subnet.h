/**
 * @file      ipv4subnet.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_NETWORK
#include <promeki/namespace.h>
#include <promeki/ipv4address.h>
#include <promeki/list.h>
#include <promeki/string.h>
#include <promeki/result.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief IPv4 address paired with its subnet mask.
 * @ingroup network
 *
 * Captures one of the @c (address, netmask) tuples a network
 * interface advertises.  The netmask is stored as a full
 * @ref Ipv4Address so non-contiguous masks (rare but legal in
 * legacy deployments) round-trip without loss; CIDR-style prefix
 * length is available via @ref prefixLen.
 *
 * @par Example
 * @code
 * Ipv4Subnet s(Ipv4Address(192, 168, 1, 5), Ipv4Address(255, 255, 255, 0));
 * assert(s.prefixLen() == 24);
 * assert(s.network() == Ipv4Address(192, 168, 1, 0));
 * assert(s.contains(Ipv4Address(192, 168, 1, 200)));
 * @endcode
 */
class Ipv4Subnet {
        public:
                /** @brief List of IPv4 subnets. */
                using List = ::promeki::List<Ipv4Subnet>;

                /**
                 * @brief Parses a CIDR-formatted subnet string.
                 *
                 * Accepts @c "192.168.1.5/24" or a plain
                 * @c "192.168.1.5" (treated as @c /32).
                 *
                 * @param str The string to parse.
                 * @return A Result holding the parsed subnet and
                 *         Error::Ok, or an empty subnet and
                 *         Error::Invalid on parse failure.
                 */
                static Result<Ipv4Subnet> fromString(const String &str);

                /**
                 * @brief Returns the canonical netmask for a CIDR
                 *        prefix length 0–32.
                 *
                 * Out-of-range values clamp to 0 or 32.
                 */
                static Ipv4Address netmaskForPrefix(int prefixLen);

                /** @brief Constructs an invalid (null) subnet. */
                Ipv4Subnet() = default;

                /**
                 * @brief Constructs a subnet from explicit address and netmask.
                 * @param addr The interface address.
                 * @param mask The netmask reported by the OS.
                 */
                Ipv4Subnet(const Ipv4Address &addr, const Ipv4Address &mask) : _addr(addr), _mask(mask) {}

                /**
                 * @brief Constructs a subnet from address and CIDR prefix length.
                 * @param addr The interface address.
                 * @param prefixLen Prefix length in bits, 0–32.
                 */
                Ipv4Subnet(const Ipv4Address &addr, int prefixLen) : _addr(addr), _mask(netmaskForPrefix(prefixLen)) {}

                /** @brief Returns true if this subnet has a non-null address. */
                bool isValid() const { return !_addr.isNull(); }

                /** @brief Returns the interface address itself. */
                const Ipv4Address &address() const { return _addr; }

                /** @brief Returns the netmask. */
                const Ipv4Address &netmask() const { return _mask; }

                /** @brief Returns the network address (address &amp; netmask). */
                Ipv4Address network() const;

                /**
                 * @brief Returns the directed broadcast address for this subnet.
                 *
                 * Equal to @c address | ~netmask.  For a @c /32 host
                 * route this collapses to the address itself.
                 */
                Ipv4Address broadcast() const;

                /**
                 * @brief Returns the CIDR prefix length, or -1 if the
                 *        netmask is non-contiguous.
                 */
                int prefixLen() const;

                /**
                 * @brief Returns true if @p addr lies inside this subnet.
                 *
                 * Computed as @c (addr & netmask) == (address & netmask).
                 */
                bool contains(const Ipv4Address &addr) const;

                /** @brief Returns @c "address/prefix" notation, or @c "address/netmask" for non-contiguous masks. */
                String toString() const;

                /** @brief Equality compares both address and netmask. */
                bool operator==(const Ipv4Subnet &o) const { return _addr == o._addr && _mask == o._mask; }
                /** @brief Inequality. */
                bool operator!=(const Ipv4Subnet &o) const { return !(*this == o); }

        private:
                Ipv4Address _addr;
                Ipv4Address _mask;
};

PROMEKI_NAMESPACE_END

PROMEKI_FORMAT_VIA_TOSTRING(promeki::Ipv4Subnet);

#endif // PROMEKI_ENABLE_NETWORK
