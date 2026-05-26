/**
 * @file      cea608decoder.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdint>
#include <promeki/cea608.h>
#include <promeki/cea608decoder.h>
#include <promeki/cea608ext.h>
#include <promeki/cea608xds.h>
#include <promeki/cea708cdp.h>
#include <promeki/color.h>
#include <promeki/enums_subtitle.h>
#include <promeki/framenumber.h>
#include <promeki/logger.h>
#include <promeki/metadata.h>
#include <promeki/rect.h>
#include <promeki/sharedptr.h>
#include <promeki/string.h>
#include <promeki/subtitle.h>
#include <promeki/timestamp.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

        /// @brief Appends @p cp as UTF-8 onto @p out.
        ///
        /// Used by the CEA-608 character path to lift G0 / Special /
        /// Extended bytes into Unicode codepoints — most basic G0
        /// positions are plain ASCII, but ten G0 positions plus the
        /// Special / Extended tables produce non-ASCII codepoints
        /// (U+00E9 é, U+2122 ™, U+266A ♪ …) that must round-trip
        /// through the @ref SubtitleSpan text as proper UTF-8.
        void appendUtf8(String &out, uint32_t cp) {
                if (cp == 0) return;
                char buf[5];
                int  n = 0;
                if (cp <= 0x7F) {
                        buf[0] = static_cast<char>(cp);
                        n = 1;
                } else if (cp <= 0x7FF) {
                        buf[0] = static_cast<char>(0xC0 | (cp >> 6));
                        buf[1] = static_cast<char>(0x80 | (cp & 0x3F));
                        n = 2;
                } else if (cp <= 0xFFFF) {
                        buf[0] = static_cast<char>(0xE0 | (cp >> 12));
                        buf[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                        buf[2] = static_cast<char>(0x80 | (cp & 0x3F));
                        n = 3;
                } else {
                        buf[0] = static_cast<char>(0xF0 | (cp >> 18));
                        buf[1] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
                        buf[2] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                        buf[3] = static_cast<char>(0x80 | (cp & 0x3F));
                        n = 4;
                }
                buf[n] = 0;
                out += String::fromUtf8(buf, static_cast<size_t>(n));
        }

        /// @brief Returns the CDP @c cc_type for the configured
        ///        channel.  Field-1 channels map to cc_type 0;
        ///        field-2 channels map to cc_type 1.
        uint8_t ccTypeForChannel(Cea608Decoder::Channel ch) {
                switch (ch) {
                        case Cea608Decoder::Channel::CC1:
                        case Cea608Decoder::Channel::CC2: return 0;
                        case Cea608Decoder::Channel::CC3:
                        case Cea608Decoder::Channel::CC4: return 1;
                }
                return 0;
        }

        /// @brief Returns @c true when the configured channel is the
        ///        "first" channel of its field (CC1 / CC3).  Within a
        ///        field, the channel bit of the first byte of a
        ///        control code (bit 3 of @c b1 after parity strip)
        ///        distinguishes the two channels: 0 = first (CC1/CC3),
        ///        1 = second (CC2/CC4).
        bool channelIsFirstInField(Cea608Decoder::Channel ch) {
                return ch == Cea608Decoder::Channel::CC1 || ch == Cea608Decoder::Channel::CC3;
        }

        /// @brief Maps a CEA-608 row (1..15) plus the row's first and
        ///        last occupied columns (0..31) to the renderer-side
        ///        @ref SubtitleAnchor.
        ///
        /// Row group → vertical: 1..4 = Top, 5..10 = Middle,
        /// 11..15 = Bottom.
        ///
        /// Horizontal recovery uses the *symmetric gap* between the
        /// row's left edge and its first cell vs. the right edge and
        /// its last cell.  A real broadcast captioner emits the row at:
        ///
        ///   Left   → first col = 0,                     last col = width-1
        ///   Center → first col = (32 - width) / 2,      last col = first + width - 1
        ///   Right  → first col = 32 - width,            last col = 31
        ///
        /// so @c leftGap = @c firstCol and @c rightGap = @c 31-lastCol
        /// encode the placement directly: Left has rightGap > leftGap,
        /// Right has leftGap > rightGap, Center has the two gaps
        /// (nearly) equal.  Using only @c firstCol — as the previous
        /// fixed-threshold version did — silently collapses every
        /// centered cue wider than ~24 chars to Left, because
        /// @c (32 - 25) / 2 = 3 falls below the old col<4 Left
        /// threshold.  The cue width at PAC time isn't carried on the
        /// wire; the cell-grid's last-occupied column substitutes for
        /// it.  Centered cues that exactly fill the row (width 32)
        /// land at @c (0, 31) which is still symmetric so we resolve
        /// them as Center — a tossup with Left, but Center matches
        /// how the encoder placed them.
        SubtitleAnchor rowToAnchor(int row, int firstCol, int lastCol) {
                const bool isTop = (row >= 1 && row <= 4);
                const bool isMid = (row >= 5 && row <= 10);
                const int  leftGap = firstCol;
                const int  rightGap = 31 - lastCol;
                const int  delta = leftGap - rightGap;
                // Center when the two gaps are within 1 cell of each
                // other.  A 1-cell tolerance covers odd cue widths (a
                // 31-cell centered cue lands at first=0, last=30 →
                // delta = 0 - 1 = -1) without claiming Center for
                // genuinely flush-left cues.
                const bool isCenter = (delta >= -1 && delta <= 1);
                if (isCenter) {
                        if (isTop) return SubtitleAnchor::TopCenter;
                        if (isMid) return SubtitleAnchor::MiddleCenter;
                        return SubtitleAnchor::BottomCenter;
                }
                if (leftGap < rightGap) {
                        if (isTop) return SubtitleAnchor::TopLeft;
                        if (isMid) return SubtitleAnchor::MiddleLeft;
                        return SubtitleAnchor::BottomLeft;
                }
                if (isTop) return SubtitleAnchor::TopRight;
                if (isMid) return SubtitleAnchor::MiddleRight;
                return SubtitleAnchor::BottomRight;
        }

        /// @brief Maps a CEA-608 primary palette index back to an
        ///        sRGB @ref Color.
        Color paletteColor(Cea608::CaptionColor c) {
                const Color::List p = Cea608::palette();
                const size_t      idx = static_cast<size_t>(c);
                if (idx >= p.size()) return Color::White;
                return p[idx];
        }

        /// @brief Current wire style; mirrors the encoder-side
        ///        tuple.  The decoder updates this on PAC and mid-
        ///        row receipts.
        struct WireStyle {
                        Cea608::CaptionColor color = Cea608::CaptionColor::White;
                        bool                 italic = false;
                        bool                 underline = false;
                        bool                 hasBg = false;
                        Cea608::CaptionColor bgColor = Cea608::CaptionColor::White;
                        bool                 bgSemiTransparent = false;
                        bool                 bgTransparent = false; ///< Set by BT (0x17, 0x2D).
                        bool                 flash = false;         ///< Set by FON.

                        /// @brief Equality across the full style tuple.
                        ///        Used by the cell-grid → span derivation
                        ///        to group contiguous same-style cells
                        ///        into a single @ref SubtitleSpan.
                        bool sameAs(const WireStyle &o) const {
                                return color == o.color && italic == o.italic
                                       && underline == o.underline && hasBg == o.hasBg
                                       && bgColor == o.bgColor
                                       && bgSemiTransparent == o.bgSemiTransparent
                                       && bgTransparent == o.bgTransparent && flash == o.flash;
                        }
        };

        /// @brief One display cell in the 15×32 receiver grid (CEA-
        ///        608-E §B.5).
        ///
        /// The cell-grid model is the literal representation the
        /// receiver maintains per spec: a 2D array of (codepoint,
        /// attribute-set) entries.  Each character pair landed on
        /// the wire writes one cell; control codes that occupy a
        /// display cell (mid-row, FA / FAU, BG attribute, BT) write
        /// a "separator" cell that carries the new attribute set;
        /// BS / DER / scroll operations clear cells.
        struct Cea608Cell {
                        /// @brief Unicode codepoint at this cell (0 if
                        ///        @ref occupied is false).
                        uint32_t codepoint = 0;
                        /// @brief Attribute set in effect at the cell.
                        WireStyle style;
                        /// @brief @c true when this cell has been
                        ///        written; @c false marks an unused
                        ///        background cell.  Distinguishing
                        ///        occupied from empty is required so
                        ///        the gap between row-start and the
                        ///        first character (PAC's indent column)
                        ///        isn't surfaced as a leading space.
                        bool occupied = false;
                        /// @brief @c true when this cell was written
                        ///        by a control code's display-cell
                        ///        slot (mid-row, FA / FAU, BG, BT) —
                        ///        as opposed to a regular character.
                        ///        The cell-grid → span derivation
                        ///        flushes the current run on every
                        ///        separator cell and emits the
                        ///        separator as its own single-cell
                        ///        span, matching the spec convention
                        ///        that "the control code's cell IS a
                        ///        styled space" while keeping the
                        ///        renderer-side span structure
                        ///        explicit (each styled run still
                        ///        gets its own @ref SubtitleSpan).
                        bool isSeparator = false;
        };

        /// @brief 15-row × 32-column receiver display memory
        ///        (CEA-608-E §B.5).
        ///
        /// Holds either the @e non-displayed memory (pop-on while
        /// the prior cue is on screen) or the @e displayed memory
        /// (pop-on post-EOC, paint-on, roll-up).  Provides cell-
        /// level read/write/clear and helpers for deriving the
        /// emitted @ref SubtitleSpan list / anchor.
        class Cea608CellGrid {
                public:
                        static constexpr int kRows = 15; ///< Rows 1..15 (1-indexed).
                        static constexpr int kCols = 32; ///< Cols 0..31 (0-indexed).

                        Cea608Cell &at(int row, int col) {
                                return cells[clampRow(row) - 1][clampCol(col)];
                        }
                        const Cea608Cell &at(int row, int col) const {
                                return cells[clampRow(row) - 1][clampCol(col)];
                        }

                        /// @brief Drops every cell on every row.
                        void clear() {
                                for (int r = 0; r < kRows; ++r) {
                                        for (int c = 0; c < kCols; ++c) {
                                                cells[r][c] = Cea608Cell();
                                        }
                                }
                        }
                        /// @brief Clears row @p row in its entirety.
                        void clearRow(int row) {
                                if (row < 1 || row > kRows) return;
                                for (int c = 0; c < kCols; ++c) cells[row - 1][c] = Cea608Cell();
                        }
                        /// @brief Clears row @p row from column @p col to
                        ///        the right edge.  Used by @ref doDER.
                        void clearRowFromCol(int row, int col) {
                                if (row < 1 || row > kRows) return;
                                if (col < 0) col = 0;
                                for (int c = col; c < kCols; ++c) cells[row - 1][c] = Cea608Cell();
                        }

                        /// @brief Writes one cell, marking it occupied.
                        void setCell(int row, int col, uint32_t cp, const WireStyle &style,
                                     bool isSeparator = false) {
                                if (row < 1 || row > kRows || col < 0 || col >= kCols) return;
                                Cea608Cell &cell = cells[row - 1][col];
                                cell.codepoint = cp;
                                cell.style = style;
                                cell.occupied = true;
                                cell.isSeparator = isSeparator;
                        }
                        /// @brief Marks a single cell unoccupied (used
                        ///        by @ref doBS).
                        void clearCell(int row, int col) {
                                if (row < 1 || row > kRows || col < 0 || col >= kCols) return;
                                cells[row - 1][col] = Cea608Cell();
                        }
                        /// @brief Returns @c true when no cell anywhere
                        ///        in the grid is occupied.
                        bool isEmpty() const {
                                for (int r = 0; r < kRows; ++r) {
                                        for (int c = 0; c < kCols; ++c) {
                                                if (cells[r][c].occupied) return false;
                                        }
                                }
                                return true;
                        }
                        /// @brief Topmost row that contains at least
                        ///        one occupied cell, or -1 if empty.
                        int firstOccupiedRow() const {
                                for (int r = 0; r < kRows; ++r) {
                                        for (int c = 0; c < kCols; ++c) {
                                                if (cells[r][c].occupied) return r + 1;
                                        }
                                }
                                return -1;
                        }
                        /// @brief Leftmost occupied column on row @p row,
                        ///        or -1 if the row is empty.
                        int rowFirstCol(int row) const {
                                if (row < 1 || row > kRows) return -1;
                                for (int c = 0; c < kCols; ++c) {
                                        if (cells[row - 1][c].occupied) return c;
                                }
                                return -1;
                        }
                        /// @brief Rightmost occupied column on row
                        ///        @p row, or -1 if the row is empty.
                        int rowLastCol(int row) const {
                                if (row < 1 || row > kRows) return -1;
                                for (int c = kCols - 1; c >= 0; --c) {
                                        if (cells[row - 1][c].occupied) return c;
                                }
                                return -1;
                        }

                private:
                        static int clampRow(int r) { return r < 1 ? 1 : (r > kRows ? kRows : r); }
                        static int clampCol(int c) { return c < 0 ? 0 : (c >= kCols ? kCols - 1 : c); }

                        Cea608Cell cells[kRows][kCols];
        };

        /// @brief Builds a @ref SubtitleSpan from @p text + @p style.
        ///        Invalid-colour sentinel is used for "white default"
        ///        so the renderer falls back to its configured
        ///        foreground.
        SubtitleSpan makeSpanFromStyle(const String &text, const WireStyle &style) {
                Color c;
                if (style.color != Cea608::CaptionColor::White) {
                        c = paletteColor(style.color);
                }
                SubtitleSpan s(text, false /* bold not representable */, style.italic, style.underline, c);
                if (style.hasBg) {
                        s.setBackgroundColor(paletteColor(style.bgColor));
                        s.setBackgroundOpacity(style.bgSemiTransparent ? SubtitleOpacity::Translucent
                                                                       : SubtitleOpacity::Solid);
                }
                // BT (Background Transparent) takes priority over the
                // BG colour attribute: the spec says the box is
                // removed, regardless of any previously-set BG colour.
                if (style.bgTransparent) {
                        s.setBackgroundOpacity(SubtitleOpacity::Transparent);
                }
                if (style.flash) {
                        s.setForegroundOpacity(SubtitleOpacity::Flash);
                }
                return s;
        }

        /// @brief Derives the renderer-facing @ref SubtitleSpan list
        ///        from a populated grid.
        ///
        /// Walk the grid top-to-bottom; for each row that contains at
        /// least one occupied cell, walk left-to-right between the
        /// row's first and last occupied columns, grouping contiguous
        /// same-style cells into one span.  Separator cells (the
        /// control-code display cells) are emitted as their own
        /// single-character spans regardless of neighbour style, so
        /// styled-run boundaries remain explicit at the renderer.
        /// Rows are joined with an embedded `"\n"` span so the
        /// renderer breaks lines accordingly.
        SubtitleSpan::List gridToSpans(const Cea608CellGrid &g) {
                SubtitleSpan::List spans;
                bool               firstRow = true;
                for (int r = 1; r <= Cea608CellGrid::kRows; ++r) {
                        const int firstCol = g.rowFirstCol(r);
                        if (firstCol < 0) continue;
                        const int lastCol = g.rowLastCol(r);
                        if (!firstRow) spans.pushToBack(SubtitleSpan(String("\n")));
                        firstRow = false;

                        String    text;
                        WireStyle curStyle;
                        bool      runActive = false;

                        auto flushRun = [&]() {
                                if (!runActive || text.isEmpty()) {
                                        text = String();
                                        runActive = false;
                                        return;
                                }
                                spans.pushToBack(makeSpanFromStyle(text, curStyle));
                                text = String();
                                runActive = false;
                        };

                        for (int c = firstCol; c <= lastCol; ++c) {
                                const Cea608Cell &cell = g.at(r, c);
                                uint32_t          cp = cell.occupied ? cell.codepoint : 0x20;
                                WireStyle         cellStyle = cell.occupied ? cell.style : curStyle;

                                if (cell.occupied && cell.isSeparator) {
                                        // Separator cell — flush the
                                        // current run and emit the
                                        // separator as its own single-
                                        // cell span.
                                        flushRun();
                                        String sep;
                                        appendUtf8(sep, cp);
                                        spans.pushToBack(makeSpanFromStyle(sep, cellStyle));
                                        // Continue with the separator's
                                        // style as the new curStyle
                                        // baseline for the next regular
                                        // run.
                                        curStyle = cellStyle;
                                        runActive = false;
                                        continue;
                                }
                                if (!runActive) {
                                        curStyle = cellStyle;
                                        runActive = true;
                                } else if (!cellStyle.sameAs(curStyle)) {
                                        flushRun();
                                        curStyle = cellStyle;
                                        runActive = true;
                                }
                                appendUtf8(text, cp);
                        }
                        flushRun();
                }
                return spans;
        }

        /// @brief Recovers the cue's anchor from the topmost occupied
        ///        row + that row's leftmost column.
        SubtitleAnchor gridToAnchor(const Cea608CellGrid &g, bool hasPac) {
                if (!hasPac) return SubtitleAnchor::Default;
                const int r = g.firstOccupiedRow();
                if (r < 0) return SubtitleAnchor::Default;
                const int firstCol = g.rowFirstCol(r);
                const int lastCol = g.rowLastCol(r);
                return rowToAnchor(r, firstCol, lastCol);
        }

        /// @brief Concatenated flat-text representation of the grid
        ///        (UTF-8, row breaks as `'\n'`).  Used by the live
        ///        @ref Cea608Decoder::displayedText accessor.
        String gridToFlatText(const Cea608CellGrid &g) {
                String out;
                bool   firstRow = true;
                for (int r = 1; r <= Cea608CellGrid::kRows; ++r) {
                        const int firstCol = g.rowFirstCol(r);
                        if (firstCol < 0) continue;
                        const int lastCol = g.rowLastCol(r);
                        if (!firstRow) appendUtf8(out, '\n');
                        firstRow = false;
                        for (int c = firstCol; c <= lastCol; ++c) {
                                const Cea608Cell &cell = g.at(r, c);
                                appendUtf8(out, cell.occupied ? cell.codepoint : 0x20);
                        }
                }
                return out;
        }

} // namespace

