/**
 * @file      cea608.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstdint>
#include <promeki/color.h>
#include <promeki/namespace.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief CEA-608 line-21 caption primitives shared by @ref Cea608Encoder
 *        and @ref Cea608Decoder.
 * @ingroup proav
 *
 * Static-only namespace-class collecting the bit-level constants and
 * helpers that both the encoder and the decoder need: odd-parity
 * stamping / checking, the byte values of the well-known two-byte
 * control codes (RCL / PAC / EOC / EDM / …), and the channel-field
 * mapping used to select CC1..CC4.
 *
 * All byte values below are **pre-parity** — bit 7 is zero in the
 * constants.  Stamping odd parity is the encoder's responsibility
 * (via @ref withOddParity); the decoder strips it via
 * @ref stripParity after first validating with @ref checkOddParity.
 *
 * @par Channel layout
 *
 * CEA-608 carries up to four caption channels (CC1..CC4) plus four
 * text channels (T1..T4) across two NTSC fields:
 *
 *  - Field 1: CC1 + CC2 + T1 + T2
 *  - Field 2: CC3 + CC4 + T3 + T4
 *
 * Inside a @ref Cea708Cdp::CcData triple, @c cc_type selects the
 * field (0 = field 1, 1 = field 2).  Within a field, the channel
 * is encoded in the first byte: the "channel bit" (bit 11 of the
 * 16-bit big-endian pair, i.e. bit 3 of byte 1 after parity strip)
 * is 0 for CC1/CC3 and 1 for CC2/CC4.
 */
struct Cea608 {
                // -- Odd parity ---------------------------------------------

                /// @brief Stamps odd parity on the high bit of @p c.
                ///        Returns @p c with bit 7 set so that the
                ///        total count of 1-bits in the byte is odd.
                static constexpr uint8_t withOddParity(uint8_t c) {
                        uint8_t v = static_cast<uint8_t>(c & 0x7F);
                        unsigned ones = 0;
                        for (int i = 0; i < 7; ++i) {
                                if (v & (1u << i)) ++ones;
                        }
                        if ((ones & 1u) == 0u) v = static_cast<uint8_t>(v | 0x80);
                        return v;
                }

                /// @brief Returns @c true when @p c has odd parity on
                ///        bit 7 (i.e. is a valid received CEA-608 byte).
                static constexpr bool checkOddParity(uint8_t c) {
                        unsigned ones = 0;
                        for (int i = 0; i < 8; ++i) {
                                if (c & (1u << i)) ++ones;
                        }
                        return (ones & 1u) != 0u;
                }

                /// @brief Strips bit 7 (the parity bit) from @p c.
                static constexpr uint8_t stripParity(uint8_t c) { return static_cast<uint8_t>(c & 0x7F); }

                // -- Channel selectors (pre-parity first-byte values) -------

                /// @brief First-byte value used for the field-1 / CC1
                ///        miscellaneous control codes (RCL, EDM, EOC,
                ///        …).  Combined with the per-code second byte
                ///        below to form a full @c (b1, b2) pair.
                ///        CC2/CC3/CC4 use different first bytes; not
                ///        modelled here (v1 is CC1-only).
                static constexpr uint8_t Cc1MiscFirstByte = 0x14;

                // -- Misc control codes (second-byte values) ----------------

                static constexpr uint8_t MiscRCL = 0x20; ///< Resume Caption Loading (pop-on).
                static constexpr uint8_t MiscBS  = 0x21; ///< Backspace.
                static constexpr uint8_t MiscDER = 0x24; ///< Delete to End of Row.
                static constexpr uint8_t MiscRU2 = 0x25; ///< Roll-Up 2 rows.
                static constexpr uint8_t MiscRU3 = 0x26; ///< Roll-Up 3 rows.
                static constexpr uint8_t MiscRU4 = 0x27; ///< Roll-Up 4 rows.
                static constexpr uint8_t MiscFON = 0x28; ///< Flash On.
                static constexpr uint8_t MiscRDC = 0x29; ///< Resume Direct Captioning (paint-on).
                static constexpr uint8_t MiscTR  = 0x2A; ///< Text Restart.
                static constexpr uint8_t MiscRTD = 0x2B; ///< Resume Text Display.
                static constexpr uint8_t MiscEDM = 0x2C; ///< Erase Displayed Memory.
                static constexpr uint8_t MiscCR  = 0x2D; ///< Carriage Return (roll-up advance).
                static constexpr uint8_t MiscENM = 0x2E; ///< Erase Non-displayed Memory.
                static constexpr uint8_t MiscEOC = 0x2F; ///< End Of Caption (swap memories).

