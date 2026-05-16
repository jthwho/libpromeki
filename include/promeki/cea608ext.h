/**
 * @file      cea608ext.h
 * @copyright Howard Logic. All rights reserved.
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
 * @brief CEA-608 (EIA-608-B) character-set helpers covering the
 *        basic G0 set plus the Special Characters and Extended Western
 *        European tables.
 * @ingroup proav
 *
 * CEA-608 carries text via four ranges:
 *
 *  - **Basic G0** (@c 0x20..@c 0x7F): mostly ASCII, with ten code
 *    positions remapped to Spanish / French / arithmetic glyphs:
 *      @c 0x2A=á  @c 0x5C=é  @c 0x5E=í  @c 0x5F=ó  @c 0x60=ú
 *      @c 0x7B=ç  @c 0x7C=÷  @c 0x7D=Ñ  @c 0x7E=ñ  @c 0x7F=█
 *  - **Special Characters** (16 glyphs): two-byte control code on
 *    CC1 = @c (0x11, @c 0x30..0x3F).  Per EIA-608-E §B.5 the receiver
 *    *replaces* the previously displayed character with the special
 *    glyph — backward-compatible for old decoders that ignore the
 *    code and show the placeholder (typically a best-fit ASCII
 *    fallback the encoder sent first).
 *  - **Extended Western European** (32 glyphs each):
 *      - @c (0x12, @c 0x20..0x3F) — Spanish, French, miscellaneous.
 *      - @c (0x13, @c 0x20..0x3F) — Portuguese, German, Danish.
 *    Same "replace previously displayed character" semantics as
 *    Special Characters.
 *
 * All wire bytes below are **pre-parity** (bit 7 zero); callers
 * stamp odd parity via @ref Cea608::withOddParity just before the
 * bytes go on the wire.
 *
 * The class is static-only.
 */
struct Cea608Ext {
                /// @brief Sentinel — codepoint not represented by the
                ///        addressed table.
                static constexpr uint32_t NoCodepoint = 0;

                /// @brief Sentinel — codepoint has no encoding in the
                ///        addressed table.
                static constexpr uint8_t NoMapping = 0xFF;

                // -- Basic G0 (10 remapped positions) -----------------------

                /**
                 * @brief Returns the Unicode codepoint that the basic
                 *        G0 byte @p b decodes to.
                 *
                 * Most G0 positions decode as plain ASCII (the byte
                 * value itself).  Ten positions carry Spanish / French
                 * / arithmetic glyphs.
                 *
                 * @param b G0 byte (caller has already stripped odd
                 *          parity and verified @p b is in
                 *          @c 0x20..0x7F).
                 * @return The mapped codepoint.  Always non-zero for
                 *         valid input.
                 */
                static uint32_t decodeG0(uint8_t b);

                /**
                 * @brief Returns the basic G0 byte for codepoint @p cp,
                 *        or @ref NoMapping when @p cp has no G0
                 *        encoding.
                 *
                 * Returns the literal byte for ASCII codepoints in
                 * @c 0x20..0x7F that aren't one of the ten remapped
                 * positions, the corresponding remapped byte for the
                 * ten Latin / arithmetic glyphs, and @ref NoMapping
                 * otherwise.
                 */
                static uint8_t encodeG0(uint32_t cp);

                // -- Special Characters (16 glyphs) -------------------------

                /**
                 * @brief Returns the codepoint for special-character
                 *        index @p idx (@c 0x30..0x3F = the 16 glyphs).
                 */
                static uint32_t decodeSpecial(uint8_t idx);

                /**
                 * @brief Returns the @c 0x30..0x3F second-byte index
                 *        that encodes codepoint @p cp as a Special
                 *        Character, or @ref NoMapping when @p cp has
                 *        no Special-Character encoding.
                 */
                static uint8_t encodeSpecial(uint32_t cp);

                /**
                 * @brief Returns the best-fit ASCII placeholder byte
                 *        for the Special Character at @p idx.
                 *
                 * Per spec the encoder sends this byte first
                 * (ahead of the doubled Special-Character control
                 * code) so old decoders that ignore the code show
                 * the placeholder and modern decoders replace it
                 * with the real glyph.
                 *
                 * @param idx Second-byte index (@c 0x30..0x3F).
                 */
                static uint8_t specialPlaceholder(uint8_t idx);

