/**
 * @file      rtppayloadanc.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_NETWORK
#include <cstddef>
#include <cstdint>
#include <promeki/ancpacket.h>
#include <promeki/buffer.h>
#include <promeki/enums.h>
#include <promeki/error.h>
#include <promeki/namespace.h>
#include <promeki/rtppacket.h>
#include <promeki/rtppayload.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief RTP payload handler for RFC 8331 / SMPTE ST 2110-40
 *        ancillary-data streams.
 * @ingroup network
 *
 * Carries one or more @ref AncPacket "ANC packets" inside an RTP
 * payload using the canonical layout from RFC 8331.  Operates
 * directly on @ref AncTransport::St291 packets — the wire bytes in
 * @ref AncPacket::data are already the 10-bit packed
 * (DID, SDID, DataCount, UDW…, Checksum) form RFC 8331 §2.2
 * requires, so @ref packAncFrame prepends the 4-byte per-packet
 * header and word-aligns; @ref unpackAncPackets does the inverse.
 * No translation happens at this layer.
 *
 * @par Payload header (RFC 8331 §2.1)
 *
 * Each RTP packet starts with an 8-byte ANC payload header
 * followed by one or more ANC packet records:
 *
 * @code
 *  0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |    Extended Sequence Number   |             Length            |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |   ANC_Count   | F |                  reserved                 |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * @endcode
 *
 *  - **ESN** — high 16 bits of the 32-bit logical sequence
 *    number; low 16 bits are the RTP header sequence number.
 *  - **Length** — byte length of the ANC data following the
 *    header in this RTP packet.
 *  - **ANC_Count** — number of ANC packet records that follow.
 *  - **F** — 2-bit field indication (00 = progressive / no
 *    associated field, 10 = interlaced field 1, 11 = interlaced
 *    field 2).
 *
 * @par Per-packet record (RFC 8331 §2.2)
 *
 * @code
 *  0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * | C |     Line_Number     |    Horizontal_Offset    |S| StrmNum |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |       DID (10b)       |       SDID (10b)      |   DataCount   |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |  …UDW (10b each)…  |  …Checksum (10b)…  |  word_align padding |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * @endcode
 *
 * The first 4 bytes are the per-packet header (C-bit, Line_Number,
 * Horizontal_Offset, S-bit, StreamNum).  The 10-bit ANC data
 * (DID, SDID, DataCount, UDW…, Checksum) is copied verbatim from
 * @ref AncPacket::data — it is already MSB-first 10-bit packed.
 * Each record is zero-padded so the next record (or the end of
 * the RTP payload) starts on a 32-bit boundary.
 *
 * @par Sequencing
 *
 * All RTP packets that carry ANC for a single frame share the
 * same RTP timestamp.  The packer sets the RTP marker bit on the
 * last packet of the frame.  Sequence number + SSRC + payload
 * type are stamped on the wire by the per-stream TX thread,
 * matching the convention used by every other @ref RtpPayload
 * subclass; only the RTP timestamp is stamped here so all packets
 * of one frame share one explicit value.
 *
 * @par Byte-stream pack/unpack
 *
 * The byte-stream @ref RtpPayload::pack and @ref RtpPayload::unpack
 * overloads are not used — ANC packetizer threads call
 * @ref packAncFrame / @ref unpackAncPackets directly.  The
 * byte-stream pack returns an empty @ref RtpPacket::List and logs
 * a warning when invoked; the byte-stream unpack returns an empty
 * @ref Buffer.  Tests that exercise the bytewise overloads will
 * see those degenerate values rather than a crash.
 */
class RtpPayloadAnc : public RtpPayload {
        public:
                /// @brief RTP clock rate for RFC 8331 ANC.  RFC 8331
                ///        §3.1 sets 90 kHz as the default; ST 2110-40
                ///        §5.3 hard-mandates 90 kHz.
                static constexpr uint32_t ClockRate = 90000;

                /// @brief Default dynamic RTP payload type number.
                static constexpr uint8_t DefaultPayloadType = 100;

                /// @brief Bytes consumed by the RFC 8331 §2.1 ANC
                ///        payload header at the start of every RTP
                ///        payload.
                static constexpr size_t PayloadHeaderSize = 8;

                /// @brief Bytes consumed by the RFC 8331 §2.2
                ///        per-packet header (C-bit, Line, HOffset,
                ///        S-bit, StreamNum).
                static constexpr size_t PerPacketHeaderSize = 4;

