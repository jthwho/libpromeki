/**
 * @file      textwrap.h
 * @copyright Jason Howard. All rights reserved.
 * @ingroup strings
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_CORE
#include <cstddef>
#include <promeki/list.h>
#include <promeki/namespace.h>
#include <promeki/stringlist.h>

PROMEKI_NAMESPACE_BEGIN

class String;

/**
 * @brief General-purpose text reflow / word-wrap utility.
 * @ingroup strings
 *
 * Splits a free-form @ref String into a @ref StringList of rows
 * laid out for display in a fixed-width region (terminal, caption
 * grid, log column, UI label).  Two policies are offered:
 *
 *   - @c Policy::Greedy — classic first-fit.  Pack as many words
 *     as possible on each row.  Fastest; tends to leave a short
 *     trailing row.
 *   - @c Policy::Balanced — minimax: minimize the longest row's
 *     width subject to the row-count produced by greedy at
 *     @ref Config::maxCols.  Visually balanced; what you want
 *     for captions, UI labels, etc.
 *
 * Word boundaries are runs of non-whitespace.  Whitespace runs
 * (spaces, tabs, line feeds, etc.) collapse to a single inter-word
 * separator.  Words longer than @ref Config::maxCols are not
 * broken — they ride on their own row and exceed @c maxCols.
 *
 * @par Honouring explicit line breaks
 *
 * With @ref Config::tryExplicitBreaks set, @c '\n' boundaries in
 * the input are tried as the layout first.  If every resulting row
 * fits in @c maxCols *and* the total row count is within
 * @c maxRows, that layout is returned verbatim.  Otherwise the
 * @c '\n's collapse to spaces and the policy runs over the whole
 * text.  This matches the intent of authoring tools (SubRip,
 * comment blocks) that mark intended line breaks but produce
 * weird-looking output if some lines fit and others get re-flowed
 * piecemeal.
 *
 * @par Row count overflow
 *
 * @ref Config::maxRows is a *soft target*.  When the text genuinely
 * needs more rows than the cap (because some word is longer than
 * @c maxCols, or the text is too long to balance within
 * @c maxRows rows), @ref reflow returns the minimum row count
 * needed to fit each row in @c maxCols and lets the caller decide
 * how to react (auto-split into time-displaced sub-cues, warn the
 * user, truncate, etc.).  Callers should check @c result.size()
 * against @c cfg.maxRows after the call.
 *
 * @par Column counting
 *
 * Row width is measured in @ref String::length() — codepoint count,
 * not bytes.  For ASCII this is identical to byte count; for
 * UTF-8 multibyte text it matches the visual column count assuming
 * each codepoint is one display column (true for basic Latin /
 * digits / punctuation, false for CJK or combining marks; the
 * caller picks @c maxCols accordingly).
 *
 * @par Examples
 *
 * Greedy:
 * @code
 *   TextWrap::Config cfg;
 *   cfg.maxCols = 16;
 *   cfg.policy  = TextWrap::Policy::Greedy;
 *   StringList rows = TextWrap::reflow("Hello world this is a test", cfg);
 *   // → ["Hello world this", "is a test"]
 * @endcode
 *
 * Balanced (minimax):
 * @code
 *   TextWrap::Config cfg;
 *   cfg.maxCols = 32;
 *   cfg.policy  = TextWrap::Policy::Balanced;
 *   StringList rows = TextWrap::reflow("Hello world this is a test sentence", cfg);
 *   // → ["Hello world this", "is a test sentence"]  (balanced 16/18)
 *   // (Greedy at maxCols=32 would yield ["Hello world this is a test", "sentence"].)
 * @endcode
 *
 * tryExplicitBreaks:
 * @code
 *   TextWrap::Config cfg;
 *   cfg.maxCols = 32;
 *   cfg.maxRows = 3;
 *   cfg.tryExplicitBreaks = true;
 *   StringList rows = TextWrap::reflow("Line one\nLine two", cfg);
 *   // → ["Line one", "Line two"]  (both ≤ 32, total ≤ 3 → honored)
 *   StringList rows2 = TextWrap::reflow(
 *       "Short line\nThis second line is far longer than thirty-two columns",
 *       cfg);
 *   // → re-flow ignoring '\n' since one explicit row overflowed.
 * @endcode
 *
 * @par Thread Safety
 *
 * Stateless and re-entrant.  @ref reflow is a pure function of
 * its inputs.
 */
