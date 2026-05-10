/**
 * @file      networkrouting.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/list.h>
#include <promeki/ipv4address.h>
#include <promeki/ipv6address.h>
#include <promeki/networkaddress.h>
#include <promeki/networkinterface.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Host-level routing, DNS, and source-address selection.
 * @ingroup network
 *
 * Companion to @ref NetworkInterface — that class describes what a
 * single NIC looks like; @c NetworkRouting answers questions whose
 * scope is the whole host's routing table (default gateway, source
 * address for an outbound flow, configured DNS resolvers).  All
 * methods are static; the class is not instantiated.
 *
 * The implementation routes through pluggable platform helpers
 * (POSIX in @c posixnetworkrouting.cpp, Windows in
 * @c windowsnetworkrouting.cpp) installed at static-init time.
 * Callers that link only the bundled POSIX backend get the POSIX
 * answers automatically; embedders adding a hardware NIC SDK with
 * its own routing table can install a higher-priority hook later.
 *
 * @par Thread Safety
 * Utility class — no instance state.  Every public method reads
 * the host's routing table on demand and is safe to call from any
 * thread.  Concurrent reads do not contend on a process-global
 * lock; the implementation re-reads the kernel route table per
 * call.
 *
 * @par Example
 * @code
 * NetworkAddress gw = NetworkRouting::defaultGatewayIpv4();
 * Ipv4Address src = NetworkRouting::sourceAddressFor(Ipv4Address(8, 8, 8, 8));
 * for (const auto &dns : NetworkRouting::dnsServers()) {
 *     promekiInfo("DNS: %s", dns.toString().cstr());
 * }
 * @endcode
 */
class NetworkRouting {
        public:
                NetworkRouting()  = delete;
                ~NetworkRouting() = delete;

                /**
                 * @brief Returns the IPv4 default gateway, or a null
                 *        @ref NetworkAddress if no default route exists.
                 *
                 * When multiple default routes are present the entry
                 * with the lowest metric wins (matches kernel forward
                 * decision).
                 */
                static NetworkAddress defaultGatewayIpv4();

                /** @brief IPv6 counterpart to @ref defaultGatewayIpv4. */
                static NetworkAddress defaultGatewayIpv6();

                /**
                 * @brief Returns the interface that owns the IPv4
                 *        default route.
                 *
                 * @return The owning interface, or an invalid handle
                 *         if no default route is configured.
                 */
                static NetworkInterface defaultRouteInterfaceIpv4();

                /** @brief IPv6 counterpart to @ref defaultRouteInterfaceIpv4. */
                static NetworkInterface defaultRouteInterfaceIpv6();

                /**
                 * @brief Picks an outbound source address for an IPv4
                 *        destination.
                 *
                 * Strategy: walk @ref NetworkInterface::findRoutesTo,
                 * return the first directly-attached subnet's primary
                 * address.  Falls back to the default-route iface's
                 * primary address.  Multicast destinations also use
                 * the default-route iface's primary address.
                 *
                 * @param dest The destination address whose source we want.
                 * @return The source address, or a null
                 *         @ref Ipv4Address when no candidate exists.
                 */
                static Ipv4Address sourceAddressFor(const Ipv4Address &dest);

                /**
                 * @brief IPv6 counterpart to the IPv4
                 *        @ref sourceAddressFor.
                 *
                 * Link-local destinations (@c fe80::/10) require an
                 * explicit interface and return a null address from
                 * this overload — a future
                 * @c sourceAddressFor(dest, iface) covers the
                 * explicit case.
                 *
                 * @param dest The destination address whose source we want.
                 */
                static Ipv6Address sourceAddressFor(const Ipv6Address &dest);

                /**
                 * @brief Returns the configured global DNS resolvers in
                 *        priority order.
                 *
                 * POSIX: parsed from @c /etc/resolv.conf.  Windows
                 * (Stage 3): walks the per-adapter
                 * @c IP_ADAPTER_ADDRESSES.FirstDnsServerAddress chain
                 * and dedups.  Per-interface DNS on Linux
                 * (@c systemd-resolved D-Bus) is out of scope — see
                 * the devplan's "Out of Scope" section.
                 */
                static List<NetworkAddress> dnsServers();

