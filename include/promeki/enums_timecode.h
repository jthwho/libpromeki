/**
 * @file      enums_timecode.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Timecode BCD pack-format enums.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_CORE
#include <promeki/namespace.h>
#include <promeki/enum.h>

PROMEKI_NAMESPACE_BEGIN

/** @addtogroup wellknownenums */
/** @{ */

/**
 * @brief Selects the on-the-wire BCD time-address layout used by
 *        @ref Timecode::toBcd64 and @ref Timecode::fromBcd64.
 *
 * The 64-bit BCD packing carries the eight BCD time digits, the binary
 * groups (32 bits of user bits), the drop-frame flag, the color-frame
 * flag, and the binary group flags BGF0/BGF1/BGF2 — i.e. the same set
 * of fields that SMPTE 12M-1 (LTC) and SMPTE 12M-2 (VITC) carry in
 * their respective time-address fields, minus the wire-level framing
 * (sync words, biphase mark transitions, CRC).  The two variants below
 * differ in how a single bit position — bit 27, "the bit immediately
 * above the 3-bit seconds-tens field" — is interpreted, since SMPTE
 * 12M-1 and 12M-2 disagree on its meaning:
 *
 * | Bit 27 | LTC (SMPTE 12M-1)              | VITC (SMPTE 12M-2)         |
 * |--------|--------------------------------|----------------------------|
 * | Name   | Polarity correction (or BGF0)  | Field marker               |
 * | Source | Computed by libvtc to balance  | Sourced from               |
 * |        | the codeword's 0/1 count       | @ref Timecode::isFirstField |
 *
 * In @c Ltc mode, @ref Timecode::toBcd64 wraps libvtc's
 * @c vtc_ltc_pack so the polarity correction bit is set correctly per
 * SMPTE 12M-1.  In @c Vitc mode, the packer writes the time digits and
 * binary groups directly into the 64-bit word and uses bit 27 as the
 * field marker — which doubles as the HFR frame-pair identifier per
 * SMPTE 12M-2 / 12-3.
 *
 * The two variants are byte-identical for "well-behaved" timecodes
 * (no field marker, no userbits, balanced 0/1 counts) — the variant
 * mostly chooses *who computes* the auxiliary bits and which spec the
 * decoder should consult to interpret them.
 *
 * Default is @c TimecodePackFormat::Vitc, since the typical libpromeki use case is
 * stamping a frame's identity (including the HFR field/pair bit) into
 * the image itself, where there is no biphase mark to balance.
 */
class TimecodePackFormat : public TypedEnum<TimecodePackFormat> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("TimecodePackFormat", "Timecode Pack Format", 0,
                                                   {"Vitc", 0, "VITC (SMPTE 12M-2)"},
                                                   {"Ltc", 1, "LTC (SMPTE 12M-1)"}); // default: Vitc

                using TypedEnum<TimecodePackFormat>::TypedEnum;

                static const TimecodePackFormat Vitc;
                static const TimecodePackFormat Ltc;
};

inline const TimecodePackFormat TimecodePackFormat::Vitc{0};
inline const TimecodePackFormat TimecodePackFormat::Ltc{1};

/** @} */

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_CORE
