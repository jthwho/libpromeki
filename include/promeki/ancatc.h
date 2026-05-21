/**
 * @file      ancatc.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_PROAV
#include <cstdint>
#include <promeki/datatype.h>
#include <promeki/error.h>
#include <promeki/json.h>
#include <promeki/namespace.h>
#include <promeki/result.h>
#include <promeki/string.h>
#include <promeki/timecode.h>

PROMEKI_NAMESPACE_BEGIN

class DataStream;

/**
 * @brief Returns @c true when @p frameRateFps is a "pair-rate" HFR rate
 *        where ST 12-1 §12 frame-pair / ST 12-2 §9.2 ATC_VITC1 + ATC_VITC2
 *        alternation is the conformant carriage path.
 *
 * Pair-rates are 48, 50, 59.94 and 60 fps (digit family fps 48/50/60 —
 * 59.94 reports as 60 from @c Timecode::fps because the digit family
 * is 30×2).  At a pair-rate every super-frame is split across two
 * consecutive physical frames; the second frame of the pair carries
 * the same BCD digits as the first plus a field-mark bit (ST 12-1 §12)
 * and is transmitted as ATC_VITC2 to disambiguate it from the first
 * frame's ATC_VITC1 packet.
 *
 * Pair-rate carriage is bandwidth-friendly (the wire format is
 * unchanged from base-rate ATC, only the FF wire slots are
 * reinterpreted as a pair index) and is the only standards-compliant
 * carriage at these rates for receivers that don't grok ST 12-3.
 */
constexpr bool ancAtcIsPairHfrRate(uint32_t frameRateFps) {
        return frameRateFps == 48u || frameRateFps == 50u || frameRateFps == 60u;
}

/**
 * @brief Returns @c true when @p frameRateFps is an ST 12-3 HFRTC rate
 *        (≥72 fps progressive) where the only conformant carriage is
 *        ATC_HFRTC (SDID=0x61) per ST 12-3 §6.
 *
 * HFRTC rates are 72, 96, 100, 119.88 and 120 fps (digit family fps
 * 72/96/100/120 — 119.88 reports as 120 from @c Timecode::fps because
 * the digit family is 30×4).  These rates can't be carried by
 * ATC_VITC1/VITC2 alternation because the super-frame group size N
 * exceeds 2; a frame-mark bit alone (one bit of phase) can't
 * disambiguate the N>2 sub-frames.
 *
 * Receivers that need per-physical-frame timecode at these rates must
 * use the ST 12-3 ATC_HFRTC carriage (separate codec).
 */
constexpr bool ancAtcIsHfrtcRate(uint32_t frameRateFps) {
        return frameRateFps == 72u || frameRateFps == 96u || frameRateFps == 100u ||
               frameRateFps == 120u;
}

/**
 * @brief Returns @c true when @p frameRateFps is in the ST 12-3 HFR
 *        range (>30 fps progressive) — the disjunction of
 *        @ref ancAtcIsPairHfrRate and @ref ancAtcIsHfrtcRate.
 *
 * Kept for callers that don't care which carriage path applies; new
 * code should pick the more specific predicate so the
 * pair-vs-HFRTC distinction is explicit at the call site.
 */
constexpr bool ancAtcIsHfrRate(uint32_t frameRateFps) {
        return ancAtcIsPairHfrRate(frameRateFps) || ancAtcIsHfrtcRate(frameRateFps);
}

/**
 * @brief Thin ATC envelope: a wall-clock @ref Timecode plus the
 *        ATC-specific DBB1 / DBB2 bytes.
 * @ingroup proav
 *
 * As of the ATC audit (Phase 4) the binary-group user bits, the
 * BGF0/BGF1/BGF2 flags and the color-frame flag are owned by the
 * wall-clock @ref Timecode itself — they're part of the ST 12-1
 * §8 time-address word, not the ATC envelope.  See
 * @ref Timecode::colorFrame and @ref Timecode::userbits.  The
 * polarity-correction bit is computed during LTC pack
 * (libvtc / ST 12-1 §9.2.3) and is not user-visible.
 *
 * What @c AncAtc still carries:
 *  - the @ref Timecode value;
 *  - the DBB1 payload-type byte (one of @ref Ltc / @ref Vitc1 /
 *    @ref Vitc2, or 0x80+bitstream for ST 12-3 ATC_HFRTC);
 *  - the DBB2 status byte (raw @c uint8_t — its bit layout is
 *    payload-type-dependent, decode/encode via the dbb2* static
 *    helpers).
 *
 * The class is plain-value; copies are independent.
 *
 * @par Example
 * @code
 * Timecode tc(Timecode::NDF30, 1, 0, 0, 0);
 * tc.setColorFrame(true);
 * tc.setUserbits(TimecodeUserbits::fromAsciiChars("TAKE"));
 *
 * AncAtc atc(tc);
 * atc.setDbb2(AncAtc::dbb2EncodeVitc(11, false, true, true));
 * @endcode
 *
 * @par Thread Safety
 * Plain value type.  Distinct instances may be used concurrently.
 */
