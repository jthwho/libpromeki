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
#include <promeki/string.h>

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

TEST_CASE("Cea708WindowState: EXT1 + G2 byte substitutes with U+FFFD replacement") {
        std::vector<uint8_t> cmds = defineWindow0(4, true);
        cmds.push_back(0x10); // EXT1
        cmds.push_back(0x32); // G2 byte (would map to a special char in full spec)
        Cea708WindowState st;
        st.processBytes(cmds.data(), cmds.size());
        // U+FFFD = 0xEF 0xBF 0xBD in UTF-8.
        CHECK(st.visibleText() == "\xEF\xBF\xBD");
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
