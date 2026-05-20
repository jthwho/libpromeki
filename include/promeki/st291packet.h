/**
 * @file      st291packet.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_PROAV
#include <cstdint>
#include <promeki/namespace.h>
#include <promeki/ancpacket.h>
#include <promeki/enums.h>
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
 * SMPTE ST 291 / RFC 8331 packet-level fields: DID, the second-word
 * interpretation (SDID for Type-2, DBN for Type-1), DataCount, UDW
 * list (10-bit words), checksum, and the per-capture framing fields
 * (VANC line, horizontal offset, F-bit, C-bit, StreamNum) carried in
 * the @ref AncPacket::meta sidecar.
 *
 * @par Type-1 vs Type-2 packets
 *
 * ST 291-1 §5.1 + RFC 8331 §2.2 define two ANC packet shapes that
 * differ only in the meaning of the **second 10-bit word** after
 * the DID:
 *
 *  - **Type 2** (DID high bit clear, i.e. DID < @c 0x80) — the second
 *    word carries the **SDID** (secondary identifier) byte.  Every
 *    well-known modern format the library registers today is Type-2
 *    (CEA-708, AFD, ATC LTC, SCTE-104, ...).
 *  - **Type 1** (DID high bit set, i.e. DID ≥ @c 0x80) — the second
 *    word carries the **Data Block Number (DBN)**.  DBN is a
 *    per-DID 8-bit cycle counter (1..255, wrapping 255→1 and skipping
 *    0) that a stream of related Type-1 packets uses to detect drops
 *    on the wire (ST 291-1 §5.2.4).  Packet-Marked-for-Deletion (DID
 *    @c 0x80) is the only Type-1 packet in common use; codecs that
 *    target Type-1 carriage build packets with
 *    @ref buildRawType1 and read DBN via @ref dbn.
 *
 * The library's wire representation is identical for both shapes —
 * DID, second-word, DataCount, UDW…, Checksum as 10-bit words packed
 * MSB-first — and which interpretation applies to the second word is
 * a function of DID's high bit.  @ref sdid returns the second-word
 * byte for Type-2 packets and zero for Type-1; @ref dbn returns the
 * second-word byte for Type-1 packets and zero for Type-2.
 *
 * The underlying @ref AncPacket::data buffer holds the **raw 10-bit
 * packed payload** of the ST 291 packet — DID, SDID/DBN, DataCount,
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
 * Each ST 291 word is 10 bits.  For DID, SDID, DBN, DC, and UDW
 * the low 8 bits are the data byte and the upper 2 bits are parity
 * (bit 8 = even parity over bits 0–7, bit 9 = NOT bit 8) per
 * ST 291-1 §6.1, §6.2, §6.4, §6.5, §6.6.  The checksum word (CS) is
 * different: it is a 9-bit value (bits 8 MSB through 0 LSB) with
 * bit 9 = NOT bit 8 per ST 291-1 §6.7 — there is no separate parity
 * bit because the CS value occupies all nine low bits.
 *
 * Words are packed contiguously starting from the MSB of byte 0;
 * the build helpers compute parity automatically from
 * caller-supplied 8-bit data bytes (or honour caller-supplied 10-bit
 * words when the upper 2 bits are non-zero).  Pass-through inputs
 * whose upper 2 bits are @c 11 are rejected because such a word is
 * a parity-violation per §6 (bit 9 = NOT bit 8 is the universal
 * encoding rule for DID/SDID/DBN/DC/UDW): a caller-supplied
 * 0x300-0x3FF word cannot be a parity-correct 10-bit ANC word.  The
 * rejection band overlaps the §9.1 protected codes 0x3FC-0x3FF, but
 * the rationale is parity-correctness, not §9.1 — the two §9.1
 * protected codes 0x000-0x003 are unreachable from the pass-through
 * path by construction (the parity-compute branch produces bits 8-9
 * of @c 01b / @c 10b for every 8-bit input).
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
 * // Build a CEA-708 CDP (Type-2) packet on line 11:
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
 *
 * // Build a Type-1 Packet-Marked-for-Deletion with explicit DBN:
 * St291Packet del = St291Packet::buildRawType1(0x80, dbn, udw, 11);
 * assert(del.isType1());
 * assert(del.dbn() == dbn && del.sdid() == 0);
 * @endcode
 *
 * @see AncPacket, AncFormat,
 *      @c RFC 8331, @c SMPTE ST 291-1
 */
