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
                // DF0 = 0x98, six argument bytes follow:
                //   b1: priority(0..7) | col_lock(0x10) | row_lock(0x20) | visible(0x40)
                //   b2: relative_pos(0x80) | anchor_v(7 bits)
                //   b3: anchor_h
                //   b4: anchor_point(high nibble) | row_count(low nibble)
                //   b5: col_count(6 bits)
                //   b6: window_style(high nibble) | pen_style(low nibble)
                //
                // row_count and col_count on the wire encode count-1.
                const uint8_t rowCountWire = 0; // 1 row
                const uint8_t colCountWire = static_cast<uint8_t>((cols > 0 ? cols - 1 : 0) & 0x3F);
                return {0x98,
                        static_cast<uint8_t>((visible ? 0x40 : 0x00) | 0x30 /* locks set */),
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

TEST_CASE("Cea708Window::putChar wraps to next row at column boundary") {
        Cea708Window w;
        w.resize(3, 2);
        w.putChar('A');
        w.putChar('B');
        w.putChar('C'); // wraps to row 1.
        w.putChar('D');
        // Row 0: "AB", row 1: "CD".
        CHECK(w.text() == "AB\nCD");
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
        cmds = {0x98, 0x40 | 0x30, 0x00, 0x00, 0x11 /* anchor=1, rows=2 */, 0x02 /* 3 cols */, 0x00};
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

TEST_CASE("Cea708WindowState: 0x08 (BS) erases the previous char") {
        Cea708WindowState    st;
        std::vector<uint8_t> cmds = defineWindow0(4, true);
        cmds.push_back('A');
        cmds.push_back('B');
        cmds.push_back(0x08); // BS
        cmds.push_back('C');
        st.processBytes(cmds.data(), cmds.size());
        CHECK(st.visibleText() == "AC");
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
        std::vector<uint8_t> cmds{0x98,
                                  0x40 | 0x30,
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

TEST_CASE("Cea708WindowState: EXT1 + reserved G2 byte falls back to U+FFFD") {
        // 0x22 is one of the reserved positions in the G2 table — no
        // defined glyph, so the decoder substitutes the replacement
        // character.
        std::vector<uint8_t> cmds = defineWindow0(4, true);
        cmds.push_back(0x10); // EXT1
        cmds.push_back(0x22); // reserved G2 position
        Cea708WindowState st;
        st.processBytes(cmds.data(), cmds.size());
        CHECK(st.visibleText() == "\xEF\xBF\xBD");
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
