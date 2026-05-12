/**
 * @file      st291packet.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstdint>
#include <promeki/namespace.h>
#include <promeki/ancpacket.h>
#include <promeki/list.h>
#include <promeki/result.h>
#include <promeki/error.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Typed accessor over an @ref AncPacket whose transport is
 *        @c AncTransport::St291.
 * @ingroup proav
 *
 * Composition helper rather than a subclass — holds an @ref AncPacket
 * by value (one refcount-shared @c Impl pointer) and exposes the
 * SMPTE ST 291 / RFC 8331 packet-level fields: DID, SDID, DataCount,
 * UDW list (10-bit words), checksum, and the per-capture framing
 * fields (VANC line, horizontal offset, F-bit, C-bit, StreamNum)
 * carried in the @ref AncPacket::meta sidecar.
 *
 * The underlying @ref AncPacket::data buffer holds the **raw 10-bit
 * packed payload** of the ST 291 packet — DID, SDID, DataCount,
 * UDW1...UDWn, Checksum — exactly as it lives on the wire (RFC 8331
 * "ANC Data" portion of the per-packet record, less the surrounding
 * Line_Number / H_Offset / StreamNum framing word, which lives in
 * @ref AncPacket::meta).  The transport-level RFC 8331 framing
 * header is added/stripped by the RTP packetiser; this class works
 * on the canonical ST 291 storage form regardless of what wire
 * carries it.
 *
 * @par 10-bit packing
 *
 * Each ST 291 word is 10 bits: the low 8 bits are the data byte and
 * the upper 2 bits are parity (bit 8 = even parity over bits 0–7,
 * bit 9 = NOT bit 8).  Words are packed contiguously starting from
 * the MSB of byte 0; the build helpers compute parity automatically
 * from caller-supplied 8-bit data bytes (or honour caller-supplied
 * 10-bit words when the upper 2 bits are non-zero).
 *
 * @par Implicit decay
 *
 * @c St291Packet is implicitly convertible to @c const @c AncPacket&,
 * so storage paths that take an @ref AncPacket (e.g.
 * @c AncPayload::addPacket, @ref AncPacket::List) accept an
 * @c St291Packet without an explicit unwrap.  Promote in the other
 * direction with @ref from.
 *
 * @par Example
 * @code
 * // Build a CEA-708 CDP packet on line 11:
 * List<uint8_t> cdpBytes = ...;
 * List<uint16_t> udw;
 * for(uint8_t b : cdpBytes) udw.pushToBack(b);
 * St291Packet p = St291Packet::build(AncFormat(AncFormat::Cea708),
 *                                     udw, 11);
 *
 * AncPayload &payload = ...;
 * payload.addPacket(p);  // implicit decay to const AncPacket&
 *
 * // Promote an inbound AncPacket back to a typed view:
 * Result<St291Packet> rp = St291Packet::from(somePkt);
 * if(isOk(rp)) {
 *         uint8_t did = value(rp).did();
 * }
 * @endcode
 *
 * @see AncPacket, AncFormat, AncMeta::St291,
 *      @c RFC 8331, @c SMPTE ST 291-1
 */
class St291Packet {
        public:
                /** @brief Default horizontal-offset value meaning "unspecified" (per RFC 8331 §2.2). */
                static constexpr uint16_t UnspecifiedHOffset = 0xFFF;

                /**
                 * @brief Promotes an existing @ref AncPacket to a typed
                 *        @c St291Packet.
                 *
                 * Validates that @p pkt's transport is
                 * @c AncTransport::St291 and that @p pkt's data buffer
                 * is large enough to contain the minimum ST 291 packet
                 * (3 ten-bit header words + 1 checksum word = 5 bytes
                 * after rounding).  Returns @c Error::InvalidArgument
                 * otherwise.
                 *
                 * @param pkt The packet to promote.
                 * @return The typed view on success.
                 */
                static Result<St291Packet> from(const AncPacket &pkt);

                /**
                 * @brief Builds an ST 291 packet from a registered
                 *        @ref AncFormat and a UDW list.
                 *
                 * Resolves DID / SDID from @p fmt.st291Did() and
                 * @p fmt.st291Sdid() (the format must have non-zero
                 * @c st291Did — i.e. it has a registered ST 291
                 * representation).  Wildcard-SDID formats
                 * (e.g. @c Smpte2020Audio) require the caller to use
                 * @ref buildRaw to supply the discriminating SDID byte.
                 *
                 * Each entry of @p udw is interpreted as an 8-bit data
                 * byte when the upper 2 bits are zero; otherwise the
                 * entry is treated as a full 10-bit word (parity bits
                 * preserved).  The build computes the checksum
                 * per ST 291 §6.4 and stores it as the trailing 10-bit
                 * word.
                 *
                 * @param fmt        The logical format (must have a
                 *                   non-zero, non-wildcard
                 *                   @c st291Sdid).
                 * @param udw        The user data words.
                 * @param line       VANC line number for the packet
                 *                   (placed in @c AncMeta::St291::Line).
                 * @param hOffset    Horizontal offset (default
                 *                   @ref UnspecifiedHOffset).
                 * @param fieldB     F-bit (true for field 2).
                 * @param cBit       C-bit (true for chrominance stream).
                 * @param streamNum  StreamNum for multi-link / 12G SDI.
                 * @return The built packet.
                 */
                static St291Packet build(const AncFormat &fmt, const List<uint16_t> &udw, uint16_t line,
                                         uint16_t hOffset = UnspecifiedHOffset, bool fieldB = false, bool cBit = false,
                                         uint8_t streamNum = 0);

