/**
 * @file      textwrap.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/char.h>
#include <promeki/list.h>
#include <promeki/string.h>
#include <promeki/stringlist.h>
#include <promeki/textwrap.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

        /// @brief Half-open codepoint range identifying one word in
        ///        the source @ref String.  Stored in codepoint units
        ///        (@c String::length) so width math is consistent
        ///        with how the caller measures @c maxCols.
        struct Word {
                        size_t start = 0;
                        size_t end = 0;
                        size_t len() const { return end - start; }
        };

        /// @brief Splits @p text on whitespace runs, returning one
        ///        @ref Word per non-whitespace run.  Newlines count
        ///        as whitespace.
        List<Word> tokenize(const String &text) {
                List<Word>   words;
                const size_t n = text.length();
                size_t       i = 0;
                while (i < n) {
                        while (i < n && text.charAt(i).isSpace()) ++i;
                        if (i >= n) break;
                        Word w;
                        w.start = i;
                        while (i < n && !text.charAt(i).isSpace()) ++i;
                        w.end = i;
                        words.pushToBack(w);
                }
                return words;
        }

        /// @brief Materialises the substring covered by @ref Word
        ///        @p w from @p text.
        String wordText(const String &text, const Word &w) {
                return text.substr(w.start, w.len());
        }

        /// @brief Joins @p words[lo..hi) into a single space-separated
        ///        @ref String.
        String joinWords(const String &text, const List<Word> &words, size_t lo, size_t hi) {
                String out;
                for (size_t i = lo; i < hi; ++i) {
                        if (i > lo) out += " ";
                        out += wordText(text, words[i]);
                }
                return out;
        }

        /// @brief Counts the rows produced by first-fit greedy
        ///        packing of @p wordWidths at width cap @p width.
        ///        Words longer than @p width get their own row
        ///        (counted regardless of overflow).
        size_t greedyRowCount(const List<size_t> &wordWidths, size_t width) {
                if (wordWidths.isEmpty()) return 0;
                size_t rows = 1;
                size_t cur = wordWidths[0];
                for (size_t i = 1; i < wordWidths.size(); ++i) {
                        const size_t w = wordWidths[i];
                        if (cur + 1 + w <= width) {
                                cur += 1 + w;
                        } else {
                                ++rows;
                                cur = w;
                        }
                }
                return rows;
        }

        /// @brief Returns the half-open row boundaries from greedy
        ///        first-fit packing.  See @ref TextWrap::rowBreaks
        ///        for the boundary convention.
        List<size_t> greedyBreaks(const List<size_t> &wordWidths, size_t width) {
                List<size_t> breaks;
                breaks.pushToBack(0);
                if (wordWidths.isEmpty()) return breaks;
                size_t cur = wordWidths[0];
                size_t rowStart = 0;
                for (size_t i = 1; i < wordWidths.size(); ++i) {
                        const size_t w = wordWidths[i];
                        if (cur + 1 + w <= width) {
                                cur += 1 + w;
                        } else {
                                breaks.pushToBack(i);
                                rowStart = i;
                                cur = w;
                        }
                }
                (void)rowStart;
                breaks.pushToBack(wordWidths.size());
                return breaks;
        }

        /// @brief Longest word width.  Lower bound for any feasible
        ///        max-line-width.
        size_t longestWord(const List<size_t> &wordWidths) {
                size_t longest = 0;
                for (size_t i = 0; i < wordWidths.size(); ++i) {
                        if (wordWidths[i] > longest) longest = wordWidths[i];
                }
                return longest;
        }

        /// @brief Sum of word widths + inter-word single spaces.
        ///        Upper bound for any feasible max-line-width.
        size_t totalJoinedWidth(const List<size_t> &wordWidths) {
                if (wordWidths.isEmpty()) return 0;
                size_t total = 0;
                for (size_t i = 0; i < wordWidths.size(); ++i) total += wordWidths[i];
                total += wordWidths.size() - 1;
                return total;
        }

        /// @brief Minimax balanced break-finder.  Binary-searches the
        ///        smallest row width @c L for which greedy packing
        ///        produces ≤ @p targetRows rows, then returns the
        ///        greedy breaks at that @c L.
        List<size_t> minimaxBreaks(const List<size_t> &wordWidths, size_t targetRows) {
                List<size_t> breaks;
                breaks.pushToBack(0);
                if (wordWidths.isEmpty()) return breaks;
                if (targetRows <= 1) {
                        breaks.pushToBack(wordWidths.size());
                        return breaks;
                }
                size_t lo = longestWord(wordWidths);
                size_t hi = totalJoinedWidth(wordWidths);
                if (lo == 0) lo = 1;
                if (hi < lo) hi = lo;
                while (lo < hi) {
                        const size_t mid = lo + (hi - lo) / 2;
                        if (greedyRowCount(wordWidths, mid) <= targetRows) {
                                hi = mid;
                        } else {
                                lo = mid + 1;
                        }
                }
                return greedyBreaks(wordWidths, lo);
        }

        /// @brief Splits @p text on @c '\n', normalising each
        ///        resulting row's whitespace (leading/trailing
        ///        trimmed, internal runs collapsed to single spaces).
        ///        Empty rows are dropped.
        StringList splitExplicitBreaks(const String &text) {
                StringList   out;
                const size_t n = text.length();
                size_t       i = 0;
                while (i <= n) {
                        size_t lineStart = i;
                        while (i < n && text.charAt(i) != '\n') ++i;
                        const size_t lineEnd = i;
                        String       line = text.substr(lineStart, lineEnd - lineStart);
                        List<Word>   words = tokenize(line);
                        if (!words.isEmpty()) {
                                out.pushToBack(joinWords(line, words, 0, words.size()));
                        }
                        if (i >= n) break;
                        ++i; // consume the '\n'
                }
                return out;
        }

        /// @brief Returns @c true when every row in @p rows fits in
        ///        @p maxCols codepoints.  When @p maxCols is 0 the
        ///        function returns @c true (no width constraint).
        bool allRowsFit(const StringList &rows, int maxCols) {
                if (maxCols <= 0) return true;
                const size_t cap = static_cast<size_t>(maxCols);
                for (size_t i = 0; i < rows.size(); ++i) {
                        if (rows[i].length() > cap) return false;
                }
                return true;
        }

        /// @brief Builds the per-word width list from a tokenised
        ///        @ref Word list.
        List<size_t> widthsOf(const List<Word> &words) {
                List<size_t> widths;
                widths.reserve(words.size());
                for (size_t i = 0; i < words.size(); ++i) widths.pushToBack(words[i].len());
                return widths;
        }

} // namespace

// ============================================================================
// Public API
// ============================================================================

List<size_t> TextWrap::rowBreaks(const List<size_t> &wordWidths, const Config &cfg) {
        List<size_t> breaks;
        breaks.pushToBack(0);
        if (wordWidths.isEmpty()) return breaks;

        // No width constraint: everything on one row.
        if (cfg.maxCols <= 0) {
                breaks.pushToBack(wordWidths.size());
                return breaks;
        }

        const size_t width = static_cast<size_t>(cfg.maxCols);
        if (cfg.policy == Policy::Greedy) {
                return greedyBreaks(wordWidths, width);
        }

        // Balanced: minimum rows that respect @c maxCols, then
        // binary-search for the tightest row width.
        size_t targetRows = greedyRowCount(wordWidths, width);
        if (targetRows < 1) targetRows = 1;
        return minimaxBreaks(wordWidths, targetRows);
}

StringList TextWrap::reflow(const String &text, const Config &cfg) {
        // Phase 1: explicit-break attempt (per author '\n').
        if (cfg.tryExplicitBreaks) {
                StringList rows = splitExplicitBreaks(text);
                const bool rowsOk = allRowsFit(rows, cfg.maxCols);
                const bool countOk =
                        (cfg.maxRows <= 0) || (rows.size() <= static_cast<size_t>(cfg.maxRows));
                if (!rows.isEmpty() && rowsOk && countOk) return rows;
        }

        // Phase 2: re-flow the whole text.
        List<Word> words = tokenize(text);
        if (words.isEmpty()) return StringList();

        const List<size_t> widths = widthsOf(words);
        const List<size_t> breaks = rowBreaks(widths, cfg);
        // breaks always carries at least { 0, words.size() } so it
        // describes at least one row.
        StringList out;
        for (size_t r = 0; r + 1 < breaks.size(); ++r) {
                out.pushToBack(joinWords(text, words, breaks[r], breaks[r + 1]));
        }
        return out;
}

PROMEKI_NAMESPACE_END
