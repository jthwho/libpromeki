/**
 * @file      anccodec_atc.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * SMPTE ST 12M-2 ATC (Ancillary Timecode) parsers and builders for the
 * three flavours (LTC, VITC1, VITC2) carried on @c AncTransport::St291.
 * Decodes / encodes the 8-UDW SMPTE 12M-2 payload to and from a
 * @ref Timecode (the libvtc-backed timecode value type).
 *
 * Wire layout (per ST 12M-2-2014 §5.3, 8 UDW bytes, low nibble lives
 * in bits 0–3 of each word):
 *
 *  | UDW | Bits 0–3                | Bits 4–7 (Binary Group N) |
 *  |-----|-------------------------|---------------------------|
 *  | 1   | Frame units             | BG1                       |
 *  | 2   | Frame tens (b0–1), DF (b2), CF (b3) | BG2           |
 *  | 3   | Seconds units           | BG3                       |
 *  | 4   | Seconds tens (b0–2), Field/Polarity (b3) | BG4      |
 *  | 5   | Minutes units           | BG5                       |
 *  | 6   | Minutes tens (b0–2), BGF0 (b3) | BG6                |
 *  | 7   | Hours units             | BG7                       |
 *  | 8   | Hours tens (b0–1), BGF1 (b2), BGF2 (b3) | BG8       |
 *
 * Binary group bits 4–7 of every UDW are left at zero on emit.  On
 * parse the binary-group bits and the BGF/CF flags are ignored — they
 * carry user data the library does not interpret at this layer.
 */

