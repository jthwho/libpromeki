/**
 * @file      cea608encoder.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <promeki/cea608.h>
#include <promeki/cea608encoder.h>
#include <promeki/cea708cdp.h>
#include <promeki/color.h>
#include <promeki/enums.h>
#include <promeki/error.h>
#include <promeki/framenumber.h>
#include <promeki/framerate.h>
#include <promeki/logger.h>
#include <promeki/map.h>
#include <promeki/sharedptr.h>
#include <promeki/string.h>
#include <promeki/subtitle.h>
#include <promeki/textwrap.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

        /// @brief Scheduled byte-pair to emit on a specific frame.
        ///        Pre-parity values — the encoder stamps parity at
        ///        emit time so the wire bytes always have odd parity.
        struct ScheduledPair {
                        uint8_t b1 = Cea608::NullB1;
                        uint8_t b2 = Cea608::NullB2;
        };

        /// @brief Converts a media-relative @ref TimeStamp (epoch =
        ///        media t=0) into milliseconds since epoch.
        int64_t timeStampToMs(const TimeStamp &ts) {
                return std::chrono::duration_cast<std::chrono::milliseconds>(ts.value().time_since_epoch()).count();
        }

        /// @brief Rounds a media-relative @ref TimeStamp to the nearest
        ///        frame index at the given @ref FrameRate.  Returns
        ///        the integer frame number.
        int64_t timeStampToFrame(const TimeStamp &ts, const FrameRate &fps) {
                if (!fps.isValid()) return 0;
                const int64_t ms = timeStampToMs(ts);
                const int64_t num = static_cast<int64_t>(fps.numerator());
                const int64_t den = static_cast<int64_t>(fps.denominator());
                // frame = ms * fps / 1000 = ms * num / (1000 * den).
                // Round to nearest using "+ half-divisor" before division.
                const int64_t denom = 1000 * den;
                const int64_t numer = ms * num;
                if (numer >= 0) {
                        return (numer + denom / 2) / denom;
                }
                return -((-numer + denom / 2) / denom);
        }

        /// @brief Encodes the displayed text into 2-byte CEA-608
        ///        character pairs (pre-parity).  Drops non-basic
        ///        characters (substituted with 0x20 space).
        ///        Multi-line cues are flattened with a space between
        ///        lines — v1 does not emit row-switching PACs.
        List<ScheduledPair> encodeCharPairs(const String &text) {
                List<ScheduledPair> out;
                // Walk the UTF-8 bytes; substitute non-ASCII / control
                // chars with 0x20.  Multi-line ('\n') → space.
                const char *p = text.cstr();
                size_t      n = text.byteCount();
                ScheduledPair pending;
                bool          half = false;
                for (size_t i = 0; i < n; ++i) {
                        uint8_t raw = static_cast<uint8_t>(p[i]);
                        uint8_t ch;
                        if (raw == '\n' || raw == '\r' || raw == '\t') {
                                ch = 0x20;
                        } else if (raw >= 0x20 && raw <= 0x7E) {
                                ch = raw;
                        } else {
                                // High-bit / UTF-8 continuation / control:
                                // substitute with space.  Full charset
                                // mapping is Phase 3.5d.
                                ch = 0x20;
                        }
                        if (!half) {
                                pending.b1 = ch;
                                half = true;
                        } else {
                                pending.b2 = ch;
                                out.pushToBack(pending);
                                pending = ScheduledPair{};
                                half = false;
                        }
                }
                if (half) {
                        // Odd character count: pad the last pair with
                        // space so the byte cadence stays even.
                        pending.b2 = 0x20;
                        out.pushToBack(pending);
                }
                return out;
        }

        /// @brief Returns the CDP @c cc_type for the configured
        ///        channel.  Field-1 channels (CC1/CC2) map to
        ///        cc_type 0; field-2 channels (CC3/CC4) map to
        ///        cc_type 1.  The intra-field channel selector
        ///        (CC1 vs CC2 within field 1) is encoded in the
        ///        byte-pair itself, not in cc_type.
        uint8_t ccTypeForChannel(Cea608Encoder::Channel ch) {
                switch (ch) {
                        case Cea608Encoder::Channel::CC1:
                        case Cea608Encoder::Channel::CC2: return 0;
                        case Cea608Encoder::Channel::CC3:
                        case Cea608Encoder::Channel::CC4: return 1;
                }
                return 0;
        }

        /// @brief Maps a @ref SubtitleAnchor + row count to the
        ///        ordered list of CEA-608 rows the layout occupies.
        ///        608 only carries the vertical position (15 rows),
        ///        so horizontal placement (Left / Center / Right) is
        ///        dropped at the wire.
        ///
        ///   Top* anchor    → rows 1..N
        ///   Middle* anchor → rows centred on 8 (N=1: 8;
        ///                                       N=2: 7,8;
        ///                                       N=3: 7,8,9;
        ///                                       N=4: 6,7,8,9)
        ///   Bottom* / Default → rows (16-N)..15 (broadcast convention)
        ///
        ///   The returned list always has exactly @p N entries with
        ///   row indices clamped to [1, 15].
        List<int> rowsForAnchor(const SubtitleAnchor &a, int N) {
                List<int> out;
                if (N < 1) return out;
                const int  v = a.value();
                const bool isTop = (v == SubtitleAnchor::TopLeft.value()
                                    || v == SubtitleAnchor::TopCenter.value()
                                    || v == SubtitleAnchor::TopRight.value());
                const bool isMid = (v == SubtitleAnchor::MiddleLeft.value()
                                    || v == SubtitleAnchor::MiddleCenter.value()
                                    || v == SubtitleAnchor::MiddleRight.value());
                int top;
                if (isTop) {
                        top = 1;
                } else if (isMid) {
                        // Anchor on row 8: N=1→[8], N=2→[7,8], N=3→[7,8,9],
                        // N=4→[6,7,8,9].  Bias upward (broadcast convention).
                        top = 8 - N / 2;
                } else {
                        // Default / Bottom* anchor.
                        top = 16 - N;
                }
                if (top < 1) top = 1;
                if (top + N - 1 > 15) top = 15 - N + 1;
                if (top < 1) top = 1; // clamp again for N > 15
                for (int i = 0; i < N; ++i) out.pushToBack(top + i);
                return out;
        }

        /// @brief Single-row anchor row (back-compat for paint-on +
        ///        roll-up which v1 flattens to one row).
        int rowFromAnchor(const SubtitleAnchor &a) {
                List<int> rows = rowsForAnchor(a, 1);
                return rows.isEmpty() ? 15 : rows[0];
        }

        /// @brief Quantises a span's foreground @ref Color to one of
        ///        the seven CEA-608 primary colours.  Invalid colour
        ///        falls back to white.
        Cea608::CaptionColor quantiseColor(const Color &c) {
                if (!c.isValid()) return Cea608::CaptionColor::White;
                const size_t idx = c.nearestPaletteIndex(Cea608::palette());
                if (idx >= Cea608::CaptionColorCount) return Cea608::CaptionColor::White;
                return static_cast<Cea608::CaptionColor>(idx);
        }

        /// @brief Tuple identifying a styled-run's wire state.  The
        ///        encoder emits a mid-row code whenever the
        ///        fg / italic / underline triple changes, and a
        ///        background-attribute code whenever the bg slot
        ///        (@c hasBg + colour + opacity) changes.  Both fire
        ///        independently — a span that flips bg colour without
        ///        touching fg gets a single doubled BG pair rather
        ///        than a full mid-row.
        struct WireStyle {
                        Cea608::CaptionColor color = Cea608::CaptionColor::White;
                        bool                 italic = false;
                        bool                 underline = false;
                        bool                 hasBg = false;
                        Cea608::CaptionColor bgColor = Cea608::CaptionColor::White;
                        bool                 bgSemiTransparent = false;

                        bool operator==(const WireStyle &o) const {
                                return color == o.color && italic == o.italic
                                       && underline == o.underline && hasBg == o.hasBg
                                       && (!hasBg || (bgColor == o.bgColor
                                                       && bgSemiTransparent == o.bgSemiTransparent));
                        }
                        bool operator!=(const WireStyle &o) const { return !(*this == o); }

                        /// @brief @c true when only the foreground triple
                        ///        (colour / italic / underline) differs
                        ///        between @c *this and @p o.  Lets the
                        ///        emitter skip the BG byte pair when the
                        ///        bg slot is unchanged.
                        bool fgDiffers(const WireStyle &o) const {
                                return color != o.color || italic != o.italic
                                       || underline != o.underline;
                        }
                        bool bgDiffers(const WireStyle &o) const {
                                return hasBg != o.hasBg
                                       || (hasBg && (bgColor != o.bgColor
                                                      || bgSemiTransparent != o.bgSemiTransparent));
                        }
        };

        /// @brief Derives the wire style from a @ref SubtitleSpan.
        ///        Italic forces white at the wire level (608
        ///        limitation); the caller emits one-shot bold
        ///        warnings separately.
        WireStyle wireStyleFor(const SubtitleSpan &span) {
                WireStyle ws;
                ws.italic = span.italic();
                ws.underline = span.underline();
                ws.color = ws.italic ? Cea608::CaptionColor::White : quantiseColor(span.color());
                if (span.backgroundColor().isValid()) {
                        ws.hasBg = true;
                        ws.bgColor = quantiseColor(span.backgroundColor());
                        ws.bgSemiTransparent =
                                span.backgroundOpacity().value() == SubtitleOpacity::Translucent.value();
                }
                return ws;
        }

        /// @brief Body byte-pair plan for one cue: every byte-pair
        ///        from the first @c PAC double through the last
        ///        character pair (no RCL, no EOC, no EDM).
        ///
        /// Single-row layout:
        ///   [PAC, PAC, (MR, MR,)? chars..., (MR, MR,)? chars..., ...]
        ///
        /// Multi-row layout (one PAC pair per physical 608 row):
        ///   [PAC0, PAC0, chars0..., PAC1, PAC1, chars1..., ...]
        ///
        /// The returned list's @c size() is the schedule's per-cue
        /// body length excluding the 2 RCL frames at the front and
        /// the 2 EOC frames at the end.  Total per-cue pre-roll
        /// frames (pop-on) = 2 (RCL) + body.size().
        struct CueBody {
                        List<ScheduledPair> bytes;     ///< Pre-parity (b1,b2) pairs to emit, frame-by-frame.
                        bool                hadBold;   ///< @c true when any span requested bold (warn flag).
        };

        /// @brief Per-row span entry in a wrap layout result.  Row
        ///        indices are assigned by the caller via
        ///        @ref rowsForAnchor; only the styled spans are
        ///        produced here.  The actual wrap algorithm
        ///        (tokenise + explicit-break attempt + balanced
        ///        minimax re-flow) lives on @ref Subtitle::wrapped
        ///        so the CEA-708 encoder shares the exact same
        ///        layout.
        using WrapRowSpans = SubtitleSpan::List;

        /// @brief Adapter: calls @ref Subtitle::wrapped and converts
        ///        the resulting "spans interleaved with `\\n`
        ///        separators" representation back into the per-row
        ///        @ref WrapRowSpans list this codec's scheduler code
        ///        consumes.  Each `\\n` span boundary starts a new
        ///        row; styled spans on a row are appended in order.
        List<WrapRowSpans> wrapStyledCue(const Subtitle &cue, int maxCols, int maxRows) {
                List<WrapRowSpans> out;
                Subtitle           w = cue.wrapped(maxCols, maxRows);
                const SubtitleSpan::List &spans = w.spans();
                WrapRowSpans              current;
                for (size_t i = 0; i < spans.size(); ++i) {
                        const SubtitleSpan &sp = spans[i];
                        if (sp.text() == "\n") {
                                if (!current.isEmpty()) {
                                        out.pushToBack(current);
                                        current = WrapRowSpans();
                                }
                                continue;
                        }
                        current.pushToBack(sp);
                }
                if (!current.isEmpty()) out.pushToBack(current);
                return out;
        }

        /// @brief Returns @c true when any @ref SubtitleSpan in @p layout
        ///        flags bold.  Used by the schedule builders to drop a
        ///        one-shot warning about CEA-608's missing bold support.
        bool layoutHadBold(const List<WrapRowSpans> &layout) {
                for (size_t r = 0; r < layout.size(); ++r) {
                        const WrapRowSpans &spans = layout[r];
                        for (size_t i = 0; i < spans.size(); ++i) {
                                if (spans[i].bold()) return true;
                        }
                }
                return false;
        }

        /// @brief One physical 608 row after wrap, with its assigned
        ///        row index (1..15) and styled span list.
        struct LaidOutRow {
                        int          row = 15;
                        WrapRowSpans spans;
        };

        /// @brief Re-applies row indices to a sub-slice of a wrap
        ///        layout (used by the cue auto-split path so each
        ///        sub-cue gets an anchor-relative row group of its
        ///        own size).
        List<LaidOutRow> placeChunk(const List<WrapRowSpans> &wrap, size_t lo, size_t hi,
                                     const SubtitleAnchor &anchor) {
                List<LaidOutRow> placed;
                if (hi > wrap.size()) hi = wrap.size();
                if (lo >= hi) return placed;
                List<int> rowIndices = rowsForAnchor(anchor, static_cast<int>(hi - lo));
                for (size_t i = lo; i < hi; ++i) {
                        LaidOutRow r;
                        const size_t idx = i - lo;
                        r.row = (idx < rowIndices.size() ? rowIndices[idx] : 15);
                        r.spans = wrap[i];
                        placed.pushToBack(r);
                }
                return placed;
        }

        /// @brief Flattens a multi-row wrap into a single combined row
        ///        (joining all spans with inter-row single spaces).
        ///        Used by paint-on / roll-up fallback paths that don't
        ///        support multi-row layouts.
        List<LaidOutRow> flattenToSingleRow(const List<WrapRowSpans> &wrap, const SubtitleAnchor &anchor) {
                List<LaidOutRow> placed;
                LaidOutRow       r;
                r.row = rowFromAnchor(anchor);
                bool firstSpan = true;
                for (size_t i = 0; i < wrap.size(); ++i) {
                        for (size_t j = 0; j < wrap[i].size(); ++j) {
                                SubtitleSpan s = wrap[i][j];
                                // Insert a space between rows when stitching
                                // two non-empty spans across the row break.
                                if (!firstSpan && !s.isEmpty() && i > 0 && j == 0) {
                                        s.setText(String(" ") + s.text());
                                }
                                r.spans.pushToBack(s);
                                if (!s.isEmpty()) firstSpan = false;
                        }
                }
                placed.pushToBack(r);
                return placed;
        }

        /// @brief Pre-parity byte pair for one of the roll-up
        ///        "set row count" control codes.
        ScheduledPair rollUpControl(int rows) {
                ScheduledPair p;
                p.b1 = Cea608::Cc1MiscFirstByte;
                switch (rows) {
                        case 3: p.b2 = Cea608::MiscRU3; break;
                        case 4: p.b2 = Cea608::MiscRU4; break;
                        case 2:
                        default: p.b2 = Cea608::MiscRU2; break;
                }
                return p;
        }

        /// @brief Emits one physical row's PAC + char-pairs +
        ///        mid-row codes into @p body.
        ///
        /// Single-row layouts produce the same byte sequence the
        /// legacy single-PAC builder did (one initial doubled PAC
        /// from the first non-empty span's style, then chars / MR
        /// codes for subsequent style changes).
        void emitRowBytes(CueBody &body, const LaidOutRow &laid) {
                const WrapRowSpans &spans = laid.spans;
                WireStyle           initial;
                for (size_t i = 0; i < spans.size(); ++i) {
                        if (spans[i].bold()) body.hadBold = true;
                        if (!spans[i].isEmpty()) {
                                initial = wireStyleFor(spans[i]);
                                break;
                        }
                }
                Cea608::PacAttr pac;
                pac.row = laid.row;
                pac.indentCol = 0;
                pac.color = initial.color;
                pac.italic = initial.italic;
                pac.underline = initial.underline;
                uint8_t pb1 = 0, pb2 = 0;
                Cea608::encodePac(pac, pb1, pb2);
                body.bytes.pushToBack(ScheduledPair{pb1, pb2});
                body.bytes.pushToBack(ScheduledPair{pb1, pb2}); // doubled
                // Emit the initial row's background-attribute pair right
                // after the PAC when the first span declares a bg colour.
                // The bg code persists on the wire until another bg code
                // or the next row's PAC overrides it.
                if (initial.hasBg) {
                        uint8_t bb1 = 0, bb2 = 0;
                        Cea608::encodeBgAttribute(initial.bgColor, initial.bgSemiTransparent, bb1, bb2);
                        body.bytes.pushToBack(ScheduledPair{bb1, bb2});
                        body.bytes.pushToBack(ScheduledPair{bb1, bb2}); // doubled
                }

                WireStyle current = initial;
                bool      anyCharsEmitted = false;
                for (size_t i = 0; i < spans.size(); ++i) {
                        const SubtitleSpan &span = spans[i];
                        if (span.isEmpty()) continue;
                        const WireStyle ws = wireStyleFor(span);
                        const bool      emitMr = anyCharsEmitted && ws.fgDiffers(current);
                        const bool      emitBg = anyCharsEmitted && ws.bgDiffers(current);
                        if (emitMr) {
                                uint8_t mb1 = 0, mb2 = 0;
                                Cea608::encodeMidRow(ws.color, ws.italic, ws.underline, mb1, mb2);
                                body.bytes.pushToBack(ScheduledPair{mb1, mb2});
                                body.bytes.pushToBack(ScheduledPair{mb1, mb2}); // doubled
                        }
                        if (emitBg) {
                                uint8_t bb1 = 0, bb2 = 0;
                                Cea608::encodeBgAttribute(
                                        ws.hasBg ? ws.bgColor : Cea608::CaptionColor::White,
                                        ws.hasBg && ws.bgSemiTransparent, bb1, bb2);
                                body.bytes.pushToBack(ScheduledPair{bb1, bb2});
                                body.bytes.pushToBack(ScheduledPair{bb1, bb2}); // doubled
                        }
                        if (emitMr || emitBg) {
                                current = ws;
                        }
                        String text = span.text();
                        if (emitMr || emitBg) {
                                // The MR / bg-attribute control code
                                // consumes one display cell as a
                                // styled space.  Drop one leading
                                // space from the new run so the
                                // control-code cell serves as the
                                // inter-run separator — otherwise the
                                // run's leading space (added by
                                // rowSpansFromWords for word-break
                                // adjacency) lands inside the styled
                                // region and shows up as a one-cell
                                // visual bleed of the new style
                                // (underline / colour / italic) into
                                // the gap before the styled text.
                                // The decoder mirrors this by
                                // inserting a neutral inter-run space
                                // span on each MR / bg receipt.
                                if (!text.isEmpty() && text.cstr()[0] == ' ') {
                                        text = text.substr(1);
                                }
                        }
                        List<ScheduledPair> chars = encodeCharPairs(text);
                        for (size_t c = 0; c < chars.size(); ++c) {
                                body.bytes.pushToBack(chars[c]);
                                anyCharsEmitted = true;
                        }
                        // First non-empty span's style is already baked
                        // into the PAC; subsequent spans need an MR if
                        // they differ.
                        if (!anyCharsEmitted) current = ws;
                }
        }

        /// @brief Builds the per-cue body byte stream for a placed
        ///        multi-row layout.
        CueBody buildCueBody(const List<LaidOutRow> &laidOut) {
                CueBody body;
                body.hadBold = false;
                for (size_t r = 0; r < laidOut.size(); ++r) {
                        emitRowBytes(body, laidOut[r]);
                }
                return body;
        }

} // namespace

// ============================================================================
// Pimpl
// ============================================================================

struct Cea608EncoderImpl {
                PROMEKI_SHARED_FINAL(Cea608EncoderImpl)

                Cea608Encoder::Config cfg;
                /// @brief Per-frame schedule.  Frames not present in
                ///        the map emit the null pair @c (0x80, 0x80).
                Map<int64_t, ScheduledPair> schedule;
};

// ============================================================================
// Cea608Encoder — construction
// ============================================================================

Cea608Encoder::Cea608Encoder() : _d(SharedPtr<Cea608EncoderImpl>::create()) {}

Cea608Encoder::Cea608Encoder(Config cfg) : _d(SharedPtr<Cea608EncoderImpl>::create()) {
        _d.modify()->cfg = std::move(cfg);
}

Cea608Encoder::~Cea608Encoder() = default;

const Cea608Encoder::Config &Cea608Encoder::config() const { return _d->cfg; }

FrameRate Cea608Encoder::frameRate() const { return _d->cfg.frameRate; }

void Cea608Encoder::reset() { _d.modify()->schedule.clear(); }

// ============================================================================
// Cea608Encoder — setSubtitles (scheduling)
// ============================================================================

namespace {

        /// @brief Logs a one-shot bold-not-representable warning the
        ///        first time a span requests bold.  Mutating reference
        ///        is the latch.
        void warnBoldOnce(bool &warned, size_t cueIndex) {
                if (warned) return;
                promekiWarn("Cea608Encoder::setSubtitles: bold is not representable "
                            "in CEA-608; bold spans encode without the bold flag "
                            "(first occurrence at cue %zu)",
                            cueIndex);
                warned = true;
        }

        /// @brief Per-cue character-pair sequence including a leading
        ///        PAC pair and any mid-row codes between style runs.
        ///        Shared by paint-on and roll-up — both emit the same
        ///        "PAC, chars, (MR, chars)*" body shape; only the
        ///        framing control codes differ.
        /// @brief Sum of codepoints across a wrap layout's rows (used
        ///        to apportion an over-cap cue's display window across
        ///        auto-split sub-cues by density).
        size_t wrapCharCount(const List<WrapRowSpans> &wrap, size_t lo, size_t hi) {
                size_t total = 0;
                if (hi > wrap.size()) hi = wrap.size();
                for (size_t i = lo; i < hi; ++i) {
                        const WrapRowSpans &row = wrap[i];
                        for (size_t s = 0; s < row.size(); ++s) total += row[s].text().length();
                }
                if (total < 1) total = 1; // never apportion by zero
                return total;
        }

        Error buildPopOnSchedule(Cea608EncoderImpl *d, const SubtitleList &subs) {
                // Walk cues in chronological order.  EDM scheduling is
                // *deferred*: a cue's EDM is held aside and only
                // committed when we know the next cue's pre-roll doesn't
                // overlap it (real-world encoders elide the EDM in that
                // case).
                int64_t      lastEocFrame = INT64_MIN;
                int64_t      pendingEdmFrame = INT64_MIN;
                bool         boldWarned = false;
                const int    maxCols = d->cfg.maxCols;
                const int    maxRows = d->cfg.maxRows;
                for (size_t i = 0; i < subs.size(); ++i) {
                        const Subtitle &cue = subs[i];

                        const int64_t cueStart = timeStampToFrame(cue.start(), d->cfg.frameRate);
                        const int64_t cueEnd = timeStampToFrame(cue.end(), d->cfg.frameRate);
                        if (cueEnd <= cueStart) continue;

                        // Build wrap layout once; auto-split iterates
                        // over @c chunkSize-sized slices of the layout.
                        List<WrapRowSpans> wrap = wrapStyledCue(cue, maxCols, maxRows);
                        if (wrap.isEmpty()) {
                                // Empty cue — emit a single empty row so the
                                // schedule produces a valid PAC + (no chars)
                                // + EOC pair at the cue's start.
                                WrapRowSpans empty;
                                wrap.pushToBack(empty);
                        }
                        if (layoutHadBold(wrap)) warnBoldOnce(boldWarned, i);

                        const size_t chunkSize = (maxRows > 0)
                                                         ? static_cast<size_t>(maxRows)
                                                         : wrap.size();
                        const size_t totalChars = wrapCharCount(wrap, 0, wrap.size());
                        const int64_t totalFrames = cueEnd - cueStart;
                        const size_t  chunkCount = (wrap.size() + chunkSize - 1) / chunkSize;
                        if (chunkCount > 1) {
                                promekiWarn(
                                        "Cea608Encoder::setSubtitles: cue %zu wraps to %zu rows "
                                        "(> maxRows=%d); auto-splitting into %zu time-displaced "
                                        "sub-cues",
                                        i, wrap.size(), maxRows, chunkCount);
                        }

                        size_t accChars = 0;
                        for (size_t c = 0; c < chunkCount; ++c) {
                                const size_t lo = c * chunkSize;
                                const size_t hi = std::min(lo + chunkSize, wrap.size());
                                const size_t chunkChars = wrapCharCount(wrap, lo, hi);

                                // Apportion the cue's display window across
                                // chunks proportional to char count.
                                const int64_t subStart = (chunkCount == 1)
                                                                 ? cueStart
                                                                 : cueStart + (totalFrames *
                                                                               static_cast<int64_t>(accChars))
                                                                                       / static_cast<int64_t>(totalChars);
                                const int64_t subEnd = (c + 1 == chunkCount)
                                                               ? cueEnd
                                                               : cueStart + (totalFrames *
                                                                              static_cast<int64_t>(accChars + chunkChars))
                                                                                      / static_cast<int64_t>(totalChars);
                                if (subEnd <= subStart) {
                                        accChars += chunkChars;
                                        continue;
                                }

                                List<LaidOutRow> laidOut = placeChunk(wrap, lo, hi, cue.anchor());
                                CueBody          body = buildCueBody(laidOut);

                                const size_t  preRollFrames = 2 + body.bytes.size();
                                const int64_t firstFrame =
                                        subStart - static_cast<int64_t>(preRollFrames);
                                if (firstFrame < 0) {
                                        promekiWarn("Cea608Encoder::setSubtitles: cue %zu sub-%zu "
                                                    "pre-roll starts at frame %lld (before frame 0); "
                                                    "cue.start is too close to media t=0 for the "
                                                    "cue's length",
                                                    i, c, static_cast<long long>(firstFrame));
                                        return Error::OutOfRange;
                                }
                                if (firstFrame <= lastEocFrame) {
                                        promekiWarn("Cea608Encoder::setSubtitles: cue %zu sub-%zu "
                                                    "pre-roll (first frame %lld) overlaps prior "
                                                    "cue's wire stream (last byte frame %lld)",
                                                    i, c, static_cast<long long>(firstFrame),
                                                    static_cast<long long>(lastEocFrame));
                                        return Error::OutOfRange;
                                }
                                if (pendingEdmFrame != INT64_MIN) {
                                        if (firstFrame > pendingEdmFrame + 1) {
                                                d->schedule.insert(pendingEdmFrame,
                                                                   ScheduledPair{Cea608::EdmB1,
                                                                                 Cea608::EdmB2});
                                                d->schedule.insert(pendingEdmFrame + 1,
                                                                   ScheduledPair{Cea608::EdmB1,
                                                                                 Cea608::EdmB2});
                                        }
                                        pendingEdmFrame = INT64_MIN;
                                }

                                int64_t f = firstFrame;
                                d->schedule.insert(f++, ScheduledPair{Cea608::RclB1, Cea608::RclB2});
                                d->schedule.insert(f++, ScheduledPair{Cea608::RclB1, Cea608::RclB2});
                                for (size_t b = 0; b < body.bytes.size(); ++b) {
                                        d->schedule.insert(f++, body.bytes[b]);
                                }
                                d->schedule.insert(f++, ScheduledPair{Cea608::EocB1, Cea608::EocB2});
                                d->schedule.insert(f++, ScheduledPair{Cea608::EocB1, Cea608::EocB2});

                                pendingEdmFrame = subEnd;
                                lastEocFrame = subStart + 1;
                                accChars += chunkChars;
                        }
                }
                if (pendingEdmFrame != INT64_MIN) {
                        d->schedule.insert(pendingEdmFrame, ScheduledPair{Cea608::EdmB1, Cea608::EdmB2});
                        d->schedule.insert(pendingEdmFrame + 1, ScheduledPair{Cea608::EdmB1, Cea608::EdmB2});
                }
                return Error::Ok;
        }

        /// @brief Schedules a paint-on cue list.
        ///
        /// Per-cue layout:
        ///
        /// @code
        ///   [RDC,RDC, PAC,PAC, (chars|MR,MR)..., EDM,EDM]
        /// @endcode
        ///
        /// Pre-roll = 4 frames (doubled RDC + doubled PAC).  Characters
        /// land at @c startFrame... @c startFrame+N-1 (where @c N is
        /// the body's char/MR-pair count, i.e. @c body.size()-2 for the
        /// initial PAC pair).  EDM doubled at @c endFrame /
        /// @c endFrame+1.
        ///
        /// Unlike pop-on (where the receiver-side memory swap happens
        /// on the @c EOC pair so cue text is hidden until @c startFrame),
        /// paint-on writes directly to displayed memory.  The first
        /// character lands at @c startFrame so the cue's leading edge
        /// matches the pop-on contract — receivers see the first char
        /// appear at @c startFrame and the cue continue to paint over
        /// the next @c N-1 frames.
        ///
        /// EDM scheduling mirrors pop-on's deferred-then-elide policy
        /// for back-to-back cues.
        Error buildPaintOnSchedule(Cea608EncoderImpl *d, const SubtitleList &subs) {
                int64_t lastByteFrame = INT64_MIN;
                int64_t pendingEdmFrame = INT64_MIN;
                bool    boldWarned = false;
                const int maxCols = d->cfg.maxCols;
                const int maxRows = d->cfg.maxRows;
                for (size_t i = 0; i < subs.size(); ++i) {
                        const Subtitle &cue = subs[i];

                        const int64_t startFrame = timeStampToFrame(cue.start(), d->cfg.frameRate);
                        const int64_t endFrame = timeStampToFrame(cue.end(), d->cfg.frameRate);
                        if (endFrame <= startFrame) continue;

                        List<WrapRowSpans> wrap = wrapStyledCue(cue, maxCols, maxRows);
                        if (wrap.isEmpty()) {
                                WrapRowSpans empty;
                                wrap.pushToBack(empty);
                        }
                        if (layoutHadBold(wrap)) warnBoldOnce(boldWarned, i);

                        // Paint-on does not auto-split: the live char stream
                        // is anchored at startFrame, and there's no clean
                        // way to time-displace sub-cues within a paint-on
                        // window without re-emitting RDC.  Flatten any
                        // over-cap layout to a single row instead.
                        if (maxRows > 0 && static_cast<int>(wrap.size()) > maxRows) {
                                promekiWarn("Cea608Encoder::setSubtitles[paint-on]: cue %zu wraps to "
                                            "%zu rows (> maxRows=%d); paint-on does not auto-split, "
                                            "flattening to a single row",
                                            i, wrap.size(), maxRows);
                                List<LaidOutRow> flat = flattenToSingleRow(wrap, cue.anchor());
                                wrap.clear();
                                if (!flat.isEmpty()) wrap.pushToBack(flat[0].spans);
                        }
                        List<LaidOutRow> laidOut = placeChunk(wrap, 0, wrap.size(), cue.anchor());
                        CueBody          body = buildCueBody(laidOut);

                        // Body shape: [PAC0,PAC0,(MR,MR,)?chars0...,PAC1,PAC1,chars1,...].
                        // Paint-on splits that into:
                        //   pre-roll (lands before startFrame): RDC,RDC + first PAC pair = 4 frames
                        //   live chars (land at startFrame onward): body.bytes minus the
                        //                                           leading first-PAC pair.
                        // Body always begins with PAC,PAC (buildCueBody contract).
                        if (body.bytes.size() < 2) {
                                // Defensive: should never happen, PAC pair always present.
                                continue;
                        }
                        const size_t  charCount = body.bytes.size() - 2;
                        const int64_t firstFrame = startFrame - 4; // 2 RDC + 2 PAC
                        const int64_t lastCharFrame = startFrame + static_cast<int64_t>(charCount) - 1;

                        if (firstFrame < 0) {
                                promekiWarn("Cea608Encoder::setSubtitles[paint-on]: cue %zu pre-roll "
                                            "starts at frame %lld (before frame 0)",
                                            i, static_cast<long long>(firstFrame));
                                return Error::OutOfRange;
                        }
                        if (firstFrame <= lastByteFrame) {
                                promekiWarn("Cea608Encoder::setSubtitles[paint-on]: cue %zu pre-roll "
                                            "(first frame %lld) overlaps prior cue's wire stream "
                                            "(last byte frame %lld)",
                                            i, static_cast<long long>(firstFrame),
                                            static_cast<long long>(lastByteFrame));
                                return Error::OutOfRange;
                        }
                        if (charCount > 0 && lastCharFrame >= endFrame) {
                                promekiWarn("Cea608Encoder::setSubtitles[paint-on]: cue %zu has "
                                            "%zu char pairs (%lld frames) but only %lld frames "
                                            "of display time — chars would overrun cue end",
                                            i, charCount, static_cast<long long>(charCount),
                                            static_cast<long long>(endFrame - startFrame));
                                return Error::OutOfRange;
                        }
                        if (pendingEdmFrame != INT64_MIN) {
                                if (firstFrame > pendingEdmFrame + 1) {
                                        d->schedule.insert(pendingEdmFrame,
                                                           ScheduledPair{Cea608::EdmB1, Cea608::EdmB2});
                                        d->schedule.insert(pendingEdmFrame + 1,
                                                           ScheduledPair{Cea608::EdmB1, Cea608::EdmB2});
                                }
                                pendingEdmFrame = INT64_MIN;
                        }

                        int64_t f = firstFrame;
                        // Doubled RDC at startFrame-4, startFrame-3.
                        d->schedule.insert(f++, ScheduledPair{Cea608::Cc1MiscFirstByte, Cea608::MiscRDC});
                        d->schedule.insert(f++, ScheduledPair{Cea608::Cc1MiscFirstByte, Cea608::MiscRDC});
                        // Doubled PAC at startFrame-2, startFrame-1.
                        d->schedule.insert(f++, body.bytes[0]);
                        d->schedule.insert(f++, body.bytes[1]);
                        // Live chars / mid-row pairs starting at startFrame.
                        for (size_t b = 2; b < body.bytes.size(); ++b) {
                                d->schedule.insert(f++, body.bytes[b]);
                        }

                        pendingEdmFrame = endFrame;
                        lastByteFrame = lastCharFrame;
                }
                if (pendingEdmFrame != INT64_MIN) {
                        d->schedule.insert(pendingEdmFrame, ScheduledPair{Cea608::EdmB1, Cea608::EdmB2});
                        d->schedule.insert(pendingEdmFrame + 1, ScheduledPair{Cea608::EdmB1, Cea608::EdmB2});
                }
                return Error::Ok;
        }

        /// @brief Schedules a roll-up cue list.
        ///
        /// Roll-up is a continuous mode: once the receiver enters
        /// roll-up via @c RU2/RU3/RU4 it stays there.  Each cue is a
        /// new "row" appended via @c CR (carriage return); rows older
        /// than the configured roll-up window scroll off the top.
        ///
        /// Per-cue layout:
        ///
        /// @code
        ///   First cue:        [RUx,RUx, CR,CR, PAC,PAC, chars/MR..., (no EDM)]
        ///   Subsequent cues:  [CR,CR, (PAC,PAC,)? chars/MR..., (no EDM)]
        /// @endcode
        ///
        /// @c RUx is emitted once at the start of the batch.  Characters
        /// land at the cue's start frame onward; the cue is fully
        /// painted by @c startFrame + N - 1.
        ///
        /// Roll-up has no per-cue EDM — the cue scrolls off when the
        /// next CR fires or stays visible after the final cue.
        Error buildRollUpSchedule(Cea608EncoderImpl *d, const SubtitleList &subs, int rollUpRows) {
                int64_t   lastByteFrame = INT64_MIN;
                bool      rollUpInitialised = false;
                bool      boldWarned = false;
                int       lastEmittedRow = -1; // tracks when we can skip PAC re-emission.
                const int maxCols = d->cfg.maxCols;
                for (size_t i = 0; i < subs.size(); ++i) {
                        const Subtitle &cue = subs[i];

                        const int64_t startFrame = timeStampToFrame(cue.start(), d->cfg.frameRate);
                        const int64_t endFrame = timeStampToFrame(cue.end(), d->cfg.frameRate);
                        if (endFrame <= startFrame) continue;

                        // Roll-up is single-row by spec — every cue
                        // appends one row at row 15 via @c CR.  Wrap is
                        // still applied so excessive widths trigger a
                        // visible warning, but the layout is always
                        // flattened back to one row for emission.
                        List<WrapRowSpans> wrap = wrapStyledCue(cue, maxCols, /*maxRows=*/1);
                        if (wrap.isEmpty()) {
                                WrapRowSpans empty;
                                wrap.pushToBack(empty);
                        }
                        if (wrap.size() > 1) {
                                promekiWarn("Cea608Encoder::setSubtitles[roll-up]: cue %zu wraps to "
                                            "%zu rows; roll-up is single-row by spec, flattening",
                                            i, wrap.size());
                                List<LaidOutRow> flat = flattenToSingleRow(wrap, cue.anchor());
                                wrap.clear();
                                if (!flat.isEmpty()) wrap.pushToBack(flat[0].spans);
                        }
                        if (layoutHadBold(wrap)) warnBoldOnce(boldWarned, i);
                        List<LaidOutRow> laidOut = placeChunk(wrap, 0, wrap.size(), cue.anchor());
                        CueBody          body = buildCueBody(laidOut);
                        if (body.bytes.size() < 2) continue; // defensive

                        // Body shape: [PAC,PAC,(MR,MR,)?chars...].
                        // Per-cue pre-roll bytes (live chars excluded):
                        //   First cue:        2 (RUx) + 2 (CR) + 2 (PAC) = 6
                        //   Subsequent cues:  2 (CR) + 2 (PAC) = 4
                        // Characters land at startFrame onward.
                        const size_t  charCount = body.bytes.size() - 2;
                        const size_t  preRoll = rollUpInitialised ? 4 : 6;
                        const int64_t firstFrame = startFrame - static_cast<int64_t>(preRoll);
                        const int64_t lastCharFrame = startFrame + static_cast<int64_t>(charCount) - 1;

                        if (firstFrame < 0) {
                                promekiWarn("Cea608Encoder::setSubtitles[roll-up]: cue %zu pre-roll "
                                            "starts at frame %lld (before frame 0)",
                                            i, static_cast<long long>(firstFrame));
                                return Error::OutOfRange;
                        }
                        if (firstFrame <= lastByteFrame) {
                                promekiWarn("Cea608Encoder::setSubtitles[roll-up]: cue %zu pre-roll "
                                            "(first frame %lld) overlaps prior cue's wire stream "
                                            "(last byte frame %lld)",
                                            i, static_cast<long long>(firstFrame),
                                            static_cast<long long>(lastByteFrame));
                                return Error::OutOfRange;
                        }
                        if (charCount > 0 && lastCharFrame >= endFrame) {
                                promekiWarn("Cea608Encoder::setSubtitles[roll-up]: cue %zu has "
                                            "%zu char pairs (%lld frames) but only %lld frames "
                                            "of display time before next cue",
                                            i, charCount, static_cast<long long>(charCount),
                                            static_cast<long long>(endFrame - startFrame));
                                return Error::OutOfRange;
                        }

                        int64_t f = firstFrame;
                        if (!rollUpInitialised) {
                                const ScheduledPair ruPair = rollUpControl(rollUpRows);
                                d->schedule.insert(f++, ruPair);
                                d->schedule.insert(f++, ruPair);
                                rollUpInitialised = true;
                                lastEmittedRow = -1;
                        }
                        // Doubled CR.
                        d->schedule.insert(f++, ScheduledPair{Cea608::Cc1MiscFirstByte, Cea608::MiscCR});
                        d->schedule.insert(f++, ScheduledPair{Cea608::Cc1MiscFirstByte, Cea608::MiscCR});
                        // Doubled PAC.  Roll-up always anchors row 15 by spec
                        // regardless of cue.anchor; buildCueBody honoured cue.anchor
                        // which we override here.  Re-encode the PAC bytes via
                        // Cea608::encodePac to keep colour/italic/underline from the
                        // body's leading PAC but lock the row to 15.
                        {
                                Cea608::PacAttr ovr;
                                if (!Cea608::decodePac(body.bytes[0].b1, body.bytes[0].b2, ovr)) {
                                        ovr = Cea608::PacAttr{};
                                }
                                ovr.row = 15;
                                uint8_t pb1 = 0, pb2 = 0;
                                Cea608::encodePac(ovr, pb1, pb2);
                                d->schedule.insert(f++, ScheduledPair{pb1, pb2});
                                d->schedule.insert(f++, ScheduledPair{pb1, pb2});
                                lastEmittedRow = 15;
                        }
                        // Live chars / mid-row pairs starting at startFrame.
                        for (size_t b = 2; b < body.bytes.size(); ++b) {
                                d->schedule.insert(f++, body.bytes[b]);
                        }
                        lastByteFrame = lastCharFrame;
                }
                (void)lastEmittedRow;
                return Error::Ok;
        }

} // namespace

