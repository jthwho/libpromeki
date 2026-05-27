/**
 * @file      networkinterface.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_NETWORK
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/list.h>
#include <promeki/macaddress.h>
#include <promeki/ipv4address.h>
#include <promeki/ipv6address.h>
#include <promeki/ipv4subnet.h>
#include <promeki/ipv6subnet.h>
#include <promeki/networkinterfaceimpl.h>

PROMEKI_NAMESPACE_BEGIN

class TextStream;

/**
 * @brief Value-type handle for a discovered network interface.
 * @ingroup network
 *
 * Wraps a backend-produced @ref NetworkInterfaceImpl by reference
 * count so handles can be passed around cheaply.  The static
 * lookup helpers (@ref enumerate, @ref findByName,
 * @ref findByIpv4Address, @ref findByIpv6Address,
 * @ref findByMacAddress, @ref findRoutesTo, @ref firstNonLoopback)
 * are the canonical entry points for callers that need a MAC, an
 * IP list, or an interface name from somewhere in the library or
 * application.
 *
 * Backends register through @ref NetworkInterfaceBackend; on POSIX
 * builds the bundled getifaddrs-based backend self-registers at
 * library load.  Hardware-specific backends (e.g. ST 2110
 * SmartNIC SDKs) plug in the same way.
 *
 * The registry stabilises impl identity across enumeration cycles,
 * so two handles obtained from different @ref enumerate calls that
 * refer to the same physical interface compare equal via
 * @ref operator==.  When an interface disappears (cable unplug,
 * USB-Ethernet removal, backend unregistered) any held handles
 * remain valid but begin reporting @c isUp() == false and an empty
 * address list — see @ref NetworkInterfaceData.
 *
 * @par Thread Safety
 * Distinct handles may be used concurrently.  Accessors on a single
 * handle pull race-free snapshots from the underlying impl, so
 * concurrent @ref data and @ref name calls are safe.  The static
 * registry (lookups, enumerate) is internally synchronised.
 */
class NetworkInterface {
        public:
                /** @brief List of @ref NetworkInterface handles. */
                using List = ::promeki::List<NetworkInterface>;

                /**
                 * @brief Returns the union of every registered backend's interfaces.
                 *
                 * Order: backend priority first (lower priority value =
                 * earlier in the list), then the backend's own
                 * enumeration order.
                 */
                static List enumerate();

                /** @brief Returns the interface with the given OS name, or an invalid handle. */
                static NetworkInterface findByName(const String &name);

                /**
                 * @brief Returns the interface that owns the given IPv4 address.
                 *
                 * Matches against the @ref ipv4Subnets address list,
                 * not the route table — so a /32 host route shared
                 * across interfaces (rare) returns the first match in
                 * enumeration order.  Use @ref findRoutesTo for
                 * subnet-cover lookups.
                 */
                static NetworkInterface findByIpv4Address(const Ipv4Address &addr);

                /**
                 * @brief Returns the interface that owns the given IPv6 address.
                 *
                 * Match semantics mirror @ref findByIpv4Address.
                 */
                static NetworkInterface findByIpv6Address(const Ipv6Address &addr);

                /**
                 * @brief Returns the interface whose primary MAC matches @p mac.
                 *
                 * Walks every interface's @ref allMacAddresses list so
                 * an interface bonded to multiple MACs (rare on Linux
                 * but possible) still resolves cleanly.  Skips
                 * interfaces with a null MAC (loopback, tun/tap).
                 */
                static NetworkInterface findByMacAddress(const MacAddress &mac);

                /**
                 * @brief Returns every interface that can route an IPv4 destination.
                 *
                 * Walks each interface's IPv4 subnets and returns the
                 * ones whose @c (network, netmask) covers @p dest.
                 * The match is "directly attached subnet" — full
                 * routing-table lookup (default route + policy
                 * routing) lives on @c NetworkRouting.
                 *
                 * Multicast destinations match every interface that
                 * is administratively up and multicast-capable.
                 */
                static List findRoutesTo(const Ipv4Address &dest);

                /**
                 * @brief Returns every interface that can route an IPv6 destination.
                 *
                 * Same semantics as the IPv4 overload, plus the
                 * link-local exception: a destination in @c fe80::/10
                 * matches every up + multicast-capable interface
                 * because link-local routing is per-interface by
                 * definition.
                 */
                static List findRoutesTo(const Ipv6Address &dest);

                /**
                 * @brief Returns the first non-loopback interface that is up and has a non-null MAC.
                 *
                 * Used as the default source of @c ts-refclk:localmac
                 * when the writer-side configuration leaves the MAC
                 * unspecified.  Returns an invalid handle if no such
                 * interface exists.
                 */
                static NetworkInterface firstNonLoopback();

