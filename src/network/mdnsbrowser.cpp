/**
 * @file      mdnsbrowser.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/mdnsbrowser.h>
#include <promeki/application.h>
#include <promeki/mdnsmanager.h>
#include <promeki/mdnsname.h>
#include <promeki/mdnspacket.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

        enum class Emit { None, Found, Updated, Lost };

        // Returns @c true when both names match up to case and an
        // optional trailing root marker on either side.  DNS names
        // are case-insensitive (RFC 1035 §3.1) and the textual form
        // is allowed to omit the empty root label; this normalises
        // both before comparing.
        bool dnsEquals(const String &a, const String &b) {
                auto strip = [](const String &s) -> String {
                        if (s.size() > 0 && s[s.size() - 1] == '.') return s.substr(0, s.size() - 1);
                        return s;
                };
                return strip(a).compareIgnoreCase(strip(b)) == 0;
        }

} // anonymous namespace

MdnsBrowser::MdnsBrowser(const MdnsServiceType &type, ObjectBase *parent)
    : ObjectBase(parent), _type(type) {
        _active.setValue(false);
        _queryFireCount.setValue(0);
}

Duration MdnsBrowser::currentBackoffInterval() const {
        Mutex::Locker lock(_entriesMtx);
        return _currentInterval;
}

MdnsBrowser::~MdnsBrowser() {
        stop();
        setManager(nullptr);
}

Error MdnsBrowser::start() {
        if (effectiveManager() == nullptr) return Error::NotReady;
        _active.setValue(true);
        // Seed the continuous-query schedule.  RFC 6762 §5.2 says
        // each successive query interval is at most twice the
        // previous, capped at one hour — we start at 1 s and double
        // from there.  The initial query is fired immediately; the
        // first follow-up tick query lands one interval later.
        {
                Mutex::Locker lock(_entriesMtx);
                _currentInterval = Duration::fromMilliseconds(InitialQueryIntervalMs);
                _nextQueryAt     = TimeStamp::now() + _currentInterval;
        }
        return sendQuery();
}

void MdnsBrowser::stop() {
        _active.setValue(false);
        // Cache is intentionally left alone — discovered instances
        // stay addressable until they expire or a Goodbye arrives.
        // Backoff schedule is reset so a future @ref start picks the
        // fresh @ref InitialQueryIntervalMs cadence rather than
        // continuing from where it left off.
        Mutex::Locker lock(_entriesMtx);
        _currentInterval = Duration();
        _nextQueryAt     = TimeStamp();
}

void MdnsBrowser::onManagerTick(const TimeStamp &now) {
        evictExpiredAt(now);
        if (!_active.value() || effectiveManager() == nullptr) return;
        // Read + advance the schedule under the mutex; release before
        // dispatching the actual send so a slow writeDatagram cannot
        // hold the cache lock against an incoming packet that lands
        // on the worker.
        bool fire = false;
        {
                Mutex::Locker lock(_entriesMtx);
                if (!_nextQueryAt.isValid()) return;   // start() not run yet
                if (now < _nextQueryAt) return;
                fire = true;
                Duration next = _currentInterval * 2;
                const Duration cap = Duration::fromMilliseconds(MaxQueryIntervalMs);
                if (next > cap) next = cap;
                _currentInterval = next;
                _nextQueryAt     = now + _currentInterval;
        }
        if (fire) {
                ++_queryFireCount;
                sendQuery();
        }
}

Error MdnsBrowser::sendQuery() {
        MdnsManager *mgr = effectiveManager();
        if (mgr == nullptr) return Error::NotReady;
        // PTR (12) on the service-type FQDN — standard browse query
        // per RFC 6763 §4.1.  Transaction ID stays at 0 so receivers
        // do not lean on it for correlation; they MUST match by name
        // and type instead.
        //
        // Known-Answer Suppression (RFC 6762 §7.1): include every
        // currently-cached PTR for the type FQDN in the Authority
        // section.  Responders that see a record they were about to
        // emit in the known-answer list MAY suppress it for the next
        // ~500 ms, which collapses the announce traffic on busy LANs
        // from N×responders to ~1 per distinct unknown instance.
        const List<MdnsRecord> known = composeKnownAnswers();
        if (known.isEmpty()) return mgr->sendQuery(_type.toFqdn(), /*PTR*/ 12, 0);
        return mgr->sendQueryWithKnownAnswers(_type.toFqdn(), /*PTR*/ 12, known, 0);
}

