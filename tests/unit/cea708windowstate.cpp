/**
 * @file      cea708windowstate.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdint>
#include <doctest/doctest.h>
#include <promeki/cea708windowstate.h>
#include <promeki/color.h>
#include <promeki/enums.h>
#include <promeki/string.h>
#include <promeki/subtitle.h>

using namespace promeki;

namespace {

        /// @brief Minimal DefineWindow command (DF0) — sets window 0
        ///        visible, single row, @p cols columns, no special
        ///        attributes.  Returns the 7-byte command sequence.
        std::vector<uint8_t> defineWindow0(uint8_t cols, bool visible = true) {
                // DF0 = 0x98, six argument bytes follow.  CEA-708-E
                // §8.10.5.2 wire layout:
                //   parm1: priority(bits 0..2) | col_lock(bit 3, 0x08)
                //          | row_lock(bit 4, 0x10) | visible(bit 5, 0x20)
                //          (bits 6, 7 reserved, must be 0).
                //   parm2: relative_pos(bit 7, 0x80) | anchor_v(bits 0..6)
                //   parm3: anchor_h
                //   parm4: anchor_point(high nibble) | row_count(low nibble)
                //   parm5: col_count(bits 0..5)
                //   parm6: window_style(bits 3..5) | pen_style(bits 0..2)
                //
                // row_count and col_count on the wire encode count-1.
                const uint8_t rowCountWire = 0; // 1 row
                const uint8_t colCountWire = static_cast<uint8_t>((cols > 0 ? cols - 1 : 0) & 0x3F);
                return {0x98,
                        static_cast<uint8_t>((visible ? 0x20 : 0x00) | 0x18 /* row+col locks */),
                        0x00,
                        0x00,
                        static_cast<uint8_t>(0x10 /* anchor 1 */ | rowCountWire),
                        colCountWire,
                        0x00};
        }

} // namespace

// ============================================================================
// Cea708Window — character grid mechanics
// ============================================================================

TEST_CASE("Cea708Window: resize zeroes the grid and resets the pen") {
        Cea708Window w;
        w.resize(2, 4);
        CHECK(w.rowCount == 2);
        CHECK(w.colCount == 4);
        CHECK(w.penRow == 0);
        CHECK(w.penCol == 0);
        CHECK(w.isEmpty());
}

TEST_CASE("Cea708Window::putChar fills cells and advances the pen") {
        Cea708Window w;
        w.resize(1, 5);
        w.putChar('H');
        w.putChar('i');
        CHECK(w.penCol == 2);
        CHECK(w.text() == "Hi");
}

TEST_CASE("Cea708Window::putChar replaces final char at column boundary (§8.4.8)") {
        // Per CEA-708-E §8.4.8: "Characters entered into a row when the
        // cursor is at the final character position shall either replace
        // the final character or be discarded."  Library picks the
        // "replace the final character" branch — the new char overwrites
        // the last cell rather than wrapping to the next row.
        Cea708Window w;
        w.resize(3, 2);
        w.putChar('A');
        w.putChar('B');
        w.putChar('C'); // pen is past col 1 → replaces final char.
        w.putChar('D'); // still past col 1 → replaces again.
        // Row 0: "AD" (B → C → D overwrote the final cell).
        CHECK(w.text() == "AD");
}

TEST_CASE("Cea708Window::carriageReturn moves to next row + col 0") {
        Cea708Window w;
        w.resize(2, 4);
        w.putChar('X');
        w.carriageReturn();
        w.putChar('Y');
        CHECK(w.text() == "X\nY");
}

TEST_CASE("Cea708Window: pen falling off bottom rolls rows up") {
        Cea708Window w;
        w.resize(2, 2);
        w.putChar('A');
        w.putChar('B');
        w.carriageReturn();
        w.putChar('C');
        w.putChar('D');
        w.carriageReturn();  // would land row 2 — rolls AB off.
        w.putChar('E');
        w.putChar('F');
        // After roll: row 0 = "CD", row 1 = "EF".
        CHECK(w.text() == "CD\nEF");
}

// ============================================================================
// Cea708WindowState — defaults
// ============================================================================

TEST_CASE("Cea708WindowState: default is fully reset (no visible windows)") {
        Cea708WindowState st;
        CHECK(st.currentWindowId() == 0);
        CHECK_FALSE(st.anyVisible());
        CHECK(st.visibleText() == "");
}

TEST_CASE("Cea708WindowState: setCurrentWindowId clamps to [0,7]") {
        Cea708WindowState st;
        st.setCurrentWindowId(3);
        CHECK(st.currentWindowId() == 3);
        st.setCurrentWindowId(99);
        CHECK(st.currentWindowId() == 7);
        st.setCurrentWindowId(-5);
        CHECK(st.currentWindowId() == 0);
}

// ============================================================================
// G0 / G1 character writes
// ============================================================================

TEST_CASE("Cea708WindowState: DefineWindow + G0 chars + DisplayWindows yields visible text") {
        Cea708WindowState        st;
        std::vector<uint8_t>     cmds = defineWindow0(8, /*visible*/ true);
        // After DefineWindow, write "HELLO".
        cmds.push_back('H');
        cmds.push_back('E');
        cmds.push_back('L');
        cmds.push_back('L');
        cmds.push_back('O');
        st.processBytes(cmds.data(), cmds.size());
        CHECK(st.anyVisible());
        CHECK(st.visibleText() == "HELLO");
}

TEST_CASE("Cea708WindowState: G1 byte 0xA0..0xFF maps to Latin-1 codepoint") {
        Cea708WindowState        st;
        std::vector<uint8_t>     cmds = defineWindow0(4, true);
        cmds.push_back(0xC9); // 'É' (Latin-1 0xC9 → U+00C9)
        st.processBytes(cmds.data(), cmds.size());
        CHECK(st.visibleText() == "\xC3\x89"); // UTF-8 for U+00C9.
}