#include <promeki/ancformat.h>
#include <promeki/ancpacket.h>
#include <promeki/anctranslator.h>
#include <promeki/enums.h>
#include <promeki/list.h>
#include <promeki/result.h>
#include <promeki/st291packet.h>
#include <promeki/timecode.h>
#include <promeki/variant.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

        // -- Mode helpers -----------------------------------------------------

        // Map SMPTE 12M-2 (fps × 10 + df flag) to a libvtc TimecodeType the
        // build-side codec uses when the source Timecode lacks a mode.  Drives
        // both round-trip integrity (the parser stamps the same mode the wire
        // implies) and the build path when no other rate hint is available.
        Timecode::Mode modeForRate(uint32_t fps, bool df) {
                if (df && fps == 30) return Timecode::Mode(Timecode::DF30);
                if (fps == 24) return Timecode::Mode(Timecode::NDF24);
                if (fps == 25) return Timecode::Mode(Timecode::NDF25);
                if (fps == 30) return Timecode::Mode(Timecode::NDF30);
                return Timecode::Mode();
        }

        // -- Parsing ----------------------------------------------------------

        Result<Variant> parseAtcSt291Impl(const AncPacket &pkt, const AncTranslateConfig & /*cfg*/) {
                Result<St291Packet> rp = St291Packet::from(pkt);
                if (rp.second().isError()) return makeError<Variant>(rp.second());
                const St291Packet &p = rp.first();
                List<uint16_t>     udw = p.udw();
                if (udw.size() < 8) {
                        return makeError<Variant>(Error::CorruptData);
                }

                // Mask off parity bits — every accessor that follows only cares
                // about the payload nibble.
                uint8_t u0 = static_cast<uint8_t>(udw[0] & 0xFF);
                uint8_t u1 = static_cast<uint8_t>(udw[1] & 0xFF);
                uint8_t u2 = static_cast<uint8_t>(udw[2] & 0xFF);
                uint8_t u3 = static_cast<uint8_t>(udw[3] & 0xFF);
                uint8_t u4 = static_cast<uint8_t>(udw[4] & 0xFF);
                uint8_t u5 = static_cast<uint8_t>(udw[5] & 0xFF);
                uint8_t u6 = static_cast<uint8_t>(udw[6] & 0xFF);

                uint8_t frameUnits = u0 & 0x0F;
                uint8_t frameTens = u1 & 0x03;
                bool    df = (u1 & 0x04) != 0;

                uint8_t secUnits = u2 & 0x0F;
                uint8_t secTens = u3 & 0x07;

                uint8_t minUnits = u4 & 0x0F;
                uint8_t minTens = u5 & 0x07;

                uint8_t hourUnits = u6 & 0x0F;
                uint8_t hourTens = static_cast<uint8_t>(udw[7] & 0x03);

                Timecode::DigitType hour =
                        static_cast<Timecode::DigitType>(hourTens * 10 + hourUnits);
                Timecode::DigitType min = static_cast<Timecode::DigitType>(minTens * 10 + minUnits);
                Timecode::DigitType sec = static_cast<Timecode::DigitType>(secTens * 10 + secUnits);
                Timecode::DigitType frm = static_cast<Timecode::DigitType>(frameTens * 10 + frameUnits);

                // Library design: we don't know the exact wall-clock rate from
                // just the 8 timecode bytes (the DF bit narrows 30→29.97-DF,
                // but 24 vs 25 vs 30-NDF is invisible).  Default to NDF30
                // unless DF is asserted (in which case 29.97-DF is the only
                // legal interpretation).  Callers that know the rate from the
                // paired video should call Timecode::setMode after parse.
                Timecode tc(modeForRate(30, df), hour, min, sec, frm);
                return makeResult<Variant>(Variant(tc));
        }

        // -- Building ---------------------------------------------------------

        // Resolves a builder-time AncFormat from a Variant produced by the
        // caller.  The Variant's typed payload doesn't carry the
        // LTC-vs-VITC1-vs-VITC2 discriminator (a Timecode is a Timecode), so
        // the format identity comes from whichever build entry point fires
        // (one per registered AncFormat::AtcLtc / AtcVitc1 / AtcVitc2).
        Result<AncPacket> buildAtcSt291(AncFormat::ID fmtId, const Variant &v,
                                         const AncTranslateConfig &cfg) {
                Timecode tc = v.get<Timecode>();

                uint8_t hour = static_cast<uint8_t>(tc.hour());
                uint8_t min = static_cast<uint8_t>(tc.min());
                uint8_t sec = static_cast<uint8_t>(tc.sec());
                uint8_t frm = static_cast<uint8_t>(tc.frame());
                bool    df = tc.isDropFrame();

                List<uint16_t> udw;
                udw.resize(8);
                udw[0] = static_cast<uint16_t>(frm % 10);
                udw[1] = static_cast<uint16_t>((frm / 10) & 0x03);
                if (df) udw[1] = static_cast<uint16_t>(udw[1] | 0x04);
                udw[2] = static_cast<uint16_t>(sec % 10);
                udw[3] = static_cast<uint16_t>((sec / 10) & 0x07);
                udw[4] = static_cast<uint16_t>(min % 10);
                udw[5] = static_cast<uint16_t>((min / 10) & 0x07);
                udw[6] = static_cast<uint16_t>(hour % 10);
                udw[7] = static_cast<uint16_t>((hour / 10) & 0x03);

                uint16_t line = cfg.getAs<uint16_t>(AncTranslateConfig::St291BuildLine, uint16_t(0));
                bool     fieldB = cfg.getAs<bool>(AncTranslateConfig::St291FieldB, false);

                St291Packet p = St291Packet::build(AncFormat(fmtId), udw, line, St291Packet::UnspecifiedHOffset, fieldB);
                return makeResult<AncPacket>(p.packet());
        }

        Result<AncPacket> buildAtcLtcSt291(const Variant &v, const AncTranslateConfig &cfg) {
                return buildAtcSt291(AncFormat::AtcLtc, v, cfg);
        }

        Result<AncPacket> buildAtcVitc1St291(const Variant &v, const AncTranslateConfig &cfg) {
                return buildAtcSt291(AncFormat::AtcVitc1, v, cfg);
        }

        Result<AncPacket> buildAtcVitc2St291(const Variant &v, const AncTranslateConfig &cfg) {
                return buildAtcSt291(AncFormat::AtcVitc2, v, cfg);
        }

} // namespace

PROMEKI_NAMESPACE_END

// All three ATC flavours share the same wire layout and the same parser /
// builder body; the registry just dispatches on the AncFormat::ID, so we
// register the same `parseAtcSt291Impl` under each ID and the format-specific
// thin wrappers above for the build direction (so the builder knows which
// DID / SDID to stamp on its way out).
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