List<MdnsRecord> MdnsBrowser::composeKnownAnswers() const {
        // Spec optimization not implemented here: §7.1 says known
        // answers should only carry entries whose remaining TTL is
        // greater than half the original TTL.  We include everything
        // — a slightly heavier query for slightly lighter responses,
        // which is the right trade for our typical small-LAN cache.
        const TimeStamp now = TimeStamp::now();
        const String typeFqdn = _type.toFqdn();
        List<MdnsRecord> out;
        Mutex::Locker lock(_entriesMtx);
        for (const auto &kv : _entries) {
                const Entry &e = kv.second;
                if (!e.hasPtr) continue;
                const String fqdn = e.instance.fqdn();
                if (fqdn.isEmpty()) continue;
                Duration remaining = e.ttl;
                if (e.lastSeen.isValid() && e.ttl.isValid()) {
                        const Duration elapsed = now - e.lastSeen;
                        if (elapsed.isValid() && elapsed < e.ttl) {
                                remaining = e.ttl - elapsed;
                        }
                }
                if (!remaining.isValid() || remaining <= Duration::zero()) continue;
                out += MdnsRecord::ptr(typeFqdn, fqdn, remaining);
        }
        return out;
}

void MdnsBrowser::setManager(MdnsManager *manager) {
        if (_manager == manager) return;
        if (_manager != nullptr) {
                _manager->unregisterBrowser(this);
        }
        _manager = manager;
        if (_manager != nullptr) {
                _manager->registerBrowser(this);
        }
}

MdnsManager *MdnsBrowser::effectiveManager() const {
        if (_manager != nullptr) return _manager;
        // Lazy fallback to the application-wide engine.  We do not
        // call registerBrowser on the singleton — the browser stays
        // unregistered from the fallback so packet fan-out does not
        // happen automatically.  Callers that want fan-out wire the
        // browser up explicitly via @ref setManager.  This keeps the
        // global from accumulating dangling raw-pointer registrations
        // as test fixtures construct and destroy browsers.
        return Application::mdnsManager();
}

void MdnsBrowser::clearCache() {
        Mutex::Locker lock(_entriesMtx);
        _entries.clear();
}

List<MdnsServiceInstance> MdnsBrowser::instances() const {
        Mutex::Locker lock(_entriesMtx);
        List<MdnsServiceInstance> out;
        for (const auto &kv : _entries) {
                if (kv.second.foundEmitted) out += kv.second.instance;
        }
        return out;
}

String MdnsBrowser::foldName(const String &s) {
        // Strip the optional trailing root marker so a hostname or
        // FQDN written either way keys identically.
        String stripped = s;
        if (stripped.size() > 0 && stripped[stripped.size() - 1] == '.') {
                stripped = stripped.substr(0, stripped.size() - 1);
        }
        return stripped.toLower();
}

String MdnsBrowser::extractInstanceLabel(const String &targetFqdn) const {
        // <Instance>.<TypeFqdn>.  Both sides arrive escape-encoded
        // (the parser produced escaped text), so we walk the labels
        // through @ref mdnsSplitName which respects @c "\\." escape
        // sequences inside an instance label.  The first label is
        // the raw instance bytes; the remaining labels must match
        // the type FQDN labels for the entry to belong to us.
        const List<String> targetLabels = mdnsSplitName(targetFqdn);
        const List<String> typeLabels   = mdnsSplitName(_type.toFqdn());
        if (targetLabels.size() != typeLabels.size() + 1) return String();
        for (size_t i = 0; i < typeLabels.size(); ++i) {
                if (targetLabels[i + 1].compareIgnoreCase(typeLabels[i]) != 0) {
                        return String();
                }
        }
        return targetLabels[0];
}

bool MdnsBrowser::isAddressable(const MdnsServiceInstance &inst) {
        return inst.port() != 0 && !inst.hostname().isEmpty();
}

