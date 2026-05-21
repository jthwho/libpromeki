/**
 * @file      anccodec_atc.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * SMPTE ST 12-2:2014 ATC (Ancillary Timecode) parsers and builders for
 * the three flavours (LTC, VITC1, VITC2) carried on
 * @c AncTransport::St291.  Decodes / encodes the 16-UDW SMPTE ST 12-2
 * payload to and from an @ref AncAtc — the libvtc-backed @ref Timecode
 * plus the binary-group user-bit nibbles, the colour-frame / polarity
 * flags, the three BGF bits, and the DBB2 status byte that
 * @ref Timecode itself does not model.
 *
 * Wire layout per ST 12-2:2014 §5–6.  Every ATC packet is a Type-2
 * ST 291 packet (ST 12-2 §5 explicit) with DID=0x60, SDID one of
 * 0x60/0x61/0x62 for LTC/VITC1/VITC2, DC=0x10, then 16 UDWs.
 *
 * Each 10-bit UDW returned by @c St291Packet::udw carries:
 *
 *   bits 0-2 — zero (ST 12-2 §6.1.4)
 *   bit  3   — DBB (UDW 1-8 b3 = DBB1 = payload type byte, LSB-first;
 *              UDW 9-16 b3 = DBB2 = VITC line-select / flags)
 *   bits 4-7 — time-code nibble (b4 = LSB) per Table 6
 *   bits 8-9 — parity (computed by the ST 291 layer)
 *
 * Time-code / flag nibble UDW assignments (Table 6, 1-indexed UDW):
 *
 *   UDW  1: Units of frames        (LTC bits 0-3)
 *   UDW  3: Tens of frames (b4-5), DF (b6), Color Frame (b7)
 *   UDW  5: Units of seconds                              (LTC 16-19)
 *   UDW  7: Tens of seconds (b4-6), Polarity (b7)
 *   UDW  9: Units of minutes                              (LTC 32-35)
 *   UDW 11: Tens of minutes (b4-6), BGF0 (b7)
 *   UDW 13: Units of hours                                (LTC 48-51)
 *   UDW 15: Tens of hours (b4-5), BGF1 (b6), BGF2 (b7)
 *
 * The remaining even-numbered UDWs (2, 4, 6, 8, 10, 12, 14, 16) carry
 * the eight 4-bit binary-group nibbles (= the @c TimecodeUserbits
 * embedded in the parsed packet's @ref Timecode), preserved verbatim
 * across a parse / build round trip.
 *
 * DBB1 (UDW 1-8 b3, LSB-first; ST 12-2 Table 2):
 *   0x00 = ATC_LTC, 0x01 = ATC_VITC1, 0x02 = ATC_VITC2.
 *
 * DBB2 (UDW 9-16 b3, LSB-first; ST 12-2 Table 3): VITC line-select
 * (b0-b4), VITC line-duplication flag (b5), time-code validity (b6),
 * user-bits process bit (b7).  Round-trips through
 * @ref AncAtc::dbb2.
 *
 * Parse-time rate-hint precedence (per ATC audit decision Q4):
 *   1. @c AncMeta::Atc::Rate on the packet's meta sidecar.  Stamped
 *      by the capture path when the paired video desc is known.
 *   2. @c AncTranslateConfig::AtcParseRateHint — application-level
 *      override for sources where the capture didn't stamp the rate
 *      (raw RTP receive, SDP-only context, file replay).
 *   3. Fail with @c Error::InsufficientContext.  We deliberately do
 *      not silently default to 30 fps — the eight time-code bytes
 *      alone cannot disambiguate 24 / 25 / 30 NDF, so a wrong
 *      default produces quiet wrong answers.
 */