Error Cea608Encoder::setSubtitles(const SubtitleList &subs) {
        auto *d = _d.modify();
        d->schedule.clear();

        if (!d->cfg.frameRate.isValid()) {
                promekiWarn("Cea608Encoder::setSubtitles: invalid frame rate");
                return Error::Invalid;
        }
        if (d->cfg.channel != Channel::CC1) {
                promekiWarn("Cea608Encoder::setSubtitles: only CC1 channel is "
                            "implemented in this phase (channel=%d)",
                            static_cast<int>(d->cfg.channel));
                return Error::NotImplemented;
        }

        // Resolve the effective mode for this batch.  When every cue
        // is @c CaptionMode::Default, the @ref Config::mode default
        // wins (preserving the legacy global-mode behaviour).  When
        // every cue carries the *same* explicit mode, that mode wins
        // (per-list override — common for SubRip files authored for
        // a specific 608 mode).  Mixed explicit modes warn-and-fall-
        // back to the config default; per-cue mid-stream mode
        // switching is a documented follow-on.
        Mode             effectiveMode = d->cfg.mode;
        bool             seenExplicit = false;
        Mode             firstExplicit = d->cfg.mode;
        bool             mixed = false;
        const auto      &entries = subs.entries();
        for (size_t i = 0; i < entries.size(); ++i) {
                const int cueModeVal = entries[i].mode().value();
                if (cueModeVal == CaptionMode::Default.value()) continue;
                Mode cueMode = d->cfg.mode;
                if (cueModeVal == CaptionMode::PopOn.value()) cueMode = Mode::PopOn;
                else if (cueModeVal == CaptionMode::PaintOn.value()) cueMode = Mode::PaintOn;
                else if (cueModeVal == CaptionMode::RollUp.value()) cueMode = Mode::RollUp;
                if (!seenExplicit) {
                        firstExplicit = cueMode;
                        seenExplicit = true;
                } else if (cueMode != firstExplicit) {
                        mixed = true;
                        break;
                }
        }
        if (mixed) {
                promekiWarn("Cea608Encoder::setSubtitles: cues mix explicit "
                            "CaptionModes; per-cue mid-stream switching is not "
                            "yet supported.  Falling back to Config::mode=%d for "
                            "the whole batch.",
                            static_cast<int>(d->cfg.mode));
        } else if (seenExplicit) {
                effectiveMode = firstExplicit;
        }

        switch (effectiveMode) {
                case Mode::PopOn:
                        return buildPopOnSchedule(d, subs);
                case Mode::PaintOn:
                        return buildPaintOnSchedule(d, subs);
                case Mode::RollUp: {
                        int rows = d->cfg.rollUpRows;
                        if (rows < 2) rows = 2;
                        if (rows > 4) rows = 4;
                        return buildRollUpSchedule(d, subs, rows);
                }
        }
        return Error::Invalid;
}

