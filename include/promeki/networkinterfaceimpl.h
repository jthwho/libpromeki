/**
 * @file      networkinterfaceimpl.h
 * @copyright Howard Logic. All rights reserved.
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
#include <promeki/sharedptr.h>
#include <promeki/readwritelock.h>
#include <promeki/enums.h>

PROMEKI_NAMESPACE_BEGIN

class TextStream;

/**
 * @brief Snapshot of per-interface metadata.
 * @ingroup network
 *
 * Held by every @ref NetworkInterfaceImpl.  Backends produce a
 * fresh snapshot each enumeration cycle; the registry then folds
 * the new snapshot into the long-lived @ref NetworkInterfaceImpl
 * via @ref NetworkInterfaceImpl::replaceData so existing handles
 * see updated state without losing identity.
 *
 * @par Thread Safety
 * Not internally thread-safe — callers obtain a value snapshot from
 * @ref NetworkInterfaceImpl::data and use it locally.  Mutating an
 * instance from one thread while another reads it is undefined.
 */
struct NetworkInterfaceData {
        String               name;            ///< @brief OS-visible interface name (e.g. "eth0").
        String               friendlyName;    ///< @brief Human-friendly name (Windows); equals @c name on POSIX.
        uint32_t             index = 0;       ///< @brief OS interface index, 0 if unknown.
        MacAddress::List     macAddresses;    ///< @brief One or more EUI-48 MACs (ordered, primary first).
        Ipv4Subnet::List     ipv4Subnets;     ///< @brief Bound IPv4 (address, netmask) tuples.
        Ipv6Subnet::List     ipv6Subnets;     ///< @brief Bound IPv6 (address, prefix length) tuples.
        uint32_t             mtu = 0;         ///< @brief Maximum transmission unit, 0 if unknown.
        NetworkInterfaceKind kind = NetworkInterfaceKind::Unknown; ///< @brief Coarse interface category.
        uint64_t             linkSpeedMbps = 0;   ///< @brief Negotiated link speed in Mb/s; 0 if unknown.
        bool                 fullDuplex = false;  ///< @brief True if the link negotiated full-duplex.
        bool                 isUp = false;        ///< @brief True if administratively up.
        bool                 isRunning = false;   ///< @brief True if link is up (carrier present).
        bool                 hasCarrier = false;  ///< @brief True if the link reports carrier present.  On Linux this comes from the sysfs @c carrier attribute and may differ from @c isRunning (@c IFF_RUNNING) when the kernel and sysfs disagree (e.g. a driver that sets RUNNING before the link is fully up); without sysfs it tracks @c isRunning.
        bool                 isLoopback = false;  ///< @brief True if the loopback interface.
        bool                 isMulticast = false; ///< @brief True if multicast-capable.
};

/**
 * @brief Per-interface statistics snapshot.
 * @ingroup network
 *
 * Returned by @ref NetworkInterface::stats.  Counters are absolute
 * (not deltas); callers compute differences over time.  Backends
 * unable to read a particular counter leave it at zero.
 *
 * @note Counters can reset to zero on interface bounce, so consumers
 * computing rates must handle a backwards step (treat negative delta
 * as a session reset and drop that sample).
 *
 * @par Thread Safety
 * Plain value type — copy semantics; not internally synchronised.
 */
struct NetworkInterfaceStats {
        uint64_t rxBytes   = 0;          ///< @brief Cumulative bytes received.
        uint64_t rxPackets = 0;          ///< @brief Cumulative packets received.
        uint64_t rxErrors  = 0;          ///< @brief Cumulative receive errors.
        uint64_t rxDropped = 0;          ///< @brief Cumulative receive drops (no buffer, etc.).
        uint64_t txBytes   = 0;          ///< @brief Cumulative bytes transmitted.
        uint64_t txPackets = 0;          ///< @brief Cumulative packets transmitted.
        uint64_t txErrors  = 0;          ///< @brief Cumulative transmit errors.
        uint64_t txDropped = 0;          ///< @brief Cumulative transmit drops.
        bool     valid     = false;      ///< @brief False when no stats source is available.
};