// ============================================================================
// Pimpl
// ============================================================================

struct Cea608DecoderImpl {
                PROMEKI_SHARED_FINAL(Cea608DecoderImpl)

                /// @brief Inferred operating mode from the most recently
                ///        seen mode-establishing control code (@c RCL,
                ///        @c RDC, @c RU2/3/4).  Default @c PopOn so
                ///        streams that omit the mode setup (legacy
                ///        captioners that don't send RCL/RDC explicitly)
                ///        still decode the pop-on byte stream.
                enum class CurrentMode {
                        PopOn   = 0,
                        PaintOn = 1,
                        RollUp  = 2,
                };

                Cea608Decoder::Config cfg;
                CurrentMode           currentMode = CurrentMode::PopOn;
                /// @brief Roll-up window size (2/3/4 rows visible).
                ///        Set on @c RUx; not currently used for cue
                ///        emission but kept for diagnostic and future
                ///        multi-row state.
                int rollUpRows = 2;
                /// @brief Roll-up base row — the row the cursor lands
                ///        on after @c RUx / @c CR.  Default per §C.10
                ///        is row 15; a PAC may move the entire window
                ///        higher (e.g. row 14 / 13 / 12 for §B.5
                ///        "top of bottom region" placements).
                int rollUpBaseRow = 15;

