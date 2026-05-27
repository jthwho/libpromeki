/**
 * @file      cea708windowstate.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdint>
#include <promeki/cea708ext.h>
#include <promeki/cea708service.h>
#include <promeki/cea708windowstate.h>
#include <promeki/list.h>
#include <promeki/logger.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

        /// @brief Encodes a UTF-32 codepoint as UTF-8 bytes, appending
        ///        to @p out.  Skips zero codepoints (empty cells).
        ///
        /// Uses @c String::fromUtf8 (not the @c const @c char @c *
        /// constructor) — the latter interprets the bytes as Latin-1
        /// and would mis-decode any multi-byte UTF-8 sequence into
        /// per-byte codepoints (e.g. 0xC3 0x89 → U+00C3 + U+0089
        /// instead of U+00C9).
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

        /// @brief Translates a G0 byte (0x20..0x7F) to its Unicode
        ///        codepoint.  CEA-708 §7.1.4 substitutes 0x7F with the
        ///        "music note" character U+266A; the rest is plain
        ///        ASCII.
        uint32_t g0ToCodepoint(uint8_t b) {
                if (b == 0x7F) return 0x266A; // music note
                return static_cast<uint32_t>(b);
        }

        /// @brief Translates a G1 byte (0xA0..0xFF) to its Unicode
        ///        codepoint.  CEA-708 §7.1.6 maps G1 directly to
        ///        Latin-1 supplement (U+00A0..U+00FF).
        uint32_t g1ToCodepoint(uint8_t b) { return static_cast<uint32_t>(b); }

        /// @brief Quantises a 0..3 channel value (the wire encoding 708
        ///        uses for fg / bg / fill / border colour channels)
        ///        into a 0..1 normalised float for @ref Color::srgb.
        ///        Mirrors the SPC / SWA expansion used elsewhere in
        ///        this translation unit.
        float style2BitChannel(uint8_t v) {
                static const float kExpand[4] = {0.0f, 85.0f / 255.0f, 170.0f / 255.0f, 1.0f};
                return kExpand[v & 0x03];
        }

        /// @brief Applies a CEA-708-E Table 26 "Predefined Window Style"
        ///        preset to @p out.  IDs 1..7 are defined; out-of-range
        ///        IDs (0 / 8..) are no-ops — the caller is responsible
        ///        for the spec §8.10.5.2 rule "ws=0 on a create defaults
        ///        to Style #1".
        ///
        /// Style #1 = "NTSC Style PopUp Captions": SOLID black fill,
        /// LEFT justify, LtR print, BtT scroll, no wrap, SNAP effect,
        /// no border.
        /// Style #2 = "PopUp Captions w/o Black Background": same as
        /// Style #1 with TRANSPARENT fill.
        /// Style #3 = "NTSC Style Centered PopUp Captions": Style #1
        /// with CENTER justify.
        /// Style #4 = "NTSC Style RollUp Captions": Style #1 + wordwrap.
        /// Style #5 = "RollUp Captions w/o Black Background": Style #4
        /// with TRANSPARENT fill.
        /// Style #6 = "NTSC Style Centered RollUp Captions": Style #4
        /// with CENTER justify.
        /// Style #7 = "Ticker Tape": Top-to-bottom print, right-to-left
        /// scroll, black SOLID fill.
        void applyWindowStyleId(Cea708WindowAttr &out, uint8_t styleId) {
                if (styleId < 1 || styleId > 7) return;
                // Cea708WindowAttr fields:
                //   fillColor, fillOpacity, borderColor, borderType,
                //   justify, printDirection, scrollDirection, wordWrap,
                //   displayEffect, effectDirection, effectSpeed
                // Field values shared by all seven styles:
                out.borderColor = Color();
                out.borderType = 0;                    // NONE
                out.displayEffect = 0;                 // SNAP
                out.effectDirection = 0;
                out.effectSpeed = 0;
                switch (styleId) {
                        case 1:
                                out.fillColor = Color::Black;
                                out.fillOpacity = SubtitleOpacity::Solid;
                                out.justify = 0; // LEFT
                                out.printDirection = 0; // LtR
                                out.scrollDirection = 3; // BtT
                                out.wordWrap = false;
                                break;
                        case 2:
                                out.fillColor = Color();
                                out.fillOpacity = SubtitleOpacity::Transparent;
                                out.justify = 0;
                                out.printDirection = 0;
                                out.scrollDirection = 3;
                                out.wordWrap = false;
                                break;
                        case 3:
                                out.fillColor = Color::Black;
                                out.fillOpacity = SubtitleOpacity::Solid;
                                out.justify = 2; // CENTER
                                out.printDirection = 0;
                                out.scrollDirection = 3;
                                out.wordWrap = false;
                                break;
                        case 4:
                                out.fillColor = Color::Black;
                                out.fillOpacity = SubtitleOpacity::Solid;
                                out.justify = 0;
                                out.printDirection = 0;
                                out.scrollDirection = 3;
                                out.wordWrap = true;
                                break;
                        case 5:
                                out.fillColor = Color();
                                out.fillOpacity = SubtitleOpacity::Transparent;
                                out.justify = 0;
                                out.printDirection = 0;
                                out.scrollDirection = 3;
                                out.wordWrap = true;
                                break;
                        case 6:
                                out.fillColor = Color::Black;
                                out.fillOpacity = SubtitleOpacity::Solid;
                                out.justify = 2; // CENTER
                                out.printDirection = 0;
                                out.scrollDirection = 3;
                                out.wordWrap = true;
                                break;
                        case 7:
                                out.fillColor = Color::Black;
                                out.fillOpacity = SubtitleOpacity::Solid;
                                out.justify = 0;
                                out.printDirection = 2; // TtB
                                out.scrollDirection = 1; // RtL
                                out.wordWrap = false;
                                break;
                }
        }

        /// @brief Applies a CEA-708-E Table 27 "Predefined Pen Style"
        ///        preset to @p out.  IDs 1..7 are defined; out-of-range
        ///        IDs are no-ops.  Spec §8.10.5.2 rule: ps=0 on a create
        ///        defaults to Pen Style #1.
        ///
        /// All seven styles share STANDARD pen size, NORMAL offset, no
        /// italics, no underline, white-solid foreground on black
        /// (styles 1..5) or transparent (styles 6, 7) character bg.
        /// Font style and edge type vary:
        ///   #1: font=0 (Default), edge=NONE         — Default NTSC Style
        ///   #2: font=1 (Mono w/ Serif), edge=NONE   — NTSC Style Mono w/ Serif
        ///   #3: font=2 (Prop w/ Serif), edge=NONE   — NTSC Style Prop w/ Serif
        ///   #4: font=3 (Mono w/o Serif), edge=NONE  — NTSC Style Mono w/o Serif
        ///   #5: font=4 (Prop w/o Serif), edge=NONE  — NTSC Style Prop w/o Serif
        ///   #6: font=3 (Mono w/o Serif), edge=UNIFORM, edge=black, bg=transparent
        ///   #7: font=4 (Prop w/o Serif), edge=UNIFORM, edge=black, bg=transparent
        void applyPenStyleId(Cea708PenAttr &out, uint8_t styleId) {
                if (styleId < 1 || styleId > 7) return;
                // Defaults shared across all seven styles.
                out.italic = false;
                out.underline = false;
                // Per CTA-708-E §8.4.12 Table 27 all seven predefined pen
                // styles use "white solid" foreground.  In the 2-bit RGB
                // channel encoding 708 uses for SPC, "white" is code 3
                // (the maximum); code 2 → 170/255 ≈ 0.667 would render
                // text at ~67% brightness.
                out.foregroundColor = Color::srgb(style2BitChannel(3),
                                                  style2BitChannel(3),
                                                  style2BitChannel(3));
                out.foregroundOpacity = SubtitleOpacity::Solid;
                out.edgeColor = Color();
                // Per §8.10.5.10 edge opacity always tracks foreground opacity;
                // since every preloaded style sets fgOpacity = Solid, edges follow.
                out.edgeOpacity = out.foregroundOpacity;
                switch (styleId) {
                        case 1:
                                out.fontFace = SubtitleFontFace::Default;
                                out.edgeStyle = SubtitleEdgeStyle::None;
                                out.backgroundColor = Color::Black;
                                out.backgroundOpacity = SubtitleOpacity::Solid;
                                break;
                        case 2:
                                out.fontFace = SubtitleFontFace::MonoSerif;
                                out.edgeStyle = SubtitleEdgeStyle::None;
                                out.backgroundColor = Color::Black;
                                out.backgroundOpacity = SubtitleOpacity::Solid;
                                break;
                        case 3:
                                out.fontFace = SubtitleFontFace::ProportionalSerif;
                                out.edgeStyle = SubtitleEdgeStyle::None;
                                out.backgroundColor = Color::Black;
                                out.backgroundOpacity = SubtitleOpacity::Solid;
                                break;
                        case 4:
                                out.fontFace = SubtitleFontFace::MonoSans;
                                out.edgeStyle = SubtitleEdgeStyle::None;
                                out.backgroundColor = Color::Black;
                                out.backgroundOpacity = SubtitleOpacity::Solid;
                                break;
                        case 5:
                                out.fontFace = SubtitleFontFace::ProportionalSans;
                                out.edgeStyle = SubtitleEdgeStyle::None;
                                out.backgroundColor = Color::Black;
                                out.backgroundOpacity = SubtitleOpacity::Solid;
                                break;
                        case 6:
                                out.fontFace = SubtitleFontFace::MonoSans;
                                out.edgeStyle = SubtitleEdgeStyle::Uniform;
                                out.edgeColor = Color::Black;
                                out.backgroundColor = Color();
                                out.backgroundOpacity = SubtitleOpacity::Transparent;
                                break;
                        case 7:
                                out.fontFace = SubtitleFontFace::ProportionalSans;
                                out.edgeStyle = SubtitleEdgeStyle::Uniform;
                                out.edgeColor = Color::Black;
                                out.backgroundColor = Color();
                                out.backgroundOpacity = SubtitleOpacity::Transparent;
                                break;
                }
        }

} // namespace

// ============================================================================
// Cea708Window
// ============================================================================

void Cea708Window::resize(int rows, int cols) {
        if (rows < 1) rows = 1;
        if (rows > MaxRows) rows = MaxRows;
        if (cols < 1) cols = 1;
        if (cols > MaxCols) cols = MaxCols;
        rowCount = rows;
        colCount = cols;
        grid = List<List<Cea708Cell>>();
        grid.reserve(static_cast<size_t>(rows));
        for (int r = 0; r < rows; ++r) {
                List<Cea708Cell> row;
                row.reserve(static_cast<size_t>(cols));
                for (int c = 0; c < cols; ++c) row.pushToBack(Cea708Cell{});
                grid.pushToBack(row);
        }
        penRow = 0;
        penCol = 0;
}

void Cea708Window::clearGrid() {
        for (size_t r = 0; r < grid.size(); ++r) {
                auto &row = grid[r];
                for (size_t c = 0; c < row.size(); ++c) row[c] = Cea708Cell{};
        }
}

void Cea708Window::putChar(uint32_t cp, const Cea708PenAttr &pen) {
        if (grid.isEmpty()) resize(rowCount, colCount);
        // Per §8.4.8: "Characters entered into a row when the cursor is at the
        // final character position shall either replace the final character or
        // be discarded."  Decoders compliant with present CEA-708 shall NOT
        // wrap or hyphenate words at the end of a row.  Implement the
        // "replace the final character" branch — the new character overwrites
        // the last cell and the cursor stays at the (post-final, off-grid)
        // column so subsequent puts continue to replace.
        if (penCol >= colCount) {
                if (colCount <= 0) return;
                if (penRow >= 0 && penRow < static_cast<int>(grid.size())) {
                        auto &row = grid[penRow];
                        if (!row.isEmpty()) {
                                row[row.size() - 1] = Cea708Cell{cp, pen};
                        }
                }
                return;
        }
        if (penRow < 0 || penRow >= rowCount) {
                // Out-of-range pen rows (e.g. from a malformed SPL) get clamped
                // rather than triggering an implicit scroll — scrolling is the
                // CR command's job, not the character-write path.
                return;
        }
        if (penRow < static_cast<int>(grid.size())
            && penCol >= 0 && penCol < static_cast<int>(grid[penRow].size())) {
                grid[penRow][penCol] = Cea708Cell{cp, pen};
        }
        ++penCol;
}

void Cea708Window::carriageReturn() {
        penCol = 0;
        ++penRow;
        if (penRow >= rowCount) {
                if (!grid.isEmpty()) grid.remove(static_cast<size_t>(0));
                List<Cea708Cell> empty;
                empty.reserve(static_cast<size_t>(colCount));
                for (int c = 0; c < colCount; ++c) empty.pushToBack(Cea708Cell{});
                grid.pushToBack(empty);
                penRow = rowCount - 1;
        }
}

String Cea708Window::text() const {
        String out;
        bool   anyRow = false;
        for (size_t r = 0; r < grid.size(); ++r) {
                const auto &row = grid[r];
                // Find the trailing non-zero cell to trim padding.
                int lastNonZero = -1;
                for (size_t c = 0; c < row.size(); ++c) {
                        if (row[c].codepoint != 0) lastNonZero = static_cast<int>(c);
                }
                if (lastNonZero < 0) continue;
                if (anyRow) out += "\n";
                for (int c = 0; c <= lastNonZero; ++c) {
                        const uint32_t cp = row[c].codepoint;
                        if (cp == 0) {
                                out += " ";
                        } else {
                                appendUtf8(out, cp);
                        }
                }
                anyRow = true;
        }
        return out;
}

namespace {
        /// @brief Builds a @ref SubtitleSpan from accumulated UTF-8 text
        ///        + the pen state that produced it.  Maps pen fields
        ///        onto the span's styling slots: pen colour fills
        ///        @c color when valid; bg / edge colours fill the
        ///        matching slots; the three opacity values transfer
        ///        directly.  Empty text returns an empty span which
        ///        callers should skip.
        SubtitleSpan spanFromPen(const String &text, const Cea708PenAttr &pen) {
                SubtitleSpan s(text, false, pen.italic, pen.underline, pen.foregroundColor);
                s.setEdgeStyle(pen.edgeStyle);
                s.setFontFace(pen.fontFace);
                if (pen.backgroundColor.isValid()) s.setBackgroundColor(pen.backgroundColor);
                if (pen.edgeColor.isValid()) s.setEdgeColor(pen.edgeColor);
                s.setForegroundOpacity(pen.foregroundOpacity);
                s.setBackgroundOpacity(pen.backgroundOpacity);
                s.setEdgeOpacity(pen.edgeOpacity);
                return s;
        }
} // namespace

SubtitleSpan::List Cea708Window::visibleSpans() const {
        SubtitleSpan::List spans;
        bool               anyRow = false;
        for (size_t r = 0; r < grid.size(); ++r) {
                const auto &row = grid[r];
                int         lastNonZero = -1;
                for (size_t c = 0; c < row.size(); ++c) {
                        if (row[c].codepoint != 0) lastNonZero = static_cast<int>(c);
                }
                if (lastNonZero < 0) continue;
                if (anyRow) {
                        // Row separator carries no styling — the
                        // SubtitleRenderer / SubRip emitters split lines
                        // on the literal '\n' character.
                        spans.pushToBack(SubtitleSpan("\n"));
                }
                // Walk this row and group consecutive cells whose pen
                // state matches into a single span.  Empty interior
                // cells (codepoint==0 between two non-empty cells) are
                // rendered as a space and inherit the pen state of the
                // run they sit inside.
                Cea708PenAttr currentPen = row[0].pen;
                String        currentText;
                for (int c = 0; c <= lastNonZero; ++c) {
                        const Cea708Cell &cell = row[c];
                        const Cea708PenAttr &cellPen =
                                (cell.codepoint == 0) ? currentPen : cell.pen;
                        if (cellPen != currentPen) {
                                if (!currentText.isEmpty()) {
                                        spans.pushToBack(spanFromPen(currentText, currentPen));
                                }
                                currentText = String();
                                currentPen = cellPen;
                        }
                        if (cell.codepoint == 0) {
                                currentText += " ";
                        } else {
                                appendUtf8(currentText, cell.codepoint);
                        }
                }
                if (!currentText.isEmpty()) {
                        spans.pushToBack(spanFromPen(currentText, currentPen));
                }
                anyRow = true;
        }
        // Window-level fill from SWA inherits onto every span that
        // didn't pick up an explicit per-cell SPC bg.  This lets a
        // broadcaster who set the whole window's background via SWA
        // (rather than re-asserting it per character via SPC) recover
        // the same visual on the receiver side.  Spans that already
        // carry a per-cell bg keep theirs.
        if (attrs.fillColor.isValid()) {
                for (size_t i = 0; i < spans.size(); ++i) {
                        SubtitleSpan &sp = spans[i];
                        if (sp.text() == "\n") continue;
                        if (sp.backgroundColor().isValid()) continue;
                        sp.setBackgroundColor(attrs.fillColor);
                        sp.setBackgroundOpacity(attrs.fillOpacity);
                }
        }
        return spans;
}

bool Cea708Window::isEmpty() const {
        for (size_t r = 0; r < grid.size(); ++r) {
                const auto &row = grid[r];
                for (size_t c = 0; c < row.size(); ++c) {
                        if (row[c].codepoint != 0) return false;
                }
        }
        return true;
}

bool Cea708Window::operator==(const Cea708Window &o) const {
        return visible == o.visible && defined == o.defined && priority == o.priority
               && anchorPoint == o.anchorPoint && anchorV == o.anchorV && anchorH == o.anchorH
               && relativePos == o.relativePos && rowCount == o.rowCount
               && colCount == o.colCount && rowLock == o.rowLock && colLock == o.colLock
               && penRow == o.penRow && penCol == o.penCol && attrs == o.attrs && pen == o.pen
               && grid == o.grid;
}

// ============================================================================
// Cea708WindowState
// ============================================================================

Cea708WindowState::Cea708WindowState() { reset(); }

void Cea708WindowState::reset() {
        for (int i = 0; i < WindowCount; ++i) _windows[i] = Cea708Window();
        _currentWindow = 0;
        // Each window owns its own @ref Cea708Window::pen, reset above
        // along with the rest of the window state.
        _pendingHighSurrogate = 0;
}

bool Cea708WindowState::anyVisible() const {
        for (int i = 0; i < WindowCount; ++i) {
                if (_windows[i].visible) return true;
        }
        return false;
}

String Cea708WindowState::visibleText() const {
        // Visit windows in priority order (lower priority = drawn on
        // top per spec).  Concatenate visible windows' text with \n.
        // Build a stable order by (priority, id).
        int order[WindowCount];
        for (int i = 0; i < WindowCount; ++i) order[i] = i;
        // Simple insertion sort by (priority, id) — N=8, no need for
        // anything fancier.
        for (int i = 1; i < WindowCount; ++i) {
                int j = i;
                while (j > 0) {
                        const int a = order[j - 1];
                        const int b = order[j];
                        if (_windows[a].priority > _windows[b].priority
                            || (_windows[a].priority == _windows[b].priority && a > b)) {
                                order[j - 1] = b;
                                order[j] = a;
                                --j;
                        } else {
                                break;
                        }
                }
        }
        String out;
        bool   any = false;
        for (int i = 0; i < WindowCount; ++i) {
                const Cea708Window &w = _windows[order[i]];
                if (!w.visible) continue;
                if (w.isEmpty()) continue;
                if (any) out += "\n";
                out += w.text();
                any = true;
        }
        return out;
}

SubtitleSpan::List Cea708WindowState::visibleSpans() const {
        // Same priority-order walk as visibleText; concatenate each
        // visible window's reconstructed spans with a "\n" separator
        // span between windows.
        int order[WindowCount];
        for (int i = 0; i < WindowCount; ++i) order[i] = i;
        for (int i = 1; i < WindowCount; ++i) {
                int j = i;
                while (j > 0) {
                        const int a = order[j - 1];
                        const int b = order[j];
                        if (_windows[a].priority > _windows[b].priority
                            || (_windows[a].priority == _windows[b].priority && a > b)) {
                                order[j - 1] = b;
                                order[j] = a;
                                --j;
                        } else {
                                break;
                        }
                }
        }
        SubtitleSpan::List out;
        bool               any = false;
        for (int i = 0; i < WindowCount; ++i) {
                const Cea708Window &w = _windows[order[i]];
                if (!w.visible) continue;
                if (w.isEmpty()) continue;
                if (any) out.pushToBack(SubtitleSpan("\n"));
                SubtitleSpan::List ws = w.visibleSpans();
                for (size_t k = 0; k < ws.size(); ++k) out.pushToBack(ws[k]);
                any = true;
        }
        return out;
}

void Cea708WindowState::processServiceBytes(const Cea708Service &svc) {
        processBytes(svc.data().data(), svc.data().size());
}

void Cea708WindowState::processBytes(const void *dataPtr, size_t size) {
        if (dataPtr == nullptr || size == 0) return;
        const auto *p = static_cast<const uint8_t *>(dataPtr);
        size_t      i = 0;
        while (i < size) {
                const uint8_t b = p[i];
                // -- Drop a pending UTF-16 high surrogate when the
                //    next byte is anything other than a fresh @c P16
                //    sequence — the surrogate pair was broken
                //    mid-stream, so commit the orphaned high half as
                //    @c U+FFFD before processing the new byte.
                if (_pendingHighSurrogate != 0 && b != 0x18) {
                        currentWindow().putChar(0xFFFD, currentWindow().pen);
                        _pendingHighSurrogate = 0;
                }
                // -- G0 (printable ASCII) ----------------------------
                if (b >= 0x20 && b <= 0x7F) {
                        currentWindow().putChar(g0ToCodepoint(b), currentWindow().pen);
                        ++i;
                        continue;
                }
                // -- G1 (Latin-1 supplement) -------------------------
                if (b >= 0xA0) {
                        currentWindow().putChar(g1ToCodepoint(b), currentWindow().pen);
                        ++i;
                        continue;
                }
                // -- C0 control codes (0x00..0x1F) -------------------
                if (b <= 0x1F) {
                        switch (b) {
                                case 0x00: // NUL
                                case 0x03: // ETX (end of text)
                                        ++i;
                                        break;
                                case 0x08: // BS
                                        // Per §7.1.4.1 and §8.10.5, BS only moves the cursor back
                                        // one column.  It does NOT erase the previous cell — that's
                                        // HCR's job.  Backspace never crosses a row boundary.
                                        if (currentWindow().penCol > 0) {
                                                --currentWindow().penCol;
                                        }
                                        ++i;
                                        break;
                                case 0x0C: // FF (form feed — clear current window)
                                        currentWindow().clearGrid();
                                        currentWindow().penRow = 0;
                                        currentWindow().penCol = 0;
                                        ++i;
                                        break;
                                case 0x0D: // CR (carriage return)
                                        currentWindow().carriageReturn();
                                        ++i;
                                        break;
                                case 0x0E: // HCR (horizontal carriage return — clear current row)
                                {
                                        Cea708Window &w = currentWindow();
                                        if (w.penRow >= 0 && w.penRow < static_cast<int>(w.grid.size())) {
                                                auto &row = w.grid[w.penRow];
                                                for (size_t c = 0; c < row.size(); ++c) row[c] = Cea708Cell{};
                                                w.penCol = 0;
                                        }
                                        ++i;
                                        break;
                                }
                                case 0x10: // EXT1 (extension prefix — C2 / G2 / C3 / G3 follow)
                                {
                                        if (i + 1 >= size) {
                                                ++i;
                                                break;
                                        }
                                        const uint8_t ext = p[i + 1];
                                        if (ext >= 0x20 && ext <= 0x7F) {
                                                // G2 character — look up the codepoint in
                                                // the CEA-708-E §7.1.8 / Table 17 G2 table.
                                                // For reserved / undefined G2 positions
                                                // the spec is silent, but §9.3 mandates the
                                                // G0 underscore (0x5F) substitute for the
                                                // analogous G3 case ("All unsupported
                                                // graphic symbols in the G3 code space
                                                // shall be substituted with the G0
                                                // underscore character (_)").  We extend
                                                // that rule to undefined G2 positions —
                                                // U+FFFD would have been wrong (a receiver
                                                // showing "missing glyph" boxes for a
                                                // future-extension reserved code).
                                                //
                                                // Note: spec §9.3 Table 28 substitutions
                                                // (e.g. ellipsis → underscore, fractions →
                                                // "%") are for receivers that don't
                                                // support a particular *defined* G2
                                                // character.  Our @ref Cea708Ext::decodeG2
                                                // supports every defined G2 character, so
                                                // we never reach the Table 28 fallback
                                                // path — defined codepoints flow through
                                                // verbatim.
                                                const uint32_t cp = Cea708Ext::decodeG2(ext);
                                                currentWindow().putChar(
                                                        cp != Cea708Ext::NoCodepoint ? cp : 0x005F, currentWindow().pen);
                                                i += 2;
                                        } else if (ext >= 0xA0) {
                                                // G3 character — only @c 0xA0 (the ATSC CC
                                                // logo, U+E000) is defined.  Per CEA-708-E
                                                // §9.3 "All unsupported graphic symbols in
                                                // the G3 code space shall be substituted
                                                // with the G0 underscore character (_),
                                                // char code 0x5F."
                                                const uint32_t cp = Cea708Ext::decodeG3(ext);
                                                currentWindow().putChar(
                                                        cp != Cea708Ext::NoCodepoint ? cp : 0x005F, currentWindow().pen);
                                                i += 2;
                                        } else if (ext <= 0x07) {
                                                // C2 single-byte controls — no extra args.
                                                i += 2;
                                        } else if (ext <= 0x0F) {
                                                // C2 reserved (1-byte payload).
                                                i += 3;
                                                if (i > size) i = size;
                                        } else if (ext <= 0x17) {
                                                // C2 reserved (2-byte payload).
                                                i += 4;
                                                if (i > size) i = size;
                                        } else if (ext <= 0x1F) {
                                                // C2 reserved (3-byte payload).
                                                i += 5;
                                                if (i > size) i = size;
                                        } else if (ext <= 0x87) {
                                                // C3 fixed 4-byte payload per §7.1.11.1 Table 22
                                                // (5-byte control code = EXT1 + ExtCode + 4 data).
                                                i += 6;
                                                if (i > size) i = size;
                                        } else if (ext <= 0x8F) {
                                                // C3 fixed 5-byte payload per §7.1.11.1 Table 22
                                                // (6-byte control code = EXT1 + ExtCode + 5 data).
                                                i += 7;
                                                if (i > size) i = size;
                                        } else if (ext <= 0x9F) {
                                                // C3 variable-length per §7.1.11.2 / Table 23.
                                                // Header byte layout: type(b7-b6) | 0(b5) | length(b4-b0).
                                                // Skip header + length payload bytes.
                                                if (i + 2 >= size) {
                                                        i = size;
                                                        break;
                                                }
                                                const uint8_t plen = static_cast<uint8_t>(p[i + 2] & 0x1F);
                                                i += 3;
                                                i += plen;
                                                if (i > size) i = size;
                                        } else {
                                                // G3 reserved already handled above (ext >= 0xA1
                                                // path at the top of the EXT1 branch); this is
                                                // unreachable.
                                                i += 2;
                                        }
                                        break;
                                }
                                case 0x18: // P16 (next 2 bytes form a 16-bit char)
                                {
                                        if (i + 2 >= size) {
                                                // Truncated P16 — drop any pending high
                                                // surrogate as orphaned (already done at
                                                // loop top when this byte != 0x18, but
                                                // reaching here means @c 0x18 was the
                                                // final byte with nothing to pair).
                                                if (_pendingHighSurrogate != 0) {
                                                        currentWindow().putChar(0xFFFD, currentWindow().pen);
                                                        _pendingHighSurrogate = 0;
                                                }
                                                i = size;
                                                break;
                                        }
                                        const uint32_t v = (static_cast<uint32_t>(p[i + 1]) << 8)
                                                           | static_cast<uint32_t>(p[i + 2]);
                                        if (_pendingHighSurrogate != 0) {
                                                // Expect a UTF-16 low surrogate to pair
                                                // with the held high half.
                                                if (v >= 0xDC00 && v <= 0xDFFF) {
                                                        const uint32_t cp =
                                                                0x10000
                                                                + ((_pendingHighSurrogate - 0xD800) << 10)
                                                                + (v - 0xDC00);
                                                        currentWindow().putChar(cp, currentWindow().pen);
                                                        _pendingHighSurrogate = 0;
                                                } else {
                                                        // Pairing failed — commit the
                                                        // orphaned high half as U+FFFD,
                                                        // then process this P16 fresh.
                                                        currentWindow().putChar(0xFFFD, currentWindow().pen);
                                                        _pendingHighSurrogate = 0;
                                                        if (v >= 0xD800 && v <= 0xDBFF) {
                                                                _pendingHighSurrogate = v;
                                                        } else if (v >= 0xDC00 && v <= 0xDFFF) {
                                                                // Lone low surrogate — drop.
                                                                currentWindow().putChar(0xFFFD, currentWindow().pen);
                                                        } else {
                                                                currentWindow().putChar(v, currentWindow().pen);
                                                        }
                                                }
                                        } else if (v >= 0xD800 && v <= 0xDBFF) {
                                                // High surrogate — hold for the next P16.
                                                _pendingHighSurrogate = v;
                                        } else if (v >= 0xDC00 && v <= 0xDFFF) {
                                                // Lone low surrogate — drop.
                                                currentWindow().putChar(0xFFFD, currentWindow().pen);
                                        } else {
                                                currentWindow().putChar(v, currentWindow().pen);
                                        }
                                        i += 3;
                                        break;
                                }
                                default:
                                        // CEA-708-E §7.1.4 "C0 Code Set"
                                        // skip-length rules for unused
                                        // codes (the named codes 0x00,
                                        // 0x03, 0x08, 0x0C, 0x0D, 0x0E,
                                        // 0x10 EXT1, 0x18 P16 are all
                                        // handled above and cannot
                                        // reach this default).
                                        //   0x00..0x0F: 1-byte sequence
                                        //   0x11..0x17: 2-byte sequence
                                        //   0x19..0x1F: 3-byte sequence
                                        // Skipping the wrong number of
                                        // bytes here desyncs the rest
                                        // of the stream, so this table
                                        // implements the spec verbatim.
                                        if (b <= 0x0F) {
                                                ++i;
                                        } else if (b <= 0x17) {
                                                i += 2;
                                                if (i > size) i = size;
                                        } else {
                                                i += 3;
                                                if (i > size) i = size;
                                        }
                                        break;
                        }
                        continue;
                }
                // -- C1 control codes (0x80..0x9F) -------------------
                if (b <= 0x9F) {
                        // CW0..CW7 (0x80..0x87): SetCurrentWindow.
                        if (b <= 0x87) {
                                setCurrentWindowId(b - 0x80);
                                ++i;
                                continue;
                        }
                        switch (b) {
                                case 0x88: // CLW — ClearWindows (1 byte: window bitmap)
                                {
                                        if (i + 1 >= size) {
                                                i = size;
                                                break;
                                        }
                                        const uint8_t bitmap = p[i + 1];
                                        for (int w = 0; w < WindowCount; ++w) {
                                                if (bitmap & (1u << w)) {
                                                        _windows[w].clearGrid();
                                                        _windows[w].penRow = 0;
                                                        _windows[w].penCol = 0;
                                                }
                                        }
                                        i += 2;
                                        break;
                                }
                                case 0x89: // DSW — DisplayWindows
                                {
                                        if (i + 1 >= size) {
                                                i = size;
                                                break;
                                        }
                                        const uint8_t bitmap = p[i + 1];
                                        for (int w = 0; w < WindowCount; ++w) {
                                                if (bitmap & (1u << w)) _windows[w].visible = true;
                                        }
                                        i += 2;
                                        break;
                                }
                                case 0x8A: // HDW — HideWindows
                                {
                                        if (i + 1 >= size) {
                                                i = size;
                                                break;
                                        }
                                        const uint8_t bitmap = p[i + 1];
                                        for (int w = 0; w < WindowCount; ++w) {
                                                if (bitmap & (1u << w)) _windows[w].visible = false;
                                        }
                                        i += 2;
                                        break;
                                }
                                case 0x8B: // TGW — ToggleWindows
                                {
                                        if (i + 1 >= size) {
                                                i = size;
                                                break;
                                        }
                                        const uint8_t bitmap = p[i + 1];
                                        for (int w = 0; w < WindowCount; ++w) {
                                                if (bitmap & (1u << w)) _windows[w].visible = !_windows[w].visible;
                                        }
                                        i += 2;
                                        break;
                                }
                                case 0x8C: // DLW — DeleteWindows
                                {
                                        if (i + 1 >= size) {
                                                i = size;
                                                break;
                                        }
                                        const uint8_t bitmap = p[i + 1];
                                        for (int w = 0; w < WindowCount; ++w) {
                                                if (bitmap & (1u << w)) _windows[w] = Cea708Window();
                                        }
                                        i += 2;
                                        break;
                                }
                                case 0x8D: // DLY — Delay (1 byte: tenths of seconds)
                                        // CEA-708-E §8.10.5.12: spec requires the receiver
                                        // to suspend service-data interpretation for the
                                        // specified N/10 seconds.  This window-state
                                        // parser has no scheduler — it processes service
                                        // bytes synchronously inside @ref processBytes
                                        // and has no concept of wall-clock time.  Real
                                        // delay semantics would need an external pacing
                                        // layer that calls @ref processBytes in chunks.
                                        // DLY is rare in practice (its primary use is
                                        // synchronising captions to live broadcast
                                        // timing, which the carrying CDP / SDI / SEI
                                        // transport already pins to a frame timestamp);
                                        // the parser advances past the command without
                                        // enforcing the wait.
                                        i += 2;
                                        if (i > size) i = size;
                                        break;
                                case 0x8E: // DLC — DelayCancel
                                        // No-op for the same reason — DLY is not
                                        // enforced, so cancelling it is also a no-op.
                                        ++i;
                                        break;
                                case 0x8F: // RST — Reset
                                        reset();
                                        ++i;
                                        break;
                                case 0x90: { // SPA — SetPenAttributes (2 args)
                                        if (i + 2 >= size) {
                                                i = size;
                                                break;
                                        }
                                        // Wire layout (CEA-708-E §8.10.5.9):
                                        //   byte1: text_tag<<4 | offset<<2 | pen_size
                                        //   byte2: italic<<7 | underline<<6 |
                                        //          edge_type<<3 | font_tag
                                        const uint8_t a1 = static_cast<uint8_t>(p[i + 1]);
                                        const uint8_t a2 = static_cast<uint8_t>(p[i + 2]);
                                        // text_tag (a1 bits 4..7) — semantic role hint
                                        // per §8.5.9 / §8.10.5.9 (Dialog, SourceId,
                                        // Lyrics, etc.).  All 16 values are well-defined
                                        // in @ref SubtitleTextTag — including the three
                                        // Reserved12..14 slots — so no clamping is needed.
                                        currentWindow().pen.textTag =
                                                SubtitleTextTag(static_cast<int>((a1 >> 4) & 0x0F));
                                        // Per §8.10.5.9: pen_size ∈ {SMALL=0, STANDARD=1, LARGE=2};
                                        // pen_offset ∈ {SUBSCRIPT=0, NORMAL=1, SUPERSCRIPT=2}.
                                        // The wire field is 2 bits, but value 3 is reserved and
                                        // shall not produce an invalid enum.  Clamp to the spec's
                                        // mid value (STANDARD / NORMAL) on reserved input.
                                        {
                                                const uint8_t offsetBits = static_cast<uint8_t>((a1 >> 2) & 0x03);
                                                const uint8_t sizeBits = static_cast<uint8_t>(a1 & 0x03);
                                                currentWindow().pen.penOffset =
                                                        SubtitlePenOffset(static_cast<int>(offsetBits == 3 ? 1 : offsetBits));
                                                currentWindow().pen.penSize =
                                                        SubtitlePenSize(static_cast<int>(sizeBits == 3 ? 1 : sizeBits));
                                        }
                                        currentWindow().pen.italic = (a2 & 0x80) != 0;
                                        currentWindow().pen.underline = (a2 & 0x40) != 0;
                                        // Per §8.10.5.9: edge_type ∈ 0..5 (None..ShadowRight);
                                        // values 6 and 7 are reserved.  Clamp reserved values
                                        // to None rather than emit an invalid enum.
                                        {
                                                const uint8_t edgeBits = static_cast<uint8_t>((a2 >> 3) & 0x07);
                                                currentWindow().pen.edgeStyle =
                                                        SubtitleEdgeStyle(static_cast<int>(edgeBits > 5 ? 0 : edgeBits));
                                        }
                                        // Per CEA-708-E §8.5.2 / §8.10.5.9 font_tag
                                        // is fully populated 0..7 (Default, MonoSerif,
                                        // ProportionalSerif, MonoSans, ProportionalSans,
                                        // Casual, Cursive, SmallCaps) — no reserved
                                        // wire values to clamp.  "Font-supplier-dependent"
                                        // in the spec refers to receiver substitution
                                        // (which concrete face renders e.g. MonoSerif),
                                        // not reserved wire codes.
                                        currentWindow().pen.fontFace = SubtitleFontFace(static_cast<int>(a2 & 0x07));
                                        i += 3;
                                        break;
                                }
                                case 0x91: { // SPC — SetPenColor (3 args)
                                        if (i + 3 >= size) {
                                                i = size;
                                                break;
                                        }
                                        // Wire layout (bytes 1, 2, 3):
                                        //   byte1: fg_opacity<<6 | fg_R<<4 | fg_G<<2 | fg_B
                                        //   byte2: bg_opacity<<6 | bg_R<<4 | bg_G<<2 | bg_B
                                        //   byte3: reserved<<6 | edge_R<<4 | edge_G<<2 | edge_B
                                        const uint8_t fgByte = static_cast<uint8_t>(p[i + 1]);
                                        const uint8_t bgByte = static_cast<uint8_t>(p[i + 2]);
                                        const uint8_t edgeByte = static_cast<uint8_t>(p[i + 3]);
                                        auto expand = [](uint8_t v) -> float {
                                                static const float kExpand[4] = {0.0f, 85.0f / 255.0f,
                                                                                  170.0f / 255.0f, 1.0f};
                                                return kExpand[v & 0x03];
                                        };
                                        currentWindow().pen.foregroundOpacity =
                                                SubtitleOpacity(static_cast<int>((fgByte >> 6) & 0x03));
                                        currentWindow().pen.foregroundColor =
                                                Color::srgb(expand((fgByte >> 4) & 0x03),
                                                            expand((fgByte >> 2) & 0x03),
                                                            expand(fgByte & 0x03));
                                        currentWindow().pen.backgroundOpacity =
                                                SubtitleOpacity(static_cast<int>((bgByte >> 6) & 0x03));
                                        if (currentWindow().pen.backgroundOpacity.value()
                                            == SubtitleOpacity::Transparent.value()) {
                                                currentWindow().pen.backgroundColor = Color();
                                        } else {
                                                currentWindow().pen.backgroundColor =
                                                        Color::srgb(expand((bgByte >> 4) & 0x03),
                                                                    expand((bgByte >> 2) & 0x03),
                                                                    expand(bgByte & 0x03));
                                        }
                                        currentWindow().pen.edgeColor = Color::srgb(expand((edgeByte >> 4) & 0x03),
                                                                      expand((edgeByte >> 2) & 0x03),
                                                                      expand(edgeByte & 0x03));
                                        // Per §8.10.5.10: "The text character edges have the
                                        // same opacity value as fg opacity."  Mirror it after
                                        // the foreground has been parsed.
                                        currentWindow().pen.edgeOpacity =
                                                currentWindow().pen.foregroundOpacity;
                                        i += 4;
                                        break;
                                }
                                case 0x92: // SPL — SetPenLocation (2 args: row, col)
                                {
                                        if (i + 2 >= size) {
                                                i = size;
                                                break;
                                        }
                                        const uint8_t row = static_cast<uint8_t>(p[i + 1] & 0x0F);
                                        const uint8_t col = static_cast<uint8_t>(p[i + 2] & 0x3F);
                                        Cea708Window &w = currentWindow();
                                        if (w.grid.isEmpty()) w.resize(w.rowCount, w.colCount);
                                        w.penRow = (row >= w.rowCount) ? (w.rowCount - 1) : row;
                                        w.penCol = (col >= w.colCount) ? (w.colCount - 1) : col;
                                        i += 3;
                                        break;
                                }
                                case 0x97: { // SWA — SetWindowAttributes (4 args)
                                        if (i + 4 >= size) {
                                                i = size;
                                                break;
                                        }
                                        // Wire layout (CEA-708-D §8.10.5.10):
                                        //   byte1: fill_opacity<<6 | fill_R<<4 | fill_G<<2 | fill_B
                                        //   byte2: border_type01<<6 | border_R<<4 | border_G<<2 | border_B
                                        //   byte3: border_type2<<7 | wordwrap<<6 | print_dir<<4
                                        //          | scroll_dir<<2 | justify
                                        //   byte4: effect_speed<<4 | effect_dir<<2 | display_effect
                                        const uint8_t a1 = static_cast<uint8_t>(p[i + 1]);
                                        const uint8_t a2 = static_cast<uint8_t>(p[i + 2]);
                                        const uint8_t a3 = static_cast<uint8_t>(p[i + 3]);
                                        const uint8_t a4 = static_cast<uint8_t>(p[i + 4]);
                                        auto expand = [](uint8_t v) -> float {
                                                static const float kExpand[4] = {0.0f, 85.0f / 255.0f,
                                                                                  170.0f / 255.0f, 1.0f};
                                                return kExpand[v & 0x03];
                                        };
                                        Cea708WindowAttr &wa = currentWindow().attrs;
                                        wa.fillOpacity =
                                                SubtitleOpacity(static_cast<int>((a1 >> 6) & 0x03));
                                        if (wa.fillOpacity.value()
                                            == SubtitleOpacity::Transparent.value()) {
                                                wa.fillColor = Color();
                                        } else {
                                                wa.fillColor = Color::srgb(expand((a1 >> 4) & 0x03),
                                                                           expand((a1 >> 2) & 0x03),
                                                                           expand(a1 & 0x03));
                                        }
                                        // Per CEA-708-E §8.10.5.10 border_type ∈ 0..5
                                        // (None / Raised / Depressed / Uniform /
                                        // ShadowLeft / ShadowRight); the 3-bit wire
                                        // field (a2 bits 7..6 || a3 bit 7) can carry
                                        // values 6 and 7, which the spec reserves.
                                        // Clamp reserved values to None — same posture
                                        // as the SPA edge_type clamp below — rather
                                        // than store an out-of-spec border style that
                                        // renderers can't interpret.
                                        const uint8_t btLow = static_cast<uint8_t>((a2 >> 6) & 0x03);
                                        const uint8_t btHi = static_cast<uint8_t>((a3 >> 7) & 0x01);
                                        const uint8_t btRaw = static_cast<uint8_t>((btHi << 2) | btLow);
                                        wa.borderType = (btRaw > 5) ? 0 : btRaw;
                                        if (wa.borderType == 0) {
                                                wa.borderColor = Color();
                                        } else {
                                                wa.borderColor = Color::srgb(expand((a2 >> 4) & 0x03),
                                                                             expand((a2 >> 2) & 0x03),
                                                                             expand(a2 & 0x03));
                                        }
                                        wa.wordWrap = (a3 & 0x40) != 0;
                                        wa.printDirection = static_cast<uint8_t>((a3 >> 4) & 0x03);
                                        wa.scrollDirection = static_cast<uint8_t>((a3 >> 2) & 0x03);
                                        wa.justify = static_cast<uint8_t>(a3 & 0x03);
                                        wa.effectSpeed = static_cast<uint8_t>((a4 >> 4) & 0x0F);
                                        wa.effectDirection = static_cast<uint8_t>((a4 >> 2) & 0x03);
                                        wa.displayEffect = static_cast<uint8_t>(a4 & 0x03);
                                        i += 5;
                                        break;
                                }
                                default: {
                                        // DF0..DF7 (0x98..0x9F): DefineWindow (6 args).
                                        if (b >= 0x98 && b <= 0x9F) {
                                                if (i + 6 >= size) {
                                                        i = size;
                                                        break;
                                                }
                                                const int     id = b - 0x98;
                                                // CEA-708-E §8.10.5.2 parm1 wire layout:
                                                //   b7 b6  b5     b4       b3       b2 b1 b0
                                                //    0  0  visible row_lock col_lock priority(3 bits)
                                                const uint8_t b1 = p[i + 1];
                                                const uint8_t b2 = p[i + 2]; // relative_pos + anchor_v
                                                const uint8_t b3 = p[i + 3]; // anchor_h
                                                const uint8_t b4 = p[i + 4]; // anchor_point + row_count
                                                const uint8_t b5 = p[i + 5]; // col_count
                                                // CEA-708-E §8.10.5.2 parm6 wire layout:
                                                //   b7 b6  b5 b4 b3      b2 b1 b0
                                                //    0  0  window_style  pen_style
                                                const uint8_t b6 = p[i + 6];

                                                Cea708Window &w = _windows[id];
                                                const int     priority = (b1 & 0x07);
                                                const bool    colLock = (b1 & 0x08) != 0;
                                                const bool    rowLock = (b1 & 0x10) != 0;
                                                const bool    visible = (b1 & 0x20) != 0;
                                                const bool    relativePos = (b2 & 0x80) != 0;
                                                const int     anchorV = (b2 & 0x7F);
                                                const int     anchorH = b3;
                                                const int     anchorPoint = (b4 & 0xF0) >> 4;
                                                int           rowCount = (b4 & 0x0F);
                                                int           colCount = (b5 & 0x3F);
                                                const uint8_t windowStyleId =
                                                        static_cast<uint8_t>((b6 >> 3) & 0x07);
                                                const uint8_t penStyleId =
                                                        static_cast<uint8_t>(b6 & 0x07);
                                                // CEA-708 §8.4.6: row_count + 1 = visible rows;
                                                // col_count + 1 = visible cols.
                                                rowCount += 1;
                                                colCount += 1;
                                                // CEA-708-E §9.4 Table 29 / §9.7 safe-title
                                                // enforcement.  At STANDARD pen size:
                                                //   4:3   → 32 cols max
                                                //   16:9  → 42 cols max
                                                // Anchor coordinates (absolute mode):
                                                //   v: 0..74 (both aspects)
                                                //   h: 0..159 (4:3) / 0..209 (16:9)
                                                // Per §9.7: "If the resulting size of any
                                                // window is larger than the safe title
                                                // area for the corresponding display's
                                                // aspect ratio, then this window will be
                                                // completely disregarded."
                                                const int maxColsForAspect =
                                                        _displayAspect == DisplayAspect::WideScreen ? 42 : 32;
                                                const int maxAnchorH =
                                                        _displayAspect == DisplayAspect::WideScreen ? 209 : 159;
                                                const bool anchorOutOfBounds =
                                                        !relativePos
                                                        && (anchorV > 74 || anchorH > maxAnchorH);
                                                const bool colCountOutOfBounds = colCount > maxColsForAspect;
                                                if (anchorOutOfBounds || colCountOutOfBounds) {
                                                        // Disregard the window — undefined,
                                                        // hidden, empty grid.  Don't make
                                                        // it the current window.
                                                        w = Cea708Window();
                                                        i += 7;
                                                        break;
                                                }
                                                const bool isNewWindow = !w.defined;
                                                // Per §8.4.4: anchor IDs range 0..8.  Values
                                                // 9..15 in the 4-bit wire field are reserved and
                                                // shall not affect display (§7.1.2); clamp them
                                                // to BottomCenter (7) as a safe fallback rather
                                                // than store an invalid anchor.
                                                const int  clampedAnchorPoint = (anchorPoint > 8) ? 7 : anchorPoint;
                                                // Per §8.10.5.2: if DefineWindow is issued for an
                                                // existing window with parameters unchanged from
                                                // the prior definition, the command shall be
                                                // ignored.  The encoder above emits explicit
                                                // ClearWindow + SetPenLocation after every
                                                // DefineWindow so cue boundaries remain
                                                // unambiguous even when the DefineWindow itself
                                                // is a no-op re-issue.
                                                if (!isNewWindow
                                                    && w.visible == visible && w.priority == priority
                                                    && w.colLock == colLock && w.rowLock == rowLock
                                                    && w.relativePos == relativePos
                                                    && w.anchorV == anchorV && w.anchorH == anchorH
                                                    && w.anchorPoint == clampedAnchorPoint
                                                    && w.rowCount == rowCount && w.colCount == colCount
                                                    && w.lastWindowStyleId == windowStyleId
                                                    && w.lastPenStyleId == penStyleId) {
                                                        // Spec says the command shall be ignored;
                                                        // current-window selection is also
                                                        // unaffected (no setCurrentWindowId).
                                                        i += 7;
                                                        break;
                                                }
                                                // Window-style preload (Table 26):
                                                //   * On new-window create with ws=0 → style #1.
                                                //   * On any non-zero ws → apply that style.
                                                //   * On update with ws=0 → do not change attrs.
                                                if (windowStyleId != 0) {
                                                        applyWindowStyleId(w.attrs, windowStyleId);
                                                } else if (isNewWindow) {
                                                        applyWindowStyleId(w.attrs, 1);
                                                }
                                                // Pen-style preload (Table 27): same rules.
                                                // Per §8.10.5.2: "When an existing window is
                                                // being updated... the pen location and pen
                                                // attributes are unaffected."  On update we
                                                // skip applyPenStyleId regardless of ps.
                                                if (isNewWindow) {
                                                        applyPenStyleId(w.pen,
                                                                        penStyleId == 0 ? 1 : penStyleId);
                                                }
                                                w.defined = true;
                                                w.visible = visible;
                                                w.priority = priority;
                                                w.colLock = colLock;
                                                w.rowLock = rowLock;
                                                w.relativePos = relativePos;
                                                w.anchorV = anchorV;
                                                w.anchorH = anchorH;
                                                w.anchorPoint = clampedAnchorPoint;
                                                w.lastWindowStyleId = windowStyleId;
                                                w.lastPenStyleId = penStyleId;
                                                if (isNewWindow) {
                                                        // Per §8.10.5.2: on create, all cell
                                                        // positions go to the window fill colour
                                                        // and the pen is at (0,0).
                                                        w.resize(rowCount, colCount);
                                                } else {
                                                        // Per §8.10.5.2 on update: the pen
                                                        // location and pen attributes are
                                                        // unaffected; window-style + pen-style
                                                        // IDs preload only the corresponding
                                                        // attribute groups (above), not the pen
                                                        // position.  Spec doesn't say grid
                                                        // content survives an update; in
                                                        // practice the encoder issues an
                                                        // explicit ClearWindow immediately
                                                        // after each DefineWindow so the grid
                                                        // is reset there.  Rebuild the grid at
                                                        // the (possibly new) dimensions but
                                                        // restore the pen state the spec
                                                        // mandates we preserve.
                                                        const int  prevPenRow = w.penRow;
                                                        const int  prevPenCol = w.penCol;
                                                        const Cea708PenAttr prevPen = w.pen;
                                                        w.resize(rowCount, colCount);
                                                        w.pen = prevPen;
                                                        w.penRow = (prevPenRow >= rowCount) ? rowCount - 1 : prevPenRow;
                                                        w.penCol = (prevPenCol >= colCount) ? colCount - 1 : prevPenCol;
                                                }
                                                setCurrentWindowId(id);
                                                i += 7;
                                                break;
                                        }
                                        // Anything else in C1 we don't recognise — consume one byte.
                                        ++i;
                                        break;
                                }
                        }
                        continue;
                }
                // Should be unreachable (covers 0x00..0x9F via earlier
                // branches and 0xA0..0xFF via the G1 branch), but a
                // defensive ++i keeps us safe even if the byte ranges
                // grow in the future.
                ++i;
        }
}

PROMEKI_NAMESPACE_END
