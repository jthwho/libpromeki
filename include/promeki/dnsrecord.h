/**
 * @file      dnsrecord.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_NETWORK
#include <cstdint>
#include <promeki/namespace.h>
#include <promeki/duration.h>
#include <promeki/ipv4address.h>
#include <promeki/ipv6address.h>
#include <promeki/list.h>
#include <promeki/string.h>
#if PROMEKI_ENABLE_MDNS
#include <promeki/mdnstxtrecord.h>
#endif

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief One question entry parsed from (or to be emitted into) a DNS packet.
 * @ingroup network
 *
 * Captures the (name, type, class) triple from the wire's Question
 * section.  @ref unicastResponse is mDNS-specific (RFC 6762 §5.4 QU
 * bit, the top bit of the on-the-wire class field); unicast DNS
 * leaves it @c false.
 *
 * Simple value type — no @c PROMEKI_SHARED_FINAL, plain copy / move,
 * not registered with the @ref Variant system.  Carried inside
 * @ref DnsPacket and constructed by the resolver when building
 * queries.
 */
struct DnsQuestion {
        /** @brief Owner name (e.g. @c "_http._tcp.local." or @c "example.com."). */
        String   name;
        /** @brief RFC 1035 record-type code (1 = A, 12 = PTR, 16 = TXT, 28 = AAAA, 33 = SRV, ...). */
        uint16_t type             = 0;
        /** @brief RFC 1035 record-class code (1 = IN is universal; the high bit is the mDNS QU flag). */
        uint16_t klass            = 1;
        /** @brief mDNS only: top bit of the class field on the wire (RFC 6762 §5.4). */
        bool     unicastResponse  = false;
};

/**
 * @brief One resource record (header + type-specific payload) decoded
 *        from a DNS packet or staged for one.
 * @ingroup network
 *
 * Carries the common header fields (name, type, class, TTL, section)
 * plus the rdata for every record type the library understands at
 * field level.  Unknown record types still parse — their @ref rawRdata
 * member holds the raw wire bytes so handlers can forward them
 * verbatim or decode by hand.
 *
 * For backwards-compat with the historical mDNS-only @c MdnsParsedRecord
 * shape:
 *  - The same nested @ref Type values (1/12/16/28/33) and @ref Section
 *    values (0..3) are kept on this class.  Existing mDNS callers that
 *    compare @c rec.type @c == @c MdnsParsedRecord::Type::Ptr continue
 *    to compile and behave identically because @c MdnsParsedRecord is
 *    now a @c using-alias of @c DnsRecord.
 *  - The same public field names (@c name, @c ttl, @c cacheFlush,
 *    @c ptrTarget, @c srvPriority / @c srvWeight / @c srvPort /
 *    @c srvTarget, @c txt, @c a, @c aaaa) are preserved.
 *  - Additional rdata fields are added for the unicast-DNS-only types
 *    (CNAME / NS / MX / SOA / NAPTR / CAA) plus a @ref rawRdata fallback.
 *
 * Simple value type — no @c PROMEKI_SHARED_FINAL.  Cheap to copy via
 * @ref List + @ref String CoW; passed by value through the resolver
 * pipeline.
 *
 * @par mDNS extensions
 *  - @ref cacheFlush carries the top bit of the wire class field
 *    (RFC 6762 §10.2).  Always @c false on @c PTR records (the
 *    protocol disallows cache-flush there) and always @c false in
 *    unicast DNS responses (the bit is reserved).
 *  - @ref txt is only populated when @c PROMEKI_ENABLE_MDNS is on;
 *    unicast TXT consumers must decode @ref rawRdata directly.  Code
 *    that touches the field must gate on the same macro.
 */
