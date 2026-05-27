/**
 * @file      tests/cea608.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Jason Howard. All rights reserved.
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

TEST_CASE("Cea608::palette returns the 7 foreground primaries + Black in CaptionColor order") {
        Color::List p = Cea608::palette();
        REQUIRE(p.size() == Cea608::CaptionColorCount);
        CHECK(p[static_cast<size_t>(Cea608::CaptionColor::White)] == Color::White);
        CHECK(p[static_cast<size_t>(Cea608::CaptionColor::Green)] == Color::Green);
        CHECK(p[static_cast<size_t>(Cea608::CaptionColor::Blue)] == Color::Blue);
        CHECK(p[static_cast<size_t>(Cea608::CaptionColor::Cyan)] == Color::Cyan);
        CHECK(p[static_cast<size_t>(Cea608::CaptionColor::Red)] == Color::Red);
        CHECK(p[static_cast<size_t>(Cea608::CaptionColor::Yellow)] == Color::Yellow);
        CHECK(p[static_cast<size_t>(Cea608::CaptionColor::Magenta)] == Color::Magenta);
        // Black is BG-attribute only (the fg paths fall back to White
        // on the wire), but it lives in the palette so that BG-side
        // nearest-colour quantisation can pick it for dark inputs.
        CHECK(p[static_cast<size_t>(Cea608::CaptionColor::Black)] == Color::Black);
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
        // Row 11's b1 (0x10) has no second-row partner — b2 with bit 5
        // set (0x60..0x7F) is NOT a valid Row-11 PAC.  Mirror of the
        // decoder's gate: without this isPac would accept the pair and
        // the dispatcher would route the bytes through PAC handling
        // only for decodePac to reject them as no-ops.
        CHECK_FALSE(Cea608::isPac(0x10, 0x60));
        CHECK_FALSE(Cea608::isPac(0x10, 0x7F));
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

TEST_CASE("Cea608::isControlPair") {
        CHECK(Cea608::isControlPair(0x14, 0x20));      // CC1 RCL
        CHECK(Cea608::isControlPair(0x11, 0x2E));      // CC1 italic mid-row
        CHECK(Cea608::isControlPair(0x17, 0x21));      // CC1 Tab Offset 1
        CHECK_FALSE(Cea608::isControlPair(0x00, 0x00));// null pair
        CHECK_FALSE(Cea608::isControlPair('A', 'B'));  // info pair
        CHECK_FALSE(Cea608::isControlPair(0x20, 0x20));// info-range
}

TEST_CASE("Cea608PairDeduper: drops the second copy of a doubled control pair") {
        Cea608PairDeduper dd;
        // First copy of EOC — accepted.
        CHECK(dd.accept(0x14, 0x2F));
        // Immediate second copy — suppressed.
        CHECK_FALSE(dd.accept(0x14, 0x2F));
        // A third copy after suppression starts a fresh event.
        CHECK(dd.accept(0x14, 0x2F));
}

TEST_CASE("Cea608PairDeduper: informational pair resets the dedup window") {
        Cea608PairDeduper dd;
        CHECK(dd.accept(0x14, 0x2F)); // first copy of EOC
        CHECK(dd.accept('A', 'B'));   // info pair
        // The next EOC is a fresh control event, not a duplicate.
        CHECK(dd.accept(0x14, 0x2F));
        CHECK_FALSE(dd.accept(0x14, 0x2F)); // its immediate copy is suppressed
}

TEST_CASE("Cea608PairDeduper: distinct control pairs are not suppressed") {
        Cea608PairDeduper dd;
        CHECK(dd.accept(0x14, 0x2F)); // EOC
        CHECK(dd.accept(0x14, 0x2C)); // EDM (different code) — accepted
        CHECK_FALSE(dd.accept(0x14, 0x2C)); // EDM duplicate suppressed
}

TEST_CASE("Cea608PairDeduper: reset clears stale state") {
        Cea608PairDeduper dd;
        CHECK(dd.accept(0x14, 0x2F)); // first
        dd.reset();
        // After reset the same pair is a fresh event, not a duplicate.
        CHECK(dd.accept(0x14, 0x2F));
}

TEST_CASE("Cea608PairDeduper: informational pairs always pass through") {
        Cea608PairDeduper dd;
        CHECK(dd.accept('A', 'B'));
        CHECK(dd.accept('A', 'B')); // identical info pair — still accepted
}

// ============================================================================
// Foreground Black (FA / FAU) + Background Transparent (BT) + Tab Offset
// round-trips per CTA-608-E §6.2 Table 3.
// ============================================================================

TEST_CASE("Cea608::encodeFgBlack / decodeFgBlack round-trip both FA and FAU") {
        // FA — Foreground Black, no underline.
        uint8_t b1 = 0, b2 = 0;
        Cea608::encodeFgBlack(/*underline*/ false, b1, b2);
        CHECK(b1 == Cea608::Cc1ExtAttrB1);
        CHECK(b2 == Cea608::FaB2);
        CHECK(Cea608::isFgBlack(b1, b2));
        bool du = true;
        REQUIRE(Cea608::decodeFgBlack(b1, b2, du));
        CHECK_FALSE(du);

        // FAU — Foreground Black, underline.
        Cea608::encodeFgBlack(/*underline*/ true, b1, b2);
        CHECK(b1 == Cea608::Cc1ExtAttrB1);
        CHECK(b2 == Cea608::FauB2);
        CHECK(Cea608::isFgBlack(b1, b2));
        du = false;
        REQUIRE(Cea608::decodeFgBlack(b1, b2, du));
        CHECK(du);

        // Non-FgBlack pairs are rejected by decodeFgBlack.
        du = false;
        CHECK_FALSE(Cea608::decodeFgBlack(Cea608::Cc1ExtAttrB1, Cea608::BtB2, du));
        CHECK_FALSE(Cea608::decodeFgBlack(0x14, 0x2F, du)); // EOC
}

