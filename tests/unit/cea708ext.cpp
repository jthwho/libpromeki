/**
 * @file      cea708ext.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdint>
#include <doctest/doctest.h>
#include <promeki/cea708ext.h>

using namespace promeki;

// ============================================================================
// G2 table — encode / decode round-trip
// ============================================================================

TEST_CASE("Cea708Ext: G2 typographic glyphs round-trip codepoint <-> wire byte") {
        struct Pair {
                uint32_t cp;
                uint8_t  wire;
        };
        const Pair table[] = {
                {0x00A0, 0x21}, // Non-breaking space
                {0x2026, 0x25}, // Horizontal ellipsis
                {0x0160, 0x2A}, // Latin capital S with caron
                {0x0152, 0x2C}, // Latin capital ligature OE
                {0x25A0, 0x30}, // Solid block
                {0x2018, 0x31}, // Open single quote
                {0x2019, 0x32}, // Close single quote
                {0x201C, 0x33}, // Open double quote
                {0x201D, 0x34}, // Close double quote
                {0x2022, 0x35}, // Bullet
                {0x2122, 0x39}, // Trademark
                {0x0161, 0x3A}, // Latin small s with caron
                {0x0153, 0x3C}, // Latin small ligature oe
                {0x2120, 0x3D}, // Service mark
                {0x0178, 0x3F}, // Latin capital Y with diaeresis
                {0x215B, 0x76}, // 1/8
                {0x215C, 0x77}, // 3/8
                {0x215D, 0x78}, // 5/8
                {0x215E, 0x79}, // 7/8
                {0x2502, 0x7A}, // Box vertical
                {0x2514, 0x7B}, // Box up+right
                {0x250C, 0x7C}, // Box down+right
                {0x2500, 0x7D}, // Box horizontal
                {0x2510, 0x7E}, // Box down+left
                {0x2518, 0x7F}, // Box up+left
        };
        for (const Pair &p : table) {
                CAPTURE(p.cp);
                CHECK(Cea708Ext::encodeG2(p.cp) == p.wire);
                CHECK(Cea708Ext::decodeG2(p.wire) == p.cp);
        }
}

TEST_CASE("Cea708Ext: G2 reserved positions return NoCodepoint on decode") {
        // 0x22, 0x26..0x29 are reserved in the G2 table.
        CHECK(Cea708Ext::decodeG2(0x22) == Cea708Ext::NoCodepoint);
        CHECK(Cea708Ext::decodeG2(0x26) == Cea708Ext::NoCodepoint);
        CHECK(Cea708Ext::decodeG2(0x40) == Cea708Ext::NoCodepoint);
}

TEST_CASE("Cea708Ext: encodeG2 returns NoMapping for codepoints outside the table") {
        CHECK(Cea708Ext::encodeG2(0x0041 /* 'A' */) == Cea708Ext::NoMapping);
        CHECK(Cea708Ext::encodeG2(0x0020 /* space — explicitly defers to G0 */)
              == Cea708Ext::NoMapping);
        CHECK(Cea708Ext::encodeG2(0xFFFF) == Cea708Ext::NoMapping);
        CHECK(Cea708Ext::encodeG2(0) == Cea708Ext::NoMapping);
}

// ============================================================================
// G3 table
// ============================================================================

TEST_CASE("Cea708Ext: G3 0xA0 round-trips as the ATSC CC logo (U+E000)") {
        CHECK(Cea708Ext::encodeG3(Cea708Ext::G3CcLogo) == 0xA0);
        CHECK(Cea708Ext::decodeG3(0xA0) == Cea708Ext::G3CcLogo);
}

TEST_CASE("Cea708Ext: G3 reserved positions return NoCodepoint") {
        CHECK(Cea708Ext::decodeG3(0xA1) == Cea708Ext::NoCodepoint);
        CHECK(Cea708Ext::decodeG3(0xFF) == Cea708Ext::NoCodepoint);
}

