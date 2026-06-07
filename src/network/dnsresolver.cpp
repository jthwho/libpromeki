/**
 * @file      dnsresolver.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/dnsresolver.h>

#include <promeki/application.h>
#include <promeki/dnsname.h>
#include <promeki/dnspacket.h>
#include <promeki/eventloop.h>
#include <promeki/logger.h>
#if PROMEKI_ENABLE_MDNS
#include <promeki/mdnsmanager.h>
#endif
#include <promeki/objectbase.tpp>
#include <promeki/random.h>
#include <promeki/tcpsocket.h>
#include <promeki/thread.h>
#include <promeki/udpsocket.h>
#include <promeki/waitcondition.h>

#include <cstdlib>

PROMEKI_NAMESPACE_BEGIN

// ============================================================================
// Internal in-flight state per outstanding query.
// ============================================================================
struct DnsResolver::Inflight {
        ObjectBasePtr<DnsLookup> lookup;     ///< Tracker — auto-nulls when the user destroys the lookup.
        String        questionName;          ///< Currently-asked owner name (CNAME chase rewrites this).
        uint16_t      type      = 0;
        uint16_t      klass     = 1;
        uint16_t      txid      = 0;         ///< Transaction id used on the wire.
        size_t        serverIdx = 0;         ///< Index into the resolver's nameserver list.
        int           attempts  = 0;         ///< UDP attempts already made (across servers).
        int           maxAttempts = 1;       ///< Total UDP attempts the config allows.
        int           cnameDepth = 0;        ///< Number of CNAME hops chased so far.
        bool          triedTcp   = false;    ///< Already promoted to TCP after a TC bit.
        StringList    searchPaths;           ///< Candidate FQDNs to try in order.
        size_t        searchIdx  = 0;        ///< Position in @c searchPaths.
        TimeStamp     startTime;             ///< For diagnostic logging.
        int           timerId    = -1;       ///< Per-attempt timeout timer registered on the lookup.
        UniquePtr<TcpSocket> tcp;            ///< Active TCP fallback socket (per attempt).
        List<uint8_t> tcpReadBuf;            ///< Accumulator for length-prefixed TCP response.
        int           tcpIoHandle = -1;      ///< EventLoop I/O handle for the TCP socket.
        bool          isMdns      = false;   ///< @c true when the flight is routed through @ref MdnsManager.
        int           mdnsObserver = -1;     ///< Per-flight @ref MdnsManager packet-observer handle.
};

// ============================================================================
// DnsLookup
// ============================================================================
DnsLookup::DnsLookup(DnsResolver *resolver, const String &name,
                     const DnsRecordType &type, const DnsRecordClass &klass)
    : ObjectBase(resolver), _resolver(resolver), _name(name), _type(type), _klass(klass) {
        _active.setValue(false);
        _emitted.setValue(false);
}

DnsLookup::~DnsLookup() {
        if (_resolver != nullptr) _resolver->removeInflight(this);
}

void DnsLookup::cancel() {
        if (!_active.value()) return;
        if (_resolver == nullptr) return;
        // Hop to the resolver's loop if necessary so the in-flight
        // map mutation stays single-threaded.
        EventLoop *loop = _resolver->eventLoop();
        if (loop != nullptr && loop != EventLoop::current()) {
                ObjectBasePtr<DnsLookup> selfPtr(this);
                loop->postCallable([selfPtr]() mutable {
                        DnsLookup *self = selfPtr.data();
                        if (self == nullptr) return;
                        if (!self->_active.value()) return;
                        self->_resolver->failLookup(self, Error(Error::Cancelled));
                });
                return;
        }
        _resolver->failLookup(this, Error(Error::Cancelled));
}

void DnsLookup::timerEvent(TimerEvent * /*e*/) {
        if (_resolver != nullptr) _resolver->onLookupTimeout(this);
}

// ============================================================================
// DnsResolver — lifecycle
// ============================================================================
namespace {
        DnsResolver *g_defaultResolver = nullptr;
        Mutex      &defaultResolverMutex() {
                static Mutex m;
                return m;
        }
} // anonymous namespace

DnsResolver *DnsResolver::defaultInstance() {
        Mutex::Locker lock(defaultResolverMutex());
        if (g_defaultResolver != nullptr) return g_defaultResolver;
        EventLoop *mainLoop = Application::mainEventLoop();
        if (mainLoop == nullptr) return nullptr;
        // Construct on the main loop so the resolver's affinity is
        // the main thread.  Post a callable to do the construction
        // there if we are not already on the main thread.
        if (EventLoop::current() == mainLoop) {
                g_defaultResolver = new DnsResolver(nullptr);
        } else {
                Mutex      doneMtx;
                Atomic<bool> done;
                done.setValue(false);
                mainLoop->postCallable([&]() {
                        g_defaultResolver = new DnsResolver(nullptr);
                        done.setValue(true);
                });
                // Busy-wait briefly for construction to complete.
                // Main-loop posting is cheap; the construction
                // itself is O(file-read) at most.
                while (!done.value()) {
                        EventLoop *cur = EventLoop::current();
                        if (cur != nullptr) cur->processEvents(0, 5);
                        else                std::this_thread::yield();
                }
        }
        return g_defaultResolver;
}

DnsResolver::DnsResolver(ObjectBase *parent) : ObjectBase(parent) {
        auto cfg = DnsConfig::loadSystem();
        if (cfg.second().isOk()) _config = cfg.first();
}

DnsResolver::~DnsResolver() {
        // Cancel every still-running lookup; the lookup destructor
        // calls back into removeInflight which mutates _byLookup so
        // we walk a snapshot.
        List<DnsLookup *> active;
        for (auto &item : _byLookup) active += item.first;
        for (DnsLookup *lk : active) {
                if (lk != nullptr) failLookup(lk, Error(Error::Stopped));
        }
        closeSockets();

        // Detach every surviving child lookup.  failLookup() only
        // deleteLater()s the inflight ones (the event loop won't run
        // again to honour that during teardown), and lookups created by
        // lookup() but not yet begun were never in _byLookup at all.  All
        // of them are ObjectBase children, so ~ObjectBase will destroy
        // them after this derived destructor returns — by which point the
        // DnsResolver subobject is gone.  Null their back-pointer now so
        // ~DnsLookup doesn't call removeInflight() on a dead resolver.
        for (ObjectBase *child : childList()) {
                if (DnsLookup *lk = dynamic_cast<DnsLookup *>(child)) {
                        lk->_resolver = nullptr;
                }
        }
}