TEST_CASE("Cea708WindowState: G0 0x7F maps to U+266A music note") {
        Cea708WindowState        st;
        std::vector<uint8_t>     cmds = defineWindow0(2, true);
        cmds.push_back(0x7F);
        st.processBytes(cmds.data(), cmds.size());
        CHECK(st.visibleText() == "\xE2\x99\xAA"); // UTF-8 for U+266A.
}

// ============================================================================
// C0 control codes
// ============================================================================

TEST_CASE("Cea708WindowState: 0x0D (CR) advances to next row") {
        Cea708WindowState    st;
        std::vector<uint8_t> cmds = defineWindow0(3, true);
        // Define has only 1 row (rowCountWire=0); use 2 rows instead.
        // Override row count: re-issue DF0 with row_count_wire=1 (=2 rows).
        // parm1 = visible(0x20) | row_lock(0x10) | col_lock(0x08) = 0x38.
        cmds = {0x98, 0x38, 0x00, 0x00, 0x11 /* anchor=1, rows=2 */, 0x02 /* 3 cols */, 0x00};
        cmds.push_back('A');
        cmds.push_back(0x0D); // CR
        cmds.push_back('B');
        st.processBytes(cmds.data(), cmds.size());
        CHECK(st.visibleText() == "A\nB");
}

TEST_CASE("Cea708WindowState: 0x0C (FF) clears the current window") {
        Cea708WindowState    st;
        std::vector<uint8_t> cmds = defineWindow0(4, true);
        cmds.push_back('A');
        cmds.push_back('B');
        cmds.push_back(0x0C); // FF — clear
        cmds.push_back('X');
        st.processBytes(cmds.data(), cmds.size());
        CHECK(st.visibleText() == "X");
}

TEST_CASE("Cea708WindowState: 0x08 (BS) moves cursor back; overwrite replaces prior cell") {
        Cea708WindowState    st;
        std::vector<uint8_t> cmds = defineWindow0(4, true);
        cmds.push_back('A');
        cmds.push_back('B');
        cmds.push_back(0x08); // BS — pen moves back to col 1, B stays in the grid
        cmds.push_back('C'); // C overwrites B at col 1
        st.processBytes(cmds.data(), cmds.size());
        CHECK(st.visibleText() == "AC");
}

TEST_CASE("Cea708WindowState: 0x08 (BS) alone does not erase the cell (§7.1.4.1)") {
        // Per CEA-708-E §7.1.4.1 / §8.10.5, BS only moves the cursor; it does NOT
        // erase.  A caller wanting to erase must explicitly write a space.
        Cea708WindowState    st;
        std::vector<uint8_t> cmds = defineWindow0(4, true);
        cmds.push_back('A');
        cmds.push_back('B');
        cmds.push_back(0x08); // BS — must NOT erase B
        st.processBytes(cmds.data(), cmds.size());
        CHECK(st.visibleText() == "AB");
}

TEST_CASE("Cea708WindowState: 0x08 (BS) never crosses a row boundary") {
        // BS at column 0 is a no-op; cursor stays put.  defineWindow0 is
        // hard-coded to row_count_wire=0 (one row), so a multi-row DF0
        // is issued by hand here.  parm1 = visible(0x20) + locks(0x18).
        Cea708WindowState    st;
        std::vector<uint8_t> cmds = {0x98, 0x38, 0x00, 0x00,
                                     0x11 /* anchor=1, rows=2 */,
                                     0x03 /* 4 cols */, 0x00};
        cmds.push_back('A');
        cmds.push_back(0x0D); // CR → next row, col 0
        cmds.push_back(0x08); // BS at col 0 of row 1 — must NOT back up into row 0
        cmds.push_back('B');
        st.processBytes(cmds.data(), cmds.size());
        CHECK(st.visibleText() == "A\nB");
}

// ============================================================================
// C1 window-manipulation commands
// ============================================================================

TEST_CASE("Cea708WindowState: HDW hides previously-visible windows") {
        Cea708WindowState    st;
        std::vector<uint8_t> cmds = defineWindow0(4, true);
        cmds.push_back('A');
        cmds.push_back(0x8A); // HDW
        cmds.push_back(0x01); // window 0
        st.processBytes(cmds.data(), cmds.size());
        CHECK_FALSE(st.anyVisible());
        CHECK(st.visibleText() == "");
}

TEST_CASE("Cea708WindowState: DSW shows previously-defined hidden windows") {
        Cea708WindowState    st;
        std::vector<uint8_t> cmds = defineWindow0(4, /*visible*/ false);
        cmds.push_back('A');
        // No characters visible while window is hidden.
        st.processBytes(cmds.data(), cmds.size());
        CHECK_FALSE(st.anyVisible());
        std::vector<uint8_t> show{0x89, 0x01};
        st.processBytes(show.data(), show.size());
        CHECK(st.anyVisible());
        CHECK(st.visibleText() == "A");
}

TEST_CASE("Cea708WindowState: TGW toggles visibility") {
        Cea708WindowState    st;
        std::vector<uint8_t> cmds = defineWindow0(4, true);
        cmds.push_back('A');
        // Toggle window 0 off then on.
        cmds.push_back(0x8B);
        cmds.push_back(0x01);
        st.processBytes(cmds.data(), cmds.size());
        CHECK_FALSE(st.anyVisible());
        std::vector<uint8_t> tgw{0x8B, 0x01};
        st.processBytes(tgw.data(), tgw.size());
        CHECK(st.anyVisible());
}

