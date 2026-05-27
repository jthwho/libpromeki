/**
 * @file      cea708ext.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/cea708ext.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

        /// @brief CEA-708-D Annex G G2 table.
        ///
        /// Rows are indexed by the (G2 byte - 0x20), so index 0
        /// corresponds to wire byte 0x20.  Undefined positions hold
        /// @ref Cea708Ext::NoCodepoint (0).
        constexpr uint32_t kG2Table[96] = {
                /* 0x20 */ 0x0020, // Transparent space — same glyph as ASCII space
                /* 0x21 */ 0x00A0, // Non-breaking transparent space
                /* 0x22 */ 0,
                /* 0x23 */ 0,
                /* 0x24 */ 0,
                /* 0x25 */ 0x2026, // Horizontal ellipsis
                /* 0x26 */ 0,
                /* 0x27 */ 0,
                /* 0x28 */ 0,
                /* 0x29 */ 0,
                /* 0x2A */ 0x0160, // Latin capital S with caron
                /* 0x2B */ 0,
                /* 0x2C */ 0x0152, // Latin capital ligature OE
                /* 0x2D */ 0,
                /* 0x2E */ 0,
                /* 0x2F */ 0,
                /* 0x30 */ 0x25A0, // Solid block (BLACK SQUARE)
                /* 0x31 */ 0x2018, // Open single quote
                /* 0x32 */ 0x2019, // Close single quote
                /* 0x33 */ 0x201C, // Open double quote
                /* 0x34 */ 0x201D, // Close double quote
                /* 0x35 */ 0x2022, // Bullet
                /* 0x36 */ 0,
                /* 0x37 */ 0,
                /* 0x38 */ 0,
                /* 0x39 */ 0x2122, // Trademark
                /* 0x3A */ 0x0161, // Latin small s with caron
                /* 0x3B */ 0,
                /* 0x3C */ 0x0153, // Latin small ligature oe
                /* 0x3D */ 0x2120, // Service mark (SM)
                /* 0x3E */ 0,
                /* 0x3F */ 0x0178, // Latin capital Y with diaeresis
                /* 0x40..0x75 — undefined */
                /* 0x40 */ 0, /* 0x41 */ 0, /* 0x42 */ 0, /* 0x43 */ 0,
                /* 0x44 */ 0, /* 0x45 */ 0, /* 0x46 */ 0, /* 0x47 */ 0,
                /* 0x48 */ 0, /* 0x49 */ 0, /* 0x4A */ 0, /* 0x4B */ 0,
                /* 0x4C */ 0, /* 0x4D */ 0, /* 0x4E */ 0, /* 0x4F */ 0,
                /* 0x50 */ 0, /* 0x51 */ 0, /* 0x52 */ 0, /* 0x53 */ 0,
                /* 0x54 */ 0, /* 0x55 */ 0, /* 0x56 */ 0, /* 0x57 */ 0,
                /* 0x58 */ 0, /* 0x59 */ 0, /* 0x5A */ 0, /* 0x5B */ 0,
                /* 0x5C */ 0, /* 0x5D */ 0, /* 0x5E */ 0, /* 0x5F */ 0,
                /* 0x60 */ 0, /* 0x61 */ 0, /* 0x62 */ 0, /* 0x63 */ 0,
                /* 0x64 */ 0, /* 0x65 */ 0, /* 0x66 */ 0, /* 0x67 */ 0,
                /* 0x68 */ 0, /* 0x69 */ 0, /* 0x6A */ 0, /* 0x6B */ 0,
                /* 0x6C */ 0, /* 0x6D */ 0, /* 0x6E */ 0, /* 0x6F */ 0,
                /* 0x70 */ 0, /* 0x71 */ 0, /* 0x72 */ 0, /* 0x73 */ 0,
                /* 0x74 */ 0, /* 0x75 */ 0,
                /* 0x76 */ 0x215B, // Vulgar fraction one eighth
                /* 0x77 */ 0x215C, // Vulgar fraction three eighths
                /* 0x78 */ 0x215D, // Vulgar fraction five eighths
                /* 0x79 */ 0x215E, // Vulgar fraction seven eighths
                /* 0x7A */ 0x2502, // Box drawings light vertical
                /* 0x7B */ 0x2514, // Box drawings light up and right
                /* 0x7C */ 0x250C, // Box drawings light down and right
                /* 0x7D */ 0x2500, // Box drawings light horizontal
                /* 0x7E */ 0x2510, // Box drawings light down and left
                /* 0x7F */ 0x2518, // Box drawings light up and left
        };

        /// @brief Returns the high surrogate for an astral codepoint.
        constexpr uint16_t highSurrogate(uint32_t cp) {
                return static_cast<uint16_t>(0xD800 + ((cp - 0x10000) >> 10));
        }

        /// @brief Returns the low surrogate for an astral codepoint.
        constexpr uint16_t lowSurrogate(uint32_t cp) {
                return static_cast<uint16_t>(0xDC00 + ((cp - 0x10000) & 0x3FF));
        }

        /// @brief Appends a single P16 sequence (0x18 hi lo) for the
        ///        16-bit value @p v to @p out at offset @p off.
        ///        Returns the new offset (off + 3).
        constexpr uint8_t emitP16(uint8_t *out, uint8_t off, uint16_t v) {
                out[off + 0] = Cea708Ext::P16;
                out[off + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
                out[off + 2] = static_cast<uint8_t>(v & 0xFF);
                return static_cast<uint8_t>(off + 3);
        }

} // namespace