void MdnsBrowser::handlePacket(const Buffer &data, const SocketAddress & /*sender*/,
                               const NetworkInterface &iface) {
        auto r = MdnsPacket::parse(data);
        if (!r.second().isOk()) return;
        // Collect directed follow-up queries across every record in
        // this packet, dedupe them, and issue them off-lock once
        // processing completes.  Batching across the packet means a
        // single PTR + bare-bones SRV stream produces one round of
        // directed queries per host, not one per record.
        List<DirectedQuery> directedQueries;
        for (const MdnsParsedRecord &rec : r.first().records()) {
                processRecord(rec, iface, directedQueries);
        }
        if (!directedQueries.isEmpty()) issueDirectedQueries(directedQueries);
}

int MdnsBrowser::evictExpired() {
        return evictExpiredAt(TimeStamp::now());
}

int MdnsBrowser::evictExpiredAt(const TimeStamp &now) {
        List<MdnsServiceInstance> lost;
        {
                Mutex::Locker lock(_entriesMtx);
                // Two-pass: walk to collect keys, then erase, so we
                // do not invalidate the iterator under our own feet.
                List<String> doomed;
                for (const auto &kv : _entries) {
                        const Entry &e = kv.second;
                        // An entry without a valid lastSeen / ttl
                        // pair predates any record refresh — leave
                        // it alone (it is either a fresh placeholder
                        // or test-injected state).
                        if (!e.lastSeen.isValid() || !e.ttl.isValid()) continue;
                        const TimeStamp expiry = e.lastSeen + e.ttl;
                        if (expiry > now) continue;
                        doomed += kv.first;
                }
                for (const String &k : doomed) {
                        auto it = _entries.find(k);
                        if (it == _entries.end()) continue;
                        if (it->second.foundEmitted) lost += it->second.instance;
                        _entries.remove(k);
                }
        }
        for (const MdnsServiceInstance &inst : lost) {
                serviceLostSignal.emit(inst);
        }
        return static_cast<int>(lost.size());
}

