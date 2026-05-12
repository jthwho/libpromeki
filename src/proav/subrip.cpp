/**
 * @file      subrip.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <chrono>
#include <cstdint>
#include <cstring>
#include <promeki/buffer.h>
#include <promeki/color.h>
#include <promeki/enums.h>
#include <promeki/error.h>
#include <promeki/list.h>
#include <promeki/logger.h>
#include <promeki/metadata.h>
#include <promeki/rect.h>
#include <promeki/result.h>
#include <promeki/string.h>
#include <promeki/subrip.h>
#include <promeki/subtitle.h>
#include <promeki/timestamp.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

        // ASCII helpers — SubRip headers are pure ASCII; the cue text is
        // arbitrary UTF-8 but we never need to interpret it character by
        // character.  Byte-level parsing keeps the line-splitter simple
        // and resilient to malformed encodings on input.

        bool isAsciiDigit(uint8_t c) { return c >= '0' && c <= '9'; }
        bool isAsciiSpace(uint8_t c) { return c == ' ' || c == '\t'; }

        /// @brief Reads up to @p n digits from @p p starting at offset @p i.
        ///        Advances @p i past the digits.  Returns false if zero
        ///        digits were available.
        bool readDigits(const uint8_t *p, size_t n, size_t &i, size_t limit, int64_t &out) {
                size_t  start = i;
                int64_t v = 0;
                while (i < limit && (i - start) < n && isAsciiDigit(p[i])) {
                        v = v * 10 + (p[i] - '0');
                        ++i;
                }
                if (i == start) return false;
                out = v;
                return true;
        }

        void skipSpaces(const uint8_t *p, size_t &i, size_t limit) {
                while (i < limit && isAsciiSpace(p[i])) ++i;
        }

        /// @brief Converts a millisecond offset into a media-relative
        ///        @ref TimeStamp (epoch = media t=0).
        TimeStamp timeStampFromMs(int64_t totalMs) {
                // TimeStamp wraps a chrono::time_point<steady_clock>;
                // the time_point's epoch is implementation-defined but
                // we only use it as a media-relative offset, so we
                // construct one directly from a chrono::milliseconds
                // duration cast into the clock's native unit.
                using ClockDur = TimeStamp::Value::duration;
                return TimeStamp(TimeStamp::Value(std::chrono::duration_cast<ClockDur>(
                        std::chrono::milliseconds(totalMs))));
        }

        /// @brief Inverse of @ref timeStampFromMs.
        int64_t timeStampToMs(const TimeStamp &ts) {
                return std::chrono::duration_cast<std::chrono::milliseconds>(ts.value().time_since_epoch()).count();
        }

        /// @brief Parses one `HH:MM:SS,mmm` (or `HH:MM:SS.mmm`) timecode
        ///        starting at @p i.  Advances @p i past the timecode on
        ///        success.  Returns @c false if the form is malformed.
        bool parseTimecode(const uint8_t *p, size_t limit, size_t &i, TimeStamp &out) {
                int64_t h = 0, m = 0, s = 0, ms = 0;
                size_t  save = i;
                if (!readDigits(p, 4, i, limit, h)) {
                        i = save;
                        return false;
                }
                if (i >= limit || p[i] != ':') {
                        i = save;
                        return false;
                }
                ++i;
                if (!readDigits(p, 2, i, limit, m)) {
                        i = save;
                        return false;
                }
                if (i >= limit || p[i] != ':') {
                        i = save;
                        return false;
                }
                ++i;
                if (!readDigits(p, 2, i, limit, s)) {
                        i = save;
                        return false;
                }
                if (i < limit && (p[i] == ',' || p[i] == '.')) {
                        ++i;
                        if (!readDigits(p, 3, i, limit, ms)) {
                                i = save;
                                return false;
                        }
                }
                out = timeStampFromMs(h * 3600000 + m * 60000 + s * 1000 + ms);
                return true;
        }

        String formatTimecode(const TimeStamp &ts) {
                int64_t ms = timeStampToMs(ts);
                if (ms < 0) ms = 0;
                int64_t h = ms / 3600000;
                ms %= 3600000;
                int64_t m = ms / 60000;
                ms %= 60000;
                int64_t s = ms / 1000;
                ms %= 1000;
                return String::sprintf("%02lld:%02lld:%02lld,%03lld", static_cast<long long>(h),
                                       static_cast<long long>(m), static_cast<long long>(s),
                                       static_cast<long long>(ms));
        }

        struct LineCursor {
                        const uint8_t *p;
                        size_t         size;
                        size_t         pos = 0;
                        size_t         lineNumber = 0; // 1-based on next-line read.

                        bool next(size_t &outStart, size_t &outLen) {
                                if (pos >= size) return false;
                                size_t start = pos;
                                while (pos < size && p[pos] != '\n') ++pos;
                                size_t end = pos;
                                if (end > start && p[end - 1] == '\r') --end;
                                if (pos < size) ++pos;
                                outStart = start;
                                outLen = end - start;
                                ++lineNumber;
                                return true;
                        }
        };

        /// @brief Strips an ASS-style `{\anN}` prefix from @p text,
        ///        writing the recognised anchor value (1..9) to
        ///        @p outAnchor.  Returns the residual text.  When no
        ///        prefix is present @p outAnchor stays
        ///        @c SubtitleAnchor::Default and @p text is returned
        ///        unchanged.
        String stripAnchorPrefix(const String &text, SubtitleAnchor &outAnchor) {
                outAnchor = SubtitleAnchor::Default;
                const char *s = text.cstr();
                if (s == nullptr) return text;
                if (text.byteCount() < 6) return text;
                if (s[0] != '{' || s[1] != '\\' || s[2] != 'a' || s[3] != 'n') return text;
                if (s[4] < '1' || s[4] > '9') return text;
                if (s[5] != '}') return text;
                // ASS numpad anchor values 1..9 match
                // @ref SubtitleAnchor values directly, no mapping needed.
                outAnchor = SubtitleAnchor(s[4] - '0');
                return text.substr(6);
        }

        String addAnchorPrefix(const String &text, const SubtitleAnchor &anchor) {
                const int v = anchor.value();
                if (v < 1 || v > 9) return text;
                String prefix = String::sprintf("{\\an%d}", v);
                return prefix + text;
        }

        /// @brief Parses the SRT-extension @c X1:n X2:n Y1:n Y2:n
        ///        coordinate suffix.  Returns a @ref Rect2Di32 with
        ///        @c (x, y, w, h) = @c (X1, Y1, X2-X1, Y2-Y1).  Falls
        ///        back to a default-invalid @ref Rect2Di32 when the
        ///        suffix is malformed or absent.
        Rect2Di32 parseCoordinateHint(const String &hint) {
                if (hint.isEmpty()) return Rect2Di32();
                int32_t x1 = -1, x2 = -1, y1 = -1, y2 = -1;
                // Walk the string as ASCII bytes (the SRT extension is
                // always 7-bit).  Tokens are `Xn:nnn` separated by
                // spaces — simple state machine, no regex.
                const char *s = hint.cstr();
                size_t      len = hint.byteCount();
                size_t      i = 0;
                while (i < len) {
                        while (i < len && (s[i] == ' ' || s[i] == '\t')) ++i;
                        if (i + 3 > len) break;
                        char    tag = s[i];
                        char    digit = s[i + 1];
                        int32_t *target = nullptr;
                        if (tag == 'X' && digit == '1') target = &x1;
                        else if (tag == 'X' && digit == '2') target = &x2;
                        else if (tag == 'Y' && digit == '1') target = &y1;
                        else if (tag == 'Y' && digit == '2') target = &y2;
                        if (target == nullptr || s[i + 2] != ':') {
                                // Unknown token: skip to next whitespace.
                                while (i < len && s[i] != ' ' && s[i] != '\t') ++i;
                                continue;
                        }
                        i += 3;
                        int32_t v = 0;
                        bool    any = false;
                        while (i < len && s[i] >= '0' && s[i] <= '9') {
                                v = v * 10 + (s[i] - '0');
                                ++i;
                                any = true;
                        }
                        if (any) *target = v;
                }
                if (x1 < 0 || x2 < 0 || y1 < 0 || y2 < 0 || x2 < x1 || y2 < y1) return Rect2Di32();
                return Rect2Di32(x1, y1, x2 - x1, y2 - y1);
        }

        /// @brief Inverse of @ref parseCoordinateHint.  Returns the
        ///        canonical `X1:n X2:n Y1:n Y2:n` suffix string when
        ///        the region is valid; empty otherwise.
        String formatCoordinateHint(const Rect2Di32 &region) {
                if (!region.isValid()) return String();
                return String::sprintf("X1:%d X2:%d Y1:%d Y2:%d", region.x(), region.x() + region.width(),
                                       region.y(), region.y() + region.height());
        }

        // ====================================================================
        // Inline-markup parser (SRT / WebVTT-flavoured HTML)
        //
        // Recognised tags:
        //   <i>...</i>                — italic
        //   <b>...</b>                — bold
        //   <u>...</u>                — underline
        //   <font color="...">...</font> — colour (any Color::fromString form)
        //   <v Speaker>...</v>        — voice / speaker attribution
        //   <br/> or <br>             — line break (becomes literal '\n')
        //
        // Anything else (unrecognised tag or stray `<`) survives as
        // literal text — the parser is intentionally lenient.  Nesting
        // works as expected; colour overrides stack so a deeper
        // <font color=...> inside an outer one wins until its </font>.
        // ====================================================================

        /// @brief ASCII lowercase compare (case-insensitive on A-Z only).
        bool iEquals(const char *a, size_t aLen, const char *b) {
                size_t bLen = std::strlen(b);
                if (aLen != bLen) return false;
                for (size_t i = 0; i < aLen; ++i) {
                        char ca = a[i];
                        char cb = b[i];
                        if (ca >= 'A' && ca <= 'Z') ca = static_cast<char>(ca + 32);
                        if (cb >= 'A' && cb <= 'Z') cb = static_cast<char>(cb + 32);
                        if (ca != cb) return false;
                }
                return true;
        }

        bool iStartsWith(const char *a, size_t aLen, const char *prefix) {
                size_t pLen = std::strlen(prefix);
                if (aLen < pLen) return false;
                return iEquals(a, pLen, prefix);
        }

        /// @brief Splits @p data of length @p len on ASCII whitespace,
        ///        returning the first token and its length.
        void firstToken(const char *data, size_t len, size_t &outOffset, size_t &outLen) {
                size_t i = 0;
                while (i < len && (data[i] == ' ' || data[i] == '\t')) ++i;
                outOffset = i;
                while (i < len && data[i] != ' ' && data[i] != '\t') ++i;
                outLen = i - outOffset;
        }

        /// @brief Extracts the value of an `attr=value` or
        ///        `attr="value"` style attribute from inside a tag
        ///        body.  Returns an empty string if the attribute is
        ///        absent.  Case-insensitive on the attribute name.
        String extractAttr(const char *data, size_t len, const char *attrName) {
                size_t aLen = std::strlen(attrName);
                size_t i = 0;
                while (i + aLen + 1 <= len) {
                        if (iStartsWith(data + i, len - i, attrName)
                            && i + aLen < len && data[i + aLen] == '=') {
                                size_t v = i + aLen + 1;
                                char   quote = 0;
                                if (v < len && (data[v] == '"' || data[v] == '\'')) {
                                        quote = data[v];
                                        ++v;
                                }
                                size_t vEnd = v;
                                while (vEnd < len
                                       && (quote != 0 ? data[vEnd] != quote
                                                      : data[vEnd] != ' ' && data[vEnd] != '\t')) {
                                        ++vEnd;
                                }
                                return String::fromUtf8(data + v, vEnd - v);
                        }
                        ++i;
                }
                return String();
        }

        struct StyleState {
                        int                bold = 0;
                        int                italic = 0;
                        int                underline = 0;
                        Color::List colorStack;

                        SubtitleSpan makeSpan(String text) const {
                                Color c = colorStack.isEmpty() ? Color() : colorStack[colorStack.size() - 1];
                                return SubtitleSpan(std::move(text), bold > 0, italic > 0, underline > 0, c);
                        }
        };

        /// @brief Parses cue-body @p text into a list of styled spans.
        ///        Captures any `<v Speaker>` voice tag into @p outSpeaker
        ///        (first occurrence wins) and strips the voice tag from
        ///        the text.
        SubtitleSpan::List parseInlineMarkup(const String &text, String &outSpeaker) {
                SubtitleSpan::List spans;
                StyleState         state;
                String             current;
                outSpeaker = String();

                auto flush = [&]() {
                        if (current.isEmpty()) return;
                        spans.pushToBack(state.makeSpan(std::move(current)));
                        current = String();
                };

                const char  *p = text.cstr();
                const size_t n = text.byteCount();
                size_t       i = 0;
                size_t       chunkStart = 0;

                // Helper: append everything between @c chunkStart and
                // the current index as UTF-8 — necessary because the
                // input is UTF-8 and a byte-level append via
                // String::operator+=(char) re-encodes high bytes
                // through Latin-1, which corrupts multi-byte runs.
                auto flushPlainChunk = [&](size_t endOff) {
                        if (endOff > chunkStart) {
                                current += String::fromUtf8(p + chunkStart, endOff - chunkStart);
                        }
                };

                while (i < n) {
                        if (p[i] != '<') {
                                ++i;
                                continue;
                        }
                        // Find the closing '>'.  An unterminated '<'
                        // (no '>' before EOL) survives as literal text.
                        size_t end = i + 1;
                        while (end < n && p[end] != '>') ++end;
                        if (end >= n) {
                                // No closing '>' — bail out; the rest
                                // of the input is one plain chunk.
                                break;
                        }
                        const char *tagBody = p + i + 1;
                        size_t      tagLen = end - i - 1;

                        // Strip trailing '/' for self-closing void tags.
                        size_t coreLen = tagLen;
                        if (coreLen > 0 && tagBody[coreLen - 1] == '/') --coreLen;

                        // First whitespace-delimited token is the tag name.
                        size_t nameOff = 0;
                        size_t nameLen = 0;
                        firstToken(tagBody, coreLen, nameOff, nameLen);
                        const char *name = tagBody + nameOff;
                        const char *rest = tagBody + nameOff + nameLen;
                        size_t      restLen = coreLen - nameOff - nameLen;

                        // Detect whether this tag is one we recognise
                        // before flushing the plain prefix, so unknown
                        // tags can absorb the tag bytes back into the
                        // pending text run without splitting a span.
                        bool styleChange = false;
                        bool dropTag = false;

                        if (iEquals(name, nameLen, "i") || iEquals(name, nameLen, "em")
                            || iEquals(name, nameLen, "/i") || iEquals(name, nameLen, "/em")
                            || iEquals(name, nameLen, "b") || iEquals(name, nameLen, "strong")
                            || iEquals(name, nameLen, "/b") || iEquals(name, nameLen, "/strong")
                            || iEquals(name, nameLen, "u") || iEquals(name, nameLen, "/u")
                            || iEquals(name, nameLen, "font") || iEquals(name, nameLen, "/font")) {
                                styleChange = true;
                                dropTag = true;
                        } else if (iEquals(name, nameLen, "v") || iEquals(name, nameLen, "/v")
                                   || iEquals(name, nameLen, "br")) {
                                dropTag = true;
                        }

                        if (!dropTag) {
                                // Unknown tag: keep the tag bytes as
                                // literal text and continue scanning
                                // after the closing '>'.
                                i = end + 1;
                                continue;
                        }

                        // Flush the literal text up to this tag and
                        // close the current span before applying any
                        // style change.
                        flushPlainChunk(i);
                        if (styleChange) flush();
                        chunkStart = end + 1;

                        if (iEquals(name, nameLen, "i") || iEquals(name, nameLen, "em")) {
                                ++state.italic;
                        } else if (iEquals(name, nameLen, "/i") || iEquals(name, nameLen, "/em")) {
                                if (state.italic > 0) --state.italic;
                        } else if (iEquals(name, nameLen, "b") || iEquals(name, nameLen, "strong")) {
                                ++state.bold;
                        } else if (iEquals(name, nameLen, "/b") || iEquals(name, nameLen, "/strong")) {
                                if (state.bold > 0) --state.bold;
                        } else if (iEquals(name, nameLen, "u")) {
                                ++state.underline;
                        } else if (iEquals(name, nameLen, "/u")) {
                                if (state.underline > 0) --state.underline;
                        } else if (iEquals(name, nameLen, "font")) {
                                String val = extractAttr(rest, restLen, "color");
                                Color  c = val.isEmpty() ? Color() : Color::fromString(val);
                                state.colorStack.pushToBack(c);
                        } else if (iEquals(name, nameLen, "/font")) {
                                if (!state.colorStack.isEmpty()) state.colorStack.remove(state.colorStack.size() - 1);
                        } else if (iEquals(name, nameLen, "v")) {
                                // <v Speaker>... — capture speaker name
                                // (the remainder after the tag-name
                                // token, trimmed) and drop the tag.
                                size_t s = 0;
                                while (s < restLen && (rest[s] == ' ' || rest[s] == '\t')) ++s;
                                size_t e = restLen;
                                while (e > s && (rest[e - 1] == ' ' || rest[e - 1] == '\t')) --e;
                                if (outSpeaker.isEmpty() && e > s) {
                                        outSpeaker = String::fromUtf8(rest + s, e - s);
                                }
                        } else if (iEquals(name, nameLen, "br")) {
                                current += "\n";
                        }
                        // /v is silently consumed.

                        i = end + 1;
                }
                // Trailing plain chunk (post-final-tag or whole text
                // when no tags were present).
                flushPlainChunk(n);
                flush();
                if (spans.isEmpty()) spans.pushToBack(state.makeSpan(String()));
                return spans;
        }

        /// @brief Inverse of @ref parseInlineMarkup.  Emits canonical
        ///        markup wrapping each span's text in the appropriate
        ///        tags; wraps the whole cue in `<v Speaker>...</v>` when
        ///        a speaker is set.
        String emitInlineMarkup(const SubtitleSpan::List &spans, const String &speaker) {
                String body;
                for (size_t i = 0; i < spans.size(); ++i) {
                        const SubtitleSpan &s = spans[i];
                        bool hasFont = s.color().isValid();
                        if (hasFont) {
                                // SubRip's <font> tag expects an HTML-style
                                // hex colour.  Include alpha only when it
                                // isn't fully opaque — keeps the canonical
                                // round-trip compact for the (overwhelmingly
                                // common) opaque-colour case.
                                const bool includeAlpha = s.color().a() < 1.0f;
                                body += "<font color=\"";
                                body += s.color().toHex(includeAlpha);
                                body += "\">";
                        }
                        if (s.bold()) body += "<b>";
                        if (s.italic()) body += "<i>";
                        if (s.underline()) body += "<u>";
                        body += s.text();
                        if (s.underline()) body += "</u>";
                        if (s.italic()) body += "</i>";
                        if (s.bold()) body += "</b>";
                        if (hasFont) body += "</font>";
                }
                if (!speaker.isEmpty()) {
                        String wrapped = "<v ";
                        wrapped += speaker;
                        wrapped += ">";
                        wrapped += body;
                        wrapped += "</v>";
                        return wrapped;
                }
                return body;
        }

} // namespace

// ============================================================================
// Parse
// ============================================================================

Result<SubtitleList> SubRip::parse(const void *data, size_t size) {
        SubtitleList out;
        if (data == nullptr || size == 0) return makeResult<SubtitleList>(std::move(out));
        const uint8_t *p = static_cast<const uint8_t *>(data);

        // Skip UTF-8 BOM.
        size_t startOff = 0;
        if (size >= 3 && p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF) startOff = 3;

        LineCursor cur{p + startOff, size - startOff};

        while (true) {
                size_t lineStart = 0;
                size_t lineLen = 0;

                // Skip leading blank lines between cues.
                bool gotLine = false;
                while ((gotLine = cur.next(lineStart, lineLen))) {
                        if (lineLen > 0) {
                                size_t end = lineLen;
                                while (end > 0 && isAsciiSpace(cur.p[lineStart + end - 1])) --end;
                                if (end > 0) {
                                        lineLen = end;
                                        break;
                                }
                        }
                }
                if (!gotLine) break;

                // Line 1: optional 1-based sequence number.  Real files
                // sometimes omit it; if the line is all digits we treat
                // it as the seq line and pull the next line as the
                // timecode; otherwise we fall through and treat *this*
                // line as the timecode.
                size_t numberLine = cur.lineNumber;
                {
                        size_t  i = 0;
                        size_t  lim = lineLen;
                        int64_t seq = 0;
                        if (readDigits(cur.p + lineStart, lineLen, i, lim, seq) && i == lim) {
                                if (!cur.next(lineStart, lineLen)) {
                                        return makeError<SubtitleList>(Error::ParseFailed);
                                }
                                while (lineLen == 0 && cur.next(lineStart, lineLen)) {
                                }
                                if (lineLen == 0) {
                                        return makeError<SubtitleList>(Error::ParseFailed);
                                }
                                size_t end = lineLen;
                                while (end > 0 && isAsciiSpace(cur.p[lineStart + end - 1])) --end;
                                lineLen = end;
                                numberLine = cur.lineNumber;
                                (void) seq;
                        }
                }

                // Line 2 (or line 1): `HH:MM:SS,mmm --> HH:MM:SS,mmm [X1:..]`.
                TimeStamp cueStart;
                TimeStamp cueEnd;
                Rect2Di32 region;
                {
                        size_t i = 0;
                        size_t lim = lineLen;
                        skipSpaces(cur.p + lineStart, i, lim);
                        if (!parseTimecode(cur.p + lineStart, lim, i, cueStart)) {
                                promekiWarn("SubRip: malformed start timecode at line %zu", numberLine);
                                return makeError<SubtitleList>(Error::ParseFailed);
                        }
                        skipSpaces(cur.p + lineStart, i, lim);
                        if (i + 3 > lim || cur.p[lineStart + i] != '-' || cur.p[lineStart + i + 1] != '-'
                            || cur.p[lineStart + i + 2] != '>') {
                                promekiWarn("SubRip: missing '-->' separator at line %zu", numberLine);
                                return makeError<SubtitleList>(Error::ParseFailed);
                        }
                        i += 3;
                        skipSpaces(cur.p + lineStart, i, lim);
                        if (!parseTimecode(cur.p + lineStart, lim, i, cueEnd)) {
                                promekiWarn("SubRip: malformed end timecode at line %zu", numberLine);
                                return makeError<SubtitleList>(Error::ParseFailed);
                        }
                        skipSpaces(cur.p + lineStart, i, lim);
                        if (i < lim) {
                                String hint = String::fromUtf8(
                                        reinterpret_cast<const char *>(cur.p + lineStart + i), lim - i);
                                region = parseCoordinateHint(hint);
                        }
                }

                // Remaining lines until blank: cue text.
                String rawText;
                bool   first = true;
                while (cur.next(lineStart, lineLen)) {
                        while (lineLen > 0 && isAsciiSpace(cur.p[lineStart + lineLen - 1])) --lineLen;
                        if (lineLen == 0) break;
                        String line = String::fromUtf8(reinterpret_cast<const char *>(cur.p + lineStart), lineLen);
                        if (!first) rawText += "\n";
                        rawText += line;
                        first = false;
                }
                SubtitleAnchor anchor = SubtitleAnchor::Default;
                String         body = stripAnchorPrefix(rawText, anchor);
                String         speaker;
                SubtitleSpan::List spans = parseInlineMarkup(body, speaker);
                out.append(Subtitle(cueStart, cueEnd, std::move(spans), anchor, region, std::move(speaker),
                                    Metadata()));
        }
        return makeResult<SubtitleList>(std::move(out));
}

Result<SubtitleList> SubRip::parse(const Buffer &buf) { return parse(buf.data(), buf.size()); }

Result<SubtitleList> SubRip::parse(const String &str) { return parse(str.cstr(), str.byteCount()); }

// ============================================================================
// Emit
// ============================================================================

Buffer SubRip::emit(const SubtitleList &list) {
        // Compose into a String first; SubRip files are small enough
        // that a single linear append is the right tradeoff.
        String      out;
        const auto &entries = list.entries();
        for (size_t i = 0; i < entries.size(); ++i) {
                const Subtitle &s = entries[i];
                out += String::number(static_cast<int>(i + 1));
                out += "\r\n";
                out += formatTimecode(s.start());
                out += " --> ";
                out += formatTimecode(s.end());
                String hint = formatCoordinateHint(s.region());
                if (!hint.isEmpty()) {
                        out += " ";
                        out += hint;
                }
                out += "\r\n";
                String markup = emitInlineMarkup(s.spans(), s.speaker());
                String body = addAnchorPrefix(markup, s.anchor());
                // Convert literal '\n' in the cue text to CRLF on the
                // wire.  Drop stray '\r' (the LF in the same position
                // expands to CRLF).
                const char *bp = body.cstr();
                size_t      blen = body.byteCount();
                for (size_t k = 0; k < blen; ++k) {
                        if (bp[k] == '\n') {
                                out += "\r\n";
                        } else if (bp[k] == '\r') {
                                // Skip.
                        } else {
                                char tmp[2] = {bp[k], 0};
                                out += tmp;
                        }
                }
                out += "\r\n\r\n";
        }
        Buffer buf(out.byteCount());
        buf.setSize(out.byteCount());
        if (out.byteCount() > 0) {
                Error err = buf.copyFrom(out.cstr(), out.byteCount(), 0);
                if (err.isError()) {
                        promekiWarn("SubRip::emit: copyFrom failed: %s", err.name().cstr());
                }
        }
        return buf;
}

String SubRip::emitString(const SubtitleList &list) {
        Buffer b = emit(list);
        if (b.size() == 0) return String();
        return String::fromUtf8(static_cast<const char *>(b.data()), b.size());
}

PROMEKI_NAMESPACE_END