                // -- Extended Spanish / Misc (0x12 / 0x1A) ------------------

                /**
                 * @brief Returns the codepoint for extended-Spanish
                 *        index @p idx (@c 0x20..0x3F).
                 */
                static uint32_t decodeExtSpanish(uint8_t idx);

                /**
                 * @brief Returns the @c 0x20..0x3F second-byte index
                 *        for codepoint @p cp in the extended-Spanish
                 *        table, or @ref NoMapping when @p cp is not
                 *        in this table.
                 */
                static uint8_t encodeExtSpanish(uint32_t cp);

                /**
                 * @brief Best-fit ASCII placeholder for an extended-
                 *        Spanish glyph.  Same backward-compat role as
                 *        @ref specialPlaceholder.
                 */
                static uint8_t extSpanishPlaceholder(uint8_t idx);

                // -- Extended Portuguese / German (0x13 / 0x1B) -------------

                /**
                 * @brief Returns the codepoint for extended-Portuguese
                 *        index @p idx (@c 0x20..0x3F).
                 */
                static uint32_t decodeExtFrench(uint8_t idx);

                /**
                 * @brief Returns the @c 0x20..0x3F second-byte index
                 *        for codepoint @p cp in the extended-Portuguese
                 *        table, or @ref NoMapping when @p cp is not in
                 *        this table.
                 */
                static uint8_t encodeExtFrench(uint32_t cp);

                /**
                 * @brief Best-fit ASCII placeholder for an extended-
                 *        Portuguese glyph.
                 */
                static uint8_t extFrenchPlaceholder(uint8_t idx);

                // -- Composite encode --------------------------------------

                /// @brief Wire-encoding kind chosen by @ref encode.
                enum class Kind : uint8_t {
                        /// @brief Codepoint has no representation in
                        ///        any 608 table — caller substitutes
                        ///        a space (0x20).
                        None = 0,
                        /// @brief Single basic-G0 byte (no control
                        ///        code needed).  @ref EncodedChar::byte
                        ///        is the wire byte.
                        BasicG0 = 1,
                        /// @brief Special Character — caller emits
                        ///        @ref EncodedChar::placeholder first
                        ///        (so old decoders show a fallback),
                        ///        then a doubled @c (0x11, code) pair
                        ///        on CC1 (or @c (0x19, code) on CC2)
                        ///        which replaces the placeholder.
                        Special = 2,
                        /// @brief Extended Spanish — placeholder +
                        ///        doubled @c (0x12, code) on CC1
                        ///        (or @c (0x1A, code) on CC2).
                        ExtSpanish = 3,
                        /// @brief Extended Portuguese / German —
                        ///        placeholder + doubled
                        ///        @c (0x13, code) on CC1 (or
                        ///        @c (0x1B, code) on CC2).
                        ExtFrench = 4,
                };

                /// @brief Result of @ref encode.  @c kind tells the
                ///        caller which path to take; the byte fields
                ///        carry the wire bytes.
                struct EncodedChar {
                                Kind    kind = Kind::None;
                                uint8_t byte = 0;        ///< G0 byte (kind == BasicG0).
                                uint8_t placeholder = 0; ///< Best-fit ASCII (kind != BasicG0).
                                uint8_t code = 0;        ///< Second control byte (kind != BasicG0).
                };

                /**
                 * @brief Picks the most compact CEA-608 wire encoding
                 *        for codepoint @p cp.
                 *
                 * Decision tree:
                 *  1. Basic G0 (ASCII or one of the ten remapped
                 *     positions) &rarr; one byte.
                 *  2. Special Character &rarr; placeholder + doubled
                 *     control pair.
                 *  3. Extended Spanish &rarr; placeholder + doubled
                 *     control pair.
                 *  4. Extended Portuguese &rarr; placeholder + doubled
                 *     control pair.
                 *  5. Anything else &rarr; @c kind = @ref Kind::None
                 *     (the caller substitutes a space).
                 */
                static EncodedChar encode(uint32_t cp);

                Cea608Ext() = delete;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_PROAV
