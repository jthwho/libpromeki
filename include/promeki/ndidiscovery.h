/**
 * @file      ndidiscovery.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/config.h>

#if PROMEKI_ENABLE_NDI

#include <atomic>
#include <thread>
#include <promeki/list.h>
#include <promeki/map.h>
#include <promeki/mutex.h>
#include <promeki/string.h>
#include <promeki/timestamp.h>
#include <promeki/waitcondition.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Process-wide NDI source discovery, lazily started on first use.
 * @ingroup proav
 *
 * Wraps the NDI SDK's discovery API (`NDIlib_find_*`) in a single
 * background thread that runs for the lifetime of the application
 * once it has been started.  All NDI source enumeration in libpromeki
 * goes through this — `NdiFactory::enumerate()` snapshots its
 * registry, and `NdiMediaIO::openSource()` calls
 * @ref waitForSource to confirm a requested source is currently
 * advertised before opening a receiver.
 *
 * @par Lazy startup
 *
 * The singleton constructs on the first @ref instance() call and not
 * before.  No work happens at static-init or during @ref NdiLib's
 * own bootstrap — a process that only sends NDI never spins up a
 * discovery thread at all.  Once started the singleton (and its
 * worker thread + `NDIlib_find_instance_t`) live until process
 * exit; there is no @c shutdown() / @c restart() surface.
 *
 * The two callers that ever trigger that first @c instance() call:
 *
 *  - @c NdiFactory::enumerate() — for probe / discovery surfaces
 *    (e.g. @c mediaplay @c --probe @c ndi).
 *  - @c NdiMediaIO::openSource() — to validate a requested source
 *    name before calling `NDIlib_recv_create_v3()`.
 *
 * @par Registry semantics
 *
 * The worker calls `NDIlib_find_wait_for_sources()` followed by
 * `NDIlib_find_get_current_sources()` once per @ref pollIntervalMs
 * interval.  Each tick's snapshot is merged into a mutex-protected
 * registry so callers see:
 *
 *  - Sources that appeared at any point — even if they're not in
 *    the *current* tick's snapshot — until the registry is
 *    explicitly trimmed (not currently exposed; sources persist for
 *    the process lifetime).
 *  - First-seen and last-seen timestamps for each source, so callers
 *    can reason about freshness.
 *
 * @par Thread safety
 *
 * All public accessors are thread-safe.  Mutator entry points
 * (@ref setPollIntervalMs, @ref setExtraIps, @ref setGroups) take
 * effect on the next worker iteration — they signal the worker via
 * a wait condition rather than blocking on a long mid-cycle wait.
 *
 * @par Failure handling
 *
 * If @ref NdiLib::isLoaded() is false, @ref instance() still
 * returns a singleton but its @ref isRunning() reports @c false and
 * @ref sources() always returns empty.  Callers should not need to
 * special-case the failure path — an empty registry is a valid
 * "no sources" answer.
 */
class NdiDiscovery {
        public:
                /**
                 * @brief A source seen by the discovery thread at some point.
                 *
                 * Records both first-seen and last-seen timestamps so callers
                 * can age out sources that have stopped advertising.  The
                 * @c canonicalName is what NDI's SDK exposes — a string of
                 * the form `MachineName (SourceName)`.
                 */
                struct Record {
                                String    canonicalName;   ///< @c MachineName (Source)
                                String    urlAddress;      ///< @c ip:port if SDK supplied one, else empty.
                                TimeStamp firstSeen;       ///< When this source was first noted.
                                TimeStamp lastSeen;        ///< Last tick the SDK still advertised it.
                };

                /// @brief Convenience type for snapshot lists.
                using RecordList = ::promeki::List<Record>;

                /**
                 * @brief Returns the singleton, constructing it on first call.
                 *
                 * Thread-safe.  The first call spins up the discovery thread;
                 * subsequent calls are cheap.
                 */
                static NdiDiscovery &instance();

                /**
                 * @brief True when the discovery worker thread is running.
                 *
                 * False when @ref NdiLib failed to load (the singleton still
                 * exists for API uniformity but does no work).
                 */
                bool isRunning() const { return _running.load(std::memory_order_acquire); }

                /**
                 * @brief Snapshot the current registry without waiting.
                 *
                 * Returns immediately with whatever the worker has
                 * accumulated so far.  Cheap (one mutex acquisition + a
                 * copy of the list).
                 */
                RecordList sources() const;

