/**
 * @file      mediaplay/helpformat.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include "helpformat.h"

#include <cstdlib>
#include <cstring>

using namespace promeki;

namespace mediaplay {

        int detectTerminalCols(int fallback) {
                int rows = 0;
                int cols = 0;
                if (AnsiStream::stdoutWindowSize(rows, cols) && cols > 0) {
                        return cols < 20 ? 20 : cols;
                }
                // Honor COLUMNS for non-TTY pipelines (e.g. CI logs,
                // `mediaplay --help | less`).  Bash exports it on
                // interactive shells; absent or unparseable values
                // fall through to the static fallback.
                const char *envCols = std::getenv("COLUMNS");
                if (envCols != nullptr && *envCols != '\0') {
                        char *end = nullptr;
                        long  parsed = std::strtol(envCols, &end, 10);
                        if (end != envCols && parsed > 0) {
                                if (parsed < 20) parsed = 20;
                                return static_cast<int>(parsed);
                        }
                }
                return fallback < 20 ? 20 : fallback;
        }

        bool helpUseColor() {
                // AnsiStream::stdoutSupportsANSI already handles the
                // NO_COLOR environment variable and (post-2026-05-13)
                // the stdout-isn't-a-TTY case — pipes / file redirects
                // / CI logs come back as "doesn't support ANSI" so the
                // help output drops styling without any extra work
                // here.
                return AnsiStream::stdoutSupportsANSI();
        }

        const HelpPalette &helpPalette() {
                // Single shared instance so every renderer pulls the
                // same colors.  System palette entries (0-15) keep the
                // 16-color fallback consistent — Basic terminals still
                // see the bright-cyan / yellow / green tints we expect.
                static const HelpPalette palette = {
                        AnsiStream::Cyan,    // section
                        AnsiStream::Yellow,  // option
                        AnsiStream::Cyan,    // keyName
                        AnsiStream::Magenta, // mode
                        AnsiStream::DarkGray // dim
                };
                return palette;
        }

        namespace {

                // Splits a logical line (no embedded '\n') into
                // whitespace-separated tokens, preserving each token's
                // exact characters.  Used by writeWrapped's emit loop —
                // String::split(" ") collapses runs of spaces which is
                // what we want for prose wrapping anyway.
                List<String> tokenizeLine(const String &line) {
                        return line.split(String(" "));
                }

                void emitPad(AnsiStream &stream, int n) {
                        for (int i = 0; i < n; ++i) stream << ' ';
                }

                // Writes a single logical line, wrapping after columns
                // that would exceed `wrapWidth`.  `firstLineCol` is the
                // column the cursor is at when the line starts; the
                // wrapper uses it as the budget for the first segment
                // and indents every continuation to `leftPad`.
                void writeOneLine(AnsiStream &stream, const String &line, int firstLineCol, int leftPad,
                                  int wrapWidth) {
                        List<String> tokens = tokenizeLine(line);
                        int          col = firstLineCol;
                        bool         first = true;
                        for (size_t i = 0; i < tokens.size(); ++i) {
                                const String &tok = tokens[i];
                                if (tok.isEmpty()) continue; // collapsed runs of spaces.
                                int tokLen = static_cast<int>(tok.size());
                                // The +1 accounts for the space we'd
                                // insert before the token (except on
                                // the very first token, which butts up
                                // against whatever was already on the
                                // line).
                                int needed = first ? tokLen : tokLen + 1;
                                if (!first && col + needed > wrapWidth) {
                                        stream << '\n';
                                        emitPad(stream, leftPad);
                                        col = leftPad;
                                        first = true; // re-anchor: no leading space.
                                        needed = tokLen;
                                }
                                if (!first) stream << ' ';
                                stream << tok;
                                col += needed;
                                first = false;
                        }
                }

        } // namespace

        void writeWrapped(AnsiStream &stream, const String &text, int leftPad, int wrapWidth) {
                if (wrapWidth <= leftPad) {
                        // Width too small to wrap usefully — emit text
                        // verbatim so callers at least see something
                        // instead of a column of single characters.
                        stream << text;
                        return;
                }
                // Walk the bytes manually so blank lines survive: the
                // help text is ASCII (every '\n' is a single byte) and
                // String::split collapses runs of delimiters into empty
                // skips, which would silently eat the paragraph breaks
                // option descriptions occasionally need.
                const char *raw = text.cstr();
                size_t      pos = 0;
                bool        firstLine = true;
                while (true) {
                        size_t start = pos;
                        while (raw[pos] != '\0' && raw[pos] != '\n') ++pos;
                        String line(raw + start, pos - start);
                        if (!firstLine) {
                                stream << '\n';
                                emitPad(stream, leftPad);
                        }
                        // Each logical line gets word-wrapped to
                        // `wrapWidth` independently — the start column
                        // is `leftPad` for continuation lines, and
                        // whatever the cursor happens to be at for the
                        // very first one.  We can't observe the cursor
                        // directly, so we assume the caller placed it
                        // at `leftPad` already (true for every call
                        // site in this codebase).
                        writeOneLine(stream, line, leftPad, leftPad, wrapWidth);
                        firstLine = false;
                        if (raw[pos] == '\0') break;
                        ++pos; // consume the '\n' and start the next line.
                }
        }

} // namespace mediaplay