class AncAtc {
        public:
                PROMEKI_DATATYPE(AncAtc, DataTypeAncAtc, 2)

                /**
                 * @brief ATC payload-type byte (DBB1) per ST 12-2 / ST 12-3.
                 *
                 * Stamped on UDWs 1..8 bit 3 LSB-first by the codec.
                 * ST 12-2 §5 assigns DID=0x60, SDID=0x60 to all three ST 12-2
                 * flavours (LTC / VITC1 / VITC2) — DBB1 is the only on-wire
                 * discriminator.  ST 12-3 ATC_HFRTC uses SDID=0x61 with
                 * DBB1 = 0x80..0x8F where the low nibble is the bitstream
                 * number (§10.1).  Values beyond the named entries are
                 * round-tripped as raw @c uint8_t so reserved future
                 * assignments survive a parse → build cycle.
                 */
                enum PayloadType : uint8_t {
                        Ltc       = 0x00, ///< Linear time code (ATC_LTC).
                        Vitc1     = 0x01, ///< Vertical interval time code #1 (ATC_VITC1).
                        Vitc2     = 0x02, ///< Vertical interval time code #2 (ATC_VITC2).
                        HfrtcBase = 0x80, ///< ST 12-3 ATC_HFRTC, bitstream 0 (range 0x80..0x8F).
                };

                /** @brief Decoded ST 12-2 DBB2 fields. */
                struct VitcDbb2 {
                                uint8_t line       = 0; ///< VITC line-select (bits 0-4, 0-31).
                                bool    duplicate  = false; ///< Line-duplication flag (bit 5).
                                bool    valid      = true;  ///< Time-code validity (bit 6 == 0).
                                bool    processed  = true;  ///< Process bit (bit 7 == 0).
                };

                /** @brief Decoded ST 12-3 DBB2 fields. */
                struct HfrtcDbb2 {
                                uint8_t superFrameCount = 0; ///< Bits 5-6: super-frame count (0=24, 1=25, 2=30).
                                uint8_t n               = 0; ///< Bits 0-4: ST 12-3 N value (3, 4 or 5).
                };

                /** @brief Default-constructs an empty ATC value (zero Timecode, Ltc payload, dbb2=0). */
                AncAtc() = default;

                /** @brief Constructs from a wall-clock @ref Timecode; DBB2 + payload-type default to zero/Ltc. */
                explicit AncAtc(const Timecode &tc) : _tc(tc) {}

                // -- Timecode --------------------------------------------

                /** @brief Returns the wall-clock @ref Timecode value. */
                const Timecode &timecode() const { return _tc; }

                /** @brief Replaces the wall-clock @ref Timecode value. */
                void setTimecode(const Timecode &tc) { _tc = tc; }

                // -- Payload type (DBB1) ---------------------------------

                /**
                 * @brief Returns the ATC payload-type byte (DBB1).
                 *
                 * Stamped from the wire DBB1 on parse; honoured on build when a
                 * build wrapper does not override it.  Values beyond the named
                 * @ref PayloadType entries round-trip as raw @c uint8_t.
                 */
                uint8_t payloadType() const { return _payloadType; }

                /** @brief Replaces the DBB1 payload-type byte. */
                void setPayloadType(uint8_t v) { _payloadType = v; }

                /** @brief Returns @c true when @ref payloadType is an ST 12-3 ATC_HFRTC value
                 *         (range 0x80..0x8F). */
                bool isHfrtcPayload() const {
                        return _payloadType >= 0x80u && _payloadType <= 0x8Fu;
                }

                /** @brief Returns the ATC_HFRTC bitstream number (low nibble of @ref payloadType),
                 *         or 0 when @ref payloadType is not an HFRTC value. */
                uint8_t hfrtcBitstream() const {
                        return isHfrtcPayload() ? static_cast<uint8_t>(_payloadType & 0x0Fu) : uint8_t{0};
                }

                // -- DBB2 ------------------------------------------------

                /**
                 * @brief Returns the DBB2 byte (UDW 9..16 b3, LSB-first across the eight UDWs).
                 *
                 * Bit semantics depend on @ref payloadType.  For LTC/VITC1/VITC2 see ST 12-2
                 * Table 3 (decode via @ref dbb2DecodeVitc).  For ATC_HFRTC see ST 12-3 §9.2.2
                 * (decode via @ref dbb2DecodeHfrtc).
                 */
                uint8_t dbb2() const { return _dbb2; }

