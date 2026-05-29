/**
 * @file      mdnsbrowser.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_MDNS
#include <promeki/namespace.h>
#include <promeki/atomic.h>
#include <promeki/buffer.h>
#include <promeki/duration.h>
#include <promeki/error.h>
#include <promeki/list.h>
#include <promeki/map.h>
#include <promeki/mdnsrecord.h>
#include <promeki/mdnsserviceinstance.h>
#include <promeki/mdnsservicetype.h>
#include <promeki/mutex.h>
#include <promeki/networkinterface.h>
#include <promeki/objectbase.h>
#include <promeki/socketaddress.h>
#include <promeki/string.h>
#include <promeki/timestamp.h>

PROMEKI_NAMESPACE_BEGIN

class MdnsManager;
struct MdnsParsedRecord;

/**
 * @brief Discovers services of a given @ref MdnsServiceType on the local network.
 * @ingroup network
 *
 * @c MdnsBrowser is the passive-mode receive path landed in step 4.
 * It subscribes to a single service type, consumes mDNS packets
 * delivered by an attached @ref MdnsManager (or fed directly through
 * @ref handlePacket in tests), maintains a @c Map of currently-known
 * @ref MdnsServiceInstance objects keyed by instance FQDN, and emits
 * signals as the population changes:
 *
 *  - @ref serviceFound — the first time an instance becomes
 *    addressable (PTR + SRV both known, so the instance has a port
 *    and a target hostname).
 *  - @ref serviceUpdated — any subsequent record (TXT update, new A /
 *    AAAA, repeated SRV with changed port / target) on an already-
 *    found instance.
 *  - @ref serviceLost — a Goodbye PTR (TTL=0) for an instance we
 *    previously found.
 *
 * Step 4 covers the receive side only.  Continuous-query backoff,
 * timer-driven TTL expiration, multi-interface attribution, and IPv6
 * land in step 5; until then the browser is purely reactive and the
 * cache shrinks only via Goodbye records.
 *
 * @par Threading model
 *
 * @ref handlePacket is the single entrypoint and is intended to be
 * called from the @ref MdnsManager worker thread.  Signal slots are
 * dispatched through the connecting object's @ref EventLoop per the
 * project's standard cross-thread marshalling, so consumers do not
 * need to lock around their slot bodies.  Public read-only accessors
 * (@ref instances) acquire an internal mutex so they are safe to call
 * from any thread.
 *
 * @par Example
 * @code
 * MdnsManager mgr;
 * MdnsBrowser browser(MdnsServiceType("http", MdnsServiceType::Protocol::Tcp));
 * browser.setManager(&mgr);
 * connect(&browser.serviceFoundSignal, [](MdnsServiceInstance inst) {
 *     promekiInfo("found %s at %s:%u",
 *                 inst.instanceName().cstr(),
 *                 inst.hostname().cstr(),
 *                 inst.port());
 * });
 * mgr.start();
 * @endcode
 *
 * @par Thread Safety
 * Construction and @ref setManager are intended to happen on the
 * owning thread.  @ref handlePacket and the @ref instances accessor
 * are safe to call concurrently.
 */
class MdnsBrowser : public ObjectBase {
                PROMEKI_OBJECT(MdnsBrowser, ObjectBase)
        public:
                /**
                 * @brief Constructs a browser for the given service type.
                 *
                 * The browser is constructed in the @b inactive state;
                 * attach a @ref MdnsManager and (optionally) call
                 * @ref start.  Constructing a browser without a manager
                 * is legal — tests drive @ref handlePacket directly
                 * without ever wiring up a real socket.
                 */
                explicit MdnsBrowser(const MdnsServiceType &type, ObjectBase *parent = nullptr);

                /** @brief Destructor.  Detaches from the manager if attached. */
                ~MdnsBrowser() override;

                /**
                 * @brief Attaches the browser to (or detaches it from) a manager.
                 *
                 * Passing @c nullptr removes the browser from its
                 * current manager's fan-out list and re-engages the
                 * implicit fallback to @ref Application::mdnsManager.
                 * Re-pointing at a different manager unregisters
                 * first then registers.  No-op when the new pointer
                 * equals the current one.
                 */
                void setManager(MdnsManager *manager);

                /** @brief Returns the @b explicitly attached manager, or @c nullptr. */
                MdnsManager *manager() const { return _manager; }

                /**
                 * @brief Returns the manager the browser will dispatch through.
                 *
                 * Returns the explicit manager when @ref setManager
                 * was called with a non-null pointer; otherwise falls
                 * back to @ref Application::mdnsManager (the
                 * application-wide singleton, lazy-created on first
                 * call).  Returns @c nullptr when no fallback is
                 * available (the library was built with @c MDNS off).
                 */
                MdnsManager *effectiveManager() const;

                /** @brief Service type the browser is subscribed to. */
                const MdnsServiceType &serviceType() const { return _type; }

                /**
                 * @brief Returns @c true between successful @ref start and @ref stop.
                 *
                 * Independent of @ref handlePacket — packets are
                 * always processed whenever the browser is registered
                 * with a manager, regardless of @ref isActive.  The
                 * flag only governs the @b active side: whether
                 * @ref start has sent an initial PTR query (and, in
                 * follow-up steps, whether the continuous-query timer
                 * is wound).
                 */
                bool isActive() const { return _active.value(); }