                /**
                 * @brief F-bit values used in the RFC 8331 §2.1
                 *        payload header.
                 *
                 *  - @c Progressive — video is progressive, or the
                 *    ANC stream is not associated with a field.
                 *  - @c Invalid — value @c 0b01, explicitly reserved
                 *    by RFC 8331 §2.1.  Receivers SHOULD ignore an
                 *    ANC data packet whose F field is @c 0b01.
                 *  - @c InterlacedField1 — interlaced source, first
                 *    field (top field for top-field-first).
                 *  - @c InterlacedField2 — interlaced source,
                 *    second field.
                 */
                enum class FieldIndication : uint8_t {
                        Progressive       = 0x0,
                        Invalid           = 0x1,
                        InterlacedField1  = 0x2,
                        InterlacedField2  = 0x3,
                };

                /**
                 * @brief Constructs an ANC payload handler.
                 *
                 * @param payloadType Dynamic RTP payload type (96-127,
                 *                    default @ref DefaultPayloadType).
                 */
                explicit RtpPayloadAnc(uint8_t payloadType = DefaultPayloadType);

                /** @copydoc RtpPayload::payloadType() */
                uint8_t payloadType() const override { return _payloadType; }

                /** @copydoc RtpPayload::clockRate() */
                uint32_t clockRate() const override { return ClockRate; }

                /**
                 * @brief Returns an empty @ref RtpPacket::List and
                 *        logs a warning.
                 *
                 * The ANC packetizer thread calls @ref packAncFrame
                 * with the typed @ref AncPacket::List; the bytewise
                 * overload exists only to satisfy the pure-virtual
                 * @ref RtpPayload::pack and is not part of the ANC
                 * data path.
                 */
                RtpPacket::List pack(const void *mediaData, size_t size) override;

                /**
                 * @brief Returns an empty @ref Buffer and logs a
                 *        warning.
                 *
                 * Mirrors the byte-stream @ref pack — the ANC
                 * depacketizer calls @ref unpackAncPackets instead.
                 */
                Buffer unpack(const RtpPacket::List &packets) override;

                /**
                 * @brief Codec-aware mid-stream-join gate.
                 *
                 * Returns @ref ValidateResult::DropSilently when the
                 * payload is too short to contain the RFC 8331 §2.1
                 * payload header or when it advertises an impossible
                 * record length.  ANC_Count=0 keep-alive payloads
                 * (RFC 8331 §2.1 / ST 2110-40 §5.5) are
                 * @ref ValidateResult::Accept — receivers SHALL
                 * process them as end-of-frame markers for frames
                 * that carry no ancillary data.  A non-zero
                 * ANC_Count with Length=0 (or vice versa) is
                 * malformed and drops silently.
                 */
                ValidateResult validate(const Buffer &unpacked) override;

                /// @brief Overrides the RTP payload type number.
                void setPayloadType(uint8_t pt) { _payloadType = pt; }

                /**
                 * @brief Sets the F-bit value emitted on ANC_Count=0
                 *        keep-alive RTP packets.
                 *
                 * ST 2110-40 §5.5 requires the sender to emit one RTP
                 * packet per video field/frame/segment even when no
                 * ANC data is present.  That keep-alive carries the
                 * F-bit appropriate to the session's video shape
                 * (@ref FieldIndication::Progressive for progressive
                 * frames, @ref FieldIndication::InterlacedField1 /
                 * @c InterlacedField2 for interlaced fields).
                 *
                 * Default: @ref FieldIndication::Progressive.  Sessions
                 * that emit interlaced video should set this from the
                 * paired video desc.
                 *
                 * @param f The session's F-bit value.  Passing
                 *          @ref FieldIndication::Invalid is rejected
                 *          (debug-build assert; release ignores).
                 */
                void setKeepAliveField(FieldIndication f);

                /** @brief Returns the F-bit used for keep-alive RTP packets. */
                FieldIndication keepAliveField() const { return _keepAliveField; }

