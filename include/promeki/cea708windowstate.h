/**
 * @file      cea708windowstate.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_PROAV
#include <cstdint>
#include <promeki/array.h>
#include <promeki/buffer.h>
#include <promeki/cea708service.h>
#include <promeki/color.h>
#include <promeki/enums.h>
#include <promeki/list.h>
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/subtitle.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Last-asserted pen attributes from @c SetPenAttributes (SPA)
 *        and @c SetPenColor (SPC).
 * @ingroup proav
 *
 * Mirrors the 708 wire shape exactly: italic / underline / edge style
 * / font face from SPA; fg / bg / edge colour + opacity from SPC.
 * @ref Cea708WindowState tracks one "current pen" globally and stamps
 * a copy onto every character cell as it's written, so a recovered
 * cue can reconstruct multi-style spans by grouping consecutive cells
 * with the same pen state.
 */
struct Cea708PenAttr {
                bool              italic = false;
                bool              underline = false;
                SubtitleEdgeStyle edgeStyle = SubtitleEdgeStyle::None;
                SubtitleFontFace  fontFace = SubtitleFontFace::Default;
                Color             foregroundColor;
                Color             backgroundColor;
                Color             edgeColor;
                SubtitleOpacity   foregroundOpacity = SubtitleOpacity::Solid;
                SubtitleOpacity   backgroundOpacity = SubtitleOpacity::Solid;
                SubtitleOpacity   edgeOpacity = SubtitleOpacity::Solid;

                /// @brief @c true when any field differs from the
                ///        codec-neutral default (no style, white-solid
                ///        fg, no bg, no edge).
                bool hasAnyStyle() const {
                        return italic || underline
                                || edgeStyle != SubtitleEdgeStyle::None
                                || fontFace != SubtitleFontFace::Default
                                || foregroundColor.isValid() || backgroundColor.isValid()
                                || edgeColor.isValid()
                                || foregroundOpacity != SubtitleOpacity::Solid
                                || backgroundOpacity != SubtitleOpacity::Solid
                                || edgeOpacity != SubtitleOpacity::Solid;
                }

                bool operator==(const Cea708PenAttr &o) const {
                        return italic == o.italic && underline == o.underline
                               && edgeStyle.value() == o.edgeStyle.value()
                               && fontFace.value() == o.fontFace.value()
                               && foregroundColor == o.foregroundColor
                               && backgroundColor == o.backgroundColor
                               && edgeColor == o.edgeColor
                               && foregroundOpacity.value() == o.foregroundOpacity.value()
                               && backgroundOpacity.value() == o.backgroundOpacity.value()
                               && edgeOpacity.value() == o.edgeOpacity.value();
                }
                bool operator!=(const Cea708PenAttr &o) const { return !(*this == o); }
};

/**
 * @brief Last-asserted window-level attributes from @c SetWindowAttributes
 *        (SWA, 0x97).
 * @ingroup proav
 *
 * Mirrors the CEA-708 §8.10.5.10 wire shape exactly.  Each window has
 * one @ref Cea708WindowAttr that the decoder updates whenever an SWA
 * command lands inside the window's service block; the encoder emits
 * SWA at @c DefineWindow time so the receiver's window state matches
 * the source's intent.
 *
 * @par Wire field summary
 *
 *  - @c fillColor / @c fillOpacity — paints the entire window's
 *    background.  Independent of per-cell SPC background.
 *  - @c borderColor / @c borderType — frames the window with one of
 *    six border styles (None / Raised / Depressed / Uniform /
 *    ShadowLeft / ShadowRight; values 6 and 7 are reserved per spec).
 *  - @c justify — paragraph justification (Left / Right / Center / Full).
 *  - @c printDirection — text flow direction (LtR / RtL / TtB / BtT).
 *  - @c scrollDirection — same enum, applies to the window-level scroll.
 *  - @c wordWrap — whether the receiver auto-wraps long lines.
 *  - @c displayEffect / @c effectDirection / @c effectSpeed — display
 *    transition for window appearance / disappearance.  @c effectSpeed
 *    is in units of 0.5s (0..7.5s).
 */
