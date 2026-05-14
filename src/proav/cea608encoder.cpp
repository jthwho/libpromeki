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
#include <promeki/cea608ext.h>
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
        ///        character pairs (pre-parity).
        ///
        /// Walks Unicode codepoints (not raw UTF-8 bytes) and
        /// dispatches each one through @ref Cea608Ext::encode:
        ///
        ///  - Basic G0 (ASCII or one of the ten remapped Latin /
        ///    arithmetic positions: á / é / í / ó / ú / ç / ÷ / Ñ /
        ///    ñ / █) &rarr; one wire byte packed into the current
        ///    char pair.
        ///  - Special Character (16 glyphs: ® / ° / ½ / ¿ / ™ / ¢ /
        ///    £ / ♪ / à / NBSP / è / â / ê / î / ô / û) &rarr; one
        ///    placeholder G0 byte (best-fit ASCII for old decoders),
        ///    then a doubled @c (0x11, 0x30..0x3F) control pair on
        ///    CC1.  The control pair tells modern decoders to
        ///    *replace* the previously displayed character — i.e.
        ///    the placeholder — with the special glyph, so the
        ///    cursor lands at the right position with the right
        ///    glyph.
        ///  - Extended Spanish (32 glyphs) &rarr; placeholder +
        ///    doubled @c (0x12, 0x20..0x3F) control pair on CC1.
        ///  - Extended Portuguese / German (32 glyphs) &rarr;
        ///    placeholder + doubled @c (0x13, 0x20..0x3F) control
        ///    pair on CC1.
        ///  - Anything else (codepoints with no 608 representation)
        ///    &rarr; substitute @c 0x20 space.
        ///
        /// When the placeholder for a Special / Extended glyph is
        /// the only character in its pair (no neighbour to share
        /// with), the encoder pads the pair's second byte with
        /// @c NUL (0x00) instead of @c 0x20 space.  Receivers ignore
        /// @c NUL after parity strip, so the cursor doesn't advance
        /// past the placeholder before the doubled control code
        /// lands and triggers the "replace previously displayed
        /// character" semantics — padding with @c 0x20 would let
        /// the cursor advance one past the placeholder, making the
        /// special glyph replace the @c 0x20 space instead of the
        /// intended placeholder.
        ///
        /// Multi-line cues are flattened with a space between
        /// lines — v1 does not emit row-switching PACs.
        List<ScheduledPair> encodeCharPairs(const String &text) {
                List<ScheduledPair> out;
                ScheduledPair       pending;
                bool                half = false;

                auto pushChar = [&](uint8_t ch) {
                        if (!half) {
                                pending.b1 = ch;
                                half = true;
                        } else {
                                pending.b2 = ch;
                                out.pushToBack(pending);
                                pending = ScheduledPair{};
                                half = false;
                        }
                };
                auto emitControlPair = [&](uint8_t cb1, uint8_t cb2) {
                        // Doubled per spec — second occurrence is the
                        // de-dup target the receiver collapses.
                        out.pushToBack({cb1, cb2});
                        out.pushToBack({cb1, cb2});
                };
                auto pushPlaceholderThenControl = [&](uint8_t placeholder, uint8_t cb1,
                                                      uint8_t cb2) {
                        if (half) {
                                // Pack the placeholder as the second
                                // byte of the pending pair so the
                                // receiver's cursor lands one past
                                // the placeholder before the control
                                // code triggers "replace previous
                                // char" — net effect: the placeholder
                                // is the char that gets replaced.
                                pending.b2 = placeholder;
                                out.pushToBack(pending);
                                pending = ScheduledPair{};
                                half = false;
                        } else {
                                // Lone placeholder: pair with a NUL
                                // pad so the cursor advances exactly
                                // once (past the placeholder) before
                                // the control code arrives.  Receivers
                                // ignore NUL bytes after parity strip.
                                out.pushToBack({placeholder, Cea608::NullB2});
                        }
                        emitControlPair(cb1, cb2);
                };

                const size_t len = text.length();
                for (size_t i = 0; i < len; ++i) {
                        char32_t cp = text.charAt(i).codepoint();
                        if (cp == U'\n' || cp == U'\r' || cp == U'\t') cp = U' ';
                        const auto enc = Cea608Ext::encode(static_cast<uint32_t>(cp));
                        switch (enc.kind) {
                                case Cea608Ext::Kind::BasicG0:
                                        pushChar(enc.byte);
                                        break;
                                case Cea608Ext::Kind::Special:
                                        pushPlaceholderThenControl(enc.placeholder, 0x11, enc.code);
                                        break;
                                case Cea608Ext::Kind::ExtSpanish:
                                        pushPlaceholderThenControl(enc.placeholder, 0x12, enc.code);
                                        break;
                                case Cea608Ext::Kind::ExtFrench:
                                        pushPlaceholderThenControl(enc.placeholder, 0x13, enc.code);
                                        break;
                                case Cea608Ext::Kind::None:
                                        pushChar(0x20);
                                        break;
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

        /// @brief Returns the channel-bit OR-mask that converts a
        ///        CC1/CC3-shaped control byte into its CC2/CC4
        ///        sibling.
        ///
        /// CEA-608 packs the intra-field channel selector into
        /// bit 3 of the first byte of every control code: bit 3 = 0
        /// for CC1 (in field 1) or CC3 (in field 2); bit 3 = 1 for
        /// CC2 / CC4.  All built-in control-byte constants
        /// (@ref Cea608::RclB1 / @ref Cea608::Cc1MiscFirstByte / the
        /// PAC / mid-row / BG-attribute / Special-Character /
        /// Extended-Character first bytes) are CC1-shaped, so OR-ing
        /// @c 0x08 into the first byte is the only mechanical step
        /// needed to retarget them to CC2 / CC4.  Cross-field routing
        /// (CC1 vs CC3, CC2 vs CC4) is handled separately by
        /// @ref ccTypeForChannel.
        uint8_t channelOffsetFor(Cea608Encoder::Channel ch) {
                switch (ch) {
                        case Cea608Encoder::Channel::CC1:
                        case Cea608Encoder::Channel::CC3: return 0x00;
                        case Cea608Encoder::Channel::CC2:
                        case Cea608Encoder::Channel::CC4: return 0x08;
                }
                return 0x00;
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
        ///
        /// The foreground paths (PAC + mid-row) have no encoding for
        /// Black — palette index 7 is BG-attribute only.  A
        /// near-Black input fg quantises to @c Cea608::CaptionColor::Black
        /// via nearest-palette but the wire would emit it as White
        /// anyway; we coerce the in-memory representation to White
        /// here so downstream "is the fg styled?" predicates
        /// (positioning degradation, mid-row emission) treat Black
        /// fg as the visual equivalent of White rather than a
        /// distinct styled colour.  The BG path keeps the Black
        /// resolution intact via the separate quantisation call.
        WireStyle wireStyleFor(const SubtitleSpan &span) {
                WireStyle ws;
                ws.italic = span.italic();
                ws.underline = span.underline();
                if (ws.italic) {
                        ws.color = Cea608::CaptionColor::White;
                } else {
                        ws.color = quantiseColor(span.color());
                        if (ws.color == Cea608::CaptionColor::Black) {
                                ws.color = Cea608::CaptionColor::White;
                        }
                }
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
        ///        row index (1..15), target start column (0..31), and
        ///        styled span list.
        ///
        /// @c targetColumn is computed at @ref placeChunk time from the
        /// cue's @ref SubtitleAnchor horizontal half plus the row's
        /// width: Left → 0, Center → @c (32 - rowWidth) / 2, Right →
        /// @c 32 - rowWidth (clamped to @c [0, 31]).  The encoder
        /// translates @c targetColumn into a PAC indent slot (multiples
        /// of 4) plus an optional doubled Tab Offset (T1/T2/T3) for the
        /// 1..3 column residual.
        struct LaidOutRow {
                        int          row = 15;
                        int          targetColumn = 0;
                        WrapRowSpans spans;
        };

        /// @brief Returns @c true when @p anchor explicitly names a
        ///        centered horizontal alignment (TopCenter /
        ///        MiddleCenter / BottomCenter).
        ///
        /// @c SubtitleAnchor::Default is treated as a no-hint anchor
        /// (the encoder leaves the row at flush-left column 0); a
        /// caller that actually wants the broadcast "centered"
        /// convention picks @c BottomCenter explicitly.
        bool isCenterAnchor(const SubtitleAnchor &a) {
                const int v = a.value();
                return v == SubtitleAnchor::TopCenter.value()
                        || v == SubtitleAnchor::MiddleCenter.value()
                        || v == SubtitleAnchor::BottomCenter.value();
        }

        /// @brief Returns @c true when @p anchor names a right-aligned
        ///        horizontal alignment (Top/Middle/BottomRight).
        bool isRightAnchor(const SubtitleAnchor &a) {
                const int v = a.value();
                return v == SubtitleAnchor::TopRight.value()
                        || v == SubtitleAnchor::MiddleRight.value()
                        || v == SubtitleAnchor::BottomRight.value();
        }

        /// @brief Counts visible cells in a wrap row (sum of every
        ///        span's text length).
        size_t rowVisibleWidth(const WrapRowSpans &row) {
                size_t w = 0;
                for (size_t s = 0; s < row.size(); ++s) w += row[s].text().length();
                return w;
        }

        /// @brief Computes the target start column for a row of
        ///        @p rowWidth cells under horizontal anchor @p anchor.
        ///
        /// Left → 0; Center → @c (32 - rowWidth) / 2; Right → @c 32 -
        /// rowWidth.  Clamps the result to @c [0, 31] so a full-width
        /// row (or a degenerate over-width row) lands at column 0
        /// rather than a negative offset that would underflow the
        /// receiver's cursor.  608 caption rows can carry at most 32
        /// cells per row.
        int targetColumnFor(const SubtitleAnchor &anchor, size_t rowWidth) {
                constexpr int kRowWidth = 32;
                int col = 0;
                const int rw = static_cast<int>(rowWidth);
                if (isCenterAnchor(anchor)) {
                        col = (kRowWidth - rw) / 2;
                } else if (isRightAnchor(anchor)) {
                        col = kRowWidth - rw;
                }
                if (col < 0) col = 0;
                if (col > 31) col = 31;
                return col;
        }

        /// @brief Re-applies row indices to a sub-slice of a wrap
        ///        layout (used by the cue auto-split path so each
        ///        sub-cue gets an anchor-relative row group of its
        ///        own size).
        ///
        /// Each emitted @ref LaidOutRow gets its target start column
        /// computed from @p anchor plus the row's visible width — the
        /// encoder's emit pass turns that into a PAC indent + Tab
        /// Offset pair so the row lands at the requested horizontal
        /// position.
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
                        r.targetColumn = targetColumnFor(anchor, rowVisibleWidth(r.spans));
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
                r.targetColumn = targetColumnFor(anchor, rowVisibleWidth(r.spans));
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
        /// PAC carries the row's vertical placement (1..15) plus
        /// either the first span's foreground style (colour / italic /
        /// underline) or the row's indent slot — the spec puts colour
        /// and indent in the same 4-bit subfield, so the two are
        /// mutually exclusive on a single PAC.  The encoder picks
        /// indent over colour when the row needs a non-zero indent
        /// (a coloured + indented row degrades to white at the cost
        /// of preserving the horizontal position the cue's anchor
        /// requested).  Tab Offset codes (T1/T2/T3, doubled) ride
        /// after the PAC for the 1..3 column residual when the target
        /// column isn't a multiple of 4.
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
                // Resolve target column → PAC indent slot + Tab Offset
                // residual.  PAC indent is multiples of 4 (0/4/.../28);
                // Tab Offset shifts 1..3 cells.  Together they cover
                // 0..31 — the full row width.
                //
                // PAC's 4-bit subfield carries colour OR italic OR
                // indent — the three are mutually exclusive on a single
                // PAC.  When a row would otherwise need indent AND its
                // first span is non-white / italic, the encoder
                // preserves the visual style and drops horizontal
                // positioning back to flush-left (column 0).  Colour
                // is the more prominent cue feature for most viewers,
                // and SubRip files in the wild rarely combine both.
                const bool hasFirstSpanStyle =
                        initial.italic || initial.color != Cea608::CaptionColor::White;
                int targetCol = (laid.targetColumn < 0)
                                        ? 0
                                        : (laid.targetColumn > 31 ? 31 : laid.targetColumn);
                if (hasFirstSpanStyle) targetCol = 0;
                const int pacIndent = (targetCol / 4) * 4;
                const int tabResidual = targetCol - pacIndent;

                Cea608::PacAttr pac;
                pac.row = laid.row;
                pac.indentCol = pacIndent;
                pac.italic = initial.italic;
                pac.underline = initial.underline;
                pac.color = (pacIndent > 0) ? Cea608::CaptionColor::White : initial.color;
                uint8_t pb1 = 0, pb2 = 0;
                Cea608::encodePac(pac, pb1, pb2);
                body.bytes.pushToBack(ScheduledPair{pb1, pb2});
                body.bytes.pushToBack(ScheduledPair{pb1, pb2}); // doubled
                // Doubled Tab Offset for the 1..3 column residual.
                if (tabResidual > 0) {
                        uint8_t tb1 = 0, tb2 = 0;
                        Cea608::encodeTabOffset(tabResidual, tb1, tb2);
                        body.bytes.pushToBack(ScheduledPair{tb1, tb2});
                        body.bytes.pushToBack(ScheduledPair{tb1, tb2}); // doubled
                }
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

        /// @brief Per-batch wire state shared across the per-mode
        ///        schedule builders.
        ///
        /// Carries the bookkeeping that the builders previously held as
        /// local variables.  Hoisting it onto a shared struct lets the
        /// per-cue mode-mixing dispatcher chain three different
        /// builders together against the same wire timeline:
        /// @ref lastByteFrame is the universal "high-water mark" every
        /// builder consults to detect cross-mode pre-roll overlap;
        /// @ref pendingEdmFrame is the pop-on / paint-on deferred
        /// EDM that fires either when the next same-mode cue's pre-
        /// roll clears it, or when the dispatcher transitions out of
        /// the mode (it forces a flush so leftover displayed text
        /// doesn't bleed into the next mode); @ref rollUpInitialised
        /// + @ref rollUpInitialisedRows let the roll-up builder skip
        /// re-emitting RUx for back-to-back roll-up cues that share
        /// the same row count, and re-emit when re-entering roll-up
        /// from a different mode or when the row count changes.
        struct ModeBuilderState {
                        /// @brief Highest frame index that any byte
                        ///        has been written to.  Every builder
                        ///        updates this after emitting; the next
                        ///        cue's pre-roll must land strictly
                        ///        after this frame.
                        int64_t lastByteFrame = INT64_MIN;
                        /// @brief Pop-on specific: frame of the second
                        ///        EOC byte for the most recent pop-on
                        ///        cue.  Pop-on uses this for its tighter
                        ///        same-mode overlap rule (the EDM is
                        ///        deferred so doesn't count against the
                        ///        next cue's pre-roll budget).
                        int64_t lastEocFrame = INT64_MIN;
                        /// @brief Pop-on / paint-on deferred EDM frame.
                        ///        @c INT64_MIN when no EDM is pending.
                        int64_t pendingEdmFrame = INT64_MIN;
                        /// @brief Roll-up: @c true once a doubled RUx
                        ///        pair has been emitted and the receiver
                        ///        is in roll-up mode.
                        bool rollUpInitialised = false;
                        /// @brief Roll-up: row count of the most
                        ///        recently emitted RUx (2 / 3 / 4).  A
                        ///        new roll-up cue with a different
                        ///        @ref Subtitle::rollUpRows re-emits
                        ///        RUx with the new count.
                        int rollUpInitialisedRows = 0;
                        /// @brief One-shot latch for the bold-not-
                        ///        representable warning.
                        bool boldWarned = false;
        };

        /// @brief Maps a cue's @ref Subtitle::mode onto the encoder's
        ///        @ref Cea608Encoder::Mode, falling back to @p def
        ///        when the cue carries @c CaptionMode::Default.
        Cea608Encoder::Mode resolveCueMode(const Subtitle           &cue,
                                           Cea608Encoder::Mode       def) {
                const int v = cue.mode().value();
                if (v == CaptionMode::PopOn.value()) return Cea608Encoder::Mode::PopOn;
                if (v == CaptionMode::PaintOn.value()) return Cea608Encoder::Mode::PaintOn;
                if (v == CaptionMode::RollUp.value()) return Cea608Encoder::Mode::RollUp;
                return def;
        }

        /// @brief Resolves the roll-up row count for a cue.  Honours
        ///        @ref Subtitle::rollUpRows when the cue specifies
        ///        a value in @c [2, 4]; otherwise falls back to
        ///        @p defaultRows (the encoder's
        ///        @ref Cea608Encoder::Config::rollUpRows).  Always
        ///        returns a value in @c [2, 4].
        int resolveRollUpRows(const Subtitle &cue, int defaultRows) {
                int r = cue.rollUpRows();
                if (r < 2 || r > 4) r = defaultRows;
                if (r < 2) r = 2;
                if (r > 4) r = 4;
                return r;
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

        /// @brief Schedules a single pop-on cue, updating @p state.
        ///
        /// Walks the cue's wrap layout, splits into chunks if it
        /// exceeds @p maxRows, and emits @c [RCL,RCL, body..., EOC,EOC]
        /// per chunk with EDM deferred to @ref ModeBuilderState::pendingEdmFrame
        /// for the dispatcher to flush (either at the next cue's
        /// pre-roll boundary or at end-of-batch / mode-transition).
        Error encodePopOnCue(Cea608EncoderImpl *d, const Subtitle &cue, size_t cueIndex,
                             ModeBuilderState &state, int maxCols, int maxRows) {
                const int64_t cueStart = timeStampToFrame(cue.start(), d->cfg.frameRate);
                const int64_t cueEnd = timeStampToFrame(cue.end(), d->cfg.frameRate);
                if (cueEnd <= cueStart) return Error::Ok;

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
                if (layoutHadBold(wrap)) warnBoldOnce(state.boldWarned, cueIndex);

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
                                cueIndex, wrap.size(), maxRows, chunkCount);
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
                                            cueIndex, c, static_cast<long long>(firstFrame));
                                return Error::OutOfRange;
                        }
                        // Cross-mode and same-mode overlap: pop-on uses
                        // the tighter @ref ModeBuilderState::lastEocFrame
                        // for same-mode chains (EDM is deferred), but
                        // also the universal @ref ModeBuilderState::lastByteFrame
                        // so it never collides with bytes a different
                        // builder wrote earlier.
                        const int64_t overlapBoundary =
                                std::max(state.lastEocFrame, state.lastByteFrame);
                        if (firstFrame <= overlapBoundary) {
                                promekiWarn("Cea608Encoder::setSubtitles: cue %zu sub-%zu "
                                            "pre-roll (first frame %lld) overlaps prior "
                                            "cue's wire stream (last byte frame %lld)",
                                            cueIndex, c, static_cast<long long>(firstFrame),
                                            static_cast<long long>(overlapBoundary));
                                return Error::OutOfRange;
                        }
                        if (state.pendingEdmFrame != INT64_MIN) {
                                if (firstFrame > state.pendingEdmFrame + 1) {
                                        d->schedule.insert(state.pendingEdmFrame,
                                                           ScheduledPair{Cea608::EdmB1,
                                                                         Cea608::EdmB2});
                                        d->schedule.insert(state.pendingEdmFrame + 1,
                                                           ScheduledPair{Cea608::EdmB1,
                                                                         Cea608::EdmB2});
                                        if (state.pendingEdmFrame + 1 > state.lastByteFrame) {
                                                state.lastByteFrame = state.pendingEdmFrame + 1;
                                        }
                                }
                                state.pendingEdmFrame = INT64_MIN;
                        }

                        int64_t f = firstFrame;
                        d->schedule.insert(f++, ScheduledPair{Cea608::RclB1, Cea608::RclB2});
                        d->schedule.insert(f++, ScheduledPair{Cea608::RclB1, Cea608::RclB2});
                        for (size_t b = 0; b < body.bytes.size(); ++b) {
                                d->schedule.insert(f++, body.bytes[b]);
                        }
                        d->schedule.insert(f++, ScheduledPair{Cea608::EocB1, Cea608::EocB2});
                        d->schedule.insert(f++, ScheduledPair{Cea608::EocB1, Cea608::EocB2});

                        state.pendingEdmFrame = subEnd;
                        state.lastEocFrame = subStart + 1;
                        if (f - 1 > state.lastByteFrame) state.lastByteFrame = f - 1;
                        accChars += chunkChars;
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
        /// @brief Schedules a single paint-on cue, updating @p state.
        ///
        /// Per-cue layout:
        ///
        /// @code
        ///   [RDC,RDC, PAC,PAC, (chars|MR,MR)..., (deferred EDM at endFrame)]
        /// @endcode
        ///
        /// EDM is held in @ref ModeBuilderState::pendingEdmFrame and
        /// flushed by the dispatcher (either at the next cue's pre-
        /// roll boundary or at end-of-batch / mode-transition).
        Error encodePaintOnCue(Cea608EncoderImpl *d, const Subtitle &cue, size_t cueIndex,
                               ModeBuilderState &state, int maxCols, int maxRows) {
                const int64_t startFrame = timeStampToFrame(cue.start(), d->cfg.frameRate);
                const int64_t endFrame = timeStampToFrame(cue.end(), d->cfg.frameRate);
                if (endFrame <= startFrame) return Error::Ok;

                List<WrapRowSpans> wrap = wrapStyledCue(cue, maxCols, maxRows);
                if (wrap.isEmpty()) {
                        WrapRowSpans empty;
                        wrap.pushToBack(empty);
                }
                if (layoutHadBold(wrap)) warnBoldOnce(state.boldWarned, cueIndex);

                // Paint-on does not auto-split: the live char stream
                // is anchored at startFrame, and there's no clean
                // way to time-displace sub-cues within a paint-on
                // window without re-emitting RDC.  Flatten any
                // over-cap layout to a single row instead.
                if (maxRows > 0 && static_cast<int>(wrap.size()) > maxRows) {
                        promekiWarn("Cea608Encoder::setSubtitles[paint-on]: cue %zu wraps to "
                                    "%zu rows (> maxRows=%d); paint-on does not auto-split, "
                                    "flattening to a single row",
                                    cueIndex, wrap.size(), maxRows);
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
                        return Error::Ok;
                }
                const size_t  charCount = body.bytes.size() - 2;
                const int64_t firstFrame = startFrame - 4; // 2 RDC + 2 PAC
                const int64_t lastCharFrame = startFrame + static_cast<int64_t>(charCount) - 1;

                if (firstFrame < 0) {
                        promekiWarn("Cea608Encoder::setSubtitles[paint-on]: cue %zu pre-roll "
                                    "starts at frame %lld (before frame 0)",
                                    cueIndex, static_cast<long long>(firstFrame));
                        return Error::OutOfRange;
                }
                if (firstFrame <= state.lastByteFrame) {
                        promekiWarn("Cea608Encoder::setSubtitles[paint-on]: cue %zu pre-roll "
                                    "(first frame %lld) overlaps prior cue's wire stream "
                                    "(last byte frame %lld)",
                                    cueIndex, static_cast<long long>(firstFrame),
                                    static_cast<long long>(state.lastByteFrame));
                        return Error::OutOfRange;
                }
                if (charCount > 0 && lastCharFrame >= endFrame) {
                        promekiWarn("Cea608Encoder::setSubtitles[paint-on]: cue %zu has "
                                    "%zu char pairs (%lld frames) but only %lld frames "
                                    "of display time — chars would overrun cue end",
                                    cueIndex, charCount, static_cast<long long>(charCount),
                                    static_cast<long long>(endFrame - startFrame));
                        return Error::OutOfRange;
                }
                if (state.pendingEdmFrame != INT64_MIN) {
                        if (firstFrame > state.pendingEdmFrame + 1) {
                                d->schedule.insert(state.pendingEdmFrame,
                                                   ScheduledPair{Cea608::EdmB1, Cea608::EdmB2});
                                d->schedule.insert(state.pendingEdmFrame + 1,
                                                   ScheduledPair{Cea608::EdmB1, Cea608::EdmB2});
                                if (state.pendingEdmFrame + 1 > state.lastByteFrame) {
                                        state.lastByteFrame = state.pendingEdmFrame + 1;
                                }
                        }
                        state.pendingEdmFrame = INT64_MIN;
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

                state.pendingEdmFrame = endFrame;
                state.lastByteFrame = lastCharFrame;
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
        /// @brief Schedules a single roll-up cue, updating @p state.
        ///
        /// Per-cue layout:
        ///
        /// @code
        ///   First cue (or row count change):  [RUx,RUx, CR,CR, PAC,PAC, chars/MR..., (no EDM)]
        ///   Subsequent same-N cues:           [CR,CR, PAC,PAC, chars/MR..., (no EDM)]
        /// @endcode
        ///
        /// @c RUx is re-emitted whenever the receiver isn't already in
        /// roll-up mode (state.rollUpInitialised false) or when the
        /// requested row count differs from the last established one
        /// (state.rollUpInitialisedRows).
        Error encodeRollUpCue(Cea608EncoderImpl *d, const Subtitle &cue, size_t cueIndex,
                              int rollUpRows, ModeBuilderState &state, int maxCols) {
                const int64_t startFrame = timeStampToFrame(cue.start(), d->cfg.frameRate);
                const int64_t endFrame = timeStampToFrame(cue.end(), d->cfg.frameRate);
                if (endFrame <= startFrame) return Error::Ok;

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
                                    cueIndex, wrap.size());
                        List<LaidOutRow> flat = flattenToSingleRow(wrap, cue.anchor());
                        wrap.clear();
                        if (!flat.isEmpty()) wrap.pushToBack(flat[0].spans);
                }
                if (layoutHadBold(wrap)) warnBoldOnce(state.boldWarned, cueIndex);
                List<LaidOutRow> laidOut = placeChunk(wrap, 0, wrap.size(), cue.anchor());
                CueBody          body = buildCueBody(laidOut);
                if (body.bytes.size() < 2) return Error::Ok; // defensive

                // Body shape: [PAC,PAC,(MR,MR,)?chars...].
                // Per-cue pre-roll bytes (live chars excluded):
                //   Need RUx:  2 (RUx) + 2 (CR) + 2 (PAC) = 6
                //   Otherwise: 2 (CR) + 2 (PAC) = 4
                // Characters land at startFrame onward.
                const bool needRux = !state.rollUpInitialised
                                     || state.rollUpInitialisedRows != rollUpRows;
                const size_t  charCount = body.bytes.size() - 2;
                const size_t  preRoll = needRux ? 6 : 4;
                const int64_t firstFrame = startFrame - static_cast<int64_t>(preRoll);
                const int64_t lastCharFrame = startFrame + static_cast<int64_t>(charCount) - 1;

                if (firstFrame < 0) {
                        promekiWarn("Cea608Encoder::setSubtitles[roll-up]: cue %zu pre-roll "
                                    "starts at frame %lld (before frame 0)",
                                    cueIndex, static_cast<long long>(firstFrame));
                        return Error::OutOfRange;
                }
                if (firstFrame <= state.lastByteFrame) {
                        promekiWarn("Cea608Encoder::setSubtitles[roll-up]: cue %zu pre-roll "
                                    "(first frame %lld) overlaps prior cue's wire stream "
                                    "(last byte frame %lld)",
                                    cueIndex, static_cast<long long>(firstFrame),
                                    static_cast<long long>(state.lastByteFrame));
                        return Error::OutOfRange;
                }
                if (charCount > 0 && lastCharFrame >= endFrame) {
                        promekiWarn("Cea608Encoder::setSubtitles[roll-up]: cue %zu has "
                                    "%zu char pairs (%lld frames) but only %lld frames "
                                    "of display time before next cue",
                                    cueIndex, charCount, static_cast<long long>(charCount),
                                    static_cast<long long>(endFrame - startFrame));
                        return Error::OutOfRange;
                }

                int64_t f = firstFrame;
                if (needRux) {
                        const ScheduledPair ruPair = rollUpControl(rollUpRows);
                        d->schedule.insert(f++, ruPair);
                        d->schedule.insert(f++, ruPair);
                        state.rollUpInitialised = true;
                        state.rollUpInitialisedRows = rollUpRows;
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
                }
                // Live chars / mid-row pairs starting at startFrame.
                for (size_t b = 2; b < body.bytes.size(); ++b) {
                        d->schedule.insert(f++, body.bytes[b]);
                }
                state.lastByteFrame = lastCharFrame;
                return Error::Ok;
        }

        /// @brief Per-cue mode-mixing dispatcher.
        ///
        /// Walks @p subs in order, resolves each cue's effective mode
        /// (cue's explicit @ref Subtitle::mode, or @p
        /// Cea608Encoder::Config::mode for @c CaptionMode::Default),
        /// dispatches to the matching per-cue encoder, and shares
        /// the wire-state bookkeeping across cues via a single
        /// @ref ModeBuilderState.
        ///
        /// On every mode transition the dispatcher:
        ///   - Flushes any deferred pop-on / paint-on EDM (without
        ///     it the prior cue's text would bleed across the mode
        ///     boundary).
        ///   - Emits a doubled EDM after a roll-up segment to clear
        ///     the residual roll-up window before the next mode
        ///     starts writing.
        ///   - Resets @ref ModeBuilderState::rollUpInitialised so
        ///     re-entering roll-up forces a fresh RUx pair.
        Error buildMixedModeSchedule(Cea608EncoderImpl *d, const SubtitleList &subs) {
                ModeBuilderState         state;
                Cea608Encoder::Mode      currentMode = Cea608Encoder::Mode::PopOn;
                bool                     currentModeValid = false;
                const int                maxCols = d->cfg.maxCols;
                const int                maxRows = d->cfg.maxRows;
                for (size_t i = 0; i < subs.size(); ++i) {
                        const Subtitle           &cue = subs[i];
                        const Cea608Encoder::Mode cueMode = resolveCueMode(cue, d->cfg.mode);

                        if (currentModeValid && currentMode != cueMode) {
                                // Flush any pending EDM (pop-on / paint-on)
                                // so the residue clears before the new mode
                                // takes over.
                                if (state.pendingEdmFrame != INT64_MIN) {
                                        d->schedule.insert(
                                                state.pendingEdmFrame,
                                                ScheduledPair{Cea608::EdmB1, Cea608::EdmB2});
                                        d->schedule.insert(
                                                state.pendingEdmFrame + 1,
                                                ScheduledPair{Cea608::EdmB1, Cea608::EdmB2});
                                        if (state.pendingEdmFrame + 1 > state.lastByteFrame) {
                                                state.lastByteFrame = state.pendingEdmFrame + 1;
                                        }
                                        state.pendingEdmFrame = INT64_MIN;
                                }
                                // Leaving roll-up: clear the residual roll-up
                                // window so the next mode starts with empty
                                // displayed memory.
                                if (currentMode == Cea608Encoder::Mode::RollUp) {
                                        const int64_t edmFrame = state.lastByteFrame + 1;
                                        d->schedule.insert(
                                                edmFrame,
                                                ScheduledPair{Cea608::EdmB1, Cea608::EdmB2});
                                        d->schedule.insert(
                                                edmFrame + 1,
                                                ScheduledPair{Cea608::EdmB1, Cea608::EdmB2});
                                        state.lastByteFrame = edmFrame + 1;
                                }
                                // Re-entering roll-up requires re-emitting
                                // RUx; reset the latch.
                                state.rollUpInitialised = false;
                                state.rollUpInitialisedRows = 0;
                                // Pop-on's tighter same-mode overlap rule
                                // doesn't carry across mode boundaries — the
                                // generic @ref ModeBuilderState::lastByteFrame
                                // is the cross-mode authority.
                                state.lastEocFrame = INT64_MIN;
                        }
                        currentMode = cueMode;
                        currentModeValid = true;

                        Error err = Error::Ok;
                        switch (cueMode) {
                                case Cea608Encoder::Mode::PopOn:
                                        err = encodePopOnCue(d, cue, i, state, maxCols, maxRows);
                                        break;
                                case Cea608Encoder::Mode::PaintOn:
                                        err = encodePaintOnCue(d, cue, i, state, maxCols, maxRows);
                                        break;
                                case Cea608Encoder::Mode::RollUp: {
                                        const int rows = resolveRollUpRows(cue, d->cfg.rollUpRows);
                                        err = encodeRollUpCue(d, cue, i, rows, state, maxCols);
                                        break;
                                }
                        }
                        if (err.isError()) return err;
                }
                // Final flush of any pending EDM at end-of-batch.
                if (state.pendingEdmFrame != INT64_MIN) {
                        d->schedule.insert(state.pendingEdmFrame,
                                           ScheduledPair{Cea608::EdmB1, Cea608::EdmB2});
                        d->schedule.insert(state.pendingEdmFrame + 1,
                                           ScheduledPair{Cea608::EdmB1, Cea608::EdmB2});
                }
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

        // Build the schedule.  Walks the cue list once, dispatching
        // each cue to the matching per-mode encoder; mode transitions
        // between consecutive cues are handled by the dispatcher.
        Error err = buildMixedModeSchedule(d, subs);
        if (err.isError()) return err;

        // Apply the channel-bit OR-mask.  All schedule builders write
        // CC1 / CC3-shaped control bytes (bit 3 = 0); for CC2 / CC4
        // we OR @c 0x08 into every first-byte that lives in the
        // control range @c 0x10..0x1F.  Char first bytes (>= 0x20)
        // and the @c NUL filler (@c 0x00) are left untouched.
        const uint8_t channelOffset = channelOffsetFor(d->cfg.channel);
        if (channelOffset != 0) {
                Map<int64_t, ScheduledPair> shifted;
                for (auto it = d->schedule.constBegin(); it != d->schedule.constEnd(); ++it) {
                        ScheduledPair p = it->second;
                        if (p.b1 >= 0x10 && p.b1 <= 0x1F) {
                                p.b1 = static_cast<uint8_t>(p.b1 | channelOffset);
                        }
                        shifted.insert(it->first, p);
                }
                d->schedule = std::move(shifted);
        }
        return Error::Ok;
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