TEST_CASE("Cea608::encodeBackgroundTransparency / isBt round-trip") {
        uint8_t b1 = 0, b2 = 0;
        Cea608::encodeBackgroundTransparency(b1, b2);
        CHECK(b1 == Cea608::Cc1ExtAttrB1);
        CHECK(b2 == Cea608::BtB2);
        CHECK(Cea608::isBt(b1, b2));
        // FA / FAU share b1 with BT but live at distinct b2 values —
        // isBt must reject them.
        CHECK_FALSE(Cea608::isBt(Cea608::Cc1ExtAttrB1, Cea608::FaB2));
        CHECK_FALSE(Cea608::isBt(Cea608::Cc1ExtAttrB1, Cea608::FauB2));
}

TEST_CASE("Cea608::encodeTabOffset / decodeTabOffset round-trip 1..3 columns") {
        for (int c = 1; c <= 3; ++c) {
                uint8_t b1 = 0, b2 = 0;
                Cea608::encodeTabOffset(c, b1, b2);
                CHECK(b1 == Cea608::Cc1ExtAttrB1);
                CHECK(b2 == static_cast<uint8_t>(0x20 + c));
                CHECK(Cea608::isTabOffset(b1, b2));
                int decoded = 0;
                REQUIRE(Cea608::decodeTabOffset(b1, b2, decoded));
                CHECK(decoded == c);
        }
        // Out-of-range columns clamp to the nearest in-range value.
        uint8_t b1 = 0, b2 = 0;
        Cea608::encodeTabOffset(0, b1, b2);
        CHECK(b2 == Cea608::TabOffsetT1);
        Cea608::encodeTabOffset(99, b1, b2);
        CHECK(b2 == Cea608::TabOffsetT3);

        // Non-TabOffset pairs are rejected by decodeTabOffset.
        int decoded = 0;
        CHECK_FALSE(Cea608::decodeTabOffset(Cea608::Cc1ExtAttrB1, Cea608::BtB2, decoded));
        CHECK_FALSE(Cea608::decodeTabOffset(0x14, 0x2F, decoded)); // EOC
}

// ============================================================================
// Channel retargeting (CTA-608-E §8.4)
// ============================================================================

TEST_CASE("Cea608::applyChannel leaves CC1 control bytes alone") {
        // CC1 / Field 1 / Channel 1: no remap.
        CHECK(Cea608::applyChannel(0x14, 0x2F, Cea608::Channel::CC1) == 0x14);
        CHECK(Cea608::applyChannel(0x11, 0x20, Cea608::Channel::CC1) == 0x11);
        CHECK(Cea608::applyChannel(0x17, 0x21, Cea608::Channel::CC1) == 0x17);
}

