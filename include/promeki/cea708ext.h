/**
 * @file      cea708ext.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_PROAV
#include <cstdint>
#include <promeki/namespace.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief CEA-708-D Annex G G2 / G3 extended-character tables and the
 *        encoder helper that picks a wire encoding for an arbitrary
 *        Unicode codepoint.
 * @ingroup proav
 *
 * CEA-708 carries text via four overlapping byte ranges:
 *
 *  - **G0** (@c 0x20..0x7F): basic ASCII, with @c 0x7F redefined as
 *    the music-note glyph.
 *  - **G1** (@c 0xA0..0xFF): direct mapping to Latin-1 supplement
 *    (U+00A0..U+00FF).
 *  - **G2** — typographic extras, fractions and box-drawing
 *    glyphs.  Reached via the @c EXT1 (@c 0x10) prefix followed by a
 *    byte in @c 0x20..0x7F.  The G2 table is sparse (about 26 of the
 *    96 positions are defined); the rest are reserved.
 *  - **G3** — a single defined position at @c EXT1 + @c 0xA0
 *    carrying the ATSC closed-caption logo.  No standard Unicode
 *    point exists; the library encodes / decodes it as
 *    @ref Cea708Ext::G3CcLogo (U+E000, Private Use Area) so the
 *    glyph survives a wire round-trip.
 *
 * Codepoints that fall outside G0/G1/G2/G3 are encoded with the
 * @c P16 (@c 0x18) prefix followed by a 16-bit value interpreted as
 * UTF-16BE.  Codepoints outside the BMP encode as a UTF-16 surrogate
 * pair (two consecutive @c P16 sequences = 6 wire bytes).
 *
 * The class is static-only — all members are @c static.  Both the
 * encoder (Cea708Encoder) and the decoder (Cea708WindowState) use
 * these helpers as the single source of truth for the mappings.
 */
struct Cea708Ext {
                /// @brief Sentinel returned by @ref encodeG2 / @ref encodeG3
                ///        when a codepoint has no G2 / G3 mapping.
                static constexpr uint8_t NoMapping = 0xFF;

                /// @brief Sentinel returned by @ref decodeG2 / @ref decodeG3
                ///        when a wire byte has no defined glyph.  Callers
                ///        substitute U+FFFD (REPLACEMENT CHARACTER) when
                ///        they need a printable codepoint.
                static constexpr uint32_t NoCodepoint = 0;

                /// @brief Codepoint the library uses for the ATSC CC logo
                ///        carried at G3 position @c 0xA0.  CEA-708-D does
                ///        not name a standard Unicode point; a Private
                ///        Use Area codepoint round-trips losslessly
                ///        through Unicode-aware text pipelines.
                static constexpr uint32_t G3CcLogo = 0xE000;

                /// @brief Wire byte for the @c EXT1 prefix (C0 control,
                ///        introduces a G2 / G3 / C2 / C3 byte).
                static constexpr uint8_t Ext1 = 0x10;

                /// @brief Wire byte for the @c P16 prefix (C0 control,
                ///        introduces a 2-byte big-endian UTF-16 value).
                static constexpr uint8_t P16 = 0x18;

                /**
                 * @brief Maps a Unicode codepoint to its EXT1+G2 wire
                 *        byte.
                 *
                 * @param cp Unicode codepoint.
                 * @return The G2 byte (in @c 0x20..0x7F) when @p cp is
                 *         in the table; @ref NoMapping otherwise.
                 */
                static uint8_t encodeG2(uint32_t cp);

                /**
                 * @brief Maps a Unicode codepoint to its EXT1+G3 wire
                 *        byte.
                 *
                 * @param cp Unicode codepoint.
                 * @return The G3 byte (currently only @c 0xA0 is
                 *         defined for @ref G3CcLogo) or @ref NoMapping
                 *         when @p cp has no G3 mapping.
                 */
                static uint8_t encodeG3(uint32_t cp);

                /**
                 * @brief Decodes an EXT1+G2 wire byte to a codepoint.
                 *
                 * @param b G2 byte (caller has already consumed the
                 *          @c EXT1 prefix and verified @p b is in
                 *          @c 0x20..0x7F).
                 * @return The mapped codepoint, or @ref NoCodepoint
                 *         when @p b is at a reserved / undefined G2
                 *         position.
                 */
                static uint32_t decodeG2(uint8_t b);

                /**
                 * @brief Decodes an EXT1+G3 wire byte to a codepoint.
                 *
                 * @param b G3 byte (caller has already consumed the
                 *          @c EXT1 prefix and verified @p b is in
                 *          @c 0xA0..0xFF).
                 * @return @ref G3CcLogo when @p b is @c 0xA0;
                 *         @ref NoCodepoint at every other position
                 *         (the rest of G3 is reserved).
                 */
                static uint32_t decodeG3(uint8_t b);

                /// @brief Result of @ref encode for a single codepoint
                ///        — up to 6 wire bytes (a UTF-16 surrogate
                ///        pair via two @c P16 sequences).
                struct EncodedChar {
                                uint8_t bytes[6] = {0, 0, 0, 0, 0, 0};
                                uint8_t length = 0; ///< 1..6
                };

                /**
                 * @brief Picks the most compact CEA-708 wire encoding
                 *        for @p cp.
                 *
                 * Decision tree (in order):
                 *  1. @c 0x20..0x7F &rarr; single G0 byte.
                 *  2. @c 0x00A0..@c 0x00FF &rarr; single G1 byte.
                 *  3. In G2 table &rarr; @c EXT1 + G2 byte (2 bytes).
                 *  4. Equal to @ref G3CcLogo &rarr; @c EXT1 + @c 0xA0
                 *     (2 bytes).
                 *  5. BMP codepoint (@c 0x0000..@c 0xFFFF) &rarr;
                 *     @c P16 + hi + lo (3 bytes).
                 *  6. Astral codepoint (@c 0x10000..@c 0x10FFFF)
                 *     &rarr; UTF-16 surrogate pair via two @c P16
                 *     sequences (6 bytes).
                 *  7. Anything outside Unicode &rarr; substitute
                 *     U+FFFD (3 bytes via P16).
                 *
                 * Codepoints below @c 0x20 (C0 controls) and in
                 * @c 0x80..@c 0x9F (C1 controls) cannot ride the wire
                 * directly; they round-trip via @c P16 instead, which
                 * decoders treat as opaque 16-bit text rather than a
                 * control-code dispatch.
                 *
                 * @param cp Unicode codepoint to encode.
                 * @return Wire bytes plus length (always &ge; 1).
                 */
                static EncodedChar encode(uint32_t cp);

                Cea708Ext() = delete;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_PROAV