// ============================================================================
// encode() — composite: G0 / G1 / G2 / G3 / P16 / surrogate pair
// ============================================================================

TEST_CASE("Cea708Ext::encode: G0 codepoints emit one byte") {
        const auto e = Cea708Ext::encode('A');
        CHECK(e.length == 1);
        CHECK(e.bytes[0] == 'A');
}

TEST_CASE("Cea708Ext::encode: G1 (Latin-1 supplement) emits one byte") {
        // U+00E9 = é
        const auto e = Cea708Ext::encode(0x00E9);
        CHECK(e.length == 1);
        CHECK(e.bytes[0] == 0xE9);
}

TEST_CASE("Cea708Ext::encode: G2 codepoint emits EXT1 + G2 byte") {
        // U+2122 = ™ → 0x10 0x39
        const auto e = Cea708Ext::encode(0x2122);
        CHECK(e.length == 2);
        CHECK(e.bytes[0] == 0x10);
        CHECK(e.bytes[1] == 0x39);
}

TEST_CASE("Cea708Ext::encode: ATSC CC logo emits EXT1 + 0xA0") {
        const auto e = Cea708Ext::encode(Cea708Ext::G3CcLogo);
        CHECK(e.length == 2);
        CHECK(e.bytes[0] == 0x10);
        CHECK(e.bytes[1] == 0xA0);
}

TEST_CASE("Cea708Ext::encode: BMP codepoint outside G0/G1/G2/G3 emits a P16 sequence") {
        // U+D55C 한 (Korean syllable HAN — common BMP non-Latin
        // case; not in any G2/G3 slot).
        const auto e = Cea708Ext::encode(0xD55C);
        CHECK(e.length == 3);
        CHECK(e.bytes[0] == 0x18);
        CHECK(e.bytes[1] == 0xD5);
        CHECK(e.bytes[2] == 0x5C);
}

TEST_CASE("Cea708Ext::encode: astral codepoint emits a UTF-16 surrogate pair via two P16 sequences") {
        // U+1D11E (MUSICAL SYMBOL G CLEF) → high D834 + low DD1E.
        const auto e = Cea708Ext::encode(0x1D11E);
        REQUIRE(e.length == 6);
        CHECK(e.bytes[0] == 0x18);
        CHECK(e.bytes[1] == 0xD8);
        CHECK(e.bytes[2] == 0x34);
        CHECK(e.bytes[3] == 0x18);
        CHECK(e.bytes[4] == 0xDD);
        CHECK(e.bytes[5] == 0x1E);
}

TEST_CASE("Cea708Ext::encode: lone surrogate codepoint substitutes U+FFFD") {
        // U+D800 alone is not a valid Unicode scalar value — emitting
        // it on the wire would produce an unpaired surrogate that
        // confuses UTF-16-aware decoders.  Substitute U+FFFD instead.
        const auto e = Cea708Ext::encode(0xD800);
        CHECK(e.length == 3);
        CHECK(e.bytes[0] == 0x18);
        CHECK(e.bytes[1] == 0xFF);
        CHECK(e.bytes[2] == 0xFD);
}

TEST_CASE("Cea708Ext::encode: codepoint above U+10FFFF substitutes U+FFFD") {
        const auto e = Cea708Ext::encode(0x110000);
        CHECK(e.length == 3);
        CHECK(e.bytes[0] == 0x18);
        CHECK(e.bytes[1] == 0xFF);
        CHECK(e.bytes[2] == 0xFD);
}

TEST_CASE("Cea708Ext::encode: U+266A (music note) maps to G0 byte 0x7F per CEA-708 §7.1.4") {
        // The receiver remaps wire byte 0x7F to U+266A; the encoder
        // mirrors that by sending U+266A as a single G0 byte 0x7F so
        // the round-trip is lossless.
        const auto e = Cea708Ext::encode(0x266A);
        CHECK(e.length == 1);
        CHECK(e.bytes[0] == 0x7F);
}
