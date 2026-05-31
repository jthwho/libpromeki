/**
 * @file      dnsresolver.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_NETWORK
#include <cstdint>
#include <promeki/namespace.h>
#include <promeki/atomic.h>
#include <promeki/dnscache.h>
#include <promeki/dnsconfig.h>
#include <promeki/dnsrecord.h>
#include <promeki/duration.h>
#include <promeki/enums_dns.h>
#include <promeki/error.h>
#include <promeki/list.h>
#include <promeki/map.h>
#include <promeki/networkaddress.h>
#include <promeki/objectbase.h>
#include <promeki/result.h>
#include <promeki/socketaddress.h>
#include <promeki/string.h>
#include <promeki/uniqueptr.h>

PROMEKI_NAMESPACE_BEGIN

class DnsResolver;
class MdnsManager;
class TcpSocket;
class UdpSocket;
class TimerEvent;

/**
 * @brief One async DNS lookup in flight.
 * @ingroup network
 *
 * Created by @ref DnsResolver::lookup; lives until it completes
 * (emits @ref answered or @ref failed) or the caller @ref cancel's
 * it.  After completion the lookup auto-destructs via
 * @ref ObjectBase::deleteLater, so callers do not need to track
 * the pointer past signal connection.
 *
 * Signals fire on the resolver's @ref EventLoop and are marshalled
 * to slot owners' loops per the project's standard cross-thread
 * dispatch.  The same lookup may be observed by multiple slot
 * connections; both signals deliver exactly once.
 *
 * @par Example
 * @code
 * DnsResolver *r = DnsResolver::defaultInstance();
 * DnsLookup *lk = r->lookup("api.example.com", DnsRecordType::A);
 * lk->answeredSignal.connect([](List<DnsRecord> recs) {
 *     for (const DnsRecord &r : recs) ...;
 * }, this);
 * lk->failedSignal.connect([](Error e) {
 *     promekiWarn("DNS lookup failed: %s", e.name().cstr());
 * }, this);
 * @endcode
 *
 * @par Thread Safety
 * Thread-affine to the resolver's EventLoop.  @ref cancel and
 * accessors may be called from any thread; everything else stays
 * inside the resolver.
 */
class DnsLookup : public ObjectBase {
                PROMEKI_OBJECT(DnsLookup, ObjectBase)
        public:
                /**
                 * @brief Constructs an unstarted lookup.
                 *
                 * Most callers go through @ref DnsResolver::lookup
                 * instead of constructing the lookup directly.  When
                 * constructed via this path the lookup will not run
                 * until the owning resolver picks it up; do not call
                 * this constructor unless you have a reason to manage
                 * the start signal yourself.
                 *
                 * @param resolver Resolver that will drive this lookup.
                 * @param name     Owner name to query.
                 * @param type     RR type to ask for.
                 * @param klass    RR class to ask for (default @c In).
                 */
                DnsLookup(DnsResolver *resolver, const String &name,
                          const DnsRecordType &type,
                          const DnsRecordClass &klass = DnsRecordClass::In);

                ~DnsLookup() override;

                /** @brief Owner name as originally requested. */
                const String &name() const { return _name; }

                /** @brief Requested record type. */
                const DnsRecordType &type() const { return _type; }

                /** @brief Requested record class. */
                const DnsRecordClass &klass() const { return _klass; }

                /**
                 * @brief Last upstream response code, if any.
                 *
                 * Populated immediately before @ref answered or
                 * @ref failed fires.  Defaults to @ref DnsRcode::NoError
                 * before completion.
                 */
                const DnsRcode &rcode() const { return _rcode; }

                /**
                 * @brief Cancels an in-flight lookup.
                 *
                 * Idempotent.  If the lookup has not yet completed,
                 * fires @ref failed with @ref Error::Cancelled and
                 * schedules destruction via
                 * @ref ObjectBase::deleteLater.  If it has already
                 * completed the call is a no-op.  Safe to call from
                 * any thread.
                 */
                void cancel();

                /** @brief @c true between construction and completion. */
                bool isActive() const { return _active.value(); }

                /** @brief Emitted exactly once with the answer RRset.  @signal */
                PROMEKI_SIGNAL(answered, List<DnsRecord>);

                /** @brief Emitted exactly once with the failure reason.  @signal */
                PROMEKI_SIGNAL(failed,   Error);

        protected:
                /** @brief Single-shot per-attempt timeout driver. */
                void timerEvent(TimerEvent *e) override;

        private:
                friend class DnsResolver;

