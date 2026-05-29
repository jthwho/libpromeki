/**
 * @file      mdnspacket.h
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
#include <promeki/result.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief One question entry parsed from an mDNS packet.
 * @ingroup network
 *
 * The @ref unicastResponse flag carries the top bit of the on-the-wire
 * class field (the QU bit of RFC 6762 §5.4) — clients use it to ask
 * for a unicast reply when their state may not be sync'd with the
 * multicast cache.
 */
struct MdnsParsedQuestion {
        /** @brief Owner name (e.g. @c "_http._tcp.local."). */
        String   name;
        /** @brief Record type the question is asking for (1=A, 12=PTR, …). */
        uint16_t type             = 0;
        /** @brief @c true when the QU bit was set on the question's class field. */
        bool     unicastResponse  = false;
};

/**
 * @brief One resource record parsed from an mDNS packet.
 * @ingroup network
 *
 * Carries the common header fields (name, TTL, cache-flush bit, section
 * the record belonged to) plus the type-specific payload.  Only the
 * field matching @ref type is meaningful; the others stay
 * default-constructed.  Unknown record types preserve @ref name and
 * @ref ttl but contribute no payload (handlers that want to forward
 * unknown rdata bytes can read them off the source @ref Buffer using
 * the offsets reported by the parser).
 */
struct MdnsParsedRecord {
        /**
         * @brief RFC 1035 record-type code, restricted to the subset
         *        mDNS deals with.
         *
         * Numeric values match the wire form so callers can compare
         * against raw inputs without an extra mapping.
         */
        enum class Type : uint16_t {
                Unknown = 0,
                A       = 1,
                Ptr     = 12,
                Txt     = 16,
                Aaaa    = 28,
                Srv     = 33,
        };

        /** @brief Which section of the packet held this record. */
        enum class Section : uint8_t {
                Question   = 0,
                Answer     = 1,
                Authority  = 2,
                Additional = 3,
        };

        /** @brief Record-type code, mapped from the on-the-wire 16-bit field. */
        Type     type        = Type::Unknown;
        /** @brief Packet section the record came from. */
        Section  section     = Section::Answer;
        /** @brief Owner name (e.g. @c "Studio Camera._ravenna._tcp.local."). */
        String   name;
        /** @brief Record TTL.  Zero TTL means "Goodbye" per RFC 6762 §10.1. */
        Duration ttl;
        /**
         * @brief Top bit of the wire class field (RFC 6762 §10.2).
         *
         * When set, the receiver should flush every prior record with
         * the same name+type+class before accepting this one.  Always
         * @c false on @c PTR records (the protocol disallows cache-
         * flush there).
         */
        bool     cacheFlush  = false;

        // ----- Type-specific payload -------------------------------------
        /** @brief PTR rdata: target name.  Valid when @ref type == @c Ptr. */
        String        ptrTarget;
        /** @brief SRV rdata: priority. Valid when @ref type == @c Srv. */
        uint16_t      srvPriority = 0;
        /** @brief SRV rdata: weight.   Valid when @ref type == @c Srv. */
        uint16_t      srvWeight   = 0;
        /** @brief SRV rdata: port.     Valid when @ref type == @c Srv. */
        uint16_t      srvPort     = 0;
        /** @brief SRV rdata: target host. Valid when @ref type == @c Srv. */
        String        srvTarget;
        /** @brief TXT rdata.  Valid when @ref type == @c Txt. */
        MdnsTxtRecord txt;
        /** @brief A rdata.  Valid when @ref type == @c A. */
        Ipv4Address   a;
        /** @brief AAAA rdata.  Valid when @ref type == @c Aaaa. */
        Ipv6Address   aaaa;
};

/**
 * @brief Decoded mDNS packet — header + questions + records.
 * @ingroup network
 *
 * Wraps the wire-level DNS packet format restricted to the mDNS
 * subset.  All three resource-record sections (Answer, Authority,
 * Additional) are flattened into @ref records in their original wire
 * order; the originating section is preserved on each record's
 * @ref MdnsParsedRecord::section field so consumers that care about
 * the additional-section "freebies" can still tell them apart.
 *
 * Parser is driven by the vendored @c mjansson/mdns library — it
 * handles DNS-name compression pointers and the per-type record
 * decoders.  TXT rdata is post-processed through @ref MdnsTxtRecord
 * so the @ref Presence distinction (KeyOnly / Empty / Present)
 * survives the round-trip.
 *
 * Simple value class — no @c PROMEKI_SHARED_FINAL.  Cheap to copy via
 * @ref List + @ref String CoW.  Not registered with the @ref Variant
 * system: the type is an intermediate parser product, not a payload
 * the engine pushes through signals.
 *
 * @par Example
 * @code
 * Buffer raw = ...;
 * auto r = MdnsPacket::parse(raw);
 * REQUIRE(r.second().isOk());
 * for (const MdnsParsedRecord &rec : r.first().records()) {
 *     if (rec.type == MdnsParsedRecord::Type::Ptr) ...
 * }
 * @endcode
 */
class MdnsPacket {
        public:
                /** @brief Wire-format header size (RFC 1035 §4.1.1). */
                static constexpr int HeaderSize = 12;

                MdnsPacket() = default;

                /**
                 * @brief Parses a raw mDNS payload.
                 *
                 * Returns @ref Error::ParseFailed for truncated or
                 * malformed packets (under-length header, names that
                 * walk off the end of the buffer, record-length
                 * fields that exceed what is left).  Unknown record
                 * types are kept verbatim (name + TTL only) so
                 * forward-compatibility does not depend on the
                 * library's enum coverage.
                 */
                static Result<MdnsPacket> parse(const Buffer &b);

                /** @brief Decode overload for raw byte ranges. */
                static Result<MdnsPacket> parse(const uint8_t *data, size_t len);

                /** @brief Transaction ID from the packet header. */
                uint16_t transactionId() const { return _id; }

                /** @brief Raw flags field of the packet header. */
                uint16_t flags() const { return _flags; }

                /** @brief @c true when bit 15 of @ref flags is set (QR=1). */
                bool isResponse() const { return (_flags & 0x8000) != 0; }

                /** @brief @c true when AA (Authoritative Answer) is set. */
                bool isAuthoritative() const { return (_flags & 0x0400) != 0; }

                /** @brief @c true when TC (Truncated) is set. */
                bool isTruncated() const { return (_flags & 0x0200) != 0; }

                /** @brief All question entries in wire order. */
                const List<MdnsParsedQuestion> &questions() const { return _questions; }

                /** @brief All records from Answer + Authority + Additional, in wire order. */
                const List<MdnsParsedRecord> &records() const { return _records; }

        private:
                // Internal mutators used only by the static @ref parse
                // method (which forwards to a free helper in the
                // .cpp).  Friended directly so callers cannot reach
                // the storage by name.
                friend Result<MdnsPacket> mdnsParsePacket(const uint8_t *, size_t);

                uint16_t                 _id    = 0;
                uint16_t                 _flags = 0;
                List<MdnsParsedQuestion> _questions;
                List<MdnsParsedRecord>   _records;
};

/**
 * @brief Free-function entry point used by @ref MdnsPacket::parse.
 *
 * Friended into @ref MdnsPacket so the parser can fill in private
 * fields without exposing setters on the value class.  Production
 * code should call @ref MdnsPacket::parse rather than this directly.
 */
Result<MdnsPacket> mdnsParsePacket(const uint8_t *bytes, size_t len);

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_MDNS