                /**
                 * @brief Escape hatch for unregistered DID/SDID pairs
                 *        or wildcard-SDID formats.
                 *
                 * Same behaviour as @ref build but with caller-supplied
                 * DID and SDID bytes.  Looks up the matching
                 * @ref AncFormat via
                 * @c AncFormat::fromSt291DidSdid(did, sdid); the
                 * resulting packet's @c format() may be
                 * @c AncFormat::Invalid when the pair has no registered
                 * mapping (the wire bytes still round-trip).
                 */
                static St291Packet buildRaw(uint8_t did, uint8_t sdid, const List<uint16_t> &udw, uint16_t line,
                                            uint16_t hOffset = UnspecifiedHOffset, bool fieldB = false,
                                            bool cBit = false, uint8_t streamNum = 0);

                /** @brief Default-constructs an invalid @c St291Packet (empty data). */
                St291Packet() = default;

                /** @brief Returns the DID byte (the data byte of the first ST 291 word). */
                uint8_t did() const;

                /** @brief Returns the SDID byte (the data byte of the second ST 291 word). */
                uint8_t sdid() const;

                /** @brief Returns the DataCount byte. */
                uint8_t dataCount() const;

                /**
                 * @brief Returns the User Data Words as full 10-bit
                 *        values (data byte in bits 0–7, parity bits in
                 *        8–9).
                 *
                 * The list has @c dataCount entries.  Mask the result
                 * with @c 0xFF to recover the 8-bit data bytes for
                 * formats (the dominant case — CEA-708, AFD, ATC, …)
                 * where the parity bits are not part of the payload.
                 */
                List<uint16_t> udw() const;

                /**
                 * @brief Returns the checksum word as stored in the
                 *        wire bytes.
                 *
                 * May differ from @ref computedChecksum when the packet
                 * was constructed by @ref buildRaw with a
                 * caller-provided checksum or when a checksum
                 * mismatch was tolerated during capture
                 * (@c AncChecksumPolicy::PreserveOrRecompute).
                 */
                uint16_t checksum() const;

                /**
                 * @brief Returns the checksum computed from the current
                 *        DID, SDID, DataCount, and UDW per ST 291 §6.4.
                 */
                uint16_t computedChecksum() const;

                /** @brief Returns @c true when @ref checksum equals @ref computedChecksum. */
                bool checksumValid() const;

                /** @brief Returns the VANC line number from
                 *  @c AncMeta::St291::Line on the underlying packet. */
                uint16_t line() const;

                /** @brief Returns the horizontal offset from
                 *  @c AncMeta::St291::HOffset. */
                uint16_t hOffset() const;

                /** @brief Returns the F-bit from
                 *  @c AncMeta::St291::FieldB. */
                bool fieldB() const;

                /** @brief Returns the C-bit from
                 *  @c AncMeta::St291::CBit. */
                bool cBit() const;

                /** @brief Returns the StreamNum from
                 *  @c AncMeta::St291::StreamNum. */
                uint8_t streamNum() const;

                /**
                 * @brief Returns @c true when the DID's high bit is set
                 *        (Type-1 packet per ST 291).
                 *
                 * Type-1 packets carry their own length; Type-2
                 * packets share a length via the surrounding stream.
                 * Almost every modern ANC format is Type-1.
                 */
                bool isType1() const { return (did() & 0x80) != 0; }

                /**
                 * @brief Replaces the UDW list and recomputes the
                 *        stored checksum.
                 *
                 * Performs CoW detach on the underlying @ref AncPacket
                 * before mutating.
                 */
                void setUdw(const List<uint16_t> &udw);

                /** @brief Replaces the VANC line number (CoW-detaches). */
                void setLine(uint16_t line);

                /** @brief Replaces the horizontal offset (CoW-detaches). */
                void setHOffset(uint16_t hOffset);

                /** @brief Replaces the F-bit (CoW-detaches). */
                void setFieldB(bool fieldB);

                /** @brief Replaces the C-bit (CoW-detaches). */
                void setCBit(bool cBit);

                /** @brief Replaces the StreamNum (CoW-detaches). */
                void setStreamNum(uint8_t streamNum);

                /** @brief Returns the underlying generic @ref AncPacket. */
                const AncPacket &packet() const { return _pkt; }

                /** @brief Implicit conversion to @c const @c AncPacket&. */
                operator const AncPacket &() const { return _pkt; }

                /**
                 * @brief Returns @c true when the underlying
                 *        @ref AncPacket is on @c AncTransport::St291 and
                 *        has non-empty wire data.
                 */
                bool isValid() const;

        private:
                explicit St291Packet(const AncPacket &pkt) : _pkt(pkt) {}
                AncPacket _pkt;
};

PROMEKI_NAMESPACE_END