TEST_CASE("Cea608::applyChannel OR's bit 3 for CC2 / CC4 (second channel of field)") {
        // CC2: F1, channel 2 — OR 0x08.
        CHECK(Cea608::applyChannel(0x14, 0x2F, Cea608::Channel::CC2) == 0x1C);
        CHECK(Cea608::applyChannel(0x11, 0x20, Cea608::Channel::CC2) == 0x19);
        CHECK(Cea608::applyChannel(0x17, 0x21, Cea608::Channel::CC2) == 0x1F);
        // CC4: F2, channel 2 — OR 0x08 then F2 misc remap.
        // 0x14 + 0x08 = 0x1C; b2=0x2F is in the misc range so 0x1C → 0x1D.
        CHECK(Cea608::applyChannel(0x14, 0x2F, Cea608::Channel::CC4) == 0x1D);
        // 0x11 + 0x08 = 0x19; not in {0x14, 0x1C} so no remap.
        CHECK(Cea608::applyChannel(0x11, 0x20, Cea608::Channel::CC4) == 0x19);
        // 0x17 + 0x08 = 0x1F; not in {0x14, 0x1C} so no remap.
        CHECK(Cea608::applyChannel(0x17, 0x21, Cea608::Channel::CC4) == 0x1F);
}

TEST_CASE("Cea608::applyChannel triggers F2 misc-control remap on CC3") {
        // CC3: F2, channel 1 — no channel-bit OR, but F2 misc remap.
        // 0x14 + b2=0x2F (EOC) → 0x15.
        CHECK(Cea608::applyChannel(0x14, 0x2F, Cea608::Channel::CC3) == 0x15);
        // 0x14 + b2=0x20 (RCL) → 0x15.
        CHECK(Cea608::applyChannel(0x14, 0x20, Cea608::Channel::CC3) == 0x15);
        // 0x14 + b2=0x70 (Row 15 PAC) — b2 outside misc range, NOT
        // remapped.  This is the load-bearing guard: PACs at Row 14 /
        // Row 15 also use b1=0x14 but with b2 in [0x40, 0x7F].
        CHECK(Cea608::applyChannel(0x14, 0x70, Cea608::Channel::CC3) == 0x14);
        // 0x11 (Row 1/2 PAC) — not in {0x14, 0x1C}, never remapped.
        CHECK(Cea608::applyChannel(0x11, 0x20, Cea608::Channel::CC3) == 0x11);
}

TEST_CASE("Cea608::applyChannel leaves non-control + null bytes unchanged") {
        // Informational character pair (b1 >= 0x20) — channel bit
        // lives only on control bytes.
        for (auto ch : {Cea608::Channel::CC1, Cea608::Channel::CC2,
                        Cea608::Channel::CC3, Cea608::Channel::CC4}) {
                CHECK(Cea608::applyChannel('A', 'B', ch) == 'A');
                CHECK(Cea608::applyChannel(0x00, 0x00, ch) == 0x00);
                CHECK(Cea608::applyChannel(0x7F, 0x7F, ch) == 0x7F);
        }
}

TEST_CASE("Cea608::applyChannelInPlace mutates b1 to match the value-returning form") {
        uint8_t b1 = 0x14;
        Cea608::applyChannelInPlace(b1, /*b2*/ 0x2F, Cea608::Channel::CC4);
        CHECK(b1 == 0x1D);

        b1 = 0x14;
        Cea608::applyChannelInPlace(b1, /*b2*/ 0x70, Cea608::Channel::CC4);
        CHECK(b1 == 0x1C); // PAC b2 — channel OR'd but NOT remapped.
}

// ============================================================================
// XDS framing predicates (CTA-608-E §9.3)
// ============================================================================

TEST_CASE("Cea608::isXdsControl / isXdsTerminator") {
        // Class Start codes (odd): 0x01, 0x03, 0x05, 0x07, 0x09, 0x0B, 0x0D.
        for (uint8_t b1 : {0x01, 0x03, 0x05, 0x07, 0x09, 0x0B, 0x0D}) {
                CHECK(Cea608::isXdsControl(b1));
        }
        // Class Continue codes (even): 0x02, 0x04, 0x06, 0x08, 0x0A, 0x0C, 0x0E.
        for (uint8_t b1 : {0x02, 0x04, 0x06, 0x08, 0x0A, 0x0C, 0x0E}) {
                CHECK(Cea608::isXdsControl(b1));
        }
        // Boundaries.
        CHECK_FALSE(Cea608::isXdsControl(0x00));
        CHECK_FALSE(Cea608::isXdsControl(0x0F)); // terminator, not control
        CHECK_FALSE(Cea608::isXdsControl(0x10)); // caption control range
        CHECK_FALSE(Cea608::isXdsControl(0x20)); // informational range

        // Terminator pair: b1 == 0x0F (b2 carries the checksum byte).
        CHECK(Cea608::isXdsTerminator(0x0F, 0x00));
        CHECK(Cea608::isXdsTerminator(0x0F, 0x7F));
        CHECK_FALSE(Cea608::isXdsTerminator(0x0E, 0x00));
        CHECK_FALSE(Cea608::isXdsTerminator(0x10, 0x00));
}
