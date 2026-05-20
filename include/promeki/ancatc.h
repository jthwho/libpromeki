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
#include <promeki/array.h>
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
 * @brief Returns @c true when @p frameRateFps is in the ST 12-3 HFR
 *        range (>30 fps progressive) where ATC_VITC1 / ATC_VITC2
 *        alternation applies (§6).
 *
 * At progressive frame rates greater than 30 fps, ST 12-3:2016 §6
 * requires the sender to alternate ATC_VITC1 (DBB1=0x01,
 * field-mark=0) on the first physical frame of each frame-pair and
 * ATC_VITC2 (DBB1=0x02, field-mark=1) on the second.  At standard
 * rates (≤ 30 fps progressive, or any interlaced rate) ATC_VITC1
 * carries every frame.  Callers use this helper plus the absolute
 * physical frame index to pick the right @c AncFormat::AtcVitc1 /
 * @c AncFormat::AtcVitc2 build entry for each frame.
 */
constexpr bool ancAtcIsHfrRate(uint32_t frameRateFps) {
        return frameRateFps > 30u;
}

/**
 * @brief Full SMPTE ST 12-1 / ST 12-2 ancillary-timecode value.
 * @ingroup proav
 *
 * Captures every field an ATC packet round-trips through the wire — not
 * just the time-address digits.  The library's @ref Timecode is a
 * libvtc-backed wall-clock type that models the four hour/min/sec/frame
 * digits plus drop-frame mode; ATC carriage adds eight 4-bit user-bit
 * nibbles, the color-frame and polarity flags, the three binary-group
 * flags (BGF0 / BGF1 / BGF2), and the DBB2 status byte
 * (VITC line-select / duplicate / validity / process).  Stuffing those
 * into @ref Timecode would bloat a shared value type the entire
 * pipeline already depends on; the project pattern is to introduce a
 * specialised value type for the specialised carriage, in the same
 * spirit as @ref Cea708Cdp / @ref HdrStaticMetadata.
 *
 * The class is plain-value: copies are independent, no internal shared
 * pointer.  Distinct instances may be used concurrently.
 *
 * @par Wire mapping (ST 12-2 §6 Table 6, indexed by UDW 1..16)
 *
 *  - UDW 1, 3, 5, 7, 9, 11, 13, 15 (odd, 1-indexed): the eight 4-bit
 *    time-address nibbles plus the flag bits described below.
 *  - UDW 2, 4, 6, 8, 10, 12, 14, 16 (even, 1-indexed): the eight
 *    4-bit binary-group nibbles
 *    (@ref userBits indices 0..7 = UDWs 2/4/6/8/10/12/14/16).
 *  - UDW 3 b7  — @ref ColorFrame flag.
 *  - UDW 7 b7  — @ref Polarity flag (LTC polarity correction; for
 *    25-Hz systems doubles as BGF0 per ST 12-1 Table 12).
 *  - UDW 11 b7 — @ref Bgf0 flag (NDF / 30-Hz rates) or @ref Polarity
 *    (25-Hz rates).
 *  - UDW 15 b6 — @ref Bgf1 flag.
 *  - UDW 15 b7 — @ref Bgf2 flag.
 *  - UDW 1..8 b3 (LSB-first across the eight UDWs) — DBB1, the
 *    payload-type byte (0x00 = LTC, 0x01 = VITC1, 0x02 = VITC2);
 *    stamped by the ATC codec from the resolved @ref AncFormat::ID,
 *    not by @c AncAtc itself.
 *  - UDW 9..16 b3 (LSB-first across the eight UDWs) — @ref dbb2,
 *    the VITC line-select / duplicate-flag / validity-bit /
 *    process-bit byte.
 *
 * The flag fields default to zero, matching the ST 12-1 §9.2.2
 * "Unused flag bits shall be set to 0 by original sources and ignored
 * by receiving equipment" allowance.  Captured ATC packets reproduce
 * their original flag bytes byte-exact on round trip.
 *
 * @par Example
 *
 * @code
 * AncAtc atc(Timecode(Timecode::NDF30, 1, 0, 0, 0));
 * atc.setUserBit(0, 0xA);            // first 4-bit user-bit nibble
 * atc.setColorFrame(true);
 * atc.setDbb2(0x0B);                 // VITC line-select 11
 *
 * Variant v(atc);
 * @endcode
 *
 * @par Thread Safety
 * Plain value type.  Copies are independent and may be used
 * concurrently; concurrent mutation of a single instance is not
 * synchronised.
 *
 * @see AncFormat::AtcLtc, AncFormat::AtcVitc1, AncFormat::AtcVitc2,
 *      AncMeta::Atc::Rate
 */