struct Cea708WindowAttr {
                /// @brief Window fill (background).  Default-constructed
                ///        @c Color means "no override" (SubtitleOpacity::Transparent).
                Color           fillColor;
                SubtitleOpacity fillOpacity = SubtitleOpacity::Solid;

                /// @brief Window border.  Default-constructed @c Color
                ///        means "no override".
                Color borderColor;
                /// @brief Border type — 0=None, 1=Raised, 2=Depressed,
                ///        3=Uniform, 4=ShadowLeft, 5=ShadowRight,
                ///        6/7=Reserved.
                uint8_t borderType = 0;

                /// @brief Justify (0=Left, 1=Right, 2=Center, 3=Full).
                uint8_t justify = 0;
                /// @brief Print direction (0=LtR, 1=RtL, 2=TtB, 3=BtT).
                uint8_t printDirection = 0;
                /// @brief Scroll direction (same enum as printDirection).
                uint8_t scrollDirection = 3; // BtT — caption convention
                /// @brief @c true when receiver word-wraps long lines.
                bool wordWrap = false;
                /// @brief Display effect (0=Snap, 1=Fade, 2=Wipe).
                uint8_t displayEffect = 0;
                /// @brief Effect direction (same enum as printDirection).
                uint8_t effectDirection = 0;
                /// @brief Effect speed in units of 0.5s (0..15 = 0..7.5s).
                uint8_t effectSpeed = 0;

                /// @brief @c true when any field differs from the
                ///        codec-neutral default (no fill, no border,
                ///        Left justify, LtR print, BtT scroll, no
                ///        wrap, snap effect).
                bool hasAnyAttribute() const {
                        return fillColor.isValid()
                               || fillOpacity != SubtitleOpacity::Solid
                               || borderColor.isValid()
                               || borderType != 0
                               || justify != 0
                               || printDirection != 0
                               || scrollDirection != 3
                               || wordWrap
                               || displayEffect != 0
                               || effectDirection != 0
                               || effectSpeed != 0;
                }

                bool operator==(const Cea708WindowAttr &o) const {
                        return fillColor == o.fillColor
                               && fillOpacity.value() == o.fillOpacity.value()
                               && borderColor == o.borderColor && borderType == o.borderType
                               && justify == o.justify && printDirection == o.printDirection
                               && scrollDirection == o.scrollDirection && wordWrap == o.wordWrap
                               && displayEffect == o.displayEffect && effectDirection == o.effectDirection
                               && effectSpeed == o.effectSpeed;
                }
                bool operator!=(const Cea708WindowAttr &o) const { return !(*this == o); }
};

/**
 * @brief One character cell in a @ref Cea708Window grid.
 * @ingroup proav
 *
 * Stores a UTF-32 codepoint plus the pen attributes that were
 * "current" when the cell was written.  Empty cells (codepoint == 0)
 * keep a default-constructed pen so the recovered span reconstruction
 * can group consecutive runs by matching pen state.
 */
struct Cea708Cell {
                uint32_t      codepoint = 0;
                Cea708PenAttr pen;

                bool operator==(const Cea708Cell &o) const {
                        return codepoint == o.codepoint && pen == o.pen;
                }
                bool operator!=(const Cea708Cell &o) const { return !(*this == o); }
};