                /**
                 * @brief Begins active discovery for the configured service type.
                 *
                 * Requires a manager attached via @ref setManager.
                 * Marks the browser active and sends one PTR query
                 * for the service-type FQDN on the manager's bound
                 * socket.  Subsequent @ref start calls re-fire the
                 * initial query — useful as a "kick the discovery"
                 * trigger when the network state changes.
                 *
                 * @return @ref Error::NotReady when no manager is
                 *         attached or the manager itself is not
                 *         active; a write-side error from
                 *         @ref MdnsManager::sendQuery otherwise.
                 */
                Error start();

                /**
                 * @brief Marks the browser inactive.
                 *
                 * Does not clear the cache — discovered instances stay
                 * addressable until they expire or a Goodbye arrives.
                 * Future steps cancel the continuous-query timer here.
                 */
                void stop();

                /**
                 * @brief Sends one PTR query for the configured service type.
                 *
                 * Available regardless of @ref isActive.  Useful for
                 * manual nudges (e.g. on user-driven "refresh"
                 * actions) without flipping the active flag.
                 */
                Error sendQuery();

                /**
                 * @brief Called by @ref MdnsManager at each housekeeping tick.
                 *
                 * Drives two periodic chores:
                 *  1. Cache eviction — defers to @ref evictExpiredAt(now).
                 *  2. Continuous-query backoff — when the browser is
                 *     @ref isActive and the next-query deadline has
                 *     elapsed, sends a fresh PTR query, doubles the
                 *     current interval up to @ref MaxQueryIntervalMs,
                 *     and reschedules the deadline accordingly.  This
                 *     mirrors the RFC 6762 §5.2 schedule.
                 *
                 * Public so the manager (and tests) can invoke it
                 * directly; called from the manager's worker thread.
                 */
                void onManagerTick(const TimeStamp &now);

                /** @brief Initial continuous-query interval in ms. */
                static constexpr int64_t InitialQueryIntervalMs = 1000;

                /** @brief Maximum continuous-query interval in ms (RFC 6762 §5.2). */
                static constexpr int64_t MaxQueryIntervalMs = 3600 * 1000;

                /**
                 * @brief RFC 6762 §10.2 cache-flush grace window in ms.
                 *
                 * A cache-flush A / AAAA record arriving within this
                 * many milliseconds of the previous flush on the same
                 * entry is treated as part of the @b same multi-record
                 * announce and appended rather than wiping the address
                 * list.  Outside the window the new record fully
                 * supersedes the prior list.  The spec mandates 1 s.
                 */
                static constexpr int64_t CacheFlushGraceMs = 1000;

                /**
                 * @brief Minimum spacing between directed follow-up queries
                 *        on the same record (ms).
                 *
                 * When the browser sees a PTR target without an SRV /
                 * TXT, or a SRV target without an A / AAAA, it fires a
                 * directed query to fill in the gap.  This constant
                 * caps how often the same directed query is re-sent so
                 * a flood of partial-info packets does not turn into a
                 * query storm.  Two seconds matches the rough cadence
                 * an Avahi browser uses for its targeted SRV / TXT
                 * re-asks; tests can pin a smaller value if the
                 * constant ever grows a setter.
                 */
                static constexpr int64_t DirectedQueryDebounceMs = 2000;

                /**
                 * @brief Returns the current continuous-query interval.
                 *
                 * Reflects @b post-tick state: after the first
                 * backoff fire-and-double in @ref onManagerTick the
                 * value will be @c 2 × @ref InitialQueryIntervalMs.
                 * Useful for tests and introspection; production
                 * code rarely needs it.
                 */
                Duration currentBackoffInterval() const;

                /**
                 * @brief Total number of times @ref onManagerTick has
                 *        decided to fire a query, regardless of
                 *        whether the send succeeded.
                 *
                 * Counts only tick-driven firings; explicit
                 * @ref sendQuery and @ref start invocations are not
                 * included.
                 */
                uint64_t queryFireCount() const { return _queryFireCount.value(); }

                /**
                 * @brief Evicts entries whose @c lastSeen + @c ttl is in the past.
                 *
                 * Walks every cached entry, drops the ones whose
                 * record TTL has elapsed against @ref TimeStamp::now,
                 * and fires @ref serviceLost for each evicted entry
                 * that was previously surfaced via @ref serviceFound.
                 * Returns the number of entries evicted.
                 */
                int evictExpired();

                /**
                 * @brief @ref evictExpired overload with an injected @c now.
                 *
                 * Exposed for tests that want to fast-forward time
                 * without sleeping.  Production code should use the
                 * zero-arg overload so the engine and the clock agree.
                 */
                int evictExpiredAt(const TimeStamp &now);

                /**
                 * @brief Snapshot of the instances currently known to the browser.
                 *
                 * Safe to call from any thread.  The returned list is
                 * a stable copy; subsequent packet processing may add,
                 * mutate, or remove entries without affecting the
                 * returned snapshot.
                 */
                List<MdnsServiceInstance> instances() const;

