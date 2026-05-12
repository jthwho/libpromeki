/**
 * @file      subrip.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstdint>
#include <promeki/buffer.h>
#include <promeki/error.h>
#include <promeki/namespace.h>
#include <promeki/result.h>
#include <promeki/string.h>
#include <promeki/subtitle.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief SubRip (`.srt`) subtitle file parser / emitter.
 * @ingroup proav
 *
 * Stateless format helper that converts between SubRip byte streams
 * and the format-agnostic @ref SubtitleList value type.  Sits in the
 * same row as future @c Scc and @c WebVtt helpers — all subtitle file
 * formats share this shape: a single @ref parse method that takes
 * bytes and returns a @ref SubtitleList, and a single @ref emit
 * method that takes a @ref SubtitleList and returns canonical bytes.
 *
 * @par File format (de-facto SubRip spec)
 *
 * @code
 * 1
 * 00:00:12,345 --> 00:00:15,678
 * Hello, world.
 *
 * 2
 * 00:00:18,000 --> 00:00:20,500 X1:040 X2:600 Y1:050 Y2:080
 * Multi-line cue
 * second line goes here.
 *
 * 3
 * 00:00:21,000 --> 00:00:24,000
 * {\an8}Top-anchored cue
 * @endcode
 *
 * Each cue is four parts separated by line endings: a 1-based sequence
 * index (which @ref parse tolerates as missing — some authoring tools
 * omit it), a `start --> end` timecode pair in `HH:MM:SS,mmm` form
 * (`,` is the spec separator; `.` is also accepted for WebVTT-mis-
 * labelled-as-SRT files), one or more lines of cue text, and a
 * blank-line terminator.  Both CRLF and LF line endings are accepted
 * by @ref parse; the canonical @ref emit uses CRLF.
 *
 * @par Positioning anchors
 *
 *  - **Coordinate hints** (`X1:nnn X2:nnn Y1:nnn Y2:nnn`) on the
 *    timecode line — informally specifying a bounding box in raster
 *    coordinates.  Stored on @ref Subtitle::positionHint.
 *  - **ASS-style numpad anchors** (`{\an1}` ... `{\an9}`) at the
 *    start of the cue text.  Mapped to @ref Subtitle::position
 *    (1 = bottom-left, 5 = center, 9 = top-right; -1 when absent).
 *
 * Both round-trip byte-stable through @ref parse + @ref emit.
 *
 * @par Class shape
 *
 * Static-only — never instantiated.  Holds no state; thread-safe by
 * construction.
 *
 * @see Subtitle, SubtitleList, Scc, Cea608Encoder
 */
class SubRip {
        public:
                /**
                 * @brief Parses a SubRip document from raw bytes.
                 *
                 * Accepts both CRLF and LF line endings; mixed endings
                 * in the same file parse fine.  Empty input parses to
                 * an empty @ref SubtitleList with no error.
                 *
                 * The returned list is left in the order cues appeared
                 * in the file (which is normally chronological for
                 * well-formed files); call @ref SubtitleList::sortByStart
                 * to force ascending order if a file isn't sorted.
                 *
                 * @param data Raw byte pointer.
                 * @param size Number of bytes.
                 * @return The parsed @ref SubtitleList on success, or
                 *         @c Error::ParseFailed when a cue's timecode
                 *         line is malformed.
                 */
                static Result<SubtitleList> parse(const void *data, size_t size);

                /** @brief Overload accepting a @ref Buffer. */
                static Result<SubtitleList> parse(const Buffer &buf);

                /** @brief Overload accepting a @ref String. */
                static Result<SubtitleList> parse(const String &str);

                /**
                 * @brief Serialises a @ref SubtitleList to canonical
                 *        SubRip bytes.
                 *
                 * Uses CRLF line endings, 1-based sequence numbering
                 * (always starts at 1), and the canonical
                 * `HH:MM:SS,mmm --> HH:MM:SS,mmm` timecode form.
                 * Re-stamps ASS anchor prefixes on the text field and
                 * the X/Y coordinate suffix on the timecode line so
                 * that round-tripping a parsed file reproduces the
                 * same content byte-for-byte (modulo sequence-number
                 * renumbering).
                 */
                static Buffer emit(const SubtitleList &list);

                /** @brief Convenience: @ref emit flattened to a @ref String. */
                static String emitString(const SubtitleList &list);

                SubRip() = delete; // Static-only utility class.
};

PROMEKI_NAMESPACE_END