/**
 * @brief One CEA-708 DTVCC caption window.
 * @ingroup proav
 *
 * CEA-708 carries captions as up to eight independent **windows**
 * per caption service.  Each window has its own position, size,
 * character grid, and visibility flag.  The active service block
 * stream selects "the current window" via @c CW0..CW7 / @c DF0..DF7
 * commands, then writes characters into the current window's grid.
 *
 * @par State tracked here
 *
 *  - @c defined / @c visible flags.
 *  - @c anchorPoint (1..9) + @c anchorV / @c anchorH coordinates +
 *    @c relativePos flag (per the DefineWindow command).
 *  - @c rowCount × @c colCount character grid.
 *  - @c penRow / @c penCol cursor position.
 *  - The character grid itself — @ref Cea708Cell per cell, carrying
 *    a UTF-32 codepoint plus the pen attributes that were current
 *    when the character was written.  Empty cells store codepoint 0
 *    with a default-constructed pen.
 *
 * @par Storage and copy semantics
 *
 * Plain value type.  The character grid is a value-type
 * @ref List<List<Cea708Cell>>; copies deep-copy the grid.  Grids
 * are typically tiny (≤ 15 × 32 cells), so the cost is
 * negligible.
 *
 * @see Cea708Service, Cea708WindowState, Cea708Decoder, Cea708PenAttr
 */
struct Cea708Window {
                /// @brief Maximum row count a single window can carry
                ///        (CEA-708 §8.4.6 caps at 15).
                static constexpr int MaxRows = 15;
                /// @brief Maximum column count a single window can carry
                ///        (CEA-708 caps at 42).
                static constexpr int MaxCols = 42;

                bool visible = false;       ///< Currently on-screen.
                bool defined = false;       ///< @c DefineWindow has been issued.
                int  priority = 0;          ///< Z-order priority (0=top, 7=bottom).
                int  anchorPoint = 1;       ///< Anchor numpad position (1..9).
                int  anchorV = 0;           ///< Anchor vertical coordinate.
                int  anchorH = 0;           ///< Anchor horizontal coordinate.
                bool relativePos = true;    ///< @c true → anchorV/H are percentage; @c false → absolute cell.
                int  rowCount = 15;         ///< Visible rows (1..15).
                int  colCount = 32;         ///< Visible columns (1..42).
                bool rowLock = true;        ///< Disallows row count change after DefineWindow.
                bool colLock = true;        ///< Disallows col count change.
                int  penRow = 0;            ///< Cursor row (0-based).
                int  penCol = 0;            ///< Cursor column (0-based).

                /// @brief Window-level attributes from @c SetWindowAttributes
                ///        (SWA, 0x97).  Updated every time an SWA
                ///        command lands inside this window's service
                ///        block; default-constructed before the first
                ///        SWA arrives.  See @ref Cea708WindowAttr.
                Cea708WindowAttr attrs;

                /// @brief Character grid.  Outer index = row (0..rowCount-1);
                ///        inner index = column (0..colCount-1).  Each
                ///        cell carries a codepoint + the pen attributes
                ///        that were current when the cell was written.
                ///        Empty cells store codepoint 0 with default pen.
                List<List<Cea708Cell>> grid;

                /// @brief Resizes the grid to @p rows × @p cols, clears all
                ///        cells, and resets the pen to (0, 0).
                void resize(int rows, int cols);

                /// @brief Zeroes every cell in the grid.  Doesn't touch the
                ///        pen position or the window's metadata.
                void clearGrid();

                /// @brief Writes one codepoint at the current pen position
                ///        with the given @p pen attributes, and advances
                ///        the pen by one column.  Wraps to the next row
                ///        when @c penCol exceeds @c colCount; rolls rows
                ///        up when the pen advances past the last row
                ///        (basic roll-up semantics).
                void putChar(uint32_t cp, const Cea708PenAttr &pen);

                /// @brief Convenience overload — writes @p cp at the
                ///        current pen position with a default-style
                ///        pen.  Equivalent to
                ///        @c putChar(cp, Cea708PenAttr{}).  Useful for
                ///        test fixtures and any caller that doesn't
                ///        track per-character pen state.
                void putChar(uint32_t cp) { putChar(cp, Cea708PenAttr{}); }

                /// @brief Advances the pen to the start of the next row,
                ///        rolling rows up when needed.
                void carriageReturn();