                // ---- Loading state (cell grid + cursor) ----
                //
                // For pop-on this is the spec's "non-displayed memory":
                // characters accumulate here while the previous cue is
                // on screen; the buffer is swapped to displayed on EOC.
                //
                // For paint-on and roll-up the buffer is *also* the live
                // display state — chars commit to it (and so to the
                // displayed view) immediately.  The buffer is emitted
                // as a cue on EDM (paint-on) or CR / next CR /
                // finalize (roll-up).

                /// @brief 15×32 cell grid holding the loading buffer.
                Cea608CellGrid loadingGrid;
                /// @brief Cursor row (1..15) — where the next @ref
                ///        appendChar / @ref insertStyledSeparatorCell
                ///        writes a cell.  Set by PAC; tracked by
                ///        appendChar / BS / DER.
                int cursorRow = 15;
                /// @brief Cursor column (0..31).  Set by PAC + Tab
                ///        Offset; advances on every regular character
                ///        and (after a non-auto-BS) on every
                ///        separator cell.  Clamps at 31 (§C.13
                ///        column-32 overflow rule).
                int cursorCol = 0;
                /// @brief Current wire style — the attribute set the
                ///        next regular or separator cell adopts.  Set
                ///        by PAC + mid-row + BG attribute + BT + FON
                ///        + FA / FAU; reset on RCL / RDC / RUx /
                ///        EOC / ENM.
                WireStyle currentStyle;
                /// @brief @c true once a PAC has been received for
                ///        the loading buffer.  Drives anchor
                ///        recovery (the cue's anchor comes from the
                ///        first occupied row's first occupied col;
                ///        without a PAC, anchor is
                ///        @ref SubtitleAnchor::Default).  Also gates
                ///        @ref insertStyledSeparatorCell — pre-PAC
                ///        control codes don't have a cursor to land
                ///        their display cell at.
                bool loadingHasPac = false;
                /// @brief Cue start timestamp for paint-on (set on
                ///        RDC, refined on first PAC / char) and the
                ///        start of the current row for roll-up.
                TimeStamp loadingStart;

                // ---- Displayed state (pop-on, post-EOC) ----

                /// @brief Grid currently visible (pop-on).
                Cea608CellGrid displayedGrid;
                /// @brief @c true if the cue currently in
                ///        @ref displayedGrid had a PAC (i.e. has a
                ///        recoverable anchor).
                bool displayedHasPac = false;
                /// @brief @ref TimeStamp at which the @ref displayedGrid
                ///        cue became visible (the @c EOC frame).
                TimeStamp displayedStart;
                /// @brief @c true when @ref displayedGrid holds a cue
                ///        that has not yet been emitted (no @c EDM or
                ///        replacing @c EOC has fired).
                bool cueDisplayed = false;
                /// @brief @ref CaptionMode of the cue currently held in
                ///        @ref displayedGrid.  Stamped on the emitted
                ///        @ref Subtitle so a cue promoted from paint-on
                ///        / roll-up (via §C.10 mode switch handling)
                ///        keeps its authoring mode rather than always
                ///        being marked Pop-On.
                CaptionMode displayedCaptionMode = CaptionMode::PopOn;

                /// @brief @ref TimeStamp of the most recent
                ///        @ref pushFrame call.  Used by
                ///        @ref finalize to close any still-displayed
                ///        cue.
                TimeStamp lastFrameTs;

                /// @brief Embedded XDS extractor — every field-2
                ///        byte pair pushed through @ref pushFrame is
                ///        forwarded here in addition to the caption-
                ///        channel processing, so a single decoder
                ///        instance surfaces both the caption cues
                ///        and the XDS program-metadata stream.
                Cea608XdsExtractor xds;

                /// @brief @c true once @ref replaceLastWithCodepoint
                ///        has emitted its one-shot warning about a
                ///        missing placeholder.  Prevents log spam
                ///        when an upstream encoder consistently drops
                ///        the standard-set fallback.
                bool missingPlaceholderWarned = false;

                /// @brief CEA-608-E §C.21 (Regulatory) hysteresis
                ///        counter.  Counts consecutive @ref pushFrame
                ///        calls in which no parity-valid byte arrived
                ///        for the configured channel.  Reaches 45 →
                ///        the displayed cue is auto-erased per spec.
                ///        Reset to 0 on any good-parity byte for the
                ///        channel.  Mostly moot for CDP / SEI paths
                ///        (parity is always good); meaningful on
                ///        analog line-21 inputs.
                int consecutiveBadFrames = 0;
                /// @brief CEA-608-E §C.21 enable-side counter (D6):
                ///        once the display has been auto-disabled (45
                ///        consecutive bad-parity frames), the display
                ///        re-enables only after 12-18 consecutive
                ///        good-parity frames.  We use 15 (the mid-
                ///        range of the spec's tolerance band).  When
                ///        @ref displayEnabled is @c false the decoder
                ///        suppresses @ref displayedText / @ref
                ///        displayedCue output until the counter clears
                ///        the threshold.  The initial state is enabled
                ///        — only an explicit auto-disable transition
                ///        flips it off.
                bool displayEnabled = true;
                int  consecutiveGoodFrames = 0;
                /// @brief Mid-range of §C.21's 12-18 frame enable
                ///        hysteresis (D6).
                static constexpr int kDisplayEnableThreshold = 15;
                /// @brief §C.9 (Preferred) 16-second auto-erase
                ///        timeout (D5): applies to ANY mode's live
                ///        cue, not just pop-on's displayed memory.
                static constexpr int64_t kAutoEraseTimeoutMs = 16000;

