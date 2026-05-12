/**
 * @file      cea708windowstate.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdint>
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
        grid = List<List<uint32_t>>();
        grid.reserve(static_cast<size_t>(rows));
        for (int r = 0; r < rows; ++r) {
                List<uint32_t> row;
                row.reserve(static_cast<size_t>(cols));
                for (int c = 0; c < cols; ++c) row.pushToBack(0u);
                grid.pushToBack(row);
        }
        penRow = 0;
        penCol = 0;
}

void Cea708Window::clearGrid() {
        for (size_t r = 0; r < grid.size(); ++r) {
                auto &row = grid[r];
                for (size_t c = 0; c < row.size(); ++c) row[c] = 0;
        }
}

void Cea708Window::putChar(uint32_t cp) {
        if (grid.isEmpty()) resize(rowCount, colCount);
        if (penCol >= colCount) {
                // Line wrap: advance to next row.
                penCol = 0;
                ++penRow;
        }
        if (penRow >= rowCount) {
                // Roll up: drop the top row, append a fresh empty row at the bottom.
                if (!grid.isEmpty()) grid.remove(static_cast<size_t>(0));
                List<uint32_t> empty;
                empty.reserve(static_cast<size_t>(colCount));
                for (int c = 0; c < colCount; ++c) empty.pushToBack(0u);
                grid.pushToBack(empty);
                penRow = rowCount - 1;
        }
        if (penRow >= 0 && penRow < static_cast<int>(grid.size())
            && penCol >= 0 && penCol < static_cast<int>(grid[penRow].size())) {
                grid[penRow][penCol] = cp;
        }
        ++penCol;
}

void Cea708Window::carriageReturn() {
        penCol = 0;
        ++penRow;
        if (penRow >= rowCount) {
                if (!grid.isEmpty()) grid.remove(static_cast<size_t>(0));
                List<uint32_t> empty;
                empty.reserve(static_cast<size_t>(colCount));
                for (int c = 0; c < colCount; ++c) empty.pushToBack(0u);
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
                        if (row[c] != 0) lastNonZero = static_cast<int>(c);
                }
                if (lastNonZero < 0) continue;
                if (anyRow) out += "\n";
                for (int c = 0; c <= lastNonZero; ++c) {
                        const uint32_t cp = row[c];
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

bool Cea708Window::isEmpty() const {
        for (size_t r = 0; r < grid.size(); ++r) {
                const auto &row = grid[r];
                for (size_t c = 0; c < row.size(); ++c) {
                        if (row[c] != 0) return false;
                }
        }
        return true;
}

bool Cea708Window::operator==(const Cea708Window &o) const {
        return visible == o.visible && defined == o.defined && priority == o.priority
               && anchorPoint == o.anchorPoint && anchorV == o.anchorV && anchorH == o.anchorH
               && relativePos == o.relativePos && rowCount == o.rowCount
               && colCount == o.colCount && rowLock == o.rowLock && colLock == o.colLock
               && penRow == o.penRow && penCol == o.penCol && grid == o.grid;
}

// ============================================================================
// Cea708WindowState
// ============================================================================

Cea708WindowState::Cea708WindowState() { reset(); }

void Cea708WindowState::reset() {
        for (int i = 0; i < WindowCount; ++i) _windows[i] = Cea708Window();
        _currentWindow = 0;
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

void Cea708WindowState::processServiceBytes(const Cea708Service &svc) {
        processBytes(svc.data().data(), svc.data().size());
}

void Cea708WindowState::processBytes(const void *dataPtr, size_t size) {
        if (dataPtr == nullptr || size == 0) return;
        const auto *p = static_cast<const uint8_t *>(dataPtr);
        size_t      i = 0;
        while (i < size) {
                const uint8_t b = p[i];
                // -- G0 (printable ASCII) ----------------------------
                if (b >= 0x20 && b <= 0x7F) {
                        currentWindow().putChar(g0ToCodepoint(b));
                        ++i;
                        continue;
                }
                // -- G1 (Latin-1 supplement) -------------------------
                if (b >= 0xA0) {
                        currentWindow().putChar(g1ToCodepoint(b));
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
                                        if (currentWindow().penCol > 0) {
                                                --currentWindow().penCol;
                                                if (currentWindow().penRow >= 0
                                                    && currentWindow().penRow
                                                               < static_cast<int>(currentWindow().grid.size())) {
                                                        auto &row = currentWindow().grid[currentWindow().penRow];
                                                        if (currentWindow().penCol >= 0
                                                            && currentWindow().penCol
                                                                       < static_cast<int>(row.size())) {
                                                                row[currentWindow().penCol] = 0;
                                                        }
                                                }
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
                                                for (size_t c = 0; c < row.size(); ++c) row[c] = 0;
                                                w.penCol = 0;
                                        }
                                        ++i;
                                        break;
                                }
                                case 0x10: // EXT1 (extension prefix — C2 / G2 follow)
                                {
                                        if (i + 1 >= size) {
                                                ++i;
                                                break;
                                        }
                                        const uint8_t ext = p[i + 1];
                                        if (ext >= 0x20 && ext <= 0x7F) {
                                                // G2 character — full table is a future task;
                                                // for now substitute with U+FFFD (replacement).
                                                currentWindow().putChar(0xFFFD);
                                                i += 2;
                                        } else if (ext >= 0xA0) {
                                                // G3 character — same treatment.
                                                currentWindow().putChar(0xFFFD);
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
                                                // C3 no-arg.
                                                i += 2;
                                        } else if (ext <= 0x8F) {
                                                // C3 reserved 1-byte payload.
                                                i += 3;
                                                if (i > size) i = size;
                                        } else if (ext <= 0x9F) {
                                                // C3 variable-length payload — spec defines this
                                                // as a 1-byte payload-length byte followed by N
                                                // bytes.  Consume defensively.
                                                if (i + 2 >= size) {
                                                        i = size;
                                                        break;
                                                }
                                                const uint8_t plen = p[i + 2];
                                                i += 3;
                                                i += plen;
                                                if (i > size) i = size;
                                        } else {
                                                // Anything else — single byte.
                                                i += 2;
                                        }
                                        break;
                                }
                                case 0x18: // P16 (next 2 bytes form a 16-bit char)
                                {
                                        if (i + 2 >= size) {
                                                i = size;
                                                break;
                                        }
                                        const uint32_t cp = (static_cast<uint32_t>(p[i + 1]) << 8)
                                                            | static_cast<uint32_t>(p[i + 2]);
                                        currentWindow().putChar(cp);
                                        i += 3;
                                        break;
                                }
                                default:
                                        // Reserved C0 — consume one byte.
                                        ++i;
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
                                        i += 2;
                                        if (i > size) i = size;
                                        break;
                                case 0x8E: // DLC — DelayCancel
                                        ++i;
                                        break;
                                case 0x8F: // RST — Reset
                                        reset();
                                        ++i;
                                        break;
                                case 0x90: // SPA — SetPenAttributes (2 args)
                                        i += 3;
                                        if (i > size) i = size;
                                        break;
                                case 0x91: // SPC — SetPenColor (3 args)
                                        i += 4;
                                        if (i > size) i = size;
                                        break;
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
                                case 0x97: // SWA — SetWindowAttributes (4 args)
                                        i += 5;
                                        if (i > size) i = size;
                                        break;
                                default: {
                                        // DF0..DF7 (0x98..0x9F): DefineWindow (6 args).
                                        if (b >= 0x98 && b <= 0x9F) {
                                                if (i + 6 >= size) {
                                                        i = size;
                                                        break;
                                                }
                                                const int     id = b - 0x98;
                                                const uint8_t b1 = p[i + 1]; // priority + col_lock + row_lock + visible
                                                const uint8_t b2 = p[i + 2]; // relative_pos + anchor_v
                                                const uint8_t b3 = p[i + 3]; // anchor_h
                                                const uint8_t b4 = p[i + 4]; // anchor_point + row_count
                                                const uint8_t b5 = p[i + 5]; // col_count
                                                const uint8_t b6 = p[i + 6]; // window_style + pen_style
                                                (void)b6;

                                                Cea708Window &w = _windows[id];
                                                const int     priority = (b1 & 0x07);
                                                const bool    colLock = (b1 & 0x10) != 0;
                                                const bool    rowLock = (b1 & 0x20) != 0;
                                                const bool    visible = (b1 & 0x40) != 0;
                                                const bool    relativePos = (b2 & 0x80) != 0;
                                                const int     anchorV = (b2 & 0x7F);
                                                const int     anchorH = b3;
                                                const int     anchorPoint = (b4 & 0xF0) >> 4;
                                                int           rowCount = (b4 & 0x0F);
                                                int           colCount = (b5 & 0x3F);
                                                // CEA-708 §8.4.6: row_count + 1 = visible rows;
                                                // col_count + 1 = visible cols.
                                                rowCount += 1;
                                                colCount += 1;
                                                w.defined = true;
                                                w.visible = visible;
                                                w.priority = priority;
                                                w.colLock = colLock;
                                                w.rowLock = rowLock;
                                                w.relativePos = relativePos;
                                                w.anchorV = anchorV;
                                                w.anchorH = anchorH;
                                                w.anchorPoint = (anchorPoint < 1) ? 1 : anchorPoint;
                                                w.resize(rowCount, colCount);
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