                // -- Convenience: pre-parity (b1, b2) for CC1 control pairs --

                /// @brief CC1 RCL: @c (0x14, 0x20).
                static constexpr uint8_t RclB1 = Cc1MiscFirstByte;
                static constexpr uint8_t RclB2 = MiscRCL;

                /// @brief CC1 EOC: @c (0x14, 0x2F).
                static constexpr uint8_t EocB1 = Cc1MiscFirstByte;
                static constexpr uint8_t EocB2 = MiscEOC;

                /// @brief CC1 EDM: @c (0x14, 0x2C).
                static constexpr uint8_t EdmB1 = Cc1MiscFirstByte;
                static constexpr uint8_t EdmB2 = MiscEDM;

                /// @brief CC1 ENM: @c (0x14, 0x2E).
                static constexpr uint8_t EnmB1 = Cc1MiscFirstByte;
                static constexpr uint8_t EnmB2 = MiscENM;

                // -- PAC (Preamble Address Code) ----------------------------

                /// @brief CC1 Row 15 / Column 0 / White / No underline.
                ///        Standard pop-on bottom-row anchor for
                ///        ASCII captions.  Computed dynamically by
                ///        @ref encodePac; provided as a constant for
                ///        the encoder's "no styling configured"
                ///        fast-path.
                static constexpr uint8_t PacRow15Col0WhiteB1 = 0x14;
                static constexpr uint8_t PacRow15Col0WhiteB2 = 0x70;

                // -- CEA-608 colour palette ---------------------------------

                /**
                 * @brief CEA-608's 7-colour primary palette.
                 *
                 * 608 doesn't carry arbitrary RGB — every styled span
                 * has to be quantized to one of these primaries.  The
                 * enum values are the @c PreambleColor codes the spec
                 * uses internally (also accepted as mid-row colour
                 * indices), so the same values double as the "colour
                 * subfield" inside @ref encodePac and @ref encodeMidRow.
                 *
                 * Italic captions in CEA-608 are encoded as "italic
                 * white" — italic isn't combinable with a non-white
                 * colour at the wire level.  @ref encodePac /
                 * @ref encodeMidRow accept a separate @c italic flag
                 * and prefer italic over colour when both are set.
                 */
                enum class CaptionColor : uint8_t {
                        White   = 0,
                        Green   = 1,
                        Blue    = 2,
                        Cyan    = 3,
                        Red     = 4,
                        Yellow  = 5,
                        Magenta = 6,
                };

                /**
                 * @brief Number of palette entries (7 — the spec's
                 *        well-known primaries).
                 */
                static constexpr size_t CaptionColorCount = 7;

                /**
                 * @brief sRGB Color values for each @ref CaptionColor in
                 *        index order.
                 *
                 * Use with @ref Color::nearestPaletteIndex to map an
                 * arbitrary span colour to its closest 608 primary:
                 *
                 * @code
                 * size_t idx = span.color().nearestPaletteIndex(Cea608::palette());
                 * Cea608::CaptionColor c = static_cast<Cea608::CaptionColor>(idx);
                 * @endcode
                 *
                 * The palette is a value list (returned by value); the
                 * underlying @ref Color::List entries are cheap CoW
                 * handles so callers may freely cache or copy.
                 */
                static Color::List palette();

                /// @brief Bundle of attributes a PAC sets at line start.
                struct PacAttr {
                                /// @brief Display row 1..15.  Clamped on encode.
                                int row = 15;
                                /// @brief Indent column (multiples of 4: 0,4,8,12,16,20,24,28).
                                ///        Quantised on encode.  PAC alone
                                ///        can't represent col 1..3 — pair
                                ///        with @ref TabOffset1/2/3 to add
                                ///        a 1-3 column fine shift.
                                int indentCol = 0;
                                /// @brief Foreground colour.
                                CaptionColor color = CaptionColor::White;
                                /// @brief Italic flag.  Forces colour to white
                                ///        on the wire — CEA-608 can't combine
                                ///        italic with a non-white colour.
                                bool italic = false;
                                /// @brief Underline flag.
                                bool underline = false;
                };

