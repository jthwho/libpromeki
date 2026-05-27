/**
 * @file      ancop47sdp.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_PROAV
#include <cstdint>
#include <promeki/array.h>
#include <promeki/datatype.h>
#include <promeki/error.h>
#include <promeki/json.h>
#include <promeki/list.h>
#include <promeki/namespace.h>
#include <promeki/result.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

class DataStream;

/**
 * @brief RDD 8 / OP-47 Subtitling Distribution Packet (SDP) value.
 * @ingroup proav
 *
 * The SDP (RDD 8-2008 §5) carries up to five ITU-R BT.653 / EBU
 * World System Teletext (WST) subtitle VBI lines inside a single
 * ST 291 ancillary-data packet at DID 0x43 / SDID 0x02.  Each carried
 * VBI line is described by a one-byte Structure A descriptor (the
 * source SD VBI line number plus the field bit) and a 45-byte
 * Structure B payload (the WST data: two-byte run-in, framing code,
 * two-byte MRAG, 40 subtitling bytes).  This value type round-trips
 * every wire field — capture paths preserve the captured descriptors
 * and WST bytes verbatim; producers stamp the SDP fields and let the
 * codec layer compute the LENGTH and SDP CHECKSUM.
 *
 * The class is plain-value: copies are independent and there is no
 * internal shared pointer.
 *
 * @par UDW layout (RDD 8 §5.1)
 *
 *  | Byte(s)        | Field                                    |
 *  |----------------|------------------------------------------|
 *  | 0              | IDENTIFIER 1 = 0x51                      |
 *  | 1              | IDENTIFIER 2 = 0x15                      |
 *  | 2              | LENGTH (full UDW byte count)             |
 *  | 3              | FORMAT CODE = 0x02 (WST teletext)        |
 *  | 4..8           | VBI Packet 1..5 Structure A descriptors  |
 *  | 9..(9+45N-1)   | Active VBI Packet Structure B payloads   |
 *  | next           | FOOTER ID = 0x74                         |
 *  | next, next+1   | FOOTER SEQUENCE COUNTER (MSB, LSB)       |
 *  | next+2         | SDP CHECKSUM                             |
 *
 * Total = 13 + 45 * (number of active packets).  Max 5 packets so the
 * worst case is 238 bytes, well under the ST 291 §3 / 5 255-byte UDW
 * cap.
 *
 * @par Structure A descriptor bit layout (RDD 8 §5.4.1)
 *
 *  - bits b0..b4 = line number (0 and 6..22 for SD video VBI line)
 *  - bits b5..b6 = reserved (zero)
 *  - bit  b7     = field number (0 = even / field 2, 1 = odd / field 1)
 *
 * RDD 8 §8 spells the field convention out: field one is the odd
 * field, field two is the even field.  The @ref VbiPacket::fieldOne
 * accessor uses that naming directly.
 *
 * @par Structure B layout (RDD 8 §5.5)
 *
 *  - bytes 0..1: run-in code (0x55 each, per ITU-R BT.653 §3.3)
 *  - byte  2   : framing code (0x27, per ITU-R BT.653 §3.4)
 *  - bytes 3..4: MRAG (Magazine + Row Address Group, Hamming-protected)
 *  - bytes 5..44: 40 bytes of subtitling data (per ITU-R BT.653 §3.6)
 *
 * The library stores the entire 45-byte payload verbatim; per-line
 * WST decoding (Hamming validation, MRAG / subtitling separation) is
 * out of scope at this layer and is the next codec layer's job.
 *
 * @par Footer Sequence Counter (RDD 8 §5.2)
 *
 * The FSC is a 16-bit counter incremented on every SDP, wrapping at
 * 65535 → 0.  Producers maintain it across SDP emissions; the codec
 * just round-trips the value held by this type.
 *
 * @par SDP Checksum (RDD 8 §5.3)
 *
 * The 8-bit SDP CHECKSUM byte makes the arithmetic sum from
 * IDENTIFIER 1 through SDP CHECKSUM inclusive equal zero modulo 256.
 * The codec build path computes and stamps it; the parse path
 * validates it against the wire and surfaces @c Error::CorruptData
 * when the stored byte does not match the recomputed value.
 *
 * @par Example
 *
 * @code
 * AncOp47Sdp sdp;
 * sdp.setFooterSequenceCounter(0x1234);
 *
 * AncOp47Sdp::VbiPacket pkt;
 * pkt.lineNumber = 9;                                  // SD VBI line 9
 * pkt.fieldOne   = true;                               // odd field
 * pkt.wstData[0] = AncOp47Sdp::RunInCode;              // 0x55
 * pkt.wstData[1] = AncOp47Sdp::RunInCode;              // 0x55
 * pkt.wstData[2] = AncOp47Sdp::FramingCode;            // 0x27
 * // ... fill MRAG (2 bytes) + subtitling (40 bytes) ...
 *
 * sdp.addPacket(pkt);
 * Variant v(sdp);
 * @endcode
 *
 * @par Thread Safety
 * Plain value type.  Copies are independent and may be used
 * concurrently; concurrent mutation of a single instance is not
 * synchronised.
 *
 * @see AncFormat::Op47Sdp, AncFormat::Op47Multipack
 */
class AncOp47Sdp {
        public:
                PROMEKI_DATATYPE(AncOp47Sdp, DataTypeAncOp47Sdp, 1)

                /// @brief Maximum VBI packets per SDP (RDD 8 §5.1: "capable
                ///        of carrying five (5) packets").
                static constexpr size_t MaxVbiPackets = 5;