TEST_CASE("Cea708WindowState: CLW clears the window's character grid") {
        Cea708WindowState    st;
        std::vector<uint8_t> cmds = defineWindow0(4, true);
        cmds.push_back('A');
        cmds.push_back('B');
        cmds.push_back(0x88); // CLW
        cmds.push_back(0x01); // window 0 bitmap
        st.processBytes(cmds.data(), cmds.size());
        CHECK(st.visibleText() == ""); // grid cleared but window still visible
        CHECK(st.anyVisible());
}

TEST_CASE("Cea708WindowState: DLW deletes the window entirely") {
        Cea708WindowState    st;
        std::vector<uint8_t> cmds = defineWindow0(4, true);
        cmds.push_back('A');
        cmds.push_back(0x8C); // DLW
        cmds.push_back(0x01); // window 0
        st.processBytes(cmds.data(), cmds.size());
        CHECK_FALSE(st.anyVisible());
        CHECK_FALSE(st.window(0).defined);
}

TEST_CASE("Cea708WindowState: CW0..CW7 selects the current window") {
        Cea708WindowState    st;
        // Define window 0 and window 1 separately.
        std::vector<uint8_t> defs;
        // DF0: row=1, col=4, visible.
        std::vector<uint8_t> def0 = defineWindow0(4, true);
        defs.insert(defs.end(), def0.begin(), def0.end());
        // DF1: same shape.
        std::vector<uint8_t> def1 = def0;
        def1[0] = 0x99; // DF1
        defs.insert(defs.end(), def1.begin(), def1.end());
        // After DF1, current window is 1.  Write 'B'.
        defs.push_back('B');
        // Switch back to window 0 (CW0 = 0x80), write 'A'.
        defs.push_back(0x80);
        defs.push_back('A');
        st.processBytes(defs.data(), defs.size());
        CHECK(st.window(0).text() == "A");
        CHECK(st.window(1).text() == "B");
        // Both visible → flattened text joins with \n (priority equal → id order).
        const String flat = st.visibleText();
        CHECK(flat == "A\nB");
}

TEST_CASE("Cea708WindowState: SPL repositions the pen to (row, col)") {
        Cea708WindowState    st;
        // parm1 = visible(0x20) | row_lock(0x10) | col_lock(0x08) = 0x38.
        std::vector<uint8_t> cmds{0x98,
                                  0x38,
                                  0x00,
                                  0x00,
                                  0x11,
                                  0x03,
                                  0x00}; // rows=2 cols=4
        // Move pen to row 1, col 2.
        cmds.push_back(0x92); // SPL
        cmds.push_back(0x01); // row 1
        cmds.push_back(0x02); // col 2
        cmds.push_back('X');
        st.processBytes(cmds.data(), cmds.size());
        // Row 0 empty, row 1 has X at col 2 — visible text strips trailing
        // padding but preserves leading spaces.
        CHECK(st.visibleText() == "  X");
}

TEST_CASE("Cea708WindowState: RST clears every window") {
        Cea708WindowState    st;
        std::vector<uint8_t> cmds = defineWindow0(4, true);
        cmds.push_back('A');
        cmds.push_back(0x8F); // RST
        st.processBytes(cmds.data(), cmds.size());
        CHECK_FALSE(st.anyVisible());
        CHECK(st.visibleText() == "");
}

TEST_CASE("Cea708WindowState: SPA text_tag is decoded into the current window's pen") {
        Cea708WindowState    st;
        std::vector<uint8_t> cmds = defineWindow0(2, true);
        // SPA byte 1 layout: text_tag<<4 | offset<<2 | pen_size.
        // text_tag = 0x08 (Lyrics), offset = 1 (Normal), pen_size = 1 (Standard).
        // byte1 = (0x08 << 4) | (0x01 << 2) | 0x01 = 0x85
        cmds.push_back(0x90);
        cmds.push_back(0x85);
        cmds.push_back(0x00);
        cmds.push_back('A');
        st.processBytes(cmds.data(), cmds.size());
        // Pen state on the current window picks up the text_tag.
        CHECK(st.currentPen().textTag == SubtitleTextTag::Lyrics);
        // The cell written after SPA inherits the same tag.
        const Cea708Cell &cell = st.window(0).grid[0][0];
        CHECK(cell.codepoint == 'A');
        CHECK(cell.pen.textTag == SubtitleTextTag::Lyrics);
}

TEST_CASE("Cea708WindowState: SPA text_tag reserved values round-trip as their reserved enum") {
        Cea708WindowState    st;
        std::vector<uint8_t> cmds = defineWindow0(2, true);
        // text_tag = 0x0D (Reserved13), offset = 1, pen_size = 1.
        cmds.push_back(0x90);
        cmds.push_back(static_cast<uint8_t>((0x0D << 4) | (0x01 << 2) | 0x01));
        cmds.push_back(0x00);
        st.processBytes(cmds.data(), cmds.size());
        CHECK(st.currentPen().textTag == SubtitleTextTag::Reserved13);
}

TEST_CASE("Cea708WindowState: SPA / SPC / SWA are consumed without affecting text") {
        Cea708WindowState    st;
        std::vector<uint8_t> cmds = defineWindow0(4, true);
        cmds.push_back('A');
        // SPA (2-byte payload): 0x90 + 2 args
        cmds.push_back(0x90);
        cmds.push_back(0x00);
        cmds.push_back(0x00);
        // SPC (3-byte payload): 0x91 + 3 args
        cmds.push_back(0x91);
        cmds.push_back(0x00);
        cmds.push_back(0x00);
        cmds.push_back(0x00);
        // SWA (4-byte payload): 0x97 + 4 args
        cmds.push_back(0x97);
        cmds.push_back(0x00);
        cmds.push_back(0x00);
        cmds.push_back(0x00);
        cmds.push_back(0x00);
        cmds.push_back('B');
        st.processBytes(cmds.data(), cmds.size());
        CHECK(st.visibleText() == "AB");
}

// ============================================================================
// SetWindowAttributes (SWA, 0x97)
// ============================================================================

