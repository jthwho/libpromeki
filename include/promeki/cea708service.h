/**
 * @file      cea708service.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_PROAV
#include <cstdint>
#include <promeki/buffer.h>
#include <promeki/cea708cdp.h>
#include <promeki/error.h>
#include <promeki/json.h>
#include <promeki/list.h>
#include <promeki/namespace.h>
#include <promeki/result.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

class DataStream;

/**
 * @brief One CEA-708 DTVCC service block.
 * @ingroup proav
 *
 * CEA-708 DTVCC ("Digital TV Closed Captions") carries up to 63
 * caption services in parallel, multiplexed inside DTVCC packets
 * that ride inside the CDP's @c cc_data section as @c cc_type=2
 * (DTVCC_PACKET_START) and @c cc_type=3 (DTVCC_PACKET_DATA) triples.
 *
 * A **service block** is the per-service unit inside a DTVCC packet:
 *
 *  - @c service_number — 1..63.  Most real streams carry only
 *    service 1 (the primary English caption service).  Services
 *    7..63 use an *extended* block header byte that follows the
 *    standard 1-byte header.
 *  - @c block_size — number of service-data bytes following the
 *    header (0..31 in the standard header form).  A block size of
 *    zero with service number zero terminates the DTVCC packet
 *    (null block).
 *  - @c data — the service-data bytes.  Each byte is one of
 *    C0 (0x00..0x1F: cursor / line break / clear), G0 (0x20..0x7F:
 *    printable ASCII), C1 (0x80..0x9F: window manipulation), or
 *    G1 (0xA0..0xFF: extended Latin characters).  C2/C3/G2/G3
 *    extended sets are reached via 0x10/0x11/0x12/0x13 escape
 *    bytes.
 *
 * This is the wire-level structural type.  Full DTVCC decode
 * (window manager, pen state, text reconstruction) layers on top
 * via @ref Cea708WindowState.  This class is the substrate the
 * encoder / decoder dispatch off.
 *
 * @par Storage and copy semantics
 *
 * Plain value type — no pimpl, no shared backing.  @c data is a
 * @ref Buffer (value-type handle), so copy is essentially free
 * (refcount bump).
 *
 * @par Variant / DataStream integration
 *
 * Registered as @c Variant::TypeCea708Service with tag
 * @c DataStream::TypeCea708Service (@c 0x60).
 *
 * @par Thread Safety
 *
 * Conditionally thread-safe.  Distinct instances may be used
 * concurrently; concurrent access to a single instance must be
 * externally synchronised.
 *
 * @see Cea708Cdp, Cea708DtvccPacket
 */
class Cea708Service {
        public:
                /// @brief Maximum @c service_number representable in
                ///        the wire format (6 bits).
                static constexpr uint8_t MaxServiceNumber = 63;

                /// @brief Service-number boundary above which the
                ///        extended header byte is required.  Services
                ///        1..6 fit the standard 1-byte block header;
                ///        7..63 need the extended form.
                static constexpr uint8_t ExtendedServiceNumberFrom = 7;

                /// @brief Default-constructs a null service block
                ///        (service_number=0, no data).  A null block
                ///        terminates a DTVCC packet's service-block
                ///        list per spec.
                Cea708Service() = default;

                /// @brief Constructs a service block with the given
                ///        service number and data bytes.
                Cea708Service(uint8_t serviceNumber, Buffer data)
                    : _serviceNumber(serviceNumber), _data(std::move(data)) {}

                /// @brief Service number (1..63).  Zero means
                ///        "null block" (DTVCC packet terminator).
                uint8_t serviceNumber() const { return _serviceNumber; }

                /// @brief Sets the service number.
                void setServiceNumber(uint8_t n) { _serviceNumber = n; }

                /// @brief Service-data bytes (the block payload, not
                ///        including the 1- or 2-byte header).
                const Buffer &data() const { return _data; }

                /// @brief Mutable access to the service-data bytes.
                Buffer &data() { return _data; }

                /// @brief @c true when this is the null-block
                ///        terminator (service_number == 0).
                bool isNull() const { return _serviceNumber == 0; }