class St291Packet {
        public:
                /**
                 * @name RFC 8331 §2.2 / ST 2110-40 §5.2.2 sentinel values
                 *       for Line_Number and Horizontal_Offset.
                 *
                 * The 11-bit Line_Number field and the 12-bit
                 * Horizontal_Offset field each have a small set of
                 * reserved values that signal "location unknown" or
                 * "location too large to encode" rather than a real
                 * coordinate.  The defaults used by the library mirror
                 * the ST 2110-40 §5.2.2 recommendation: when the
                 * capture path cannot stamp an exact location,
                 * Line_Number defaults to @ref UnspecifiedLine (0x7FE,
                 * "two lines after the switching point" per RP 168)
                 * and Horizontal_Offset defaults to
                 * @ref UnspecifiedHOffset (0xFFF, "no specific
                 * horizontal location").
                 */
                /// @{

                /// @brief Line_Number sentinel "without specific line location"
                ///        (RFC 8331 §2.2).
                static constexpr uint16_t LineNoSpecific = 0x7FF;

                /// @brief Line_Number sentinel "anywhere from two lines
                ///        after the switching line to the line before
                ///        active video" (RFC 8331 §2.2 / RP 168) — the
                ///        recommended default per ST 2110-40 §5.2.2.
                static constexpr uint16_t LineSwitchingDefault = 0x7FE;

                /// @brief Line_Number sentinel "line number larger than
                ///        11 bits" (RFC 8331 §2.2).
                static constexpr uint16_t LineLargerThan11Bits = 0x7FD;

                /// @brief Default Line_Number used by the library when
                ///        no specific line is known.  Alias of
                ///        @ref LineSwitchingDefault (ST 2110-40 §5.2.2
                ///        recommendation).
                static constexpr uint16_t UnspecifiedLine = LineSwitchingDefault;

                /// @brief Horizontal_Offset sentinel "without specific
                ///        horizontal location" (RFC 8331 §2.2).
                static constexpr uint16_t UnspecifiedHOffset = 0xFFF;

                /// @brief Horizontal_Offset sentinel "within HANC space"
                ///        (RFC 8331 §2.2).
                static constexpr uint16_t HOffsetInHanc = 0xFFE;

                /// @brief Horizontal_Offset sentinel "within active-video
                ///        region (between SAV and EAV)" (RFC 8331 §2.2).
                static constexpr uint16_t HOffsetInActiveVideo = 0xFFD;

                /// @brief Horizontal_Offset sentinel "value larger than
                ///        12 bits" (RFC 8331 §2.2).
                static constexpr uint16_t HOffsetLargerThan12Bits = 0xFFC;

                /// @}