TEST_CASE("Cea708WindowState: SWA fill_color decodes from a max-red opaque payload") {
        Cea708WindowState    st;
        std::vector<uint8_t> cmds = defineWindow0(2, true);
        // SWA: fill_opacity=Solid (0 → 00), fill_R=3, fill_G=0, fill_B=0
        // → byte1 = 0b00_11_00_00 = 0x30
        // (CEA-708-D §8.10.5.10: opacity wire bits 00=Solid, 11=Transparent.)
        cmds.push_back(0x97);
        cmds.push_back(0x30);
        cmds.push_back(0x00);
        cmds.push_back(0x00);
        cmds.push_back(0x00);
        cmds.push_back('A');
        st.processBytes(cmds.data(), cmds.size());
        const Cea708WindowAttr &a = st.window(0).attrs;
        CHECK(a.fillColor.isValid());
        CHECK(a.fillColor.r8() == 255);
        CHECK(a.fillColor.g8() == 0);
        CHECK(a.fillColor.b8() == 0);
        CHECK(a.fillOpacity == SubtitleOpacity::Solid);
}

TEST_CASE("Cea708WindowState: SWA fill_opacity=Transparent clears fillColor") {
        Cea708WindowState    st;
        std::vector<uint8_t> cmds = defineWindow0(2, true);
        // fill_opacity=Transparent (wire bits 11 = enum value 3), no fill RGB.
        cmds.push_back(0x97);
        cmds.push_back(static_cast<uint8_t>(SubtitleOpacity::Transparent.value() << 6));
        cmds.push_back(0x00);
        cmds.push_back(0x00);
        cmds.push_back(0x00);
        st.processBytes(cmds.data(), cmds.size());
        CHECK_FALSE(st.window(0).attrs.fillColor.isValid());
        CHECK(st.window(0).attrs.fillOpacity == SubtitleOpacity::Transparent);
}

TEST_CASE("Cea708WindowState: SWA reserved border_type 6 / 7 clamps to None") {
        Cea708WindowState st;
        // border_type wire field is 3 bits split across a2[7:6] and a3[7].
        // Encode raw value 6 = 0b110: a2[7:6] = 0b10 (= 2), a3[7] = 1.
        // a2 = 0b10_000000 = 0x80;  a3 = 0b1_0000000 = 0x80.
        std::vector<uint8_t> cmds = defineWindow0(2, true);
        cmds.push_back(0x97);
        cmds.push_back(0x00);
        cmds.push_back(0x80);
        cmds.push_back(0x80);
        cmds.push_back(0x00);
        st.processBytes(cmds.data(), cmds.size());
        // Reserved value 6 must clamp to None (0); borderColor stays unset.
        CHECK(st.window(0).attrs.borderType == 0);
        CHECK_FALSE(st.window(0).attrs.borderColor.isValid());

        Cea708WindowState st7;
        // Raw value 7 = 0b111: a2[7:6] = 0b11, a3[7] = 1.
        cmds = defineWindow0(2, true);
        cmds.push_back(0x97);
        cmds.push_back(0x00);
        cmds.push_back(0xC0);
        cmds.push_back(0x80);
        cmds.push_back(0x00);
        st7.processBytes(cmds.data(), cmds.size());
        CHECK(st7.window(0).attrs.borderType == 0);
}

TEST_CASE("Cea708WindowState: SWA byte3 carries justify / wordwrap / scroll / print direction") {
        Cea708WindowState    st;
        std::vector<uint8_t> cmds = defineWindow0(2, true);
        // byte3: border_type2=0, wordwrap=1 (bit 6), print_dir=1 (bits 5..4),
        //        scroll_dir=2 (bits 3..2), justify=2 (bits 1..0)
        // → 0_1_01_10_10 = 0b01_01_10_10 = 0x5A
        cmds.push_back(0x97);
        cmds.push_back(0x00);
        cmds.push_back(0x00);
        cmds.push_back(0x5A);
        cmds.push_back(0x00);
        st.processBytes(cmds.data(), cmds.size());
        const Cea708WindowAttr &a = st.window(0).attrs;
        CHECK(a.wordWrap == true);
        CHECK(a.printDirection == 1);
        CHECK(a.scrollDirection == 2);
        CHECK(a.justify == 2);
}

TEST_CASE("Cea708WindowState: SWA byte4 carries effect speed / direction / display effect") {
        Cea708WindowState    st;
        std::vector<uint8_t> cmds = defineWindow0(2, true);
        // byte4: effect_speed=5 (bits 7..4), effect_dir=1 (bits 3..2), display_effect=2 (bits 1..0)
        // → 0101_01_10 = 0x56
        cmds.push_back(0x97);
        cmds.push_back(0x00);
        cmds.push_back(0x00);
        cmds.push_back(0x00);
        cmds.push_back(0x56);
        st.processBytes(cmds.data(), cmds.size());
        const Cea708WindowAttr &a = st.window(0).attrs;
        CHECK(a.effectSpeed == 5);
        CHECK(a.effectDirection == 1);
        CHECK(a.displayEffect == 2);
}

TEST_CASE("Cea708WindowState: SWA border_type combines byte2 (low 2 bits) + byte3 (high bit)") {
        Cea708WindowState    st;
        std::vector<uint8_t> cmds = defineWindow0(2, true);
        // border_type = 5 (ShadowRight) → low 2 bits = 0b01 (in byte2 << 6),
        //                                   high bit  = 0b1 (in byte3 << 7)
        cmds.push_back(0x97);
        cmds.push_back(0x00);
        cmds.push_back(static_cast<uint8_t>(0x01 << 6)); // border_type01=01
        cmds.push_back(static_cast<uint8_t>(0x80));      // border_type2=1
        cmds.push_back(0x00);
        st.processBytes(cmds.data(), cmds.size());
        CHECK(st.window(0).attrs.borderType == 5);
}