                /// @brief @c true when this block uses the
                ///        2-byte extended header (service 7..63).
                bool isExtended() const { return _serviceNumber >= ExtendedServiceNumberFrom; }

                /// @brief Convenience: builds a service block for
                ///        @p serviceNumber carrying @p text as G0
                ///        printable characters.
                ///
                /// Each byte of @p text is emitted verbatim if it
                /// falls in the G0 range (0x20..0x7F).  Other bytes
                /// (including non-ASCII / control codes) are
                /// substituted with 0x20 (space).  This is the
                /// minimum viable producer for plain-text caption
                /// content — windowed / styled output is a future
                /// follow-on.
                static Cea708Service fromText(uint8_t serviceNumber, const String &text);

                /// @brief Extracts printable G0 characters from
                ///        @ref data.  Skips C0 / C1 control bytes
                ///        and any G1 / extended characters.  This is
                ///        the inverse of @ref fromText for the basic
                ///        ASCII subset.
                String text() const;

                /**
                 * @brief Produces a JSON representation for inspection.
                 *
                 * Shape: @c {serviceNumber, dataSize, dataHex}.  Used
                 * by tooling that wants a stable typed view of a
                 * service block independently of the @ref Cea708Cdp
                 * wrapping.
                 */
                JsonObject toJson() const;

                /**
                 * @brief Short human-readable summary.
                 *
                 * Reports the service number + data byte count;
                 * designed for log lines, not machine consumption.
                 */
                String toString() const;

                /// @brief Value equality — compares the service number
                ///        and the @ref data bytes (deep compare, not
                ///        @ref Buffer handle identity).  Two service
                ///        blocks with the same wire bytes are
                ///        interchangeable.
                bool operator==(const Cea708Service &o) const;
                bool operator!=(const Cea708Service &o) const { return !(*this == o); }

        private:
                uint8_t _serviceNumber = 0;
                Buffer  _data;
};

/** @brief Writes a @ref Cea708Service to a @ref DataStream. */
DataStream &operator<<(DataStream &stream, const Cea708Service &svc);

/** @brief Reads a @ref Cea708Service from a @ref DataStream. */
DataStream &operator>>(DataStream &stream, Cea708Service &svc);

/**
 * @brief One CEA-708 DTVCC packet.
 * @ingroup proav
 *
 * A DTVCC packet is the multi-frame container for one or more
 * @ref Cea708Service blocks.  Up to 128 bytes per packet:
 *
 *  - **Header byte** (1 byte): @c sequence_number (2 bits) +
 *    @c packet_size_code (6 bits).  Wire @c packet_size_code carries
 *    @c (packet_size_in_bytes - 1).  When @c packet_size_code is 0
 *    the actual packet size is 128 bytes (the maximum).
 *  - **Payload** (up to 127 bytes): a stream of service blocks.
 *    The list is terminated either by reaching @c packet_size_code +
 *    1 - 1 bytes or by a null service block (service_number=0,
 *    block_size=0).
 *
 * The packet is then split into @ref Cea708Cdp::CcData triples for
 * carriage inside a CDP:
 *
 *  - The first triple uses @c cc_type=2 (DTVCC_PACKET_START) and
 *    its @c (b1, b2) carry the header byte followed by the first
 *    payload byte (or 0xFF if the packet ends after the header).
 *  - Subsequent triples use @c cc_type=3 (DTVCC_PACKET_DATA),
 *    carrying two payload bytes each.
 *  - The total triple count is @c ceil((packet_size + 1) / 2).
 *
 * @par Spec note: @c packet_size_code semantics
 *
 * The wire @c packet_size_code field encodes "packet size in bytes
 * minus 1" except for the special value 0 which means 128.  This
 * lets the 6-bit field cover the full 1..128 byte range without
 * a separate "is empty" sentinel.  For round-trip, callers should
 * use @c payloadByteCount() to query the actual payload length.
 *
 * @par Storage and copy semantics
 *
 * Plain value type — no pimpl.  Service block list is a value-type
 * @ref List of value-type @ref Cea708Service entries.
 *
 * @par Variant / DataStream integration
 *
 * Registered as @c Variant::TypeCea708DtvccPacket with tag
 * @c DataStream::TypeCea708DtvccPacket (@c 0x61).
 *
 * @see Cea708Service, Cea708Cdp::CcData
 */
