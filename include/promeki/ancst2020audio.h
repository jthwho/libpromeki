/**
 * @file      ancst2020audio.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_PROAV
#include <cstdint>
#include <promeki/buffer.h>
#include <promeki/datatype.h>
#include <promeki/error.h>
#include <promeki/json.h>
#include <promeki/namespace.h>
#include <promeki/result.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

class DataStream;

/**
 * @brief SMPTE ST 2020 Dolby audio metadata value, Method A carriage.
 * @ingroup proav
 *
 * ST 2020-1:2014 §7 defines DID 0x45 for VANC packets carrying audio
 * metadata associated with a Dolby-encoded audio program.  The SDID
 * byte (ST 2020-1 §7.1 Table 1) selects which audio channel pair the
 * metadata applies to — 0x01 = "no specific association", 0x02 =
 * channel pair 1/2, 0x03 = channel pair 3/4, …, 0x09 = channel pair
 * 15/16.  ST 2020-2:2014 (Method A) defines the in-VANC byte layout
 * used here: a one-byte Payload Descriptor followed by the metadata
 * frame bytes (up to 254 per packet; one logical frame larger than
 * 254 bytes splits across two consecutive VANC packets per §5.3).
 *
 * @par UDW layout (ST 2020-2 §5.1, §5.3, §5.4)
 *
 *  | Byte(s)   | Field                                                   |
 *  |-----------|---------------------------------------------------------|
 *  | 0         | Payload Descriptor (see bit layout below)               |
 *  | 1..DC-1   | Metadata Frame bytes (up to 254 per VANC packet)        |
 *
 * @par Payload Descriptor bit layout (ST 2020-2 §5.4 Table 1)
 *
 *  - bit 7 = COMPATIBILITY (set to 0)
 *  - bit 6 = Reserved (0)
 *  - bit 5 = Reserved (0)
 *  - bit 4..3 = VERSION (`01b` for the current ST 2020-2 syntax)
 *  - bit 2 = DOUBLE_PKT — 1 when the metadata frame is split across
 *            two packets
 *  - bit 1 = SECOND_PKT — 1 when this is the second of the pair
 *  - bit 0 = DUPLICATE_PKT — 1 when this packet re-issues the metadata
 *            from the previous frame (used at 50/60 Hz so the frame-pair
 *            second frame gets the same metadata as the first)
 *
 * This value type holds the *parsed metadata frame* — the receive
 * side reassembles the two halves of a split frame before surfacing
 * the value, so the `DOUBLE_PKT` / `SECOND_PKT` bits are not part of
 * the value-type contract.  The codec decides at build time whether
 * to emit one or two packets based on the metadata frame length.
 *
 * @par Channel-pair enumeration (ST 2020-1 §7.1 Table 1)
 *
 * The library uses the SDID byte directly as the channel-pair
 * identifier.  Named constants @ref NoAssociation through
 * @ref ChannelPair15_16 cover the nine valid SDID values; arbitrary
 * SDIDs round-trip verbatim through @ref setChannelPair so a future
 * ST 2020-1 revision that defines additional SDIDs Just Works at
 * this layer.
 *
 * @par Metadata frame contents
 *
 * ST 2020-1 §5 describes the metadata frame as an asynchronous serial
 * bitstream framed by sync segments and data segments; the actual
 * contents are defined by the audio codec (Dolby E, Dolby Digital,
 * Dolby Digital Plus, …).  This value type treats the metadata frame
 * as opaque bytes — deeper parsing belongs to a separate value type
 * or codec layer.
 *
 * @par Example
 *
 * @code
 * AncSt2020Audio meta;
 * meta.setChannelPair(AncSt2020Audio::ChannelPair1_2);   // SDID 0x02
 * Buffer payload(180);
 * payload.setSize(180);
 * // ... fill payload with Dolby metadata bytes ...
 * meta.setMetadataFrame(std::move(payload));
 *
 * Variant v(meta);
 * @endcode
 *
 * @par Thread Safety
 * Plain value type.  Copies are independent and may be used
 * concurrently; concurrent mutation of a single instance is not
 * synchronised.
 *
 * @see AncFormat::Smpte2020Audio
 */
class AncSt2020Audio {
        public:
                PROMEKI_DATATYPE(AncSt2020Audio, DataTypeSt2020Audio, 1)

                /// @brief Channel-pair association values per ST 2020-1
                ///        §7.1 Table 1 (= SDID byte on the wire).
                enum ChannelPair : uint8_t {
                        NoAssociation    = 0x01,
                        ChannelPair1_2   = 0x02,
                        ChannelPair3_4   = 0x03,
                        ChannelPair5_6   = 0x04,
                        ChannelPair7_8   = 0x05,
                        ChannelPair9_10  = 0x06,
                        ChannelPair11_12 = 0x07,
                        ChannelPair13_14 = 0x08,
                        ChannelPair15_16 = 0x09,
                };

