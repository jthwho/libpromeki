/**
 * @file      networkinterfacemonitor.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_NETWORK
#include <promeki/objectbase.h>
#include <promeki/error.h>
#include <promeki/networkinterface.h>
#include <promeki/ipv4address.h>
#include <promeki/ipv6address.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Push-based notifier for network-interface state changes.
 * @ingroup network
 *
 * Wraps a per-OS notification source (@c AF_NETLINK on Linux,
 * @c PF_ROUTE on BSD/macOS, @c NotifyIpInterfaceChange +
 * @c NotifyUnicastIpAddressChange on Windows) and fires signals
 * when interfaces appear, disappear, change link state, or
 * gain/lose addresses.
 *
 * The monitor is opt-in: programs that don't construct one keep
 * the zero-thread, zero-netlink-fd profile of the polled
 * @ref NetworkInterface API.  Construct a monitor on the thread
 * that should receive its signals (signals are emitted on the
 * monitor's affinity thread), call @ref start, connect signals,
 * and let it run until @ref stop or destruction.
 *
 * Signals carry @ref NetworkInterface by value.  Stage 1's
 * impl-stabilisation guarantees that handles delivered through
 * signals compare equal (via @c operator==) to handles obtained
 * later through @ref NetworkInterface::findByName / @c enumerate
 * — slot code can use @c == directly rather than name compares.
 *
 * @par Thread Safety
 * <b>Thread-affine.</b>  This class must only be used from the
 * thread that created it (or the thread it was moved to via
 * @ref ObjectBase::moveToThread).  Signals may be connected from
 * any thread; emission marshals to the monitor's
 * @ref ObjectBase::eventLoop.
 *
 * @par Example
 * @code
 * NetworkInterfaceMonitor monitor;
 * connect(&monitor.linkUpSignal, [](NetworkInterface i) {
 *     promekiInfo("link up: %s", i.name().cstr());
 * });
 * monitor.start();
 * // ... event loop runs ...
 * @endcode
 */
class NetworkInterfaceMonitor : public ObjectBase {
                PROMEKI_OBJECT(NetworkInterfaceMonitor, ObjectBase)
        public:
                /** @brief Default debounce window in milliseconds. */
                static constexpr unsigned int DefaultDebounceMs = 50;

                /** @brief Constructs an idle monitor. */
                explicit NetworkInterfaceMonitor(ObjectBase *parent = nullptr);

                /** @brief Destructor.  Calls @ref stop if running. */
                ~NetworkInterfaceMonitor() override;

                /**
                 * @brief Starts monitoring.
                 *
                 * Opens the platform notification source, primes the
                 * previous-state cache from a first
                 * @ref NetworkInterface::enumerate (no signals fire on
                 * priming), and registers the source with the
                 * monitor's @ref ObjectBase::eventLoop.
                 *
                 * @return @ref Error::Ok on success, otherwise a
                 *         platform-specific error code.
                 */
                Error start();

                /**
                 * @brief Stops monitoring.
                 *
                 * Cancels the platform notification source and
                 * removes any pending debounce timer.  Does not clear
                 * the previous-state cache; a subsequent @ref start
                 * resumes diffing from where the previous run left
                 * off.
                 */
                void stop();

                /** @brief Returns true if the monitor is started. */
                bool isRunning() const { return _running; }

                /**
                 * @brief Sets the debounce coalescing window.
                 *
                 * @param ms Window in milliseconds.  @c 0 disables
                 *           coalescing — every OS event triggers a
                 *           full diff cycle, which is rarely what
                 *           callers want during interface flapping.
                 */
                void setDebounceMs(unsigned int ms) { _debounceMs = ms; }

                /** @brief Returns the current debounce window in milliseconds. */
                unsigned int debounceMs() const { return _debounceMs; }

                /**
                 * @brief Forces a synchronous diff cycle for tests.
                 *
                 * Bypasses debounce and enumerates / diffs / emits in
                 * the calling thread.  Production code should rely on
                 * the OS notification path instead — this hook exists
                 * so unit tests can drive deterministic diffs against
                 * a @c FakeBackend.
                 */
                void testForceRescan();