class Cea708DtvccPacket {
        public:
                /// @brief Maximum payload bytes (the spec's
                ///        @c packet_size_code = 0 wire value).
                static constexpr uint8_t MaxPayloadBytes = 127;

                /// @brief @c cc_type for the first triple of a DTVCC
                ///        packet inside a CDP's @c cc_data section.
                static constexpr uint8_t CcTypePacketStart = 2;

                /// @brief @c cc_type for continuation triples.
                static constexpr uint8_t CcTypePacketData = 3;

                /// @brief Default-constructs an empty DTVCC packet
                ///        (sequence 0, no service blocks).
                Cea708DtvccPacket() = default;

                /// @brief Constructs with explicit fields.
                Cea708DtvccPacket(uint8_t sequenceNumber, List<Cea708Service> blocks)
                    : _sequenceNumber(sequenceNumber & 0x03), _serviceBlocks(std::move(blocks)) {}

                /// @brief 2-bit packet sequence number (0..3).
                uint8_t sequenceNumber() const { return _sequenceNumber; }

                /// @brief Sets the sequence number (low 2 bits used).
                void setSequenceNumber(uint8_t n) { _sequenceNumber = static_cast<uint8_t>(n & 0x03); }

                /// @brief Service blocks carried in this packet.
                const List<Cea708Service> &serviceBlocks() const { return _serviceBlocks; }

                /// @brief Mutable access to the service-block list.
                List<Cea708Service> &serviceBlocks() { return _serviceBlocks; }

                /// @brief Total payload byte count this packet would
                ///        emit (sum of per-block header sizes + data
                ///        sizes).  Excludes the 1-byte packet header.
                size_t payloadByteCount() const;

                /// @brief Serialises this packet into its raw
                ///        payload bytes (without the 1-byte packet
                ///        header).  Each block is prefixed with its
                ///        1- or 2-byte header, then its data bytes.
                Buffer toPayloadBytes() const;

                /// @brief Inverse of @ref toPayloadBytes.  Parses
                ///        the raw payload into a service-block list.
                static Result<List<Cea708Service>> parsePayloadBytes(const void *data, size_t size);

                /// @brief Emits this packet as a list of
                ///        @ref Cea708Cdp::CcData triples ready to
                ///        ride inside a CDP's @c cc_data section.
                ///
                /// The first triple uses @c cc_type=2; subsequent
                /// triples use @c cc_type=3.  Odd-byte tails are
                /// padded with 0xFF (the spec's "no data" filler).
                Cea708Cdp::CcDataList toCcData() const;

                /// @brief Inverse of @ref toCcData.  Walks @p triples
                ///        accepting one packet-start triple followed
                ///        by zero or more packet-data triples.
                ///        Surfaces @c Error::ParseFailed when the
                ///        triple sequence is malformed.
                static Result<Cea708DtvccPacket> fromCcData(const Cea708Cdp::CcDataList &triples);

                /**
                 * @brief Produces a JSON representation for inspection.
                 *
                 * Shape: @c {sequenceNumber, payloadByteCount,
                 * serviceBlocks: [{serviceNumber, dataSize, dataHex}, ...]}.
                 */
                JsonObject toJson() const;

                /**
                 * @brief Short human-readable summary.
                 *
                 * Reports the sequence number + service-block count;
                 * designed for log lines, not machine consumption.
                 */
                String toString() const;

                bool operator==(const Cea708DtvccPacket &o) const {
                        return _sequenceNumber == o._sequenceNumber && _serviceBlocks == o._serviceBlocks;
                }
                bool operator!=(const Cea708DtvccPacket &o) const { return !(*this == o); }

        private:
                uint8_t             _sequenceNumber = 0;
                List<Cea708Service> _serviceBlocks;
};

/** @brief Writes a @ref Cea708DtvccPacket to a @ref DataStream. */
DataStream &operator<<(DataStream &stream, const Cea708DtvccPacket &pkt);

/** @brief Reads a @ref Cea708DtvccPacket from a @ref DataStream. */
DataStream &operator>>(DataStream &stream, Cea708DtvccPacket &pkt);

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_PROAV