void MdnsBrowser::processRecord(const MdnsParsedRecord &rec, const NetworkInterface &iface,
                                List<DirectedQuery> &directedQueries) {
        const String typeFqdn = _type.toFqdn();
        const TimeStamp     now      = TimeStamp::now();
        Emit                emitKind = Emit::None;
        MdnsServiceInstance emitInstance;

        {
                Mutex::Locker lock(_entriesMtx);

                switch (rec.type) {
                        case MdnsParsedRecord::Type::Ptr: {
                                // PTR's owner must be our service type FQDN.
                                if (!dnsEquals(rec.name, typeFqdn)) break;
                                const String &targetFqdn = rec.ptrTarget;
                                if (targetFqdn.isEmpty()) break;

                                const String key = foldName(targetFqdn);

                                // Goodbye — TTL=0 PTR for a known instance.
                                if (rec.ttl == Duration::zero()) {
                                        auto it = _entries.find(key);
                                        if (it == _entries.end()) break;
                                        Entry e = it->second;
                                        _entries.remove(key);
                                        if (e.foundEmitted) {
                                                emitKind     = Emit::Lost;
                                                emitInstance = e.instance;
                                        }
                                        break;
                                }

                                // Hello — create a placeholder or
                                // refresh an existing entry's TTL.
                                auto it = _entries.find(key);
                                if (it == _entries.end()) {
                                        Entry e;
                                        e.hasPtr   = true;
                                        e.lastSeen = now;
                                        e.ttl      = rec.ttl;
                                        e.instance.setType(_type);
                                        e.instance.setInstanceName(extractInstanceLabel(targetFqdn));
                                        e.instance.setTtl(rec.ttl);
                                        e.instance.setLastSeen(now);
                                        if (iface.isValid()) {
                                                e.instance.setInterfaceIndex(static_cast<int>(iface.index()));
                                        }
                                        _entries.insert(key, std::move(e));
                                        planDirectedFollowups(_entries.find(key)->second, now, directedQueries);
                                } else {
                                        it->second.hasPtr  = true;
                                        it->second.lastSeen = now;
                                        it->second.ttl     = rec.ttl;
                                        it->second.instance.setTtl(rec.ttl);
                                        it->second.instance.setLastSeen(now);
                                        planDirectedFollowups(it->second, now, directedQueries);
                                }
                                break;
                        }

                        case MdnsParsedRecord::Type::Srv: {
                                const String key = foldName(rec.name);

                                Entry *entry = nullptr;
                                auto   it    = _entries.find(key);
                                if (it == _entries.end()) {
                                        // SRV arrived without a
                                        // preceding PTR (legal in
                                        // additional-section freebies
                                        // and unsolicited announces).
                                        Entry e;
                                        e.instance.setType(_type);
                                        e.instance.setInstanceName(extractInstanceLabel(rec.name));
                                        if (iface.isValid()) {
                                                e.instance.setInterfaceIndex(static_cast<int>(iface.index()));
                                        }
                                        _entries.insert(key, std::move(e));
                                        entry = &_entries.find(key)->second;
                                } else {
                                        entry = &it->second;
                                }
                                // Reject SRV records whose owner does
                                // not belong to our type — protects
                                // the cache from spurious entries
                                // when a multi-type packet is
                                // delivered to multiple browsers.
                                if (entry->instance.instanceName().isEmpty()) {
                                        _entries.remove(key);
                                        break;
                                }

                                MdnsServiceInstance &inst = entry->instance;
                                bool changed = false;
                                if (inst.hostname() != rec.srvTarget) {
                                        inst.setHostname(rec.srvTarget);
                                        changed = true;
                                }
                                if (inst.port() != rec.srvPort) {
                                        inst.setPort(rec.srvPort);
                                        changed = true;
                                }
                                if (rec.ttl.isValid()) inst.setTtl(rec.ttl);
                                entry->lastSeen = now;
                                entry->ttl      = rec.ttl;
                                inst.setLastSeen(now);

                                if (!entry->foundEmitted && isAddressable(inst)) {
                                        entry->foundEmitted = true;
                                        emitKind            = Emit::Found;
                                        emitInstance        = inst;
                                } else if (entry->foundEmitted && changed) {
                                        emitKind     = Emit::Updated;
                                        emitInstance = inst;
                                }
                                // SRV brought a fresh hostname — if
                                // no A/AAAA cached yet for it, kick a
                                // directed query for the address
                                // records.
                                planDirectedFollowups(*entry, now, directedQueries);
                                break;
                        }

                        case MdnsParsedRecord::Type::Txt: {
                                const String key = foldName(rec.name);
                                auto         it  = _entries.find(key);
                                if (it == _entries.end()) break;
                                Entry &entry = it->second;
                                entry.lastSeen = now;
                                if (rec.ttl.isValid()) entry.ttl = rec.ttl;
                                entry.instance.setLastSeen(now);
                                if (entry.instance.txt() == rec.txt) break;
                                entry.instance.setTxt(rec.txt);
                                if (entry.foundEmitted) {
                                        emitKind     = Emit::Updated;
                                        emitInstance = entry.instance;
                                }
                                break;
                        }

                        case MdnsParsedRecord::Type::A:
                        case MdnsParsedRecord::Type::Aaaa: {
                                // Find every entry whose hostname
                                // matches this address record's owner.
                                // For step 4 we just emit Updated when
                                // a new address lands; later steps can
                                // batch this with the other freebies
                                // in the same packet.
                                Entry *target = nullptr;
                                for (auto &kv : _entries) {
                                        if (dnsEquals(kv.second.instance.hostname(), rec.name)) {
                                                target = &kv.second;
                                                break;
                                        }
                                }
                                if (target == nullptr) break;
                                target->lastSeen = now;
                                if (rec.ttl.isValid()) target->ttl = rec.ttl;
                                target->instance.setLastSeen(now);

                                // RFC 6762 §10.2 cache-flush bit:
                                // wipe the prior address list of this
                                // family before appending the new one
                                // — unless the previous flush landed
                                // within @ref CacheFlushGraceMs, in
                                // which case this record is part of
                                // the same multi-record announce and
                                // is appended rather than replacing.
                                bool changed = false;
                                const Duration grace =
                                    Duration::fromMilliseconds(CacheFlushGraceMs);
                                if (rec.type == MdnsParsedRecord::Type::A) {
                                        if (rec.cacheFlush) {
                                                const bool inGrace =
                                                    target->lastV4FlushAt.isValid() &&
                                                    (now - target->lastV4FlushAt) < grace;
                                                if (!inGrace && !target->instance.ipv4Addresses().isEmpty()) {
                                                        target->instance.setIpv4Addresses(List<Ipv4Address>());
                                                        changed = true;
                                                }
                                                target->lastV4FlushAt = now;
                                        }
                                        List<Ipv4Address> v4 = target->instance.ipv4Addresses();
                                        if (!v4.contains(rec.a)) {
                                                v4 += rec.a;
                                                target->instance.setIpv4Addresses(v4);
                                                changed = true;
                                        }
                                } else {
                                        if (rec.cacheFlush) {
                                                const bool inGrace =
                                                    target->lastV6FlushAt.isValid() &&
                                                    (now - target->lastV6FlushAt) < grace;
                                                if (!inGrace && !target->instance.ipv6Addresses().isEmpty()) {
                                                        target->instance.setIpv6Addresses(List<Ipv6Address>());
                                                        changed = true;
                                                }
                                                target->lastV6FlushAt = now;
                                        }
                                        List<Ipv6Address> v6 = target->instance.ipv6Addresses();
                                        if (!v6.contains(rec.aaaa)) {
                                                v6 += rec.aaaa;
                                                target->instance.setIpv6Addresses(v6);
                                                changed = true;
                                        }
                                }
                                if (changed && target->foundEmitted) {
                                        emitKind     = Emit::Updated;
                                        emitInstance = target->instance;
                                }
                                break;
                        }

                        case MdnsParsedRecord::Type::Unknown:
                                break;
                }
        }

        switch (emitKind) {
                case Emit::Found:   serviceFoundSignal.emit(emitInstance);   break;
                case Emit::Updated: serviceUpdatedSignal.emit(emitInstance); break;
                case Emit::Lost:    serviceLostSignal.emit(emitInstance);    break;
                case Emit::None:    break;
        }
}