                DnsResolver           *_resolver = nullptr;
                String                 _name;
                DnsRecordType          _type     = DnsRecordType::A;
                DnsRecordClass         _klass    = DnsRecordClass::In;
                DnsRcode               _rcode    = DnsRcode::NoError;
                Atomic<bool>           _active;
                Atomic<bool>           _emitted;
};

/**
 * @brief Library-native DNS stub resolver.
 * @ingroup network
 *
 * Issues unicast DNS queries (RFC 1035) against the configured
 * recursive name servers.  Speaks the wire protocol directly through
 * the project's @ref UdpSocket / @ref TcpSocket — never calls into
 * the host's @c getaddrinfo / @c gethostbyname / @c res_query.  Used
 * by @ref NetworkAddress::resolve and by @ref HttpClient /
 * @ref WebSocket / @ref RtmpClient for hostname resolution.
 *
 * Server discovery uses the host's stub-resolver configuration:
 *  - @b POSIX: parses @c /etc/resolv.conf (@c nameserver,
 *    @c search, @c domain, @c options ndots:/timeout:/attempts:).
 *  - @b Windows: walks per-adapter DNS lists via
 *    @c GetAdaptersAddresses().
 *  - @b Caller override: @ref setNameservers /
 *    @ref setSearchDomains bypass discovery entirely.
 *  - @b Fallback: @c 127.0.0.53 (systemd-resolved stub) when no
 *    configuration is found.
 *
 * @par Features
 *  - Async lookup via @ref lookup, plus sync convenience wrappers
 *    (@ref lookupSync / @ref resolveSync) for legacy
 *    @c getaddrinfo-replacement call sites.
 *  - Multi-server failover on UDP timeout (cycles through
 *    @c nameservers per @c attempts).
 *  - UDP-to-TCP fallback when the upstream response has the @c TC
 *    (truncated) bit set (RFC 1035 §4.2.1).
 *  - EDNS0 with a 1232-byte UDP payload size (DNS Flag Day 2020).
 *  - CNAME chase up to @ref MaxCnameDepth hops.
 *  - Search-list expansion governed by RFC 1535 / 1536 / 6761
 *    semantics: a name with @c ndots-or-more dots is queried
 *    bare-first, then with appended search domains; an unqualified
 *    name is suffixed first.
 *  - TTL-honoring positive + negative cache (@ref DnsCache),
 *    capped per @ref DnsCache::setMaxTtl.
 *
 * @par Out of scope
 *  - DNSSEC validation — the resolver passes the DO bit through
 *    and surfaces RRSIG / DNSKEY records opaquely, but does no
 *    chain validation.
 *  - DoT / DoH — plain UDP + TCP only.
 *  - Recursive resolution from the root — this is a stub.
 *
 * @par Thread Safety
 * The resolver is thread-affine to its @ref EventLoop.  Public
 * mutators (@ref setNameservers, etc.) and the sync wrappers may
 * be called from any thread; the cache is internally synchronised.
 * Cross-thread @ref lookup posts the actual work to the
 * resolver's loop.
 */
class DnsResolver : public ObjectBase {
                PROMEKI_OBJECT(DnsResolver, ObjectBase)
        public:
                /** @brief CNAME chase depth before giving up (RFC 1034 §3.6.2 — no firm cap, 16 is a common safety net). */
                static constexpr int MaxCnameDepth = 16;

                /** @brief EDNS0 UDP payload size sent in queries (DNS Flag Day 2020). */
                static constexpr uint16_t Edns0UdpSize = 1232;

                /** @brief Default per-attempt UDP timeout in ms when the config does not override it. */
                static constexpr int64_t DefaultAttemptTimeoutMs = 3000;

                /**
                 * @brief Returns the process-wide default resolver.
                 *
                 * Constructed lazily on the main thread's EventLoop
                 * (per @ref Application::mainEventLoop).  When the
                 * main loop does not yet exist (i.e. no
                 * @ref Application has been constructed) the call
                 * returns @c nullptr; sync wrappers handle this by
                 * spinning a private resolver on a temporary
                 * EventLoop.
                 */
                static DnsResolver *defaultInstance();

                /**
                 * @brief Constructs a resolver on the current thread's EventLoop.
                 *
                 * Each instance owns its own @ref DnsCache,
                 * @ref DnsConfig, and UDP socket(s).  Most callers
                 * should use @ref defaultInstance unless they need
                 * to point the resolver at a specific
                 * configuration (test rigs, private-resolver
                 * scenarios).
                 *
                 * @param parent Optional parent ObjectBase.
                 */
                explicit DnsResolver(ObjectBase *parent = nullptr);

                ~DnsResolver() override;

