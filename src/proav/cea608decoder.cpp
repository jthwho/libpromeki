/**
 * @file      cea608decoder.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdint>
#include <promeki/cea608.h>
#include <promeki/cea608decoder.h>
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

        /// @brief Maps a CEA-608 row (1..15) to the renderer-side
        ///        @ref SubtitleAnchor.  608 doesn't carry horizontal
        ///        placement, so we settle on the centred variant
        ///        per row group.
        SubtitleAnchor rowToAnchor(int row) {
                if (row >= 1 && row <= 4) return SubtitleAnchor::TopCenter;
                if (row >= 5 && row <= 10) return SubtitleAnchor::MiddleCenter;
                return SubtitleAnchor::BottomCenter; // 11..15
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
                        char tmp[2] = {static_cast<char>(c), 0};
                        loadingText += tmp;
                        if (currentMode == CurrentMode::PaintOn || currentMode == CurrentMode::RollUp) {
                                displayedFlat += tmp;
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

                void doRCL() {
                        // Resume Caption Loading: enter pop-on mode and
                        // clear non-displayed memory.  Reset the wire
                        // style to defaults (white, no italic, no
                        // underline).  The currently-displayed cue is
                        // untouched until the next EOC or EDM.
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
                        if (loadingHasPac && !loadingSpans.isEmpty()) {
                                loadingSpans.pushToBack(SubtitleSpan(String("\n")));
                        }
                        loadingAnchor = rowToAnchor(pac.row);
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
                        // Erase Non-displayed Memory.  In pop-on
                        // (where this is most often emitted) this
                        // clears the load buffer.  In other modes
                        // the loading buffer is the live cue, so
                        // ENM still clears it but loses the current
                        // paint-on / roll-up cue's chars.
                        loadingSpans = SubtitleSpan::List();
                        loadingText = String();
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

                                // Dispatch — try PAC and mid-row first
                                // (cover the 0x10..0x17 control space
                                // beyond just 0x14 misc), then the
                                // 0x14 misc codes.
                                if (Cea608::isPac(b1, b2)) {
                                        Cea608::PacAttr pac;
                                        if (Cea608::decodePac(b1, b2, pac)) doPac(pac, ts);
                                } else if (Cea608::isMidRow(b1, b2)) {
                                        Cea608::CaptionColor c;
                                        bool                 it = false, ul = false;
                                        if (Cea608::decodeMidRow(b1, b2, c, it, ul)) doMidRow(c, it, ul);
                                } else if (Cea608::isBgAttribute(b1, b2)) {
                                        Cea608::CaptionColor c;
                                        bool                 semi = false;
                                        if (Cea608::decodeBgAttribute(b1, b2, c, semi)) doBgAttribute(c, semi);
                                } else if (b1 == 0x14) {
                                        switch (b2) {
                                                case Cea608::MiscRCL: doRCL(); break;
                                                case Cea608::MiscRDC: doRDC(ts); break;
                                                case Cea608::MiscRU2: doRUx(2, ts); break;
                                                case Cea608::MiscRU3: doRUx(3, ts); break;
                                                case Cea608::MiscRU4: doRUx(4, ts); break;
                                                case Cea608::MiscCR:  doCR(ts); break;
                                                case Cea608::MiscEDM: doEDM(ts); break;
                                                case Cea608::MiscENM: doENM(); break;
                                                case Cea608::MiscEOC: doEOC(ts); break;
                                                default:
                                                        // BS / DER / FON / TR / RTD —
                                                        // v1 ignores.
                                                        break;
                                        }
                                }
                                // Other 0x1x first bytes (Tab Offsets,
                                // special-char tables): v1 ignores.

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
