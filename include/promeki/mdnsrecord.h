/**
 * @file      mdnsrecord.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_MDNS
#include <cstdint>
#include <promeki/namespace.h>
#include <promeki/buffer.h>
#include <promeki/duration.h>
#include <promeki/error.h>
#include <promeki/ipv4address.h>
#include <promeki/ipv6address.h>
#include <promeki/list.h>
#include <promeki/mdnstxtrecord.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief A single mDNS resource record ready to be put on the wire.
 * @ingroup network
 *
 * @c MdnsRecord is the publish-side counterpart to
 * @ref MdnsParsedRecord — it carries everything the encoder needs to
 * emit one resource record into a packet's Answer / Authority /
 * Additional section.  Construct via the named static factories
 * (@ref ptr, @ref srv, @ref txt, @ref a, @ref aaaa); the cache-flush
 * bit is set to @c true by default for everything except @c PTR (RFC
 * 6762 §10.2 disallows cache-flush on @c PTR).
 *
 * Simple value type — copies are trivial, no shared state.  The
 * record is type-tagged so any one instance carries only its
 * type-specific payload; accessing the wrong field on the wrong type
 * is benign but returns default-constructed data.
 *
 * @par Thread Safety
 * Distinct instances may be used concurrently.  A single instance is
 * conditionally thread-safe: const operations may be called from
 * multiple threads, but any mutation must be externally synchronized.
 *
 * @par Example — building the record set for a service announce
 * @code
 * MdnsServiceType type("http", MdnsServiceType::Protocol::Tcp);
 * String instance = "Studio Camera";
 * String fqdn = instance + "." + type.toFqdn();
 * Duration ttl = Duration::fromMinutes(75);
 *
 * List<MdnsRecord> records;
 * records += MdnsRecord::ptr(type.toFqdn(), fqdn, ttl);
 * records += MdnsRecord::srv(fqdn, "host.local.", 8080, 0, 0, ttl);
 * records += MdnsRecord::txt(fqdn, txt, ttl);
 * records += MdnsRecord::a("host.local.", Ipv4Address(192, 168, 1, 7), ttl);
 * @endcode
 */
class MdnsRecord {
        public:
                /** @brief Record type the wire form will carry. */
                enum class Type : uint16_t {
                        Unknown = 0,
                        A       = 1,
                        Ptr     = 12,
                        Txt     = 16,
                        Aaaa    = 28,
                        Srv     = 33,
                };

                /** @brief Default TTL when none is specified (RFC 6762 §10 — 75 minutes). */
                static constexpr int64_t DefaultTtlSeconds = 75 * 60;

                MdnsRecord() = default;

                /**
                 * @brief Constructs a @c PTR record.
                 *
                 * @param owner   FQDN whose @c PTR list this record
                 *                contributes to (e.g. @c "_http._tcp.local.").
                 * @param target  FQDN the @c PTR points at (e.g.
                 *                @c "Studio Camera._http._tcp.local.").
                 * @param ttl     Record TTL.  Use @ref Duration::zero
                 *                to emit a @b Goodbye PTR per RFC 6762
                 *                §10.1.
                 */
                static MdnsRecord ptr(const String &owner, const String &target,
                                      const Duration &ttl = Duration::fromSeconds(DefaultTtlSeconds));

                /**
                 * @brief Constructs an @c SRV record.
                 *
                 * Cache-flush bit defaults to @c true (@c SRV is a
                 * unique-by-name record per RFC 2782 / 6763 §6).
                 *
                 * @param owner    FQDN that names the service instance
                 *                 (e.g. @c "Studio Camera._http._tcp.local.").
                 * @param target   The host FQDN the service runs on.
                 * @param port     TCP / UDP port.
                 * @param priority @c SRV priority field.  Lower is preferred.
                 * @param weight   @c SRV weight field used for tiebreak.
                 * @param ttl      Record TTL.
                 */
                static MdnsRecord srv(const String &owner, const String &target, uint16_t port,
                                      uint16_t priority = 0, uint16_t weight = 0,
                                      const Duration &ttl = Duration::fromSeconds(DefaultTtlSeconds));

                /**
                 * @brief Constructs a @c TXT record.
                 *
                 * @param owner FQDN of the service instance the TXT
                 *              attributes belong to.
                 * @param txt   The attribute collection.
                 * @param ttl   Record TTL.
                 */
                static MdnsRecord txt(const String &owner, const MdnsTxtRecord &txt,
                                      const Duration &ttl = Duration::fromSeconds(DefaultTtlSeconds));

                /**
                 * @brief Constructs an @c A record.
                 *
                 * @param owner   Host FQDN the address belongs to.
                 * @param address The IPv4 address.
                 * @param ttl     Record TTL.
                 */
                static MdnsRecord a(const String &owner, const Ipv4Address &address,
                                    const Duration &ttl = Duration::fromSeconds(DefaultTtlSeconds));