                /**
                 * @brief Replaces the nameserver list.
                 *
                 * Bypasses @c /etc/resolv.conf for this resolver.
                 * Existing in-flight queries keep using whichever
                 * server they already targeted; new lookups walk
                 * the new list.
                 */
                void setNameservers(const List<SocketAddress> &servers);

                /** @brief Returns the current nameserver list. */
                List<SocketAddress> nameservers() const;

                /** @brief Replaces the search-domain list. */
                void setSearchDomains(const StringList &domains);

                /** @brief Returns the current search-domain list. */
                StringList searchDomains() const;

                /** @brief Sets the entire configuration block (servers + search + timeouts). */
                void setConfig(const DnsConfig &cfg);

                /** @brief Returns the current configuration. */
                DnsConfig config() const;

                /**
                 * @brief Re-reads the system stub-resolver configuration.
                 *
                 * Useful when DHCP pushes a new resolver list mid-run.
                 * Existing in-flight queries are not interrupted.
                 */
                Error reloadSystemConfig();

                /** @brief Returns the shared TTL cache. */
                DnsCache &cache() { return _cache; }
                /** @copydoc cache() */
                const DnsCache &cache() const { return _cache; }

                /**
                 * @brief Sets the @ref MdnsManager used for
                 *        @c .local. hostname resolution.
                 *
                 * When unset (the default) and
                 * @c PROMEKI_ENABLE_MDNS is on, the resolver lazily
                 * pulls @ref Application::mdnsManager.  When set,
                 * the resolver uses the explicit manager — useful
                 * for tests that want a private manager on a fixed
                 * interface set and for production code that
                 * configures the manager up-front.
                 *
                 * Names that do @b not end in @c ".local." continue
                 * to go through unicast DNS regardless.  When mDNS
                 * is disabled at build time this setter exists but
                 * is a no-op.
                 */
                void setMdnsManager(MdnsManager *manager);

                /** @brief Returns the explicit @ref MdnsManager, or @c nullptr. */
                MdnsManager *mdnsManager() const;

                /**
                 * @brief Enables or disables @c .local. routing through @ref MdnsManager.
                 *
                 * Defaults to @c true.  When disabled, names with a
                 * @c .local. suffix are sent through the unicast
                 * path like any other name — which typically
                 * @ref Error::HostNotFound's because configured
                 * upstream resolvers don't carry @c .local. zones.
                 * Useful for:
                 *  - Tests that want predictable NXDOMAIN behaviour
                 *    on @c .local. without spinning up an mDNS
                 *    engine.
                 *  - Apps that explicitly don't want mDNS side
                 *    effects (multicast group joins, the lazy-start
                 *    of @ref Application::mdnsManager) triggered
                 *    by a hostname lookup.
                 *
                 * No-op at runtime when @c PROMEKI_ENABLE_MDNS is
                 * off at build time — routing is already disabled
                 * in that build.
                 */
                void setMdnsRoutingEnabled(bool enable);

                /** @brief Returns @c true when @c .local. routing is enabled. */
                bool mdnsRoutingEnabled() const;

                /**
                 * @brief Issues an async lookup.
                 *
                 * The returned @ref DnsLookup is parented to the
                 * resolver and auto-destructs after emitting
                 * @ref DnsLookup::answered or
                 * @ref DnsLookup::failed.  Connect the signals
                 * immediately on the returned pointer — the lookup
                 * may not start until the resolver's EventLoop runs,
                 * but it is safe to connect from the calling thread.
                 *
                 * @param name  Owner name to query (text form).
                 * @param type  RR type to ask for.
                 * @param klass RR class (default @c In).
                 * @return A new @ref DnsLookup.
                 */
                DnsLookup *lookup(const String &name,
                                  const DnsRecordType &type = DnsRecordType::A,
                                  const DnsRecordClass &klass = DnsRecordClass::In);

                /**
                 * @brief Synchronous one-shot lookup.
                 *
                 * Blocks the calling thread until the lookup
                 * completes or @p timeout elapses.  Spins a
                 * private EventLoop on the calling thread so the
                 * call may be issued from any thread including
                 * one that already has its own loop running
                 * elsewhere (the private loop does not interfere
                 * with that).
                 *
                 * Suitable as a drop-in replacement for blocking
                 * @c getaddrinfo calls in legacy code; new code
                 * should prefer @ref lookup + signals.
                 *
                 * @param name   Owner name (text form).
                 * @param type   RR type to ask for.
                 * @param timeout Overall deadline.  Default 5 s.
                 * @return The answer RRset on success;
                 *         @ref Error::HostNotFound for
                 *         @c NxDomain or no-records-of-this-type
                 *         responses; @ref Error::Timeout if the
                 *         deadline expires; other errors for
                 *         transport / configuration problems.
                 */
                static Result<List<DnsRecord>> lookupSync(
                        const String &name,
                        const DnsRecordType &type = DnsRecordType::A,
                        const Duration &timeout = Duration::fromSeconds(5));

