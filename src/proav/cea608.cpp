/**
 * @file      cea608.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdint>
#include <promeki/cea608.h>
#include <promeki/color.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

        // -- PAC b1 lookup table ---------------------------------------
        //
        // CEA-608 PAC pairs are organised as 14 row pairs (rows 1+2,
        // 3+4, ...) plus a singleton row 11.  For CC1 the first byte
        // depends on row; the second byte's bit 5 selects between the
        // two rows of a pair.  Row 11 is special: its b1 (0x10)
        // doesn't share a pair partner, and bit 5 of b2 stays 0.
        //
        // Indices below are 1-based row numbers (the [0] slot is
        // unused / 0).  All values are pre-parity.

        constexpr uint8_t kPacB1[16] = {
                0x00, 0x11, 0x11, 0x12, 0x12, 0x15, 0x15, 0x16,
                0x16, 0x17, 0x17, 0x10, 0x13, 0x13, 0x14, 0x14,
        };

        /// @brief Row-of-pair bit (bit 5 of PAC b2): 0 for the first
        ///        row of a pair, 1 for the second.  Row 11 is its own
        ///        singleton with bit 5 = 0.
        constexpr uint8_t kPacB2RowBit[16] = {
                0, 0, 0x20, 0, 0x20, 0, 0x20, 0,
                0x20, 0, 0x20, 0, 0, 0x20, 0, 0x20,
        };

        /// @brief Inverse lookup: given a recognised PAC b1, returns
        ///        the row pair's first row index (1..14), or 0 when
        ///        @p b1 is not a PAC first byte.  Row 11 has no
        ///        partner — caller must check b2 bit 5 to disambiguate.
        constexpr int rowPairFirstForB1(uint8_t b1) {
                switch (b1) {
                        case 0x11: return 1;
                        case 0x12: return 3;
                        case 0x15: return 5;
                        case 0x16: return 7;
                        case 0x17: return 9;
                        case 0x10: return 11;
                        case 0x13: return 12;
                        case 0x14: return 14;
                        default:   return 0;
                }
        }

        // -- PAC b2 colour / indent sub-field --------------------------
        //
        // The 4 bits at positions 4..1 of b2 encode either a colour
        // (codes 0..7) or an indent (codes 8..15, with implicit white
        // colour).  Mid-row codes use the same colour sub-field at
        // positions 3..1 of b2 (with bit 4 always 0 and bit 5 always
        // 1, giving the 0x20..0x2F range).

        constexpr uint8_t colorSubfield(Cea608::CaptionColor c, bool italic) {
                if (italic) return 7; // "italic white" code
                switch (c) {
                        case Cea608::CaptionColor::White:   return 0;
                        case Cea608::CaptionColor::Green:   return 1;
                        case Cea608::CaptionColor::Blue:    return 2;
                        case Cea608::CaptionColor::Cyan:    return 3;
                        case Cea608::CaptionColor::Red:     return 4;
                        case Cea608::CaptionColor::Yellow:  return 5;
                        case Cea608::CaptionColor::Magenta: return 6;
                        // Black has no foreground wire encoding (the
                        // code-7 slot is "italic white" in PAC + mid-
                        // row).  Fall back to White.
                        case Cea608::CaptionColor::Black:   return 0;
                }
                return 0;
        }

        constexpr int snapIndentCol(int col) {
                if (col <= 0) return 0;
                if (col >= 28) return 28;
                return (col / 4) * 4;
        }

} // namespace

Color::List Cea608::palette() {
        Color::List p;
        p.reserve(CaptionColorCount);
        // sRGB primaries matching the 608 spec's well-known palette.
        // Stored in @ref CaptionColor index order.  Index 7 (Black)
        // is BG-attribute only — keeping it in the palette lets
        // BG-side nearest-colour quantisation pick Black for dark
        // input colours.
        p.pushToBack(Color::White);
        p.pushToBack(Color::Green);
        p.pushToBack(Color::Blue);
        p.pushToBack(Color::Cyan);
        p.pushToBack(Color::Red);
        p.pushToBack(Color::Yellow);
        p.pushToBack(Color::Magenta);
        p.pushToBack(Color::Black);
        return p;
}

void Cea608::encodePac(const PacAttr &attr, uint8_t &b1, uint8_t &b2) {
        int row = attr.row;
        if (row < 1) row = 1;
        if (row > 15) row = 15;
        b1 = kPacB1[row];
        uint8_t rowBit = kPacB2RowBit[row];

        uint8_t subfield = 0;
        if (attr.italic) {
                // Italic forces white; indent is ignored in italic
                // sub-field, callers wanting an italic-with-indent
                // line should emit Tab Offsets after the PAC.
                subfield = 7;
        } else if (attr.color != CaptionColor::White) {
                // Non-white colour: use the colour-encoding sub-field
                // (0..6).  Indent is implicit col 0; callers wanting
                // a coloured + indented line emit Tab Offsets after.
                subfield = colorSubfield(attr.color, false);
        } else {
                // White (the default colour): use the indent sub-field
                // (8..15) so the canonical "Row 15 White Col 0" PAC
                // encodes as the 0x70 second-byte form the rest of
                // the 608 ecosystem expects.  Indent 0..28 step 4.
                subfield = static_cast<uint8_t>(8 + (snapIndentCol(attr.indentCol) / 4));
        }
        // b2 layout: 0b 0 1 R P P P P U
        //   R = rowBit (already 0 or 0x20)
        //   PPPP = subfield (4 bits)
        //   U = underline
        b2 = 0x40 | rowBit | static_cast<uint8_t>((subfield & 0x0F) << 1)
             | (attr.underline ? 0x01 : 0x00);
}

bool Cea608::isPac(uint8_t b1, uint8_t b2) {
        const int pairFirst = rowPairFirstForB1(b1);
        if (pairFirst == 0) return false;
        // b2 must be in [0x40, 0x7F] (top two bits = 01).
        if ((b2 & 0xC0) != 0x40) return false;
        // Row 11's b1 (0x10) has no second-row partner — bit 5 of
        // b2 must be 0 for the pair to be a valid PAC.  Without
        // this mirror of decodePac's constraint, isPac would
        // accept b1=0x10 + b2 in [0x60, 0x7F] and the decoder
        // dispatcher would route those bytes through PAC handling
        // only for decodePac to reject them as no-ops.
        if (pairFirst == 11 && (b2 & 0x20) != 0) return false;
        return true;
}

bool Cea608::decodePac(uint8_t b1, uint8_t b2, PacAttr &out) {
        const int pairFirst = rowPairFirstForB1(b1);
        if (pairFirst == 0) return false;
        if ((b2 & 0xC0) != 0x40) return false;

        // Row 11's b1 (0x10) has no partner — bit 5 must be 0.
        const bool secondOfPair = (b2 & 0x20) != 0;
        if (pairFirst == 11 && secondOfPair) return false;

        out.row = pairFirst + (secondOfPair ? 1 : 0);
        const uint8_t subfield = static_cast<uint8_t>((b2 >> 1) & 0x0F);
        out.underline = (b2 & 0x01) != 0;
        out.italic = false;
        out.indentCol = 0;
        out.color = CaptionColor::White;

        if (subfield <= 6) {
                out.color = static_cast<CaptionColor>(subfield);
        } else if (subfield == 7) {
                out.italic = true;
        } else {
                // 8..15 → indent 0..28 step 4.
                out.indentCol = (subfield - 8) * 4;
        }
        return true;
}

void Cea608::encodeMidRow(CaptionColor color, bool italic, bool underline, uint8_t &b1, uint8_t &b2) {
        // Mid-row codes live at b1=0x11 / b2 in [0x20, 0x2F].  The
        // 3-bit colour subfield is at b2 bits 3..1; bit 0 is
        // underline.  Bit 4 stays 0 (distinguishes mid-row from PAC,
        // which has bit 4 of the 4-bit subfield set when in an
        // indent / italic-white slot).
        b1 = 0x11;
        const uint8_t subfield = italic ? static_cast<uint8_t>(7) : colorSubfield(color, false);
        b2 = 0x20 | static_cast<uint8_t>((subfield & 0x07) << 1) | (underline ? 0x01 : 0x00);
}

bool Cea608::isMidRow(uint8_t b1, uint8_t b2) {
        if (b1 != 0x11) return false;
        return (b2 & 0xF0) == 0x20;
}

bool Cea608::decodeMidRow(uint8_t b1, uint8_t b2, CaptionColor &outColor, bool &outItalic, bool &outUnderline) {
        if (!isMidRow(b1, b2)) return false;
        const uint8_t subfield = static_cast<uint8_t>((b2 >> 1) & 0x07);
        outUnderline = (b2 & 0x01) != 0;
        outItalic = (subfield == 7);
        outColor = outItalic ? CaptionColor::White : static_cast<CaptionColor>(subfield);
        return true;
}

// -- Background attribute codes (CTA-608-E §6.2 Table 3) -----------
//
// Wire layout for CC1 (channel 1, field 1):
//   b1 = 0x10
//   b2 = 0x20 + (colorIdx << 1) + (semiTransparent ? 1 : 0)
//
// where colorIdx is:
//   0 = White, 1 = Green, 2 = Blue, 3 = Cyan, 4 = Red,
//   5 = Yellow, 6 = Magenta, 7 = Black.
//
// Index 7 (Black) is BG-attribute only — the PAC + mid-row colour
// subfields don't carry Black (the spec uses the code-7 slot for
// "italic white" instead).  @ref Cea608::CaptionColor exposes
// Black as enum value 7 for the BG-attribute round-trip; the fg
// paths (encodePac / encodeMidRow) treat Black as White on the
// wire.
//
// The bg attribute is doubled on the wire like other control
// codes — the encoder schedules a second copy of the pair
// adjacent to the first.

void Cea608::encodeBgAttribute(CaptionColor color, bool semiTransparent, uint8_t &b1, uint8_t &b2) {
        b1 = 0x10;
        const uint8_t idx = static_cast<uint8_t>(static_cast<uint8_t>(color) & 0x07);
        b2 = static_cast<uint8_t>(0x20 | (idx << 1) | (semiTransparent ? 0x01 : 0x00));
}

bool Cea608::isBgAttribute(uint8_t b1, uint8_t b2) {
        if (b1 != 0x10) return false;
        return (b2 & 0xF0) == 0x20;
}

bool Cea608::decodeBgAttribute(uint8_t b1, uint8_t b2, CaptionColor &outColor, bool &outSemiTransparent) {
        if (!isBgAttribute(b1, b2)) return false;
        const uint8_t idx = static_cast<uint8_t>((b2 >> 1) & 0x07);
        outSemiTransparent = (b2 & 0x01) != 0;
        outColor = static_cast<CaptionColor>(idx);
        return true;
}

bool Cea608::isBt(uint8_t b1, uint8_t b2) {
        // §6.2 Table 3: BT = 0x17, 0x2D.  FA = 0x17, 0x2E and FAU =
        // 0x17, 0x2F share the same first byte — distinguish via b2.
        return b1 == Cc1ExtAttrB1 && b2 == BtB2;
}

void Cea608::encodeTabOffset(int columns, uint8_t &b1, uint8_t &b2) {
        if (columns < 1) columns = 1;
        if (columns > 3) columns = 3;
        b1 = Cc1ExtAttrB1;
        b2 = static_cast<uint8_t>(0x20 + columns); // 0x21 / 0x22 / 0x23
}

bool Cea608::isTabOffset(uint8_t b1, uint8_t b2) {
        return b1 == Cc1ExtAttrB1 && b2 >= TabOffsetT1 && b2 <= TabOffsetT3;
}

bool Cea608::decodeTabOffset(uint8_t b1, uint8_t b2, int &outColumns) {
        if (!isTabOffset(b1, b2)) return false;
        outColumns = static_cast<int>(b2 - 0x20); // 1 / 2 / 3
        return true;
}

PROMEKI_NAMESPACE_END