void DnsResolver::setNameservers(const List<SocketAddress> &servers) {
        Mutex::Locker lock(_cfgMtx);
        _config.setNameservers(servers);
}

List<SocketAddress> DnsResolver::nameservers() const {
        Mutex::Locker lock(_cfgMtx);
        return _config.nameservers();
}

void DnsResolver::setSearchDomains(const StringList &domains) {
        Mutex::Locker lock(_cfgMtx);
        _config.setSearchDomains(domains);
}

StringList DnsResolver::searchDomains() const {
        Mutex::Locker lock(_cfgMtx);
        return _config.searchDomains();
}

void DnsResolver::setConfig(const DnsConfig &cfg) {
        Mutex::Locker lock(_cfgMtx);
        _config = cfg;
}

DnsConfig DnsResolver::config() const {
        Mutex::Locker lock(_cfgMtx);
        return _config;
}

Error DnsResolver::reloadSystemConfig() {
        auto r = DnsConfig::loadSystem();
        if (r.second().isError()) return r.second();
        setConfig(r.first());
        return Error();
}

void DnsResolver::setMdnsManager(MdnsManager *manager) {
#if PROMEKI_ENABLE_MDNS
        Mutex::Locker lock(_cfgMtx);
        _mdnsManager = manager;
#else
        (void)manager;
#endif
}

MdnsManager *DnsResolver::mdnsManager() const {
#if PROMEKI_ENABLE_MDNS
        Mutex::Locker lock(_cfgMtx);
        return _mdnsManager;
#else
        return nullptr;
#endif
}

void DnsResolver::setMdnsRoutingEnabled(bool enable) {
        Mutex::Locker lock(_cfgMtx);
        _mdnsRoutingEnabled = enable;
}

bool DnsResolver::mdnsRoutingEnabled() const {
        Mutex::Locker lock(_cfgMtx);
        return _mdnsRoutingEnabled;
}

MdnsManager *DnsResolver::effectiveMdnsManager() {
#if PROMEKI_ENABLE_MDNS
        {
                Mutex::Locker lock(_cfgMtx);
                if (_mdnsManager != nullptr) return _mdnsManager;
        }
        // Fall back to the application-wide singleton.  This
        // accessor lazily starts the manager on the main loop;
        // we deliberately don't cache it on @c _mdnsManager so a
        // subsequent @ref Application::stopMdnsManager does not
        // leave us holding a dangling pointer.
        return Application::mdnsManager();
#else
        return nullptr;
#endif
}

bool DnsResolver::isMdnsName(const String &name) const {
        // Case-insensitive ".local." or ".local" suffix check.
        // Names returned by the search-list expansion always carry
        // the trailing dot; user-supplied names may or may not.
        String canonical = dnsCanonicalName(name);
        const String suffix(".local.");
        if (canonical.size() < suffix.size()) return false;
        return canonical.endsWith(suffix);
}

// ============================================================================
// Socket management
// ============================================================================
Error DnsResolver::openSocketsIfNeeded() {
        // We open both families lazily but on first use bring up at
        // least the v4 socket — almost every host has at least one
        // v4 nameserver in /etc/resolv.conf (loopback systemd-resolved
        // stub, DHCP-pushed gateway, etc.).  The v6 socket comes up
        // only if we discover an IPv6 nameserver in the config so we
        // do not waste an fd on v4-only hosts.
        DnsConfig cfg = config();
        bool      needV4 = false;
        bool      needV6 = false;
        for (const SocketAddress &srv : cfg.nameservers()) {
                if (srv.address().isIPv6()) needV6 = true;
                else                        needV4 = true;
        }
        // If the config carries no servers at all, still come up on
        // v4 so a later setNameservers() call has a socket ready.
        if (!needV4 && !needV6) needV4 = true;

        EventLoop *loop = eventLoop();

        if (needV4 && !_socketV4) {
                _socketV4.reset(new UdpSocket(this));
                Error e = _socketV4->open(IODevice::ReadWrite);
                if (e.isError()) {
                        _socketV4.reset();
                        return e;
                }
                // Connect-less UDP — we use writeDatagram per query.
                // Bind to the wildcard so the kernel picks an
                // ephemeral source port.
                e = _socketV4->bind(SocketAddress::any(0));
                if (e.isError()) {
                        _socketV4.reset();
                        return e;
                }
                // Non-blocking is mandatory: @ref handleSocketReadable
                // drains every pending datagram in a tight loop and
                // depends on the trailing @c readDatagram returning
                // @c -1 / @c EAGAIN once the buffer is empty.  The
                // default 5-second SO_RCVTIMEO that
                // @ref AbstractSocket::createSocket installs would
                // otherwise stall the loop for 5 s after every
                // response.
                _socketV4->setNonBlocking(true);
                if (loop != nullptr) {
                        UdpSocket *raw = _socketV4.get();
                        _ioHandleV4 = loop->addIoSource(
                            raw->socketDescriptor(), EventLoop::IoRead,
                            [this, raw](int, uint32_t events) {
                                    if ((events & EventLoop::IoRead) != 0) {
                                            handleSocketReadable(raw);
                                    }
                            });
                }
        }

        if (needV6 && !_socketV6) {
                _socketV6.reset(new UdpSocket(this));
                Error e = _socketV6->openIpv6(IODevice::ReadWrite);
                if (e.isError()) {
                        // Don't fail the whole resolver if v6 socket
                        // creation fails (no IPv6 stack, etc.); just
                        // skip the v6 side and let the v4 path handle
                        // what it can.
                        _socketV6.reset();
                        promekiWarn("DnsResolver: IPv6 socket open failed: %s — "
                                    "IPv6 nameservers will be skipped", e.name().cstr());
                } else {
                        e = _socketV6->bind(SocketAddress(Ipv6Address::any(), 0));
                        if (e.isError()) {
                                _socketV6.reset();
                                promekiWarn("DnsResolver: IPv6 socket bind failed: %s",
                                            e.name().cstr());
                        } else {
                                // Same non-blocking requirement as
                                // the v4 path — see the comment in
                                // the v4 branch above.
                                _socketV6->setNonBlocking(true);
                                if (loop != nullptr) {
                                        UdpSocket *raw = _socketV6.get();
                                        _ioHandleV6 = loop->addIoSource(
                                            raw->socketDescriptor(), EventLoop::IoRead,
                                            [this, raw](int, uint32_t events) {
                                                    if ((events & EventLoop::IoRead) != 0) {
                                                            handleSocketReadable(raw);
                                                    }
                                            });
                                }
                        }
                }
        }

        return Error();
}