TEST_CASE("Cea708Window::visibleSpans applies SWA fill to spans without explicit bg") {
        Cea708WindowState    st;
        std::vector<uint8_t> cmds = defineWindow0(2, true);
        // SWA with opaque green fill (G channel = 3 → max).
        // byte1: fill_op=Solid (00) | fill_R=0 | fill_G=3 | fill_B=0 → 0x0C
        cmds.push_back(0x97);
        cmds.push_back(0x0C);
        cmds.push_back(0x00);
        cmds.push_back(0x00);
        cmds.push_back(0x00);
        // SPC asserting transparent per-cell bg.  Pen Style #1 (the
        // default preloaded on DF0 with ps=0 per CEA-708-E §8.10.5.2)
        // sets per-character bg to solid black, which would paint over
        // the SWA window fill on receive.  Real encoders emit SPC
        // right after DF0 to bring the pen to a known state — mirror
        // that here so the SWA window fill is what shows through.
        //   byte1 (fg): Solid white (Solid=00, R=3, G=3, B=3)  = 0x3F
        //   byte2 (bg): Transparent (11), RGB ignored          = 0xC0
        //   byte3 (edge): all zero
        cmds.push_back(0x91);
        cmds.push_back(0x3F);
        cmds.push_back(0xC0);
        cmds.push_back(0x00);
        cmds.push_back('H');
        cmds.push_back('I');
        st.processBytes(cmds.data(), cmds.size());
        SubtitleSpan::List spans = st.visibleSpans();
        REQUIRE(spans.size() == 1);
        CHECK(spans[0].text() == "HI");
        CHECK(spans[0].backgroundColor().isValid());
        CHECK(spans[0].backgroundColor().g8() == 255);
}

TEST_CASE("Cea708Window::visibleSpans: per-cell SPC bg wins over SWA fill") {
        Cea708WindowState    st;
        std::vector<uint8_t> cmds = defineWindow0(2, true);
        // SWA: opaque red fill.  byte1 = fill_op=Solid (00) << 6 | fill_R=3 << 4 = 0x30
        cmds.push_back(0x97);
        cmds.push_back(0x30);
        cmds.push_back(0x00);
        cmds.push_back(0x00);
        cmds.push_back(0x00);
        // SPC asserting opaque blue per-cell bg before writing chars.
        // byte1 (fg): default white (Solid=00, R=3, G=3, B=3) = 0x3F
        // byte2 (bg): Solid(00) | R=0 | G=0 | B=3                = 0x03
        // byte3 (edge): all zero
        cmds.push_back(0x91);
        cmds.push_back(0x3F);
        cmds.push_back(0x03);
        cmds.push_back(0x00);
        cmds.push_back('B');
        st.processBytes(cmds.data(), cmds.size());
        SubtitleSpan::List spans = st.visibleSpans();
        REQUIRE(spans.size() == 1);
        // Per-cell SPC bg (blue) wins over the window-level SWA fill (red).
        CHECK(spans[0].backgroundColor().b8() == 255);
        CHECK(spans[0].backgroundColor().r8() == 0);
}

// ============================================================================
// Robustness
// ============================================================================

TEST_CASE("Cea708WindowState: malformed bytes don't deadlock the parser") {
        // A run of "reserved" bytes that don't decode to anything.
        std::vector<uint8_t> cmds = defineWindow0(4, true);
        cmds.push_back('A');
        cmds.push_back(0x01); // reserved C0
        cmds.push_back(0x02); // reserved C0
        cmds.push_back('B');
        Cea708WindowState st;
        st.processBytes(cmds.data(), cmds.size());
        CHECK(st.visibleText() == "AB");
}

TEST_CASE("Cea708WindowState: truncated multi-byte command consumes available bytes") {
        // SPC has 3 args; provide only 2 then EOB.  Parser should
        // not crash and not produce text.
        std::vector<uint8_t> cmds = defineWindow0(4, true);
        cmds.push_back(0x91); // SPC
        cmds.push_back(0x00);
        cmds.push_back(0x00);
        // EOB — third arg missing.
        Cea708WindowState st;
        st.processBytes(cmds.data(), cmds.size());
        // No exception; window 0 is still set up.
        CHECK(st.anyVisible());
}

TEST_CASE("Cea708WindowState: EXT1 + G2 byte decodes to the mapped codepoint") {
        // G2 0x32 → U+2019 RIGHT SINGLE QUOTATION MARK (close single quote).
        std::vector<uint8_t> cmds = defineWindow0(4, true);
        cmds.push_back(0x10); // EXT1
        cmds.push_back(0x32); // G2: close single quote
        Cea708WindowState st;
        st.processBytes(cmds.data(), cmds.size());
        // U+2019 in UTF-8: 0xE2 0x80 0x99.
        CHECK(st.visibleText() == "\xE2\x80\x99");
}

TEST_CASE("Cea708WindowState: EXT1 + reserved G2 byte falls back to G0 underscore") {
        // 0x22 is one of the reserved positions in the G2 table — no
        // defined glyph.  CEA-708-E §9.3 mandates G0 underscore (0x5F)
        // as the substitute for unsupported G3 characters; the library
        // extends that rule to undefined G2 positions for the same
        // reason (a reserved future-extension code shouldn't surface
        // as a "missing glyph" U+FFFD box).
        std::vector<uint8_t> cmds = defineWindow0(4, true);
        cmds.push_back(0x10); // EXT1
        cmds.push_back(0x22); // reserved G2 position
        Cea708WindowState st;
        st.processBytes(cmds.data(), cmds.size());
        CHECK(st.visibleText() == "_");
}