                /**
                 * @brief Encodes a PAC into a pre-parity @c (b1, b2)
                 *        pair.
                 *
                 * Always succeeds — the implementation clamps @c row
                 * to 1..15 and snaps @c indentCol down to the nearest
                 * multiple of 4 (0..28).  When @c italic is set the
                 * encoded colour is forced to "italic white" and the
                 * supplied @c color / indentCol are ignored.
                 *
                 * The two bytes are returned pre-parity (bit 7 always
                 * 0); callers stamp odd parity via @ref withOddParity
                 * just before they go on the wire.
                 *
                 * @param attr Source attributes.
                 * @param[out] b1 First wire byte (pre-parity).
                 * @param[out] b2 Second wire byte (pre-parity).
                 */
                static void encodePac(const PacAttr &attr, uint8_t &b1, uint8_t &b2);

                /**
                 * @brief Returns @c true when @c (b1, b2) is a CC1 PAC.
                 *
                 * Inputs are pre-parity (bit 7 zero).  PAC pairs have
                 * @c b1 in @c {0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
                 * 0x16, 0x17} and @c b2 in @c [0x40, 0x7F]; other ranges
                 * belong to mid-row codes (b2 0x20-0x2F), misc control
                 * codes (b2 0x20-0x2F on b1=0x14), Tab Offsets, etc.
                 */
                static bool isPac(uint8_t b1, uint8_t b2);

                /**
                 * @brief Decodes a CC1 PAC into its attributes.
                 *
                 * @return @c true on success; @c false when
                 *         @c (b1, b2) is not a recognised PAC.
                 */
                static bool decodePac(uint8_t b1, uint8_t b2, PacAttr &out);

                // -- Mid-row codes ------------------------------------------

                /**
                 * @brief Encodes a mid-row code (colour / italic /
                 *        underline change within a line).
                 *
                 * @param color     Foreground colour.  Ignored when
                 *                  @p italic is @c true (italic is
                 *                  always white on the wire).
                 * @param italic    Italic flag.
                 * @param underline Underline flag.
                 * @param[out] b1   First wire byte (pre-parity).
                 * @param[out] b2   Second wire byte (pre-parity).
                 *
                 * Always succeeds.
                 */
                static void encodeMidRow(CaptionColor color, bool italic, bool underline, uint8_t &b1,
                                         uint8_t &b2);

                /**
                 * @brief Returns @c true when @c (b1, b2) is a CC1
                 *        mid-row code.
                 */
                static bool isMidRow(uint8_t b1, uint8_t b2);

                /**
                 * @brief Decodes a mid-row code.
                 *
                 * @return @c true on success.
                 */
                static bool decodeMidRow(uint8_t b1, uint8_t b2, CaptionColor &outColor, bool &outItalic,
                                         bool &outUnderline);

                // -- Null pair (no-op filler) -------------------------------

                /// @brief CEA-608 null filler.  Pre-parity @c (0x00, 0x00);
                ///        with odd parity stamped that becomes
                ///        @c (0x80, 0x80) on the wire.  Inserted by the
                ///        encoder on frames where the schedule has
                ///        nothing to send, ignored by the decoder.
                static constexpr uint8_t NullB1 = 0x00;
                static constexpr uint8_t NullB2 = 0x00;

                // -- Predicates ---------------------------------------------

                /// @brief Returns @c true when the (parity-stripped)
                ///        byte is a printable CEA-608 character (the
                ///        spec calls this the "Basic Character Set",
                ///        roughly 0x20..0x7F with a few substitutions
                ///        for accented characters at 0x2A/0x5C/0x5E/…).
                ///        The encoder routes characters this returns
                ///        @c true for as raw passthrough.
                static constexpr bool isBasicChar(uint8_t c) {
                        const uint8_t v = stripParity(c);
                        return v >= 0x20 && v <= 0x7F;
                }

                Cea608() = delete;
};

PROMEKI_NAMESPACE_END
