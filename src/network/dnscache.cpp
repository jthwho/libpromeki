/**
 * @file      dnscache.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/dnscache.h>
#include <promeki/dnsname.h>

PROMEKI_NAMESPACE_BEGIN

DnsCache::DnsCache()
    : _maxTtl(Duration::fromSeconds(DefaultMaxTtlSeconds))
    , _maxNegTtl(Duration::fromSeconds(DefaultMaxNegativeTtlSeconds))
    , _cap(DefaultCapacity) {}

void DnsCache::setMaxTtl(const Duration &d) {
        Mutex::Locker lock(_mtx);
        _maxTtl = d;
}

Duration DnsCache::maxTtl() const {
        Mutex::Locker lock(_mtx);
        return _maxTtl;
}

void DnsCache::setMaxNegativeTtl(const Duration &d) {
        Mutex::Locker lock(_mtx);
        _maxNegTtl = d;
}

Duration DnsCache::maxNegativeTtl() const {
        Mutex::Locker lock(_mtx);
        return _maxNegTtl;
}

void DnsCache::setCapacity(size_t cap) {
        Mutex::Locker lock(_mtx);
        _cap = cap;
        evictIfOversized();
}

size_t DnsCache::capacity() const {
        Mutex::Locker lock(_mtx);
        return _cap;
}

size_t DnsCache::size() const {
        Mutex::Locker lock(_mtx);
        return _entries.size();
}

void DnsCache::clear() {
        Mutex::Locker lock(_mtx);
        _entries.clear();
}

DnsCache::Hit DnsCache::get(const String &name, uint16_t type, uint16_t klass) const {
        Hit hit;
        Key key{ dnsCanonicalName(name), type, klass };

        // const_cast is safe: we hold the only handle to _entries
        // while the mutex is locked and we only manipulate it for
        // lazy eviction.
        Mutex::Locker lock(_mtx);
        auto &entries = const_cast<Map<Key, Entry> &>(_entries);
        auto  it      = entries.find(key);
        if (it == entries.end()) return hit;

        const TimeStamp now = TimeStamp::now();
        if (it->second.expiresAt <= now) {
                entries.remove(it);
                return hit;
        }
        hit.found    = true;
        hit.negative = it->second.negative;
        hit.rcode    = it->second.rcode;
        hit.records  = it->second.records;
        return hit;
}

void DnsCache::put(const String &name, uint16_t type, uint16_t klass,
                   const List<DnsRecord> &records, const Duration &ttlCap) {
        // Determine the per-entry TTL: the minimum TTL across the
        // answer RRset, clamped by the cache cap.  An empty RRset is
        // treated as a negative entry (RFC 2308 §3 considers an
        // empty-answer success a "no data" situation that should be
        // negatively cached).
        Mutex::Locker lock(_mtx);
        Entry entry;
        if (records.isEmpty()) {
                entry.negative = true;
                entry.rcode    = DnsRcode::NoError;
                Duration ttl   = ttlCap.nanoseconds() > 0 ? ttlCap : _maxNegTtl;
                if (ttl > _maxNegTtl) ttl = _maxNegTtl;
                if (ttl.nanoseconds() <= 0) return;   // nothing to cache
                entry.expiresAt = TimeStamp::now() + ttl;
        } else {
                Duration minTtl = records[0].ttl;
                for (const DnsRecord &r : records) {
                        if (r.ttl < minTtl) minTtl = r.ttl;
                }
                Duration ttl = minTtl;
                if (ttlCap.nanoseconds() > 0 && ttlCap < ttl) ttl = ttlCap;
                if (ttl > _maxTtl) ttl = _maxTtl;
                if (ttl.nanoseconds() <= 0) return;   // explicit zero TTL: do not cache
                entry.negative  = false;
                entry.rcode     = DnsRcode::NoError;
                entry.records   = records;
                entry.expiresAt = TimeStamp::now() + ttl;
        }

        Key key{ dnsCanonicalName(name), type, klass };
        _entries[key] = std::move(entry);
        evictIfOversized();
}

void DnsCache::putNegative(const String &name, uint16_t type, uint16_t klass,
                           const DnsRcode &rcode, const Duration &ttlCap) {
        Mutex::Locker lock(_mtx);
        Entry         entry;
        entry.negative = true;
        entry.rcode    = rcode;
        Duration ttl   = ttlCap.nanoseconds() > 0 ? ttlCap : _maxNegTtl;
        if (ttl > _maxNegTtl) ttl = _maxNegTtl;
        if (ttl.nanoseconds() <= 0) return;
        entry.expiresAt = TimeStamp::now() + ttl;

        Key key{ dnsCanonicalName(name), type, klass };
        _entries[key] = std::move(entry);
        evictIfOversized();
}

void DnsCache::evictIfOversized() {
        // Called with _mtx held.  Strategy: when we exceed the cap,
        // walk the map and drop entries whose @c expiresAt is in the
        // past first; if that is still not enough, drop the entry
        // with the soonest @c expiresAt repeatedly until we are
        // under the cap.  Cap of @c 0 means "no cap".
        if (_cap == 0) return;
        if (_entries.size() <= _cap) return;

        const TimeStamp now = TimeStamp::now();
        // Pass 1: drop expired entries.
        auto it = _entries.begin();
        while (it != _entries.end()) {
                if (it->second.expiresAt <= now) {
                        it = _entries.remove(it);
                } else {
                        ++it;
                }
        }
        // Pass 2: drop next-to-expire entries until under cap.
        while (_entries.size() > _cap) {
                auto victim = _entries.begin();
                for (auto cur = _entries.begin(); cur != _entries.end(); ++cur) {
                        if (cur->second.expiresAt < victim->second.expiresAt) victim = cur;
                }
                _entries.remove(victim);
        }
}

PROMEKI_NAMESPACE_END