                /**
                 * @brief Promotes an existing @ref AncPacket to a typed
                 *        @c St291Packet.
                 *
                 * Validates:
                 *  - @p pkt's transport is @c AncTransport::St291.
                 *  - @p pkt's data buffer is large enough to contain
                 *    the minimum ST 291 packet (3 ten-bit header words
                 *    + 1 checksum word = 5 bytes after rounding).
                 *  - The buffer length is consistent with the declared
                 *    DataCount byte: @c ceil((4 + DC) * 10 / 8) bytes
                 *    are required for the full DID + SDID/DBN + DC +
                 *    UDW… + CS sequence.  A buffer shorter than that
                 *    is a truncated capture and is rejected
                 *    (ST 291-1 §6.5 + RFC 8331 §2.2).
                 *
                 * Returns @c Error::InvalidArgument when any structural
                 * check fails.
                 *
                 * @par Checksum policy
                 *
                 * The @p policy parameter governs how the §6.4
                 * Checksum_Word is treated on promotion:
                 *
                 *  - @ref AncChecksumPolicy::PreserveOrRecompute
                 *    (default) — accept the packet regardless of the
                 *    stored checksum.  Preserves byte-exact replay for
                 *    captured packets that may contain occasional bit
                 *    errors downstream codecs gracefully tolerate.
                 *  - @ref AncChecksumPolicy::AlwaysRecompute — same
                 *    behaviour as @c PreserveOrRecompute on the parse
                 *    path; the policy distinction matters only when
                 *    the packet is later re-emitted.
                 *  - @ref AncChecksumPolicy::StrictValidate — RFC 8331
                 *    §7 SHOULD-check: validate that the stored
                 *    Checksum_Word equals the value recomputed over
                 *    (DID, SDID, DataCount, UDW) per ST 291-1 §6.4.
                 *    On mismatch the promotion fails with
                 *    @ref Error::InvalidChecksum.
                 *
                 * Default is tolerant on the general-purpose library
                 * entry point so byte-exact replay of captured packets
                 * with occasional bit errors keeps working;
                 * production-grade ingest sessions opt into
                 * @c StrictValidate via the translator's
                 * @c AncTranslateConfig::Checksum key.
                 *
                 * @param pkt    The packet to promote.
                 * @param policy The checksum policy to apply on promotion
                 *               (default @c PreserveOrRecompute).
                 * @return The typed view on success.
                 */
                static Result<St291Packet> from(const AncPacket        &pkt,
                                                AncChecksumPolicy policy = AncChecksumPolicy::PreserveOrRecompute);

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
                 *                   (placed on @c AncPacket::st291Line()).
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
                 * DID and second-word byte.  Use this for Type-2
                 * packets (DID < @c 0x80); the second-word byte is
                 * the SDID.  For Type-1 packets (DID ≥ @c 0x80) prefer
                 * @ref buildRawType1 — the parameter name makes the
                 * DBN-vs-SDID distinction explicit at the call site.
                 *
                 * Looks up the matching @ref AncFormat via
                 * @c AncFormat::fromSt291DidSdid(did, sdid); the
                 * resulting packet's @c format() may be
                 * @c AncFormat::Invalid when the pair has no registered
                 * mapping (the wire bytes still round-trip).
                 */
                static St291Packet buildRaw(uint8_t did, uint8_t sdid, const List<uint16_t> &udw, uint16_t line,
                                            uint16_t hOffset = UnspecifiedHOffset, bool fieldB = false,
                                            bool cBit = false, uint8_t streamNum = 0);

                /**
                 * @brief Builds a **Type-1** ST 291 packet (DID ≥
                 *        @c 0x80) with an explicit Data Block Number.
                 *
                 * Type-1 packets carry the DBN in word 1 in place of
                 * the SDID.  Callers that emit Type-1 streams manage
                 * the per-DID DBN cycle themselves (ST 291-1 §5.2.4:
                 * 1..255, wrapping 255→1, skipping 0) and pass the
                 * current DBN here.  When @p did has its high bit
                 * clear (a Type-2 DID) the build fails and an invalid
                 * @c St291Packet is returned.
                 *
                 * @param did       The Type-1 DID byte (must have the
                 *                  high bit set).
                 * @param dbn       The Data Block Number for this
                 *                  packet in the per-DID stream.
                 * @param udw       The user data words.
                 * @param line      VANC line number for the packet.
                 * @param hOffset   Horizontal offset (default
                 *                  @ref UnspecifiedHOffset).
                 * @param fieldB    F-bit (true for field 2).
                 * @param cBit      C-bit (true for chrominance stream).
                 * @param streamNum StreamNum for multi-link / 12G SDI.
                 */
                static St291Packet buildRawType1(uint8_t did, uint8_t dbn, const List<uint16_t> &udw, uint16_t line,
                                                 uint16_t hOffset = UnspecifiedHOffset, bool fieldB = false,
                                                 bool cBit = false, uint8_t streamNum = 0);

