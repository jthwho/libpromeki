/**
 * @file      scc.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstdint>
#include <promeki/buffer.h>
#include <promeki/error.h>
#include <promeki/list.h>
#include <promeki/namespace.h>
#include <promeki/result.h>
#include <promeki/string.h>
#include <promeki/timecode.h>

PROMEKI_NAMESPACE_BEGIN

class DataStream;

/**
 * @brief Scenarist Closed Caption (`.scc`) file value type.
 * @ingroup proav
 *
 * Scenarist SCC is the de-facto interchange format for CEA-608
 * caption byte streams.  A real broadcast captioner emits its
 * output as a list of timecode-indexed byte-pair rows, one row
 * per video frame that carries a caption byte pair, in the
 * following text form:
 *
 * @code
 * Scenarist_SCC V1.0
 *
 * 01:00:00;00	9420 9420 947a 947a 9723 9723 4e6f 7421 942f 942f
 * 01:00:02;15	942c 942c
 * @endcode
 *
 * Each non-header row is `<timecode>\t<byte-pair> <byte-pair>...`,
 * with the timecode in either `HH:MM:SS:FF` (non-drop-frame) or
 * `HH:MM:SS;FF` (drop-frame) form, and each byte-pair a 4-hex-digit
 * uint16_t (high byte = first wire byte, low byte = second wire
 * byte).  The bytes already carry odd parity (so 'A' = 0x41 → 0xC1
 * after the high bit gets the parity stamp).
 *
 * SCC files are the canonical "what a real captioner emits" test
 * artifact.  Round-tripping them through the @ref Cea708Cdp wire
 * layer (without going through @ref Cea608Encoder) proves the CDP
 * carriage path independently of the encoder's scheduling
 * decisions — see @ref MediaConfig::TpgAncCaptionsScc.
 *
 * @par Storage and copy semantics
 *
 * Plain value type — no pimpl, no shared backing.  Copy is a deep
 * copy of the underlying @ref Line list; cheap for typical SCC
 * files (a few hundred rows at most).
 *
 * @par Variant / DataStream integration
 *
 * Registered as @c Variant::TypeScc with tag
 * @c DataStream::TypeScc (0x5E).
 *
 * @par Thread Safety
 *
 * Conditionally thread-safe.  Distinct instances may be used
 * concurrently; concurrent access to a single instance must be
 * externally synchronised.
 *
 * @see Cea708Cdp, Cea608Encoder, SubRip
 */
class Scc {
        public:
                /**
                 * @brief One row of an SCC file: a timecode anchor
                 *        plus the byte-pair list emitted at that
                 *        timecode.
                 *
                 * Each @c uint16_t carries one caption byte pair:
                 * the high byte is the first wire byte (with odd
                 * parity already stamped), the low byte is the
                 * second wire byte.  Multiple byte pairs on the same
                 * row correspond to consecutive frames starting at
                 * @ref start.
                 */
                struct Line {
                                /// @brief Anchor timecode for the first byte pair on this row.
                                Timecode start;
                                /// @brief One @c uint16_t per emitted byte pair.
                                ///        High byte = first wire byte, low byte = second.
                                List<uint16_t> bytePairs;

                                bool operator==(const Line &o) const {
                                        return start == o.start && bytePairs == o.bytePairs;
                                }
                                bool operator!=(const Line &o) const { return !(*this == o); }
                };

                using LineList = ::promeki::List<Line>;

                /// @brief Canonical file header line (without the
                ///        trailing CRLF).
                static constexpr const char *HeaderString = "Scenarist_SCC V1.0";

                /** @brief Default-constructs an empty SCC document. */
                Scc() = default;

                /** @brief Constructs from a pre-built line list. */
                explicit Scc(LineList lines) : _lines(std::move(lines)) {}

                /// @brief @c true when the document carries no rows.
                bool isEmpty() const { return _lines.isEmpty(); }

                /// @brief Number of rows.
                size_t size() const { return _lines.size(); }

                /// @brief Read-only access to the line list.
                const LineList &lines() const { return _lines; }

                /// @brief Mutable access to the line list.
                LineList &lines() { return _lines; }

                /// @brief Appends a row to the end of the line list.
                void append(const Line &line) { _lines.pushToBack(line); }

                /// @brief Clears every row.
                void clear() { _lines = LineList(); }

                bool operator==(const Scc &o) const { return _lines == o._lines; }
                bool operator!=(const Scc &o) const { return !(*this == o); }

                /**
                 * @brief Parses an SCC document from raw bytes.
                 *
                 * Accepts both CRLF and LF line endings; a UTF-8 BOM
                 * (`EF BB BF`) at the start is tolerated and skipped.
                 * Blank lines between rows are skipped.  Comments are
                 * not part of the SCC spec and are not recognised —
                 * any non-header non-blank non-data line surfaces
                 * @c Error::ParseFailed.
                 *
                 * @param data Raw byte pointer.
                 * @param size Number of bytes.
                 * @return The parsed @ref Scc on success, or
                 *         @c Error::ParseFailed when the header is
                 *         missing or a row is malformed.
                 */
                static Result<Scc> fromBuffer(const void *data, size_t size);

                /** @brief Overload accepting a @ref Buffer. */
                static Result<Scc> fromBuffer(const Buffer &buf);

                /** @brief Overload accepting a @ref String. */
                static Result<Scc> fromString(const String &str);

                /**
                 * @brief Serialises to canonical SCC bytes.
                 *
                 * Uses CRLF line endings, the canonical
                 * `Scenarist_SCC V1.0` header (followed by a blank
                 * line), the timecode in `HH:MM:SS;FF` form when the
                 * row's timecode is drop-frame and `HH:MM:SS:FF`
                 * otherwise, and lower-case 4-hex-digit byte-pair
                 * groups separated by single spaces.
                 *
                 * Rows whose @c start timecode is invalid emit an
                 * `00:00:00:00` placeholder.
                 */
                Buffer toBuffer() const;

                /** @brief Convenience: @ref toBuffer flattened to a @ref String. */
                String toString() const;

        private:
                LineList _lines;
};

/** @brief Writes an @ref Scc to a @ref DataStream. */
DataStream &operator<<(DataStream &stream, const Scc &scc);

/** @brief Reads an @ref Scc from a @ref DataStream. */
DataStream &operator>>(DataStream &stream, Scc &scc);

PROMEKI_NAMESPACE_END