// ============================================================================
// Cea608Encoder — nextFrame
// ============================================================================

Cea708Cdp::CcDataList Cea608Encoder::nextFrame(FrameNumber frame) const {
        Cea708Cdp::CcDataList out;
        if (!frame.isValid()) {
                // Unknown frame: emit the null pair so the consumer
                // still gets a valid cc_data triple in the cc_data
                // section (some receivers complain about empty cc_data
                // when the section is marked present).
                out.pushToBack(Cea708Cdp::CcData{true, ccTypeForChannel(_d->cfg.channel),
                                                Cea608::withOddParity(Cea608::NullB1),
                                                Cea608::withOddParity(Cea608::NullB2)});
                return out;
        }
        const int64_t fn = frame.value();
        uint8_t       b1 = Cea608::NullB1;
        uint8_t       b2 = Cea608::NullB2;
        auto          it = _d->schedule.find(fn);
        if (it != _d->schedule.end()) {
                b1 = it->second.b1;
                b2 = it->second.b2;
        }
        out.pushToBack(Cea708Cdp::CcData{true, ccTypeForChannel(_d->cfg.channel),
                                        Cea608::withOddParity(b1), Cea608::withOddParity(b2)});
        return out;
}

FrameNumber Cea608Encoder::earliestStartFor(const Subtitle &cue) const {
        if (!_d->cfg.frameRate.isValid()) return FrameNumber::unknown();
        const int64_t startFrame = timeStampToFrame(cue.start(), _d->cfg.frameRate);
        // For pop-on the body length scales with the layout's row
        // count (one doubled PAC pair per row); for paint-on the
        // pre-roll is fixed at 4 (RDC + first PAC) and the rest
        // streams live, so the *display* window matters more than
        // pre-roll for fit; roll-up still reports the worst-case
        // first-cue pre-roll.
        List<WrapRowSpans> wrap = wrapStyledCue(cue, _d->cfg.maxCols, _d->cfg.maxRows);
        if (wrap.isEmpty()) {
                WrapRowSpans empty;
                wrap.pushToBack(empty);
        }
        // earliestStartFor is a single-cue diagnostic — it reports the
        // pre-roll of the *first chunk* when auto-split is needed,
        // since that's what determines whether the cue fits at the
        // start of the media.
        const size_t chunkSize =
                (_d->cfg.maxRows > 0) ? static_cast<size_t>(_d->cfg.maxRows) : wrap.size();
        const size_t firstChunkHi = std::min(chunkSize, wrap.size());
        List<LaidOutRow> firstChunk = placeChunk(wrap, 0, firstChunkHi, cue.anchor());
        const CueBody    body = buildCueBody(firstChunk);
        int64_t          firstFrame = 0;
        switch (_d->cfg.mode) {
                case Mode::PopOn:
                        // Pre-roll = 2 (RCL) + body (PAC pairs + chars + MRs).
                        // EOC pair lands at and after the chunk's start.
                        firstFrame = startFrame - static_cast<int64_t>(2 + body.bytes.size());
                        break;
                case Mode::PaintOn:
                        // Pre-roll = 4 (RDC + first PAC), chars (and any
                        // row 2+ PAC pairs) stream live from startFrame.
                        firstFrame = startFrame - 4;
                        break;
                case Mode::RollUp:
                        // First cue pre-roll = 6 (RUx + CR + PAC); subsequent cues = 4.
                        // earliestStartFor is a single-cue diagnostic so we report the
                        // worst case (first-cue) — callers building schedules can use
                        // setSubtitles' actual layout.
                        firstFrame = startFrame - 6;
                        break;
        }
        if (firstFrame < 0) return FrameNumber::unknown();
        return FrameNumber(firstFrame);
}