                /**
                 * @brief Per-datagram entry point called from the manager worker.
                 *
                 * Parses @p data through @ref MdnsPacket and applies
                 * every relevant @ref MdnsParsedRecord to the in-
                 * memory cache.  Unparseable packets are dropped
                 * silently.  Safe to call concurrently from multiple
                 * threads; internal state is mutex-protected.
                 */
                void handlePacket(const Buffer &data, const SocketAddress &sender,
                                  const NetworkInterface &iface);

                /** @brief Removes every cached instance without emitting signals. */
                void clearCache();

                /** @brief Emitted the first time an instance becomes addressable. @signal */
                PROMEKI_SIGNAL(serviceFound,   MdnsServiceInstance);
                /** @brief Emitted on every change to an already-found instance.  @signal */
                PROMEKI_SIGNAL(serviceUpdated, MdnsServiceInstance);
                /** @brief Emitted when an instance Goodbye is received.            @signal */
                PROMEKI_SIGNAL(serviceLost,    MdnsServiceInstance);

        private:
                // Internal per-instance bookkeeping.  Stored in a Map
                // keyed by the lower-cased instance FQDN so DNS's
                // case-insensitive match is honoured cheaply.
                struct Entry {
                        MdnsServiceInstance instance;
                        TimeStamp           lastSeen;        ///< Wall-clock of last record refresh.
                        Duration            ttl;             ///< TTL of the most recent record applied.
                        // Per-family cache-flush timestamps used to
                        // implement the RFC 6762 §10.2 "1-second
                        // grace" — a cache-flush A/AAAA arriving
                        // within @ref CacheFlushGraceMs of the prior
                        // one is treated as a continuation of the
                        // same multi-record announce and appended
                        // rather than wiping the list.
                        TimeStamp           lastV4FlushAt;
                        TimeStamp           lastV6FlushAt;
                        // Per-record-type "last directed query sent"
                        // timestamps for the follow-up query
                        // debounce (see @ref DirectedQueryDebounceMs).
                        // Index by @ref MdnsParsedRecord::Type cast
                        // to size_t via @ref directedQuerySlot.
                        TimeStamp           lastDirectedQuerySrvAt;
                        TimeStamp           lastDirectedQueryTxtAt;
                        TimeStamp           lastDirectedQueryAAt;
                        TimeStamp           lastDirectedQueryAaaaAt;
                        bool                foundEmitted = false;
                        bool                hasPtr       = false;
                };

                struct DirectedQuery {
                        String   name;
                        uint16_t recordType = 0;
                };

                // Helper: extract the instance label from a full
                // <Instance>._<app>._<proto>.<domain>. FQDN given the
                // browser's service-type FQDN.  Returns an empty
                // String when @p targetFqdn does not end with the
                // expected suffix.
                String extractInstanceLabel(const String &targetFqdn) const;

                // Returns a case-folded FQDN suitable for map keying.
                static String foldName(const String &s);

                // Applies one record to the cache.  May emit
                // serviceFound / serviceUpdated / serviceLost — the
                // signals are emitted with the mutex released.
                // Any directed follow-up queries needed to fill in
                // gaps (e.g. PTR → SRV/TXT, SRV → A/AAAA) are
                // appended to @p directedQueries; the caller is
                // responsible for issuing them after the mutex is
                // released.
                void processRecord(const MdnsParsedRecord &record,
                                   const NetworkInterface &iface,
                                   List<DirectedQuery>    &directedQueries);

                // Helper used by @ref processRecord.  Examines the
                // given @p entry against the "what we still need"
                // gap list and stages directed queries onto
                // @p directedQueries when the @ref DirectedQueryDebounceMs
                // gate allows.  Updates the entry's debounce
                // timestamps in place when a query is staged.
                void planDirectedFollowups(Entry &entry, const TimeStamp &now,
                                           List<DirectedQuery> &directedQueries);

                // Issues every staged directed query off-lock.  No-op
                // when @ref _manager is null or not active.
                void issueDirectedQueries(const List<DirectedQuery> &queries);

                // Builds the Known-Answer Suppression list for an
                // outbound continuous-query — one PTR record per
                // currently-cached, still-fresh entry.  Walks under
                // @ref _entriesMtx; safe to call from any thread.
                List<MdnsRecord> composeKnownAnswers() const;

                // Returns @c true when the instance has the minimal
                // identity needed to be exposed via @ref serviceFound
                // (port + hostname).  Address records may arrive
                // later; tests can decide separately how strict to be.
                static bool isAddressable(const MdnsServiceInstance &inst);

                MdnsServiceType   _type;
                MdnsManager      *_manager = nullptr;
                mutable Mutex     _entriesMtx;
                Map<String, Entry> _entries;
                Atomic<bool>      _active;
                Atomic<uint64_t>  _queryFireCount;

                // Backoff schedule — protected by the same mutex as
                // the entries map.  Reset to the initial interval on
                // every @ref start; doubled by @ref onManagerTick.
                TimeStamp         _nextQueryAt;
                Duration          _currentInterval;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_MDNS