                /// @brief Most recently processed control code's
                ///        first byte (post parity strip).  Used to
                ///        suppress the doubled-control-code spec
                ///        duplicate.
                uint8_t lastCtlB1 = 0xFF;
                /// @brief Most recently processed control code's
                ///        second byte (post parity strip).
                uint8_t lastCtlB2 = 0xFF;
                /// @brief When @c false, the next identical
                ///        control-code pair is treated as the spec
                ///        duplicate and skipped.  Reset to @c true on
                ///        a non-control byte pair so a character
                ///        between control codes breaks the
                ///        "consecutive" rule.
                bool dupConsumed = true;

                /// @brief Channel context for character-pair attribution.
                ///
                /// Character pairs (b1 in 0x20..0x7F) carry no channel
                /// information on the wire — per CEA-608-E §3.3 they
                /// belong to the channel of the most recently received
                /// control code (which DID carry a channel-selector bit).
                ///
                /// The decoder uses this flag to skip characters that
                /// belong to a different intra-field channel (CC2 chars
                /// arriving at a CC1-configured decoder, or vice versa).
                /// Without this guard a multi-channel stream
                /// (CC1+CC2 in the same field, T1+T2 alongside captions)
                /// would corrupt the target channel by appending the
                /// peer channel's characters.
                ///
                /// Default @c true so streams that send characters
                /// before any control code (out-of-spec but common in
                /// legacy captioners) still surface their text on the
                /// configured channel.  Cross-field text-mode controls
                /// (TR / RTD on T1-T4) set this to @c false so any
                /// subsequent characters intended for the text channel
                /// don't bleed into the caption stream.
                bool lastControlIsForOurChannel = true;

                /// @brief Cues emitted so far.
                SubtitleList cues;

                /// @brief Maps the decoder's internal mode tracking onto
                ///        the codec-agnostic @ref CaptionMode that
                ///        @ref Subtitle::mode carries.
                CaptionMode currentCaptionMode() const {
                        switch (currentMode) {
                                case CurrentMode::PopOn:   return CaptionMode::PopOn;
                                case CurrentMode::PaintOn: return CaptionMode::PaintOn;
                                case CurrentMode::RollUp:  return CaptionMode::RollUp;
                        }
                        return CaptionMode::Default;
                }

                /// @brief Builds a @ref Subtitle from a populated grid
                ///        + bookkeeping fields.
                Subtitle gridToCue(const Cea608CellGrid &g, const TimeStamp &start,
                                   const TimeStamp &end, bool hasPac, CaptionMode mode) const {
                        SubtitleSpan::List spans = gridToSpans(g);
                        Subtitle s(start, end, spans, gridToAnchor(g, hasPac), Rect2Di32(), String(),
                                   Metadata());
                        s.setMode(mode);
                        return s;
                }

                /// @brief Appends one character byte to the cell grid
                ///        at the cursor.
                ///
                /// Translates the basic-G0 byte into its Unicode
                /// codepoint — most positions are plain ASCII, but
                /// ten remapped positions (0x2A=á / 0x5C=é / 0x5E=í /
                /// 0x5F=ó / 0x60=ú / 0x7B=ç / 0x7C=÷ / 0x7D=Ñ /
                /// 0x7E=ñ / 0x7F=█) decode to the mapped Latin /
                /// arithmetic glyph.  Cursor advances by 1 cell
                /// (clamped at column 31 per §C.13).
                void appendChar(uint8_t c) {
                        if (c < 0x20 || c > 0x7F) return;
                        const uint32_t cp = Cea608Ext::decodeG0(c);
                        loadingGrid.setCell(cursorRow, cursorCol, cp, currentStyle,
                                            /*isSeparator=*/false);
                        if (cursorCol < 31) ++cursorCol;
                        // §C.13 column-32 overflow: when the cursor
                        // is already at col 31, further chars overwrite
                        // that cell (matches the cursor clamp above
                        // and the spec's "no advance past col 32"
                        // requirement).
                }

                /// @brief Replaces the most recently written codepoint
                ///        in the loading grid with @p cp.
                ///
                /// Backs the EIA-608-B "Special / Extended Character"
                /// receiver convention: the encoder emits a best-fit
                /// ASCII placeholder ahead of the doubled control
                /// pair so old decoders show a recognisable fallback;
                /// modern decoders follow the control receipt by
                /// replacing the placeholder with the real glyph.
                ///
                /// If no placeholder cell is found, the wire is out-
                /// of-spec; we tolerate by appending the codepoint
                /// as if it were a fresh character + emit a one-shot
                /// warning the first time it happens so telemetry can
                /// surface the upstream encoder defect.
                void replaceLastWithCodepoint(uint32_t cp) {
                        const int targetCol = cursorCol - 1;
                        if (targetCol < 0
                            || !loadingGrid.at(cursorRow, targetCol).occupied
                            || loadingGrid.at(cursorRow, targetCol).isSeparator) {
                                if (!missingPlaceholderWarned) {
                                        missingPlaceholderWarned = true;
                                        promekiWarn(
                                                "Cea608Decoder: Extended Character (U+%04X) arrived "
                                                "with no preceding placeholder — upstream encoder "
                                                "dropped the standard-set fallback char before the "
                                                "doubled control pair.  Tolerating once; further "
                                                "occurrences will be silently appended.",
                                                static_cast<unsigned>(cp));
                                }
                                // §6.4.2 fallback (D9): "the cursor
                                // moves to the left one column position
                                // (unless the Extended Character is the
                                // first character on a row), erasing
                                // any character which may be in that
                                // location, then displays the Extended
                                // Character."  Step the cursor back by
                                // one before writing — only when the
                                // extended char isn't the first on the
                                // row.
                                if (cursorCol > 0) --cursorCol;
                                loadingGrid.setCell(cursorRow, cursorCol, cp, currentStyle);
                                if (cursorCol < 31) ++cursorCol;
                                return;
                        }
                        // Preserve the placeholder's style (it was the
                        // style in effect when the placeholder was
                        // written — usually the same as currentStyle
                        // but not guaranteed under mid-row reorderings).
                        const WireStyle priorStyle = loadingGrid.at(cursorRow, targetCol).style;
                        loadingGrid.setCell(cursorRow, targetCol, cp, priorStyle);
                }

                /// @brief Resets the loading grid + cursor + style to
                ///        defaults.  Called when starting a new cue
                ///        (RCL / RDC / RUx) or after an emit.
                void resetLoading() {
                        loadingGrid.clear();
                        cursorRow = rollUpBaseRow;
                        cursorCol = 0;
                        currentStyle = WireStyle();
                        loadingHasPac = false;
                }

                /// @brief Commits the currently @ref displayedGrid as
                ///        a finalized @ref Subtitle.
                void emitDisplayed(const TimeStamp &end) {
                        if (!cueDisplayed) return;
                        cues.append(gridToCue(displayedGrid, displayedStart, end, displayedHasPac,
                                              displayedCaptionMode));
                        displayedGrid.clear();
                        displayedHasPac = false;
                        displayedStart = TimeStamp();
                        cueDisplayed = false;
                        displayedCaptionMode = CaptionMode::PopOn;
                }

                /// @brief Emits the current @ref loadingGrid as a cue
                ///        with the given start / end timestamps and
                ///        clears the loading buffer.
                void emitLoading(const TimeStamp &start, const TimeStamp &end) {
                        if (loadingGrid.isEmpty()) {
                                resetLoading();
                                return;
                        }
                        cues.append(gridToCue(loadingGrid, start, end, loadingHasPac,
                                              currentCaptionMode()));
                        resetLoading();
                }

                /// @brief Helper: when leaving paint-on / roll-up via
                ///        a mode-switch control code (RCL / RDC / RUx
                ///        / EOC), promote the live loading grid
                ///        (which is paint-on / roll-up's displayed
                ///        memory in our model) into the canonical
                ///        @ref displayedGrid slot.  The cue's
                ///        lifespan continues until the next EDM /
                ///        EOC / finalize ends it — per §C.10 "any
                ///        displayed captioning shall be unaffected"
                ///        by the mode switch.
                void promoteLoadingToDisplayed(const TimeStamp &ts) {
                        if (loadingGrid.isEmpty()) return;
                        if (cueDisplayed) emitDisplayed(ts);
                        const CaptionMode srcMode = currentCaptionMode();
                        displayedGrid = loadingGrid;
                        displayedHasPac = loadingHasPac;
                        displayedStart = loadingStart;
                        cueDisplayed = !displayedGrid.isEmpty();
                        displayedCaptionMode = srcMode;
                        if (!cueDisplayed) {
                                displayedGrid.clear();
                                displayedHasPac = false;
                                displayedStart = TimeStamp();
                                displayedCaptionMode = CaptionMode::PopOn;
                        }
                }

