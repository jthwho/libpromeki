/**
 * @file      cea608ext.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/cea608ext.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

        // ====================================================================
        // Basic G0 — ten remapped positions
        // ====================================================================
        //
        // CEA-608 §A.5: the basic G0 set repurposes ten ASCII code
        // positions for Spanish / French / arithmetic glyphs.  All
        // other positions in @c 0x20..0x7F decode as plain ASCII.

        struct G0Remap {
                uint8_t  byte;
                uint32_t cp;
        };
        constexpr G0Remap kG0Remap[10] = {
                {0x2A, 0x00E1}, // á
                {0x5C, 0x00E9}, // é
                {0x5E, 0x00ED}, // í
                {0x5F, 0x00F3}, // ó
                {0x60, 0x00FA}, // ú
                {0x7B, 0x00E7}, // ç
                {0x7C, 0x00F7}, // ÷
                {0x7D, 0x00D1}, // Ñ
                {0x7E, 0x00F1}, // ñ
                {0x7F, 0x2588}, // █ FULL BLOCK
        };

        // ====================================================================
        // Special Characters (16 glyphs) — control byte (0x11, 0x30..0x3F)
        // ====================================================================
        //
        // EIA-608-B §A.5.  Indexed by (idx - 0x30); placeholder is the
        // best-fit ASCII glyph the encoder sends ahead of the control
        // code so old decoders show a recognisable fallback.

        struct SpecialEntry {
                uint32_t cp;
                uint8_t  placeholder;
        };
        constexpr SpecialEntry kSpecial[16] = {
                /* 0x30 */ {0x00AE, '('}, // ®
                /* 0x31 */ {0x00B0, '*'}, // °
                /* 0x32 */ {0x00BD, '/'}, // ½
                /* 0x33 */ {0x00BF, '?'}, // ¿
                /* 0x34 */ {0x2122, '('}, // ™
                /* 0x35 */ {0x00A2, 'c'}, // ¢
                /* 0x36 */ {0x00A3, 'L'}, // £
                /* 0x37 */ {0x266A, '!'}, // ♪
                /* 0x38 */ {0x00E0, 'a'}, // à
                /* 0x39 */ {0x00A0, ' '}, // non-breaking space
                /* 0x3A */ {0x00E8, 'e'}, // è
                /* 0x3B */ {0x00E2, 'a'}, // â
                /* 0x3C */ {0x00EA, 'e'}, // ê
                /* 0x3D */ {0x00EE, 'i'}, // î
                /* 0x3E */ {0x00F4, 'o'}, // ô
                /* 0x3F */ {0x00FB, 'u'}, // û
        };

        // ====================================================================
        // Extended Spanish / Misc — control byte (0x12, 0x20..0x3F)
        // ====================================================================
        //
        // EIA-608-B §A.6 first pair.  32 positions.  Empty slots
        // hold @c {0, 0}.

        constexpr SpecialEntry kExtSpanish[32] = {
                /* 0x20 */ {0x00C1, 'A'}, // Á
                /* 0x21 */ {0x00C9, 'E'}, // É
                /* 0x22 */ {0x00D3, 'O'}, // Ó
                /* 0x23 */ {0x00DA, 'U'}, // Ú
                /* 0x24 */ {0x00DC, 'U'}, // Ü
                /* 0x25 */ {0x00FC, 'u'}, // ü
                /* 0x26 */ {0x2018, '\''}, // ' (left single quote — Annex K)
                /* 0x27 */ {0x00A1, '!'}, // ¡
                /* 0x28 */ {0x002A, '*'}, // *
                /* 0x29 */ {0x0027, '\''}, // '
                /* 0x2A */ {0x2014, '-'}, // —
                /* 0x2B */ {0x00A9, 'C'}, // ©
                /* 0x2C */ {0x2120, 'S'}, // ℠
                /* 0x2D */ {0x2022, '.'}, // •
                /* 0x2E */ {0x201C, '"'}, // "
                /* 0x2F */ {0x201D, '"'}, // "
                /* 0x30 */ {0x00C0, 'A'}, // À
                /* 0x31 */ {0x00C2, 'A'}, // Â
                /* 0x32 */ {0x00C7, 'C'}, // Ç
                /* 0x33 */ {0x00C8, 'E'}, // È
                /* 0x34 */ {0x00CA, 'E'}, // Ê
                /* 0x35 */ {0x00CB, 'E'}, // Ë
                /* 0x36 */ {0x00EB, 'e'}, // ë
                /* 0x37 */ {0x00CE, 'I'}, // Î
                /* 0x38 */ {0x00CF, 'I'}, // Ï
                /* 0x39 */ {0x00EF, 'i'}, // ï
                /* 0x3A */ {0x00D4, 'O'}, // Ô
                /* 0x3B */ {0x00D9, 'U'}, // Ù
                /* 0x3C */ {0x00F9, 'u'}, // ù
                /* 0x3D */ {0x00DB, 'U'}, // Û
                /* 0x3E */ {0x00AB, '<'}, // «
                /* 0x3F */ {0x00BB, '>'}, // »
        };

        // ====================================================================
        // Extended Portuguese / German — control byte (0x13, 0x20..0x3F)
        // ====================================================================
        //
        // EIA-608-B §A.6 second pair.  Includes some box-drawing
        // glyphs at the tail.

        constexpr SpecialEntry kExtFrench[32] = {
                /* 0x20 */ {0x00C3, 'A'}, // Ã
                /* 0x21 */ {0x00E3, 'a'}, // ã
                /* 0x22 */ {0x00CD, 'I'}, // Í
                /* 0x23 */ {0x00CC, 'I'}, // Ì
                /* 0x24 */ {0x00EC, 'i'}, // ì
                /* 0x25 */ {0x00D2, 'O'}, // Ò
                /* 0x26 */ {0x00F2, 'o'}, // ò
                /* 0x27 */ {0x00D5, 'O'}, // Õ
                /* 0x28 */ {0x00F5, 'o'}, // õ
                /* 0x29 */ {0x007B, '{'}, // {
                /* 0x2A */ {0x007D, '}'}, // }
                /* 0x2B */ {0x005C, '\\'},// \    (.
                /* 0x2C */ {0x005E, '^'}, // ^
                /* 0x2D */ {0x005F, '_'}, // _
                /* 0x2E */ {0x007C, '|'}, // |
                /* 0x2F */ {0x007E, '~'}, // ~
                /* 0x30 */ {0x00C4, 'A'}, // Ä
                /* 0x31 */ {0x00E4, 'a'}, // ä
                /* 0x32 */ {0x00D6, 'O'}, // Ö
                /* 0x33 */ {0x00F6, 'o'}, // ö
                /* 0x34 */ {0x00DF, 'B'}, // ß
                /* 0x35 */ {0x00A5, 'Y'}, // ¥
                /* 0x36 */ {0x00A4, 'X'}, // ¤
                /* 0x37 */ {0x2502, '|'}, // │ box vertical
                /* 0x38 */ {0x00C5, 'A'}, // Å
                /* 0x39 */ {0x00E5, 'a'}, // å
                /* 0x3A */ {0x00D8, 'O'}, // Ø
                /* 0x3B */ {0x00F8, 'o'}, // ø
                /* 0x3C */ {0x250C, '+'}, // ┌ box top-left
                /* 0x3D */ {0x2510, '+'}, // ┐ box top-right
                /* 0x3E */ {0x2514, '+'}, // └ box bottom-left
                /* 0x3F */ {0x2518, '+'}, // ┘ box bottom-right
        };

        bool isG0RemapByte(uint8_t b) {
                for (const G0Remap &r : kG0Remap) {
                        if (r.byte == b) return true;
                }
                return false;
        }

} // namespace

