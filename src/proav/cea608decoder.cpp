/**
 * @file      cea608decoder.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdint>
#include <promeki/cea608.h>
#include <promeki/cea608decoder.h>
#include <promeki/cea608ext.h>
#include <promeki/cea708cdp.h>
#include <promeki/color.h>
#include <promeki/enums.h>
#include <promeki/framenumber.h>
#include <promeki/logger.h>
#include <promeki/metadata.h>
#include <promeki/rect.h>
#include <promeki/sharedptr.h>
#include <promeki/string.h>
#include <promeki/subtitle.h>
#include <promeki/timestamp.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

        /// @brief Appends @p cp as UTF-8 onto @p out.
        ///
        /// Used by the CEA-608 character path to lift G0 / Special /
        /// Extended bytes into Unicode codepoints — most basic G0
        /// positions are plain ASCII, but ten G0 positions plus the
        /// Special / Extended tables produce non-ASCII codepoints
        /// (U+00E9 é, U+2122 ™, U+266A ♪ …) that must round-trip
        /// through the @ref SubtitleSpan text as proper UTF-8.
        void appendUtf8(String &out, uint32_t cp) {
                if (cp == 0) return;
                char buf[5];
                int  n = 0;
                if (cp <= 0x7F) {
                        buf[0] = static_cast<char>(cp);
                        n = 1;
                } else if (cp <= 0x7FF) {
                        buf[0] = static_cast<char>(0xC0 | (cp >> 6));
                        buf[1] = static_cast<char>(0x80 | (cp & 0x3F));
                        n = 2;
                } else if (cp <= 0xFFFF) {
                        buf[0] = static_cast<char>(0xE0 | (cp >> 12));
                        buf[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                        buf[2] = static_cast<char>(0x80 | (cp & 0x3F));
                        n = 3;
                } else {
                        buf[0] = static_cast<char>(0xF0 | (cp >> 18));
                        buf[1] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
                        buf[2] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                        buf[3] = static_cast<char>(0x80 | (cp & 0x3F));
                        n = 4;
                }
                buf[n] = 0;
                out += String::fromUtf8(buf, static_cast<size_t>(n));
        }

        /// @brief Returns the CDP @c cc_type for the configured
        ///        channel.  Field-1 channels map to cc_type 0;
        ///        field-2 channels map to cc_type 1.
        uint8_t ccTypeForChannel(Cea608Decoder::Channel ch) {
                switch (ch) {
                        case Cea608Decoder::Channel::CC1:
                        case Cea608Decoder::Channel::CC2: return 0;
                        case Cea608Decoder::Channel::CC3:
                        case Cea608Decoder::Channel::CC4: return 1;
                }
                return 0;
        }

        /// @brief Returns @c true when the configured channel is the
        ///        "first" channel of its field (CC1 / CC3).  Within a
        ///        field, the channel bit of the first byte of a
        ///        control code (bit 3 of @c b1 after parity strip)
        ///        distinguishes the two channels: 0 = first (CC1/CC3),
        ///        1 = second (CC2/CC4).
        bool channelIsFirstInField(Cea608Decoder::Channel ch) {
                return ch == Cea608Decoder::Channel::CC1 || ch == Cea608Decoder::Channel::CC3;
        }

        /// @brief Maps a CEA-608 row (1..15) plus the row's start
        ///        column (0..31) to the renderer-side
        ///        @ref SubtitleAnchor.
        ///
        /// Row group → vertical: 1..4 = Top, 5..10 = Middle,
        /// 11..15 = Bottom.  Column → horizontal: column 0..3 = Left,
        /// 4..23 = Center, 24..31 = Right.  The thresholds are
        /// pragmatic: a real broadcast captioner emits column 0 for
        /// flush-left, somewhere near the row's centre for centered,
        /// and column ≈ @c (32 - rowWidth) for flush-right; without
        /// the cue width at PAC time the decoder uses the start
        /// column alone as the horizontal-half discriminator.
        SubtitleAnchor rowToAnchor(int row, int column) {
                const bool isTop = (row >= 1 && row <= 4);
                const bool isMid = (row >= 5 && row <= 10);
                if (column < 4) {
                        if (isTop) return SubtitleAnchor::TopLeft;
                        if (isMid) return SubtitleAnchor::MiddleLeft;
                        return SubtitleAnchor::BottomLeft;
                }
                if (column < 24) {
                        if (isTop) return SubtitleAnchor::TopCenter;
                        if (isMid) return SubtitleAnchor::MiddleCenter;
                        return SubtitleAnchor::BottomCenter;
                }
                if (isTop) return SubtitleAnchor::TopRight;
                if (isMid) return SubtitleAnchor::MiddleRight;
                return SubtitleAnchor::BottomRight;
        }

        /// @brief Maps a CEA-608 primary palette index back to an
        ///        sRGB @ref Color.
        Color paletteColor(Cea608::CaptionColor c) {
                const Color::List p = Cea608::palette();
                const size_t      idx = static_cast<size_t>(c);
                if (idx >= p.size()) return Color::White;
                return p[idx];
        }

        /// @brief Current wire style; mirrors the encoder-side
        ///        tuple.  The decoder updates this on PAC and mid-
        ///        row receipts.
        struct WireStyle {
                        Cea608::CaptionColor color = Cea608::CaptionColor::White;
                        bool                 italic = false;
                        bool                 underline = false;
                        bool                 hasBg = false;
                        Cea608::CaptionColor bgColor = Cea608::CaptionColor::White;
                        bool                 bgSemiTransparent = false;
                        bool                 flash = false; ///< Set by FON.
        };

} // namespace

// ============================================================================
// Pimpl
// ============================================================================

struct Cea608DecoderImpl {
                PROMEKI_SHARED_FINAL(Cea608DecoderImpl)

                /// @brief Inferred operating mode from the most recently
                ///        seen mode-establishing control code (@c RCL,
                ///        @c RDC, @c RU2/3/4).  Default @c PopOn so
                ///        streams that omit the mode setup (legacy
                ///        captioners that don't send RCL/RDC explicitly)
                ///        still decode the pop-on byte stream.
                enum class CurrentMode {
                        PopOn  = 0,
                        PaintOn = 1,
                        RollUp  = 2,
                };

                Cea608Decoder::Config cfg;
                CurrentMode           currentMode = CurrentMode::PopOn;
                /// @brief Roll-up window size (2/3/4 rows visible).
                ///        Set on @c RUx; not currently used for cue
                ///        emission but kept for diagnostic and future
                ///        multi-row state.
                int rollUpRows = 2;

                // ---- Loading buffer (used by all modes) ----
                //
                // For PopOn this is the spec's "non-displayed memory":
                // characters accumulate here while the previous cue is
                // on screen; the buffer is swapped to displayed on EOC.
                //
                // For PaintOn and RollUp the buffer is *also* the live
                // display state — chars commit to it (and so to the
                // displayed view) immediately.  The buffer is emitted
                // as a cue on EDM (PaintOn) or CR / next CR / finalize
                // (RollUp).

                /// @brief Styled spans accumulated so far.
                SubtitleSpan::List loadingSpans;
                /// @brief Anchor implied by the most recent PAC.
                SubtitleAnchor loadingAnchor;
                /// @brief Current wire style — driven by PAC + mid-row
                ///        receipts; resets to defaults on RCL / RDC /
                ///        RUx.
                WireStyle loadingStyle;
                /// @brief Character accumulator for the *current* span
                ///        (the run of characters that has been seen
                ///        since the last PAC / mid-row code).  Flushed
                ///        into @ref loadingSpans whenever the style
                ///        changes or on EOC / CR / EDM.
                String loadingText;
                /// @brief @c true once a PAC has been received for the
                ///        loading buffer.  Drives whether we bother to
                ///        consult the anchor / styled spans on cue
                ///        emission.
                bool loadingHasPac = false;
                /// @brief Start column of the current loading row —
                ///        the most recent PAC's @c indentCol slot (in
                ///        @c {0, 4, 8, ..., 28}) plus any Tab Offset
                ///        shifts seen since that PAC.  Reset on every
                ///        PAC so multi-row cues track each row's
                ///        cursor independently.
                int loadingColumn = 0;
                /// @brief @c true while the loading buffer is still
                ///        accumulating the cue's first physical row.
                ///        The cue's @ref loadingAnchor (horizontal
                ///        half) is committed off the first row's
                ///        start column only — subsequent rows can be
                ///        at different widths and would otherwise
                ///        shift the recovered anchor mid-cue.  Goes
                ///        @c false on the second PAC (the row-break
                ///        boundary the encoder uses).
                bool loadingOnFirstRow = true;
                /// @brief Cue start timestamp for paint-on (set on
                ///        RDC, refined on first PAC / char) and the
                ///        start of the current row for roll-up.
                TimeStamp loadingStart;

                // ---- Displayed state ----

                /// @brief Spans currently visible.
                SubtitleSpan::List displayedSpans;
                /// @brief Anchor of the visible cue.
                SubtitleAnchor displayedAnchor;
                /// @brief @ref TimeStamp at which the @ref displayed
                ///        cue became visible (the @c EOC frame).
                TimeStamp displayedStart;
                /// @brief Flat text of the visible cue — recomputed
                ///        whenever @ref displayedSpans changes.  Kept
                ///        for the @ref Cea608Decoder::displayedText
                ///        accessor; the cue's authoritative span list
                ///        is @ref displayedSpans.
                String displayedFlat;
                /// @brief @c true when @ref displayedSpans holds a cue
                ///        that has not yet been emitted (no @c EDM or
                ///        replacing @c EOC has fired).
                bool cueDisplayed = false;

                /// @brief @ref TimeStamp of the most recent
                ///        @ref pushFrame call.  Used by
                ///        @ref finalize to close any still-displayed
                ///        cue.
                TimeStamp lastFrameTs;

                /// @brief Most recently processed control code's
                ///        first byte (post parity strip).  Used to
                ///        suppress the doubled-control-code spec
                ///        duplicate.
                uint8_t lastCtlB1 = 0xFF;
                /// @brief Most recently processed control code's
                ///        second byte (post parity strip).
                uint8_t lastCtlB2 = 0xFF;
                /// @brief When @c false, the next identical
                ///        control-code pair is treated as the spec
                ///        duplicate and skipped.  Reset to @c true on
                ///        a non-control byte pair so a character
                ///        between control codes breaks the
                ///        "consecutive" rule.
                bool dupConsumed = true;

                /// @brief Cues emitted so far.
                SubtitleList cues;

                /// @brief Appends one character byte to the current
                ///        span's text buffer.  Drops anything outside
                ///        0x20..0x7E (v1 doesn't model the 608
                ///        extended sets).
                ///
                /// For paint-on / roll-up modes the loading buffer is
                /// also the live display state — mirror the append
                /// into @ref displayedFlat so the @ref displayedText
                /// accessor stays current without callers having to
                /// flush a temporary span.
                void appendChar(uint8_t c) {
                        if (c < 0x20 || c > 0x7F) return;
                        // Translate the basic-G0 byte into its Unicode
                        // codepoint — most positions are plain ASCII,
                        // but ten remapped positions (0x2A=á / 0x5C=é /
                        // 0x5E=í / 0x5F=ó / 0x60=ú / 0x7B=ç / 0x7C=÷ /
                        // 0x7D=Ñ / 0x7E=ñ / 0x7F=█) decode to the
                        // mapped Latin / arithmetic glyph.
                        const uint32_t cp = Cea608Ext::decodeG0(c);
                        appendUtf8(loadingText, cp);
                        if (currentMode == CurrentMode::PaintOn || currentMode == CurrentMode::RollUp) {
                                appendUtf8(displayedFlat, cp);
                        }
                }

                /// @brief Replaces the most recently appended codepoint
                ///        in the loading buffer with @p cp.
                ///
                /// Backs the EIA-608-B "Special / Extended Character"
                /// receiver convention: the encoder emits a best-fit
                /// ASCII placeholder ahead of the doubled control
                /// pair so old decoders show a recognisable fallback;
                /// modern decoders follow the control receipt by
                /// replacing the placeholder with the real glyph.  No-op
                /// when the loading buffer is empty (the control code
                /// arrived without a preceding placeholder — out-of-
                /// spec stream that we tolerate by treating the code
                /// as a no-op).
                void replaceLastWithCodepoint(uint32_t cp) {
                        if (loadingText.isEmpty()) {
                                appendUtf8(loadingText, cp);
                                if (currentMode == CurrentMode::PaintOn
                                    || currentMode == CurrentMode::RollUp) {
                                        appendUtf8(displayedFlat, cp);
                                }
                                return;
                        }
                        loadingText = loadingText.left(loadingText.length() - 1);
                        appendUtf8(loadingText, cp);
                        if (currentMode == CurrentMode::PaintOn
                            || currentMode == CurrentMode::RollUp) {
                                if (!displayedFlat.isEmpty()) {
                                        displayedFlat = displayedFlat.left(displayedFlat.length() - 1);
                                }
                                appendUtf8(displayedFlat, cp);
                        }
                }

                /// @brief Builds a @ref SubtitleSpan from the current
                ///        loading style + text.  Invalid colour
                ///        sentinel is used for "white default" so the
                ///        renderer falls back to its configured
                ///        foreground.
                SubtitleSpan makeSpan(const String &text) const {
                        Color c; // invalid = inherit (white default)
                        if (loadingStyle.color != Cea608::CaptionColor::White) {
                                c = paletteColor(loadingStyle.color);
                        }
                        SubtitleSpan s(text, false /* bold not representable */,
                                       loadingStyle.italic, loadingStyle.underline, c);
                        if (loadingStyle.hasBg) {
                                s.setBackgroundColor(paletteColor(loadingStyle.bgColor));
                                s.setBackgroundOpacity(loadingStyle.bgSemiTransparent
                                                              ? SubtitleOpacity::Translucent
                                                              : SubtitleOpacity::Solid);
                        }
                        if (loadingStyle.flash) {
                                s.setForegroundOpacity(SubtitleOpacity::Flash);
                        }
                        return s;
                }

                /// @brief Flushes @ref loadingText into a span (when
                ///        non-empty) and clears the text buffer.
                void flushCurrentText() {
                        if (loadingText.isEmpty()) return;
                        loadingSpans.pushToBack(makeSpan(loadingText));
                        loadingText = String();
                }

                /// @brief Returns the concatenated text of @p spans.
                static String flatten(const SubtitleSpan::List &spans) {
                        String out;
                        for (size_t i = 0; i < spans.size(); ++i) out += spans[i].text();
                        return out;
                }

                /// @brief Maps the decoder's internal mode tracking onto
                ///        the codec-agnostic @ref CaptionMode that
                ///        @ref Subtitle::mode carries.  Pop-on cues land
                ///        through emitDisplayed (EOC committed); paint-on
                ///        and roll-up cues land through emitLoading.
                CaptionMode currentCaptionMode() const {
                        switch (currentMode) {
                        case CurrentMode::PopOn:  return CaptionMode::PopOn;
                        case CurrentMode::PaintOn: return CaptionMode::PaintOn;
                        case CurrentMode::RollUp: return CaptionMode::RollUp;
                        }
                        return CaptionMode::Default;
                }

                /// @brief Commits the currently @ref displayed cue
                ///        as a finalized @ref Subtitle.
                void emitDisplayed(const TimeStamp &end) {
                        if (!cueDisplayed) return;
                        Subtitle s(displayedStart, end, displayedSpans, displayedAnchor, Rect2Di32(),
                                   String(), Metadata());
                        // emitDisplayed only fires for pop-on (EOC swaps
                        // loading → displayed); stamp the mode so the
                        // recovered cue round-trips.
                        s.setMode(CaptionMode::PopOn);
                        cues.append(s);
                        displayedSpans = SubtitleSpan::List();
                        displayedAnchor = SubtitleAnchor::Default;
                        displayedStart = TimeStamp();
                        displayedFlat = String();
                        cueDisplayed = false;
                }

                void resetLoading() {
                        loadingSpans = SubtitleSpan::List();
                        loadingText = String();
                        loadingStyle = WireStyle();
                        loadingHasPac = false;
                        loadingColumn = 0;
                        loadingOnFirstRow = true;
                        loadingAnchor = SubtitleAnchor::Default;
                }

                /// @brief Emits the current loading buffer as a cue
                ///        with the given start / end timestamps and
                ///        clears the buffer.  Also clears the live
                ///        @ref displayedFlat mirror for paint-on /
                ///        roll-up modes.
                void emitLoading(const TimeStamp &start, const TimeStamp &end) {
                        flushCurrentText();
                        if (loadingSpans.isEmpty()) {
                                resetLoading();
                                if (currentMode == CurrentMode::PaintOn
                                    || currentMode == CurrentMode::RollUp) {
                                        displayedFlat = String();
                                }
                                return;
                        }
                        Subtitle s(start, end, loadingSpans,
                                   loadingHasPac ? loadingAnchor : SubtitleAnchor::Default, Rect2Di32(),
                                   String(), Metadata());
                        // emitLoading fires for paint-on (EDM commits the
                        // streaming chars) and roll-up (CR commits the
                        // prior row).  Stamp the live mode.
                        s.setMode(currentCaptionMode());
                        cues.append(s);
                        resetLoading();
                        if (currentMode == CurrentMode::PaintOn || currentMode == CurrentMode::RollUp) {
                                displayedFlat = String();
                        }
                }

                void doRCL(const TimeStamp &ts) {
                        // Resume Caption Loading: enter pop-on mode and
                        // clear non-displayed memory.  Reset the wire
                        // style to defaults (white, no italic, no
                        // underline).  The currently-displayed cue is
                        // untouched until the next EOC or EDM.
                        //
                        // Cross-mode flush: in paint-on / roll-up the
                        // loading buffer is the live displayed cue,
                        // not non-displayed memory — finalise it at
                        // @p ts so the visible content doesn't bleed
                        // across the mode switch.  (Encoder dispatcher
                        // emits its own EDM before re-entering pop-on,
                        // but a wild captioner stream might not.)
                        if (currentMode == CurrentMode::PaintOn
                            || currentMode == CurrentMode::RollUp) {
                                emitLoading(loadingStart, ts);
                        }
                        currentMode = CurrentMode::PopOn;
                        resetLoading();
                }

                void doRDC(const TimeStamp &ts) {
                        // Resume Direct Captioning: enter paint-on
                        // mode.  Per spec, paint-on writes to displayed
                        // memory directly.  We model that as: the
                        // loading buffer becomes the live cue, emitted
                        // on EDM.  Any in-flight pop-on cue is
                        // finalized at @p ts so it doesn't bleed across
                        // the mode change.
                        if (cueDisplayed) emitDisplayed(ts);
                        currentMode = CurrentMode::PaintOn;
                        resetLoading();
                        displayedFlat = String();
                        loadingStart = ts;
                }

                void doRUx(int rows, const TimeStamp &ts) {
                        // RU2/RU3/RU4: enter roll-up mode with N
                        // visible rows.  Per spec, RUx clears displayed
                        // memory and positions the cursor at row 15.
                        // Any in-flight pop-on cue is finalized.
                        if (cueDisplayed) emitDisplayed(ts);
                        currentMode = CurrentMode::RollUp;
                        rollUpRows = rows;
                        resetLoading();
                        displayedFlat = String();
                        loadingStart = ts;
                }

                void doCR(const TimeStamp &ts) {
                        if (currentMode != CurrentMode::RollUp) return;
                        // Carriage Return: finalize the current row as
                        // a cue (when non-empty) and start a new row.
                        emitLoading(loadingStart, ts);
                        loadingStart = ts;
                }

                void doPac(const Cea608::PacAttr &pac, const TimeStamp &ts) {
                        // PAC at the start of a line sets row + style.
                        // Mid-cue PACs (the encoder emits one per
                        // physical 608 row of a multi-row cue) signal
                        // a row break — push a "\n" marker span so
                        // the renderer breaks lines accordingly.
                        // Without this, multi-row 608 cues decode as
                        // a single concatenated horizontal line and
                        // blow past the 32-col / 3-row receiver grid.
                        flushCurrentText();
                        const bool isFirstPac = !loadingHasPac;
                        if (loadingHasPac && !loadingSpans.isEmpty()) {
                                loadingSpans.pushToBack(SubtitleSpan(String("\n")));
                                // We're past the first row now — Tab
                                // Offsets that follow must not shift
                                // the cue's anchor (it was committed
                                // off the first row's start column).
                                loadingOnFirstRow = false;
                        }
                        // Reset the per-row column tracker so the new
                        // row's Tab Offset shifts accumulate from the
                        // new PAC's indent slot — without this, a
                        // multi-row cue's second-row Tab Offsets would
                        // add to the first row's running column and
                        // mis-position any anchor refinement.
                        loadingColumn = pac.indentCol;
                        // Only the first PAC contributes to the cue's
                        // anchor.  Subsequent PACs are row breaks
                        // within the same cue and keep the first
                        // row's anchor in place (the cue gets one
                        // anchor regardless of how many physical rows
                        // it spans).
                        if (isFirstPac) {
                                loadingAnchor = rowToAnchor(pac.row, loadingColumn);
                        }
                        loadingStyle.color = pac.color;
                        loadingStyle.italic = pac.italic;
                        loadingStyle.underline = pac.underline;
                        loadingHasPac = true;
                        // Paint-on: PAC marks the start of a new cue
                        // (RDC sets the mode, PAC sets the row + start).
                        if (currentMode == CurrentMode::PaintOn && loadingSpans.isEmpty()
                            && loadingText.isEmpty()) {
                                loadingStart = ts;
                        }
                }

                /// @brief Honours a Tab Offset code.  Shifts the
                ///        loading-row's start column by 1..3 cells —
                ///        the receiver-side cursor advances by that
                ///        many positions before subsequent characters
                ///        land.
                ///
                /// Tab Offset is the residual companion to PAC indent
                /// (multiples of 4): together they cover the full
                /// 0..31 column range.  The decoder uses the combined
                /// column to refine the recovered cue's horizontal
                /// anchor (Center / Right vs Left).
                void doTabOffset(int columns) {
                        loadingColumn += columns;
                        if (loadingColumn > 31) loadingColumn = 31;
                        // Only refine the anchor on Tab Offsets that
                        // arrive before the first row break — past that
                        // point the cue's anchor was committed off the
                        // first row's start column and further-row
                        // Tab Offsets just shift the current row's
                        // cursor (not the cue's anchor).
                        if (loadingHasPac && loadingOnFirstRow) {
                                loadingAnchor =
                                        rowToAnchor(pacRowFor(loadingAnchor), loadingColumn);
                        }
                }

                /// @brief Reverse of @ref rowToAnchor for the
                ///        vertical-half — used by @ref doTabOffset
                ///        to refresh @c loadingAnchor without losing
                ///        the row group the prior PAC established.
                int pacRowFor(const SubtitleAnchor &a) const {
                        const int v = a.value();
                        if (v == SubtitleAnchor::TopLeft.value()
                            || v == SubtitleAnchor::TopCenter.value()
                            || v == SubtitleAnchor::TopRight.value()) return 1;
                        if (v == SubtitleAnchor::MiddleLeft.value()
                            || v == SubtitleAnchor::MiddleCenter.value()
                            || v == SubtitleAnchor::MiddleRight.value()) return 8;
                        return 15;
                }

                /// @brief Returns @c true when the last accumulated
                ///        span already ends with a space character, so
                ///        a freshly-arrived control code's display-cell
                ///        space would be redundant.  Used by
                ///        @ref doMidRow / @ref doBgAttribute to avoid
                ///        emitting a double inter-run gap when the
                ///        prior run was odd-length and the encoder
                ///        appended a pad-space to even out the byte
                ///        pair before the control code.
                bool lastSpanEndsWithSpace() const {
                        if (loadingSpans.isEmpty()) return false;
                        const SubtitleSpan &last = loadingSpans[loadingSpans.size() - 1];
                        const String       &t    = last.text();
                        if (t.isEmpty()) return false;
                        return t.cstr()[t.byteCount() - 1] == ' ';
                }

                void doMidRow(Cea608::CaptionColor c, bool italic, bool underline) {
                        flushCurrentText();
                        // The mid-row control code consumes one cell
                        // on screen, displayed as a styled space.
                        // The encoder (cea608encoder.cpp emitRowBytes)
                        // strips one leading space from the next run
                        // at this boundary so the MR cell serves as
                        // the inter-run visual separator.  Mirror
                        // that here by inserting a neutral (no-style)
                        // single-space span between the styled spans
                        // — keeps the underline / colour / italic
                        // exactly under the styled text without
                        // bleeding one cell into each neighbour.
                        //
                        // If the previous run was odd-length, the
                        // encoder already appended a pad-space to the
                        // last byte pair before this control code —
                        // that pad serves as the separator, so don't
                        // emit a second neutral span on top.
                        if (loadingHasPac && !loadingSpans.isEmpty() && !lastSpanEndsWithSpace()) {
                                loadingSpans.pushToBack(SubtitleSpan(String(" ")));
                        }
                        loadingStyle.color = c;
                        loadingStyle.italic = italic;
                        loadingStyle.underline = underline;
                }

                /// @brief Apply a CC1 background-attribute (EIA-608-B
                ///        §7.6) receipt: flush the current run and
                ///        switch the loading style's bg slot.  Future
                ///        @c flushCurrentText calls stamp the new bg on
                ///        every emitted span until the next bg /
                ///        carriage-return / PAC.
                void doBgAttribute(Cea608::CaptionColor c, bool semiTransparent) {
                        flushCurrentText();
                        // Bg-attribute also consumes one cell as a
                        // styled space — mirror the doMidRow handling
                        // so a bg-colour transition produces a
                        // neutral inter-run separator (or absorbs an
                        // existing pad-space) instead of baking a
                        // styled space into either neighbour.
                        if (loadingHasPac && !loadingSpans.isEmpty() && !lastSpanEndsWithSpace()) {
                                loadingSpans.pushToBack(SubtitleSpan(String(" ")));
                        }
                        loadingStyle.hasBg = true;
                        loadingStyle.bgColor = c;
                        loadingStyle.bgSemiTransparent = semiTransparent;
                }

                void doEOC(const TimeStamp &ts) {
                        // Pop-on only: swap non-displayed → displayed.
                        // In paint-on / roll-up mode EOC is unexpected;
                        // we treat it as a no-op to be permissive.
                        if (currentMode != CurrentMode::PopOn) return;
                        flushCurrentText();
                        if (cueDisplayed) emitDisplayed(ts);

                        displayedSpans = std::move(loadingSpans);
                        displayedAnchor = loadingHasPac ? loadingAnchor : SubtitleAnchor::Default;
                        displayedStart = ts;
                        displayedFlat = flatten(displayedSpans);
                        cueDisplayed = !displayedFlat.isEmpty();
                        if (!cueDisplayed) {
                                displayedSpans = SubtitleSpan::List();
                                displayedAnchor = SubtitleAnchor::Default;
                                displayedStart = TimeStamp();
                                displayedFlat = String();
                        }

                        loadingSpans = SubtitleSpan::List();
                        loadingText = String();
                        loadingStyle = WireStyle();
                        loadingHasPac = false;
                        loadingColumn = 0;
                        loadingOnFirstRow = true;
                        loadingAnchor = SubtitleAnchor::Default;
                }

                void doEDM(const TimeStamp &ts) {
                        // Pop-on: finalize the currently-displayed cue.
                        // Paint-on: finalize the loading buffer (which
                        //   was the live cue).
                        // Roll-up: finalize the current row.
                        if (currentMode == CurrentMode::PaintOn || currentMode == CurrentMode::RollUp) {
                                emitLoading(loadingStart, ts);
                                loadingStart = ts;
                                return;
                        }
                        if (cueDisplayed) emitDisplayed(ts);
                }

                void doENM() {
                        // Erase Non-displayed Memory.  Only meaningful
                        // in pop-on, where the loading buffer is the
                        // non-displayed memory.  In paint-on / roll-up
                        // the loading buffer is the live displayed cue
                        // (chars commit immediately) — ENM in those
                        // modes is a no-op per spec.
                        if (currentMode != CurrentMode::PopOn) return;
                        loadingSpans = SubtitleSpan::List();
                        loadingText = String();
                }

                /// @brief Honours the @c BS (Backspace) misc code.
                ///
                /// Removes the most recently appended codepoint from
                /// the loading buffer — used by live captioners for
                /// typo correction (the captioner sends the wrong
                /// char then immediately backspaces and sends the
                /// right one).  No-op when the buffer is empty.
                void doBS() {
                        if (!loadingText.isEmpty()) {
                                loadingText = loadingText.left(loadingText.length() - 1);
                                if (currentMode == CurrentMode::PaintOn
                                    || currentMode == CurrentMode::RollUp) {
                                        if (!displayedFlat.isEmpty()) {
                                                displayedFlat = displayedFlat.left(
                                                        displayedFlat.length() - 1);
                                        }
                                }
                                return;
                        }
                        // Loading text is empty — try the trailing span.
                        if (!loadingSpans.isEmpty()) {
                                SubtitleSpan &last =
                                        const_cast<SubtitleSpan &>(loadingSpans[loadingSpans.size() - 1]);
                                const String &t = last.text();
                                if (!t.isEmpty()) {
                                        last.setText(t.left(t.length() - 1));
                                }
                        }
                }

                /// @brief Honours the @c DER (Delete to End of Row)
                ///        misc code.
                ///
                /// Discards every character on the loading row from
                /// the cursor to the row's end.  Since the decoder
                /// doesn't track an explicit cursor (it accumulates
                /// chars into @ref loadingText), the closest semantic
                /// match is "drop everything in the loading buffer
                /// since the last PAC" — the paint-on / roll-up live
                /// captioning case where DER is most commonly used.
                void doDER() {
                        loadingText = String();
                        if (currentMode == CurrentMode::PaintOn
                            || currentMode == CurrentMode::RollUp) {
                                // Discard live spans built since the
                                // most recent PAC (the same boundary
                                // the encoder uses for row breaks).
                                loadingSpans = SubtitleSpan::List();
                                displayedFlat = String();
                        }
                }

                /// @brief Honours the @c FON (Flash On) misc code.
                ///
                /// Switches subsequent characters' foreground opacity
                /// to @ref SubtitleOpacity::Flash.  The flash flag
                /// persists until the next PAC or mid-row code resets
                /// the wire style.  The paint engine renders Flash
                /// as the renderer sees fit; the wire layer just
                /// records the intent on the styled span.
                void doFON() {
                        flushCurrentText();
                        loadingStyle.flash = true;
                }

                /// @brief Processes one parity-stripped byte pair.
                void process(uint8_t b1, uint8_t b2, const TimeStamp &ts) {
                        // Null pair: no-op.
                        if (b1 == 0x00 && b2 == 0x00) return;

                        // Control code (first byte in 0x10..0x1F).
                        if (b1 >= 0x10 && b1 <= 0x1F) {
                                // Channel-bit filter: bit 3 of b1
                                // selects channel within the field.
                                const bool isFirstInField = (b1 & 0x08) == 0;
                                if (isFirstInField != channelIsFirstInField(cfg.channel)) {
                                        return;
                                }

                                // Doubled-control-code spec rule:
                                // identical consecutive control pairs
                                // collapse to one.  A character pair
                                // between resets the rule.
                                if (!dupConsumed && b1 == lastCtlB1 && b2 == lastCtlB2) {
                                        dupConsumed = true;
                                        return;
                                }

                                // From here on we treat the control
                                // byte as channel-agnostic — the per-
                                // channel byte family (CC1 = 0x10..0x17,
                                // CC2 = 0x18..0x1F) is the same control
                                // code with just the channel selector
                                // bit toggled.  Mask the channel bit
                                // out so the @ref Cea608 helpers
                                // (which are written against the CC1
                                // byte layout) and the @c b1 == 0x14
                                // misc-control switch work for both
                                // sub-channels.
                                const uint8_t b1c = static_cast<uint8_t>(b1 & 0xF7);

                                // Dispatch — try PAC and mid-row first
                                // (cover the 0x10..0x17 control space
                                // beyond just 0x14 misc), then the
                                // 0x14 misc codes.
                                if (Cea608::isPac(b1c, b2)) {
                                        Cea608::PacAttr pac;
                                        if (Cea608::decodePac(b1c, b2, pac)) doPac(pac, ts);
                                } else if (Cea608::isMidRow(b1c, b2)) {
                                        Cea608::CaptionColor c;
                                        bool                 it = false, ul = false;
                                        if (Cea608::decodeMidRow(b1c, b2, c, it, ul)) doMidRow(c, it, ul);
                                } else if (Cea608::isTabOffset(b1c, b2)) {
                                        int columns = 0;
                                        if (Cea608::decodeTabOffset(b1c, b2, columns)) doTabOffset(columns);
                                } else if (Cea608::isBgAttribute(b1c, b2)) {
                                        Cea608::CaptionColor c;
                                        bool                 semi = false;
                                        if (Cea608::decodeBgAttribute(b1c, b2, c, semi)) doBgAttribute(c, semi);
                                } else if (b1c == 0x14) {
                                        switch (b2) {
                                                case Cea608::MiscRCL: doRCL(ts); break;
                                                case Cea608::MiscRDC: doRDC(ts); break;
                                                case Cea608::MiscRU2: doRUx(2, ts); break;
                                                case Cea608::MiscRU3: doRUx(3, ts); break;
                                                case Cea608::MiscRU4: doRUx(4, ts); break;
                                                case Cea608::MiscCR:  doCR(ts); break;
                                                case Cea608::MiscEDM: doEDM(ts); break;
                                                case Cea608::MiscENM: doENM(); break;
                                                case Cea608::MiscEOC: doEOC(ts); break;
                                                case Cea608::MiscBS:  doBS(); break;
                                                case Cea608::MiscDER: doDER(); break;
                                                case Cea608::MiscFON: doFON(); break;
                                                default:
                                                        // TR / RTD: text-channel
                                                        // codes — captioning
                                                        // doesn't model them.
                                                        break;
                                        }
                                } else if (b1c == 0x11 && b2 >= 0x30 && b2 <= 0x3F) {
                                        // Special Character (16 glyphs):
                                        // ® / ° / ½ / ¿ / ™ / ¢ / £ / ♪ /
                                        // à / NBSP / è / â / ê / î / ô /
                                        // û.  Replace the preceding
                                        // placeholder character with the
                                        // mapped codepoint.
                                        const uint32_t cp = Cea608Ext::decodeSpecial(b2);
                                        if (cp != Cea608Ext::NoCodepoint) {
                                                replaceLastWithCodepoint(cp);
                                        }
                                } else if (b1c == 0x12 && b2 >= 0x20 && b2 <= 0x3F) {
                                        // Extended Spanish / Misc.
                                        const uint32_t cp = Cea608Ext::decodeExtSpanish(b2);
                                        if (cp != Cea608Ext::NoCodepoint) {
                                                replaceLastWithCodepoint(cp);
                                        }
                                } else if (b1c == 0x13 && b2 >= 0x20 && b2 <= 0x3F) {
                                        // Extended Portuguese / German.
                                        const uint32_t cp = Cea608Ext::decodeExtFrench(b2);
                                        if (cp != Cea608Ext::NoCodepoint) {
                                                replaceLastWithCodepoint(cp);
                                        }
                                }
                                // Other 0x1x first bytes (Tab Offsets,
                                // unmodelled control space): v1 ignores.

                                lastCtlB1 = b1;
                                lastCtlB2 = b2;
                                dupConsumed = false;
                                return;
                        }

                        // Character pair (first byte >= 0x20).
                        appendChar(b1);
                        if (b2 >= 0x20) appendChar(b2);
                        dupConsumed = true;
                }
};