                /// @brief Joins every non-empty cell into a string,
                ///        separating rows with `\n`.  Trailing spaces /
                ///        empty cells on each row are stripped.  Pen
                ///        attributes are dropped — see @ref visibleSpans
                ///        for the styled view.
                String text() const;

                /// @brief Reconstructs the window's visible content as
                ///        an ordered list of styled @ref SubtitleSpan
                ///        runs.  Consecutive cells in the same row
                ///        whose pen state matches collapse into a
                ///        single span; row breaks emit a span whose
                ///        text is @c "\n" and pen state matches the
                ///        prior run.
                SubtitleSpan::List visibleSpans() const;

                /// @brief @c true when every cell of the grid is empty.
                bool isEmpty() const;

                bool operator==(const Cea708Window &o) const;
                bool operator!=(const Cea708Window &o) const { return !(*this == o); }
};

/**
 * @brief Composite state of all eight DTVCC windows for one
 *        caption service.
 * @ingroup proav
 *
 * The DTVCC service-block byte stream (the @c data() bytes of a
 * @ref Cea708Service) is a sequence of:
 *
 *  - **G0** (0x20..0x7F) — printable ASCII (0x7F is "music note"
 *    U+266A).
 *  - **G1** (0xA0..0xFF) — Latin-1 supplement characters.
 *  - **C0** (0x00..0x1F) — basic control codes (CR, FF, ETX, …).
 *  - **C1** (0x80..0x9F) — window-manipulation commands (CW0..CW7,
 *    DefineWindow, DisplayWindows, …).
 *  - **EXT1 (0x10)** — escape into the C2 / G2 extension sets.
 *  - **P16 (0x18)** — escape into the C3 / G3 / 16-bit-character
 *    extension sets.
 *
 * @ref processBytes walks the stream and updates this state.
 * @ref visibleText flattens the displayed grid into the
 * `Subtitle::text()`-shaped string that the decoder ultimately
 * emits.
 *
 * @par Scope of the parser
 *
 * This is the minimum-viable decoder targeting the realistic
 * single-service single-window plain-text path that covers
 * essentially every modern broadcast 708 stream:
 *
 *  - DefineWindow (DF0..DF7) — recognised; sets row/col counts,
 *    anchor, defined/visible flags, marks as the current window.
 *  - SetCurrentWindow (CW0..CW7).
 *  - DisplayWindows / HideWindows / ToggleWindows / ClearWindows /
 *    DeleteWindows (CLW / DSW / HDW / TGW / DLW) — visibility
 *    bitmap updates.
 *  - SetPenLocation (SPL) — repositions the cursor.
 *  - SetPenAttributes / SetPenColor (SPA / SPC) — fully decoded
 *    into @ref currentPen and stamped onto every character cell
 *    so multi-style cues recover with span boundaries via
 *    @ref Cea708Window::visibleSpans.
 *  - SetWindowAttributes (SWA) — fully decoded into the current
 *    window's @ref Cea708Window::attrs (fill colour / opacity,
 *    border, justify, scroll / print direction, word-wrap,
 *    display effect).  When set, the window's @c fillColor is
 *    inherited by every span emitted from that window's grid
 *    that doesn't carry an explicit per-cell SPC bg colour.
 *  - Delay (DLY), DelayCancel (DLC), Reset (RST) — recognised.
 *  - C0: ETX terminates a row, CR moves cursor to next row, FF
 *    clears the current window, BS moves cursor back one column.
 *  - EXT1 + EXT1-prefixed C2 / G2 / C3 / G3 commands consume
 *    their expected argument bytes (full mapping table is a
 *    future task).
 *  - G0 / G1: written verbatim to the grid (G1 mapped to
 *    Latin-1 supplement codepoints).
 *
 * Anything not recognised consumes one byte and continues — the
 * parser never stalls.
 */