#include <promeki/ancatc.h>
#include <promeki/ancformat.h>
#include <promeki/ancmeta.h>
#include <promeki/ancpacket.h>
#include <promeki/anctranslator.h>
#include <promeki/enums.h>
#include <promeki/error.h>
#include <promeki/list.h>
#include <promeki/result.h>
#include <promeki/st291packet.h>
#include <promeki/timecode.h>
#include <promeki/variant.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

        // -- Wire-layout constants per ST 12-2:2014 ---------------------------

        // DC = 0x10: every ATC packet shall carry exactly 16 UDWs (§5).
        constexpr size_t AtcUdwCount = 16;

        // 0-indexed UDW positions of the eight time-address nibble slots,
        // in spec order.  The interleaved UDWs (1, 3, 5, 7, 9, 11, 13, 15
        // in 1-indexed form) skip over the binary-group UDWs that sit
        // between them.
        constexpr size_t IdxFrameUnits = 0;   // UDW 1
        constexpr size_t IdxFrameTens  = 2;   // UDW 3
        constexpr size_t IdxSecUnits   = 4;   // UDW 5
        constexpr size_t IdxSecTens    = 6;   // UDW 7
        constexpr size_t IdxMinUnits   = 8;   // UDW 9
        constexpr size_t IdxMinTens    = 10;  // UDW 11
        constexpr size_t IdxHourUnits  = 12;  // UDW 13
        constexpr size_t IdxHourTens   = 14;  // UDW 15

        // 0-indexed UDW positions of the eight binary-group nibble
        // slots (UDWs 2, 4, 6, 8, 10, 12, 14, 16 in 1-indexed form).
        // The eight nibbles map to the @ref TimecodeUserbits eight-nibble
        // array on the Timecode itself (Phase 4 audit reshape — the user
        // bits live on Timecode, not on AncAtc).
        constexpr size_t UserBitCount = 8;
        constexpr size_t UserBitIdx[UserBitCount] = {
                1, 3, 5, 7, 9, 11, 13, 15};

        // -- Mode helpers -----------------------------------------------------

        // Map (fps, df-flag) to a libvtc Timecode::Mode the parser
        // stamps on the returned AncAtc.  The eight time-code bytes
        // themselves don't disambiguate 24 / 25 / 30-NDF — only the DF
        // bit narrows 30 → 29.97-DF and 60 → 59.94-DF, so the rate hint
        // (Q4) must come from somewhere external.
        //
        // Explicit pair-HFR cases (48/50/60) are kept here for
        // documentation: Phase 2's Mode(fps, flags) walk already
        // resolves them via the standard-formats table, but spelling
        // them out makes the parser's pair-rate intent obvious at the
        // call site below.
        Timecode::Mode modeForRate(uint32_t fps, bool df) {
                if (fps == 0) return Timecode::Mode();
                if (df && fps == 30) return Timecode::Mode(Timecode::DF30);
                if (df && fps == 60) return Timecode::Mode(Timecode::DF60);   // 59.94 DF
                if (fps == 24) return Timecode::Mode(Timecode::NDF24);
                if (fps == 25) return Timecode::Mode(Timecode::NDF25);
                if (fps == 30) return Timecode::Mode(Timecode::NDF30);
                if (fps == 48) return Timecode::Mode(Timecode::NDF48);
                if (fps == 50) return Timecode::Mode(Timecode::NDF50);
                if (fps == 60) return Timecode::Mode(Timecode::NDF60);
                // Genuine HFRTC rates (72/96/100/120) and custom rates
                // fall through to the standard-formats walk.  Phase 2
                // makes this path produce the right libvtc format
                // pointer for every standard rate; truly custom rates
                // hit vtc_format_find_or_create.
                uint32_t vtcFlags = df ? uint32_t(Timecode::DropFrame) : 0u;
                return Timecode::Mode(fps, vtcFlags);
        }

        // -- Bit helpers ------------------------------------------------------

        // Extracts the 4-bit time-code group from the bits 4-7 of a 10-bit
        // UDW's data byte (the parity bits 8-9 are implicitly masked off
        // by the >> 4 / & 0x0F chain).
        inline uint8_t udwNibble(uint16_t udw) {
                return static_cast<uint8_t>((udw >> 4) & 0x0F);
        }

        // Builds the 10-bit UDW value for a time-code-bearing or binary-
        // group-bearing slot: time-code nibble in bits 4-7, DBB in bit 3,
        // bits 0-2 zero per §6.1.4, parity (bits 8-9) left at zero so
        // St291Packet::build computes it.
        inline uint16_t makeUdw(uint8_t nibble, bool dbb) {
                uint16_t v = static_cast<uint16_t>((nibble & 0x0F) << 4);
                if (dbb) v = static_cast<uint16_t>(v | 0x08);
                return v;
        }


        // -- Parsing ----------------------------------------------------------

        AncTranslator::ParseResult parseAtcSt291Impl(const AncPacket &pkt, const AncTranslateConfig &cfg) {
                Result<St291Packet> rp = St291Packet::from(pkt, cfg.checksumPolicy());
                if (rp.second().isError()) return makeError<Variant>(rp.second());
                const St291Packet &p   = rp.first();
                List<uint16_t>     udw = p.udw();
                // ST 12-2 §5: DC shall be 0x10 → exactly 16 UDWs.
                if (udw.size() < AtcUdwCount) {
                        return makeError<Variant>(Error::CorruptData);
                }

                // Frame digits + DF + CF flags.
                uint8_t frameUnits      = udwNibble(udw[IdxFrameUnits]);
                uint8_t frameTensNibble = udwNibble(udw[IdxFrameTens]);
                uint8_t frameTens       = static_cast<uint8_t>(frameTensNibble & 0x03);
                bool    df              = (frameTensNibble & 0x04) != 0;
                bool    colorFrame      = (frameTensNibble & 0x08) != 0;

                // Second digits + polarity flag.
                uint8_t secUnits      = udwNibble(udw[IdxSecUnits]);
                uint8_t secTensNibble = udwNibble(udw[IdxSecTens]);
                uint8_t secTens       = static_cast<uint8_t>(secTensNibble & 0x07);
                bool    polarity      = (secTensNibble & 0x08) != 0;

                // Minute digits + BGF0 flag.
                uint8_t minUnits      = udwNibble(udw[IdxMinUnits]);
                uint8_t minTensNibble = udwNibble(udw[IdxMinTens]);
                uint8_t minTens       = static_cast<uint8_t>(minTensNibble & 0x07);
                bool    bgf0          = (minTensNibble & 0x08) != 0;

                // Hour digits + BGF1 + BGF2 flags.
                uint8_t hourUnits      = udwNibble(udw[IdxHourUnits]);
                uint8_t hourTensNibble = udwNibble(udw[IdxHourTens]);
                uint8_t hourTens       = static_cast<uint8_t>(hourTensNibble & 0x03);
                bool    bgf1           = (hourTensNibble & 0x04) != 0;
                bool    bgf2           = (hourTensNibble & 0x08) != 0;

                Timecode::DigitType hour = static_cast<Timecode::DigitType>(hourTens * 10 + hourUnits);
                Timecode::DigitType min  = static_cast<Timecode::DigitType>(minTens * 10 + minUnits);
                Timecode::DigitType sec  = static_cast<Timecode::DigitType>(secTens * 10 + secUnits);
                Timecode::DigitType frm  = static_cast<Timecode::DigitType>(frameTens * 10 + frameUnits);

                // Rate hint precedence (audit Q4): per-packet meta wins,
                // then cfg, then hard fail.  Silent 30-fps default is the
                // worst option — produces wrong-rate Timecode silently.
                uint32_t metaRate = pkt.meta().get(AncMeta::Atc::Rate).get<uint32_t>();
                uint32_t cfgHint  = cfg.getAs<uint32_t>(AncTranslateConfig::AtcParseRateHint, uint32_t(0));
                uint32_t fps      = metaRate != 0 ? metaRate : cfgHint;
                if (fps == 0) {
                        return makeError<Variant>(Error::InsufficientContext);
                }

                // Pair-rate HFR (ST 12-1 §12 / ST 12-2 §9.2 Am1:2013):
                // at 48/50/60 fps the FF wire slots carry pair_index
                // (0..fps/2-1) and the polarity bit slot (UDW 7 bit 7,
                // = sub-frame_1 in ST 12-1 §12) carries the field-mark.
                // The physical frame is reconstructed as
                //   physical_frame = pair_index * 2 + field_mark.
                // Re-uses the "polarity" bit we decoded above as the
                // field-mark per the ST 12-2 Am1 reinterpretation —
                // the pre-Am1 meaning (LTC polarity) is non-sensical
                // for ATC anyway (ATC has no biphase mark to
                // polarity-correct).
                if (ancAtcIsPairHfrRate(fps)) {
                        uint32_t pairIndex = static_cast<uint32_t>(frm);
                        uint32_t fieldMark = polarity ? 1u : 0u;
                        frm = static_cast<Timecode::DigitType>(pairIndex * 2u + fieldMark);
                }

                Timecode tc(modeForRate(fps, df), hour, min, sec, frm);

                // Phase 4: BGF + color-frame + user bits ride on the
                // Timecode now, not on the AncAtc envelope.  Polarity
                // (UDW 7 b7) is not surfaced — at non-pair-rates the
                // captured bit has no semantic meaning (libvtc
                // recomputes the LTC polarity-correction bit at pack
                // time per ST 12-1 §9.2.3, and ATC itself has no
                // biphase mark to polarity-correct).  At pair-rate HFR
                // the same wire bit is reinterpreted as the ST 12-2
                // Am1 §9.2 field-mark and consumed above to recover
                // the physical-frame index.
                tc.setColorFrame(colorFrame);
                TimecodeUserbits::Nibbles ubNibs{};
                for (size_t i = 0; i < UserBitCount; ++i) {
                        ubNibs[i] = udwNibble(udw[UserBitIdx[i]]);
                }
                uint8_t modeBits = 0;
                if (bgf0) modeBits |= 0x01u;
                if (bgf1) modeBits |= 0x02u;
                if (bgf2) modeBits |= 0x04u;
                tc.setUserbits(TimecodeUserbits::fromNibbles(
                        ubNibs, static_cast<TimecodeUserbits::Mode>(modeBits)));

                AncAtc atc(tc);

                // DBB1: UDW 1..8 b3, LSB-first across the eight UDWs.
                // This is the §6.2.1 payload-type byte that discriminates
                // ATC_LTC / ATC_VITC1 / ATC_VITC2 — ST 12-2:2014 §5
                // assigns DID=0x60 / SDID=0x60 to all three flavours, so
                // DBB1 is the only on-wire signal for the flavour.
                uint8_t dbb1 = 0;
                for (size_t i = 0; i < 8; ++i) {
                        if ((udw[i] & 0x08u) != 0) {
                                dbb1 = static_cast<uint8_t>(dbb1 | (1u << i));
                        }
                }
                atc.setPayloadType(dbb1);

                // DBB2: UDW 9..16 b3, LSB-first across the eight UDWs.
                uint8_t dbb2 = 0;
                for (size_t i = 0; i < 8; ++i) {
                        if ((udw[8 + i] & 0x08u) != 0) {
                                dbb2 = static_cast<uint8_t>(dbb2 | (1u << i));
                        }
                }
                atc.setDbb2(dbb2);

                return makeResult<Variant>(Variant(atc));
        }

        // -- Building ---------------------------------------------------------

        // Packs an AncAtc's BCD digits, flag bits, binary-group nibbles,
        // and DBB2 byte plus the DBB1 payload-type byte into the 16-UDW
        // SMPTE ST 12-2 layout.  Factored so the Variant-driven build
        // entry point and the SyncPolicy re-encode path (Repeat[idx>0])
        // share one source of truth for the wire bit-twiddling.  DBB1
        // is taken from @c atc.payloadType() — the format-keyed build
        // wrappers below stamp that field before calling here so a
        // caller's explicit @c AncFormat::AtcLtc / @c AtcVitc1 /
        // @c AtcVitc2 choice wins over any stale value rooted in the
        // value type.
        //
        // @p legacyFieldMark forces the ST 12-2 §9.2 field-mark bit
        // to 0 at pair-rate HFR — see AncTranslateConfig::
        // AtcVitcLegacyFieldMark for the rationale.  Has no effect at
        // non-pair-rates (the polarity slot is already always zero
        // at those rates).
        List<uint16_t> packAtcUdw(const AncAtc &atc, bool legacyFieldMark) {
                const Timecode &tc       = atc.timecode();
                uint8_t         hour     = static_cast<uint8_t>(tc.hour());
                uint8_t         min      = static_cast<uint8_t>(tc.min());
                uint8_t         sec      = static_cast<uint8_t>(tc.sec());
                bool            df       = tc.isDropFrame();
                uint8_t         dbb1     = atc.payloadType();
                uint8_t         dbb2     = atc.dbb2();

                // Pair-rate HFR (ST 12-1 §12 / ST 12-2 §9.2 Am1:2013):
                // the FF wire slots carry pair_index (= super-frame
                // index, 0..fps/2-1) rather than the raw physical
                // frame, and the polarity bit slot becomes the
                // field-mark.  See parseAtcSt291Impl for the inverse.
                //
                // At non-pair-rates @c frm carries the raw frame
                // digit (0..fps-1 at base rates) and @c fieldMark is
                // forced to zero — both libvtc-side LTC polarity
                // computation and the ATC informational slot keep the
                // bit clear.
                const bool isPair    = ancAtcIsPairHfrRate(tc.fps());
                uint8_t    frm       = isPair
                                              ? static_cast<uint8_t>(tc.superFrameIndex())
                                              : static_cast<uint8_t>(tc.frame());
                bool       fieldMark = false;
                if (isPair && !legacyFieldMark) {
                        fieldMark = (tc.subFrameIndex() & 1u) != 0u;
                }

                // Phase 4: BGF + color-frame + user bits ride on the
                // Timecode now.  Polarity is left at zero on the wire
                // at non-pair-rates — libvtc recomputes it during LTC
                // pack; ATC is informational so the bit has no semantic
                // meaning there.
                const bool colorFrame = tc.colorFrame();
                const uint8_t bgfMode = static_cast<uint8_t>(tc.userbits().mode());
                const bool bgf0 = (bgfMode & 0x01u) != 0u;
                const bool bgf1 = (bgfMode & 0x02u) != 0u;
                const bool bgf2 = (bgfMode & 0x04u) != 0u;
                const TimecodeUserbits::Nibbles &ub = tc.userbits().nibbles();

                // UDW 1-8 (indices 0-7) carry DBB1 bit i LSB-first per §6.2.1;
                // UDW 9-16 (indices 8-15) carry DBB2 bit (i-8) LSB-first
                // per §6.2.2.
                auto dbbBit = [&](size_t i) -> bool {
                        if (i < 8) return ((dbb1 >> i) & 1u) != 0;
                        return ((dbb2 >> (i - 8)) & 1u) != 0;
                };

                // Initialise all 16 slots with their DBB bit and an empty
                // nibble.  Binary-group slots get their user-bit nibble
                // below; time-address slots get overwritten with the
                // proper nibble.
                List<uint16_t> udw;
                udw.resize(AtcUdwCount);
                for (size_t i = 0; i < AtcUdwCount; ++i) udw[i] = makeUdw(0, dbbBit(i));

                // Time-address tens nibbles carry the flag bits in their
                // upper 2 bits per ST 12-2 Table 6.
                uint8_t frameTensNibble = static_cast<uint8_t>((frm / 10) & 0x03);
                if (df) frameTensNibble = static_cast<uint8_t>(frameTensNibble | 0x04);
                if (colorFrame) frameTensNibble = static_cast<uint8_t>(frameTensNibble | 0x08);
                uint8_t secTensNibble = static_cast<uint8_t>((sec / 10) & 0x07);
                // bit 3 of secTens is the polarity slot — at non-pair-rates
                // this is left clear (libvtc handles polarity on the LTC
                // wire and ATC carries no biphase mark).  At pair-rate
                // HFR (48/50/60) the same bit is repurposed as the
                // ST 12-2 Am1 §9.2 field-mark.
                if (fieldMark) secTensNibble = static_cast<uint8_t>(secTensNibble | 0x08);
                uint8_t minTensNibble = static_cast<uint8_t>((min / 10) & 0x07);
                if (bgf0) minTensNibble = static_cast<uint8_t>(minTensNibble | 0x08);
                uint8_t hourTensNibble = static_cast<uint8_t>((hour / 10) & 0x03);
                if (bgf1) hourTensNibble = static_cast<uint8_t>(hourTensNibble | 0x04);
                if (bgf2) hourTensNibble = static_cast<uint8_t>(hourTensNibble | 0x08);

                udw[IdxFrameUnits] = makeUdw(static_cast<uint8_t>(frm % 10), dbbBit(IdxFrameUnits));
                udw[IdxFrameTens]  = makeUdw(frameTensNibble, dbbBit(IdxFrameTens));
                udw[IdxSecUnits]   = makeUdw(static_cast<uint8_t>(sec % 10), dbbBit(IdxSecUnits));
                udw[IdxSecTens]    = makeUdw(secTensNibble, dbbBit(IdxSecTens));
                udw[IdxMinUnits]   = makeUdw(static_cast<uint8_t>(min % 10), dbbBit(IdxMinUnits));
                udw[IdxMinTens]    = makeUdw(minTensNibble, dbbBit(IdxMinTens));
                udw[IdxHourUnits]  = makeUdw(static_cast<uint8_t>(hour % 10), dbbBit(IdxHourUnits));
                udw[IdxHourTens]   = makeUdw(hourTensNibble, dbbBit(IdxHourTens));

                // Binary-group nibbles into the even-1-indexed UDW slots.
                for (size_t i = 0; i < UserBitCount; ++i) {
                        udw[UserBitIdx[i]] = makeUdw(static_cast<uint8_t>(ub[i] & 0x0F),
                                                     dbbBit(UserBitIdx[i]));
                }
                return udw;
        }

        // Promotes a Variant carrying either AncAtc or Timecode into an
        // AncAtc.  Bare Timecode payloads (the pre-F5 API shape) yield
        // an AncAtc with zero flags / user bits / DBB2 — capture paths
        // that flow through the new AncAtc parse step preserve every
        // field, but synthesised Timecode → ATC build paths land on
        // ST 12-1 §9.2.2 "set to 0" defaults.
        AncAtc variantToAncAtc(const Variant &v) {
                if (v.type() == DataTypeAncAtc) return v.get<AncAtc>();
                return AncAtc(v.get<Timecode>());
        }

        // Resolves a builder-time AncFormat from a Variant produced by the
        // caller.  The Variant's typed payload doesn't carry the
        // LTC-vs-VITC1-vs-VITC2 discriminator, so the format identity
        // comes from whichever build entry point fires (one per
        // registered AncFormat::AtcLtc / AtcVitc1 / AtcVitc2), and
        // drives both the SDID stamp and the DBB1 byte in packAtcUdw.
        // Maps the DBB1 payload-type byte back to the AncFormat::ID the
        // (DID,SDID) lookup would *like* to resolve to.  Used by
        // @c syncPolicyAtc when re-encoding a captured packet under
        // Repeat[idx>0] — the captured @c pkt.format().id() is always
        // @c AtcLtc (lowest ID wins the shared (0x60,0x60) slot), so
        // we recover the real flavour from the parsed
        // @c AncAtc::payloadType.
        inline AncFormat::ID formatForPayloadType(uint8_t dbb1) {
                switch (dbb1) {
                        case AncAtc::Vitc1: return AncFormat::AtcVitc1;
                        case AncAtc::Vitc2: return AncFormat::AtcVitc2;
                        default: return AncFormat::AtcLtc;
                }
        }

        // Core build path.  @c atc.payloadType() is the authoritative
        // DBB1 byte — callers wanting a specific flavour override the
        // field on the AncAtc before invoking this.  @p fmtId is used
        // for the @c St291Packet::build registry lookup (DID/SDID
        // resolution); all three ATC IDs resolve to (0x60,0x60).
        AncTranslator::PacketsResult buildAtcSt291Impl(AncFormat::ID fmtId, const AncAtc &atc,
                                                       const AncTranslateConfig &cfg) {
                bool legacyFieldMark = cfg.getAs<bool>(
                        AncTranslateConfig::AtcVitcLegacyFieldMark, false);
                List<uint16_t> udw = packAtcUdw(atc, legacyFieldMark);

                uint16_t line   = cfg.getAs<uint16_t>(AncTranslateConfig::St291BuildLine,
                                                      St291Packet::UnspecifiedLine);
                bool     fieldB = cfg.getAs<bool>(AncTranslateConfig::St291FieldB, false);
                bool     cBit = cfg.getAs<bool>(AncTranslateConfig::St291BuildCBit, false);

                St291Packet     p = St291Packet::build(AncFormat(fmtId), udw, line, St291Packet::UnspecifiedHOffset,
                                                        fieldB, cBit);
                AncPacket::List out;
                out.pushToBack(p.packet());
                return makeResult<AncPacket::List>(std::move(out));
        }

        // Format-keyed build wrappers.  Each overrides @c
        // AncAtc::payloadType to its specific DBB1 value so the
        // caller's explicit format choice wins over any stale value
        // riding in the AncAtc.
        AncTranslator::PacketsResult buildAtcLtcSt291(const Variant &v, const AncTranslateConfig &cfg) {
                AncAtc atc = variantToAncAtc(v);
                atc.setPayloadType(AncAtc::Ltc);
                return buildAtcSt291Impl(AncFormat::AtcLtc, atc, cfg);
        }

        AncTranslator::PacketsResult buildAtcVitc1St291(const Variant &v, const AncTranslateConfig &cfg) {
                AncAtc atc = variantToAncAtc(v);
                atc.setPayloadType(AncAtc::Vitc1);
                return buildAtcSt291Impl(AncFormat::AtcVitc1, atc, cfg);
        }

        AncTranslator::PacketsResult buildAtcVitc2St291(const Variant &v, const AncTranslateConfig &cfg) {
                AncAtc atc = variantToAncAtc(v);
                atc.setPayloadType(AncAtc::Vitc2);
                return buildAtcSt291Impl(AncFormat::AtcVitc2, atc, cfg);
        }

        // Sync policy: ATC must keep advancing across a Repeat run rather
        // than freezing on the original timecode — receivers that decode
        // wallclock from ATC would otherwise see the clock stop.  Output
        // timecode = input timecode + repeatIndex (libvtc handles
        // drop-frame rollover, so a Repeat run that crosses an
        // xx:00:59:29 → xx:01:00:02 boundary in DF30 advances correctly
        // without us re-implementing the rule).
        //
        // On Drop the packet is lost (the next surviving ATC packet
        // re-establishes the wall clock).  On Play and Repeat[idx=0] the
        // input packet is copied through verbatim — no re-encode, no
        // chance of subtle byte differences from the parse/build round
        // trip.  Only Repeat[idx>0] re-encodes.
        AncTranslator::PacketsResult syncPolicyAtc(const AncPacket &pkt, FrameSyncDisposition d,
                                               uint8_t repeatIndex, const AncTranslateConfig &cfg) {
                AncPacket::List out;
                if (d.kind() == FrameSyncDisposition::Drop) {
                        return makeResult<AncPacket::List>(std::move(out));
                }
                if (d.kind() == FrameSyncDisposition::Play || repeatIndex == 0) {
                        out.pushToBack(pkt);
                        return makeResult<AncPacket::List>(std::move(out));
                }

                // Repeat[idx>0]: parse, advance by repeatIndex frames, re-encode.
                AncTranslator::ParseResult parsed = parseAtcSt291Impl(pkt, cfg);
                if (parsed.second().isError()) return makeError<AncPacket::List>(parsed.second());
                AncAtc   atc = parsed.first().get<AncAtc>();
                Timecode tc  = atc.timecode();
                for (uint8_t i = 0; i < repeatIndex; ++i) ++tc;
                atc.setTimecode(tc);

                // Preserve the original packet's line / fieldB so the wire
                // framing matches the source rather than whatever defaults
                // are baked into the held cfg.  Other cfg keys (checksum
                // policy etc.) flow through unchanged.
                Result<St291Packet> rp = St291Packet::from(pkt);
                if (rp.second().isError()) return makeError<AncPacket::List>(rp.second());
                AncTranslateConfig overrideCfg = cfg;
                overrideCfg.set(AncTranslateConfig::St291BuildLine, rp.first().line());
                overrideCfg.set(AncTranslateConfig::St291FieldB, rp.first().fieldB());

                // ST 12-2:2014 §5 collapses LTC/VITC1/VITC2 onto a
                // single (DID=0x60, SDID=0x60), so @c pkt.format().id()
                // is always @c AtcLtc on capture.  Recover the actual
                // flavour from the parsed @c AncAtc::payloadType so a
                // Repeat re-emit preserves the original DBB1 byte
                // (otherwise a captured VITC1 stream would silently
                // re-emit as LTC under Repeat).
                AncFormat::ID fmtId = formatForPayloadType(atc.payloadType());
                return buildAtcSt291Impl(fmtId, atc, overrideCfg);
        }

} // namespace

