/**
 * @file      scc.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdint>
#include <cstring>
#include <promeki/buffer.h>
#include <promeki/datastream.h>
#include <promeki/error.h>
#include <promeki/list.h>
#include <promeki/logger.h>
#include <promeki/result.h>
#include <promeki/scc.h>
#include <promeki/string.h>
#include <promeki/timecode.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

        bool isAsciiSpace(uint8_t c) { return c == ' ' || c == '\t'; }
        bool isAsciiDigit(uint8_t c) { return c >= '0' && c <= '9'; }
        bool isHexDigit(uint8_t c) {
                return isAsciiDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        }

        int hexVal(uint8_t c) {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
                if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
                return -1;
        }

        /// @brief Splits @p data into a list of CRLF / LF-terminated lines
        ///        (line endings stripped).  Trailing empty line preserved
        ///        when the file ends without a newline only if the trailing
        ///        text is non-empty.
        List<String> splitLines(const uint8_t *data, size_t size) {
                List<String> out;
                size_t       start = 0;
                size_t       i = 0;
                // Skip UTF-8 BOM if present.
                if (size >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) {
                        start = 3;
                        i = 3;
                }
                while (i < size) {
                        if (data[i] == '\n') {
                                size_t lineEnd = i;
                                if (lineEnd > start && data[lineEnd - 1] == '\r') --lineEnd;
                                out.pushToBack(
                                        String::fromUtf8(reinterpret_cast<const char *>(data + start),
                                                         lineEnd - start));
                                ++i;
                                start = i;
                        } else {
                                ++i;
                        }
                }
                if (start < size) {
                        // No trailing newline — append the final segment.
                        size_t lineEnd = size;
                        if (lineEnd > start && data[lineEnd - 1] == '\r') --lineEnd;
                        out.pushToBack(String::fromUtf8(reinterpret_cast<const char *>(data + start),
                                                       lineEnd - start));
                }
                return out;
        }

        /// @brief Trims ASCII whitespace from the start of @p s in place.
        void trimLeft(String &s) {
                const char *cp = s.cstr();
                size_t      n = s.byteCount();
                size_t      i = 0;
                while (i < n && isAsciiSpace(static_cast<uint8_t>(cp[i]))) ++i;
                if (i > 0) {
                        s = String::fromUtf8(cp + i, n - i);
                }
        }

        /// @brief Returns @c true when @p s is empty / whitespace-only.
        bool isBlankLine(const String &s) {
                const char *cp = s.cstr();
                size_t      n = s.byteCount();
                for (size_t i = 0; i < n; ++i) {
                        if (!isAsciiSpace(static_cast<uint8_t>(cp[i]))) return false;
                }
                return true;
        }

        /// @brief Parses a SMPTE timecode `HH:MM:SS:FF` (non-drop-frame,
        ///        `:` before FF) or `HH:MM:SS;FF` (drop-frame, `;`).
        ///        Returns @c true on success.
        bool parseSccTimecode(const char *cp, size_t n, size_t &i, Timecode &out) {
                auto readN = [&](int count, int &v) -> bool {
                        v = 0;
                        int got = 0;
                        while (got < count && i < n && isAsciiDigit(static_cast<uint8_t>(cp[i]))) {
                                v = v * 10 + (cp[i] - '0');
                                ++i;
                                ++got;
                        }
                        return got == count;
                };
                int h = 0, m = 0, s = 0, f = 0;
                if (!readN(2, h)) return false;
                if (i >= n || cp[i] != ':') return false;
                ++i;
                if (!readN(2, m)) return false;
                if (i >= n || cp[i] != ':') return false;
                ++i;
                if (!readN(2, s)) return false;
                if (i >= n) return false;
                bool dropFrame = false;
                if (cp[i] == ';') {
                        dropFrame = true;
                } else if (cp[i] != ':') {
                        return false;
                }
                ++i;
                if (!readN(2, f)) return false;

                // SCC frame rate is canonically 30 (NDF) or 29.97 (DF) — the
                // header doesn't carry a rate, so we infer from the
                // separator.
                Timecode::Mode mode = dropFrame ? Timecode::Mode(Timecode::DF30) : Timecode::Mode(Timecode::NDF30);
                out = Timecode(mode, static_cast<Timecode::DigitType>(h),
                               static_cast<Timecode::DigitType>(m), static_cast<Timecode::DigitType>(s),
                               static_cast<Timecode::DigitType>(f));
                return true;
        }

        /// @brief Formats @p tc as either `HH:MM:SS:FF` (NDF) or
        ///        `HH:MM:SS;FF` (DF) into @p out.  Invalid timecode
        ///        emits an `00:00:00:00` placeholder.
        void formatSccTimecode(const Timecode &tc, String &out) {
                int h = static_cast<int>(tc.hour());
                int m = static_cast<int>(tc.min());
                int s = static_cast<int>(tc.sec());
                int f = static_cast<int>(tc.frame());
                bool dropFrame = tc.mode().isDropFrame();
                char buf[16];
                std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d%c%02d", h, m, s,
                              dropFrame ? ';' : ':', f);
                out += buf;
        }

        /// @brief Parses one row's byte-pair list.  Expects the cursor
        ///        positioned at the first hex digit (caller skipped the
        ///        leading tab / spaces).  Returns @c true on success.
        bool parseBytePairs(const char *cp, size_t n, size_t &i, List<uint16_t> &out) {
                while (i < n) {
                        while (i < n && isAsciiSpace(static_cast<uint8_t>(cp[i]))) ++i;
                        if (i >= n) break;
                        if (!isHexDigit(static_cast<uint8_t>(cp[i]))) return false;
                        // Read 4 hex digits.
                        int digits[4] = {-1, -1, -1, -1};
                        int got = 0;
                        while (got < 4 && i < n && isHexDigit(static_cast<uint8_t>(cp[i]))) {
                                digits[got] = hexVal(static_cast<uint8_t>(cp[i]));
                                if (digits[got] < 0) return false;
                                ++i;
                                ++got;
                        }
                        if (got != 4) return false;
                        const uint16_t pair = static_cast<uint16_t>(
                                (digits[0] << 12) | (digits[1] << 8) | (digits[2] << 4) | digits[3]);
                        out.pushToBack(pair);
                }
                return true;
        }

} // namespace

// ============================================================================
// Scc — parse
// ============================================================================

Result<Scc> Scc::fromBuffer(const void *data, size_t size) {
        if (data == nullptr && size > 0) {
                return makeError<Scc>(Error(Error::Invalid));
        }
        const auto *bytes = static_cast<const uint8_t *>(data);

        List<String> lines = splitLines(bytes, size);
        // First non-blank line must be the canonical header.
        size_t cursor = 0;
        while (cursor < lines.size() && isBlankLine(lines[cursor])) ++cursor;
        if (cursor >= lines.size()) {
                // Empty file: return an empty Scc (no header required for
                // empty content).  Mirrors SubRip::parse on an empty input.
                return makeResult<Scc>(Scc());
        }
        // Trim leading whitespace + compare to header.
        String first = lines[cursor];
        trimLeft(first);
        if (!(first == Scc::HeaderString || first.startsWith(Scc::HeaderString))) {
                promekiWarn("Scc::fromBuffer: missing '%s' header", Scc::HeaderString);
                return makeError<Scc>(Error(Error::ParseFailed));
        }
        ++cursor;

        Scc out;
        for (; cursor < lines.size(); ++cursor) {
                const String &line = lines[cursor];
                if (isBlankLine(line)) continue;
                const char *cp = line.cstr();
                size_t      n = line.byteCount();
                size_t      i = 0;
                // Skip leading whitespace (some authoring tools indent).
                while (i < n && isAsciiSpace(static_cast<uint8_t>(cp[i]))) ++i;
                Scc::Line row;
                if (!parseSccTimecode(cp, n, i, row.start)) {
                        promekiWarn("Scc::fromBuffer: malformed timecode at row %zu", cursor);
                        return makeError<Scc>(Error(Error::ParseFailed));
                }
                // Expect at least one whitespace (tab) between TC and bytes.
                if (i >= n || !isAsciiSpace(static_cast<uint8_t>(cp[i]))) {
                        promekiWarn("Scc::fromBuffer: missing tab after timecode at row %zu", cursor);
                        return makeError<Scc>(Error(Error::ParseFailed));
                }
                while (i < n && isAsciiSpace(static_cast<uint8_t>(cp[i]))) ++i;
                if (!parseBytePairs(cp, n, i, row.bytePairs)) {
                        promekiWarn("Scc::fromBuffer: malformed byte pairs at row %zu", cursor);
                        return makeError<Scc>(Error(Error::ParseFailed));
                }
                out._lines.pushToBack(row);
        }
        return makeResult<Scc>(std::move(out));
}

Result<Scc> Scc::fromBuffer(const Buffer &buf) { return fromBuffer(buf.data(), buf.size()); }

Result<Scc> Scc::fromString(const String &str) { return fromBuffer(str.cstr(), str.byteCount()); }

// ============================================================================
// Scc — emit
// ============================================================================

Buffer Scc::toBuffer() const {
        String out;
        out += Scc::HeaderString;
        out += "\r\n\r\n";
        for (size_t i = 0; i < _lines.size(); ++i) {
                const Line &row = _lines[i];
                formatSccTimecode(row.start, out);
                out += "\t";
                for (size_t j = 0; j < row.bytePairs.size(); ++j) {
                        if (j > 0) out += " ";
                        const uint16_t v = row.bytePairs[j];
                        char           hex[5];
                        std::snprintf(hex, sizeof(hex), "%04x", static_cast<unsigned>(v));
                        out += hex;
                }
                out += "\r\n";
        }
        Buffer buf(out.byteCount());
        buf.setSize(out.byteCount());
        if (out.byteCount() > 0) {
                Error err = buf.copyFrom(out.cstr(), out.byteCount(), 0);
                if (err.isError()) {
                        promekiWarn("Scc::toBuffer: copyFrom failed: %s", err.name().cstr());
                }
        }
        return buf;
}

String Scc::toString() const {
        Buffer b = toBuffer();
        if (b.size() == 0) return String();
        return String::fromUtf8(static_cast<const char *>(b.data()), b.size());
}

// ============================================================================
// DataStream operators
// ============================================================================

DataStream &operator<<(DataStream &stream, const Scc &scc) {
        // Wire format mirrors the SCC text file structure:
        //   tag TypeScc, uint32 row count, then per row:
        //     uint8 H, uint8 M, uint8 S, uint8 F, uint8 dropFrame,
        //     uint32 byte-pair count, N x uint16 byte pairs.
        // Timecode's built-in DataStream operator round-trips through
        // toString/fromString and loses the libvtc Mode pointer on
        // re-parse (the rate isn't carried in the canonical "HH:MM:SS:FF"
        // form), so we serialise the digits + DF bit directly to keep
        // round-trip equality stable.
        stream.writeTag(DataStream::TypeScc);
        const Scc::LineList &lines = scc.lines();
        const uint32_t       n = static_cast<uint32_t>(lines.size());
        stream << n;
        for (size_t i = 0; i < lines.size(); ++i) {
                const Scc::Line &row = lines[i];
                const uint8_t    h = row.start.hour();
                const uint8_t    m = row.start.min();
                const uint8_t    s = row.start.sec();
                const uint8_t    f = row.start.frame();
                const uint8_t    drop = row.start.mode().isDropFrame() ? 1 : 0;
                stream << h << m << s << f << drop;
                const uint32_t bp = static_cast<uint32_t>(row.bytePairs.size());
                stream << bp;
                for (size_t j = 0; j < row.bytePairs.size(); ++j) {
                        const uint16_t v = row.bytePairs[j];
                        stream << v;
                }
        }
        return stream;
}

DataStream &operator>>(DataStream &stream, Scc &scc) {
        scc = Scc();
        if (!stream.readTag(DataStream::TypeScc)) {
                return stream;
        }
        uint32_t n = 0;
        stream >> n;
        for (uint32_t i = 0; i < n; ++i) {
                Scc::Line row;
                uint8_t   h = 0, m = 0, s = 0, f = 0, drop = 0;
                stream >> h >> m >> s >> f >> drop;
                Timecode::Mode mode = drop ? Timecode::Mode(Timecode::DF30)
                                           : Timecode::Mode(Timecode::NDF30);
                row.start = Timecode(mode, static_cast<Timecode::DigitType>(h),
                                     static_cast<Timecode::DigitType>(m),
                                     static_cast<Timecode::DigitType>(s),
                                     static_cast<Timecode::DigitType>(f));
                uint32_t bp = 0;
                stream >> bp;
                for (uint32_t j = 0; j < bp; ++j) {
                        uint16_t v = 0;
                        stream >> v;
                        row.bytePairs.pushToBack(v);
                }
                scc.lines().pushToBack(row);
        }
        return stream;
}

PROMEKI_NAMESPACE_END