void DnsResolver::closeSockets() {
        EventLoop *loop = eventLoop();
        if (_ioHandleV4 >= 0 && loop != nullptr) {
                loop->removeIoSource(_ioHandleV4);
                _ioHandleV4 = -1;
        }
        if (_ioHandleV6 >= 0 && loop != nullptr) {
                loop->removeIoSource(_ioHandleV6);
                _ioHandleV6 = -1;
        }
        if (_socketV4) _socketV4->close();
        if (_socketV6) _socketV6->close();
        _socketV4.reset();
        _socketV6.reset();
}

// ============================================================================
// Lookup API
// ============================================================================
DnsLookup *DnsResolver::lookup(const String &name, const DnsRecordType &type,
                               const DnsRecordClass &klass) {
        DnsLookup *lookup = new DnsLookup(this, name, type, klass);
        lookup->_active.setValue(true);

        // Always defer the actual work via postCallable so the
        // caller has time to connect signal handlers on the
        // returned @ref DnsLookup before any completion (cache
        // hit, IP-literal short-circuit, ...) fires.  A purely
        // synchronous beginLookup would race the user's
        // @c answeredSignal.connect call and silently drop the
        // first delivery.
        EventLoop *loop = eventLoop();
        if (loop != nullptr) {
                ObjectBasePtr<DnsLookup> selfPtr(lookup);
                loop->postCallable([this, selfPtr]() mutable {
                        DnsLookup *self = selfPtr.data();
                        if (self == nullptr) return;
                        beginLookup(self);
                });
        } else {
                beginLookup(lookup);
        }
        return lookup;
}

void DnsResolver::beginLookup(DnsLookup *lookup) {
        if (lookup == nullptr) return;
        DnsConfig cfg = config();
        if (cfg.nameservers().isEmpty()) {
                failLookup(lookup, Error(Error::NotReady));
                return;
        }
        Error e = openSocketsIfNeeded();
        if (e.isError()) {
                failLookup(lookup, e);
                return;
        }

        // Cache check on the literal requested name (no search-list
        // expansion for the cache key — a different search-list at
        // call time would mean a different effective FQDN).
        DnsCache::Hit hit = _cache.get(lookup->name(),
                                       static_cast<uint16_t>(lookup->type().value()),
                                       static_cast<uint16_t>(lookup->klass().value()));
        if (hit.found) {
                lookup->_rcode = hit.rcode;
                if (hit.negative) {
                        failLookup(lookup, Error(Error::HostNotFound));
                } else {
                        succeedLookup(lookup, hit.records);
                }
                return;
        }

        Inflight *flight   = new Inflight();
        flight->lookup     = ObjectBasePtr<DnsLookup>(lookup);
        flight->type       = static_cast<uint16_t>(lookup->type().value());
        flight->klass      = static_cast<uint16_t>(lookup->klass().value());
        // mDNS names (.local. suffix) bypass the unicast search
        // list per RFC 6762 §3 — there's no point appending corporate
        // search domains to "printer.local".  Single-candidate path.
        // When mDNS routing is disabled we fall through to the
        // unicast search-list expansion; .local will then almost
        // certainly NXDOMAIN against upstream servers.
        if (mdnsRoutingEnabled() && isMdnsName(lookup->name())) {
                String n = lookup->name();
                if (!n.endsWith('.')) n += '.';
                flight->searchPaths += n;
        } else {
                flight->searchPaths = expandSearch(lookup->name());
        }
        flight->searchIdx  = 0;
        flight->maxAttempts = cfg.attempts() * static_cast<int>(cfg.nameservers().size());
        flight->startTime  = TimeStamp::now();
        if (flight->searchPaths.isEmpty()) {
                delete flight;
                failLookup(lookup, Error(Error::Invalid));
                return;
        }
        flight->questionName = flight->searchPaths[0];

        _byLookup[lookup] = flight;
        beginAttempt(flight);
}

void DnsResolver::beginAttempt(Inflight *flight) {
        if (flight == nullptr) return;
        DnsLookup *lookup = flight->lookup.data();
        if (lookup == nullptr) {
                // Lookup gone — clean up.
                _byLookup.remove(static_cast<DnsLookup *>(nullptr));
                delete flight;
                return;
        }

        // mDNS-bound names route to the multicast path instead of
        // unicast nameservers.  Only A / AAAA / PTR / SRV / TXT are
        // meaningful on .local. — other RR types fall through and
        // will NXDOMAIN against the multicast responders.  When
        // mDNS is disabled at build time, or the caller has opted
        // out via @ref setMdnsRoutingEnabled, we let the unicast
        // path take them and surface whatever the upstream resolver
        // returns (typically NXDOMAIN).
#if PROMEKI_ENABLE_MDNS
        if (mdnsRoutingEnabled() && isMdnsName(flight->questionName)) {
                Error me = beginMdnsLookup(flight);
                if (me.isError()) {
                        failLookup(lookup, me);
                }
                return;
        }
#endif

        // Allocate (or rotate) transaction id.  Reuse the same id
        // across UDP retries on the same question so a stale late
        // response can still be matched if it eventually arrives.
        if (flight->txid == 0 || flight->attempts > 0) {
                // Pull old id out of the inflight map.
                if (flight->txid != 0) {
                        auto it = _inflight.find(flight->txid);
                        if (it != _inflight.end() && it->second == flight) {
                                _inflight.remove(it);
                        }
                }
                flight->txid = allocateTransactionId();
                _inflight[flight->txid] = flight;
        }

        Error e = sendQueryUdp(flight);
        if (e.isError()) {
                // No usable server / send failed — try next server or
                // bail out.
                ++flight->attempts;
                if (flight->attempts >= flight->maxAttempts) {
                        failLookup(lookup, Error(Error::HostNotFound));
                        return;
                }
                flight->serverIdx = (flight->serverIdx + 1) % nameservers().size();
                beginAttempt(flight);
                return;
        }

        DnsConfig          cfg     = config();
        const unsigned int timeoutMs = static_cast<unsigned int>(
            cfg.timeout().milliseconds() > 0 ? cfg.timeout().milliseconds() : DefaultAttemptTimeoutMs);
        // Stop any prior timer for this attempt.
        if (flight->timerId >= 0) {
                lookup->stopTimer(flight->timerId);
                flight->timerId = -1;
        }
        flight->timerId = lookup->startTimer(timeoutMs, /*singleShot=*/true);
}

