/**
 * @file      dnspacket.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_NETWORK
#include <cstdint>
#include <promeki/namespace.h>
#include <promeki/buffer.h>
#include <promeki/dnsrecord.h>
#include <promeki/error.h>
#include <promeki/list.h>
#include <promeki/result.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Wire-format DNS message — header + questions + records.
 * @ingroup network
 *
 * Models the on-the-wire DNS packet (RFC 1035 §4.1).  Used by both
 * unicast DNS (the @ref DnsResolver) and mDNS (where the alias
 * @c MdnsPacket carries the same shape, with cache-flush /
 * unicast-response semantics layered on at the parse and build
 * sites).
 *
 * The three resource-record sections (Answer, Authority, Additional)
 * are flattened into @ref records in their original wire order; the
 * originating section is preserved on each record's
 * @ref DnsRecord::section field so consumers that care about the
 * additional-section "freebies" can still tell them apart.
 *
 * Simple value class — no @c PROMEKI_SHARED_FINAL, plain copy / move,
 * not registered with the @ref Variant system: the type is an
 * intermediate parser / builder product, not a payload the engine
 * pushes through signals.
 *
 * @par Parsing
 * @code
 * Buffer raw = ...;
 * auto [pkt, err] = DnsPacket::parse(raw);
 * if (err.isError()) return err;
 * for (const DnsRecord &rec : pkt.records()) {
 *     if (rec.type == DnsRecord::Type::A) { ... }
 * }
 * @endcode
 *
 * @par Building
 * @code
 * DnsPacket::Builder b;
 * b.setTransactionId(0x1234);
 * b.setRecursionDesired(true);
 * b.addQuestion("example.com.", DnsRecord::Type::A);
 * Buffer pkt = b.finish();
 * @endcode
 */
class DnsPacket {
        public:
                /** @brief Wire-format header size (RFC 1035 §4.1.1). */
                static constexpr int HeaderSize = 12;

                /** @brief Builds DNS message bytes from the inside out. */
                class Builder;

                DnsPacket() = default;

                /**
                 * @brief Parses a raw DNS message in unicast-DNS mode.
                 *
                 * Treats the high bits of the class field as plain
                 * class bits per RFC 1035.  Returns
                 * @ref Error::ParseFailed for truncated or malformed
                 * messages (under-length header, names that walk off
                 * the end of the buffer, record-length fields that
                 * exceed what is left).  Unknown record types
                 * preserve @c name + @c ttl + raw rdata bytes
                 * (@ref DnsRecord::rawRdata) so forward-compatibility
                 * does not depend on the library's enum coverage.
                 */
                static Result<DnsPacket> parse(const Buffer &b);

                /** @brief Decode overload for raw byte ranges. */
                static Result<DnsPacket> parse(const uint8_t *data, size_t len);

                /**
                 * @brief Parses a raw DNS message in mDNS mode.
                 *
                 * Same as @ref parse but interprets the top bit of
                 * the class field per RFC 6762:
                 *  - On questions, the bit is the @b QU
                 *    unicast-response-preferred flag (RFC 6762 §5.4)
                 *    → @ref DnsQuestion::unicastResponse.
                 *  - On records, the bit is the @b cache-flush flag
                 *    (RFC 6762 §10.2) → @ref DnsRecord::cacheFlush.
                 *    Always masked off for @c PTR records — the
                 *    protocol disallows cache-flush there.
                 *
                 * The class field stored on the record / question is
                 * the 15-bit residue.
                 */
                static Result<DnsPacket> parseMdns(const Buffer &b);
                /** @copydoc parseMdns(const Buffer&) */
                static Result<DnsPacket> parseMdns(const uint8_t *data, size_t len);

                /** @brief Transaction ID from the packet header. */
                uint16_t transactionId() const { return _id; }

                /** @brief Sets the transaction ID (used by builders / tests). */
                void setTransactionId(uint16_t id) { _id = id; }

                /** @brief Raw flags field of the packet header. */
                uint16_t flags() const { return _flags; }

                /** @brief Sets the flags field (used by builders / tests). */
                void setFlags(uint16_t f) { _flags = f; }

                /** @brief @c true when bit 15 of @ref flags is set (QR=1). */
                bool isResponse() const { return (_flags & 0x8000) != 0; }

                /** @brief @c true when AA (Authoritative Answer) is set. */
                bool isAuthoritative() const { return (_flags & 0x0400) != 0; }

                /** @brief @c true when TC (Truncated) is set. */
                bool isTruncated() const { return (_flags & 0x0200) != 0; }

                /** @brief @c true when RD (Recursion Desired) is set. */
                bool isRecursionDesired() const { return (_flags & 0x0100) != 0; }

                /** @brief @c true when RA (Recursion Available) is set. */
                bool isRecursionAvailable() const { return (_flags & 0x0080) != 0; }

                /** @brief Extracted 4-bit RCODE (response code) field. */
                uint8_t rcode() const { return static_cast<uint8_t>(_flags & 0x000F); }

                /** @brief Extracted 4-bit OPCODE field. */
                uint8_t opcode() const { return static_cast<uint8_t>((_flags >> 11) & 0x0F); }

                /** @brief All question entries in wire order. */
                const List<DnsQuestion> &questions() const { return _questions; }
                /** @brief Mutable accessor (used by builders / tests). */
                List<DnsQuestion> &questions() { return _questions; }

                /** @brief All records from Answer + Authority + Additional, in wire order. */
                const List<DnsRecord> &records() const { return _records; }
                /** @brief Mutable accessor (used by builders / tests). */
                List<DnsRecord> &records() { return _records; }