TEST_CASE("Cea708WindowState: EXT1 + 0xA0 decodes to the ATSC CC logo (U+E000)") {
        // The lone defined G3 position carries the ATSC CC logo, which
        // the library maps to a Private Use Area codepoint so the
        // glyph round-trips.
        std::vector<uint8_t> cmds = defineWindow0(4, true);
        cmds.push_back(0x10); // EXT1
        cmds.push_back(0xA0); // G3: ATSC CC logo
        Cea708WindowState st;
        st.processBytes(cmds.data(), cmds.size());
        // U+E000 in UTF-8: 0xEE 0x80 0x80.
        CHECK(st.visibleText() == "\xEE\x80\x80");
}

TEST_CASE("Cea708WindowState: P16 with a UTF-16 surrogate pair decodes to one astral codepoint") {
        // U+1D11E (MUSICAL SYMBOL G CLEF) — surrogate pair D834 DD1E.
        std::vector<uint8_t> cmds = defineWindow0(4, true);
        cmds.push_back(0x18); // P16
        cmds.push_back(0xD8);
        cmds.push_back(0x34);
        cmds.push_back(0x18); // P16
        cmds.push_back(0xDD);
        cmds.push_back(0x1E);
        Cea708WindowState st;
        st.processBytes(cmds.data(), cmds.size());
        // U+1D11E in UTF-8: 0xF0 0x9D 0x84 0x9E.
        CHECK(st.visibleText() == "\xF0\x9D\x84\x9E");
}

TEST_CASE("Cea708WindowState: orphaned high surrogate decays to U+FFFD") {
        // High surrogate followed by a non-P16 byte — the surrogate
        // pair is broken; the decoder commits the orphaned half as
        // U+FFFD before processing the new byte.
        std::vector<uint8_t> cmds = defineWindow0(4, true);
        cmds.push_back(0x18); // P16
        cmds.push_back(0xD8);
        cmds.push_back(0x34); // high surrogate
        cmds.push_back('A');  // G0 — breaks the pairing
        Cea708WindowState st;
        st.processBytes(cmds.data(), cmds.size());
        CHECK(st.visibleText() == "\xEF\xBF\xBD" "A");
}

TEST_CASE("Cea708WindowState: surrogate pair survives a processBytes split") {
        // High surrogate is the last byte of one processBytes call;
        // the low surrogate starts the next.  The pair must still join
        // because Cea708WindowState retains the pending high half on
        // its state.
        std::vector<uint8_t> cmds = defineWindow0(4, true);
        cmds.push_back(0x18); // P16
        cmds.push_back(0xD8);
        cmds.push_back(0x34); // high surrogate
        Cea708WindowState st;
        st.processBytes(cmds.data(), cmds.size());
        std::vector<uint8_t> tail = {0x18, 0xDD, 0x1E}; // P16 + low surrogate
        st.processBytes(tail.data(), tail.size());
        CHECK(st.visibleText() == "\xF0\x9D\x84\x9E");
}

TEST_CASE("Cea708WindowState: empty input is a no-op") {
        Cea708WindowState st;
        st.processBytes(nullptr, 0);
        st.processBytes("", 0);
        CHECK_FALSE(st.anyVisible());
}

// ============================================================================
// processServiceBytes convenience overload
// ============================================================================

TEST_CASE("Cea708WindowState::processServiceBytes routes through Cea708Service::data") {
        Cea708Service svc(1, [&] {
                std::vector<uint8_t> bytes = defineWindow0(4, true);
                bytes.push_back('Z');
                Buffer b(bytes.size());
                b.setSize(bytes.size());
                b.copyFrom(bytes.data(), bytes.size(), 0);
                return b;
        }());
        Cea708WindowState st;
        st.processServiceBytes(svc);
        CHECK(st.visibleText() == "Z");
}

// ============================================================================
// DefineWindow parm1 spec-correct bit layout (CEA-708-E §8.10.5.2)
// ============================================================================

TEST_CASE("Cea708WindowState: DefineWindow parm1 example from CEA-708-E §8.10.5.2 p67") {
        // The spec's worked example (page 66-67) encodes DefineWindow
        // for window 2, visible=YES, row_lock=YES, col_lock=YES,
        // priority=0 as the 7-byte sequence:
        //   0x9A 0x38 0x4A 0xD1 0x8B 0x0F 0x11
        // Parm1 = 0x38 = 00111000 = visible(bit5=1) row_lock(bit4=1)
        //                col_lock(bit3=1) priority=0(bits 2..0).
        std::vector<uint8_t> spec{0x9A, 0x38, 0x4A, 0xD1, 0x8B, 0x0F, 0x11};
        Cea708WindowState    st;
        st.processBytes(spec.data(), spec.size());
        const Cea708Window  &w = st.window(2);
        CHECK(w.defined);
        CHECK(w.visible);
        CHECK(w.rowLock);
        CHECK(w.colLock);
        CHECK(w.priority == 0);
        // Parm2 = 0x4A: relativePos=0(bit7), anchor_v=0x4A=74.
        CHECK_FALSE(w.relativePos);
        CHECK(w.anchorV == 74);
        // Parm3 = 0xD1: anchor_h = 209.
        CHECK(w.anchorH == 209);
        // Parm4 = 0x8B: anchor_point=8 (high nibble), row_count=11
        // (low nibble + 1 = 12 rows).
        CHECK(w.anchorPoint == 8);
        CHECK(w.rowCount == 12);
        // Parm5 = 0x0F: col_count = 15 + 1 = 16 cols.
        CHECK(w.colCount == 16);
        // Parm6 = 0x11: ws=(0x11 >> 3) & 7 = 2, ps=(0x11 & 7) = 1.
        // Style #2 = PopUp w/o Black Background → transparent fill.
        CHECK_FALSE(w.attrs.fillColor.isValid());
        CHECK(w.attrs.fillOpacity == SubtitleOpacity::Transparent);
        // After DF, this is the current window.
        CHECK(st.currentWindowId() == 2);
}

