/**
 * @file      anccodec_cea608.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * SMPTE 334-1 raw line-21 CEA-608 detailer for @c AncTransport::St291
 * (DID 0x61 / SDID 0x02).  Unlike the other ANC codecs in this
 * directory, ST 334-1 has @em no registered parser / builder — the
 * library carries modern 608 captions inside the @ref Cea708Cdp (CDP)
 * envelope via @c AncFormat::Cea708, and the legacy raw line-21 format
 * is descriptor-only (see @c AncFormat::Cea608).  This file therefore
 * registers only a @ref AncTranslator detailer: it decodes the raw
 * line-21 byte pairs directly off the ST 291 framing so the Details /
 * analysis path can describe a captured 334-1 packet even though the
 * translate path can't (yet) round-trip it.
 *
 * Each 334-1 packet carries the two NTSC line-21 data bytes as its
 * user-data words (one data byte per 10-bit UDW).  The detailer walks
 * them in pairs, validates the odd-parity stamp every received byte
 * carries, strips parity, and classifies each pair as a control code
 * (PAC / mid-row / Tab Offset / misc) or printable G0 characters.  The
 * NTSC field a pair belongs to is not derivable from the raw packet
 * alone (it depends on the carriage line / external context), so the
 * detailer reports the intra-field channel (CC1 vs CC2) but not the
 * absolute CC1..CC4 channel.
 */