                void doRCL(const TimeStamp &ts) {
                        // §C.10: "The RCL command should have no effect
                        // except to select pop-on style.  If roll-up or
                        // paint-on style is in effect, any displayed
                        // captioning shall be unaffected."
                        //
                        // D3 spec-conformance fix: per §C.10 / §B.8.3
                        // RCL itself does NOT clear non-displayed
                        // memory when staying in pop-on — the encoder
                        // is expected to follow RCL with ENM if a clean
                        // slate is wanted.  When transitioning from
                        // paint-on or roll-up the loading buffer holds
                        // the live displayed cue (paint-on / roll-up
                        // model), so we DO clear it after promoting
                        // that content to displayed — otherwise the
                        // very next EOC would re-emit it as a pop-on
                        // cue.
                        const bool wasLive = (currentMode == CurrentMode::PaintOn
                                              || currentMode == CurrentMode::RollUp);
                        if (wasLive) {
                                promoteLoadingToDisplayed(ts);
                        }
                        currentMode = CurrentMode::PopOn;
                        rollUpBaseRow = 15;
                        if (wasLive) {
                                // The promoted content now lives in
                                // displayedGrid; reset the pop-on non-
                                // displayed slot to empty.
                                resetLoading();
                        } else {
                                // Pop-on → pop-on: leave any in-flight
                                // non-displayed cue alone (D3).
                                cursorRow = rollUpBaseRow;
                                cursorCol = 0;
                                currentStyle = WireStyle();
                        }
                }

                void doRDC(const TimeStamp &ts) {
                        // §C.10: "The RDC command should have no
                        // effect except to select paint-on style.
                        // If pop-on or roll-up captioning is already
                        // displayed, any displayed captioning shall be
                        // unaffected."
                        //
                        // §C.10 (switching INTO paint-on from roll-up):
                        // the live roll-up content remains on screen —
                        // we promote loading into displayed so the user
                        // doesn't see a phantom blank flash.
                        if (currentMode == CurrentMode::PaintOn
                            || currentMode == CurrentMode::RollUp) {
                                promoteLoadingToDisplayed(ts);
                        }
                        currentMode = CurrentMode::PaintOn;
                        rollUpBaseRow = 15;
                        resetLoading();
                        loadingStart = ts;
                }

                void doRUx(int rows, const TimeStamp &ts) {
                        // §C.10: "The RUx commands should have no
                        // effect except to select roll-up style and
                        // the size of the window.  If pop-on or paint-
                        // on captioning is already present in either
                        // memory, it shall be erased."
                        //
                        // Spec-conformance fix (D2): when switching INTO
                        // roll-up from pop-on or paint-on, erase the
                        // prior displayed cue (emitting it first so its
                        // end timestamp is recorded) and the non-
                        // displayed pop-on memory too.  The existing
                        // promote-loading path is only correct when
                        // switching from roll-up → roll-up (an Nth row
                        // becomes the (N+1)th window's anchor).
                        if (currentMode == CurrentMode::PopOn
                            || currentMode == CurrentMode::PaintOn) {
                                // §C.10 mandates erasing any pop-on /
                                // paint-on caption already in memory.
                                if (cueDisplayed) emitDisplayed(ts);
                                displayedGrid.clear();
                                displayedHasPac = false;
                                displayedStart = TimeStamp();
                                displayedCaptionMode = CaptionMode::PopOn;
                                loadingGrid.clear();
                                loadingHasPac = false;
                        } else if (currentMode == CurrentMode::RollUp) {
                                // Roll-up → roll-up: the prior live
                                // content stays on the screen as it
                                // rolls into the new window.  Promote
                                // it to displayed so any subsequent CR
                                // / next-row composition treats it as
                                // the prior row's visible state.
                                promoteLoadingToDisplayed(ts);
                        }
                        currentMode = CurrentMode::RollUp;
                        rollUpRows = rows;
                        // Default roll-up base row is 15 per §B.5
                        // ("bottom of caption area"); a PAC may shift
                        // the window to a top region (rows 1..4) or
                        // mid region (rows 5..10), in which case the
                        // base row is the PAC's row.
                        rollUpBaseRow = 15;
                        resetLoading();
                        loadingStart = ts;
                }

                void doCR(const TimeStamp &ts) {
                        if (currentMode != CurrentMode::RollUp) return;
                        // Carriage Return: finalize the current row as
                        // a cue (when non-empty) and start a new row.
                        emitLoading(loadingStart, ts);
                        loadingStart = ts;
                }

                void doPac(const Cea608::PacAttr &pac, const TimeStamp &ts) {
                        // PAC positions the cursor + sets style.  In
                        // roll-up, PAC also moves the base row (and
                        // hence the cursor target for subsequent CR).
                        cursorRow = pac.row;
                        cursorCol = pac.indentCol;
                        currentStyle.color = pac.color;
                        currentStyle.italic = pac.italic;
                        currentStyle.underline = pac.underline;
                        // PAC resets row-scoped attrs per §C.14
                        // ("PAC-set attributes do not carry over from
                        // one row to the next").
                        currentStyle.flash = false;
                        currentStyle.bgTransparent = false;
                        loadingHasPac = true;
                        if (currentMode == CurrentMode::RollUp) {
                                rollUpBaseRow = pac.row;
                        }
                        // Paint-on: PAC marks the start of a new cue
                        // (RDC sets the mode, PAC sets the row + start).
                        if (currentMode == CurrentMode::PaintOn && loadingGrid.isEmpty()) {
                                loadingStart = ts;
                        }
                }

                /// @brief Honours a Tab Offset code.  Shifts the
                ///        cursor by 1..3 cells — the receiver-side
                ///        cursor advances by that many positions
                ///        before subsequent characters land.
                ///
                /// Tab Offset is the residual companion to PAC indent
                /// (multiples of 4): together they cover the full
                /// 0..31 column range.
                void doTabOffset(int columns) {
                        cursorCol += columns;
                        if (cursorCol > 31) cursorCol = 31;
                }

                /// @brief Implements §6.2's "the control code occupies
                ///        one cell as a styled space" rule shared by
                ///        mid-row codes, FA / FAU, BG-attribute, and
                ///        BT.  Call AFTER the new style has been
                ///        stamped onto @ref currentStyle.
                ///
                /// §6.2 auto-BS extended-decoder convention: when the
                /// cell immediately left of the cursor holds an
                /// unstyled space (the encoder's preceding-space
                /// scaffold), that space cell is restyled in place
                /// rather than pushing a new cell.  Otherwise the
                /// separator goes at the cursor and the cursor
                /// advances by one cell.
                void insertStyledSeparatorCell() {
                        if (!loadingHasPac) return;
                        // Auto-BS over a prior unstyled space — but
                        // only when that prior cell is itself a
                        // regular space (not already a separator).
                        // Replacing a separator would lose the
                        // earlier style change.
                        if (cursorCol > 0) {
                                const Cea608Cell &prior = loadingGrid.at(cursorRow, cursorCol - 1);
                                if (prior.occupied && prior.codepoint == 0x20
                                    && !prior.isSeparator) {
                                        loadingGrid.setCell(cursorRow, cursorCol - 1, 0x20,
                                                            currentStyle, /*isSeparator=*/true);
                                        return;
                                }
                        }
                        // No auto-BS opportunity — push the separator
                        // at the cursor and advance.
                        loadingGrid.setCell(cursorRow, cursorCol, 0x20, currentStyle,
                                            /*isSeparator=*/true);
                        if (cursorCol < 31) ++cursorCol;
                }

                void doMidRow(Cea608::CaptionColor c, bool italic, bool underline) {
                        // Per §6.2: update style, then emit the styled-
                        // space cell that the MR code's display slot
                        // represents.
                        currentStyle.color = c;
                        currentStyle.italic = italic;
                        currentStyle.underline = underline;
                        insertStyledSeparatorCell();
                }

                /// @brief Apply a CC1 background-attribute (EIA-608-B
                ///        §7.6) receipt: switch the loading style's bg
                ///        slot and emit the styled-space cell.
                void doBgAttribute(Cea608::CaptionColor c, bool semiTransparent) {
                        currentStyle.hasBg = true;
                        currentStyle.bgColor = c;
                        currentStyle.bgSemiTransparent = semiTransparent;
                        // BG attribute supersedes any prior BT — the
                        // box reappears with the new colour.
                        currentStyle.bgTransparent = false;
                        insertStyledSeparatorCell();
                }