uint8_t Cea708Ext::encodeG2(uint32_t cp) {
        // Special case: U+0020 (ASCII space) is at G2 0x20 but the
        // caller almost always wants the bare G0 0x20 byte instead.
        // Returning NoMapping here keeps Cea708Ext::encode() picking
        // the cheaper G0 path; explicit callers of encodeG2 still get
        // NoMapping here and can fall back themselves.
        if (cp == 0x0020 || cp == 0) return NoMapping;
        for (uint8_t i = 0; i < 96; ++i) {
                if (kG2Table[i] == cp) return static_cast<uint8_t>(0x20 + i);
        }
        return NoMapping;
}

uint8_t Cea708Ext::encodeG3(uint32_t cp) {
        if (cp == G3CcLogo) return 0xA0;
        return NoMapping;
}

uint32_t Cea708Ext::decodeG2(uint8_t b) {
        if (b < 0x20 || b > 0x7F) return NoCodepoint;
        return kG2Table[b - 0x20];
}

uint32_t Cea708Ext::decodeG3(uint8_t b) {
        if (b == 0xA0) return G3CcLogo;
        return NoCodepoint;
}

Cea708Ext::EncodedChar Cea708Ext::encode(uint32_t cp) {
        EncodedChar out;

        // CEA-708 §7.1.4 redefines G0 byte 0x7F as the EIGHTH NOTE
        // glyph (U+266A) — mirror the decoder's reverse mapping so a
        // music note round-trips losslessly.
        if (cp == 0x266A) {
                out.bytes[0] = 0x7F;
                out.length = 1;
                return out;
        }
        // 1. G0
        if (cp >= 0x20 && cp <= 0x7E) {
                out.bytes[0] = static_cast<uint8_t>(cp);
                out.length = 1;
                return out;
        }
        // 2. G1
        if (cp >= 0xA0 && cp <= 0xFF) {
                out.bytes[0] = static_cast<uint8_t>(cp);
                out.length = 1;
                return out;
        }
        // 3. G2
        const uint8_t g2 = encodeG2(cp);
        if (g2 != NoMapping) {
                out.bytes[0] = Ext1;
                out.bytes[1] = g2;
                out.length = 2;
                return out;
        }
        // 4. G3
        const uint8_t g3 = encodeG3(cp);
        if (g3 != NoMapping) {
                out.bytes[0] = Ext1;
                out.bytes[1] = g3;
                out.length = 2;
                return out;
        }
        // 5. BMP via P16
        if (cp <= 0xFFFF) {
                // Surrogate codepoints (U+D800..U+DFFF) are not valid
                // standalone Unicode scalars; substitute U+FFFD so the
                // wire never carries an unpaired surrogate that would
                // confuse a UTF-16-aware decoder.
                if (cp >= 0xD800 && cp <= 0xDFFF) {
                        emitP16(out.bytes, 0, 0xFFFD);
                        out.length = 3;
                        return out;
                }
                emitP16(out.bytes, 0, static_cast<uint16_t>(cp));
                out.length = 3;
                return out;
        }
        // 6. Astral plane via UTF-16 surrogate pair
        if (cp <= 0x10FFFF) {
                emitP16(out.bytes, 0, highSurrogate(cp));
                emitP16(out.bytes, 3, lowSurrogate(cp));
                out.length = 6;
                return out;
        }
        // 7. Outside Unicode — substitute U+FFFD
        emitP16(out.bytes, 0, 0xFFFD);
        out.length = 3;
        return out;
}

PROMEKI_NAMESPACE_END