TEST_CASE("Cea708WindowState: DefineWindow parm1 visible bit is 0x20, not 0x40") {
        // Direct spec-correctness check — bit 6 (mask 0x40) is reserved
        // and must NOT be interpreted as visible.  Setting only bit 6
        // should produce a HIDDEN window.
        std::vector<uint8_t> cmds{0x98, 0x40 /* reserved bit only */, 0x00, 0x00, 0x10, 0x03, 0x00};
        cmds.push_back('A');
        Cea708WindowState st;
        st.processBytes(cmds.data(), cmds.size());
        // Window is defined but hidden (bit 6 is reserved, bit 5 is
        // the real visible bit and is clear).
        CHECK(st.window(0).defined);
        CHECK_FALSE(st.window(0).visible);
        CHECK(st.visibleText() == "");
}

TEST_CASE("Cea708WindowState: DefineWindow ws=0 on create preloads Window Style #1") {
        // CEA-708-E §8.10.5.2: "ws... When zero during a window create,
        // the window style is automatically set to window style #1."
        // Style #1 = NTSC PopUp Captions: SOLID black fill.
        std::vector<uint8_t> cmds{
                0x98,
                0x38, // visible + both locks
                0x00, 0x00, 0x10, 0x03,
                0x00 // ws=0, ps=0 → preload Style #1 + Pen Style #1
        };
        Cea708WindowState st;
        st.processBytes(cmds.data(), cmds.size());
        const Cea708Window &w = st.window(0);
        CHECK(w.attrs.fillColor.isValid());
        CHECK(w.attrs.fillColor.r8() == 0);
        CHECK(w.attrs.fillColor.g8() == 0);
        CHECK(w.attrs.fillColor.b8() == 0);
        CHECK(w.attrs.fillOpacity == SubtitleOpacity::Solid);
}

TEST_CASE("Cea708WindowState: DefineWindow ws=2 preloads transparent fill") {
        // Window Style #2 = "PopUp Captions w/o Black Background":
        // transparent fill.  Encoded in parm6 bits 5..3 = 010 → 0x10.
        std::vector<uint8_t> cmds{0x98, 0x38, 0x00, 0x00, 0x10, 0x03,
                                  static_cast<uint8_t>((2 << 3) | 0) /* ws=2 */};
        Cea708WindowState st;
        st.processBytes(cmds.data(), cmds.size());
        const Cea708Window &w = st.window(0);
        CHECK_FALSE(w.attrs.fillColor.isValid());
        CHECK(w.attrs.fillOpacity == SubtitleOpacity::Transparent);
}

// ============================================================================
// C0 reserved-opcode skip lengths (CEA-708-E §7.1.4)
// ============================================================================

TEST_CASE("Cea708WindowState: reserved C0 in 0x11..0x17 skips 2 bytes (1 arg)") {
        // The reserved C0 code 0x11 has a 2-byte sequence per spec
        // §7.1.4: skip the code byte + 1 arg byte.  A naive
        // 1-byte-skip would misalign 'B' as the arg byte of 0x11
        // instead of treating 'B' as a G0 character.
        std::vector<uint8_t> cmds = defineWindow0(4, true);
        cmds.push_back('A');
        cmds.push_back(0x11); // reserved C0 (2-byte sequence)
        cmds.push_back(0x00); // its arg byte
        cmds.push_back('B');
        Cea708WindowState st;
        st.processBytes(cmds.data(), cmds.size());
        CHECK(st.visibleText() == "AB");
}

TEST_CASE("Cea708WindowState: reserved C0 in 0x19..0x1F skips 3 bytes (2 args)") {
        // The reserved C0 code 0x19 has a 3-byte sequence per spec
        // §7.1.4: skip the code byte + 2 arg bytes.
        std::vector<uint8_t> cmds = defineWindow0(4, true);
        cmds.push_back('A');
        cmds.push_back(0x19); // reserved C0 (3-byte sequence)
        cmds.push_back(0x00); // arg 1
        cmds.push_back(0x00); // arg 2
        cmds.push_back('B');
        Cea708WindowState st;
        st.processBytes(cmds.data(), cmds.size());
        CHECK(st.visibleText() == "AB");
}

// ============================================================================
// Safe-title aspect enforcement (CEA-708-E §9.4 / §9.7)
// ============================================================================

TEST_CASE("Cea708WindowState: 16:9 default disregards a window with > 42 cols") {
        // Col count 50 (wire encoding 49 = 0x31) exceeds the 16:9
        // safe-title cap of 42 chars.  Per §9.7 the window is
        // completely disregarded — undefined, hidden, no current
        // window change.
        Cea708WindowState st;
        // DF0 with col_count_wire = 49 (= 50 visible cols).
        std::vector<uint8_t> cmds{0x98, 0x38, 0x00, 0x00, 0x10,
                                  0x31 /* col_count_wire = 49 */, 0x00};
        st.processBytes(cmds.data(), cmds.size());
        CHECK_FALSE(st.window(0).defined);
        CHECK_FALSE(st.anyVisible());
        // Current window stays at the default (0) — DF0 didn't take.
        CHECK(st.currentWindowId() == 0);
}

TEST_CASE("Cea708WindowState: 4:3 mode disregards a window with > 32 cols") {
        // Same as above but in 4:3 mode where the cap is 32.  A 40-col
        // request (wire 0x27) is fine for 16:9 but should be
        // disregarded under 4:3.
        Cea708WindowState st;
        st.setDisplayAspect(Cea708WindowState::DisplayAspect::Standard);
        std::vector<uint8_t> cmds{0x98, 0x38, 0x00, 0x00, 0x10,
                                  0x27 /* col_count_wire = 39 = 40 cols */, 0x00};
        st.processBytes(cmds.data(), cmds.size());
        CHECK_FALSE(st.window(0).defined);
}