                /// @brief Honours the §6.2 Table 3 BT (Background
                ///        Transparent) code (0x17, 0x2D).
                ///
                /// Like the BG attribute, BT consumes one display
                /// cell as a styled space (via the §6.2 auto-BS
                /// over the encoder's preceding space).  After
                /// receipt all subsequent characters in the row
                /// render with no background box.
                void doBt() {
                        currentStyle.bgTransparent = true;
                        // BT supersedes any BG colour attribute set
                        // earlier on the row.
                        currentStyle.hasBg = false;
                        insertStyledSeparatorCell();
                }

                /// @brief Honours the @c FON (Flash On) misc code.
                ///
                /// Per §6.2 (Table 3) FON occupies one display cell as
                /// a styled space — same as the mid-row, FA / FAU, BG-
                /// attribute, and BT codes.  D12 spec-conformance fix:
                /// route through the shared @ref insertStyledSeparatorCell
                /// path so the renderer sees the cell.  The flash flag
                /// persists on subsequent characters in the row and
                /// resets at the next PAC.
                void doFON() {
                        currentStyle.flash = true;
                        insertStyledSeparatorCell();
                }

                void doEOC(const TimeStamp &ts) {
                        // §C.10: "The EOC command should have no effect
                        // except to (1) exchange displayed and non-
                        // displayed memories and (2) force the decoder
                        // into pop-on style."
                        //
                        // §C.11 EOC during roll-up / paint-on (spec
                        // conformance fix D1): "The effect of an EOC
                        // while roll-up captioning style is selected is
                        // to swap the entire roll-up caption (if any)
                        // from the displayed memory to non-displayed
                        // memory."  Practical effect: the screen blanks
                        // (the cue ends here, with a blank cue replacing
                        // it) and the prior roll-up content moves into
                        // non-displayed memory waiting for another EOC
                        // to flip it back into view.
                        if (currentMode == CurrentMode::PaintOn
                            || currentMode == CurrentMode::RollUp) {
                                // End the prior live cue at the EOC
                                // frame — the screen is about to blank.
                                emitLoading(loadingStart, ts);
                                // Whatever was sitting in the live
                                // (loading) grid is now in our caller's
                                // emitted cue list and the grid is
                                // cleared by emitLoading.  In the
                                // §C.11 model, the prior content would
                                // now be in non-displayed memory; the
                                // displayed memory was the same live
                                // grid that we just emitted, so the
                                // screen is blank.  Force pop-on; the
                                // next captioner action lands in the
                                // freshly-reset pop-on loading buffer.
                                if (cueDisplayed) {
                                        // Defensive: in the unusual case
                                        // a prior pop-on cue is still
                                        // displayed when we re-enter
                                        // here (shouldn't happen given
                                        // the mode invariants), close
                                        // it at the EOC frame.
                                        emitDisplayed(ts);
                                }
                                currentMode = CurrentMode::PopOn;
                                rollUpBaseRow = 15;
                                cursorRow = rollUpBaseRow;
                                cursorCol = 0;
                                currentStyle = WireStyle();
                                return;
                        }
                        // Standard pop-on EOC: swap displayed ↔
                        // non-displayed.  The previously-displayed
                        // cue's lifespan ends here; the prepared cue
                        // becomes visible.
                        if (cueDisplayed) emitDisplayed(ts);

                        displayedGrid = loadingGrid;
                        displayedHasPac = loadingHasPac;
                        displayedStart = ts;
                        cueDisplayed = !displayedGrid.isEmpty();
                        displayedCaptionMode = CaptionMode::PopOn;
                        if (!cueDisplayed) {
                                displayedGrid.clear();
                                displayedHasPac = false;
                                displayedStart = TimeStamp();
                                displayedCaptionMode = CaptionMode::PopOn;
                        }

                        loadingGrid.clear();
                        loadingHasPac = false;
                        cursorRow = rollUpBaseRow;
                        cursorCol = 0;
                        currentStyle = WireStyle();
                }

                void doEDM(const TimeStamp &ts) {
                        // Pop-on: finalize the currently-displayed cue.
                        // Paint-on: finalize the loading buffer (which
                        //   was the live cue).
                        // Roll-up: finalize the current row.
                        if (currentMode == CurrentMode::PaintOn
                            || currentMode == CurrentMode::RollUp) {
                                emitLoading(loadingStart, ts);
                                loadingStart = ts;
                                return;
                        }
                        if (cueDisplayed) emitDisplayed(ts);
                }

                void doENM() {
                        // Erase Non-displayed Memory.  Only meaningful
                        // in pop-on, where the loading buffer is the
                        // non-displayed memory.  In paint-on / roll-up
                        // the loading buffer is the live displayed cue
                        // (chars commit immediately) — ENM in those
                        // modes is a no-op per spec.
                        if (currentMode != CurrentMode::PopOn) return;
                        loadingGrid.clear();
                        loadingHasPac = false;
                        cursorRow = rollUpBaseRow;
                        cursorCol = 0;
                        currentStyle = WireStyle();
                }

                /// @brief Honours the @c BS (Backspace) misc code per
                ///        §B.12 / §C.13.
                ///
                /// Per spec, BS "shall move the cursor one column to
                /// the left, erasing the character or Mid-Row Code
                /// occupying that location."  Two normative edge
                /// cases:
                ///
                ///  - §C.13: BS received when the cursor is in
                ///    Column 1 (the leftmost absolute column) shall
                ///    be ignored.
                ///  - §B.12: BS must not skip backwards over a PAC.
                ///    In our model PACs reposition the cursor at
                ///    receipt and don't occupy a cell; the cell
                ///    grid's per-row independence guarantees BS at
                ///    column 1 of a row cannot erase a cell on the
                ///    previous row.
                void doBS() {
                        // §C.13 Column-1 rule: in our 0-indexed model
                        // Column 1 == @c cursorCol 0.
                        if (cursorCol <= 0) return;
                        // §C.13 column-32 rule (D4): "A BS received
                        // either before or after displaying a character
                        // in Column 32 shall move the cursor to Column
                        // 31 and erase the character there."  In our
                        // model column 32 == cursorCol 31 (post-clamp);
                        // the appendChar / separator-cell paths clamp
                        // cursorCol at 31 after writing, so a BS issued
                        // while the cell at column 31 (cursorCol 31) is
                        // occupied must erase that cell in place rather
                        // than stepping back to column 30 and erasing
                        // what's there.
                        if (cursorCol == 31 && loadingGrid.at(cursorRow, 31).occupied) {
                                loadingGrid.clearCell(cursorRow, 31);
                                return;
                        }
                        --cursorCol;
                        loadingGrid.clearCell(cursorRow, cursorCol);
                }

                /// @brief Honours the @c DER (Delete to End of Row)
                ///        misc code per §B.3 / §C.5.
                ///
                /// Per spec, DER deletes "from (and including) the
                /// current cell to the end of the row".  The cell-
                /// grid model expresses this directly via
                /// @ref Cea608CellGrid::clearRowFromCol.
                void doDER() {
                        loadingGrid.clearRowFromCol(cursorRow, cursorCol);
                }