uint16_t DnsResolver::allocateTransactionId() {
        // Random 16-bit id, avoiding zero (some servers / middleware
        // mishandle it).
        while (true) {
                uint16_t id =
                        static_cast<uint16_t>(Random::global().randomInt(1, 0xFFFF));
                if (_inflight.find(id) == _inflight.end()) return id;
        }
}

Error DnsResolver::sendQueryUdp(Inflight *flight) {
        DnsConfig cfg = config();
        if (cfg.nameservers().isEmpty()) return Error(Error::NotReady);

        DnsPacket::Builder b;
        b.setTransactionId(flight->txid);
        b.setRecursionDesired(true);
        b.addQuestion(flight->questionName,
                      static_cast<DnsRecord::Type>(flight->type),
                      flight->klass);
        b.addEdns0(Edns0UdpSize, /*doBit=*/false);
        Buffer pkt = b.finish();
        if (pkt.size() == 0) return Error(Error::Invalid);

        // Pick current server (wrap on overrun — defensive) and
        // route through the family-matching socket.  An IPv6
        // nameserver on a host whose v6 socket failed to open
        // surfaces as @ref Error::NotOpen so the caller failover
        // path moves on to the next server.
        const SocketAddress &srv =
            cfg.nameservers()[flight->serverIdx % cfg.nameservers().size()];
        UdpSocket *sock = nullptr;
        if (srv.address().isIPv6()) {
                sock = _socketV6.get();
        } else {
                sock = _socketV4.get();
        }
        if (sock == nullptr) return Error(Error::NotOpen);
        int64_t n = sock->writeDatagram(pkt, srv);
        if (n < 0) return Error::syserr();
        return Error();
}

// ============================================================================
// Inbound packet handling
// ============================================================================
void DnsResolver::handleSocketReadable(UdpSocket *sock) {
        if (sock == nullptr) return;
        // Drain everything pending.
        uint8_t       buf[2048];
        SocketAddress sender;
        while (true) {
                int64_t n = sock->readDatagram(buf, sizeof(buf), &sender);
                if (n <= 0) break;
                processResponseBytes(buf, static_cast<size_t>(n));
        }
}

void DnsResolver::processResponseBytes(const uint8_t *data, size_t len) {
        auto pr = DnsPacket::parse(data, len);
        if (pr.second().isError()) return;
        const DnsPacket &pkt = pr.first();

        // Match the transaction id.
        auto it = _inflight.find(pkt.transactionId());
        if (it == _inflight.end()) return;
        Inflight  *flight = it->second;
        DnsLookup *lookup = (flight != nullptr) ? flight->lookup.data() : nullptr;
        if (flight == nullptr || lookup == nullptr) {
                // Lookup vanished — drop and clean up.
                _inflight.remove(it);
                delete flight;
                return;
        }

        // RFC 5452 §9: verify the response's Question section
        // echoes our outstanding question (name + type + class)
        // before accepting any of its records.  Mismatched
        // responses are dropped silently — a spoofer that guesses
        // the 16-bit transaction id but not the question won't
        // poison the cache, and a misrouted reply for a different
        // query on the same socket can't bleed across in-flights.
        //
        // The TC-bit short-circuit below runs only after this
        // check passes, so a truncated wrong-question response
        // doesn't trigger a TCP fallback either.
        {
                bool       questionMatches = false;
                const String want = dnsCanonicalName(flight->questionName);
                for (const DnsQuestion &q : pkt.questions()) {
                        if (q.type != flight->type) continue;
                        if ((q.klass & 0x7FFF) != (flight->klass & 0x7FFF)) continue;
                        if (dnsCanonicalName(q.name) != want) continue;
                        questionMatches = true;
                        break;
                }
                if (!questionMatches) return;
        }

        // Cancel the in-flight timer; we have a response.
        if (flight->timerId >= 0) {
                lookup->stopTimer(flight->timerId);
                flight->timerId = -1;
        }

        // Truncated → promote to TCP and re-issue the same question.
        if (pkt.isTruncated() && !flight->triedTcp) {
                flight->triedTcp = true;
                beginTcpFallback(flight);
                return;
        }

        const uint8_t rcode = pkt.rcode();
        lookup->_rcode = DnsRcode(static_cast<int>(rcode));

        // CNAME handling: if the answer set holds a CNAME for our
        // current question name but no records of the asked-for
        // type, chase the CNAME.
        if (rcode == 0) {
                bool        sawCname = false;
                String      cnameTarget;
                List<DnsRecord> matches = filterAnswerRecords(
                        pkt, flight->questionName, flight->type, flight->klass);
                if (matches.isEmpty()) {
                        for (const DnsRecord &r : pkt.records()) {
                                if (r.type == DnsRecord::Type::Cname &&
                                    dnsCanonicalName(r.name) ==
                                        dnsCanonicalName(flight->questionName)) {
                                        sawCname    = true;
                                        cnameTarget = r.cnameTarget;
                                        break;
                                }
                        }
                }
                if (!matches.isEmpty()) {
                        // Cache the positive answer keyed on the
                        // *original* user-supplied name when this
                        // is the first attempt (no CNAME chase, no
                        // search-list rewrite).  Otherwise cache
                        // under the current question name.
                        const String &cacheKey =
                            (flight->cnameDepth == 0 && flight->searchIdx == 0)
                                ? lookup->name()
                                : flight->questionName;
                        _cache.put(cacheKey, flight->type, flight->klass, matches);
                        succeedLookup(lookup, matches);
                        return;
                }
                if (sawCname && flight->cnameDepth < MaxCnameDepth) {
                        followCname(flight, cnameTarget);
                        return;
                }
                // NoError with no matching records: cache negatively
                // with SOA-minimum TTL if available, otherwise the
                // cache's default policy.
                Duration negTtl = soaMinimumFromAuthority(pkt);
                _cache.putNegative(lookup->name(), flight->type, flight->klass,
                                   DnsRcode::NoError, negTtl);
                failLookup(lookup, Error(Error::HostNotFound));
                return;
        }

        if (rcode == 3) {   // NxDomain
                // Try the next search-list candidate before giving up.
                if (flight->searchIdx + 1 < flight->searchPaths.size()) {
                        ++flight->searchIdx;
                        flight->questionName = flight->searchPaths[flight->searchIdx];
                        flight->attempts = 0;
                        flight->serverIdx = 0;
                        flight->cnameDepth = 0;
                        beginAttempt(flight);
                        return;
                }
                Duration negTtl = soaMinimumFromAuthority(pkt);
                _cache.putNegative(lookup->name(), flight->type, flight->klass,
                                   DnsRcode::NxDomain, negTtl);
                failLookup(lookup, Error(Error::HostNotFound));
                return;
        }

        // Any other rcode (ServFail / Refused / NotImp / FormErr ...)
        // gets surfaced as ProtocolError so callers can distinguish
        // it from a clean NXDOMAIN.
        failLookup(lookup, Error(Error::ProtocolError));
}

