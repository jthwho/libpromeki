/**
 * @file      mediaplay/helpformat.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Shared formatting helpers for the mediaplay CLI's textual output —
 * terminal-width detection, color enablement, and word-wrapping
 * primitives so usage(), --list-io, and --list-config can render
 * responsively to whatever terminal they land in.
 */

#pragma once

#include <promeki/ansistream.h>
#include <promeki/list.h>
#include <promeki/string.h>

namespace mediaplay {

        /**
 * @brief Returns the column width of stdout, falling back to a default.
 *
 * Tries (in order):
 *  - @ref promeki::AnsiStream::stdoutWindowSize, when stdout is a TTY.
 *  - The @c COLUMNS environment variable, for non-TTY pipelines that
 *    still want a controlled width.
 *  - The supplied @p fallback (default 80).
 *
 * Always returns at least @c 20 — narrower than that is rare in
 * practice and producing zero-width output is worse than overflowing.
 */
        int detectTerminalCols(int fallback = 80);

        /**
 * @brief Returns true when help output should emit ANSI color.
 *
 * Thin alias for @ref promeki::AnsiStream::stdoutSupportsANSI, which
 * already honors @c NO_COLOR (see https://no-color.org/) and returns
 * @c false when stdout isn't a TTY.  When the result is @c false,
 * callers should disable the @ref promeki::AnsiStream they construct
 * so styling sequences are dropped but text still flows through.
 */
        bool helpUseColor();

        /**
 * @brief Color palette used by the mediaplay help output.
 *
 * The values are tuned for both 256-color and the basic 16-color
 * fallback (every entry maps to a system color so a Basic terminal
 * still renders sensibly).
 */
        struct HelpPalette {
                        promeki::AnsiStream::AnsiColor section; ///< Section headings (e.g. "Stages:").
                        promeki::AnsiStream::AnsiColor option;  ///< Option specs (e.g. "-s, --src <NAME>").
                        promeki::AnsiStream::AnsiColor keyName; ///< Config key names in --list-config.
                        promeki::AnsiStream::AnsiColor mode;    ///< MediaIO Mode badge in --list-io.
                        promeki::AnsiStream::AnsiColor dim;     ///< Type / Range / Def labels (secondary text).
        };

        /** @brief Returns the palette used by every mediaplay help renderer. */
        const HelpPalette &helpPalette();

        /**
 * @brief Wraps and writes @p text across multiple lines at @p wrapWidth.
 *
 * The first line is emitted at the current cursor position; subsequent
 * lines are indented with @p leftPad spaces.  Existing newlines in
 * @p text are honored (a single @c '\n' starts a new line; a blank
 * line in the input produces a blank line in the output).  Words that
 * are individually longer than the remaining width are emitted as-is
 * rather than mangled — the cost is one over-long line, the win is
 * that paths and long type names are not silently broken in the
 * middle.
 *
 * @param stream    The AnsiStream to write to.  Its enabled / disabled
 *                  state is preserved; no styling is emitted here.
 * @param text      The text to wrap.
 * @param leftPad   Indentation applied to every line @em after the
 *                  first (the first line starts wherever the caller's
 *                  cursor already is).
 * @param wrapWidth The maximum line width in columns.  A value less
 *                  than @p leftPad + 1 disables wrapping for that
 *                  invocation.
 */
        void writeWrapped(promeki::AnsiStream &stream, const promeki::String &text, int leftPad, int wrapWidth);

} // namespace mediaplay