// ============================================================================
// Cea608Decoder
// ============================================================================

Cea608Decoder::Cea608Decoder() : _d(SharedPtr<Cea608DecoderImpl>::create()) {}

Cea608Decoder::Cea608Decoder(Config cfg) : _d(SharedPtr<Cea608DecoderImpl>::create()) {
        _d.modify()->cfg = cfg;
}

Cea608Decoder::~Cea608Decoder() = default;

const Cea608Decoder::Config &Cea608Decoder::config() const { return _d->cfg; }

void Cea608Decoder::reset() {
        auto *d = _d.modify();
        d->currentMode = Cea608DecoderImpl::CurrentMode::PopOn;
        d->rollUpRows = 2;
        d->loadingSpans = SubtitleSpan::List();
        d->loadingAnchor = SubtitleAnchor::Default;
        d->loadingStyle = WireStyle();
        d->loadingText = String();
        d->loadingHasPac = false;
        d->loadingColumn = 0;
        d->loadingOnFirstRow = true;
        d->loadingStart = TimeStamp();
        d->displayedSpans = SubtitleSpan::List();
        d->displayedAnchor = SubtitleAnchor::Default;
        d->displayedStart = TimeStamp();
        d->displayedFlat = String();
        d->cueDisplayed = false;
        d->lastFrameTs = TimeStamp();
        d->lastCtlB1 = 0xFF;
        d->lastCtlB2 = 0xFF;
        d->dupConsumed = true;
        d->cues = SubtitleList();
}