class Cea708WindowState {
        public:
                /// @brief CEA-708 caps services at 8 windows each.
                static constexpr int WindowCount = 8;

                Cea708WindowState();

                /// @brief Resets every window to its default
                ///        (undefined, hidden, empty) state.  Called
                ///        on the @c RST command and at construction
                ///        time.
                void reset();

                /// @brief Current-window index (0..7).  Selected by
                ///        @c CW0..CW7 and implicitly by @c DF0..DF7.
                int currentWindowId() const { return _currentWindow; }
                /// @brief Sets the current window.  Out-of-range
                ///        values are clamped to [0, 7].
                void setCurrentWindowId(int id) {
                        _currentWindow = (id < 0) ? 0 : (id > 7) ? 7 : id;
                }

                /// @brief Read-only access to the eight windows.
                const Cea708Window &window(int id) const { return _windows[clampId(id)]; }

                /// @brief Mutable access to the eight windows.
                Cea708Window &window(int id) { return _windows[clampId(id)]; }

                /// @brief Convenience: the currently-active window
                ///        (the one G0 / G1 character writes target).
                const Cea708Window &currentWindow() const { return _windows[_currentWindow]; }
                Cea708Window       &currentWindow() { return _windows[_currentWindow]; }

                /// @brief Most recently asserted pen attributes (from
                ///        @c SPA / @c SPC).  Service-wide, not per-cell —
                ///        see @ref Cea708PenAttr for the limitation.
                const Cea708PenAttr &currentPen() const { return _pen; }
                Cea708PenAttr       &currentPen() { return _pen; }

                /// @brief @c true when at least one window has
                ///        @ref Cea708Window::visible set.
                bool anyVisible() const;

                /// @brief Returns the flat text of every currently
                ///        visible window, joined with `\n`.  Windows
                ///        are walked in @c Cea708Window::priority
                ///        order (lower priority = drawn on top, so
                ///        emitted first).
                String visibleText() const;

                /// @brief Reconstructs the visible content of every
                ///        currently-visible window as an ordered list
                ///        of styled @ref SubtitleSpan runs (same
                ///        priority order as @ref visibleText).  Windows
                ///        are separated by a `\n` span.  Multi-style
                ///        cues recover with full span boundaries — one
                ///        span per run of cells that share pen state.
                SubtitleSpan::List visibleSpans() const;

                /// @brief Processes one service-block payload byte
                ///        stream, updating window state in place.
                ///        Equivalent to @c processBytes(svc.data().data(),
                ///        svc.data().size()).
                void processServiceBytes(const Cea708Service &svc);

                /// @brief Processes a raw byte stream (G0 / G1 / C0 /
                ///        C1 / EXT1 / P16) — same semantics as
                ///        @ref processServiceBytes but takes raw
                ///        pointers.  Bytes that can't be interpreted
                ///        are consumed silently (one byte at a time)
                ///        so the parser never deadlocks on a malformed
                ///        stream.
                void processBytes(const void *data, size_t size);

        private:
                /// @brief Per-service window array, indexed by window ID (0..7).
                using WindowArray = Array<Cea708Window, WindowCount>;

                WindowArray   _windows;
                int           _currentWindow = 0;
                Cea708PenAttr _pen;

                /// @brief UTF-16 high surrogate held over from the most
                ///        recent @c P16 — combined with the next @c P16
                ///        when that arrives bearing a low surrogate.
                ///        Cleared by @ref reset and any non-P16 byte
                ///        that lands in between (an unpaired high
                ///        surrogate decays to U+FFFD).  Persisted on
                ///        the state so a surrogate pair split across
                ///        @ref processBytes calls (e.g. across DTVCC
                ///        packets) still pairs correctly.  Zero means
                ///        no pending surrogate.
                uint32_t _pendingHighSurrogate = 0;

                static int clampId(int id) {
                        if (id < 0) return 0;
                        if (id > 7) return 7;
                        return id;
                }
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_PROAV