void DnsResolver::followCname(Inflight *flight, const String &target) {
        flight->questionName = target;
        ++flight->cnameDepth;
        flight->attempts = 0;
        flight->triedTcp = false;
        flight->serverIdx = 0;
        beginAttempt(flight);
}

void DnsResolver::onLookupTimeout(DnsLookup *lookup) {
        auto it = _byLookup.find(lookup);
        if (it == _byLookup.end()) return;
        Inflight *flight = it->second;
        if (flight == nullptr) return;
        // Stop the timer that just fired.
        if (flight->timerId >= 0) {
                lookup->stopTimer(flight->timerId);
                flight->timerId = -1;
        }
        ++flight->attempts;
        if (flight->attempts >= flight->maxAttempts) {
                failLookup(lookup, Error(Error::Timeout));
                return;
        }
        // Rotate to next server.
        DnsConfig cfg = config();
        if (cfg.nameservers().isEmpty()) {
                failLookup(lookup, Error(Error::NotReady));
                return;
        }
        flight->serverIdx = (flight->serverIdx + 1) % cfg.nameservers().size();
        beginAttempt(flight);
}

// ============================================================================
// Completion paths
// ============================================================================
void DnsResolver::succeedLookup(DnsLookup *lookup, const List<DnsRecord> &records) {
        if (lookup == nullptr) return;
        if (lookup->_emitted.exchange(true)) return;
        lookup->_active.setValue(false);
        lookup->answeredSignal.emit(records);
        removeInflight(lookup);
        lookup->deleteLater();
}

void DnsResolver::failLookup(DnsLookup *lookup, Error err) {
        if (lookup == nullptr) return;
        if (lookup->_emitted.exchange(true)) return;
        lookup->_active.setValue(false);
        lookup->failedSignal.emit(err);
        removeInflight(lookup);
        lookup->deleteLater();
}

void DnsResolver::removeInflight(DnsLookup *lookup) {
        auto it = _byLookup.find(lookup);
        if (it == _byLookup.end()) return;
        Inflight *flight = it->second;
        _byLookup.remove(it);
        if (flight == nullptr) return;
        if (flight->timerId >= 0 && lookup != nullptr) {
                lookup->stopTimer(flight->timerId);
        }
        if (flight->txid != 0) {
                auto jt = _inflight.find(flight->txid);
                if (jt != _inflight.end() && jt->second == flight) {
                        _inflight.remove(jt);
                }
        }
        if (flight->tcpIoHandle >= 0 && eventLoop() != nullptr) {
                eventLoop()->removeIoSource(flight->tcpIoHandle);
                flight->tcpIoHandle = -1;
        }
#if PROMEKI_ENABLE_MDNS
        if (flight->mdnsObserver >= 0) {
                MdnsManager *mgr = effectiveMdnsManager();
                if (mgr != nullptr) mgr->unregisterPacketObserver(flight->mdnsObserver);
                flight->mdnsObserver = -1;
        }
#endif
        delete flight;
}