String Cea608Decoder::displayedText() const {
        // For pop-on, the live display is the swapped-in cue
        // (displayedFlat).  For paint-on and roll-up, the loading
        // buffer is the live display — return its flat text on the
        // fly.  We can't return a String& from a temporary, so the
        // pimpl caches displayedFlat for paint-on/roll-up callers
        // via the live-update path on appendChar.
        return _d->displayedFlat;
}

Subtitle Cea608Decoder::displayedCue() const {
        const Cea608DecoderImpl *d = _d.operator->();
        if (d->currentMode == Cea608DecoderImpl::CurrentMode::PaintOn
            || d->currentMode == Cea608DecoderImpl::CurrentMode::RollUp) {
                // Live state lives in the loading buffer.  Flush the
                // trailing text run into a temp span list before
                // returning so the renderer sees the current text.
                SubtitleSpan::List spans = d->loadingSpans;
                if (!d->loadingText.isEmpty()) {
                        Color c;
                        if (d->loadingStyle.color != Cea608::CaptionColor::White) {
                                c = paletteColor(d->loadingStyle.color);
                        }
                        spans.pushToBack(SubtitleSpan(d->loadingText, false, d->loadingStyle.italic,
                                                     d->loadingStyle.underline, c));
                }
                if (spans.isEmpty()) return Subtitle();
                return Subtitle(d->loadingStart, d->lastFrameTs, spans,
                                d->loadingHasPac ? d->loadingAnchor : SubtitleAnchor::Default,
                                Rect2Di32(), String(), Metadata());
        }
        if (!d->cueDisplayed || d->displayedSpans.isEmpty()) return Subtitle();
        return Subtitle(d->displayedStart, d->lastFrameTs, d->displayedSpans, d->displayedAnchor,
                        Rect2Di32(), String(), Metadata());
}

