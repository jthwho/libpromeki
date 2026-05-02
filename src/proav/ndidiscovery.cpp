/**
 * @file      ndidiscovery.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/config.h>

#if PROMEKI_ENABLE_NDI

#include <promeki/ndidiscovery.h>

#include <algorithm>
#include <promeki/logger.h>
#include <promeki/ndilib.h>
#include <promeki/thread.h>

#include <Processing.NDI.Lib.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

        constexpr int    kDefaultPollIntervalMs = 500;
        constexpr int    kMaxWaitMs             = 60'000;
        constexpr int    kMaxUptimeWaitMs       = 30'000;

        // Build the comma-separated string the SDK expects, or nullptr
        // when empty (the SDK treats nullptr as "default").  We hold
        // the backing storage in a String the caller pins for the
        // lifetime of the NDIlib_find_create_t struct.
        const char *cstrOrNull(const String &s) { return s.isEmpty() ? nullptr : s.cstr(); }

} // namespace

NdiDiscovery &NdiDiscovery::instance() {
        static NdiDiscovery s;
        return s;
}

NdiDiscovery::NdiDiscovery() {
        // Anchor the NdiLib singleton so its destructor runs *after*
        // ours (static destructor order: last-constructed runs first).
        // If the lib failed to load there is nothing to discover —
        // the singleton stays in the not-running state and every
        // accessor returns empty / false.
        NdiLib &lib = NdiLib::instance();
        if (!lib.isLoaded()) {
                promekiWarn("NdiDiscovery: NDI runtime not loaded — discovery disabled");
                return;
        }
        _worker = std::thread([this] { workerMain(); });
}

NdiDiscovery::~NdiDiscovery() {
        _shutdown.store(true, std::memory_order_release);
        {
                Mutex::Locker lk(_mutex);
                _condConfig.wakeAll();
        }
        if (_worker.joinable()) {
                _worker.join();
        }
}

NdiDiscovery::RecordList NdiDiscovery::sources() const {
        RecordList    out;
        Mutex::Locker lk(_mutex);
        out.reserve(_registry.size());
        for (const auto &kv : _registry) {
                out.pushToBack(kv.second);
        }
        return out;
}

NdiDiscovery::RecordList NdiDiscovery::sources(int minUptimeMs) const {
        if (!_running.load(std::memory_order_acquire)) {
                return RecordList();
        }
        if (minUptimeMs <= 0) {
                return sources();
        }
        if (minUptimeMs > kMaxUptimeWaitMs) {
                minUptimeMs = kMaxUptimeWaitMs;
        }
        // The worker sets _startTime exactly once before flipping the
        // _running flag we already checked above — no race here.
        int64_t alreadyUp = _startTime.elapsedMilliseconds();
        if (alreadyUp >= minUptimeMs) {
                return sources();
        }
        unsigned int remaining = static_cast<unsigned int>(minUptimeMs - alreadyUp);
        Mutex::Locker lk(_mutex);
        // Wait on _condRegistry which the worker pulses on every tick.
        // Even if no source ever appears, we'll fall through after the
        // remaining-uptime budget elapses.
        _condRegistry.wait(_mutex, remaining);
        // Lock is auto-released by the Locker on return.
        RecordList out;
        out.reserve(_registry.size());
        for (const auto &kv : _registry) {
                out.pushToBack(kv.second);
        }
        return out;
}

String NdiDiscovery::waitForSource(const String &nameOrPattern, int timeoutMs) {
        // The worker thread is started in the constructor when the
        // SDK is loaded; it may not have flipped @c _running on yet
        // when the very first caller (often the URL-driven open path)
        // arrives.  Short-circuiting on @c _running here would race
        // that startup and surface as an instant "not found" instead
        // of honouring the configured wait — the SDK-not-loaded case
        // is the one we actually want to bail on.
        if (!NdiLib::instance().isLoaded()) return String();
        if (timeoutMs < 0) timeoutMs = 0;
        if (timeoutMs > kMaxWaitMs) timeoutMs = kMaxWaitMs;

        // A full canonical name (`Machine (Source)`) is matched
        // exactly; a source-only name is matched against the
        // parenthesised tail of every registered canonical, which
        // is how `ndi:///<source>` resolves to "any machine".
        const bool   isFullCanonical = nameOrPattern.contains('(');
        const String suffix          = isFullCanonical ? String() : (String(" (") + nameOrPattern + ")");

        auto findMatch = [&]() -> String {
                if (isFullCanonical) {
                        auto it = _registry.find(nameOrPattern);
                        return it != _registry.end() ? it->second.canonicalName : String();
                }
                for (const auto &kv : _registry) {
                        if (kv.second.canonicalName.endsWith(suffix)) {
                                return kv.second.canonicalName;
                        }
                }
                return String();
        };

        TimeStamp     deadline = TimeStamp::now();
        Mutex::Locker lk(_mutex);
        String        match = findMatch();
        if (!match.isEmpty()) return match;
        for (;;) {
                int64_t elapsed = deadline.elapsedMilliseconds();
                if (elapsed >= timeoutMs) return String();
                unsigned int remaining = static_cast<unsigned int>(timeoutMs - elapsed);
                _condRegistry.wait(_mutex, remaining);
                match = findMatch();
                if (!match.isEmpty()) return match;
                if (_shutdown.load(std::memory_order_acquire)) return String();
        }
}

void NdiDiscovery::setPollIntervalMs(int ms) {
        if (ms < 50) ms = 50;
        if (ms > 60'000) ms = 60'000;
        _pollIntervalMs.store(ms, std::memory_order_release);
}

void NdiDiscovery::setGroups(const String &commaSeparated) {
        Mutex::Locker lk(_mutex);
        if (_groups == commaSeparated) return;
        _groups = commaSeparated;
        _configDirty.store(true, std::memory_order_release);
        _condConfig.wakeAll();
}

void NdiDiscovery::setExtraIps(const String &commaSeparated) {
        Mutex::Locker lk(_mutex);
        if (_extraIps == commaSeparated) return;
        _extraIps = commaSeparated;
        _configDirty.store(true, std::memory_order_release);
        _condConfig.wakeAll();
}

int NdiDiscovery::pollIntervalMs() const {
        return _pollIntervalMs.load(std::memory_order_acquire);
}

int64_t NdiDiscovery::uptimeMs() const {
        if (!_running.load(std::memory_order_acquire)) return 0;
        return _startTime.elapsedMilliseconds();
}

void NdiDiscovery::workerMain() {
        Thread::setCurrentThreadName("ndi-discovery");

        const NDIlib_v6 *api = NdiLib::instance().api();
        if (!api || !api->find_create_v2 || !api->find_destroy ||
            !api->find_wait_for_sources || !api->find_get_current_sources) {
                promekiErr("NdiDiscovery: NDI function table missing required find_* entries");
                return;
        }

        // Snapshot the configured values once per recreate-cycle.  The
        // SDK requires the strings to remain valid for the lifetime
        // of the NDIlib_find_create_t we hand to find_create_v2 — we
        // hold them in local Strings inside this function.
        String        groupsLocal;
        String        extraIpsLocal;
        NDIlib_find_instance_t find = nullptr;

        auto buildFind = [&]() -> NDIlib_find_instance_t {
                {
                        Mutex::Locker lk(_mutex);
                        groupsLocal   = _groups;
                        extraIpsLocal = _extraIps;
                        _configDirty.store(false, std::memory_order_release);
                }
                NDIlib_find_create_t cfg;
                cfg.show_local_sources = true;
                cfg.p_groups           = cstrOrNull(groupsLocal);
                cfg.p_extra_ips        = cstrOrNull(extraIpsLocal);
                NDIlib_find_instance_t f = api->find_create_v2(&cfg);
                if (!f) {
                        promekiErr("NdiDiscovery: NDIlib_find_create_v2 returned NULL");
                }
                return f;
        };

        find = buildFind();
        if (!find) return;

        _startTime.update();
        _running.store(true, std::memory_order_release);

        while (!_shutdown.load(std::memory_order_acquire)) {
                if (_configDirty.load(std::memory_order_acquire)) {
                        api->find_destroy(find);
                        find = buildFind();
                        if (!find) break;
                }

                int waitMs = _pollIntervalMs.load(std::memory_order_acquire);
                // The SDK call returns true if anything changed during
                // the wait — either way we then snapshot the current
                // source list, so the return value is purely an
                // optimisation hint we don't need.
                api->find_wait_for_sources(find, static_cast<uint32_t>(waitMs));
                if (_shutdown.load(std::memory_order_acquire)) break;

                uint32_t                count = 0;
                const NDIlib_source_t *list = api->find_get_current_sources(find, &count);

                {
                        Mutex::Locker lk(_mutex);
                        TimeStamp now = TimeStamp::now();
                        for (uint32_t i = 0; i < count; ++i) {
                                const NDIlib_source_t &s = list[i];
                                if (!s.p_ndi_name) continue;
                                String name = s.p_ndi_name;
                                String url = s.p_url_address ? String(s.p_url_address) : String();
                                auto it = _registry.find(name);
                                if (it == _registry.end()) {
                                        Record r;
                                        r.canonicalName = name;
                                        r.urlAddress    = url;
                                        r.firstSeen     = now;
                                        r.lastSeen      = now;
                                        _registry.insert(name, r);
                                } else {
                                        it->second.lastSeen   = now;
                                        if (!url.isEmpty()) it->second.urlAddress = url;
                                }
                        }
                        _condRegistry.wakeAll();
                }
        }

        if (find) {
                api->find_destroy(find);
        }
        _running.store(false, std::memory_order_release);
}

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NDI
