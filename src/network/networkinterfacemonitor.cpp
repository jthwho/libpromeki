/**
 * @file      networkinterfacemonitor.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/networkinterfacemonitor.h>
#include <promeki/networkinterfacebackend.h>
#include <promeki/eventloop.h>
#include <promeki/mutex.h>
#include <promeki/map.h>
#include <promeki/set.h>
#include <promeki/list.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_DEBUG(NetworkInterfaceMonitor)

struct NetworkInterfaceMonitor::Private {
        // Keyed by raw impl pointer — pointer identity is stable per
        // physical interface thanks to Stage 1 impl-stabilisation.
        Map<const NetworkInterfaceImpl *, PreviousEntry> previous;
};

namespace {
        struct MonitorRegistry {
                Mutex                            mutex;
                List<NetworkInterfaceMonitor *> monitors;
        };

        MonitorRegistry &registry() {
                static MonitorRegistry r;
                return r;
        }

        void registerMonitor(NetworkInterfaceMonitor *m) {
                MonitorRegistry &r = registry();
                Mutex::Locker    lock(r.mutex);
                r.monitors.pushToBack(m);
        }

        void unregisterMonitor(NetworkInterfaceMonitor *m) {
                MonitorRegistry &r = registry();
                Mutex::Locker    lock(r.mutex);
                for (size_t i = 0; i < r.monitors.size(); ++i) {
                        if (r.monitors[i] == m) {
                                r.monitors.remove(i);
                                return;
                        }
                }
        }
}

NetworkInterfaceMonitor::NetworkInterfaceMonitor(ObjectBase *parent)
    : ObjectBase(parent), _priv(new Private()) {}

NetworkInterfaceMonitor::~NetworkInterfaceMonitor() {
        if (_running) stop();
        delete _priv;
}

Error NetworkInterfaceMonitor::start() {
        if (_running) return Error::Ok;
        Error err = networkInterfaceMonitorPlatformOpen(this);
        if (err.isError()) {
                promekiWarn("NetworkInterfaceMonitor::start: platform open failed: %s", err.name().cstr());
                return err;
        }
        _running = true;
        registerMonitor(this);

        // Prime the previous-state cache from a first enumeration —
        // no signals fire on priming since there's no prior state to
        // diff against.
        auto first = NetworkInterface::enumerate();
        for (const auto &iface : first) {
                if (!iface.isValid()) continue;
                const NetworkInterfaceImpl *raw = iface.impl().ptr();
                _priv->previous.insert(raw, PreviousEntry{iface.impl(), iface.data()});
        }
        return Error::Ok;
}

void NetworkInterfaceMonitor::stop() {
        if (!_running) return;
        if (_debounceTimer >= 0) {
                stopTimer(_debounceTimer);
                _debounceTimer = -1;
        }
        networkInterfaceMonitorPlatformClose(this);
        unregisterMonitor(this);
        _running = false;
}

void NetworkInterfaceMonitor::testForceRescan() {
        runDiff();
}

NetworkInterfaceMonitor *NetworkInterfaceMonitor::anyRunning() {
        MonitorRegistry &r = registry();
        Mutex::Locker    lock(r.mutex);
        return r.monitors.isEmpty() ? nullptr : r.monitors.front();
}

// Default no-op platform implementation.  Linker behaviour: the
// platform-specific TU (e.g. linuxnetworkinterfacemonitor.cpp) is
// only added to the build on its target OS, and that TU defines a
// non-stub version of these symbols.  When no platform TU is
// selected the common TU keeps the monitor running as an idle stub.
// Marked weak so a platform TU's strong definition wins at link.
#if !defined(PROMEKI_PLATFORM_LINUX) && !defined(PROMEKI_PLATFORM_BSD) && !defined(PROMEKI_PLATFORM_WINDOWS)
Error networkInterfaceMonitorPlatformOpen(NetworkInterfaceMonitor *) { return Error::Ok; }
void  networkInterfaceMonitorPlatformClose(NetworkInterfaceMonitor *) {}
#endif

void NetworkInterfaceMonitor::kickDebounce() {
        // Re-arm the single-shot debounce timer.  When called from
        // the OS notification thread we cross into the monitor's
        // affinity thread via postCallable so the timer arm + diff
        // cycle stay on the right loop.
        EventLoop *loop = eventLoop();
        if (loop == nullptr) {
                runDiff();
                return;
        }
        static const auto kKickLabel = EventLoop::Label{"NetIfMonitor.kickDebounce"};
        loop->postCallable(kKickLabel, [this]() {
                if (!_running) return;
                if (_debounceTimer >= 0) {
                        stopTimer(_debounceTimer);
                        _debounceTimer = -1;
                }
                if (_debounceMs == 0) {
                        runDiff();
                        return;
                }
                _debounceTimer = eventLoop()->startTimer(_debounceMs,
                        [this]() {
                                _debounceTimer = -1;
                                runDiff();
                        }, /*singleShot=*/ true);
        });
}