                /**
                 * @brief Packs an entire frame's ANC packets into one
                 *        or more RFC 8331 RTP packets.
                 *
                 * Each packet in @p packets must have
                 * @ref AncPacket::transport equal to
                 * @ref AncTransport::St291; the function asserts in
                 * debug builds and returns an empty list in release
                 * when this contract is violated.  The packer:
                 *  - prepends the 8-byte ANC payload header
                 *    (RFC 8331 §2.1) to each RTP packet;
                 *  - emits one §2.2 per-packet record per
                 *    @ref AncPacket, copying the 10-bit data bytes
                 *    verbatim from @ref AncPacket::data and reading
                 *    line/h-offset/F-bit/C-bit/StreamNum from
                 *    @ref AncPacket::meta keys declared in
                 *    @ref AncMeta;
                 *  - greedy-fits multiple records into a single RTP
                 *    packet up to @ref maxPayloadSize, splitting a
                 *    frame into multiple RTP packets when needed;
                 *  - sets the RTP marker bit on the last packet of
                 *    the frame;
                 *  - stamps @p rtpTimestamp on every RTP header in
                 *    the returned list.
                 *
                 * When @p packets is empty (or none of the supplied
                 * packets are @ref AncTransport::St291), emits a
                 * single ST 2110-40 §5.5 keep-alive RTP packet with
                 * ANC_Count=0, Length=0, Marker=1, and the F-bit set
                 * to @ref keepAliveField.  Word_align padding is not
                 * used on the keep-alive (RFC 8331 §2.2).
                 *
                 * @param packets     The frame's @ref AncPacket "ANC
                 *                    packets" (all must be on
                 *                    @ref AncTransport::St291).
                 * @param rtpTimestamp Shared RTP timestamp for the
                 *                    frame.
                 * @return The wire RTP packets, sharing a single
                 *         underlying @ref Buffer.  Always contains
                 *         at least one packet when the call succeeds
                 *         (the keep-alive case).
                 */
                RtpPacket::List packAncFrame(const AncPacket::List &packets,
                                             uint32_t                rtpTimestamp);

                /**
                 * @brief Unpacks a list of RTP packets carrying one
                 *        frame's worth of RFC 8331 ANC data.
                 *
                 * Walks each RTP packet's payload (after the 12-byte
                 * RTP header) and produces one @ref AncPacket per
                 * §2.2 per-packet record found, appending them to
                 * @p out in arrival order.  Every emitted
                 * @ref AncPacket has @ref AncTransport::St291 and
                 * @ref AncPacket::data set to the 10-bit packed
                 * (DID, SDID, DataCount, UDW…, Checksum) bytes from
                 * the record (the same canonical form
                 * @ref St291Packet operates on).  Per-record ST 291
                 * framing populated directly on the @ref AncPacket
                 * handle: @ref AncPacket::st291Line,
                 * @ref AncPacket::st291HOffset,
                 * @ref AncPacket::st291FieldB (from the surrounding
                 * payload header's F-bit),
                 * @ref AncPacket::st291CBit,
                 * @ref AncPacket::st291StreamNum.
                 *
                 * Truncated or self-inconsistent payloads return
                 * @c Error::OutOfRange; @p out holds whatever
                 * records were successfully decoded before the
                 * error.
                 *
                 * Per-payload handling:
                 *  - @c ANC_Count == 0 (RFC 8331 §2.1 / ST 2110-40
                 *    §5.5 keep-alive): no records appended; the
                 *    payload signals end-of-frame for a frame with no
                 *    ancillary data.
                 *  - F-bit == @c 0b01 (RFC 8331 §2.1 invalid value):
                 *    the payload's records are skipped per the §2.1
                 *    SHOULD-ignore directive; the function continues
                 *    processing subsequent RTP packets.
                 *
                 * @par Checksum policy
                 *
                 * The @p policy parameter is forwarded to
                 * @c St291Packet::from on every successfully
                 * extracted §2.2 record.  Under
                 * @ref AncChecksumPolicy::StrictValidate any record
                 * whose stored ST 291 §6.4 Checksum_Word does not
                 * match the recomputed value causes
                 * @c unpackAncPackets to return
                 * @c Error::InvalidChecksum; records emitted before
                 * the failure remain in @p out.  The default
                 * @ref AncChecksumPolicy::PreserveOrRecompute accepts
                 * every well-framed record regardless of its
                 * checksum (the byte-exact replay default).
                 *
                 * @param in     RTP packets to unpack (caller has
                 *               reassembled them across the marker
                 *               bit / timestamp boundary).
                 * @param out    Output list, appended to.
                 * @param policy Checksum policy applied to each emitted
                 *               record (default
                 *               @c PreserveOrRecompute).
                 *
                 * @return @c Error::Ok on success;
                 *         @c Error::OutOfRange on truncation;
                 *         @c Error::InvalidChecksum when StrictValidate
                 *         is in force and a record's checksum does not
                 *         match;
                 *         @c Error::Ok with no appended packets if
                 *         every input payload had
                 *         @c ANC_Count == 0 or F == @c 0b01.
                 */
                Error unpackAncPackets(const RtpPacket::List &in,
                                       AncPacket::List       &out,
                                       AncChecksumPolicy      policy = AncChecksumPolicy::PreserveOrRecompute);

        private:
                uint8_t         _payloadType;
                FieldIndication _keepAliveField = FieldIndication::Progressive;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NETWORK