                /** @brief Constructs an invalid (empty) handle. */
                NetworkInterface() = default;

                /** @brief Constructs a handle from a backend-produced impl. */
                explicit NetworkInterface(NetworkInterfaceImplPtr impl) : _d(std::move(impl)) {}

                /** @brief Returns true if this handle wraps a valid impl. */
                bool isValid() const { return _d.isValid(); }

                /**
                 * @brief Returns the full snapshot for this interface.
                 *
                 * Pulls a single race-free copy of every field.
                 * Preferred over multiple per-field accessors when
                 * reading more than one value, since each accessor
                 * takes a fresh snapshot under the impl's read lock.
                 *
                 * Returns a default-constructed @ref NetworkInterfaceData
                 * when the handle is invalid.
                 */
                NetworkInterfaceData data() const;

                /** @brief Returns the OS-visible interface name (e.g. "eth0"). */
                String name() const;

                /**
                 * @brief Returns the human-friendly name.
                 *
                 * On POSIX equals @ref name; on Windows the GUID-keyed
                 * adapter exposes a separate friendly name (e.g.
                 * @c "Ethernet 2") which is what users actually see.
                 */
                String friendlyName() const;

                /** @brief Returns the OS interface index, or 0 if unknown. */
                uint32_t index() const;

                /** @brief Returns the primary MAC address, or a null MAC if none. */
                MacAddress macAddress() const;

                /** @brief Returns every MAC associated with this interface (primary first). */
                MacAddress::List allMacAddresses() const;

                /** @brief Returns the IPv4 subnets bound to this interface. */
                Ipv4Subnet::List ipv4Subnets() const;

                /** @brief Returns the IPv6 subnets bound to this interface. */
                Ipv6Subnet::List ipv6Subnets() const;

                /** @brief Returns just the IPv4 addresses, dropping netmask information. */
                Ipv4Address::List ipv4Addresses() const;

                /** @brief Returns just the IPv6 addresses, dropping prefix length information. */
                Ipv6Address::List ipv6Addresses() const;

                /** @brief Returns the maximum transmission unit, or 0 if unknown. */
                uint32_t mtu() const;

                /** @brief Returns the coarse interface category. */
                NetworkInterfaceKind kind() const;

                /** @brief Returns the negotiated link speed in Mb/s, or 0 if unknown. */
                uint64_t linkSpeedMbps() const;

                /** @brief Returns true if the link negotiated full duplex. */
                bool fullDuplex() const;

                /** @brief Returns true if the interface is administratively up. */
                bool isUp() const;

                /** @brief Returns true if the link is up (carrier present). */
                bool isRunning() const;

                /**
                 * @brief Returns true if the link reports carrier present.
                 *
                 * On Linux this reads from
                 * @c /sys/class/net/&lt;iface&gt;/carrier and may
                 * differ from @ref isRunning when the kernel and
                 * sysfs disagree; on POSIX without sysfs it tracks
                 * @ref isRunning.
                 */
                bool hasCarrier() const;

                /** @brief Returns true if this is the loopback interface. */
                bool isLoopback() const;

                /** @brief Returns true if the interface is multicast-capable. */
                bool isMulticast() const;

                /**
                 * @brief Returns true if this interface can route to @p dest via a directly-attached subnet.
                 *
                 * Multicast destinations match when the interface is
                 * up and multicast-capable.
                 */
                bool canRoute(const Ipv4Address &dest) const;

                /**
                 * @brief Returns true if this interface can route to @p dest via a directly-attached subnet.
                 *
                 * Link-local @c fe80::/10 and multicast destinations
                 * match when the interface is up and multicast-capable.
                 */
                bool canRoute(const Ipv6Address &dest) const;

                /** @brief Returns the current statistics snapshot. */
                NetworkInterfaceStats stats() const;

                /**
                 * @brief Returns a single-line summary of this interface.
                 *
                 * Format: <tt>name (friendly) [kind] up/down MAC=... addrs=...</tt>.
                 * Returns an empty string for an invalid handle.
                 */
                String toString() const;

                /** @brief Returns the underlying impl handle. */
                const NetworkInterfaceImplPtr &impl() const { return _d; }

                /** @brief Equality compares the underlying impl identity. */
                bool operator==(const NetworkInterface &o) const { return _d == o._d; }
                /** @brief Inequality. */
                bool operator!=(const NetworkInterface &o) const { return !(*this == o); }

        private:
                NetworkInterfaceImplPtr _d;
};

/** @brief Streams the @ref NetworkInterface::toString summary. */
TextStream &operator<<(TextStream &stream, const NetworkInterface &iface);

PROMEKI_NAMESPACE_END

PROMEKI_FORMAT_VIA_TOSTRING(promeki::NetworkInterface);

#endif // PROMEKI_ENABLE_NETWORK
