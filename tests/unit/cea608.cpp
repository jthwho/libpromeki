/**
 * @file      tests/cea608.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Coverage for the @ref Cea608 PAC + mid-row encode / decode helpers
 * and the @ref Cea608::palette accessor.
 */

#include <doctest/doctest.h>
#include <promeki/cea608.h>
#include <promeki/color.h>

using namespace promeki;

// ============================================================================
// Palette
// ============================================================================

TEST_CASE("Cea608::palette returns the 7 well-known primaries in CaptionColor order") {
        Color::List p = Cea608::palette();
        REQUIRE(p.size() == Cea608::CaptionColorCount);
        CHECK(p[static_cast<size_t>(Cea608::CaptionColor::White)] == Color::White);
        CHECK(p[static_cast<size_t>(Cea608::CaptionColor::Green)] == Color::Green);
        CHECK(p[static_cast<size_t>(Cea608::CaptionColor::Blue)] == Color::Blue);
        CHECK(p[static_cast<size_t>(Cea608::CaptionColor::Cyan)] == Color::Cyan);
        CHECK(p[static_cast<size_t>(Cea608::CaptionColor::Red)] == Color::Red);
        CHECK(p[static_cast<size_t>(Cea608::CaptionColor::Yellow)] == Color::Yellow);
        CHECK(p[static_cast<size_t>(Cea608::CaptionColor::Magenta)] == Color::Magenta);
}

// ============================================================================
// PAC
// ============================================================================

TEST_CASE("Cea608::encodePac default Row 15 White Col 0 matches the canonical constants") {
        Cea608::PacAttr a;
        // Defaults: row=15, indent=0, white, no italic, no underline.
        uint8_t b1 = 0, b2 = 0;
        Cea608::encodePac(a, b1, b2);
        CHECK(b1 == Cea608::PacRow15Col0WhiteB1);
        CHECK(b2 == Cea608::PacRow15Col0WhiteB2);
}

TEST_CASE("Cea608::encodePac row 1 (top) selects the right b1/b2") {
        Cea608::PacAttr a;
        a.row = 1;
        uint8_t b1 = 0, b2 = 0;
        Cea608::encodePac(a, b1, b2);
        // Row 1 uses b1 = 0x11, bit5 = 0, indent 0 white = subfield 8.
        CHECK(b1 == 0x11);
        CHECK(b2 == 0x50);
}

TEST_CASE("Cea608::encodePac coloured rows use the colour sub-field") {
        Cea608::PacAttr a;
        a.row = 15;
        a.color = Cea608::CaptionColor::Red;
        uint8_t b1 = 0, b2 = 0;
        Cea608::encodePac(a, b1, b2);
        // Row 15: b1 = 0x14, bit5 = 1.  Red colour subfield = 4 →
        // b2 = 0x40 | 0x20 | (4 << 1) | 0 = 0x68.
        CHECK(b1 == 0x14);
        CHECK(b2 == 0x68);
}

TEST_CASE("Cea608::encodePac italic forces the italic-white sub-field (7)") {
        Cea608::PacAttr a;
        a.row = 1;
        a.italic = true;
        // Italic ignores colour and indent.
        a.color = Cea608::CaptionColor::Red;
        a.indentCol = 12;
        uint8_t b1 = 0, b2 = 0;
        Cea608::encodePac(a, b1, b2);
        // Row 1 b1=0x11, bit5=0.  Italic subfield = 7 →
        // b2 = 0x40 | 0 | (7 << 1) | 0 = 0x4E.
        CHECK(b1 == 0x11);
        CHECK(b2 == 0x4E);
}

TEST_CASE("Cea608::encodePac underline flips bit 0") {
        Cea608::PacAttr a;
        a.row = 15;
        a.underline = true;
        uint8_t b1 = 0, b2 = 0;
        Cea608::encodePac(a, b1, b2);
        CHECK(b1 == 0x14);
        CHECK(b2 == (Cea608::PacRow15Col0WhiteB2 | 0x01)); // +underline
}

TEST_CASE("Cea608::encodePac indent column snaps to multiples of 4") {
        Cea608::PacAttr a;
        a.row = 15;
        a.color = Cea608::CaptionColor::White;
        a.indentCol = 11;
        uint8_t b1 = 0, b2 = 0;
        Cea608::encodePac(a, b1, b2);
        // Indent 11 → snap to 8 → subfield = 8 + 8/4 = 10 → b2 bits
        // 4..1 = 0xA → b2 = 0x40 | 0x20 | (0xA << 1) = 0x74.
        CHECK(b1 == 0x14);
        CHECK(b2 == 0x74);
}