class DnsRecord {
        public:
                /**
                 * @brief Wire-format record type code.
                 *
                 * Numeric values match the on-the-wire 16-bit field
                 * exactly; the historical mDNS subset (A/PTR/TXT/AAAA/
                 * SRV) keeps its prior identities.  Codes outside this
                 * enum's named entries land in @ref type as the raw
                 * @c uint16_t value (cast to @c Type); switch
                 * statements should always include a @c default arm.
                 */
                enum class Type : uint16_t {
                        Unknown = 0,
                        A       = 1,
                        Ns      = 2,
                        Cname   = 5,
                        Soa     = 6,
                        Ptr     = 12,
                        Mx      = 15,
                        Txt     = 16,
                        Aaaa    = 28,
                        Srv     = 33,
                        Naptr   = 35,
                        Opt     = 41,
                        Ds      = 43,
                        Rrsig   = 46,
                        Nsec    = 47,
                        Dnskey  = 48,
                        Tlsa    = 52,
                        Svcb    = 64,
                        Https   = 65,
                        Axfr    = 252,
                        Any     = 255,
                        Caa     = 257,
                };

                /** @brief Which section of the packet held this record. */
                enum class Section : uint8_t {
                        Question   = 0,
                        Answer     = 1,
                        Authority  = 2,
                        Additional = 3,
                };

                /** @brief Default TTL (75 minutes) when none is specified — matches RFC 6762 §10's mDNS-typical TTL. */
                static constexpr int64_t DefaultTtlSeconds = 75 * 60;

                // ===== Common header fields =====================================
                /** @brief Record-type code (wire form). */
                Type     type        = Type::Unknown;
                /** @brief Packet section the record came from. */
                Section  section     = Section::Answer;
                /** @brief Record-class code (wire form, top bit masked off for mDNS). */
                uint16_t klass       = 1;   ///< @c 1 == @c IN
                /** @brief Owner name (e.g. @c "example.com." or @c "Studio Cam._http._tcp.local."). */
                String   name;
                /** @brief Record TTL.  Zero TTL means "Goodbye" per RFC 6762 §10.1. */
                Duration ttl;
                /**
                 * @brief mDNS only: top bit of the wire class field
                 *        (RFC 6762 §10.2 cache-flush).
                 *
                 * Always @c false on @c PTR records — the protocol
                 * disallows cache-flush there — and always @c false
                 * in unicast DNS responses (the bit is reserved).
                 */
                bool     cacheFlush  = false;

                // ===== Type-specific payload ====================================
                /** @brief @c PTR / @c CNAME / @c NS rdata: target name (the historical @c ptrTarget). */
                String        ptrTarget;
                /** @brief @c CNAME rdata: canonical name target. */
                String        cnameTarget;
                /** @brief @c NS rdata: name-server target. */
                String        nsTarget;
                /** @brief @c SRV rdata: priority (RFC 2782). */
                uint16_t      srvPriority = 0;
                /** @brief @c SRV rdata: weight (RFC 2782). */
                uint16_t      srvWeight   = 0;
                /** @brief @c SRV rdata: port (RFC 2782). */
                uint16_t      srvPort     = 0;
                /** @brief @c SRV rdata: target host (RFC 2782). */
                String        srvTarget;
                /** @brief @c MX rdata: preference (lower is preferred). */
                uint16_t      mxPreference = 0;
                /** @brief @c MX rdata: mail-exchange host name. */
                String        mxExchange;
#if PROMEKI_ENABLE_MDNS
                /**
                 * @brief @c TXT rdata as DNS-SD key/value pairs.
                 *
                 * Only built into the library when
                 * @c PROMEKI_ENABLE_MDNS is on — the parser falls
                 * back to leaving @ref rawRdata populated for the
                 * raw wire bytes when mDNS is disabled.  Consumers
                 * that need TXT decoding without the rest of the
                 * mDNS engine should write a small helper that
                 * walks the length-prefixed-string form themselves.
                 */
                MdnsTxtRecord txt;
#endif
                /** @brief @c A rdata: IPv4 address (RFC 1035). */
                Ipv4Address   a;
                /** @brief @c AAAA rdata: IPv6 address (RFC 3596). */
                Ipv6Address   aaaa;

                // ===== SOA fields (RFC 1035) ====================================
                /** @brief @c SOA rdata: primary name server. */
                String   soaMname;
                /** @brief @c SOA rdata: responsible-party mailbox in DNS-encoded form. */
                String   soaRname;
                /** @brief @c SOA rdata: zone serial number. */
                uint32_t soaSerial  = 0;
                /** @brief @c SOA rdata: refresh interval in seconds. */
                uint32_t soaRefresh = 0;
                /** @brief @c SOA rdata: retry interval in seconds. */
                uint32_t soaRetry   = 0;
                /** @brief @c SOA rdata: expire interval in seconds. */
                uint32_t soaExpire  = 0;
                /** @brief @c SOA rdata: minimum (negative-cache TTL) in seconds (RFC 2308). */
                uint32_t soaMinimum = 0;