#include <promeki/ancformat.h>
#include <promeki/ancpacket.h>
#include <promeki/anctranslator.h>
#include <promeki/cea608.h>
#include <promeki/enums_anc.h>
#include <promeki/list.h>
#include <promeki/result.h>
#include <promeki/st291packet.h>
#include <promeki/stringlist.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

        // Names the 14 SMPTE/CEA-608 miscellaneous control codes by their
        // second byte (the first byte selects the field / channel).  The
        // second-byte values are the parity-stripped Cea608::Misc*
        // constants.  Reserved second bytes surface as raw hex.
        String cea608MiscName(uint8_t b2) {
                switch (b2) {
                        case Cea608::MiscRCL: return String("RCL (Resume Caption Loading)");
                        case Cea608::MiscBS:  return String("BS (Backspace)");
                        case Cea608::MiscDER: return String("DER (Delete to End of Row)");
                        case Cea608::MiscRU2: return String("RU2 (Roll-Up 2)");
                        case Cea608::MiscRU3: return String("RU3 (Roll-Up 3)");
                        case Cea608::MiscRU4: return String("RU4 (Roll-Up 4)");
                        case Cea608::MiscFON: return String("FON (Flash On)");
                        case Cea608::MiscRDC: return String("RDC (Resume Direct Captioning)");
                        case Cea608::MiscTR:  return String("TR (Text Restart)");
                        case Cea608::MiscRTD: return String("RTD (Resume Text Display)");
                        case Cea608::MiscEDM: return String("EDM (Erase Displayed Memory)");
                        case Cea608::MiscCR:  return String("CR (Carriage Return)");
                        case Cea608::MiscENM: return String("ENM (Erase Non-displayed Memory)");
                        case Cea608::MiscEOC: return String("EOC (End Of Caption)");
                        default:              return String::sprintf("Misc 0x%02X", b2);
                }
        }

        // Names a CaptionColor for mid-row / PAC colour reporting.  Not a
        // TypedEnum, so spelled out here rather than via valueName().
        String cea608ColorName(Cea608::CaptionColor c) {
                switch (c) {
                        case Cea608::CaptionColor::White:   return String("White");
                        case Cea608::CaptionColor::Green:   return String("Green");
                        case Cea608::CaptionColor::Blue:    return String("Blue");
                        case Cea608::CaptionColor::Cyan:    return String("Cyan");
                        case Cea608::CaptionColor::Red:     return String("Red");
                        case Cea608::CaptionColor::Yellow:  return String("Yellow");
                        case Cea608::CaptionColor::Magenta: return String("Magenta");
                        case Cea608::CaptionColor::Black:   return String("Black");
                }
                return String("?");
        }

        // Describes a parity-stripped control pair.  PAC / mid-row / Tab
        // Offset get a structured render; the misc-control family is named
        // by second byte; anything else surfaces as raw hex.
        String cea608ControlName(uint8_t b1, uint8_t b2) {
                Cea608::PacAttr pac;
                if (Cea608::decodePac(b1, b2, pac)) {
                        return String::sprintf("PAC (row %d, col %d, %s%s%s)", pac.row, pac.indentCol,
                                               cea608ColorName(pac.color).cstr(),
                                               pac.underline ? ", underline" : "",
                                               pac.italic ? ", italic" : "");
                }
                Cea608::CaptionColor color = Cea608::CaptionColor::White;
                bool                 italic = false, underline = false;
                if (Cea608::decodeMidRow(b1, b2, color, italic, underline)) {
                        return String::sprintf("Mid-row (%s%s%s)", cea608ColorName(color).cstr(),
                                               underline ? ", underline" : "",
                                               italic ? ", italic" : "");
                }
                int cols = 0;
                if (Cea608::decodeTabOffset(b1, b2, cols)) {
                        return String::sprintf("Tab Offset %d", cols);
                }
                // Misc control: first byte 0x14/0x15 (field 1) or 0x1C/0x1D
                // (field 2) with the second byte in the 0x20..0x2F range.
                if ((b1 == 0x14 || b1 == 0x15 || b1 == 0x1C || b1 == 0x1D) && b2 >= 0x20 &&
                    b2 <= 0x2F) {
                        return cea608MiscName(b2);
                }
                return String::sprintf("Control 0x%02X 0x%02X", b1, b2);
        }

        // Renders a non-control (informational) pair as its printable G0
        // characters.  Each parity-stripped byte in 0x20..0x7F is a
        // printable Latin character; bytes outside that range render as a
        // hex escape so nothing is silently dropped.
        String cea608TextName(uint8_t b1, uint8_t b2) {
                String s("Text \"");
                for (uint8_t b : {b1, b2}) {
                        if (b == 0x00) continue; // padding byte within a pair
                        if (b >= 0x20 && b <= 0x7F) {
                                s += static_cast<char>(b);
                        } else {
                                s += String::sprintf("\\x%02X", b);
                        }
                }
                s += "\"";
                return s;
        }

        // Full human-readable analysis of a SMPTE 334-1 raw line-21
        // packet.  Always returns a populated AncDetails — a packet that
        // cannot be framed still surfaces an Error issue.
        AncDetails detailCea608St291(const AncPacket &pkt, const AncTranslateConfig & /*cfg*/) {
                AncDetails d;

                Result<St291Packet> rp = St291Packet::from(pkt);
                if (rp.second().isError()) {
                        d.addError(String("ST 291 framing decode failed: ") + rp.second().desc());
                        return d;
                }
                const St291Packet &p   = rp.first();
                List<uint16_t>     udw = p.udw();

                d.addField("DID", String::sprintf("0x%02X", p.did()));
                d.addField("SDID", String::sprintf("0x%02X", p.sdid()));
                d.addField("DataCount", String::number(p.dataCount()));
                d.addField("Line", String::number(p.line()));
                if (!p.checksumValid()) {
                        d.addWarning(String::sprintf(
                                "Stored checksum 0x%03X does not match computed 0x%03X",
                                p.checksum(), p.computedChecksum()));
                }

                if (udw.isEmpty()) {
                        d.addWarning("No user-data words present — empty line-21 payload");
                        return d;
                }
                if ((udw.size() & 1u) != 0) {
                        d.addWarning(String::sprintf(
                                "Odd UDW count %zu — line-21 data is carried as byte pairs; the "
                                "trailing byte is unpaired", udw.size()));
                }

                // Walk the line-21 bytes in pairs.  Each received byte
                // carries odd parity on bit 7; a parity failure means the
                // pair is corrupt and the decode below is unreliable.
                for (size_t i = 0; i + 1 < udw.size(); i += 2) {
                        uint8_t rawB1 = static_cast<uint8_t>(udw[i] & 0xFF);
                        uint8_t rawB2 = static_cast<uint8_t>(udw[i + 1] & 0xFF);
                        String  label = String::sprintf("Pair[%zu]", i / 2);

                        bool    parityOk = Cea608::checkOddParityPair(rawB1, rawB2);
                        uint8_t b1       = Cea608::stripParity(rawB1);
                        uint8_t b2       = Cea608::stripParity(rawB2);

                        String desc;
                        if (b1 == 0x00 && b2 == 0x00) {
                                desc = String("Null (padding)");
                        } else if (Cea608::isControlPair(b1, b2)) {
                                // Bit 3 of the control byte selects the second
                                // channel of the field (CC2 / CC4).  The
                                // absolute field isn't in the raw packet, so
                                // report the intra-field channel only.
                                bool   secondChan = (b1 & 0x08) != 0;
                                String ch = secondChan ? String("CC2/CC4") : String("CC1/CC3");
                                desc = ch + ": " + cea608ControlName(b1, b2);
                        } else {
                                desc = cea608TextName(b1, b2);
                        }

                        d.addField(label, String::sprintf("%02X %02X — %s", rawB1, rawB2,
                                                          desc.cstr()));
                        if (!parityOk) {
                                d.addWarning(String::sprintf(
                                        "Pair[%zu] (%02X %02X) failed odd-parity check; decode is "
                                        "best-effort", i / 2, rawB1, rawB2));
                        }
                }

                return d;
        }

} // namespace

PROMEKI_NAMESPACE_END

PROMEKI_REGISTER_ANC_DETAILER(Cea608_St291, Cea608, ::promeki::AncTransport::St291,
                              ::promeki::detailCea608St291)
