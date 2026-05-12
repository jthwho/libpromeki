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
        };

} // namespace

// ============================================================================
// Pimpl
// ============================================================================

struct Cea608DecoderImpl {
                PROMEKI_SHARED_FINAL(Cea608DecoderImpl)

                Cea608Decoder::Config cfg;

                // ---- Pop-on loading state (non-displayed buffer) ----

                /// @brief Styled spans accumulated into non-displayed
                ///        memory.  Flushed on EOC into @ref displayed.
                SubtitleSpan::List loadingSpans;
                /// @brief Anchor implied by the most recent PAC.
                SubtitleAnchor loadingAnchor;
                /// @brief Current wire style — driven by PAC + mid-row
                ///        receipts; resets to defaults on RCL.
                WireStyle loadingStyle;
                /// @brief Character accumulator for the *current* span
                ///        (the run of characters that has been seen
                ///        since the last PAC / mid-row code).  Flushed
                ///        into @ref loadingSpans whenever the style
                ///        changes or on EOC.
                String loadingText;
                /// @brief @c true once a PAC has been received for the
                ///        non-displayed buffer.  Drives whether we
                ///        bother to consult the anchor / styled spans
                ///        on EOC vs falling back to plain-text legacy
                ///        behaviour.
                bool loadingHasPac = false;

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
                ///        non-displayed span's text buffer.  Drops
                ///        anything outside 0x20..0x7E (v1 doesn't
                ///        model the 608 extended sets).
                void appendChar(uint8_t c) {
                        if (c < 0x20 || c > 0x7F) return;
                        char tmp[2] = {static_cast<char>(c), 0};
                        loadingText += tmp;
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
                        return SubtitleSpan(text, false /* bold not representable */,
                                            loadingStyle.italic, loadingStyle.underline, c);
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

                /// @brief Commits the currently @ref displayed cue
                ///        as a finalized @ref Subtitle.
                void emitDisplayed(const TimeStamp &end) {
                        if (!cueDisplayed) return;
                        Subtitle s(displayedStart, end, displayedSpans, displayedAnchor, Rect2Di32(),
                                   String(), Metadata());
                        cues.append(s);
                        displayedSpans = SubtitleSpan::List();
                        displayedAnchor = SubtitleAnchor::Default;
                        displayedStart = TimeStamp();
                        displayedFlat = String();
                        cueDisplayed = false;
                }

                void doRCL() {
                        // Resume Caption Loading: clear non-displayed
                        // memory and reset the wire style to defaults
                        // (white, no italic, no underline).  The
                        // currently-displayed cue is untouched until
                        // the next EOC or EDM.
                        loadingSpans = SubtitleSpan::List();
                        loadingText = String();
                        loadingStyle = WireStyle();
                        loadingHasPac = false;
                        loadingAnchor = SubtitleAnchor::Default;
                }

                void doPac(const Cea608::PacAttr &pac) {
                        // PAC at the start of a line sets row + style.
                        // Mid-line PACs nominally move the cursor to a
                        // new row; v1 collapses them to "anchor +
                        // style update" without modeling cursor
                        // teleport (the encoder only emits one PAC
                        // per cue anyway, so this matches our round-
                        // trip contract).
                        flushCurrentText();
                        loadingAnchor = rowToAnchor(pac.row);
                        loadingStyle.color = pac.color;
                        loadingStyle.italic = pac.italic;
                        loadingStyle.underline = pac.underline;
                        loadingHasPac = true;
                }

                void doMidRow(Cea608::CaptionColor c, bool italic, bool underline) {
                        flushCurrentText();
                        loadingStyle.color = c;
                        loadingStyle.italic = italic;
                        loadingStyle.underline = underline;
                }

                void doEOC(const TimeStamp &ts) {
                        // Flush the trailing text run and swap
                        // non-displayed → displayed.  If a cue was
                        // already on screen, it ends now.
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
                        // RCL-style reset of the loading state — the
                        // sender will typically send another RCL
                        // before the next cue, but we reset here too
                        // so a non-conforming stream doesn't leak
                        // style across cues.
                        loadingText = String();
                        loadingStyle = WireStyle();
                        loadingHasPac = false;
                        loadingAnchor = SubtitleAnchor::Default;
                }

                void doEDM(const TimeStamp &ts) {
                        // Erase Displayed Memory: finalize the cue.
                        if (cueDisplayed) emitDisplayed(ts);
                }

                void doENM() {
                        // Erase Non-displayed Memory.  Common after
                        // RCL has already done the same; harmless to
                        // re-do.  Don't reset PAC state — ENM only
                        // touches the buffer.
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
                                        if (Cea608::decodePac(b1, b2, pac)) doPac(pac);
                                } else if (Cea608::isMidRow(b1, b2)) {
                                        Cea608::CaptionColor c;
                                        bool                 it = false, ul = false;
                                        if (Cea608::decodeMidRow(b1, b2, c, it, ul)) doMidRow(c, it, ul);
                                } else if (b1 == 0x14) {
                                        switch (b2) {
                                                case Cea608::MiscRCL: doRCL(); break;
                                                case Cea608::MiscEDM: doEDM(ts); break;
                                                case Cea608::MiscENM: doENM(); break;
                                                case Cea608::MiscEOC: doEOC(ts); break;
                                                default:
                                                        // BS / DER / RUx / FON / RDC /
                                                        // TR / RTD / CR — v1 ignores.
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
        d->loadingSpans = SubtitleSpan::List();
        d->loadingAnchor = SubtitleAnchor::Default;
        d->loadingStyle = WireStyle();
        d->loadingText = String();
        d->loadingHasPac = false;
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

const String &Cea608Decoder::displayedText() const { return _d->displayedFlat; }

Subtitle Cea608Decoder::displayedCue() const {
        if (!_d->cueDisplayed || _d->displayedSpans.isEmpty()) return Subtitle();
        // End is provisional — the cue is still live; the renderer
        // doesn't look at start/end anyway (cue selection happens
        // upstream of the renderer).  Pass the most-recent frame
        // timestamp so callers that *do* read the end at least see
        // a non-decreasing value.
        return Subtitle(_d->displayedStart, _d->lastFrameTs, _d->displayedSpans, _d->displayedAnchor,
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
        // Close any still-displayed cue at the last-pushed timestamp.
        if (d->cueDisplayed) d->emitDisplayed(d->lastFrameTs);
        SubtitleList out = d->cues;
        // Reset for re-use.
        d->loadingSpans = SubtitleSpan::List();
        d->loadingAnchor = SubtitleAnchor::Default;
        d->loadingStyle = WireStyle();
        d->loadingText = String();
        d->loadingHasPac = false;
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