                /// @brief Processes one parity-stripped byte pair.
                void process(uint8_t b1, uint8_t b2, const TimeStamp &ts) {
                        // Null pair: no-op.
                        if (b1 == 0x00 && b2 == 0x00) return;

                        // -- Field-2 misc-control remap (CEA-608-E §8.4) --
                        //
                        // Spec §8.4(a)(b): in field 2, the non-printing
                        // first byte of the miscellaneous control-code
                        // pairs that fall in @c (0x14, 0x20..0x2F) in
                        // field 1 is replaced with @c (0x15, ...) when
                        // used in field 2; same for @c (0x1C, ...) →
                        // @c (0x1D, ...).
                        if (ccTypeForChannel(cfg.channel) == 1) {
                                if (b2 >= 0x20 && b2 <= 0x2F) {
                                        if (b1 == 0x15) b1 = 0x14;
                                        else if (b1 == 0x1D) b1 = 0x1C;
                                }
                        }

                        // Control code (first byte in 0x10..0x1F).
                        if (b1 >= 0x10 && b1 <= 0x1F) {
                                // Channel-bit filter: bit 3 of b1
                                // selects channel within the field.
                                const bool isFirstInField = (b1 & 0x08) == 0;
                                const bool matchesChannel =
                                        (isFirstInField == channelIsFirstInField(cfg.channel));

                                // Doubled-control-code spec rule:
                                // identical consecutive control pairs
                                // collapse to one.  A character pair
                                // between resets the rule.  Check
                                // BEFORE touching @ref
                                // lastControlIsForOurChannel — the
                                // duplicate is not a fresh control
                                // event, so the prior control's
                                // channel-attribution decision must
                                // persist (otherwise a TR / RTD pair
                                // would forget its text-mode flag the
                                // moment the duplicate arrived).
                                if (!dupConsumed && b1 == lastCtlB1 && b2 == lastCtlB2) {
                                        dupConsumed = true;
                                        return;
                                }

                                // Update character-attribution context
                                // EVEN for non-matching controls — per
                                // §3.3 character pairs belong to the
                                // most recent control's channel, so a
                                // peer-channel control means subsequent
                                // chars are for the peer (and we should
                                // skip them).
                                lastControlIsForOurChannel = matchesChannel;
                                if (!matchesChannel) {
                                        // Record this peer-channel
                                        // control as the most-recent
                                        // one so its duplicate also
                                        // collapses correctly.
                                        lastCtlB1 = b1;
                                        lastCtlB2 = b2;
                                        dupConsumed = false;
                                        return;
                                }

                                const uint8_t b1c = static_cast<uint8_t>(b1 & 0xF7);

                                if (Cea608::isPac(b1c, b2)) {
                                        Cea608::PacAttr pac;
                                        if (Cea608::decodePac(b1c, b2, pac)) doPac(pac, ts);
                                } else if (Cea608::isMidRow(b1c, b2)) {
                                        Cea608::CaptionColor c;
                                        bool                 it = false, ul = false;
                                        if (Cea608::decodeMidRow(b1c, b2, c, it, ul))
                                                doMidRow(c, it, ul);
                                } else if (Cea608::isTabOffset(b1c, b2)) {
                                        int columns = 0;
                                        if (Cea608::decodeTabOffset(b1c, b2, columns))
                                                doTabOffset(columns);
                                } else if (Cea608::isBt(b1c, b2)) {
                                        doBt();
                                } else if (Cea608::isFgBlack(b1c, b2)) {
                                        bool ul = false;
                                        if (Cea608::decodeFgBlack(b1c, b2, ul)) {
                                                doMidRow(Cea608::CaptionColor::Black, false, ul);
                                        }
                                } else if (Cea608::isBgAttribute(b1c, b2)) {
                                        Cea608::CaptionColor c;
                                        bool                 semi = false;
                                        if (Cea608::decodeBgAttribute(b1c, b2, c, semi))
                                                doBgAttribute(c, semi);
                                } else if (b1c == 0x14) {
                                        switch (b2) {
                                                case Cea608::MiscRCL: doRCL(ts); break;
                                                case Cea608::MiscRDC: doRDC(ts); break;
                                                case Cea608::MiscRU2: doRUx(2, ts); break;
                                                case Cea608::MiscRU3: doRUx(3, ts); break;
                                                case Cea608::MiscRU4: doRUx(4, ts); break;
                                                case Cea608::MiscCR:  doCR(ts); break;
                                                case Cea608::MiscEDM: doEDM(ts); break;
                                                case Cea608::MiscENM: doENM(); break;
                                                case Cea608::MiscEOC: doEOC(ts); break;
                                                case Cea608::MiscBS:  doBS(); break;
                                                case Cea608::MiscDER: doDER(); break;
                                                case Cea608::MiscFON: doFON(); break;
                                                case 0x2A: // TR — Text Restart
                                                case 0x2B: // RTD — Resume Text Display
                                                        // Text-channel mode-establishing
                                                        // codes (§7).  Caption decoders
                                                        // don't model text mode, but
                                                        // they MUST drop subsequent
                                                        // characters that belong to
                                                        // the text channel sharing
                                                        // our byte family — TR/RTD
                                                        // mean "the channel that
                                                        // shares my CC1 / CC2 / CC3 /
                                                        // CC4 byte slot is now in
                                                        // text mode (T1/T2/T3/T4),
                                                        // so subsequent character
                                                        // pairs are NOT mine."
                                                        // Marking the context flag
                                                        // false achieves that without
                                                        // implementing a full Text
                                                        // Mode subsystem.
                                                        lastControlIsForOurChannel = false;
                                                        break;
                                                default:
                                                        // Other unmodelled 0x14 misc
                                                        // codes (reserved / vendor) —
                                                        // ignored.
                                                        break;
                                        }
                                } else if (b1c == 0x11 && b2 >= 0x30 && b2 <= 0x3F) {
                                        // Special Character (16 glyphs).
                                        const uint32_t cp = Cea608Ext::decodeSpecial(b2);
                                        if (cp != Cea608Ext::NoCodepoint) {
                                                replaceLastWithCodepoint(cp);
                                        }
                                } else if (b1c == 0x12 && b2 >= 0x20 && b2 <= 0x3F) {
                                        // Extended Spanish / Misc.
                                        const uint32_t cp = Cea608Ext::decodeExtSpanish(b2);
                                        if (cp != Cea608Ext::NoCodepoint) {
                                                replaceLastWithCodepoint(cp);
                                        }
                                } else if (b1c == 0x13 && b2 >= 0x20 && b2 <= 0x3F) {
                                        // Extended Portuguese / German.
                                        const uint32_t cp = Cea608Ext::decodeExtPortugueseGerman(b2);
                                        if (cp != Cea608Ext::NoCodepoint) {
                                                replaceLastWithCodepoint(cp);
                                        }
                                }

                                lastCtlB1 = b1;
                                lastCtlB2 = b2;
                                dupConsumed = false;
                                return;
                        }

                        // Character pair (first byte >= 0x20).  Skip
                        // when the most recent control was for a
                        // peer channel — those characters belong to
                        // the peer, not us.
                        if (!lastControlIsForOurChannel) {
                                dupConsumed = true;
                                return;
                        }
                        appendChar(b1);
                        if (b2 >= 0x20) appendChar(b2);
                        dupConsumed = true;
                }
};

// ============================================================================
// Cea608Decoder
// ============================================================================

Cea608Decoder::Cea608Decoder() : _d(SharedPtr<Cea608DecoderImpl>::create()) {}

Cea608Decoder::Cea608Decoder(Config cfg) : _d(SharedPtr<Cea608DecoderImpl>::create()) {
        _d.modify()->cfg = cfg;
}

Cea608Decoder::~Cea608Decoder() = default;

const Cea608Decoder::Config &Cea608Decoder::config() const { return _d->cfg; }

void Cea608Decoder::reset() {
        auto *d = _d.modify();
        d->currentMode = Cea608DecoderImpl::CurrentMode::PopOn;
        d->rollUpRows = 2;
        d->rollUpBaseRow = 15;
        d->loadingGrid.clear();
        d->cursorRow = 15;
        d->cursorCol = 0;
        d->currentStyle = WireStyle();
        d->loadingHasPac = false;
        d->loadingStart = TimeStamp();
        d->displayedGrid.clear();
        d->displayedHasPac = false;
        d->displayedStart = TimeStamp();
        d->cueDisplayed = false;
        d->displayedCaptionMode = CaptionMode::PopOn;
        d->lastFrameTs = TimeStamp();
        d->lastCtlB1 = 0xFF;
        d->lastCtlB2 = 0xFF;
        d->dupConsumed = true;
        d->lastControlIsForOurChannel = true;
        d->cues = SubtitleList();
        d->xds.reset();
        d->missingPlaceholderWarned = false;
        d->consecutiveBadFrames = 0;
        d->consecutiveGoodFrames = 0;
        d->displayEnabled = true;
}

namespace {

        /// @brief Overlays @p over onto @p under: cells occupied in
        ///        @p over take precedence; empty cells fall through
        ///        to @p under.  Used by paint-on / roll-up live state
        ///        to honour §C.10 "any displayed captioning shall be
        ///        unaffected" while the live loading grid paints on
        ///        top of the prior cue (D7).
        Cea608CellGrid overlayGrids(const Cea608CellGrid &under, const Cea608CellGrid &over) {
                Cea608CellGrid out = under;
                for (int r = 1; r <= Cea608CellGrid::kRows; ++r) {
                        for (int c = 0; c < Cea608CellGrid::kCols; ++c) {
                                const Cea608Cell &ov = over.at(r, c);
                                if (ov.occupied) out.at(r, c) = ov;
                        }
                }
                return out;
        }

} // namespace