                /**
                 * @brief Snapshot the registry, blocking until the worker has been up at least @p minUptimeMs.
                 *
                 * Useful for the first probe in a fresh process — gives the
                 * mDNS protocol a moment to discover the network before
                 * reporting back.  Subsequent calls return instantly because
                 * the worker has been running for longer.
                 *
                 * @param minUptimeMs Minimum worker uptime (ms) before the
                 *                    snapshot is taken.  @c 0 disables the
                 *                    wait — equivalent to the no-arg overload.
                 *                    Capped at 30 seconds defensively.
                 */
                RecordList sources(int minUptimeMs) const;

                /**
                 * @brief Block until a matching source appears in the registry.
                 *
                 * Returns the canonical name (`MachineName (Source)`) of the
                 * first registry entry that matches @p nameOrPattern, or an
                 * empty @c String if the timeout elapses first.
                 *
                 * Two match forms are accepted:
                 *  - Full canonical (contains `(`): host portion matched
                 *    case-insensitively (DNS hostnames are case-folded per
                 *    RFC 1035, and the NDI SDK can report a different host
                 *    casing than the OS hostname); source portion matched
                 *    exactly.
                 *  - Source-only (no `(`): matches any registry entry whose
                 *    canonical ends with `" (<nameOrPattern>)"` — i.e. any
                 *    machine on the network advertising that source name.
                 *    Used by the `ndi:///<source>` URL form.
                 *
                 * If multiple registry entries satisfy a source-only pattern,
                 * the first one encountered in the registry's iteration order
                 * is returned.  Callers that need deterministic disambiguation
                 * should pass the full canonical name.
                 *
                 * @param nameOrPattern Full canonical or source-only name.
                 * @param timeoutMs     Maximum wait, in milliseconds.  Capped
                 *                      at 60 seconds defensively.
                 */
                String waitForSource(const String &nameOrPattern, int timeoutMs);

                /**
                 * @brief Apply waitForSource's matching rules to a record snapshot.
                 *
                 * Pure function over a snapshot — no locking, no SDK
                 * calls — so it's the testable core of @ref waitForSource.
                 * Returns the canonical name of the first matching record,
                 * or an empty @c String if nothing matches.  See
                 * @ref waitForSource for the full match-rule docs.
                 */
                static String matchCanonical(const RecordList &records,
                                             const String     &nameOrPattern);

                /**
                 * @brief Set the worker's between-poll interval.
                 *
                 * Default 500 ms.  The new value is read at the top of the
                 * next worker iteration.
                 */
                void setPollIntervalMs(int ms);

                /**
                 * @brief Set the comma-separated NDI groups to advertise to.
                 *
                 * Empty (default) discovers every group.  Changing this
                 * destroys and recreates the underlying @c NDIlib_find
                 * instance on the next worker iteration.
                 */
                void setGroups(const String &commaSeparated);

                /**
                 * @brief Set the comma-separated extra IPs / hostnames for non-mDNS discovery.
                 *
                 * For non-broadcast subnets where mDNS / Bonjour cannot
                 * traverse the network boundary.  Same recreate-on-change
                 * semantics as @ref setGroups.
                 */
                void setExtraIps(const String &commaSeparated);

                /**
                 * @brief Returns the configured poll interval in milliseconds.
                 */
                int pollIntervalMs() const;

                /**
                 * @brief Returns the worker uptime in milliseconds since first start.
                 *
                 * Returns @c 0 when the worker never started (NDI library
                 * load failure).
                 */
                int64_t uptimeMs() const;

                NdiDiscovery(const NdiDiscovery &) = delete;
                NdiDiscovery &operator=(const NdiDiscovery &) = delete;

        private:
                NdiDiscovery();
                ~NdiDiscovery();

                void workerMain();

                mutable Mutex                 _mutex;
                mutable WaitCondition         _condRegistry; ///< Notified when a tick updates the registry.
                WaitCondition                 _condConfig;   ///< Notified when config changes (wakes the worker early).
                Map<String, Record>           _registry;     ///< canonicalName → Record.
                std::atomic<bool>             _running{false};
                std::atomic<bool>             _shutdown{false};
                std::atomic<bool>             _configDirty{false}; ///< Worker recreates find instance on next iter.
                std::atomic<int>              _pollIntervalMs{500};
                String                        _groups;       ///< Mirror of the SDK config (mutex-protected).
                String                        _extraIps;     ///< Mirror of the SDK config (mutex-protected).
                TimeStamp                     _startTime;    ///< Set when the worker thread first runs.
                std::thread                   _worker;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NDI
