/**
 * @file      cea608ext.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdint>
#include <doctest/doctest.h>
#include <promeki/cea608ext.h>

using namespace promeki;

// ============================================================================
// Basic G0 — ASCII passthrough + ten remapped Latin / arithmetic positions
// ============================================================================

TEST_CASE("Cea608Ext: basic G0 plain-ASCII positions decode as themselves") {
        // Plain ASCII positions (everything in 0x20..0x7F that isn't
        // one of the ten remapped slots).
        for (uint8_t b : {0x20, 0x21, 0x22, 0x29, 0x40, 0x41, 0x5A, 0x5B, 0x5D, 0x61}) {
                CAPTURE(b);
                CHECK(Cea608Ext::decodeG0(b) == static_cast<uint32_t>(b));
                CHECK(Cea608Ext::encodeG0(static_cast<uint32_t>(b)) == b);
        }
}

TEST_CASE("Cea608Ext: basic G0 ten remapped positions round-trip codepoint <-> wire byte") {
        struct Pair {
                uint8_t  byte;
                uint32_t cp;
        };
        const Pair table[] = {
                {0x2A, 0x00E1}, // á
                {0x5C, 0x00E9}, // é
                {0x5E, 0x00ED}, // í
                {0x5F, 0x00F3}, // ó
                {0x60, 0x00FA}, // ú
                {0x7B, 0x00E7}, // ç
                {0x7C, 0x00F7}, // ÷
                {0x7D, 0x00D1}, // Ñ
                {0x7E, 0x00F1}, // ñ
                {0x7F, 0x2588}, // █
        };
        for (const Pair &p : table) {
                CAPTURE(p.byte);
                CHECK(Cea608Ext::decodeG0(p.byte) == p.cp);
                CHECK(Cea608Ext::encodeG0(p.cp) == p.byte);
        }
}

// ============================================================================
// Special Characters (16 glyphs at (0x11, 0x30..0x3F))
// ============================================================================

TEST_CASE("Cea608Ext: Special Characters round-trip codepoint <-> table index") {
        struct Pair {
                uint8_t  idx;
                uint32_t cp;
        };
        const Pair table[] = {
                {0x30, 0x00AE}, // ®
                {0x31, 0x00B0}, // °
                {0x32, 0x00BD}, // ½
                {0x33, 0x00BF}, // ¿
                {0x34, 0x2122}, // ™
                {0x35, 0x00A2}, // ¢
                {0x36, 0x00A3}, // £
                {0x37, 0x266A}, // ♪
                {0x38, 0x00E0}, // à
                {0x39, 0x00A0}, // NBSP
                {0x3A, 0x00E8}, // è
                {0x3B, 0x00E2}, // â
                {0x3C, 0x00EA}, // ê
                {0x3D, 0x00EE}, // î
                {0x3E, 0x00F4}, // ô
                {0x3F, 0x00FB}, // û
        };
        for (const Pair &p : table) {
                CAPTURE(p.idx);
                CHECK(Cea608Ext::decodeSpecial(p.idx) == p.cp);
                CHECK(Cea608Ext::encodeSpecial(p.cp) == p.idx);
        }
}

TEST_CASE("Cea608Ext: Special-Character placeholder is sane ASCII for old-decoder fallback") {
        // Sanity: each placeholder is a printable ASCII character —
        // it has to render as something on a decoder that doesn't
        // recognise the doubled control code that follows.
        for (uint8_t i = 0x30; i <= 0x3F; ++i) {
                const uint8_t ph = Cea608Ext::specialPlaceholder(i);
                CAPTURE(i);
                CHECK(ph >= 0x20);
                CHECK(ph <= 0x7E);
        }
}

// ============================================================================
// Extended Spanish (0x12, 0x20..0x3F) — 32 glyphs
// ============================================================================

TEST_CASE("Cea608Ext: extended Spanish key glyphs round-trip") {
        struct Pair {
                uint8_t  idx;
                uint32_t cp;
        };
        const Pair table[] = {
                {0x20, 0x00C1}, // Á
                {0x21, 0x00C9}, // É
                {0x22, 0x00D3}, // Ó
                {0x27, 0x00A1}, // ¡
                {0x2B, 0x00A9}, // ©
                {0x32, 0x00C7}, // Ç
                {0x3E, 0x00AB}, // «
                {0x3F, 0x00BB}, // »
        };
        for (const Pair &p : table) {
                CAPTURE(p.idx);
                CHECK(Cea608Ext::decodeExtSpanish(p.idx) == p.cp);
                CHECK(Cea608Ext::encodeExtSpanish(p.cp) == p.idx);
        }
}

// ============================================================================
// Extended Portuguese / German (0x13, 0x20..0x3F) — 32 glyphs
// ============================================================================

TEST_CASE("Cea608Ext: extended French / German key glyphs round-trip") {
        struct Pair {
                uint8_t  idx;
                uint32_t cp;
        };
        const Pair table[] = {
                {0x20, 0x00C3}, // Ã
                {0x22, 0x00CD}, // Í
                {0x30, 0x00C4}, // Ä
                {0x32, 0x00D6}, // Ö
                {0x34, 0x00DF}, // ß
                {0x38, 0x00C5}, // Å
                {0x3A, 0x00D8}, // Ø
        };
        for (const Pair &p : table) {
                CAPTURE(p.idx);
                CHECK(Cea608Ext::decodeExtPortugueseGerman(p.idx) == p.cp);
                CHECK(Cea608Ext::encodeExtPortugueseGerman(p.cp) == p.idx);
        }
}

// ============================================================================
// Composite encode() — picks the cheapest path
// ============================================================================

TEST_CASE("Cea608Ext::encode: ASCII -> BasicG0 byte") {
        const auto e = Cea608Ext::encode('A');
        CHECK(e.kind == Cea608Ext::Kind::BasicG0);
        CHECK(e.byte == 'A');
}

TEST_CASE("Cea608Ext::encode: remapped G0 codepoint -> BasicG0 byte (single wire byte)") {
        // U+00E9 é — the cheapest encoding is the basic G0 byte 0x5C
        // (one wire byte), not the extended Spanish entry which
        // would cost a doubled control pair.
        const auto e = Cea608Ext::encode(0x00E9);
        CHECK(e.kind == Cea608Ext::Kind::BasicG0);
        CHECK(e.byte == 0x5C);
}

TEST_CASE("Cea608Ext::encode: Special Character -> placeholder + (0x11, 0x3X) code") {
        // U+2122 ™ — Special Character index 0x34, placeholder '('.
        const auto e = Cea608Ext::encode(0x2122);
        CHECK(e.kind == Cea608Ext::Kind::Special);
        CHECK(e.placeholder == '(');
        CHECK(e.code == 0x34);
}

TEST_CASE("Cea608Ext::encode: extended Spanish codepoint -> placeholder + (0x12, 0x2X) code") {
        // U+00C1 Á — extended Spanish index 0x20, placeholder 'A'.
        const auto e = Cea608Ext::encode(0x00C1);
        CHECK(e.kind == Cea608Ext::Kind::ExtSpanish);
        CHECK(e.placeholder == 'A');
        CHECK(e.code == 0x20);
}

TEST_CASE("Cea608Ext::encode: extended French codepoint -> placeholder + (0x13, 0x3X) code") {
        // U+00DF ß — extended French index 0x34, placeholder 'B'.
        const auto e = Cea608Ext::encode(0x00DF);
        CHECK(e.kind == Cea608Ext::Kind::ExtPortugueseGerman);
        CHECK(e.placeholder == 'B');
        CHECK(e.code == 0x34);
}

TEST_CASE("Cea608Ext::encode: codepoint with no 608 representation -> Kind::None") {
        // U+1F600 (😀) is an astral codepoint — no 608 path.
        const auto e = Cea608Ext::encode(0x1F600);
        CHECK(e.kind == Cea608Ext::Kind::None);
}
