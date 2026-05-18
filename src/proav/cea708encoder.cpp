/**
 * @file      cea708encoder.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <chrono>
#include <cstdint>
#include <promeki/list.h>
#include <promeki/buffer.h>
#include <promeki/cea708cdp.h>
#include <promeki/cea708encoder.h>
#include <promeki/cea708ext.h>
#include <promeki/cea708service.h>
#include <promeki/cea708windowstate.h>
#include <promeki/error.h>
#include <promeki/framenumber.h>
#include <promeki/framerate.h>
#include <promeki/logger.h>
#include <promeki/map.h>
#include <promeki/sharedptr.h>
#include <promeki/subtitle.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

        int64_t timeStampToMs(const TimeStamp &ts) {
                return ts.milliseconds();
        }

        int64_t timeStampToFrame(const TimeStamp &ts, const FrameRate &fps) {
                if (!fps.isValid()) return 0;
                const int64_t ms = timeStampToMs(ts);
                const int64_t num = static_cast<int64_t>(fps.numerator());
                const int64_t den = static_cast<int64_t>(fps.denominator());
                const int64_t denom = 1000 * den;
                const int64_t numer = ms * num;
                if (numer >= 0) return (numer + denom / 2) / denom;
                return -((-numer + denom / 2) / denom);
        }

        /// @brief Quantises an 8-bit colour channel (0..255) to the
        ///        2-bit wire field 708 SetPenColor uses (0..3).  Simple
        ///        clamp + right-shift; matches the renderer's reverse
        ///        mapping when the decoder reconstructs Color.
        uint8_t channelTo2Bit(uint8_t v) { return static_cast<uint8_t>(v >> 6); }

        /// @brief Packs a @ref SubtitleSpan's per-span style into the
        ///        2-byte SetPenAttributes argument pair.
        ///
        /// Byte 1: text_tag (top nibble, 0 = default) | offset (bits 2-3,
        ///         01 = normal) | pen_size (bits 0-1, 01 = standard).
        /// Byte 2: italic (bit 7) | underline (bit 6) | edge_type (bits 3-5)
        ///         | font_tag (bits 0-2).
        void packSpaArgs(const SubtitleSpan &s, uint8_t &b1, uint8_t &b2) {
                b1 = static_cast<uint8_t>((0 << 4) | (1 << 2) | 1); // default text_tag, normal offset, standard pen size
                const uint8_t italicBit = s.italic() ? 0x80 : 0x00;
                const uint8_t ulBit = s.underline() ? 0x40 : 0x00;
                const uint8_t edge = static_cast<uint8_t>(s.edgeStyle().value() & 0x07);
                const uint8_t font = static_cast<uint8_t>(s.fontFace().value() & 0x07);
                b2 = static_cast<uint8_t>(italicBit | ulBit | (edge << 3) | font);
        }

        /// @brief Packs a @ref SubtitleSpan's per-span colour into the
        ///        3-byte SetPenColor argument triple.
        ///
        /// Byte 1: fg_opacity (top 2 bits) | fg_R (bits 5-4) | fg_G (bits 3-2) | fg_B (bits 1-0).
        /// Byte 2: bg_opacity | bg_R | bg_G | bg_B — same layout.
        /// Byte 3: reserved=00 | edge_R | edge_G | edge_B — same colour layout.
        ///
        /// Returns @c true when any colour or opacity slot differs from
        /// the codec-neutral defaults (white-solid foreground, no bg,
        /// no edge), so callers can skip emitting SPC entirely when
        /// the span is plain.  An invalid (default-constructed) @ref Color
        /// is treated as "white" for the foreground slot — receivers
        /// then paint with the renderer's default — and as "no override"
        /// (encoded as 00 with Transparent opacity) for bg / edge.
        bool packSpcArgs(const SubtitleSpan &s, uint8_t &b1, uint8_t &b2, uint8_t &b3) {
                // Foreground.
                Color fg = s.color();
                if (!fg.isValid()) fg = Color::White;
                const uint8_t fgOp = static_cast<uint8_t>(s.foregroundOpacity().value() & 0x03);
                b1 = static_cast<uint8_t>((fgOp << 6)
                                          | (channelTo2Bit(fg.r8()) << 4)
                                          | (channelTo2Bit(fg.g8()) << 2)
                                          | channelTo2Bit(fg.b8()));
                // Background.
                Color   bg = s.backgroundColor();
                uint8_t bgOp = static_cast<uint8_t>(s.backgroundOpacity().value() & 0x03);
                if (!bg.isValid()) {
                        bg = Color::Black;
                        // Background absent → encode Transparent so a
                        // 708 decoder doesn't paint a stray black bg.
                        // A caller that *wants* opaque black should set
                        // backgroundColor explicitly.
                        bgOp = static_cast<uint8_t>(SubtitleOpacity::Transparent.value() & 0x03);
                }
                b2 = static_cast<uint8_t>((bgOp << 6)
                                          | (channelTo2Bit(bg.r8()) << 4)
                                          | (channelTo2Bit(bg.g8()) << 2)
                                          | channelTo2Bit(bg.b8()));
                // Edge.
                Color edge = s.edgeColor();
                if (!edge.isValid()) edge = Color::Black;
                b3 = static_cast<uint8_t>((channelTo2Bit(edge.r8()) << 4)
                                          | (channelTo2Bit(edge.g8()) << 2)
                                          | channelTo2Bit(edge.b8()));
                // Worth emitting?  Anything off-default counts.
                const bool fgDefault = !s.color().isValid()
                                       && s.foregroundOpacity().value() == SubtitleOpacity::Solid.value();
                const bool bgDefault = !s.backgroundColor().isValid();
                const bool edgeDefault = !s.edgeColor().isValid()
                                         && s.edgeStyle().value() == SubtitleEdgeStyle::None.value();
                return !(fgDefault && bgDefault && edgeDefault);
        }

        /// @brief Detects whether @p cue's spans share a single
        ///        @ref SubtitleSpan::backgroundColor — i.e. the cue
        ///        wants a uniform window-level fill instead of per-cell
        ///        SPC bg.  Returns the shared bg + opacity through the
        ///        out-params and @c true; returns @c false (with
        ///        out-params untouched) when spans disagree, no span
        ///        sets a bg, or any span sets bg to @c Transparent.
        ///
        ///        Used by @ref buildShowCueBytes to decide whether to
        ///        emit a @c SetWindowAttributes (SWA) command setting
        ///        the window @c fill_color.  Per-span SPC bg still
        ///        rides as today — receivers paint SWA fill first,
        ///        then per-cell SPC bg on top.
        bool detectUniformBackground(const Subtitle &cue, Color &outBg, SubtitleOpacity &outOpacity) {
                const SubtitleSpan::List &spans = cue.spans();
                bool                      anyValid = false;
                Color                     bg;
                SubtitleOpacity           op = SubtitleOpacity::Solid;
                for (size_t i = 0; i < spans.size(); ++i) {
                        const SubtitleSpan &s = spans[i];
                        if (s.text() == "\n") continue;
                        const Color           &spanBg = s.backgroundColor();
                        const SubtitleOpacity &spanOp = s.backgroundOpacity();
                        if (!spanBg.isValid()) return false;
                        if (spanOp.value() == SubtitleOpacity::Transparent.value()) return false;
                        if (!anyValid) {
                                bg = spanBg;
                                op = spanOp;
                                anyValid = true;
                                continue;
                        }
                        if (spanBg != bg) return false;
                        if (spanOp.value() != op.value()) return false;
                }
                if (!anyValid) return false;
                outBg = bg;
                outOpacity = op;
                return true;
        }

        /// @brief Packs a @ref Cea708WindowAttr into the 4-byte SWA
        ///        argument tuple per CEA-708-D §8.10.5.10.  Returns
        ///        @c true when the attrs differ from defaults (and
        ///        the encoder should actually emit SWA); otherwise the
        ///        caller can skip the command entirely.
        ///
        ///        Wire layout:
        ///          byte1 = fill_opacity<<6 | fill_R<<4 | fill_G<<2 | fill_B
        ///          byte2 = border_type01<<6 | border_R<<4 | border_G<<2 | border_B
        ///          byte3 = border_type2<<7 | wordwrap<<6 | print_dir<<4
        ///                  | scroll_dir<<2 | justify
        ///          byte4 = effect_speed<<4 | effect_dir<<2 | display_effect
        bool packSwaArgs(const Cea708WindowAttr &a, uint8_t &b1, uint8_t &b2, uint8_t &b3, uint8_t &b4) {
                if (!a.hasAnyAttribute()) return false;
                Color         fill = a.fillColor;
                if (!fill.isValid()) fill = Color::Black;
                const uint8_t fillOp = static_cast<uint8_t>(a.fillColor.isValid()
                                                                    ? (a.fillOpacity.value() & 0x03)
                                                                    : SubtitleOpacity::Transparent.value());
                b1 = static_cast<uint8_t>((fillOp << 6)
                                          | (channelTo2Bit(fill.r8()) << 4)
                                          | (channelTo2Bit(fill.g8()) << 2)
                                          | channelTo2Bit(fill.b8()));
                Color border = a.borderColor;
                if (!border.isValid()) border = Color::Black;
                const uint8_t btLow = static_cast<uint8_t>(a.borderType & 0x03);
                const uint8_t btHi = static_cast<uint8_t>((a.borderType >> 2) & 0x01);
                b2 = static_cast<uint8_t>((btLow << 6)
                                          | (channelTo2Bit(border.r8()) << 4)
                                          | (channelTo2Bit(border.g8()) << 2)
                                          | channelTo2Bit(border.b8()));
                b3 = static_cast<uint8_t>((btHi << 7)
                                          | ((a.wordWrap ? 1u : 0u) << 6)
                                          | ((a.printDirection & 0x03) << 4)
                                          | ((a.scrollDirection & 0x03) << 2)
                                          | (a.justify & 0x03));
                b4 = static_cast<uint8_t>(((a.effectSpeed & 0x0F) << 4)
                                          | ((a.effectDirection & 0x03) << 2)
                                          | (a.displayEffect & 0x03));
                return true;
        }

        /// @brief Maps a @ref SubtitleAnchor enum value (1..9 numpad
        ///        convention used by ASS @c {\anN}, or 0 for Default)
        ///        onto a CEA-708 DefineWindow @c anchor_point (1..9
        ///        reading-order convention).
        ///
        /// SubtitleAnchor uses the numpad/keypad layout where 1=BottomLeft
        /// and 9=TopRight; CEA-708 uses reading order where 1=TopLeft
        /// and 9=BottomRight.  The horizontal axis matches; only the
        /// vertical layer flips.
        uint8_t subtitleAnchorTo708(int value) {
                switch (value) {
                        case 1: return 7; // BottomLeft
                        case 2: return 8; // BottomCenter
                        case 3: return 9; // BottomRight
                        case 4: return 4; // MiddleLeft
                        case 5: return 5; // MiddleCenter
                        case 6: return 6; // MiddleRight
                        case 7: return 1; // TopLeft
                        case 8: return 2; // TopCenter
                        case 9: return 3; // TopRight
                        default: return 8; // Default → BottomCenter (caption convention)
                }
        }

        /// @brief Returns the SMPTE-334 / CEA-708 percent-space
        ///        anchor_v / anchor_h pair that places the window's
        ///        @p anchorPoint corner at a sensible default position
        ///        on the safe-title area.  Percent values are 0..99 for
        ///        vertical and 0..99 for horizontal.
        void anchorPointToWireXY(uint8_t anchorPoint, uint8_t &av, uint8_t &ah) {
                // Vertical: top row gets 5%, middle gets 50%, bottom gets 90%.
                if (anchorPoint <= 3)      av = 5;   // top
                else if (anchorPoint <= 6) av = 50;  // middle
                else                       av = 90;  // bottom
                // Horizontal: left gets 5%, center gets 50%, right gets 90%.
                const uint8_t hMod = static_cast<uint8_t>((anchorPoint - 1) % 3);
                if (hMod == 0)      ah = 5;   // left
                else if (hMod == 1) ah = 50;  // center
                else                ah = 90;  // right
        }

        /// @brief One cell of laid-out cue content: the Unicode
        ///        codepoint to emit plus a pointer to the source span
        ///        (so SPA / SPC commands can ride immediately before
        ///        any character whose owning span differs from the
        ///        wire's current pen state).
        ///
        /// The wire encoding for the codepoint is picked at emission
        /// time by @ref Cea708Ext::encode, which yields anywhere from
        /// 1 (G0 / G1) to 6 (UTF-16 surrogate pair via two P16
        /// sequences) bytes per cell.
        struct LaidOutCell {
                char32_t            cp;   ///< Codepoint (U+0020..U+10FFFF)
                const SubtitleSpan *span; ///< Owning span (style source)
        };
        struct LaidOutRow {
                List<LaidOutCell> cells;
        };

        /// @brief Reads the row layout from a wrapped @ref Subtitle.
        ///        @ref Subtitle::wrapped returns a cue whose spans
        ///        are interleaved with unstyled @c "\n" separators;
        ///        each separator starts a new row.  The cells in
        ///        each row carry their source-span pointer so the
        ///        byte-stream builder can interleave SPA / SPC
        ///        commands at span boundaries.
        ///
        /// Walks each span by Unicode codepoint via @c String::charAt
        /// rather than by raw UTF-8 byte — multi-byte sequences
        /// produce a single cell, so the receiver's grid sees one
        /// cell per visible glyph regardless of the source
        /// encoding's byte cost.
        List<LaidOutRow> layoutCueText(const Subtitle &wrappedCue) {
                List<LaidOutRow>   rows;
                rows.emplaceToBack();
                const SubtitleSpan::List &spans = wrappedCue.spans();
                for (size_t s = 0; s < spans.size(); ++s) {
                        const SubtitleSpan &sp = spans[s];
                        const String       &t = sp.text();
                        if (t == "\n") {
                                rows.emplaceToBack();
                                continue;
                        }
                        const size_t len = t.length();
                        for (size_t i = 0; i < len; ++i) {
                                const char32_t cp = t.charAt(i).codepoint();
                                if (cp == 0x0A /* literal newline inside a span */) {
                                        rows.emplaceToBack();
                                        continue;
                                }
                                if (cp == 0x0D /* CR */ || cp == 0x09 /* Tab */) {
                                        rows.back().cells.pushToBack({U' ', &sp});
                                        continue;
                                }
                                rows.back().cells.pushToBack({cp, &sp});
                        }
                }
                while (rows.size() > 1 && rows.back().cells.isEmpty()) rows.popFromBack();
                if (rows.isEmpty()) rows.emplaceToBack();
                return rows;
        }

        /// @brief Builds the byte stream that defines a window with
        ///        the dimensions / anchor required to display @p cue,
        ///        writes the cue's text (with per-span style commands
        ///        and CR row breaks), and shows the window via DSW.
        ///
        /// The window's @c rowCount is the number of physical lines
        /// needed (hard newlines from the cue text + auto-wrap rows
        /// when a single line exceeds @p windowCols).  The
        /// @c colCount is the longest physical line's length.
        /// Anchor is taken from @ref Subtitle::anchor; default fallback
        /// is @c BottomCenter.
        ///
        /// @p forcedRowCount is honored when > 0 (roll-up uses 3+ rows
        /// so the receiver scrolls); when 0 the encoder picks just
        /// enough rows to hold the cue.
        ///
        /// Out-of-range characters substitute with space (0x20).
        Buffer buildShowCueBytes(const Subtitle &cue, int windowCols, int forcedRowCount = 0) {
                if (windowCols < 1) windowCols = 1;
                if (windowCols > Cea708Window::MaxCols) windowCols = Cea708Window::MaxCols;

                // -- Lay out the cue text into rows ------------------
                //
                // Subtitle::wrapped does the work: explicit '\n'
                // breaks first, then balanced minimax re-flow,
                // honouring @c windowCols and the 15-row maximum.
                // The returned cue's spans are interleaved with
                // "\n" separators so @ref layoutCueText can walk
                // them and recover per-row, per-cell layout with
                // span pointers preserved for SPA / SPC emission.
                const Subtitle           wrappedCue =
                        cue.wrapped(windowCols, Cea708Window::MaxRows);
                List<LaidOutRow>  rows = layoutCueText(wrappedCue);
                if (rows.isEmpty()) rows.emplaceToBack();

                int rowCount = static_cast<int>(rows.size());
                if (forcedRowCount > rowCount) rowCount = forcedRowCount;
                if (rowCount < 1) rowCount = 1;
                if (rowCount > Cea708Window::MaxRows) rowCount = Cea708Window::MaxRows;

                int colCount = 0;
                for (const auto &r : rows) {
                        const int rc = static_cast<int>(r.cells.size());
                        if (rc > colCount) colCount = rc;
                }
                if (colCount < 1) colCount = 1;
                if (colCount > Cea708Window::MaxCols) colCount = Cea708Window::MaxCols;

                // -- DF0 args ----------------------------------------
                const uint8_t anchorPoint =
                        subtitleAnchorTo708(cue.anchor().value());
                uint8_t anchorV = 0;
                uint8_t anchorH = 0;
                anchorPointToWireXY(anchorPoint, anchorV, anchorH);

                // DF0 arg1: invisible (bit 6 = 0), locks (bits 4+5),
                // priority 0.  Real-world captioners author windows
                // hidden, write the text, then DSW to flip visibility
                // atomically — that way a multi-packet / multi-frame
                // show transaction doesn't paint the window
                // incrementally as each chunk lands.
                const uint8_t df0Arg1 = static_cast<uint8_t>(0x30);
                // anchor byte: relative_pos (bit 7) | anchor_v (bits 0..6).
                const uint8_t df0Anchor = static_cast<uint8_t>(0x80 | (anchorV & 0x7F));
                const uint8_t df0AnchorH = anchorH;
                // row_count_wire: 4-bit "rows beyond the first".
                int rowWire = rowCount - 1;
                if (rowWire < 0) rowWire = 0;
                if (rowWire > 15) rowWire = 15;
                const uint8_t df0Rows = static_cast<uint8_t>(((anchorPoint & 0x0F) << 4)
                                                              | (rowWire & 0x0F));
                const uint8_t colWire = static_cast<uint8_t>((colCount - 1) & 0x3F);
                const uint8_t df0Style = 0x00;

                List<uint8_t> bytes;
                bytes.reserve(static_cast<size_t>(7 + colCount * rowCount + rowCount + 2));
                bytes.pushToBack(0x98); // DF0 (window 0)
                bytes.pushToBack(df0Arg1);
                bytes.pushToBack(df0Anchor);
                bytes.pushToBack(df0AnchorH);
                bytes.pushToBack(df0Rows);
                bytes.pushToBack(colWire);
                bytes.pushToBack(df0Style);

                // -- SWA (SetWindowAttributes) ----------------------
                //
                // Emit immediately after DF0 — the receiver applies
                // SWA to the *current* window (the one DF0 just made
                // current).  Today we only auto-derive @c fill_color +
                // @c fill_opacity from the cue's spans when every span
                // shares a backgroundColor; the rest of the SWA
                // payload (border / justify / scroll dir / effect)
                // stays at codec-neutral defaults until a cue-level
                // data model carries that information.
                Cea708WindowAttr swaAttrs;
                Color            uniformBg;
                SubtitleOpacity  uniformBgOp = SubtitleOpacity::Solid;
                if (detectUniformBackground(cue, uniformBg, uniformBgOp)) {
                        swaAttrs.fillColor = uniformBg;
                        swaAttrs.fillOpacity = uniformBgOp;
                }
                uint8_t swaB1 = 0, swaB2 = 0, swaB3 = 0, swaB4 = 0;
                if (packSwaArgs(swaAttrs, swaB1, swaB2, swaB3, swaB4)) {
                        bytes.pushToBack(0x97); // SWA
                        bytes.pushToBack(swaB1);
                        bytes.pushToBack(swaB2);
                        bytes.pushToBack(swaB3);
                        bytes.pushToBack(swaB4);
                }

                // -- Walk rows + cells, emitting style cmds + chars ---
                //
                // CEA-708 pen state is global on the receiver and
                // persists across DefineWindow calls — a styled cue
                // (e.g. magenta @c <font color="#FF00FF">) leaves the
                // wire's last-emitted SetPenAttributes / SetPenColor
                // values in force until the *next* SPA / SPC
                // command lands.  Without an explicit reset every cue
                // would inherit the previous cue's residual pen, so
                // we always emit SPA + SPC with default args right
                // after DF0 to bring the wire to a known state.
                // Subsequent commands then only re-emit when the
                // current cell needs different state.
                SubtitleSpan defaultSpan; // for "revert to default" args
                uint8_t      defSpaB1 = 0, defSpaB2 = 0;
                uint8_t      defSpcB1 = 0, defSpcB2 = 0, defSpcB3 = 0;
                packSpaArgs(defaultSpan, defSpaB1, defSpaB2);
                packSpcArgs(defaultSpan, defSpcB1, defSpcB2, defSpcB3);

                bytes.pushToBack(0x90); // SPA
                bytes.pushToBack(defSpaB1);
                bytes.pushToBack(defSpaB2);
                bytes.pushToBack(0x91); // SPC
                bytes.pushToBack(defSpcB1);
                bytes.pushToBack(defSpcB2);
                bytes.pushToBack(defSpcB3);

                bool    lastSpaValid = true;
                bool    lastSpcValid = true;
                uint8_t lastSpaB1 = defSpaB1, lastSpaB2 = defSpaB2;
                uint8_t lastSpcB1 = defSpcB1, lastSpcB2 = defSpcB2, lastSpcB3 = defSpcB3;

                for (size_t ri = 0; ri < rows.size(); ++ri) {
                        if (ri > 0) bytes.pushToBack(0x0D); // CR — next row
                        const auto &row = rows[ri];
                        for (size_t ci = 0; ci < row.cells.size(); ++ci) {
                                const LaidOutCell &cell = row.cells[ci];
                                if (cell.span != nullptr) {
                                        // SPA: assert pen-attribute state.
                                        uint8_t spaB1 = 0, spaB2 = 0;
                                        packSpaArgs(*cell.span, spaB1, spaB2);
                                        if (!lastSpaValid || spaB1 != lastSpaB1 || spaB2 != lastSpaB2) {
                                                bytes.pushToBack(0x90);
                                                bytes.pushToBack(spaB1);
                                                bytes.pushToBack(spaB2);
                                                lastSpaValid = true;
                                                lastSpaB1 = spaB1;
                                                lastSpaB2 = spaB2;
                                        }
                                        // SPC: assert pen-colour state.
                                        // Always emit if state differs from
                                        // wire; revert to defaults when
                                        // the span carries no override and
                                        // a prior span asserted something.
                                        uint8_t    spcB1 = 0, spcB2 = 0, spcB3 = 0;
                                        const bool spcNeeded =
                                                packSpcArgs(*cell.span, spcB1, spcB2, spcB3);
                                        const uint8_t emitB1 = spcNeeded ? spcB1 : defSpcB1;
                                        const uint8_t emitB2 = spcNeeded ? spcB2 : defSpcB2;
                                        const uint8_t emitB3 = spcNeeded ? spcB3 : defSpcB3;
                                        const bool    needEmit =
                                                spcNeeded
                                                        ? (!lastSpcValid || emitB1 != lastSpcB1
                                                           || emitB2 != lastSpcB2 || emitB3 != lastSpcB3)
                                                        : (lastSpcValid
                                                           && (emitB1 != lastSpcB1 || emitB2 != lastSpcB2
                                                               || emitB3 != lastSpcB3));
                                        if (needEmit) {
                                                bytes.pushToBack(0x91);
                                                bytes.pushToBack(emitB1);
                                                bytes.pushToBack(emitB2);
                                                bytes.pushToBack(emitB3);
                                                lastSpcValid = true;
                                                lastSpcB1 = emitB1;
                                                lastSpcB2 = emitB2;
                                                lastSpcB3 = emitB3;
                                        }
                                }
                                const Cea708Ext::EncodedChar enc =
                                        Cea708Ext::encode(static_cast<uint32_t>(cell.cp));
                                for (uint8_t bi = 0; bi < enc.length; ++bi) {
                                        bytes.pushToBack(enc.bytes[bi]);
                                }
                        }
                }

                bytes.pushToBack(0x89); // DSW
                bytes.pushToBack(0x01); // window 0 bitmap

                Buffer buf(bytes.size());
                buf.setSize(bytes.size());
                if (!bytes.isEmpty()) buf.copyFrom(bytes.data(), bytes.size(), 0);
                return buf;
        }

        /// @brief Builds the byte stream that hides window 0
        ///        (HDW + bitmap).
        Buffer buildHideWindowBytes() {
                uint8_t bytes[2] = {0x8A, 0x01};
                Buffer  buf(2);
                buf.setSize(2);
                buf.copyFrom(bytes, 2, 0);
                return buf;
        }

        /// @brief Returns the number of bytes a CEA-708 command/char
        ///        starting with @p b consumes (the lead byte plus its
        ///        argument bytes).  Used by @ref splitIntoSafeChunks
        ///        to break a service-byte stream at command-aligned
        ///        boundaries.  Unknown / reserved bytes default to a
        ///        1-byte length so the chunker never deadlocks.
        size_t cea708CommandLength(uint8_t b) {
                // G0 / G1 printable characters.
                if (b >= 0x20 && b <= 0x7F) return 1;
                if (b >= 0xA0) return 1;
                // C0 controls (0x00..0x1F).
                if (b == 0x10) return 2; // EXT1 — single extension byte
                if (b == 0x18) return 3; // P16 — 16-bit char (2-byte arg)
                if (b <= 0x1F) return 1;
                // C1 controls (0x80..0x9F).
                if (b <= 0x87) return 1; // CW0..CW7
                if (b <= 0x8C) return 2; // CLW, DSW, HDW, TGW, DLW (1-byte bitmap)
                if (b == 0x8D) return 2; // DLY (1-byte tenths-of-second)
                if (b <= 0x8F) return 1; // DLC, RST
                if (b == 0x90) return 3; // SPA — SetPenAttributes (2 args)
                if (b == 0x91) return 4; // SPC — SetPenColor (3 args)
                if (b == 0x92) return 3; // SPL — SetPenLocation (2 args)
                if (b == 0x97) return 5; // SWA — SetWindowAttributes (4 args)
                return 7;                // DF0..DF7 — DefineWindow (6 args)
        }

        /// @brief Splits a CEA-708 service byte stream into chunks of
        ///        at most @p maxChunkSize bytes, never breaking a
        ///        multi-byte command across a chunk boundary.
        ///
        /// The decoder's @ref Cea708WindowState::processBytes does not
        /// carry partial-command state across calls — every byte that
        /// belongs to a command must be in the same processBytes
        /// invocation as the lead byte.  Since each service block is
        /// processed atomically by the decoder, the encoder must
        /// guarantee chunks land at command boundaries.
        List<Buffer> splitIntoSafeChunks(const Buffer &src, size_t maxChunkSize) {
                List<Buffer> out;
                const size_t total = src.size();
                if (total == 0) return out;
                const auto *p = static_cast<const uint8_t *>(src.data());
                size_t      chunkStart = 0;
                size_t      cursor = 0;
                while (cursor < total) {
                        size_t cmdLen = cea708CommandLength(p[cursor]);
                        // A command longer than the per-chunk limit is a
                        // wire-format error — fall back to truncating it
                        // so progress continues.
                        if (cmdLen > maxChunkSize) cmdLen = maxChunkSize;
                        if (cursor + cmdLen > total) cmdLen = total - cursor;
                        const size_t inFlight = cursor - chunkStart;
                        if (inFlight + cmdLen > maxChunkSize) {
                                // Emitting this command would overflow
                                // the chunk — flush the in-flight bytes
                                // first.
                                Buffer chunk(inFlight);
                                chunk.setSize(inFlight);
                                Error err = chunk.copyFrom(p + chunkStart, inFlight, 0);
                                if (err.isError()) {
                                        promekiWarn("Cea708Encoder::splitIntoSafeChunks: chunk "
                                                    "copy failed: %s",
                                                    err.name().cstr());
                                        return out;
                                }
                                out.pushToBack(std::move(chunk));
                                chunkStart = cursor;
                        }
                        cursor += cmdLen;
                }
                if (cursor > chunkStart) {
                        const size_t left = cursor - chunkStart;
                        Buffer       chunk(left);
                        chunk.setSize(left);
                        Error err = chunk.copyFrom(p + chunkStart, left, 0);
                        if (err.isError()) {
                                promekiWarn("Cea708Encoder::splitIntoSafeChunks: trailing chunk "
                                            "copy failed: %s",
                                            err.name().cstr());
                                return out;
                        }
                        out.pushToBack(std::move(chunk));
                }
                return out;
        }

        /// @brief Wraps @p serviceBytes into a list of single-block
        ///        DTVCC packets (one packet per service-block chunk),
        ///        each carrying up to 31 service-data bytes for
        ///        @p serviceNumber.
        ///
        /// Three layered wire-format limits force this packing shape:
        ///   - @c block_size is 5 bits (max 31 service-data bytes per
        ///     block);
        ///   - @c packet_size_code is 6 bits (this codec interprets
        ///     it directly as the byte count, max packet 63 bytes —
        ///     well within 1 block + 1-byte service header);
        ///   - the carrying CDP's @c cc_count is 5 bits (max 31
        ///     triples per CDP / per video frame), and a single
        ///     31-byte block becomes ~17 triples — comfortably under
        ///     the cap so each packet fits in one CDP.
        ///
        /// The caller is responsible for distributing the returned
        /// packets across consecutive video frames (one packet per
        /// frame) so the per-frame @c cc_count budget is never
        /// exceeded.  @p sequenceNumber is advanced past every
        /// packet emitted.
        List<Cea708Cdp::CcDataList> wrapInDtvccPackets(uint8_t serviceNumber, Buffer serviceBytes,
                                                       uint8_t &sequenceNumber) {
                constexpr size_t kMaxBlockData = 31;
                List<Cea708Cdp::CcDataList> out;
                List<Buffer> chunks = splitIntoSafeChunks(serviceBytes, kMaxBlockData);
                if (chunks.isEmpty()) {
                        // Empty service bytes — emit one empty packet so
                        // the sequence slot still advances.
                        Cea708DtvccPacket pkt;
                        pkt.setSequenceNumber(sequenceNumber);
                        out.pushToBack(pkt.toCcData());
                        sequenceNumber = static_cast<uint8_t>((sequenceNumber + 1) & 0x03);
                        return out;
                }
                for (size_t i = 0; i < chunks.size(); ++i) {
                        Cea708DtvccPacket pkt;
                        pkt.setSequenceNumber(sequenceNumber);
                        pkt.serviceBlocks().pushToBack(
                                Cea708Service(serviceNumber, std::move(chunks[i])));
                        out.pushToBack(pkt.toCcData());
                        sequenceNumber = static_cast<uint8_t>((sequenceNumber + 1) & 0x03);
                }
                return out;
        }

} // namespace