                /// @brief Max metadata-frame bytes carried by a single
                ///        ST 2020-2 VANC packet (§5.3: DC1 = MDF + 1
                ///        ≤ 255 → MDF ≤ 254).
                static constexpr size_t MaxSinglePacketBytes = 254;

                /// @brief Max metadata-frame bytes carried across a pair
                ///        of split VANC packets (2 × 254 — §5.3).
                static constexpr size_t MaxMetadataFrameBytes = 2 * MaxSinglePacketBytes;

                /// @brief Mask for the COMPATIBILITY bit (§5.4.1, bit 7).
                ///        Shall be 0 on every conformant Method-A packet.
                static constexpr uint8_t PayloadDescriptorCompatibilityBit = 0x80;

                /// @brief Payload Descriptor mask for the current ST 2020-2
                ///        Mapping Syntax Version ("01b" per §5.4.2 Table 2):
                ///        bit 4 = 0, bit 3 = 1 → byte value `0x08`.
                static constexpr uint8_t PayloadDescriptorVersionV1 = 0x08;

                /// @brief Mask for the DOUBLE_PKT bit (§5.4.3, bit 2).
                static constexpr uint8_t PayloadDescriptorDoubleBit = 0x04;

                /// @brief Mask for the SECOND_PKT bit (§5.4.3, bit 1).
                static constexpr uint8_t PayloadDescriptorSecondBit = 0x02;

                /// @brief Mask for the DUPLICATE_PKT bit (§5.4.4, bit 0).
                static constexpr uint8_t PayloadDescriptorDuplicateBit = 0x01;

                /// @brief Mask for the version-bit field (bits 4..3) on
                ///        the payload descriptor byte.
                static constexpr uint8_t PayloadDescriptorVersionMask = 0x18;

                /** @brief Default-constructs an empty value (no association, no payload). */
                AncSt2020Audio() = default;

                // -- Channel pair association ----------------------------

                /** @brief Returns the SDID byte (= channel-pair association). */
                uint8_t channelPair() const { return _channelPair; }

                /** @brief Replaces the SDID byte (= channel-pair association). */
                void setChannelPair(uint8_t v) { _channelPair = v; }

                // -- Duplicate flag --------------------------------------

                /**
                 * @brief Returns the DUPLICATE_PKT flag (Payload Descriptor
                 *        bit 0, ST 2020-2 §5.4.4).
                 *
                 * Set when this metadata frame duplicates the one carried
                 * in the previous video frame — used at 50 / 59.94 / 60
                 * Hz to give the second physical frame of a frame-pair
                 * the same metadata as the first.
                 */
                bool duplicate() const { return _duplicate; }

                /** @brief Replaces the DUPLICATE_PKT flag. */
                void setDuplicate(bool b) { _duplicate = b; }

                // -- Metadata frame --------------------------------------

                /** @brief Returns the carried metadata-frame bytes (ST 2020-1 §5). */
                const Buffer &metadataFrame() const { return _metadataFrame; }

                /**
                 * @brief Replaces the carried metadata-frame bytes.
                 *
                 * The library treats the contents as opaque — deeper
                 * parsing per ST 2020-1 §5.1 / §5.2 (sync segments + data
                 * segments + end-of-frame sync) is the next codec layer's
                 * job.  Frames longer than @ref MaxMetadataFrameBytes are
                 * accepted by the value type but rejected by the codec
                 * at build time.
                 */
                void setMetadataFrame(Buffer bytes) { _metadataFrame = std::move(bytes); }

                // -- Comparison -----------------------------------------

                /** @brief Field-wise equality. */
                bool operator==(const AncSt2020Audio &o) const {
                        return _channelPair == o._channelPair && _duplicate == o._duplicate &&
                               _metadataFrame == o._metadataFrame;
                }

                /** @brief Inequality. */
                bool operator!=(const AncSt2020Audio &o) const { return !(*this == o); }

                // -- Diagnostics ----------------------------------------

                /** @brief Returns a short human-readable summary. */
                String toString() const;

                /** @brief Returns a structured JSON representation. */
                JsonObject toJson() const;

                // -- DataStream -----------------------------------------

                /** @brief Writes the canonical wire body via @ref PROMEKI_DATATYPE. */
                Error writeToStream(DataStream &s) const;

                /** @brief Reads the canonical wire body for wire version @p V. */
                template <uint32_t V> static Result<AncSt2020Audio> readFromStream(DataStream &s);

        private:
                uint8_t _channelPair = NoAssociation;
                bool    _duplicate = false;
                Buffer  _metadataFrame;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_PROAV