                /**
                 * @brief Synchronous host-address lookup.
                 *
                 * Equivalent to @ref NetworkAddress::resolve but
                 * goes through the library's own DNS path rather
                 * than calling @c getaddrinfo.  When @p prefer is
                 * @ref NetworkAddress::PreferIPv4 the resolver
                 * issues an @c A query and falls back to @c AAAA
                 * on @c NxDomain; @c PreferIPv6 reverses the order;
                 * @c AnyFamily issues @c A first.
                 *
                 * @param host    Hostname or IP literal to resolve.
                 *                IP literals short-circuit the DNS
                 *                path and round-trip through
                 *                @ref NetworkAddress::fromString.
                 * @param prefer  Address-family preference.
                 * @param timeout Overall deadline.  Default 5 s.
                 * @return The resolved address on success, an
                 *         @ref Error on failure.
                 */
                static Result<NetworkAddress> resolveSync(
                        const String &host,
                        NetworkAddress::FamilyPreference prefer = NetworkAddress::PreferIPv4,
                        const Duration &timeout = Duration::fromSeconds(5));

                /**
                 * @brief Reverse PTR lookup helper.
                 *
                 * Constructs the @c <reversed>.in-addr.arpa /
                 * @c ip6.arpa name and issues a @c PTR query.
                 * Returns @ref Error::Invalid when @p addr is not
                 * a resolved IPv4 / IPv6 address.
                 *
                 * @param addr     The address to reverse-resolve.
                 * @param timeout  Overall deadline.  Default 5 s.
                 * @return The list of @c PTR records, or an error.
                 */
                static Result<List<DnsRecord>> lookupReverseSync(
                        const NetworkAddress &addr,
                        const Duration &timeout = Duration::fromSeconds(5));

        private:
                friend class DnsLookup;

                struct Inflight;

                Error openSocketsIfNeeded();
                void  closeSockets();
                void  beginLookup(DnsLookup *lookup);
                void  beginAttempt(Inflight *flight);
                bool  isMdnsName(const String &name) const;
                Error beginMdnsLookup(Inflight *flight);
                void  handleMdnsPacket(Inflight *flight, const Buffer &data);
                MdnsManager *effectiveMdnsManager();
                void  onLookupTimeout(DnsLookup *lookup);
                void  failLookup(DnsLookup *lookup, Error err);
                void  succeedLookup(DnsLookup *lookup, const List<DnsRecord> &records);
                void  handleSocketReadable(UdpSocket *sock);
                void  processResponseBytes(const uint8_t *data, size_t len);
                void  followCname(Inflight *flight, const String &target);
                void  beginTcpFallback(Inflight *flight);
                void  removeInflight(DnsLookup *lookup);
                uint16_t allocateTransactionId();
                List<String> expandSearch(const String &name) const;
                Error  sendQueryUdp(Inflight *flight);
                static List<DnsRecord> filterAnswerRecords(
                        const class DnsPacket &pkt,
                        const String &name, uint16_t type, uint16_t klass);
                static Duration soaMinimumFromAuthority(const class DnsPacket &pkt);

                mutable Mutex                _cfgMtx;
                DnsConfig                    _config;
                DnsCache                     _cache;
                UniquePtr<UdpSocket>         _socketV4;
                UniquePtr<UdpSocket>         _socketV6;
                int                          _ioHandleV4 = -1;
                int                          _ioHandleV6 = -1;

                // mDNS routing — populated lazily via
                // @ref effectiveMdnsManager.  @c _mdnsManager is the
                // explicit override; when null we fall back to
                // @ref Application::mdnsManager.  The observer handle
                // is registered with the manager on first .local.
                // lookup and unregistered when the resolver dies.
                MdnsManager                 *_mdnsManager        = nullptr;
                int                          _mdnsObserverHandle = -1;
                bool                         _mdnsRoutingEnabled = true;

                // In-flight bookkeeping: keyed by transaction id.
                // The Inflight owns the timer; lookup ownership stays
                // with the parent (this resolver).  An Inflight is
                // created when @ref beginLookup posts to our loop and
                // destroyed when the lookup completes or is
                // cancelled.
                Map<uint16_t, Inflight *>    _inflight;
                Map<DnsLookup *, Inflight *> _byLookup;
                uint16_t                     _nextTxId = 0;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NETWORK