                // ===== NAPTR fields (RFC 3403) ==================================
                /** @brief @c NAPTR rdata: order field. */
                uint16_t naptrOrder      = 0;
                /** @brief @c NAPTR rdata: preference field. */
                uint16_t naptrPreference = 0;
                /** @brief @c NAPTR rdata: flags (character-string). */
                String   naptrFlags;
                /** @brief @c NAPTR rdata: services (character-string). */
                String   naptrService;
                /** @brief @c NAPTR rdata: regexp (character-string). */
                String   naptrRegexp;
                /** @brief @c NAPTR rdata: replacement (DNS-encoded name). */
                String   naptrReplacement;

                // ===== CAA fields (RFC 8659) ====================================
                /** @brief @c CAA rdata: flags byte. */
                uint8_t  caaFlags = 0;
                /** @brief @c CAA rdata: property tag (e.g. @c "issue"). */
                String   caaTag;
                /** @brief @c CAA rdata: property value (free-form). */
                String   caaValue;

                // ===== Catch-all =================================================
                /**
                 * @brief Raw rdata bytes — always populated for types
                 *        the library does not field-decode.
                 *
                 * Held as a @ref List<uint8_t> so consumers can walk
                 * the bytes without having to know the original
                 * packet's layout.  Field-decoded types may leave this
                 * empty.
                 */
                List<uint8_t> rawRdata;

                // ===== Convenience helpers ======================================
                /** @brief @c true when @ref ttl is exactly zero (mDNS Goodbye record). */
                bool isGoodbye() const { return ttl == Duration::zero(); }

                /** @brief Construct an @c A record. */
                static DnsRecord makeA(const String &owner, const Ipv4Address &addr,
                                       const Duration &ttl = Duration::fromSeconds(DefaultTtlSeconds));

                /** @brief Construct an @c AAAA record. */
                static DnsRecord makeAaaa(const String &owner, const Ipv6Address &addr,
                                          const Duration &ttl = Duration::fromSeconds(DefaultTtlSeconds));

                /** @brief Construct a @c PTR record (the historical mDNS-style entry point). */
                static DnsRecord makePtr(const String &owner, const String &target,
                                         const Duration &ttl = Duration::fromSeconds(DefaultTtlSeconds));

                /** @brief Construct an @c SRV record. */
                static DnsRecord makeSrv(const String &owner, const String &target, uint16_t port,
                                         uint16_t priority = 0, uint16_t weight = 0,
                                         const Duration &ttl = Duration::fromSeconds(DefaultTtlSeconds));

                /** @brief Construct a @c CNAME record. */
                static DnsRecord makeCname(const String &owner, const String &target,
                                           const Duration &ttl = Duration::fromSeconds(DefaultTtlSeconds));

                /** @brief Construct an @c NS record. */
                static DnsRecord makeNs(const String &owner, const String &target,
                                        const Duration &ttl = Duration::fromSeconds(DefaultTtlSeconds));

                /** @brief Construct an @c MX record. */
                static DnsRecord makeMx(const String &owner, const String &exchange, uint16_t preference,
                                        const Duration &ttl = Duration::fromSeconds(DefaultTtlSeconds));
};

#if PROMEKI_ENABLE_MDNS
/**
 * @brief Drop-in alias of the legacy mDNS-only parsed-record type.
 *
 * @c MdnsParsedRecord existed before the generalised @ref DnsRecord;
 * the type is now an alias so existing call sites that name it (and
 * its nested @c Type / @c Section enums) keep compiling.
 */
using MdnsParsedRecord = DnsRecord;

/**
 * @brief Drop-in alias of the legacy mDNS-only parsed-question type.
 *
 * Mirrors the @ref MdnsParsedRecord alias.
 */
using MdnsParsedQuestion = DnsQuestion;
#endif

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NETWORK