// ============================================================================
// TCP fallback
// ============================================================================
void DnsResolver::beginTcpFallback(Inflight *flight) {
        if (flight == nullptr) return;
        DnsLookup *lookup = flight->lookup.data();
        if (lookup == nullptr) return;
        DnsConfig cfg = config();
        if (cfg.nameservers().isEmpty()) {
                failLookup(lookup, Error(Error::NotReady));
                return;
        }
        const SocketAddress &srv =
            cfg.nameservers()[flight->serverIdx % cfg.nameservers().size()];

        flight->tcp.reset(new TcpSocket(this));
        // Open the TCP socket in the family matching the server.
        Error e = srv.address().isIPv6()
                      ? flight->tcp->openIpv6(IODevice::ReadWrite)
                      : flight->tcp->open(IODevice::ReadWrite);
        if (e.isError()) {
                failLookup(lookup, e);
                return;
        }
        // connectToHost is synchronous; without setNonBlocking the
        // kernel default connect timeout (~75 s on Linux) would stall
        // the resolver's EventLoop on an unreachable server.  Set
        // non-blocking so the implementation's poll-based wait
        // honours the resolver's own per-attempt timeout instead.
        flight->tcp->setNonBlocking(true);
        e = flight->tcp->connectToHost(srv);
        if (e.isError() && e != Error::TryAgain && e != Error::Busy) {
                failLookup(lookup, e);
                return;
        }

        // Build query + length prefix + send.
        DnsPacket::Builder b;
        b.setTransactionId(flight->txid);
        b.setRecursionDesired(true);
        b.addQuestion(flight->questionName,
                      static_cast<DnsRecord::Type>(flight->type),
                      flight->klass);
        // No EDNS0 needed on TCP — the 2-byte length prefix already
        // lets the response carry up to 65535 bytes.
        Buffer pkt = b.finish();
        if (pkt.size() == 0) {
                failLookup(lookup, Error(Error::Invalid));
                return;
        }
        uint8_t prefix[2];
        prefix[0] = static_cast<uint8_t>((pkt.size() >> 8) & 0xFF);
        prefix[1] = static_cast<uint8_t>(pkt.size() & 0xFF);
        flight->tcp->write(prefix, 2);
        flight->tcp->write(pkt.data(), pkt.size());

        flight->tcpReadBuf.clear();

        // Register read-side IO source.  Capture an
        // @ref ObjectBasePtr to the lookup so a teardown (cancel,
        // success on a different path, resolver shutdown) that
        // destroys the lookup between callback dispatches turns the
        // callback into a no-op rather than dereferencing freed
        // memory.  The flight pointer alone is unsafe because
        // @ref removeInflight deletes it as part of the success /
        // failure paths invoked from inside the callback.
        EventLoop *loop = eventLoop();
        if (loop == nullptr) {
                failLookup(lookup, Error(Error::NotReady));
                return;
        }
        TcpSocket *raw      = flight->tcp.get();
        ObjectBasePtr<DnsLookup> lookupPtr(lookup);
        flight->tcpIoHandle = loop->addIoSource(
            raw->socketDescriptor(), EventLoop::IoRead,
            [this, lookupPtr, raw](int /*fd*/, uint32_t events) mutable {
                    DnsLookup *lk = lookupPtr.data();
                    if (lk == nullptr) return;
                    auto it = _byLookup.find(lk);
                    if (it == _byLookup.end()) return;
                    Inflight *flt = it->second;
                    if (flt == nullptr) return;
                    if ((events & EventLoop::IoError) != 0) {
                            failLookup(lk, Error(Error::IOError));
                            return;
                    }
                    if ((events & EventLoop::IoRead) == 0) return;
                    uint8_t buf[2048];
                    int64_t n = raw->read(buf, sizeof(buf));
                    if (n <= 0) {
                            failLookup(lk, Error(Error::ConnectionReset));
                            return;
                    }
                    for (int64_t i = 0; i < n; ++i) flt->tcpReadBuf += buf[i];
                    if (flt->tcpReadBuf.size() < 2) return;
                    const size_t want =
                        (static_cast<size_t>(flt->tcpReadBuf[0]) << 8) |
                        static_cast<size_t>(flt->tcpReadBuf[1]);
                    if (flt->tcpReadBuf.size() < 2 + want) return;
                    processResponseBytes(&flt->tcpReadBuf[2], want);
            });
}

// ============================================================================
// mDNS (.local.) routing
// ============================================================================
#if PROMEKI_ENABLE_MDNS
Error DnsResolver::beginMdnsLookup(Inflight *flight) {
        if (flight == nullptr) return Error(Error::Invalid);
        DnsLookup   *lookup = flight->lookup.data();
        MdnsManager *mgr    = effectiveMdnsManager();
        if (mgr == nullptr) return Error(Error::NotReady);
        if (!mgr->isActive()) {
                Error e = mgr->start();
                if (e.isError()) return e;
        }
        flight->isMdns = true;

        // Register a per-flight packet observer.  The observer
        // fires on the MdnsManager's worker thread; everything it
        // touches on this DnsResolver lives on the resolver's
        // EventLoop, so we marshal the packet back via
        // @c postCallable.  An @ref ObjectBasePtr to the lookup
        // gates the dispatch — a teardown between marshalling and
        // dispatch turns the callable into a no-op.
        EventLoop *myLoop = eventLoop();
        if (myLoop == nullptr) return Error(Error::NotReady);
        ObjectBasePtr<DnsLookup> lookupPtr(lookup);
        flight->mdnsObserver = mgr->registerPacketObserver(
            [this, lookupPtr, myLoop](NetworkInterface, SocketAddress, Buffer data) mutable {
                    // Buffer is a value-type handle (refcount), so
                    // capturing by value here is cheap and safe to
                    // hand to another thread's loop.
                    myLoop->postCallable([this, lookupPtr, data]() mutable {
                            DnsLookup *lk = lookupPtr.data();
                            if (lk == nullptr) return;
                            auto it = _byLookup.find(lk);
                            if (it == _byLookup.end()) return;
                            Inflight *flt = it->second;
                            if (flt == nullptr || !flt->isMdns) return;
                            handleMdnsPacket(flt, data);
                    });
            });
        if (flight->mdnsObserver < 0) return Error(Error::LibraryFailure);

        // Send the multicast question.  The QU bit is left off so
        // the response goes to the multicast group — receivers
        // bound to 5353 (including the MdnsManager whose packet
        // observer we just registered) see it.
        Error e = mgr->sendQuery(flight->questionName, flight->type, /*txid=*/0, /*qu=*/false);
        if (e.isError()) return e;

        // Per-attempt timeout (re-using the unicast knobs).
        DnsConfig          cfg = config();
        const unsigned int timeoutMs = static_cast<unsigned int>(
            cfg.timeout().milliseconds() > 0 ? cfg.timeout().milliseconds() : DefaultAttemptTimeoutMs);
        if (flight->timerId >= 0) {
                lookup->stopTimer(flight->timerId);
                flight->timerId = -1;
        }
        flight->timerId = lookup->startTimer(timeoutMs, /*singleShot=*/true);
        return Error();
}