String Cea608Decoder::displayedText() const {
        const Cea608DecoderImpl *d = _d.operator->();
        // §C.21 enable-side hysteresis (D6): suppress output while
        // the display is auto-disabled and re-acquiring signal.
        if (!d->displayEnabled) return String();
        if (d->currentMode == Cea608DecoderImpl::CurrentMode::PaintOn
            || d->currentMode == Cea608DecoderImpl::CurrentMode::RollUp) {
                // §C.10 (D7): a paint-on / roll-up live cue overlays
                // the prior displayed cue — any displayed captioning
                // is unaffected by entering paint-on / roll-up.
                if (d->cueDisplayed && !d->displayedGrid.isEmpty()) {
                        const Cea608CellGrid merged
                                = overlayGrids(d->displayedGrid, d->loadingGrid);
                        return gridToFlatText(merged);
                }
                return gridToFlatText(d->loadingGrid);
        }
        if (!d->cueDisplayed) return String();
        return gridToFlatText(d->displayedGrid);
}

Subtitle Cea608Decoder::displayedCue() const {
        const Cea608DecoderImpl *d = _d.operator->();
        // §C.21 enable-side hysteresis (D6).
        if (!d->displayEnabled) return Subtitle();
        if (d->currentMode == Cea608DecoderImpl::CurrentMode::PaintOn
            || d->currentMode == Cea608DecoderImpl::CurrentMode::RollUp) {
                // §C.10 (D7): overlay live loading on top of the prior
                // displayed cue.
                if (d->cueDisplayed && !d->displayedGrid.isEmpty()) {
                        const Cea608CellGrid merged
                                = overlayGrids(d->displayedGrid, d->loadingGrid);
                        if (merged.isEmpty()) return Subtitle();
                        const TimeStamp start
                                = !d->loadingGrid.isEmpty() ? d->loadingStart : d->displayedStart;
                        const bool hasPac = d->loadingHasPac || d->displayedHasPac;
                        return d->gridToCue(merged, start, d->lastFrameTs, hasPac,
                                            d->currentCaptionMode());
                }
                if (d->loadingGrid.isEmpty()) return Subtitle();
                return d->gridToCue(d->loadingGrid, d->loadingStart, d->lastFrameTs,
                                    d->loadingHasPac, d->currentCaptionMode());
        }
        if (!d->cueDisplayed || d->displayedGrid.isEmpty()) return Subtitle();
        return d->gridToCue(d->displayedGrid, d->displayedStart, d->lastFrameTs,
                            d->displayedHasPac, d->displayedCaptionMode);
}

void Cea608Decoder::pushFrame(FrameNumber /*frame*/, TimeStamp ts, const Cea708Cdp::CcDataList &data) {
        auto         *d = _d.modify();
        const uint8_t wantCcType = ccTypeForChannel(d->cfg.channel);
        // CEA-608-E §C.9 (Preferred): a decoder should auto-erase a
        // live cue after 16 seconds with no refresh.  Spec-conformance
        // fix D5: apply to ALL modes (pop-on displayed memory, paint-on
        // and roll-up live loading memory), not just pop-on.
        if (ts.isValid()) {
                if (d->cueDisplayed && d->displayedStart.isValid()) {
                        const int64_t elapsedMs = (ts - d->displayedStart).milliseconds();
                        if (elapsedMs >= Cea608DecoderImpl::kAutoEraseTimeoutMs) {
                                d->emitDisplayed(ts);
                        }
                }
                if ((d->currentMode == Cea608DecoderImpl::CurrentMode::PaintOn
                     || d->currentMode == Cea608DecoderImpl::CurrentMode::RollUp)
                    && !d->loadingGrid.isEmpty() && d->loadingStart.isValid()) {
                        const int64_t elapsedMs = (ts - d->loadingStart).milliseconds();
                        if (elapsedMs >= Cea608DecoderImpl::kAutoEraseTimeoutMs) {
                                d->emitLoading(d->loadingStart, ts);
                                d->loadingStart = ts;
                        }
                }
        }
        d->lastFrameTs = ts;
        bool frameHadGoodChannelByte = false;
        for (size_t i = 0; i < data.size(); ++i) {
                const Cea708Cdp::CcData &t = data[i];
                if (!t.valid) continue;
                if (t.type == wantCcType) {
                        if (Cea608::checkOddParity(t.b1) && Cea608::checkOddParity(t.b2)) {
                                frameHadGoodChannelByte = true;
                                d->process(Cea608::stripParity(t.b1), Cea608::stripParity(t.b2),
                                           ts);
                        }
                }
                if (t.type == 1) {
                        Cea708Cdp::CcDataList one;
                        one.pushToBack(t);
                        d->xds.pushFrame(one);
                }
        }
        // CEA-608-E §C.21 (Regulatory) disable-side: 45 consecutive
        // no-data frames auto-erase the display.
        if (frameHadGoodChannelByte) {
                d->consecutiveBadFrames = 0;
                // §C.21 enable-side (D6): once disabled, the display
                // re-enables only after 12-18 consecutive good-parity
                // frames.  Use the mid-range (15).  When already
                // enabled the counter just sticks at / above the
                // threshold.
                if (!d->displayEnabled) {
                        if (d->consecutiveGoodFrames < Cea608DecoderImpl::kDisplayEnableThreshold) {
                                ++d->consecutiveGoodFrames;
                        }
                        if (d->consecutiveGoodFrames >= Cea608DecoderImpl::kDisplayEnableThreshold) {
                                d->displayEnabled = true;
                                d->consecutiveGoodFrames = 0;
                        }
                } else {
                        d->consecutiveGoodFrames = 0;
                }
        } else if (d->consecutiveBadFrames < 45) {
                ++d->consecutiveBadFrames;
                // Any bad frame breaks the enable-side run.
                d->consecutiveGoodFrames = 0;
                if (d->consecutiveBadFrames >= 45) {
                        if (d->currentMode == Cea608DecoderImpl::CurrentMode::PaintOn
                            || d->currentMode == Cea608DecoderImpl::CurrentMode::RollUp) {
                                d->emitLoading(d->loadingStart, ts);
                        } else if (d->cueDisplayed) {
                                d->emitDisplayed(ts);
                        }
                        // §C.21 disable-side: gate further output
                        // until the enable threshold has been met.
                        d->displayEnabled = false;
                }
        } else {
                d->consecutiveGoodFrames = 0;
        }
}

List<Cea608XdsPacket> Cea608Decoder::drainXdsPackets() {
        auto *d = _d.modify();
        return d->xds.drain();
}

size_t Cea608Decoder::xdsPending() const { return _d->xds.pending(); }

uint32_t Cea608Decoder::xdsChecksumFailures() const { return _d->xds.checksumFailures(); }

SubtitleList Cea608Decoder::finalize() {
        auto *d = _d.modify();
        // Close any still-live cue at the last-pushed timestamp.
        if (d->currentMode == Cea608DecoderImpl::CurrentMode::PaintOn
            || d->currentMode == Cea608DecoderImpl::CurrentMode::RollUp) {
                d->emitLoading(d->loadingStart, d->lastFrameTs);
        } else if (d->cueDisplayed) {
                d->emitDisplayed(d->lastFrameTs);
        }
        SubtitleList out = d->cues;
        // Reset for re-use.
        d->currentMode = Cea608DecoderImpl::CurrentMode::PopOn;
        d->rollUpRows = 2;
        d->rollUpBaseRow = 15;
        d->loadingGrid.clear();
        d->cursorRow = 15;
        d->cursorCol = 0;
        d->currentStyle = WireStyle();
        d->loadingHasPac = false;
        d->loadingStart = TimeStamp();
        d->displayedGrid.clear();
        d->displayedHasPac = false;
        d->displayedStart = TimeStamp();
        d->cueDisplayed = false;
        d->displayedCaptionMode = CaptionMode::PopOn;
        d->lastFrameTs = TimeStamp();
        d->lastCtlB1 = 0xFF;
        d->lastCtlB2 = 0xFF;
        d->dupConsumed = true;
        d->lastControlIsForOurChannel = true;
        d->cues = SubtitleList();
        d->consecutiveBadFrames = 0;
        d->consecutiveGoodFrames = 0;
        d->displayEnabled = true;
        // Note: the XDS extractor is NOT reset here — pending /
        // already-extracted XDS packets remain available via
        // @ref drainXdsPackets after finalize.  Callers wanting a
        // clean slate should call @ref reset.
        return out;
}

PROMEKI_NAMESPACE_END