                /** @brief Default-constructs an invalid @c St291Packet (empty data). */
                St291Packet() = default;

                /** @brief Returns the DID byte (the data byte of the first ST 291 word). */
                uint8_t did() const;

                /**
                 * @brief Returns the SDID byte for Type-2 packets, or
                 *        zero when this is a Type-1 packet.
                 *
                 * Type-1 packets (DID ≥ @c 0x80) carry a DBN in word 1
                 * instead of an SDID; use @ref dbn for those.  The
                 * Type-1 SDP/SDP-fmtp sentinel value per RFC 8331 §3.1
                 * is @c 0x00, which is what this accessor returns —
                 * the actual wire byte (the DBN) is still available
                 * via @ref dbn or @ref udwRaw.
                 */
                uint8_t sdid() const;

                /**
                 * @brief Returns the Data Block Number for Type-1
                 *        packets, or zero when this is a Type-2 packet.
                 *
                 * Type-1 packets (DID ≥ @c 0x80) store an 8-bit DBN
                 * in word 1 in place of the SDID; consumers that want
                 * to track stream continuity (drop detection per
                 * ST 291-1 §5.2.4) read it here.  Returns @c 0 for
                 * Type-2 packets — those have no DBN concept.
                 */
                uint8_t dbn() const;

                /** @brief Returns the DataCount byte. */
                uint8_t dataCount() const;

                /**
                 * @brief Returns the User Data Words as 8-bit data
                 *        bytes — parity bits stripped.
                 *
                 * The list has @c dataCount entries; each entry's
                 * value is in the range @c 0x00–0xFF.  This is the
                 * accessor every in-tree codec uses, since the upper
                 * 2 parity bits of each ST 291 UDW are computed from
                 * the data byte and carry no information beyond it
                 * (the build path recomputes them automatically when
                 * the packet is re-emitted).
                 *
                 * For byte-exact replay verification that needs to
                 * preserve the parity-bit encoding (the rare case
                 * where a sender hand-built parity values the library
                 * would not naturally emit), see @ref udwRaw.
                 */
                List<uint16_t> udw() const;

                /**
                 * @brief Returns the User Data Words as **raw 10-bit
                 *        values** — same as @ref udw but with the
                 *        parity bits preserved verbatim.
                 *
                 * @ref udw and @ref udwRaw return the same list for
                 * packets built via @ref build / @ref buildRaw from
                 * 8-bit data bytes (the build path computes parity
                 * and emits the canonical encoding).  They differ
                 * only when the wire bytes carry a non-canonical
                 * parity encoding — e.g. byte-exact replay of a
                 * received packet where the sender's parity bits
                 * matter to a strict consumer.  This accessor is the
                 * tool for that case.
                 */
                List<uint16_t> udwRaw() const;

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
                 *  @c AncPacket::st291Line() on the underlying packet. */
                uint16_t line() const;

                /** @brief Returns the horizontal offset from
                 *  @c AncPacket::st291HOffset(). */
                uint16_t hOffset() const;

                /** @brief Returns the F-bit from
                 *  @c AncPacket::st291FieldB(). */
                bool fieldB() const;

                /** @brief Returns the C-bit from
                 *  @c AncPacket::st291CBit(). */
                bool cBit() const;

                /** @brief Returns the StreamNum from
                 *  @c AncPacket::st291StreamNum(). */
                uint8_t streamNum() const;

                /**
                 * @brief Returns @c true when the DID's high bit is set
                 *        (Type-1 packet per ST 291-1 §5.1).
                 *
                 * Type-1 packets (DID ≥ @c 0x80) carry a Data Block
                 * Number in word 1; Type-2 packets (DID < @c 0x80)
                 * carry an SDID there.  The DC, UDW, and CS layout
                 * is identical.  Almost every modern ANC format the
                 * library registers is Type-2; the canonical Type-1
                 * is Packet-Marked-for-Deletion (DID @c 0x80,
                 * ST 291-1 §6.3).
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

#endif // PROMEKI_ENABLE_PROAV