                /**
                 * @brief One row from the kernel's IP routing table.
                 * @ingroup network
                 *
                 * Carries the destination prefix, the next-hop
                 * gateway (null for directly-attached subnets), the
                 * owning interface, and the kernel's metric.  Used by
                 * @ref routesFor to expose the full set of candidate
                 * paths a destination address could take, not just
                 * the directly-attached one @ref NetworkInterface
                 * surfaces.
                 *
                 * @par Thread Safety
                 * Plain value type — no internal synchronisation;
                 * callers copy as needed.
                 */
                struct Route {
                        NetworkAddress   destination; ///< @brief Destination network address (null = default route).
                        int              prefixLen = 0; ///< @brief Destination prefix length (0 = default route).
                        NetworkAddress   gateway;     ///< @brief Next-hop gateway, or null for on-link routes.
                        NetworkInterface iface;       ///< @brief Outgoing interface (invalid handle if unresolved).
                        uint32_t         metric = 0;  ///< @brief Kernel route metric — lower is preferred.
                };

                /**
                 * @brief Returns every route that can carry @p dest.
                 *
                 * Walks the host's IPv4 routing table and returns
                 * every entry whose prefix covers @p dest, including
                 * the default route as a fallback.  Result is sorted
                 * by metric ascending, so the first entry is the path
                 * the kernel would actually pick.
                 *
                 * Multicast and link-local destinations resolve to
                 * the routes the kernel exposes for those classes —
                 * specific behaviour is platform-dependent and
                 * matches what @c ip route get prints.
                 *
                 * @param dest The destination address being routed.
                 *
                 * @par Use case
                 * RTP TX picking an outbound iface from an SDP
                 * @c c=IN @c IP4 line walks the candidates and prefers
                 * the lowest-metric Ethernet/Wifi route, ignoring
                 * docker/veth bridges that would otherwise score
                 * higher than the physical NIC.
                 */
                static List<Route> routesFor(const Ipv4Address &dest);

                /**
                 * @brief IPv6 counterpart to the IPv4 @ref routesFor.
                 * @param dest The destination address being routed.
                 */
                static List<Route> routesFor(const Ipv6Address &dest);

                /**
                 * @brief Hook installed by platform backends.
                 *
                 * Concrete backends fill in the per-platform route
                 * lookups; the @c NetworkRouting public API delegates
                 * to whichever backend was registered.  Higher
                 * priority (lower numeric value) wins, matching the
                 * @ref NetworkInterfaceBackend convention.
                 *
                 * @par Thread Safety
                 * Concrete implementations must be safe to call
                 * concurrently from any thread (no reliance on
                 * thread-affine state).  Backends are stateless in
                 * practice — every call hits the OS directly.
                 */
                class Backend {
                        public:
                                virtual ~Backend() = default;

                                /** @brief Backend implementation of @ref NetworkRouting::defaultGatewayIpv4. */
                                virtual NetworkAddress defaultGatewayIpv4() const = 0;

                                /** @brief Backend implementation of @ref NetworkRouting::defaultGatewayIpv6. */
                                virtual NetworkAddress defaultGatewayIpv6() const = 0;

                                /** @brief Backend implementation of @ref NetworkRouting::defaultRouteInterfaceIpv4. */
                                virtual NetworkInterface defaultRouteInterfaceIpv4() const = 0;

                                /** @brief Backend implementation of @ref NetworkRouting::defaultRouteInterfaceIpv6. */
                                virtual NetworkInterface defaultRouteInterfaceIpv6() const = 0;

                                /** @brief Backend implementation of @ref NetworkRouting::dnsServers. */
                                virtual List<NetworkAddress> dnsServers() const = 0;

                                /** @brief Backend implementation of @ref NetworkRouting::routesFor for IPv4. */
                                virtual List<Route> routesForIpv4(const Ipv4Address &dest) const = 0;

                                /** @brief Backend implementation of @ref NetworkRouting::routesFor for IPv6. */
                                virtual List<Route> routesForIpv6(const Ipv6Address &dest) const = 0;
                };

                /**
                 * @brief Registers a routing backend.
                 *
                 * Takes ownership of @p backend.  Call from
                 * static-init.  The most-recently-registered backend
                 * wins (callers that need ordering should swap a
                 * higher-priority impl in by calling this last).
                 */
                static void setBackend(Backend *backend);

                /** @brief Returns the active backend, or null if none. */
                static Backend *backend();
};

PROMEKI_NAMESPACE_END
