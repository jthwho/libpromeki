/**
 * @file      cea608.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_PROAV
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
 * constants.  Every decode helper in this struct (@ref decodePac,
 * @ref decodeMidRow, @ref decodeMisc, @ref decodeBgAttribute, etc.)
 * assumes its inputs have already been validated with
 * @ref checkOddParity (or @ref checkOddParityPair for byte pairs) and
 * stripped via @ref stripParity.  The decoders do **not** re-validate
 * parity — feeding them bytes with bit 7 still set will misinterpret
 * the upper bit as data and produce garbage.  The typical ingress
 * pattern is:
 *
 * @code
 * if (!Cea608::checkOddParityPair(b1, b2)) continue; // drop the pair
 * const uint8_t v1 = Cea608::stripParity(b1);
 * const uint8_t v2 = Cea608::stripParity(b2);
 * if (Cea608::decodePac(v1, v2, pac)) { ... }
 * @endcode
 *
 * Stamping odd parity is the encoder's responsibility
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
 * is encoded in the first byte: bit 3 of byte 1 (after parity
 * strip) is 0 for CC1/CC3 and 1 for CC2/CC4.  Equivalently, if
 * the byte pair is viewed as a 16-bit big-endian word @c ((b1<<8)
 * | b2), the channel bit lives at bit 11 of that 16-bit value.
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

                /// @brief Validates odd parity on both bytes of a
                ///        received pair.  Returns @c true only when
                ///        both bytes pass @ref checkOddParity.  Use
                ///        this at the field-2 / line-21 ingress before
                ///        feeding pre-parity bytes (via @ref stripParity)
                ///        into the higher-level decoders
                ///        (@ref decodePac, @ref decodeMidRow,
                ///        @ref decodeMisc, @ref decodeBgAttribute) —
                ///        those decoders trust their inputs to already
                ///        be parity-validated and parity-stripped per
                ///        the contract documented on each function.
                static constexpr bool checkOddParityPair(uint8_t b1, uint8_t b2) {
                        return checkOddParity(b1) && checkOddParity(b2);
                }

                // -- Channel selectors (pre-parity first-byte values) -------

                /// @brief Caption channel selector.  CC1 / CC2 ride in
                ///        field 1 (@c cc_type = 0); CC3 / CC4 ride in
                ///        field 2 (@c cc_type = 1).  Within a field,
                ///        the second channel (CC2 / CC4) is encoded by
                ///        bit 3 of the first control byte being set;
                ///        CC1 / CC3 leave bit 3 clear.  Used by
                ///        @ref applyChannel to retarget a CC1-shaped
                ///        control pair onto its CC2 / CC3 / CC4 sibling.
                enum class Channel : uint8_t {
                        CC1 = 0, ///< Field 1, channel 1.
                        CC2 = 1, ///< Field 1, channel 2.
                        CC3 = 2, ///< Field 2, channel 1.
                        CC4 = 3, ///< Field 2, channel 2.
                };

                /// @brief @c true when @p ch is the second channel of
                ///        its field (CC2 or CC4) — those channels OR
                ///        @c 0x08 into the first control byte to set
                ///        the intra-field channel bit.
                static constexpr bool isSecondChannelOfField(Channel ch) {
                        return ch == Channel::CC2 || ch == Channel::CC4;
                }

                /// @brief @c true when @p ch lives in field 2 — those
                ///        channels trigger the §8.4(a)(b) misc-control
                ///        first-byte remap (@c 0x14 → @c 0x15,
                ///        @c 0x1C → @c 0x1D).
                static constexpr bool isFieldTwo(Channel ch) {
                        return ch == Channel::CC3 || ch == Channel::CC4;
                }

                /// @brief First-byte value used for the field-1 / CC1
                ///        miscellaneous control codes (RCL, EDM, EOC,
                ///        …).  Combined with the per-code second byte
                ///        below to form a full @c (b1, b2) pair.  Use
                ///        @ref applyChannel to retarget the resulting
                ///        pair onto CC2 / CC3 / CC4 (the encoder builds
                ///        every control pair CC1-shaped and channel-
                ///        shifts in a single post-pass).
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
                 * @brief CEA-608's 8-colour palette.
                 *
                 * 608 doesn't carry arbitrary RGB — every styled span
                 * has to be quantised to one of these primaries.  The
                 * enum values are the @c PreambleColor codes the spec
                 * uses internally (also accepted as mid-row + BG-
                 * attribute colour indices), so the same values
                 * double as the "colour subfield" inside
                 * @ref encodePac, @ref encodeMidRow, and
                 * @ref encodeBgAttribute.
                 *
                 * Index 0..6 (White / Green / Blue / Cyan / Red /
                 * Yellow / Magenta) are valid for both foreground
                 * (PAC + mid-row) and background paths.  Index 7
                 * (Black) is **BG-attribute only** — the foreground
                 * subfields don't carry Black (the spec carves the
                 * code-7 slot out for "italic white" in PAC and
                 * mid-row, leaving only the BG-attribute code family
                 * with a Black entry).  @ref encodePac /
                 * @ref encodeMidRow treat @c Black as @c White on
                 * the wire; @ref encodeBgAttribute round-trips it
                 * with index 7.
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
                        Black   = 7, ///< BG-attribute only — fg paths treat as White.
                };

                /**
                 * @brief Number of palette entries (8 — 7 primaries
                 *        plus Black for BG attribute).
                 */
                static constexpr size_t CaptionColorCount = 8;

                /**
                 * @brief Number of palette entries that round-trip
                 *        through the foreground paths (PAC + mid-row
                 *        colour subfield).
                 *
                 * The PAC + mid-row colour subfield carries 3 bits;
                 * code 7 is reserved for "italic white", leaving 7
                 * encodable foreground primaries (White / Green /
                 * Blue / Cyan / Red / Yellow / Magenta).  Black
                 * (palette index 7) is BG-attribute only — fg paths
                 * silently fall back to White on the wire.  Tests
                 * that iterate the round-trip-able fg colour set use
                 * this constant.
                 */
                static constexpr size_t FgCaptionColorCount = 7;

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

                // -- Background attribute codes -----------------------------
                //
                // Background attribute codes (CTA-608-E §6.2 Table 3) live
                // at @c b1=0x10 (CC1 channel 1) with @c b2 in @c [0x20, 0x2F].
                // They set the background colour and opacity for subsequent
                // characters on the row.  Doubled per spec like other
                // control codes.  This is part of the §6.2 "extended"
                // attribute set — older 608 decoders that don't recognise
                // them treat the bytes as no-ops, so emitting them is safe.

                /**
                 * @brief Encodes a CC1 background attribute code.
                 *
                 * @param color           Background colour.
                 * @param semiTransparent @c true for the spec's
                 *                        "semi-transparent" variant (50%
                 *                        alpha); @c false for opaque.
                 * @param[out] b1         First wire byte (pre-parity).
                 *                        Always @c 0x10 for CC1.
                 * @param[out] b2         Second wire byte (pre-parity).
                 *
                 * Always succeeds.
                 */
                static void encodeBgAttribute(CaptionColor color, bool semiTransparent, uint8_t &b1,
                                              uint8_t &b2);

                /**
                 * @brief Returns @c true when @c (b1, b2) is a CC1
                 *        background-attribute code.
                 */
                static bool isBgAttribute(uint8_t b1, uint8_t b2);

                /**
                 * @brief Decodes a CC1 background-attribute code.
                 *
                 * @return @c true on success; @c false when @c (b1, b2)
                 *         is not a recognised background-attribute code.
                 */
                static bool decodeBgAttribute(uint8_t b1, uint8_t b2, CaptionColor &outColor,
                                              bool &outSemiTransparent);

                /**
                 * @brief Returns @c true when @c (b1, b2) is the
                 *        Background Transparent (BT) code per
                 *        CTA-608-E §6.2 Table 3.
                 *
                 * BT lives in the @c (@ref Cc1ExtAttrB1 = 0x17,
                 * @ref BtB2 = 0x2D) family — distinct from the
                 * @c (0x10, 0x20..0x2F) BG colour family.  BT removes
                 * the background box entirely; the previously-set BG
                 * colour becomes invisible until the next BG attribute
                 * or PAC re-establishes it.
                 *
                 * @param b1  First wire byte (pre-parity, CC1 form).
                 * @param b2  Second wire byte (pre-parity).
                 */
                static bool isBt(uint8_t b1, uint8_t b2);

                /**
                 * @brief Encodes the Background Transparent (BT) code
                 *        (CTA-608-E §6.2 Table 3).
                 *
                 * Produces (@ref Cc1ExtAttrB1, @ref BtB2).  BT carries
                 * no parameters — it's a pure mode toggle that removes
                 * the background box until the next BG attribute or PAC
                 * re-establishes it.  Bytes are pre-parity; callers
                 * stamp odd parity at emit time.
                 */
                static void encodeBackgroundTransparency(uint8_t &b1, uint8_t &b2) {
                        b1 = Cc1ExtAttrB1;
                        b2 = BtB2;
                }

                // -- Tab Offset codes (CTA-608-E §6.2 Table 3) -------------
                //
                // Tab Offset codes nudge the pen position 1, 2, or 3
                // columns to the right of where a PAC just landed it.
                // They're the fine-grained complement to PAC's
                // multiples-of-4 indent slots: PAC indent 8 + Tab
                // Offset T2 places a row at column 10 (8 + 2).  The
                // encoder uses them to honour @ref SubtitleAnchor's
                // horizontal half (Center / Right) when the cue's
                // computed start column isn't already a multiple of 4.

                /// @brief First wire byte for the CC1 Tab Offset code
                ///        family AND the CC1 Foreground / Background
                ///        Attribute code family (BT, FA, FAU).  Both
                ///        families share @c b1 = @c 0x17 per CTA-608-E
                ///        §6.2 Table 3 — the second byte disambiguates
                ///        them.  CC2 form is @c 0x1F (channel-bit OR'd
                ///        in via @ref applyChannel).
                static constexpr uint8_t Cc1ExtAttrB1 = 0x17;

                /// @brief Second-byte values for the three Tab Offset
                ///        codes — T1 advances the pen by 1 column, T2
                ///        by 2, T3 by 3.
                static constexpr uint8_t TabOffsetT1 = 0x21;
                static constexpr uint8_t TabOffsetT2 = 0x22;
                static constexpr uint8_t TabOffsetT3 = 0x23;

                /**
                 * @brief Encodes a Tab Offset code for a 1, 2, or 3
                 *        column shift.
                 *
                 * Always succeeds — values outside @c [1, 3] clamp to
                 * the nearest in-range value.  Bytes are returned
                 * pre-parity (bit 7 zero); callers stamp odd parity
                 * via @ref withOddParity at emit time.
                 *
                 * **Caller responsibility (CEA-608-E Annex B §B.4):**
                 * "Tab Offsets shall not move the cursor beyond the 32nd
                 * column of the current row."  This function does not
                 * know the cursor's current column, so it cannot enforce
                 * the rule itself — the calling pen-position layer
                 * must guarantee that the most recent PAC's indent
                 * plus this offset stays in @c [0, 32].  The encoder
                 * in @ref Cea608Encoder honours this via its row-layout
                 * clamp that caps the final column at 31 before
                 * splitting into @c (pacIndent, residual) pairs.
                 *
                 * @param columns Column shift (1, 2, or 3).
                 * @param[out] b1 First wire byte (always
                 *                @ref Cc1ExtAttrB1 = @c 0x17).
                 * @param[out] b2 Second wire byte (one of
                 *                @ref TabOffsetT1 / @ref TabOffsetT2 /
                 *                @ref TabOffsetT3).
                 */
                static void encodeTabOffset(int columns, uint8_t &b1, uint8_t &b2);

                /**
                 * @brief Returns @c true when @c (b1, b2) is a CC1
                 *        Tab Offset code.
                 */
                static bool isTabOffset(uint8_t b1, uint8_t b2);

                /**
                 * @brief Decodes a Tab Offset code into the column shift.
                 *
                 * @param b1 First wire byte (pre-parity).
                 * @param b2 Second wire byte (pre-parity).
                 * @param[out] outColumns Column shift (1, 2, or 3).
                 * @return @c true on success; @c false when @c (b1, b2)
                 *         is not a Tab Offset code.
                 */
                static bool decodeTabOffset(uint8_t b1, uint8_t b2, int &outColumns);

                // -- Foreground Attribute Codes (FA / FAU, BT) --------------
                //
                // CTA-608-E §6.2 Table 3: optional "extended decoder"
                // codes that supplement the standard 7-colour PAC + mid-
                // row foreground palette with a Black-foreground entry,
                // and supplement the BG attribute set with a "Background
                // Transparent" entry.  All three live in CC1's @c 0x17
                // first-byte family (CC2 form uses @c 0x1F via
                // @ref applyChannel).
                //
                // Per spec, each FA / FAU pair is preceded by a literal
                // space and "incorporates an automatic BS" on extended
                // decoders so the visual effect is a single space
                // tinted with the new colour.  The encoder honours this
                // when emitting Black-foreground spans; the decoder
                // applies the colour to subsequent characters without
                // synthesising the BS (the upstream space + BS pair
                // are a backward-compat hack for standard decoders).
                //
                // Field 2 doesn't remap these — §8.4(a)(b) only covers
                // @c 0x14 ↔ 0x15 and @c 0x1C ↔ 0x1D.  CC3 / CC4 use
                // the same 0x17 / 0x1F first bytes as CC1 / CC2.

                /// @brief Background Transparent (BT) — second byte.
                static constexpr uint8_t BtB2 = 0x2D;
                /// @brief Foreground Black, no underline (FA) — second byte.
                static constexpr uint8_t FaB2 = 0x2E;
                /// @brief Foreground Black, underline (FAU) — second byte.
                static constexpr uint8_t FauB2 = 0x2F;

                /**
                 * @brief Encodes the Foreground Black attribute code
                 *        (CTA-608-E §6.2 Table 3).
                 *
                 * Produces (@ref Cc1ExtAttrB1, @ref FaB2) when @p underline
                 * is false (mnemonic FA) or (@ref Cc1ExtAttrB1, @ref FauB2)
                 * when true (FAU).  Bytes are pre-parity; callers
                 * stamp odd parity at emit time.
                 */
                static void encodeFgBlack(bool underline, uint8_t &b1, uint8_t &b2) {
                        b1 = Cc1ExtAttrB1;
                        b2 = underline ? FauB2 : FaB2;
                }

                /// @brief Returns @c true when @c (b1, b2) is a CC1 FA
                ///        or FAU code (Foreground Black, with or
                ///        without underline).
                static bool isFgBlack(uint8_t b1, uint8_t b2) {
                        return b1 == Cc1ExtAttrB1 && (b2 == FaB2 || b2 == FauB2);
                }

                /// @brief Decodes an FA / FAU code.  Returns @c true
                ///        and sets @p outUnderline on success; @c false
                ///        when @c (b1, b2) is not a Foreground Black
                ///        code.
                static bool decodeFgBlack(uint8_t b1, uint8_t b2, bool &outUnderline) {
                        if (!isFgBlack(b1, b2)) return false;
                        outUnderline = (b2 == FauB2);
                        return true;
                }

                // -- Null pair (no-op filler) -------------------------------

                /// @brief CEA-608 null filler.  Pre-parity @c (0x00, 0x00);
                ///        with odd parity stamped that becomes
                ///        @c (0x80, 0x80) on the wire.  Inserted by the
                ///        encoder on frames where the schedule has
                ///        nothing to send, ignored by the decoder.
                static constexpr uint8_t NullB1 = 0x00;
                static constexpr uint8_t NullB2 = 0x00;

                // -- Doubled-control-code detection -------------------------

                /// @brief @c true when @c (b1, b2) is a control pair (as
                ///        opposed to an informational character pair).
                ///        CEA-608 control pairs have @c b1 in @c 0x10..0x1F;
                ///        informational pairs have @c b1 in @c 0x20..0x7F.
                ///        @c (0x00, 0x00) (the null filler) returns
                ///        @c false.  Inputs are pre-parity.
                static constexpr bool isControlPair(uint8_t b1, uint8_t b2) {
                        (void)b2;
                        return b1 >= 0x10 && b1 <= 0x1F;
                }

                // -- Channel retargeting (CTA-608-E §8.4) -------------------

                /**
                 * @brief Retargets a CC1-shaped pre-parity @c (b1, b2)
                 *        pair onto the @p channel form.
                 *
                 * Encapsulates the §8.4(a)(b) channel-bit OR + field-2
                 * misc-control first-byte remap that's otherwise
                 * scattered across every per-control encoder.  Two
                 * mechanical steps:
                 *
                 *   1. For CC2 / CC4 (second channel of the field),
                 *      OR @c 0x08 into @p b1 when it's in the control
                 *      range @c 0x10..0x1F.  This is the intra-field
                 *      channel selector.
                 *   2. For CC3 / CC4 (field 2), remap the misc-control
                 *      first byte: @c 0x14 → @c 0x15 and @c 0x1C →
                 *      @c 0x1D.  This remap is *gated on the misc-
                 *      control b2 range* (@c 0x20..0x2F) — PACs at row
                 *      14 / 15 also use @c b1 = 0x14 / 0x1C but their
                 *      b2 is in @c [0x40, 0x7F], so they MUST NOT be
                 *      remapped.  Other §6.2 control families (PAC,
                 *      mid-row, BG attribute, Tab Offset, FA/FAU/BT,
                 *      Special / Extended characters) keep the same
                 *      first byte in both fields per spec.
                 *
                 * @p b1 outside the control range is returned
                 * unchanged.  Null filler @c (0x00, 0x00) and
                 * informational character pairs (@p b1 >= 0x20) are
                 * also returned unchanged — the channel selector lives
                 * only on control bytes.
                 *
                 * @param b1      First wire byte (pre-parity, CC1 form).
                 * @param b2      Second wire byte (pre-parity); needed
                 *                only to gate the misc-control remap.
                 * @param channel Target channel.
                 * @return The remapped first byte.
                 */
                static constexpr uint8_t applyChannel(uint8_t b1, uint8_t b2, Channel channel) {
                        if (b1 < 0x10 || b1 > 0x1F) return b1;
                        uint8_t out = b1;
                        if (isSecondChannelOfField(channel)) {
                                out = static_cast<uint8_t>(out | 0x08);
                        }
                        if (isFieldTwo(channel) && b2 >= 0x20 && b2 <= 0x2F) {
                                if (out == 0x14) out = 0x15;
                                else if (out == 0x1C) out = 0x1D;
                        }
                        return out;
                }

                /**
                 * @brief Retargets a CC1-shaped @c (b1, b2) pair onto
                 *        @p channel in place.
                 *
                 * Convenience wrapper around the value-returning
                 * @ref applyChannel — modifies @p b1 directly.  @p b2
                 * is read-only (used only to gate the §8.4(a)(b) misc-
                 * control remap on field 2).
                 */
                static constexpr void applyChannelInPlace(uint8_t &b1, uint8_t b2, Channel channel) {
                        b1 = applyChannel(b1, b2, channel);
                }

                // -- XDS framing predicates (CTA-608-E §9.3) ----------------

                /// @brief @c true when @p b1 is an XDS Class Start or
                ///        Class Continue control byte (@c 0x01..0x0E).
                ///        XDS framing lives in field 2; callers must
                ///        also filter by @c cc_type == 1 before
                ///        consulting this predicate.
                static constexpr bool isXdsControl(uint8_t b1) {
                        return b1 >= 0x01 && b1 <= 0x0E;
                }

                /// @brief @c true when @c (b1, b2) is the XDS
                ///        End / Checksum pair (@p b1 == @c 0x0F).  @p b2
                ///        is the checksum byte; this predicate does not
                ///        validate it.
                static constexpr bool isXdsTerminator(uint8_t b1, uint8_t b2) {
                        (void)b2;
                        return b1 == 0x0F;
                }

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