                /**
                 * @brief Constructs an @c AAAA record.
                 *
                 * @param owner   Host FQDN the address belongs to.
                 * @param address The IPv6 address.
                 * @param ttl     Record TTL.
                 */
                static MdnsRecord aaaa(const String &owner, const Ipv6Address &address,
                                       const Duration &ttl = Duration::fromSeconds(DefaultTtlSeconds));

                /** @brief Returns the record type. */
                Type type() const { return _type; }

                /** @brief Returns the owner name. */
                const String &name() const { return _name; }

                /** @brief Returns the record TTL. */
                const Duration &ttl() const { return _ttl; }

                /**
                 * @brief Returns @c true when the cache-flush bit will
                 *        be set on the wire class field (RFC 6762 §10.2).
                 */
                bool cacheFlush() const { return _cacheFlush; }

                /**
                 * @brief Sets the cache-flush bit.  No-op for @c PTR
                 *        records — RFC 6762 §10.2 disallows cache-flush
                 *        on @c PTR and the encoder masks the bit off
                 *        regardless of this setting.
                 */
                void setCacheFlush(bool enable) { _cacheFlush = enable; }

                /** @brief @c PTR rdata: target name. */
                const String &ptrTarget() const { return _ptrTarget; }

                /** @brief @c SRV rdata: priority. */
                uint16_t srvPriority() const { return _srvPriority; }
                /** @brief @c SRV rdata: weight. */
                uint16_t srvWeight()   const { return _srvWeight; }
                /** @brief @c SRV rdata: port. */
                uint16_t srvPort()     const { return _srvPort; }
                /** @brief @c SRV rdata: target host. */
                const String &srvTarget() const { return _srvTarget; }

                /** @brief @c TXT rdata. */
                const MdnsTxtRecord &txtRecord() const { return _txt; }

                /** @brief @c A rdata. */
                const Ipv4Address &aAddress() const { return _a; }

                /** @brief @c AAAA rdata. */
                const Ipv6Address &aaaaAddress() const { return _aaaa; }

                /** @brief Returns @c true when @ref ttl is exactly zero (Goodbye record). */
                bool isGoodbye() const { return _ttl == Duration::zero(); }

                /** @brief Equality across every field. */
                bool operator==(const MdnsRecord &other) const;
                bool operator!=(const MdnsRecord &other) const { return !(*this == other); }

        private:
                Type           _type       = Type::Unknown;
                String         _name;
                Duration       _ttl;
                bool           _cacheFlush = true;

                String         _ptrTarget;
                uint16_t       _srvPriority = 0;
                uint16_t       _srvWeight   = 0;
                uint16_t       _srvPort     = 0;
                String         _srvTarget;
                MdnsTxtRecord  _txt;
                Ipv4Address    _a;
                Ipv6Address    _aaaa;
};

/**
 * @brief Builds an mDNS response packet containing the given answers.
 *
 * Header is flagged QR=1 + AA=1 (authoritative response).  The
 * Question section is empty; every record is placed in the Answer
 * section in the order given.  Cache-flush bits per
 * @ref MdnsRecord::cacheFlush are honoured on every type @e except
 * @c PTR, which the encoder masks back per RFC 6762 §10.2.
 *
 * @param records Records to emit.  Empty produces a valid 12-byte
 *                header-only response.
 * @param transactionId Optional transaction ID; defaults to zero per
 *                      RFC 6762 §18.1.
 */
Buffer mdnsBuildAnnounce(const List<MdnsRecord> &records, uint16_t transactionId = 0);

/**
 * @brief Builds an mDNS Goodbye packet (RFC 6762 §10.1).
 *
 * Convenience wrapper around @ref mdnsBuildAnnounce that clones each
 * @p record with @c TTL=0 before emitting.  The cache-flush bit
 * carries through unchanged — receivers MUST flush prior records on
 * a TTL=0 cache-flush record per §10.2.
 */
Buffer mdnsBuildGoodbye(const List<MdnsRecord> &records, uint16_t transactionId = 0);

/**
 * @brief Builds an mDNS probe query for the given record set.
 *
 * RFC 6762 §8.1: the probe is a query for the names the prospective
 * publisher is about to claim.  The Question section carries one
 * @c ANY (@c QTYPE=255) question per distinct owner name; the
 * Authority section carries the tentative records the publisher
 * intends to own, so other publishers can lexicographic-compare and
 * back off if they outrank.
 *
 * Probe queries set the QU (unicast-response-preferred) bit on the
 * question class so existing owners can reply directly even before
 * their multicast cache catches up.
 */
Buffer mdnsBuildProbe(const List<MdnsRecord> &records, uint16_t transactionId = 0);

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_MDNS