class AncAtc {
        public:
                PROMEKI_DATATYPE(AncAtc, DataTypeAncAtc, 1)

                /// @brief Number of 4-bit binary-group nibbles carried by
                ///        an ATC packet (eight per ST 12-2 §6.2 Table 6).
                static constexpr size_t UserBitCount = 8;

                /// @brief Array of the eight binary-group nibbles, each
                ///        masked to its low 4 bits.  Index @c i maps to
                ///        UDW @c (2*(i+1)) per Table 6.
                using UserBits = ::promeki::Array<uint8_t, UserBitCount>;

                /**
                 * @brief ATC flag bits per ST 12-1 §9 / ST 12-2 §6.
                 *
                 * Bit positions are arbitrary inside the @ref flags()
                 * byte; only the wire mapping in @ref AncAtc's class
                 * doc is normative.  The values are stable so callers
                 * can store @c flags() blindly.
                 */
                enum Flag : uint8_t {
                        ColorFrame = 0x01, ///< UDW 3 bit 7 — colour-frame identification.
                        Polarity   = 0x02, ///< UDW 7 bit 7 — LTC polarity correction (25-Hz: BGF0).
                        Bgf0       = 0x04, ///< UDW 11 bit 7 — binary-group flag 0 (NDF / 30-Hz rates).
                        Bgf1       = 0x08, ///< UDW 15 bit 6 — binary-group flag 1.
                        Bgf2       = 0x10, ///< UDW 15 bit 7 — binary-group flag 2.
                };

                /** @brief Default-constructs an empty ATC value (all fields zero). */
                AncAtc() = default;

                /** @brief Constructs from a wall-clock @ref Timecode; user-bits / flags / DBB2 default to zero. */
                explicit AncAtc(const Timecode &tc) : _tc(tc) {}

                /** @brief Constructs from a wall-clock @ref Timecode + binary-group nibbles. */
                AncAtc(const Timecode &tc, const UserBits &ub) : _tc(tc), _userBits(ub) {}

                // -- Timecode --------------------------------------------

                /** @brief Returns the wall-clock @ref Timecode value. */
                const Timecode &timecode() const { return _tc; }

                /** @brief Replaces the wall-clock @ref Timecode value. */
                void setTimecode(const Timecode &tc) { _tc = tc; }

                // -- User bits -------------------------------------------

                /** @brief Returns the eight binary-group nibbles (low 4 bits each). */
                const UserBits &userBits() const { return _userBits; }

                /** @brief Replaces all eight binary-group nibbles. */
                void setUserBits(const UserBits &ub) { _userBits = ub; }

                /**
                 * @brief Returns binary-group nibble @p i (0..7); out-of-range returns 0.
                 *
                 * @param i Index in [0, @ref UserBitCount).
                 */
                uint8_t userBit(size_t i) const {
                        return i < UserBitCount ? _userBits[i] : uint8_t(0);
                }

                /**
                 * @brief Sets binary-group nibble @p i to the low 4
                 *        bits of @p nibble.
                 *
                 * @param i      Index in [0, @ref UserBitCount).
                 * @param nibble Value whose low 4 bits replace nibble @p i.
                 */
                void setUserBit(size_t i, uint8_t nibble) {
                        if (i < UserBitCount) {
                                _userBits[i] = static_cast<uint8_t>(nibble & 0x0F);
                        }
                }

                // -- Flags -----------------------------------------------

                /** @brief Returns the raw flag byte (bitwise OR of @ref Flag values). */
                uint8_t flags() const { return _flags; }

                /** @brief Replaces the raw flag byte. */
                void setFlags(uint8_t f) { _flags = f; }

                /** @brief Returns @c true when @p f is set in @ref flags. */
                bool hasFlag(Flag f) const { return (_flags & static_cast<uint8_t>(f)) != 0; }