// ============================================================================
// Basic G0
// ============================================================================

uint32_t Cea608Ext::decodeG0(uint8_t b) {
        for (const G0Remap &r : kG0Remap) {
                if (r.byte == b) return r.cp;
        }
        return static_cast<uint32_t>(b);
}

uint8_t Cea608Ext::encodeG0(uint32_t cp) {
        // Codepoints in the ASCII range (0x20..0x7F) that aren't one
        // of the ten remapped positions encode as themselves.
        if (cp >= 0x20 && cp <= 0x7F && !isG0RemapByte(static_cast<uint8_t>(cp))) {
                return static_cast<uint8_t>(cp);
        }
        for (const G0Remap &r : kG0Remap) {
                if (r.cp == cp) return r.byte;
        }
        return NoMapping;
}

// ============================================================================
// Special Characters
// ============================================================================

uint32_t Cea608Ext::decodeSpecial(uint8_t idx) {
        if (idx < 0x30 || idx > 0x3F) return NoCodepoint;
        return kSpecial[idx - 0x30].cp;
}

uint8_t Cea608Ext::encodeSpecial(uint32_t cp) {
        for (uint8_t i = 0; i < 16; ++i) {
                if (kSpecial[i].cp == cp) return static_cast<uint8_t>(0x30 + i);
        }
        return NoMapping;
}