PROMEKI_NAMESPACE_END

// All three ATC flavours share the same wire layout and the same parser /
// builder body; the registry just dispatches on the AncFormat::ID, so we
// register the same `parseAtcSt291Impl` under each ID and the format-specific
// thin wrappers above for the build direction (so the builder knows which
// DID / SDID + DBB1 byte to stamp on its way out).
PROMEKI_REGISTER_ANC_PARSER(AtcLtc_St291, AtcLtc, ::promeki::AncTransport::St291,
                             ::promeki::parseAtcSt291Impl)
PROMEKI_REGISTER_ANC_PARSER(AtcVitc1_St291, AtcVitc1, ::promeki::AncTransport::St291,
                             ::promeki::parseAtcSt291Impl)
PROMEKI_REGISTER_ANC_PARSER(AtcVitc2_St291, AtcVitc2, ::promeki::AncTransport::St291,
                             ::promeki::parseAtcSt291Impl)

PROMEKI_REGISTER_ANC_BUILDER(AtcLtc_St291, AtcLtc, ::promeki::AncTransport::St291,
                              ::promeki::buildAtcLtcSt291)
PROMEKI_REGISTER_ANC_BUILDER(AtcVitc1_St291, AtcVitc1, ::promeki::AncTransport::St291,
                              ::promeki::buildAtcVitc1St291)
PROMEKI_REGISTER_ANC_BUILDER(AtcVitc2_St291, AtcVitc2, ::promeki::AncTransport::St291,
                              ::promeki::buildAtcVitc2St291)

PROMEKI_REGISTER_ANC_SYNC_POLICY(AtcLtc, AtcLtc, ::promeki::syncPolicyAtc)
PROMEKI_REGISTER_ANC_SYNC_POLICY(AtcVitc1, AtcVitc1, ::promeki::syncPolicyAtc)
PROMEKI_REGISTER_ANC_SYNC_POLICY(AtcVitc2, AtcVitc2, ::promeki::syncPolicyAtc)