/**
 * @brief Virtual base for backend-owned network interface records.
 * @ingroup network
 *
 * Concrete backends (POSIX getifaddrs, Windows IPHelper, ST 2110
 * SmartNIC SDK adapters, etc.) derive from this class to expose
 * their interfaces through the @ref NetworkInterface handle.  Owns
 * a @ref NetworkInterfaceData snapshot for the descriptive fields
 * and implements @ref stats for live state.
 *
 * The registry stabilises impl identity across enumeration cycles
 * by mutating an existing impl's snapshot via @ref replaceData
 * rather than minting a fresh impl on every call.  This means
 * accessors must be race-free against an in-flight @ref replaceData
 * — handled internally via a @ref ReadWriteLock.
 *
 * @par Thread Safety
 * All public methods are safe to call concurrently from multiple
 * threads.  @ref data and @ref replaceData are race-free via an
 * internal @ref ReadWriteLock; @ref stats implementations override
 * the default empty snapshot and must be internally synchronised
 * (the default base implementation is trivially safe).
 */
class NetworkInterfaceImpl {
        public:
                PROMEKI_SHARED_BASE(NetworkInterfaceImpl)

                /** @brief Constructs from a snapshot.  Backend-only constructor. */
                explicit NetworkInterfaceImpl(NetworkInterfaceData data) : _data(std::move(data)) {}

                virtual ~NetworkInterfaceImpl() = default;

                /**
                 * @brief Returns the current snapshot by value.
                 *
                 * Race-free against a concurrent @ref replaceData call.
                 * Callers reading multiple fields should pull a single
                 * snapshot here and read from the returned struct rather
                 * than calling per-field accessors repeatedly.
                 */
                NetworkInterfaceData data() const {
                        ReadWriteLock::ReadLocker lock(_dataLock);
                        return _data;
                }

                /**
                 * @brief Replaces the current snapshot.
                 *
                 * Used by the registry to refresh long-lived impls in
                 * place during @ref NetworkInterfaceBackend::enumerateAll.
                 * Backends must not call this directly; they construct
                 * fresh impls and let the registry decide whether to
                 * fold the snapshot into an existing entry.
                 */
                void replaceData(NetworkInterfaceData data) {
                        ReadWriteLock::WriteLocker lock(_dataLock);
                        _data = std::move(data);
                }

                /**
                 * @brief Returns a current statistics snapshot.
                 *
                 * Default returns @c valid=false.  Backends that can
                 * read counters override this; the descriptive snapshot
                 * (name, MAC, IPs, MTU, flags) is captured at
                 * @ref NetworkInterfaceBackend::enumerate time and
                 * refreshed on subsequent enumeration cycles.
                 */
                virtual NetworkInterfaceStats stats() const { return NetworkInterfaceStats{}; }

        private:
                mutable ReadWriteLock _dataLock;
                NetworkInterfaceData  _data;
};

/** @brief SharedPtr alias for @ref NetworkInterfaceImpl. */
using NetworkInterfaceImplPtr = SharedPtr<NetworkInterfaceImpl, false>;

/**
 * @brief Streams a multi-line dump of every @ref NetworkInterfaceData field.
 *
 * Useful in caller code that has already pulled a snapshot via
 * @ref NetworkInterface::data and wants to log it with @c promekiDebug.
 */
TextStream &operator<<(TextStream &stream, const NetworkInterfaceData &data);

/**
 * @brief Streams a single-line summary of @ref NetworkInterfaceStats.
 *
 * Format: <tt>rx=B/P/E/D tx=B/P/E/D [valid|invalid]</tt>.
 */
TextStream &operator<<(TextStream &stream, const NetworkInterfaceStats &stats);

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NETWORK