                /**
                 * @brief Returns the first started monitor in the
                 *        process registry, or @c nullptr.
                 *
                 * Discovery convenience for components that want to
                 * subscribe without explicit dependency injection;
                 * prefer DI for new code.
                 */
                static NetworkInterfaceMonitor *anyRunning();

                /** @brief Emitted when a new interface is added. @signal */
                PROMEKI_SIGNAL(interfaceAdded, NetworkInterface);
                /** @brief Emitted when an interface disappears. @signal */
                PROMEKI_SIGNAL(interfaceRemoved, NetworkInterface);
                /** @brief Emitted when an interface's link comes up (carrier present). @signal */
                PROMEKI_SIGNAL(linkUp, NetworkInterface);
                /** @brief Emitted when an interface's link goes down. @signal */
                PROMEKI_SIGNAL(linkDown, NetworkInterface);
                /** @brief Emitted when an IPv4 address is bound to an interface. @signal */
                PROMEKI_SIGNAL(addressAddedIpv4, NetworkInterface, Ipv4Address);
                /** @brief Emitted when an IPv4 address is removed from an interface. @signal */
                PROMEKI_SIGNAL(addressRemovedIpv4, NetworkInterface, Ipv4Address);
                /** @brief Emitted when an IPv6 address is bound to an interface. @signal */
                PROMEKI_SIGNAL(addressAddedIpv6, NetworkInterface, Ipv6Address);
                /** @brief Emitted when an IPv6 address is removed from an interface. @signal */
                PROMEKI_SIGNAL(addressRemovedIpv6, NetworkInterface, Ipv6Address);
                /**
                 * @brief Coalesced "go re-read" hook — emitted once per
                 *        diff cycle regardless of how many per-iface
                 *        signals fired.
                 * @signal
                 */
                PROMEKI_SIGNAL(interfacesChanged);

                /**
                 * @brief Schedules a debounced diff cycle.
                 *
                 * Called by platform notification code (Linux netlink
                 * read callback, BSD route-socket callback, Windows
                 * change-notify thunk) from any thread.  Public so
                 * platform TUs can drive the monitor without needing
                 * a friend declaration.
                 */
                void kickDebounce();

        private:
                // Runs the diff cycle synchronously on the monitor's
                // affinity thread.  Called by the debounce timer or
                // @ref testForceRescan.
                void runDiff();

                bool         _running     = false;
                int          _debounceTimer = -1;
                unsigned int _debounceMs  = DefaultDebounceMs;

                struct PreviousEntry {
                        NetworkInterfaceImplPtr impl;
                        NetworkInterfaceData    data;
                };
                // Keyed by raw impl pointer (const because SharedPtr
                // exposes only @c const access) — Stage 1 stabilises
                // impl identity so the pointer is a stable per-iface
                // key.  The @c PreviousEntry holds a SharedPtr too, so
                // the impl outlives a registry-driven eviction
                // mid-cycle.
                struct Private;
                Private *_priv;
};

/**
 * @brief Platform-provided open hook.
 *
 * Each platform TU (linuxnetworkinterfacemonitor.cpp,
 * bsdnetworkinterfacemonitor.cpp, windowsnetworkinterfacemonitor.cpp)
 * defines this; the platform-neutral common TU declares it and links
 * against whichever platform implementation CMake selects.  When
 * @c PROMEKI_PLATFORM_* matches no supported notification source the
 * common TU's stub returns @c Error::Ok so the monitor degrades to
 * an idle no-op.
 *
 * @param m The monitor to wire up.  Implementations should register
 *          their notification source with @c m->eventLoop and call
 *          @c m->kickDebounce on each notification.
 */
Error networkInterfaceMonitorPlatformOpen(NetworkInterfaceMonitor *m);

/** @brief Platform-provided close hook. */
void networkInterfaceMonitorPlatformClose(NetworkInterfaceMonitor *m);

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NETWORK
