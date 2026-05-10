/**
 * @file      networkinterfacebackend.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/networkinterfacebackend.h>
#include <promeki/stringlist.h>
#include <promeki/map.h>
#include <promeki/set.h>
#include <promeki/mutex.h>

#include <chrono>

PROMEKI_NAMESPACE_BEGIN

namespace {

        using BackendList = List<NetworkInterfaceBackend *>;

        // Composite key that pins an impl to a specific (backend, name,
        // index) tuple.  The kernel index is part of the key so an
        // interface that disappears and a same-name one with a
        // different ifindex don't alias.
        struct ImplKey {
                String   backendName;
                String   ifName;
                uint32_t ifIndex = 0;

                bool operator<(const ImplKey &o) const {
                        if (backendName < o.backendName) return true;
                        if (o.backendName < backendName) return false;
                        if (ifName < o.ifName) return true;
                        if (o.ifName < ifName) return false;
                        return ifIndex < o.ifIndex;
                }
                bool operator==(const ImplKey &o) const {
                        return backendName == o.backendName && ifName == o.ifName && ifIndex == o.ifIndex;
                }
        };

        using ImplCache = Map<ImplKey, NetworkInterfaceImplPtr>;

        using SteadyClock = std::chrono::steady_clock;
        using SteadyTime  = SteadyClock::time_point;

        struct Registry {
                Mutex                                    mutex;
                BackendList                              backends;        // Owned.
                ImplCache                                implCache;       // Stable impls keyed by (backendName, ifName, ifIndex).
                NetworkInterfaceBackend::ImplList        cachedEnumerationResult;
                SteadyTime                               lastEnumeration{};
                bool                                     hasCachedEnumeration = false;
                unsigned int                             ttlMs = NetworkInterfaceBackend::DefaultEnumerationTtlMs;
        };

        // Construct-On-First-Use to keep static-init registration safe
        // regardless of translation-unit ordering.
        Registry &registry() {
                static Registry r;
                return r;
        }

        // Insert @p backend into @p list keeping sorted-by-priority order
        // (lower priority first; ties preserve insertion order).
        void insertSorted(BackendList &list, NetworkInterfaceBackend *backend) {
                int prio = backend->priority();
                size_t i = 0;
                for (; i < list.size(); ++i) {
                        if (list[i]->priority() > prio) break;
                }
                list.insert(i, backend);
        }

        // Builds a "down" snapshot from the existing one: clears
        // address bindings and link-state booleans, but preserves
        // identifying fields (name, friendlyName, index, MAC list,
        // kind) so diagnostics aren't blanked out when an interface
        // disappears.
        NetworkInterfaceData makeDownSnapshot(const NetworkInterfaceData &prev) {
                NetworkInterfaceData snap;
                snap.name         = prev.name;
                snap.friendlyName = prev.friendlyName;
                snap.index        = prev.index;
                snap.macAddresses = prev.macAddresses;
                snap.kind         = prev.kind;
                snap.mtu          = prev.mtu;
                // ipv4Subnets / ipv6Subnets, isUp, isRunning, hasCarrier
                // all default-construct to empty / false — the entire
                // point of the down snapshot.
                return snap;
        }

}

void NetworkInterfaceBackend::registerBackend(NetworkInterfaceBackend *backend) {
        if (backend == nullptr) return;
        Registry &reg = registry();
        Mutex::Locker lock(reg.mutex);

        // Replace any existing backend with the same name so a later
        // registration wins (used by tests that swap a stub backend
        // in for a real one).
        const String name = backend->name();
        for (size_t i = 0; i < reg.backends.size(); ++i) {
                if (reg.backends[i]->name() == name) {
                        // Drop cache entries that came from this backend
                        // before we delete it.
                        for (auto it = reg.implCache.begin(); it != reg.implCache.end();) {
                                if (it->first.backendName == name) {
                                        if (it->second.isValid()) {
                                                it->second.modify()->replaceData(
                                                        makeDownSnapshot(it->second->data()));
                                        }
                                        it = reg.implCache.remove(it);
                                } else {
                                        ++it;
                                }
                        }
                        delete reg.backends[i];
                        reg.backends.remove(i);
                        break;
                }
        }
        insertSorted(reg.backends, backend);
        // A new or replaced backend changes the enumeration result.
        reg.hasCachedEnumeration = false;
        reg.cachedEnumerationResult.clear();
}

void NetworkInterfaceBackend::unregisterBackend(const String &name) {
        Registry     &reg = registry();
        Mutex::Locker lock(reg.mutex);
        for (size_t i = 0; i < reg.backends.size(); ++i) {
                if (reg.backends[i]->name() == name) {
                        // Refresh every cache entry tagged with this
                        // backend to a down snapshot so callers holding
                        // handles see the iface go away rather than
                        // freeze on the last-known state.
                        for (auto it = reg.implCache.begin(); it != reg.implCache.end();) {
                                if (it->first.backendName == name) {
                                        if (it->second.isValid()) {
                                                it->second.modify()->replaceData(
                                                        makeDownSnapshot(it->second->data()));
                                        }
                                        it = reg.implCache.remove(it);
                                } else {
                                        ++it;
                                }
                        }
                        delete reg.backends[i];
                        reg.backends.remove(i);
                        reg.hasCachedEnumeration = false;
                        reg.cachedEnumerationResult.clear();
                        return;
                }
        }
}

StringList NetworkInterfaceBackend::registeredBackends() {
        Registry     &reg = registry();
        Mutex::Locker lock(reg.mutex);
        StringList    out;
        for (auto *b : reg.backends) out.pushToBack(b->name());
        return out;
}

NetworkInterfaceBackend::ImplList NetworkInterfaceBackend::enumerateAll() {
        Registry     &reg = registry();
        Mutex::Locker lock(reg.mutex);

        // TTL fast-path: if a recent cached result is still warm,
        // return it without walking any backend.  Tight loops calling
        // findRoutesTo / findByName get this for free; subscribers to
        // a NetworkInterfaceMonitor invalidate the cache before each
        // diff cycle so they always see fresh data.
        if (reg.ttlMs > 0 && reg.hasCachedEnumeration) {
                auto now = SteadyClock::now();
                auto age = std::chrono::duration_cast<std::chrono::milliseconds>(now - reg.lastEnumeration);
                if (age.count() >= 0 && static_cast<unsigned int>(age.count()) < reg.ttlMs) {
                        return reg.cachedEnumerationResult;
                }
        }

        // Walk each backend in priority order, deduplicate by ifName
        // across backends (lower priority wins — backends are already
        // sorted, so seenNames is populated in priority order), and
        // fold each surviving record into the cache via replaceData
        // on hit, push-new on miss.  Holding the registry mutex
        // through @c enumerate() is safe because production backends
        // (POSIX getifaddrs, Windows IPHelper, hardware NIC SDKs)
        // never re-enter the registry — they only call OS APIs.
        ImplList     out;
        Set<String>  seenNames;
        Set<ImplKey> refreshed;

        for (auto *backend : reg.backends) {
                const String backendName = backend->name();
                ImplList     part        = backend->enumerate();
                for (auto &impl : part) {
                        if (!impl.isValid()) continue;
                        const NetworkInterfaceData freshData = impl->data();
                        const String              &nm        = freshData.name;
                        if (seenNames.contains(nm)) continue; // higher-priority backend already claimed it
                        seenNames.insert(nm);

                        ImplKey key;
                        key.backendName = backendName;
                        key.ifName      = nm;
                        key.ifIndex     = freshData.index;

                        auto it = reg.implCache.find(key);
                        if (it != reg.implCache.end() && it->second.isValid()) {
                                it->second.modify()->replaceData(freshData);
                                out.pushToBack(it->second);
                        } else {
                                reg.implCache.insert(key, impl);
                                out.pushToBack(impl);
                        }
                        refreshed.insert(key);
                }
        }

        // Anything in the cache that wasn't refreshed this cycle has
        // disappeared.  Roll the impl forward to a "down" snapshot so
        // existing handle holders see the change, then drop the
        // cache entry — keeping the cache bounded by currently-up
        // interfaces.
        for (auto it = reg.implCache.begin(); it != reg.implCache.end();) {
                if (refreshed.contains(it->first)) {
                        ++it;
                        continue;
                }
                if (it->second.isValid()) {
                        it->second.modify()->replaceData(makeDownSnapshot(it->second->data()));
                }
                it = reg.implCache.remove(it);
        }

        reg.cachedEnumerationResult = out;
        reg.lastEnumeration         = SteadyClock::now();
        reg.hasCachedEnumeration    = true;
        return out;
}

void NetworkInterfaceBackend::invalidateEnumerationCache() {
        Registry     &reg = registry();
        Mutex::Locker lock(reg.mutex);
        reg.hasCachedEnumeration = false;
        reg.cachedEnumerationResult.clear();
}

unsigned int NetworkInterfaceBackend::enumerationTtlMs() {
        Registry     &reg = registry();
        Mutex::Locker lock(reg.mutex);
        return reg.ttlMs;
}

void NetworkInterfaceBackend::setEnumerationTtlMs(unsigned int ms) {
        Registry     &reg = registry();
        Mutex::Locker lock(reg.mutex);
        reg.ttlMs = ms;
        if (ms == 0) {
                reg.hasCachedEnumeration = false;
                reg.cachedEnumerationResult.clear();
        }
}

PROMEKI_NAMESPACE_END