void DnsResolver::handleMdnsPacket(Inflight *flight, const Buffer &data) {
        if (flight == nullptr) return;
        DnsLookup *lookup = flight->lookup.data();
        if (lookup == nullptr) return;
        auto r = DnsPacket::parseMdns(data);
        if (r.second().isError()) return;
        const DnsPacket &pkt = r.first();
        // Only consider response packets; mDNS queries can ride
        // on the same multicast group, but they're not for us.
        if (!pkt.isResponse()) return;

        // Walk the records (mDNS responses put A/AAAA in the Answer
        // section; some implementations stuff PTR target addresses
        // into Additional).  Match on owner name + type.
        List<DnsRecord> matches;
        const String canonical = dnsCanonicalName(flight->questionName);
        for (const DnsRecord &rec : pkt.records()) {
                if (static_cast<uint16_t>(rec.type) != flight->type) continue;
                if (dnsCanonicalName(rec.name) != canonical) continue;
                matches += rec;
        }
        if (matches.isEmpty()) return;

        // Stop the per-attempt timer and cache the answer.
        if (flight->timerId >= 0) {
                lookup->stopTimer(flight->timerId);
                flight->timerId = -1;
        }
        _cache.put(lookup->name(), flight->type, flight->klass, matches);
        succeedLookup(lookup, matches);
}
#else
Error DnsResolver::beginMdnsLookup(Inflight *) { return Error(Error::NotSupported); }
void  DnsResolver::handleMdnsPacket(Inflight *, const Buffer &) {}
#endif

// ============================================================================
// Search-list expansion
// ============================================================================
List<String> DnsResolver::expandSearch(const String &name) const {
        List<String> out;
        if (name.isEmpty()) return out;

        // Always queue the canonicalised verbatim name first.
        const bool absolute = name.endsWith('.');
        String     base     = name;
        if (!absolute) {
                // Append the root marker so the upstream resolver
                // does not run its own search-list against us.
        }

        DnsConfig cfg = config();
        // Count dots (in the original text form, ignoring escapes
        // for the heuristic — close enough for the RFC 6761
        // single-label test).
        int dots = 0;
        for (size_t i = 0; i < name.size(); ++i) {
                if (name.cstr()[i] == '.') ++dots;
        }
        // Absolute names + names with >= ndots dots get the bare
        // form first; unqualified short names get the search-list
        // suffixes first.
        const bool bareFirst = absolute || dots >= cfg.ndots();

        auto withSuffix = [](const String &n, const String &dom) {
                String s = n;
                if (!s.endsWith('.')) s += '.';
                if (dom.isEmpty()) return s;
                if (!dom.endsWith('.')) s += dom + String(".");
                else                    s += dom;
                return s;
        };

        if (bareFirst) {
                String bare = base;
                if (!bare.endsWith('.')) bare += '.';
                out += bare;
        }
        for (const String &dom : cfg.searchDomains()) {
                out += withSuffix(base, dom);
        }
        if (!bareFirst) {
                String bare = base;
                if (!bare.endsWith('.')) bare += '.';
                out += bare;
        }
        return out;
}

// ============================================================================
// Helpers
// ============================================================================
List<DnsRecord> DnsResolver::filterAnswerRecords(const DnsPacket &pkt,
                                                  const String &name,
                                                  uint16_t type, uint16_t klass) {
        List<DnsRecord> out;
        const String canonical = dnsCanonicalName(name);
        for (const DnsRecord &r : pkt.records()) {
                if (r.section != DnsRecord::Section::Answer) continue;
                if (static_cast<uint16_t>(r.type) != type) continue;
                // Class match: top-bit-masked compare so mDNS-style
                // sentinels (impossible here for unicast DNS, but
                // defensive) don't cause a miss.
                if ((r.klass & 0x7FFF) != (klass & 0x7FFF)) continue;
                if (dnsCanonicalName(r.name) != canonical) continue;
                out += r;
        }
        return out;
}

Duration DnsResolver::soaMinimumFromAuthority(const DnsPacket &pkt) {
        for (const DnsRecord &r : pkt.records()) {
                if (r.section != DnsRecord::Section::Authority) continue;
                if (r.type    != DnsRecord::Type::Soa) continue;
                return Duration::fromSeconds(static_cast<int64_t>(r.soaMinimum));
        }
        return Duration::zero();
}

// ============================================================================
// Sync wrappers
// ============================================================================
Result<List<DnsRecord>> DnsResolver::lookupSync(const String &name,
                                                 const DnsRecordType &type,
                                                 const Duration &timeout) {
        // Preferred path: route through the process-wide default
        // resolver so successive sync calls share the TTL cache.
        // The default resolver runs on the main thread's EventLoop;
        // we post the lookup creation there, attach signal handlers
        // that wake a local condition variable, and block the
        // calling thread until the lookup completes or the timeout
        // expires.
        //
        // Fallback path (no default resolver available): spin a
        // private DnsResolver on the calling thread.  This is also
        // what happens when the call originates from the main
        // thread itself — driving processEvents on the main loop
        // from inside a sync wrapper would re-enter the resolver
        // (and any other main-loop work).
        EventLoop *mainLoop = Application::mainEventLoop();
        DnsResolver *shared = (mainLoop != nullptr &&
                               EventLoop::current() != mainLoop)
                                  ? DnsResolver::defaultInstance()
                                  : nullptr;
        if (shared != nullptr) {
                Mutex                   mtx;
                WaitCondition           cv;
                bool                    done   = false;
                Result<List<DnsRecord>> result =
                    makeError<List<DnsRecord>>(Error::Timeout);

                // Lookup creation has to happen on the resolver's
                // loop because @ref DnsResolver::lookup posts to
                // that loop; we marshal the lookup pointer back via
                // an ObjectBasePtr so the signal-handler scope can
                // survive whatever cross-thread ordering games the
                // loops play.
                ObjectBasePtr<DnsLookup> lookupPtr;
                Atomic<bool>             readyToConnect;
                readyToConnect.setValue(false);
                mainLoop->postCallable([&]() {
                        DnsLookup *lk = shared->lookup(name, type);
                        lookupPtr      = ObjectBasePtr<DnsLookup>(lk);
                        readyToConnect.setValue(true);
                });
                // Briefly wait for the lookup to be created on the
                // resolver loop.  This is bounded by the overall
                // timeout — main-loop liveness is the caller's
                // problem.
                const TimeStamp createDeadline = TimeStamp::now() + timeout;
                while (!readyToConnect.value() && TimeStamp::now() < createDeadline) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                DnsLookup *lookup = lookupPtr.data();
                if (lookup == nullptr) {
                        return makeError<List<DnsRecord>>(Error::Timeout);
                }
                // Connect signals from the calling thread; the
                // dispatch crosses back to the calling thread's
                // EventLoop only when the slot has a non-resolver
                // context object — we use the resolver as context
                // here so the slot runs on the resolver's loop,
                // which is exactly where we want it.
                lookup->answeredSignal.connect([&](List<DnsRecord> recs) {
                        Mutex::Locker l(mtx);
                        result = makeResult(recs);
                        done   = true;
                        cv.wakeOne();
                }, shared);
                lookup->failedSignal.connect([&](Error e) {
                        Mutex::Locker l(mtx);
                        result = makeError<List<DnsRecord>>(e);
                        done   = true;
                        cv.wakeOne();
                }, shared);

                {
                        Mutex::Locker l(mtx);
                        const int64_t timeoutMs = timeout.milliseconds();
                        if (!done) {
                                cv.wait(mtx, [&] { return done; },
                                        static_cast<unsigned int>(timeoutMs > 0 ? timeoutMs : 0));
                        }
                }
                if (!done) {
                        // Tear down the abandoned lookup on its
                        // own loop.
                        DnsLookup *toCancel = lookupPtr.data();
                        if (toCancel != nullptr) toCancel->cancel();
                }
                return result;
        }

        // Private-resolver fallback: spin a one-shot EventLoop on
        // the calling thread.  No cache sharing.
        EventLoop   loop;
        DnsResolver res;
        DnsLookup  *lookup = res.lookup(name, type);
        Atomic<bool> done;
        done.setValue(false);
        Mutex                resultMtx;
        Result<List<DnsRecord>> result =
            makeError<List<DnsRecord>>(Error::Timeout);

        lookup->answeredSignal.connect([&](List<DnsRecord> recs) {
                Mutex::Locker l(resultMtx);
                result = makeResult(recs);
                done.setValue(true);
                loop.quit(0);
        }, &res);
        lookup->failedSignal.connect([&](Error e) {
                Mutex::Locker l(resultMtx);
                result = makeError<List<DnsRecord>>(e);
                done.setValue(true);
                loop.quit(0);
        }, &res);

        const TimeStamp deadline = TimeStamp::now() + timeout;
        while (!done.value() && TimeStamp::now() < deadline) {
                const int64_t remainMs = (deadline - TimeStamp::now()).milliseconds();
                if (remainMs <= 0) break;
                const unsigned int cap = static_cast<unsigned int>(remainMs < 50 ? remainMs : 50);
                loop.processEvents(EventLoop::WaitForMore, cap);
        }
        if (!done.value()) lookup->cancel();
        return result;
}