                /** @brief Sets (@p on=true) or clears (@p on=false) flag @p f. */
                void setFlag(Flag f, bool on) {
                        if (on) _flags = static_cast<uint8_t>(_flags | static_cast<uint8_t>(f));
                        else _flags = static_cast<uint8_t>(_flags & ~static_cast<uint8_t>(f));
                }

                /** @name Per-flag convenience accessors */
                /// @{
                bool colorFrame() const { return hasFlag(ColorFrame); }
                void setColorFrame(bool on) { setFlag(ColorFrame, on); }
                bool polarity() const { return hasFlag(Polarity); }
                void setPolarity(bool on) { setFlag(Polarity, on); }
                bool bgf0() const { return hasFlag(Bgf0); }
                void setBgf0(bool on) { setFlag(Bgf0, on); }
                bool bgf1() const { return hasFlag(Bgf1); }
                void setBgf1(bool on) { setFlag(Bgf1, on); }
                bool bgf2() const { return hasFlag(Bgf2); }
                void setBgf2(bool on) { setFlag(Bgf2, on); }
                /// @}

                // -- DBB2 ------------------------------------------------

                /**
                 * @brief Returns the DBB2 byte (UDW 9..16 b3, LSB-first).
                 *
                 * Bit assignments per ST 12-2 Table 3:
                 *  - bits 0..4: VITC line-select
                 *  - bit 5: VITC line-duplication flag
                 *  - bit 6: time-code validity bit
                 *  - bit 7: user-bits process bit
                 *
                 * Per §6.2.2 all bits may be zero for HD digital
                 * applications; captured packets reproduce their
                 * original DBB2 byte verbatim.
                 */
                uint8_t dbb2() const { return _dbb2; }

                /** @brief Replaces the DBB2 byte. */
                void setDbb2(uint8_t v) { _dbb2 = v; }

                // -- Comparison ------------------------------------------

                /** @brief Field-wise equality. */
                bool operator==(const AncAtc &o) const {
                        return _tc == o._tc && _userBits == o._userBits &&
                               _flags == o._flags && _dbb2 == o._dbb2;
                }

                /** @brief Inequality. */
                bool operator!=(const AncAtc &o) const { return !(*this == o); }

                // -- Diagnostics -----------------------------------------

                /** @brief Returns a short human-readable summary (timecode + non-default extras). */
                String toString() const;

                /** @brief Returns a structured JSON representation (used by InspectorMediaIO). */
                JsonObject toJson() const;

                // -- DataStream ------------------------------------------

                /** @brief Writes the canonical wire body via @ref PROMEKI_DATATYPE. */
                Error writeToStream(DataStream &s) const;

                /** @brief Reads the canonical wire body for wire version @p V. */
                template <uint32_t V> static Result<AncAtc> readFromStream(DataStream &s);

                /**
                 * @brief Returns the ATC carriage @ref AncFormat::ID for
                 *        physical frame @p frameIndex at source rate
                 *        @p frameRateFps, per ST 12-3:2016 §6.
                 *
                 * Encodes the HFR alternation rule the audit calls out as
                 * D1f:
                 *  - At @p frameRateFps ≤ 30 (or interlaced), every
                 *    frame uses @c AncFormat::AtcVitc1.
                 *  - At HFR (>30 fps progressive), the first physical
                 *    frame of each frame-pair (@c frameIndex even)
                 *    uses @c AtcVitc1 (DBB1=0x01, field-mark=0); the
                 *    second (@c frameIndex odd) uses @c AtcVitc2
                 *    (DBB1=0x02, field-mark=1).
                 *
                 * Returns a plain @c int (the @c AncFormat::ID enum
                 * value).  Callers cast back to @c AncFormat::ID at the
                 * use site to keep this header free of an
                 * @c ancformat.h include — avoiding a circular
                 * dependency since @c ancformat.h depends on
                 * @c enums.h which depends on this header indirectly.
                 *
                 * @param frameRateFps Source frame rate in whole fps.
                 * @param frameIndex   Absolute physical frame index (0-based).
                 * @return The @c AncFormat::ID enum value to use for
                 *         this frame's ATC packet.
                 */
                static int atcVitcFormatForFrame(uint32_t frameRateFps, uint64_t frameIndex);

        private:
                Timecode _tc;
                UserBits _userBits{};
                uint8_t  _flags = 0;
                uint8_t  _dbb2 = 0;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_PROAV