                /// @brief Structure B payload size in bytes per active
                ///        VBI packet (RDD 8 §5.5.2: 2 run-in + 1 framing
                ///        + 2 MRAG + 40 subtitling).
                static constexpr size_t WstPacketSize = 45;

                /// @brief IDENTIFIER 1 wire byte (RDD 8 §5.1).
                static constexpr uint8_t Identifier1 = 0x51;

                /// @brief IDENTIFIER 2 wire byte (RDD 8 §5.1).
                static constexpr uint8_t Identifier2 = 0x15;

                /// @brief FORMAT CODE for WST (System B) teletext
                ///        subtitles (RDD 8 §5.1, "FORMAT CODE = 102h").
                static constexpr uint8_t FormatCodeWstTeletext = 0x02;

                /// @brief FOOTER ID wire byte (RDD 8 §5.1).
                static constexpr uint8_t FooterId = 0x74;

                /// @brief WST run-in code byte (ITU-R BT.653 §3.3; appears
                ///        twice at the start of every Structure B).
                static constexpr uint8_t RunInCode = 0x55;

                /// @brief WST framing-code byte (ITU-R BT.653 §3.4;
                ///        immediately follows the two run-in bytes).
                static constexpr uint8_t FramingCode = 0x27;

                /// @brief Field-bit mask for the Structure A descriptor
                ///        byte (b7 = field one, RDD 8 §5.4.1).
                static constexpr uint8_t FieldOneBit = 0x80;

                /// @brief Line-number mask for the Structure A
                ///        descriptor byte (bits b0..b4, RDD 8 §5.4.1).
                static constexpr uint8_t LineMask = 0x1F;

                /**
                 * @brief One carried VBI line — Structure A descriptor
                 *        plus 45-byte Structure B payload.
                 *
                 * The descriptor's reserved bits (b5, b6) round-trip
                 * verbatim through @ref reservedBits so a captured
                 * non-zero reserved nibble survives a copy + emit.
                 */
                struct VbiPacket {
                                /// @brief Source SD VBI line number (bits
                                ///        b0..b4 of the descriptor).
                                uint8_t lineNumber = 0;

                                /// @brief Field bit (b7) — @c true =
                                ///        odd field (field 1), @c false
                                ///        = even field (field 2).
                                bool fieldOne = false;

                                /// @brief Reserved Structure A descriptor
                                ///        bits b5, b6 (RDD 8 §5.4.1).
                                ///
                                /// Stored in the low two bits of this
                                /// byte — bit 0 of @c reservedBits maps
                                /// to descriptor b5, bit 1 to descriptor
                                /// b6.  Defaults to 0 per spec; preserved
                                /// across round-trip so a non-conforming
                                /// source's reserved bits don't get
                                /// silently masked.
                                uint8_t reservedBits = 0;

                                /// @brief Full 45-byte WST payload
                                ///        (Structure B) — run-in,
                                ///        framing, MRAG, subtitling.
                                ::promeki::Array<uint8_t, WstPacketSize> wstData{};

                                /** @brief Field-wise equality. */
                                bool operator==(const VbiPacket &o) const {
                                        return lineNumber == o.lineNumber &&
                                               fieldOne == o.fieldOne &&
                                               reservedBits == o.reservedBits &&
                                               wstData == o.wstData;
                                }

                                /** @brief Inequality. */
                                bool operator!=(const VbiPacket &o) const { return !(*this == o); }
                };

                /// @brief List of carried VBI packets (max @ref MaxVbiPackets).
                using PacketList = ::promeki::List<VbiPacket>;

                /** @brief Default-constructs an empty SDP (FSC=0, no packets). */
                AncOp47Sdp() = default;

                // -- Footer Sequence Counter -----------------------------

                /** @brief Returns the 16-bit Footer Sequence Counter. */
                uint16_t footerSequenceCounter() const { return _fsc; }

                /** @brief Replaces the Footer Sequence Counter. */
                void setFooterSequenceCounter(uint16_t v) { _fsc = v; }

                // -- VBI packets ----------------------------------------

                /** @brief Returns the carried VBI packets (0..@ref MaxVbiPackets). */
                const PacketList &packets() const { return _packets; }

                /**
                 * @brief Replaces the carried VBI packet list.
                 *
                 * Lists longer than @ref MaxVbiPackets are accepted by
                 * the value type but rejected by the codec at build
                 * time — the wire format only carries five Structure A
                 * descriptors.  The library prefers to fail loud at
                 * build rather than silently truncate at this layer.
                 */
                void setPackets(PacketList p) { _packets = std::move(p); }

                /** @brief Appends @p p to the VBI packet list. */
                void addPacket(const VbiPacket &p) { _packets.pushToBack(p); }

                /** @brief Clears every carried VBI packet. */
                void clearPackets() { _packets.clear(); }

                // -- Comparison -----------------------------------------

                /** @brief Field-wise equality. */
                bool operator==(const AncOp47Sdp &o) const {
                        return _fsc == o._fsc && _packets == o._packets;
                }

                /** @brief Inequality. */
                bool operator!=(const AncOp47Sdp &o) const { return !(*this == o); }

                // -- Diagnostics ----------------------------------------

                /** @brief Returns a short human-readable summary (FSC + packet count). */
                String toString() const;

                /** @brief Returns a structured JSON representation. */
                JsonObject toJson() const;

                // -- DataStream -----------------------------------------

                /** @brief Writes the canonical wire body via @ref PROMEKI_DATATYPE. */
                Error writeToStream(DataStream &s) const;

                /** @brief Reads the canonical wire body for wire version @p V. */
                template <uint32_t V> static Result<AncOp47Sdp> readFromStream(DataStream &s);

        private:
                uint16_t   _fsc = 0;
                PacketList _packets;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_PROAV