SubtitleList Cea608Encoder::encodableSubset(const SubtitleList &in, SubtitleList *outDropped) const {
        SubtitleList kept;
        if (!_d->cfg.frameRate.isValid()) return kept;
        // Mirror @ref setSubtitles' per-mode pre-roll bookkeeping so a cue
        // surfaced through @ref setSubtitles after this filter never hits
        // an OutOfRange.  Each mode has its own collision rules; see the
        // mode-specific schedule builders for full detail.
        //
        // For pop-on with auto-split, the per-cue pre-roll is the
        // *first sub-cue's* pre-roll (other sub-cues piggyback inside
        // the cue's display window).  The last wire byte is the last
        // sub-cue's EOC frame, which lies at @c endFrame + 1 (the
        // doubled EOC).
        int64_t     lastByteFrame = INT64_MIN;
        bool        rollUpInitialised = false;
        const auto &entries = in.entries();
        for (size_t i = 0; i < entries.size(); ++i) {
                const Subtitle &cue = entries[i];
                const int64_t   startFrame = timeStampToFrame(cue.start(), _d->cfg.frameRate);
                const int64_t   endFrame = timeStampToFrame(cue.end(), _d->cfg.frameRate);
                if (endFrame <= startFrame) {
                        kept.append(cue);
                        continue;
                }
                List<WrapRowSpans> wrap = wrapStyledCue(cue, _d->cfg.maxCols, _d->cfg.maxRows);
                if (wrap.isEmpty()) {
                        WrapRowSpans empty;
                        wrap.pushToBack(empty);
                }
                const size_t chunkSize = (_d->cfg.maxRows > 0)
                                                 ? static_cast<size_t>(_d->cfg.maxRows)
                                                 : wrap.size();
                const size_t firstChunkHi = std::min(chunkSize, wrap.size());
                const size_t chunkCount = (wrap.size() + chunkSize - 1) / chunkSize;
                List<LaidOutRow> firstChunk = placeChunk(wrap, 0, firstChunkHi, cue.anchor());
                const CueBody    body = buildCueBody(firstChunk);
                size_t           preRoll = 0;
                int64_t          lastCharFrame = INT64_MIN;
                bool             charsOverrun = false;
                size_t           charCount = 0;
                if (body.bytes.size() >= 2) charCount = body.bytes.size() - 2;
                switch (_d->cfg.mode) {
                        case Mode::PopOn:
                                preRoll = 2 + body.bytes.size();
                                // For multi-sub-cue pop-on the *last* wire
                                // byte is the final sub-cue's doubled EOC
                                // at frame @c endFrame + 1.
                                lastCharFrame = (chunkCount > 1) ? endFrame + 1 : startFrame + 1;
                                break;
                        case Mode::PaintOn:
                                preRoll = 4;
                                lastCharFrame = startFrame + static_cast<int64_t>(charCount) - 1;
                                if (charCount > 0 && lastCharFrame >= endFrame) charsOverrun = true;
                                break;
                        case Mode::RollUp:
                                preRoll = rollUpInitialised ? 4 : 6;
                                lastCharFrame = startFrame + static_cast<int64_t>(charCount) - 1;
                                if (charCount > 0 && lastCharFrame >= endFrame) charsOverrun = true;
                                break;
                }
                const int64_t firstFrame = startFrame - static_cast<int64_t>(preRoll);
                if (firstFrame < 0 || firstFrame <= lastByteFrame || charsOverrun) {
                        if (outDropped != nullptr) outDropped->append(cue);
                        continue;
                }
                kept.append(cue);
                lastByteFrame = lastCharFrame;
                if (_d->cfg.mode == Mode::RollUp) rollUpInitialised = true;
        }
        return kept;
}

PROMEKI_NAMESPACE_END