void NetworkInterfaceMonitor::runDiff() {
        if (!_running) return;
        // Force the registry to re-read each backend so we observe
        // the change that triggered this cycle.  Stage 5 wires this
        // into the TTL on enumerate; until then the call is harmless.
        NetworkInterfaceBackend::invalidateEnumerationCache();

        auto current = NetworkInterface::enumerate();

        // Track which impl pointers from @c _priv->previous survived
        // this cycle so we can emit removed for the rest.
        Set<const NetworkInterfaceImpl *> seenRaw;

        // Pass 1 — added / linkUp / linkDown / address-set diff for
        // current interfaces.
        for (const auto &iface : current) {
                if (!iface.isValid()) continue;
                const NetworkInterfaceImpl *raw = iface.impl().ptr();
                seenRaw.insert(raw);
                NetworkInterfaceData freshData = iface.data();
                auto                 it       = _priv->previous.find(raw);
                if (it == _priv->previous.end()) {
                        interfaceAddedSignal.emit(iface);
                        if (freshData.isRunning) linkUpSignal.emit(iface);
                        for (const auto &s : freshData.ipv4Subnets) {
                                addressAddedIpv4Signal.emit(iface, s.address());
                        }
                        for (const auto &s : freshData.ipv6Subnets) {
                                addressAddedIpv6Signal.emit(iface, s.address());
                        }
                        continue;
                }
                const NetworkInterfaceData &prev = it->second.data;
                if (prev.isRunning != freshData.isRunning) {
                        if (freshData.isRunning) linkUpSignal.emit(iface);
                        else                     linkDownSignal.emit(iface);
                }
                // IPv4 address-set diff: anything in fresh not in prev → added,
                // anything in prev not in fresh → removed.  The lists are
                // small in practice so the O(N*M) walk is fine.
                for (const auto &fresh : freshData.ipv4Subnets) {
                        bool found = false;
                        for (const auto &p : prev.ipv4Subnets) {
                                if (p.address() == fresh.address()) {
                                        found = true;
                                        break;
                                }
                        }
                        if (!found) addressAddedIpv4Signal.emit(iface, fresh.address());
                }
                for (const auto &p : prev.ipv4Subnets) {
                        bool found = false;
                        for (const auto &fresh : freshData.ipv4Subnets) {
                                if (p.address() == fresh.address()) {
                                        found = true;
                                        break;
                                }
                        }
                        if (!found) addressRemovedIpv4Signal.emit(iface, p.address());
                }
                for (const auto &fresh : freshData.ipv6Subnets) {
                        bool found = false;
                        for (const auto &p : prev.ipv6Subnets) {
                                if (p.address() == fresh.address()) {
                                        found = true;
                                        break;
                                }
                        }
                        if (!found) addressAddedIpv6Signal.emit(iface, fresh.address());
                }
                for (const auto &p : prev.ipv6Subnets) {
                        bool found = false;
                        for (const auto &fresh : freshData.ipv6Subnets) {
                                if (p.address() == fresh.address()) {
                                        found = true;
                                        break;
                                }
                        }
                        if (!found) addressRemovedIpv6Signal.emit(iface, p.address());
                }
        }

        // Pass 2 — removed for previous entries that didn't survive.
        // The PreviousEntry's SharedPtr keeps the impl alive even if
        // the registry already evicted the cache entry.
        for (auto it = _priv->previous.begin(); it != _priv->previous.end();) {
                if (seenRaw.contains(it->first)) {
                        ++it;
                        continue;
                }
                interfaceRemovedSignal.emit(NetworkInterface(it->second.impl));
                it = _priv->previous.remove(it);
        }

        // Rebuild previous from current.
        for (const auto &iface : current) {
                const NetworkInterfaceImpl *raw = iface.impl().ptr();
                _priv->previous.insert(raw, PreviousEntry{iface.impl(), iface.data()});
        }

        interfacesChangedSignal.emit();
}

PROMEKI_NAMESPACE_END