void MdnsBrowser::planDirectedFollowups(Entry &entry, const TimeStamp &now,
                                        List<DirectedQuery> &directedQueries) {
        // Caller holds @ref _entriesMtx.  We mutate the entry's
        // debounce timestamps in place.  Directed queries are staged
        // onto the caller-owned list and issued off-lock.
        const Duration debounce = Duration::fromMilliseconds(DirectedQueryDebounceMs);
        const MdnsServiceInstance &inst = entry.instance;
        const String instanceFqdn       = inst.fqdn();
        if (instanceFqdn.isEmpty()) return;

        auto shouldAsk = [&](const TimeStamp &lastAt) {
                return !lastAt.isValid() || (now - lastAt) >= debounce;
        };

        // SRV: known by virtue of inst.port() being non-zero.
        if (inst.port() == 0 && shouldAsk(entry.lastDirectedQuerySrvAt)) {
                directedQueries += DirectedQuery{instanceFqdn, /*SRV*/ 33};
                entry.lastDirectedQuerySrvAt = now;
        }
        // TXT: presence inferred from non-empty record. We treat
        // "empty TXT plus PTR known" as "have not asked" — yes this
        // re-asks for hosts that publish a genuinely empty TXT, but
        // the debounce caps the spam and the receiver will accept
        // and discard the duplicate cheaply.
        if (inst.txt().isEmpty() && shouldAsk(entry.lastDirectedQueryTxtAt)) {
                directedQueries += DirectedQuery{instanceFqdn, /*TXT*/ 16};
                entry.lastDirectedQueryTxtAt = now;
        }
        // A / AAAA: only meaningful once we know the SRV target
        // hostname.
        const String &host = inst.hostname();
        if (!host.isEmpty()) {
                if (inst.ipv4Addresses().isEmpty() && shouldAsk(entry.lastDirectedQueryAAt)) {
                        directedQueries += DirectedQuery{host, /*A*/ 1};
                        entry.lastDirectedQueryAAt = now;
                }
                if (inst.ipv6Addresses().isEmpty() && shouldAsk(entry.lastDirectedQueryAaaaAt)) {
                        directedQueries += DirectedQuery{host, /*AAAA*/ 28};
                        entry.lastDirectedQueryAaaaAt = now;
                }
        }
}

void MdnsBrowser::issueDirectedQueries(const List<DirectedQuery> &queries) {
        MdnsManager *mgr = effectiveManager();
        if (mgr == nullptr) return;
        for (const DirectedQuery &q : queries) {
                mgr->sendQuery(q.name, q.recordType, 0);
        }
}

PROMEKI_NAMESPACE_END
