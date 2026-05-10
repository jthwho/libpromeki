/**
 * @file      networkinterfacebackend.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/stringlist.h>
#include <promeki/list.h>
#include <promeki/networkinterfaceimpl.h>

PROMEKI_NAMESPACE_BEGIN

class NetworkInterface;

/**
 * @brief Plug-in source of @ref NetworkInterface objects.
 * @ingroup network
 *
 * Backends adapt a discovery mechanism (POSIX getifaddrs, Windows
 * IPHelper, Mellanox/NVIDIA Rivermax SDK, custom FPGA NIC SDKs,
 * etc.) into the unified @ref NetworkInterface API.  Register an
 * instance with @ref registerBackend at static-init time; the
 * registry calls @ref enumerate on demand and merges results
 * across all registered backends.
 *
 * @par Thread Safety
 * The static registry is internally synchronised.  Concrete
 * backends must be safe to call @ref enumerate / @ref name from
 * any thread.
 */
class NetworkInterfaceBackend {
        public:
                /** @brief List of @ref NetworkInterfaceImpl handles a backend produces. */
                using ImplList = ::promeki::List<NetworkInterfaceImplPtr>;

                /**
                 * @brief Registers a backend with the global registry.
                 *
                 * The registry takes ownership of @p backend.  Safe to
                 * call from static-init.  If a backend with the same
                 * @ref name is already registered the new one
                 * replaces it.
                 */
                static void registerBackend(NetworkInterfaceBackend *backend);

                /**
                 * @brief Removes a registered backend by name.
                 *
                 * The matching backend (if any) is destroyed.  Used by
                 * tests; production code rarely unregisters.
                 */
                static void unregisterBackend(const String &name);

                /** @brief Returns the names of all registered backends in priority order. */
                static StringList registeredBackends();

                /**
                 * @brief Returns the union of every backend's @ref enumerate output.
                 *
                 * Backends are queried in priority order; duplicates
                 * (same interface name from multiple backends) are
                 * resolved by keeping the higher-priority entry.
                 *
                 * Result is TTL-cached (default 250 ms; configurable
                 * via @ref setEnumerationTtlMs); calls within the
                 * window return the previous list without re-walking
                 * any backend.  Use @ref invalidateEnumerationCache
                 * to force the next call to re-enumerate, or
                 * @ref setEnumerationTtlMs(0) to disable caching
                 * entirely (e.g. in tests).
                 */
                static ImplList enumerateAll();

                /**
                 * @brief Drops the TTL cache used by @ref enumerateAll.
                 *
                 * Forces the next call to re-walk every backend.
                 * @ref NetworkInterfaceMonitor calls this immediately
                 * before each diff cycle so subscribers always see
                 * the freshest data.
                 */
                static void invalidateEnumerationCache();

                /** @brief Returns the current enumeration TTL in milliseconds. */
                static unsigned int enumerationTtlMs();

                /**
                 * @brief Sets the enumeration TTL in milliseconds.
                 *
                 * @c 0 disables caching — every @ref enumerateAll
                 * call re-walks every backend.
                 */
                static void setEnumerationTtlMs(unsigned int ms);

                /** @brief Default TTL applied at startup. */
                static constexpr unsigned int DefaultEnumerationTtlMs = 250;

                virtual ~NetworkInterfaceBackend() = default;

                /** @brief Returns the human-readable backend name (e.g. "posix", "rivermax"). */
                virtual String name() const = 0;

                /**
                 * @brief Returns a numeric priority used to order backends.
                 *
                 * Lower values run first.  Default is 100; the built-in
                 * POSIX backend uses 100.  Hardware-specific backends
                 * (ST 2110 NICs) typically register with a lower value
                 * so their interfaces sort ahead of generic OS
                 * interfaces.
                 */
                virtual int priority() const { return 100; }

                /**
                 * @brief Returns the current list of interface impls.
                 *
                 * Backends may construct fresh impls on every call or
                 * cache and re-emit the same handles.  Callers should
                 * treat the returned list as a snapshot.
                 */
                virtual ImplList enumerate() const = 0;
};

PROMEKI_NAMESPACE_END