// ============================================================================
// Pimpl
// ============================================================================

struct Cea708EncoderImpl {
                PROMEKI_SHARED_FINAL(Cea708EncoderImpl)

                Cea708Encoder::Config cfg;
                /// @brief Per-frame schedule of cc_data triples.
                ///        Frames not present in the map emit no
                ///        DTVCC payload for the configured service.
                Map<int64_t, Cea708Cdp::CcDataList> schedule;
};

// ============================================================================
// Cea708Encoder
// ============================================================================

Cea708Encoder::Cea708Encoder() : _d(SharedPtr<Cea708EncoderImpl>::create()) {}

Cea708Encoder::Cea708Encoder(Config cfg) : _d(SharedPtr<Cea708EncoderImpl>::create()) {
        _d.modify()->cfg = std::move(cfg);
}

Cea708Encoder::~Cea708Encoder() = default;

const Cea708Encoder::Config &Cea708Encoder::config() const { return _d->cfg; }

FrameRate Cea708Encoder::frameRate() const { return _d->cfg.frameRate; }

void Cea708Encoder::reset() { _d.modify()->schedule.clear(); }

Error Cea708Encoder::setSubtitles(const SubtitleList &subs) {
        auto *d = _d.modify();
        d->schedule.clear();
        if (!d->cfg.frameRate.isValid()) {
                promekiWarn("Cea708Encoder::setSubtitles: invalid frame rate");
                return Error::Invalid;
        }
        if (d->cfg.serviceNumber < 1 || d->cfg.serviceNumber > Cea708Service::MaxServiceNumber) {
                promekiWarn("Cea708Encoder::setSubtitles: serviceNumber %u out of range",
                            static_cast<unsigned>(d->cfg.serviceNumber));
                return Error::Invalid;
        }

        // 708 modes are encoded by how the window-manipulation commands
        // wrap the cue text:
        //   PopOn  (default) — DefineWindow(visible, 1 row) + chars
        //                      + DSW at cue.start; HideWindow at cue.end.
        //   PaintOn          — same show transaction at cue.start, but
        //                      no HideWindow at cue.end — the window
        //                      stays visible until the next cue's
        //                      transaction overwrites or a future cue
        //                      defines a new window.  Mirrors live
        //                      "characters appear as transmitted" intent.
        //   RollUp           — DefineWindow with @ref rollUpRows rows
        //                      so the receiver scrolls instead of
        //                      overwriting the single visible line;
        //                      no HideWindow at cue.end.
        // Cue.mode = Default falls through to PopOn for the encoder's
        // wire shape.  Per-cue mode mixing is fully supported here
        // because each cue's transaction is self-contained.
        constexpr int kRollUpRows = 3;
        // CDP's cc_count field is 5 bits (CEA-708-D / SMPTE 334-2)
        // → at most 31 cc_data triples can ride a single video frame.
        // The encoder's per-packet wire shape (1 service block of <=31
        // bytes per DTVCC packet) produces up to ~17 triples per
        // packet, so two packets in the same frame can overflow this
        // cap; addToSchedule rejects such collisions.
        constexpr size_t kCcCountMax = 31;
        // Helper: append @p packet's triples to whatever's already
        // scheduled at @p frame, or insert the packet as the new entry.
        // Refuses to push past the 31-triple per-frame cc_count cap
        // and returns @c Error::OutOfRange on overflow.
        auto addToSchedule = [&](int64_t frame, Cea708Cdp::CcDataList packet) -> Error {
                auto it = d->schedule.find(frame);
                if (it == d->schedule.end()) {
                        d->schedule.insert(frame, std::move(packet));
                        return Error::Ok;
                }
                Cea708Cdp::CcDataList &existing = it->second;
                if (existing.size() + packet.size() > kCcCountMax) {
                        promekiWarn("Cea708Encoder::setSubtitles: frame %lld would exceed "
                                    "cc_count=31 cap (%zu + %zu triples)",
                                    static_cast<long long>(frame), existing.size(),
                                    packet.size());
                        return Error::OutOfRange;
                }
                for (size_t i = 0; i < packet.size(); ++i) {
                        existing.pushToBack(packet[i]);
                }
                return Error::Ok;
        };
        uint8_t seq = 0;
        for (size_t i = 0; i < subs.size(); ++i) {
                const Subtitle &cue = subs[i];
                const int64_t   startFrame = timeStampToFrame(cue.start(), d->cfg.frameRate);
                const int64_t   endFrame = timeStampToFrame(cue.end(), d->cfg.frameRate);
                if (endFrame <= startFrame) continue;
                const int  cueMode = cue.mode().value();
                const bool isRollUp = cueMode == CaptionMode::RollUp.value();
                const bool isPaintOn = cueMode == CaptionMode::PaintOn.value();
                const int  rowCount = isRollUp ? kRollUpRows : 1;
                Buffer showBytes = buildShowCueBytes(cue, d->cfg.windowCols, rowCount);
                List<Cea708Cdp::CcDataList> showPackets =
                        wrapInDtvccPackets(d->cfg.serviceNumber, showBytes, seq);
                // A cue's show packets occupy one frame each starting at
                // startFrame.  The cue is on screen from
                // [startFrame, endFrame); if the show transaction needs
                // more frames than the cue's duration, it would either
                // bleed into the next cue's schedule slots or (for the
                // last cue in the list) the receiver would still be
                // applying the DefineWindow when the cue's nominal end
                // has passed.  Drop oversized cues with a warning so
                // setSubtitles never silently produces malformed output.
                const int64_t cueFrames = endFrame - startFrame;
                if (static_cast<int64_t>(showPackets.size()) > cueFrames) {
                        promekiWarn("Cea708Encoder::setSubtitles: cue at frame %lld needs "
                                    "%zu show packets but only %lld frames available — "
                                    "cue dropped",
                                    static_cast<long long>(startFrame), showPackets.size(),
                                    static_cast<long long>(cueFrames));
                        continue;
                }
                for (size_t pi = 0; pi < showPackets.size(); ++pi) {
                        Error err = addToSchedule(startFrame + static_cast<int64_t>(pi),
                                                  std::move(showPackets[pi]));
                        if (err.isError()) return err;
                }
                // Pop-on (default) cues commit a HideWindow at end so
                // the cue clears.  Paint-on / roll-up cues skip the
                // hide — the window stays visible until the next cue's
                // DefineWindow overwrites it.
                //
                // When the *next* cue's startFrame coincides with this
                // cue's endFrame, the HideWindow would collide with the
                // next cue's DefineWindow at the same frame.  Both can't
                // fit in a single video frame's cc_count budget, and a
                // DefineWindow already replaces window 0 atomically with
                // its DSW boundary — so elide the redundant HideWindow.
                if (!isRollUp && !isPaintOn) {
                        bool elideHide = false;
                        if (i + 1 < subs.size()) {
                                const int64_t nextStart =
                                        timeStampToFrame(subs[i + 1].start(), d->cfg.frameRate);
                                if (nextStart <= endFrame) elideHide = true;
                        }
                        if (!elideHide) {
                                Buffer                       hideBytes = buildHideWindowBytes();
                                List<Cea708Cdp::CcDataList>  hidePackets =
                                        wrapInDtvccPackets(d->cfg.serviceNumber, hideBytes, seq);
                                for (size_t pi = 0; pi < hidePackets.size(); ++pi) {
                                        Error err = addToSchedule(
                                                endFrame + static_cast<int64_t>(pi),
                                                std::move(hidePackets[pi]));
                                        if (err.isError()) return err;
                                }
                        }
                }
        }
        return Error::Ok;
}

Cea708Cdp::CcDataList Cea708Encoder::nextFrame(FrameNumber frame) const {
        Cea708Cdp::CcDataList out;
        if (!frame.isValid()) return out;
        auto it = _d->schedule.find(frame.value());
        if (it == _d->schedule.end()) return out;
        return it->second;
}

PROMEKI_NAMESPACE_END