uint8_t Cea608Ext::specialPlaceholder(uint8_t idx) {
        if (idx < 0x30 || idx > 0x3F) return ' ';
        return kSpecial[idx - 0x30].placeholder;
}

// ============================================================================
// Extended Spanish
// ============================================================================

uint32_t Cea608Ext::decodeExtSpanish(uint8_t idx) {
        if (idx < 0x20 || idx > 0x3F) return NoCodepoint;
        return kExtSpanish[idx - 0x20].cp;
}

uint8_t Cea608Ext::encodeExtSpanish(uint32_t cp) {
        for (uint8_t i = 0; i < 32; ++i) {
                if (kExtSpanish[i].cp == cp) return static_cast<uint8_t>(0x20 + i);
        }
        return NoMapping;
}

uint8_t Cea608Ext::extSpanishPlaceholder(uint8_t idx) {
        if (idx < 0x20 || idx > 0x3F) return ' ';
        return kExtSpanish[idx - 0x20].placeholder;
}

// ============================================================================
// Extended French / German / Portuguese
// ============================================================================

uint32_t Cea608Ext::decodeExtFrench(uint8_t idx) {
        if (idx < 0x20 || idx > 0x3F) return NoCodepoint;
        return kExtFrench[idx - 0x20].cp;
}

uint8_t Cea608Ext::encodeExtFrench(uint32_t cp) {
        for (uint8_t i = 0; i < 32; ++i) {
                if (kExtFrench[i].cp == cp) return static_cast<uint8_t>(0x20 + i);
        }
        return NoMapping;
}

uint8_t Cea608Ext::extFrenchPlaceholder(uint8_t idx) {
        if (idx < 0x20 || idx > 0x3F) return ' ';
        return kExtFrench[idx - 0x20].placeholder;
}

// ============================================================================
// Composite encode
// ============================================================================

Cea608Ext::EncodedChar Cea608Ext::encode(uint32_t cp) {
        EncodedChar out;
        // 1. Basic G0 (covers ASCII + the ten remapped Latin / arith
        //    glyphs).  Cheapest: one wire byte.
        const uint8_t g0 = encodeG0(cp);
        if (g0 != NoMapping) {
                out.kind = Kind::BasicG0;
                out.byte = g0;
                return out;
        }
        // 2. Special Characters.
        const uint8_t sp = encodeSpecial(cp);
        if (sp != NoMapping) {
                out.kind = Kind::Special;
                out.placeholder = specialPlaceholder(sp);
                out.code = sp;
                return out;
        }
        // 3. Extended Spanish / Misc.
        const uint8_t es = encodeExtSpanish(cp);
        if (es != NoMapping) {
                out.kind = Kind::ExtSpanish;
                out.placeholder = extSpanishPlaceholder(es);
                out.code = es;
                return out;
        }
        // 4. Extended Portuguese / German.
        const uint8_t ef = encodeExtFrench(cp);
        if (ef != NoMapping) {
                out.kind = Kind::ExtFrench;
                out.placeholder = extFrenchPlaceholder(ef);
                out.code = ef;
                return out;
        }
        // 5. None.
        out.kind = Kind::None;
        return out;
}

PROMEKI_NAMESPACE_END
