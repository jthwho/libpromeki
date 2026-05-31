/**
 * @file      dnscache.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_NETWORK
#include <cstdint>
#include <promeki/namespace.h>
#include <promeki/dnsrecord.h>
#include <promeki/duration.h>
#include <promeki/enums_dns.h>
#include <promeki/list.h>
#include <promeki/map.h>
#include <promeki/mutex.h>
#include <promeki/string.h>
#include <promeki/timestamp.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief TTL-honoring stub-resolver cache.
 * @ingroup network
 *
 * Stores the answer-section RR set returned by an upstream resolver
 * (or a negative-cache marker for NXDOMAIN replies) keyed on
 * (canonical name, record type, record class).
 *
 *  - @b Positive entries (rcode == @c NoError, at least one record):
 *    cached for the minimum TTL across the answer records, clamped
 *    by @ref setMaxTtl.  Looked up by exact match; partial-overlap
 *    misses are not promoted.
 *  - @b Negative entries (rcode == @c NxDomain, or rcode ==
 *    @c NoError with an empty answer section): cached for the SOA
 *    @c minimum field in the response's Authority section, clamped
 *    by @ref setMaxNegativeTtl, per RFC 2308 §4.  Re-asked queries
 *    return @c NxDomain (or empty success) without going to the wire.
 *
 * Thread-safe — every operation takes the same per-instance @ref Mutex
 * so the cache can be shared across the resolver's worker thread and
 * any thread calling the sync wrappers.  Cache misses are common and
 * cheap so contention is not a concern.
 *
 * Simple value class.  No @c PROMEKI_SHARED_FINAL; the resolver owns
 * the singleton instance behind @ref DnsResolver::cache.
 */
class DnsCache {
        public:
                /** @brief Default positive-cache cap (1 hour). */
                static constexpr int64_t DefaultMaxTtlSeconds = 3600;

                /** @brief Default negative-cache cap (5 min — RFC 2308 §5 recommendation). */
                static constexpr int64_t DefaultMaxNegativeTtlSeconds = 300;

                /** @brief Default upper bound on the number of entries kept. */
                static constexpr size_t DefaultCapacity = 4096;

                DnsCache();

                /** @brief Sets the upper bound on positive-entry TTL. */
                void setMaxTtl(const Duration &d);
                /** @brief Returns the current positive-entry TTL cap. */
                Duration maxTtl() const;

                /** @brief Sets the upper bound on negative-entry TTL. */
                void setMaxNegativeTtl(const Duration &d);
                /** @brief Returns the current negative-entry TTL cap. */
                Duration maxNegativeTtl() const;

                /** @brief Sets the upper bound on the number of cached entries.  @c 0 disables the cap. */
                void setCapacity(size_t cap);

                /** @brief Returns the current entry-count cap. */
                size_t capacity() const;

                /** @brief Returns the current entry count. */
                size_t size() const;

                /** @brief Removes every entry. */
                void clear();

                /** @brief Result of @ref get. */
                struct Hit {
                        /** @brief @c true when the entry was found and is fresh. */
                        bool          found    = false;
                        /** @brief @c true when the entry is a negative-cache marker. */
                        bool          negative = false;
                        /** @brief Cached rcode (relevant for negatives). */
                        DnsRcode      rcode;
                        /** @brief Cached records (empty for negatives). */
                        List<DnsRecord> records;
                };

                /**
                 * @brief Looks up an entry for (name, type, klass).
                 *
                 * Name lookups are case-insensitive (canonicalised
                 * via @ref dnsCanonicalName before matching).
                 * Expired entries are evicted lazily as part of this
                 * call and reported as @c found=false.
                 */
                Hit get(const String &name, uint16_t type, uint16_t klass) const;

                /**
                 * @brief Stores a positive-cache entry.
                 *
                 * @param name    Owner name (case-insensitive).
                 * @param type    Record type.
                 * @param klass   Record class.
                 * @param records Records returned by the upstream
                 *                resolver — the answer RRset only;
                 *                authority + additional records
                 *                should not be inserted here.
                 *                Empty @p records is treated as a
                 *                negative entry with the SOA-minimum
                 *                TTL passed in @p ttlCap or, if
                 *                missing, the cache's
                 *                @ref maxNegativeTtl.
                 * @param ttlCap  Optional explicit cap on the entry's
                 *                TTL (e.g. the SOA @c minimum field
                 *                for an empty-answer negative).
                 *                @ref Duration::zero leaves the cap
                 *                to the cache's own policy.
                 */
                void put(const String &name, uint16_t type, uint16_t klass,
                         const List<DnsRecord> &records,
                         const Duration &ttlCap = Duration::zero());

                /**
                 * @brief Stores a negative-cache entry for an explicit @c NxDomain rcode.
                 *
                 * @param name    Owner name.
                 * @param type    Record type the query asked for.
                 * @param klass   Record class.
                 * @param rcode   Response code to surface on hit
                 *                (typically @ref DnsRcode::NxDomain).
                 * @param ttlCap  Optional cap on the negative TTL
                 *                (typically the SOA minimum field
                 *                from the upstream response's
                 *                Authority section).
                 */
                void putNegative(const String &name, uint16_t type, uint16_t klass,
                                 const DnsRcode &rcode,
                                 const Duration &ttlCap = Duration::zero());

        private:
                struct Entry {
                        TimeStamp       expiresAt;
                        bool            negative = false;
                        DnsRcode        rcode    = DnsRcode::NoError;
                        List<DnsRecord> records;
                };

                struct Key {
                        String   name;
                        uint16_t type  = 0;
                        uint16_t klass = 0;
                        bool operator<(const Key &o) const {
                                if (name  < o.name)  return true;
                                if (o.name  < name)  return false;
                                if (type  < o.type)  return true;
                                if (type  > o.type)  return false;
                                return klass < o.klass;
                        }
                };

                void evictIfOversized();

                mutable Mutex     _mtx;
                Map<Key, Entry>   _entries;
                Duration          _maxTtl;
                Duration          _maxNegTtl;
                size_t            _cap = DefaultCapacity;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NETWORK