/**
 * @brief Stateful filter that collapses doubled CEA-608 control pairs.
 * @ingroup proav
 *
 * CEA-608 mandates that every control-code pair (@c b1 in @c 0x10..0x1F)
 * is transmitted twice in immediate succession for error protection
 * (§6.1).  Receivers must recognise the second copy and drop it, so
 * each logical control event fires only once.  This struct encapsulates
 * that rule for external consumers who walk the byte-pair stream
 * outside of @ref Cea608Decoder.
 *
 * Usage:
 *
 * @code
 * Cea608PairDeduper dd;
 * for (auto [b1, b2] : pairs) {
 *     if (!dd.accept(b1, b2)) continue; // skip second copy of a doubled control
 *     // ... process (b1, b2) ...
 * }
 * @endcode
 *
 * The filter tolerates the real-world case where a control pair arrives
 * without its duplicate (encoders sometimes drop the second copy on a
 * busy frame): a *single* control pair still triggers @ref accept
 * returning @c true.  An interleaved informational pair between two
 * identical control pairs also resets the rule, matching §6.1's
 * "immediate" requirement.
 *
 * Reset via @ref reset between independent caption streams (e.g.
 * when switching channels) so a control pair at the new stream's
 * start doesn't get suppressed by stale state.
 */
struct Cea608PairDeduper {
                /// @brief Returns @c true when the caller should process
                ///        @c (b1, b2), @c false when it's the spec-required
                ///        immediate duplicate of the previous control pair
                ///        and should be ignored.  Always @c true for
                ///        informational pairs.
                bool accept(uint8_t b1, uint8_t b2) {
                        if (!Cea608::isControlPair(b1, b2)) {
                                // Informational pair clears the dedup window;
                                // a control pair following an info pair is
                                // always a fresh logical event.
                                _hasPending = false;
                                return true;
                        }
                        if (_hasPending && b1 == _lastB1 && b2 == _lastB2) {
                                // Second copy of the same control pair —
                                // suppress it and clear the window so a
                                // *third* copy (rare, but seen in real
                                // streams) is recognised as a new event.
                                _hasPending = false;
                                return false;
                        }
                        _lastB1 = b1;
                        _lastB2 = b2;
                        _hasPending = true;
                        return true;
                }

                /// @brief Clears the dedup window.  Call when switching
                ///        between independent caption channels so the
                ///        first control pair on the new channel isn't
                ///        suppressed by stale state from the prior one.
                void reset() {
                        _lastB1 = 0;
                        _lastB2 = 0;
                        _hasPending = false;
                }

        private:
                uint8_t _lastB1 = 0;
                uint8_t _lastB2 = 0;
                bool    _hasPending = false;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_PROAV