void Cea608Decoder::pushFrame(FrameNumber /*frame*/, TimeStamp ts, const Cea708Cdp::CcDataList &data) {
        auto         *d = _d.modify();
        const uint8_t wantCcType = ccTypeForChannel(d->cfg.channel);
        d->lastFrameTs = ts;
        for (size_t i = 0; i < data.size(); ++i) {
                const Cea708Cdp::CcData &t = data[i];
                if (!t.valid) continue;
                if (t.type != wantCcType) continue;
                // Validate parity; treat parity-fail as null pair.
                if (!Cea608::checkOddParity(t.b1) || !Cea608::checkOddParity(t.b2)) continue;
                d->process(Cea608::stripParity(t.b1), Cea608::stripParity(t.b2), ts);
        }
}

SubtitleList Cea608Decoder::finalize() {
        auto *d = _d.modify();
        // Close any still-live cue at the last-pushed timestamp.
        if (d->currentMode == Cea608DecoderImpl::CurrentMode::PaintOn
            || d->currentMode == Cea608DecoderImpl::CurrentMode::RollUp) {
                // Paint-on: still-painting cue gets end=lastFrameTs.
                // Roll-up: the final row (no trailing CR / EDM) gets
                // end=lastFrameTs.
                d->emitLoading(d->loadingStart, d->lastFrameTs);
        } else if (d->cueDisplayed) {
                d->emitDisplayed(d->lastFrameTs);
        }
        SubtitleList out = d->cues;
        // Reset for re-use.
        d->currentMode = Cea608DecoderImpl::CurrentMode::PopOn;
        d->rollUpRows = 2;
        d->loadingSpans = SubtitleSpan::List();
        d->loadingAnchor = SubtitleAnchor::Default;
        d->loadingStyle = WireStyle();
        d->loadingText = String();
        d->loadingHasPac = false;
        d->loadingColumn = 0;
        d->loadingOnFirstRow = true;
        d->loadingStart = TimeStamp();
        d->displayedSpans = SubtitleSpan::List();
        d->displayedAnchor = SubtitleAnchor::Default;
        d->displayedStart = TimeStamp();
        d->displayedFlat = String();
        d->cueDisplayed = false;
        d->lastFrameTs = TimeStamp();
        d->lastCtlB1 = 0xFF;
        d->lastCtlB2 = 0xFF;
        d->dupConsumed = true;
        d->cues = SubtitleList();
        return out;
}

PROMEKI_NAMESPACE_END