                /** @brief Replaces the DBB2 byte. */
                void setDbb2(uint8_t v) { _dbb2 = v; }

                /**
                 * @brief Decodes an ST 12-2 DBB2 byte (Table 3).
                 *
                 *  - bits 0-4: VITC line-select
                 *  - bit 5:    VITC line-duplication
                 *  - bit 6:    0 = valid, 1 = interpolated
                 *  - bit 7:    0 = processed, 1 = retransmitted
                 */
                static VitcDbb2 dbb2DecodeVitc(uint8_t b) {
                        VitcDbb2 r;
                        r.line      = static_cast<uint8_t>(b & 0x1Fu);
                        r.duplicate = (b & 0x20u) != 0u;
                        r.valid     = (b & 0x40u) == 0u;
                        r.processed = (b & 0x80u) == 0u;
                        return r;
                }

                /** @brief Inverse of @ref dbb2DecodeVitc. */
                static uint8_t dbb2EncodeVitc(uint8_t line, bool duplicate, bool valid, bool processed) {
                        uint8_t b = static_cast<uint8_t>(line & 0x1Fu);
                        if (duplicate) b = static_cast<uint8_t>(b | 0x20u);
                        if (!valid) b = static_cast<uint8_t>(b | 0x40u);
                        if (!processed) b = static_cast<uint8_t>(b | 0x80u);
                        return b;
                }

                /**
                 * @brief Decodes an ST 12-3 ATC_HFRTC DBB2 byte (§9.2.2).
                 *
                 *  - bits 0-4: N (3, 4 or 5)
                 *  - bits 5-6: super-frame count (0=24, 1=25, 2=30)
                 *  - bit  7:   reserved (0)
                 */
                static HfrtcDbb2 dbb2DecodeHfrtc(uint8_t b) {
                        HfrtcDbb2 r;
                        r.n               = static_cast<uint8_t>(b & 0x1Fu);
                        r.superFrameCount = static_cast<uint8_t>((b >> 5) & 0x03u);
                        return r;
                }

                /** @brief Inverse of @ref dbb2DecodeHfrtc. */
                static uint8_t dbb2EncodeHfrtc(uint8_t superFrameCount, uint8_t n) {
                        return static_cast<uint8_t>(((superFrameCount & 0x03u) << 5) | (n & 0x1Fu));
                }

                // -- Comparison ------------------------------------------

                /** @brief Field-wise equality. */
                bool operator==(const AncAtc &o) const {
                        return _tc == o._tc && _payloadType == o._payloadType && _dbb2 == o._dbb2;
                }

                /** @brief Inequality. */
                bool operator!=(const AncAtc &o) const { return !(*this == o); }

                // -- Diagnostics -----------------------------------------

                /** @brief Returns a short human-readable summary. */
                String toString() const;

                /** @brief Returns a structured JSON representation. */
                JsonObject toJson() const;

                // -- DataStream ------------------------------------------

                /** @brief Writes the canonical wire body. */
                Error writeToStream(DataStream &s) const;

                /** @brief Reads the canonical wire body for wire version @p V. */
                template <uint32_t V> static Result<AncAtc> readFromStream(DataStream &s);

                /**
                 * @brief Returns the ATC carriage @ref AncFormat::ID for
                 *        physical frame @p frameIndex at source rate
                 *        @p frameRateFps, per ST 12-2:2014 Am1 §9.2 /
                 *        ST 12-1 §12.
                 *
                 * Only meaningful at base rates (≤30 fps) and pair-rate
                 * HFR (48/50/60); at HFRTC rates (≥72 fps; see
                 * @ref ancAtcIsHfrtcRate) the conformant carriage is
                 * ATC_HFRTC and a VITC1/VITC2 result is informational
                 * only.  Callers at HFRTC rates should use the AtcHfrtc
                 * build path instead.
                 *
                 *  - Base rates (≤30 fps): always returns @c AtcVitc1
                 *    (one packet per physical frame).
                 *  - Pair-rate HFR (48/50/60): alternates
                 *    @c AtcVitc1 (even frames) and @c AtcVitc2 (odd
                 *    frames) so the receiver can reconstruct the
                 *    pair-index + field-mark sequence.
                 *  - HFRTC rates (72/96/100/120): returns
                 *    @c AtcVitc1 / @c AtcVitc2 alternation matching the
                 *    pair-rate contract, but this is informational —
                 *    those rates require ATC_HFRTC carriage.
                 *
                 * Returns @c int (the @c AncFormat::ID enum value) to keep this
                 * header free of an @c ancformat.h include.
                 */
                static int atcVitcFormatForFrame(uint32_t frameRateFps, uint64_t frameIndex);

        private:
                Timecode _tc;
                uint8_t  _payloadType = Ltc;
                uint8_t  _dbb2 = 0;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_PROAV