Result<NetworkAddress> DnsResolver::resolveSync(const String &host,
                                                 NetworkAddress::FamilyPreference prefer,
                                                 const Duration &timeout) {
        if (host.isEmpty()) return makeError<NetworkAddress>(Error::Invalid);
        // IP-literal shortcut.  Url::host() returns @c "[::1]" for
        // IPv6 literals (brackets are part of the authority
        // production in RFC 3986); strip the brackets before
        // attempting the address parse so we don't fall through to
        // the (doomed) DNS path on a literal address.
        {
                String stripped = host;
                if (stripped.size() >= 2 && stripped.startsWith('[') &&
                    stripped.endsWith(']')) {
                        stripped = stripped.mid(1, stripped.size() - 2);
                }
                auto na = NetworkAddress::fromString(stripped);
                if (na.second().isOk() && na.first().isResolved()) {
                        return makeResult(na.first());
                }
        }
        // "localhost" shortcut so loopback resolves even when no
        // resolver is reachable.
        if (host.toLower() == String("localhost")) {
                if (prefer == NetworkAddress::PreferIPv6) {
                        return makeResult(NetworkAddress(Ipv6Address::loopback()));
                }
                return makeResult(NetworkAddress(Ipv4Address::loopback()));
        }

        const DnsRecordType firstType =
            (prefer == NetworkAddress::PreferIPv6) ? DnsRecordType::Aaaa : DnsRecordType::A;
        const DnsRecordType secondType =
            (prefer == NetworkAddress::PreferIPv6) ? DnsRecordType::A : DnsRecordType::Aaaa;

        auto pickFirst = [&](const List<DnsRecord> &recs) -> Result<NetworkAddress> {
                for (const DnsRecord &r : recs) {
                        if (r.type == DnsRecord::Type::A) {
                                return makeResult(NetworkAddress(r.a));
                        }
                        if (r.type == DnsRecord::Type::Aaaa) {
                                return makeResult(NetworkAddress(r.aaaa));
                        }
                }
                return makeError<NetworkAddress>(Error::HostNotFound);
        };

        auto r1 = lookupSync(host, firstType, timeout);
        if (r1.second().isOk() && !r1.first().isEmpty()) {
                auto pick = pickFirst(r1.first());
                if (pick.second().isOk()) return pick;
        }
        if (prefer == NetworkAddress::AnyFamily) {
                return makeError<NetworkAddress>(Error::HostNotFound);
        }
        auto r2 = lookupSync(host, secondType, timeout);
        if (r2.second().isOk() && !r2.first().isEmpty()) {
                auto pick = pickFirst(r2.first());
                if (pick.second().isOk()) return pick;
        }
        return makeError<NetworkAddress>(Error::HostNotFound);
}

Result<List<DnsRecord>> DnsResolver::lookupReverseSync(const NetworkAddress &addr,
                                                        const Duration &timeout) {
        if (!addr.isResolved()) return makeError<List<DnsRecord>>(Error::Invalid);
        String name;
        if (addr.isIPv4()) {
                Ipv4Address v4 = addr.toIpv4();
                char buf[64];
                std::snprintf(buf, sizeof(buf), "%u.%u.%u.%u.in-addr.arpa.",
                              v4.octet(3), v4.octet(2), v4.octet(1), v4.octet(0));
                name = String(buf);
        } else {
                Ipv6Address     v6 = addr.toIpv6();
                const uint8_t  *r  = v6.raw();
                String s;
                for (int i = 15; i >= 0; --i) {
                        const uint8_t b = r[i];
                        const char    hexlo = "0123456789abcdef"[b & 0x0F];
                        const char    hexhi = "0123456789abcdef"[(b >> 4) & 0x0F];
                        s += hexlo; s += '.'; s += hexhi; s += '.';
                }
                s += String("ip6.arpa.");
                name = s;
        }
        return lookupSync(name, DnsRecordType::Ptr, timeout);
}

PROMEKI_NAMESPACE_END