class TextWrap {
        public:
                /// @brief Layout algorithm selector.
                enum class Policy {
                        Greedy,   ///< First-fit: pack maximally per row.
                        Balanced, ///< Minimax: minimize the longest row's width.
                };

                /** @brief Reflow configuration. */
                struct Config {
                                /// @brief Hard width cap per row, in
                                ///        codepoints.  0 = no width limit
                                ///        (returns the whole text on one
                                ///        row, whitespace-normalised).
                                int maxCols = 0;

                                /// @brief Soft row-count target.  Used by
                                ///        @ref tryExplicitBreaks to accept
                                ///        the explicit-break layout, and
                                ///        as a diagnostic the caller can
                                ///        check after the call.  0 = no
                                ///        row preference.
                                int maxRows = 0;

                                /// @brief Layout policy.  Defaults to
                                ///        @c Balanced for visually
                                ///        balanced rows.
                                Policy policy = Policy::Balanced;

                                /// @brief When @c true, the input's
                                ///        @c '\n' boundaries are tried as
                                ///        the layout first.  If every
                                ///        resulting row fits in @c maxCols
                                ///        *and* total rows ≤ @c maxRows,
                                ///        that layout wins.  Otherwise
                                ///        @c '\n's collapse to spaces and
                                ///        the policy runs over the whole
                                ///        text.
                                bool tryExplicitBreaks = false;
                };

                /**
                 * @brief Reflows @p text into a @ref StringList of rows.
                 *
                 * @param text  Source text.  Whitespace is normalised to
                 *              single spaces (except for @c '\n', which
                 *              is honoured first when
                 *              @ref Config::tryExplicitBreaks is set).
                 * @param cfg   Reflow configuration.
                 * @return One @ref String per row.  Empty when @p text is
                 *         empty or all-whitespace.  Returned row count
                 *         may exceed @ref Config::maxRows when the text
                 *         genuinely cannot fit (callers should check).
                 */
                static StringList reflow(const String &text, const Config &cfg);

                /**
                 * @brief Lower-level word-distribution primitive.
                 *
                 * Given the codepoint widths of N atomic words (in
                 * the order they appear in the source), returns
                 * @c R+1 indices delimiting @c R rows.  The first
                 * index is always 0; subsequent indices are the
                 * first-word index of each new row; the last index is
                 * @c N.  Row @c r covers words
                 * @c [result[r], result[r+1]).
                 *
                 * @ref Config::tryExplicitBreaks is *not* honoured by
                 * this overload (there is no @ref String to split on
                 * @c '\n').  Callers that want to honour author breaks
                 * should split their word list themselves and call
                 * this function per segment, or call @ref reflow.
                 *
                 * @par Use cases
                 *
                 * Useful when the caller carries per-word metadata
                 * that must survive the wrap — for example styled
                 * runs in caption authoring (italic / colour / bold)
                 * or ANSI-coloured terminal output.  Tokenise on
                 * whitespace, run @ref rowBreaks over the resulting
                 * widths, then slice the styled data back by the
                 * returned indices.
                 *
                 * @param wordWidths  Codepoint widths of each word.
                 *                    Empty input returns @c {0}.
                 * @param cfg         Same width / row constraints as
                 *                    @ref reflow.
                 * @return Half-open row boundaries (size = rowCount + 1).
                 */
                static List<size_t> rowBreaks(const List<size_t> &wordWidths, const Config &cfg);
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_CORE