TEST_CASE("Cea608::encodePac row out of range is clamped") {
        Cea608::PacAttr lo, hi;
        lo.row = 0;
        hi.row = 99;
        uint8_t b1 = 0, b2 = 0;
        Cea608::encodePac(lo, b1, b2);
        // Row clamps to 1 → b1 = 0x11.
        CHECK(b1 == 0x11);
        Cea608::encodePac(hi, b1, b2);
        // Row clamps to 15 → b1 = 0x14.
        CHECK(b1 == 0x14);
}

TEST_CASE("Cea608::isPac discriminates PAC from mid-row + misc") {
        // PAC: b1 in {0x10..0x17}, b2 in [0x40, 0x7F].
        CHECK(Cea608::isPac(0x14, 0x70));
        CHECK(Cea608::isPac(0x11, 0x4E));
        CHECK(Cea608::isPac(0x10, 0x40)); // Row 11
        // Not PACs:
        CHECK_FALSE(Cea608::isPac(0x11, 0x20)); // mid-row
        CHECK_FALSE(Cea608::isPac(0x14, 0x2F)); // EOC
        CHECK_FALSE(Cea608::isPac(0x00, 0x40)); // unrelated
        CHECK_FALSE(Cea608::isPac(0x14, 0x80)); // b2 out of PAC range
}

TEST_CASE("Cea608: encodePac / decodePac round-trip a fully styled cue") {
        Cea608::PacAttr in;
        in.row = 9;
        in.indentCol = 0;
        in.color = Cea608::CaptionColor::Cyan;
        in.italic = false;
        in.underline = true;
        uint8_t b1 = 0, b2 = 0;
        Cea608::encodePac(in, b1, b2);

        Cea608::PacAttr out;
        REQUIRE(Cea608::decodePac(b1, b2, out));
        CHECK(out.row == 9);
        CHECK(out.indentCol == 0);
        CHECK(out.color == Cea608::CaptionColor::Cyan);
        CHECK_FALSE(out.italic);
        CHECK(out.underline);
}

TEST_CASE("Cea608: decodePac handles indent-only and italic-only sub-fields") {
        // Indent 12 (subfield 11 = 0xB → b2 = 0x40 | 0x20 | (0xB<<1) | 0 = 0x76).
        Cea608::PacAttr out;
        REQUIRE(Cea608::decodePac(0x14, 0x76, out));
        CHECK(out.row == 15);
        CHECK(out.indentCol == 12);
        CHECK(out.color == Cea608::CaptionColor::White);
        CHECK_FALSE(out.italic);

        // Italic at row 1 (b1=0x11, b2=0x4E).
        REQUIRE(Cea608::decodePac(0x11, 0x4E, out));
        CHECK(out.row == 1);
        CHECK(out.italic);
        CHECK(out.color == Cea608::CaptionColor::White);
}

// ============================================================================
// Mid-row
// ============================================================================

TEST_CASE("Cea608::encodeMidRow / decodeMidRow round-trip") {
        // FG mid-row carries 7 primaries (no Black — code 7 is
        // "italic white" instead).  Iterate the round-trip-able fg
        // colour set; Black at the wire-fg path falls back to White
        // and is exercised separately in the BG-attribute tests.
        for (uint8_t c = 0; c < Cea608::FgCaptionColorCount; ++c) {
                for (int u = 0; u < 2; ++u) {
                        uint8_t b1 = 0, b2 = 0;
                        Cea608::encodeMidRow(static_cast<Cea608::CaptionColor>(c), false, u != 0, b1, b2);
                        REQUIRE(b1 == 0x11);
                        REQUIRE(Cea608::isMidRow(b1, b2));
                        Cea608::CaptionColor dc;
                        bool                 di = true, du = false;
                        REQUIRE(Cea608::decodeMidRow(b1, b2, dc, di, du));
                        CHECK(dc == static_cast<Cea608::CaptionColor>(c));
                        CHECK_FALSE(di);
                        CHECK(du == (u != 0));
                }
        }
        // Italic mid-row.
        uint8_t b1 = 0, b2 = 0;
        Cea608::encodeMidRow(Cea608::CaptionColor::Red, true, false, b1, b2);
        CHECK(b1 == 0x11);
        CHECK(b2 == 0x2E); // subfield 7, no underline
        Cea608::CaptionColor dc;
        bool                 di = false, du = false;
        REQUIRE(Cea608::decodeMidRow(b1, b2, dc, di, du));
        CHECK(di);
        CHECK(dc == Cea608::CaptionColor::White);
        CHECK_FALSE(du);
}

TEST_CASE("Cea608::isMidRow rejects non-mid-row pairs") {
        CHECK_FALSE(Cea608::isMidRow(0x14, 0x70)); // PAC
        CHECK_FALSE(Cea608::isMidRow(0x14, 0x2F)); // EOC misc
        CHECK_FALSE(Cea608::isMidRow(0x11, 0x40)); // PAC at row 1
        CHECK_FALSE(Cea608::isMidRow(0x11, 0x30)); // unknown range
}