                /** @brief Records filtered to the given section. */
                List<DnsRecord> recordsInSection(DnsRecord::Section section) const;

        private:
                uint16_t          _id    = 0;
                uint16_t          _flags = 0;
                List<DnsQuestion> _questions;
                List<DnsRecord>   _records;
};

/**
 * @brief Incremental builder for outbound DNS messages.
 * @ingroup network
 *
 * Constructs the wire bytes of a DNS query (and, when needed, a
 * minimal response).  Each @c add* call appends to the current
 * section; the per-section RR counts in the header are recomputed at
 * @ref finish.  Domain-name compression is applied automatically via
 * a per-builder dictionary so repeated suffixes (the common case for
 * unicast DNS responses) emit as 2-byte pointers.
 *
 * @par Compression scope
 * The dictionary spans the entire packet — including the question
 * section — so a single fully-qualified query name is encoded once
 * and referenced from any subsequent record whose target is the same
 * name.  Compression can be globally disabled via
 * @ref setCompressionEnabled when wire-form determinism matters
 * (test fixtures, signed-zone work).
 */
class DnsPacket::Builder {
        public:
                Builder();

                /** @brief Sets the header transaction ID (default @c 0). */
                Builder &setTransactionId(uint16_t id);

                /** @brief Sets the QR (response) flag. */
                Builder &setResponse(bool r);

                /** @brief Sets the AA (authoritative-answer) flag. */
                Builder &setAuthoritative(bool aa);

                /** @brief Sets the TC (truncated) flag. */
                Builder &setTruncated(bool tc);

                /** @brief Sets the RD (recursion-desired) flag. */
                Builder &setRecursionDesired(bool rd);

                /** @brief Sets the RA (recursion-available) flag. */
                Builder &setRecursionAvailable(bool ra);

                /** @brief Sets the OPCODE field (4 bits, default @c 0 = Query). */
                Builder &setOpcode(uint8_t opcode);

                /** @brief Sets the RCODE field (4 bits, default @c 0 = NoError). */
                Builder &setRcode(uint8_t rcode);

                /** @brief Enables / disables domain-name compression (default: enabled). */
                Builder &setCompressionEnabled(bool enable);

                /**
                 * @brief Appends a question to the Question section.
                 *
                 * @param name           Owner name (text form).
                 * @param type           RFC 1035 record-type code.
                 * @param klass          RFC 1035 class code (default @c 1 = IN).
                 * @param unicastResponse @c true to set the mDNS QU
                 *                       bit on the wire class field.
                 */
                Builder &addQuestion(const String &name, DnsRecord::Type type,
                                     uint16_t klass = 1, bool unicastResponse = false);

                /** @copydoc addQuestion(const String&, DnsRecord::Type, uint16_t, bool) */
                Builder &addQuestion(const DnsQuestion &q);

                /**
                 * @brief Appends a record to the Answer section.
                 *
                 * For mDNS the @ref DnsRecord::cacheFlush bit is
                 * honoured on every type @e except @c PTR (the
                 * protocol disallows cache-flush there and the
                 * encoder masks the bit back per RFC 6762 §10.2).
                 */
                Builder &addAnswer(const DnsRecord &r);

                /** @brief Appends a record to the Authority section. */
                Builder &addAuthority(const DnsRecord &r);

                /** @brief Appends a record to the Additional section. */
                Builder &addAdditional(const DnsRecord &r);

                /**
                 * @brief Adds an EDNS0 @c OPT pseudo-record to the
                 *        Additional section (RFC 6891).
                 *
                 * Carries the client's UDP payload size — most
                 * resolvers default to 1232 bytes (the DNS Flag Day
                 * 2020 recommendation, which fits in a 1500-byte
                 * MTU minus IPv6 + UDP overhead).
                 *
                 * @param udpPayloadSize Maximum UDP payload the
                 *                       sender will accept.
                 * @param doBit          When @c true, sets the DO
                 *                       (DNSSEC OK) bit.  This
                 *                       library does not validate
                 *                       DNSSEC; setting the bit just
                 *                       asks upstream resolvers to
                 *                       include signatures in the
                 *                       response.
                 */
                Builder &addEdns0(uint16_t udpPayloadSize = 1232, bool doBit = false);

                /**
                 * @brief Finalises the packet and returns its bytes.
                 *
                 * Calling @c finish more than once is allowed; each
                 * call re-runs the encoder against the current
                 * state.  Returns an empty Buffer (logical size 0)
                 * on encoder failure.
                 */
                Buffer finish() const;

                /** @brief Returns @c true when an encode failed under @ref finish. */
                bool lastEncodeFailed() const { return _lastEncodeFailed; }

        private:
                uint16_t        _id           = 0;
                uint16_t        _flags        = 0;
                bool            _compress     = true;
                mutable bool    _lastEncodeFailed = false;
                List<DnsQuestion> _questions;
                List<DnsRecord> _answers;
                List<DnsRecord> _authority;
                List<DnsRecord> _additional;
};

#if PROMEKI_ENABLE_MDNS
/**
 * @brief Drop-in alias of the legacy mDNS-only packet type.
 *
 * @c MdnsPacket existed before the generalised @ref DnsPacket; the
 * type is now an alias so existing call sites that name
 * @c MdnsPacket::parse / @c MdnsPacket::HeaderSize / etc. keep
 * working without source-level churn.  mDNS-specific @c parseMdns
 * semantics (cache-flush + QU bits) are reached through
 * @ref DnsPacket::parseMdns.
 */
using MdnsPacket = DnsPacket;
#endif

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NETWORK