TEST_CASE("Cea708WindowState: 16:9 mode accepts a 42-col window") {
        // Col count exactly 42 (wire 0x29 = 41) lands at the spec cap
        // and should be accepted.
        Cea708WindowState st;
        std::vector<uint8_t> cmds{0x98, 0x38, 0x00, 0x00, 0x10,
                                  0x29 /* col_count_wire = 41 = 42 cols */, 0x00};
        st.processBytes(cmds.data(), cmds.size());
        CHECK(st.window(0).defined);
        CHECK(st.window(0).colCount == 42);
}

TEST_CASE("Cea708WindowState: absolute anchor_h beyond 4:3 bound disregards window") {
        // 4:3 mode caps absolute anchor_h at 159.  Try 200 (rel_pos=0,
        // anchor_h=200=0xC8).  The window should be disregarded.
        Cea708WindowState st;
        st.setDisplayAspect(Cea708WindowState::DisplayAspect::Standard);
        std::vector<uint8_t> cmds{0x98, 0x38,
                                  0x00 /* parm2: rel_pos=0, anchor_v=0 */,
                                  0xC8 /* parm3: anchor_h=200 */,
                                  0x10, 0x1F /* col_count_wire = 31 = 32 cols (OK for 4:3) */,
                                  0x00};
        st.processBytes(cmds.data(), cmds.size());
        CHECK_FALSE(st.window(0).defined);
}

TEST_CASE("Cea708WindowState: relative positioning bypasses absolute anchor bounds") {
        // Relative positioning (bit 7 of parm2 set) gives anchor as
        // percentages 0..99, which we don't safe-title-bound — only
        // absolute-coord values are checked.  A relative anchor_h of
        // 99 should be fine even in 4:3 mode.
        Cea708WindowState st;
        st.setDisplayAspect(Cea708WindowState::DisplayAspect::Standard);
        std::vector<uint8_t> cmds{0x98, 0x38,
                                  0x80 /* parm2: rel_pos=1, anchor_v=0 */,
                                  0x63 /* parm3: anchor_h=99 (relative %) */,
                                  0x10, 0x1F /* 32 cols */, 0x00};
        st.processBytes(cmds.data(), cmds.size());
        CHECK(st.window(0).defined);
}

// ============================================================================
// Per-window pen state (CEA-708-E §8.5.10)
// ============================================================================

TEST_CASE("Cea708WindowState: each window carries its own pen") {
        // Define window 0 with a red foreground, then define window 1
        // and assert white foreground.  Switch back to window 0 via
        // CW0 — the previously-asserted red pen must still apply (each
        // window has its own pen per spec §8.5.10).
        Cea708WindowState st;
        // Define window 0, then SPC red foreground (R=3, G=0, B=0).
        std::vector<uint8_t> def0 = {0x98, 0x38, 0x00, 0x00, 0x10, 0x03, 0x00};
        // SPC: byte1 = Solid(00) | R=3 | G=0 | B=0 = 0x30, others zero.
        std::vector<uint8_t> spcRed = {0x91, 0x30, 0xC0, 0x00};
        st.processBytes(def0.data(), def0.size());
        st.processBytes(spcRed.data(), spcRed.size());
        const Color win0Pen = st.window(0).pen.foregroundColor;
        REQUIRE(win0Pen.isValid());
        CHECK(win0Pen.r8() == 255);
        // Define window 1 (DF1 = 0x99), with default Pen Style #1
        // preload (which gives white-on-black).  This must NOT change
        // window 0's pen.
        std::vector<uint8_t> def1 = {0x99, 0x38, 0x00, 0x00, 0x10, 0x03, 0x00};
        st.processBytes(def1.data(), def1.size());
        // After DF1 the current window is 1; the global @ref currentPen
        // returns window 1's pen, while @ref window(0).pen retains
        // window 0's red.
        CHECK(st.currentWindowId() == 1);
        CHECK(st.window(0).pen.foregroundColor.r8() == 255);   // red preserved
        // Pen Style #1 fg = white-solid per CEA-708-E §8.4.12 Table 27.
        // In the 0..3 wire encoding "white" is code 3 → 255/255 each
        // channel via the 0..3 → {0, 85, 170, 255} mapping shared by
        // SPC.
        CHECK(st.window(1).pen.foregroundColor.r8() == 255);
        CHECK(st.window(1).pen.foregroundColor.g8() == 255);
        CHECK(st.window(1).pen.foregroundColor.b8() == 255);
        // Switch back to window 0 via CW0.  Writing a character there
        // should stamp the red foreground, not window 1's white.
        std::vector<uint8_t> cw0AndChar = {0x80 /* CW0 */, 'X'};
        st.processBytes(cw0AndChar.data(), cw0AndChar.size());
        const auto spans = st.window(0).visibleSpans();
        REQUIRE(spans.size() == 1);
        CHECK(spans[0].text() == "X");
        CHECK(spans[0].color().r8() == 255);
        CHECK(spans[0].color().g8() == 0);
        CHECK(spans[0].color().b8() == 0);
}

TEST_CASE("Cea708WindowState: reserved C0 in 0x00..0x0F skips 1 byte") {
        // The reserved C0 codes in 0x00..0x0F (excluding the named
        // codes NUL=0x00, ETX=0x03, BS=0x08, FF=0x0C, CR=0x0D, HCR=0x0E)
        // are 1-byte sequences per spec §7.1.4.
        std::vector<uint8_t> cmds = defineWindow0(4, true);
        cmds.push_back('A');
        cmds.push_back(0x01); // reserved C0 (1-byte sequence)
        cmds.push_back(0x02); // another reserved C0 (1-byte)
        cmds.push_back('B');
        Cea708WindowState st;
        st.processBytes(cmds.data(), cmds.size());
        CHECK(st.visibleText() == "AB");
}
