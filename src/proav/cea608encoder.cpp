/**
 * @file      cea608encoder.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

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

        /// @brief Maps a @ref SubtitleAnchor to a CEA-608 row.  608
        ///        only carries the vertical position (15 rows), so
        ///        horizontal placement (Left / Center / Right) is
        ///        dropped at the wire.
        int rowFromAnchor(const SubtitleAnchor &a) {
                const int v = a.value();
                if (v == SubtitleAnchor::TopLeft.value() || v == SubtitleAnchor::TopCenter.value()
                    || v == SubtitleAnchor::TopRight.value()) {
                        return 1;
                }
                if (v == SubtitleAnchor::MiddleLeft.value() || v == SubtitleAnchor::MiddleCenter.value()
                    || v == SubtitleAnchor::MiddleRight.value()) {
                        return 8;
                }
                // Default + Bottom* → row 15 (the SubRip / 608 convention).
                return 15;
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
        ///        encoder emits a mid-row code whenever this state
        ///        changes between spans.
        struct WireStyle {
                        Cea608::CaptionColor color = Cea608::CaptionColor::White;
                        bool                 italic = false;
                        bool                 underline = false;
                        bool                 operator==(const WireStyle &o) const {
                                return color == o.color && italic == o.italic && underline == o.underline;
                        }
                        bool operator!=(const WireStyle &o) const { return !(*this == o); }
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
                return ws;
        }

        /// @brief Body byte-pair plan for one cue: every byte-pair
        ///        from the first @c PAC double through the last
        ///        character pair (no RCL, no EOC, no EDM).
        ///
        /// Layout: [PAC, PAC, (MR, MR,)? chars..., (MR, MR,)? chars..., ...]
        ///
        /// The returned list's @c size() is the schedule's per-cue
        /// body length excluding the 2 RCL frames at the front and
        /// the 2 EOC frames at the end.  Total per-cue pre-roll
        /// frames = 2 (RCL) + body.size().
        struct CueBody {
                        List<ScheduledPair> bytes;     ///< Pre-parity (b1,b2) pairs to emit, frame-by-frame.
                        bool                hadBold;   ///< @c true when any span requested bold (warn flag).
        };

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

        CueBody buildCueBody(const Subtitle &cue) {
                CueBody body;
                body.hadBold = false;

                // Initial PAC: row from anchor, style from the first
                // non-empty span (or default White / no-italic / no-
                // underline when the cue has no spans).
                const SubtitleSpan::List &spans = cue.spans();
                WireStyle                 initial;
                for (size_t i = 0; i < spans.size(); ++i) {
                        if (spans[i].bold()) body.hadBold = true;
                        if (!spans[i].isEmpty()) {
                                initial = wireStyleFor(spans[i]);
                                break;
                        }
                }

                Cea608::PacAttr pac;
                pac.row = rowFromAnchor(cue.anchor());
                pac.indentCol = 0;
                pac.color = initial.color;
                pac.italic = initial.italic;
                pac.underline = initial.underline;
                uint8_t pb1 = 0, pb2 = 0;
                Cea608::encodePac(pac, pb1, pb2);
                body.bytes.pushToBack(ScheduledPair{pb1, pb2});
                body.bytes.pushToBack(ScheduledPair{pb1, pb2}); // doubled

                // Walk spans; emit a mid-row code when the wire style
                // changes between spans, then the span's character
                // pairs.  Skip empty spans entirely — they neither
                // change style observably nor produce characters.
                WireStyle current = initial;
                bool      anyCharsEmitted = false;
                for (size_t i = 0; i < spans.size(); ++i) {
                        const SubtitleSpan &span = spans[i];
                        if (span.isEmpty()) continue;
                        const WireStyle ws = wireStyleFor(span);
                        if (anyCharsEmitted && ws != current) {
                                uint8_t mb1 = 0, mb2 = 0;
                                Cea608::encodeMidRow(ws.color, ws.italic, ws.underline, mb1, mb2);
                                body.bytes.pushToBack(ScheduledPair{mb1, mb2});
                                body.bytes.pushToBack(ScheduledPair{mb1, mb2}); // doubled
                                current = ws;
                        }
                        List<ScheduledPair> chars = encodeCharPairs(span.text());
                        for (size_t c = 0; c < chars.size(); ++c) {
                                body.bytes.pushToBack(chars[c]);
                                anyCharsEmitted = true;
                        }
                        // First non-empty span's style is already
                        // baked into the PAC; subsequent spans need
                        // an MR if they differ.
                        if (!anyCharsEmitted) current = ws;
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
        Error buildPopOnSchedule(Cea608EncoderImpl *d, const SubtitleList &subs) {
                // Walk cues in chronological order.  EDM scheduling is
                // *deferred*: a cue's EDM is held aside and only
                // committed when we know the next cue's pre-roll doesn't
                // overlap it (real-world encoders elide the EDM in that
                // case).
                int64_t lastEocFrame = INT64_MIN;
                int64_t pendingEdmFrame = INT64_MIN;
                bool    boldWarned = false;
                for (size_t i = 0; i < subs.size(); ++i) {
                        const Subtitle &cue = subs[i];

                        const int64_t startFrame = timeStampToFrame(cue.start(), d->cfg.frameRate);
                        const int64_t endFrame = timeStampToFrame(cue.end(), d->cfg.frameRate);
                        if (endFrame <= startFrame) continue;

                        CueBody body = buildCueBody(cue);
                        if (body.hadBold) warnBoldOnce(boldWarned, i);

                        const size_t  preRollFrames = 2 + body.bytes.size();
                        const int64_t firstFrame = startFrame - static_cast<int64_t>(preRollFrames);

                        if (firstFrame < 0) {
                                promekiWarn("Cea608Encoder::setSubtitles: cue %zu pre-roll "
                                            "starts at frame %lld (before frame 0); cue.start "
                                            "is too close to media t=0 for the cue's length",
                                            i, static_cast<long long>(firstFrame));
                                return Error::OutOfRange;
                        }
                        if (firstFrame <= lastEocFrame) {
                                promekiWarn("Cea608Encoder::setSubtitles: cue %zu pre-roll (first "
                                            "frame %lld) overlaps prior cue's wire stream (last "
                                            "byte frame %lld)",
                                            i, static_cast<long long>(firstFrame),
                                            static_cast<long long>(lastEocFrame));
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
                        d->schedule.insert(f++, ScheduledPair{Cea608::RclB1, Cea608::RclB2});
                        d->schedule.insert(f++, ScheduledPair{Cea608::RclB1, Cea608::RclB2});
                        for (size_t b = 0; b < body.bytes.size(); ++b) {
                                d->schedule.insert(f++, body.bytes[b]);
                        }
                        d->schedule.insert(f++, ScheduledPair{Cea608::EocB1, Cea608::EocB2});
                        d->schedule.insert(f++, ScheduledPair{Cea608::EocB1, Cea608::EocB2});

                        pendingEdmFrame = endFrame;
                        lastEocFrame = startFrame + 1;
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
                for (size_t i = 0; i < subs.size(); ++i) {
                        const Subtitle &cue = subs[i];

                        const int64_t startFrame = timeStampToFrame(cue.start(), d->cfg.frameRate);
                        const int64_t endFrame = timeStampToFrame(cue.end(), d->cfg.frameRate);
                        if (endFrame <= startFrame) continue;

                        CueBody body = buildCueBody(cue);
                        if (body.hadBold) warnBoldOnce(boldWarned, i);

                        // Body shape from buildCueBody: [PAC,PAC,(MR,MR,)?chars...].
                        // Paint-on splits that into:
                        //   pre-roll (lands before startFrame): RDC,RDC + PAC,PAC = 4 frames
                        //   live chars (land at startFrame onward): body.bytes minus the
                        //                                           leading PAC,PAC.
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
                int64_t lastByteFrame = INT64_MIN;
                bool    rollUpInitialised = false;
                bool    boldWarned = false;
                int     lastEmittedRow = -1; // tracks when we can skip PAC re-emission.
                for (size_t i = 0; i < subs.size(); ++i) {
                        const Subtitle &cue = subs[i];

                        const int64_t startFrame = timeStampToFrame(cue.start(), d->cfg.frameRate);
                        const int64_t endFrame = timeStampToFrame(cue.end(), d->cfg.frameRate);
                        if (endFrame <= startFrame) continue;

                        CueBody body = buildCueBody(cue);
                        if (body.hadBold) warnBoldOnce(boldWarned, i);
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

        switch (d->cfg.mode) {
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
        const CueBody body = buildCueBody(cue);
        int64_t firstFrame = 0;
        switch (_d->cfg.mode) {
                case Mode::PopOn:
                        // Pre-roll = 2 (RCL) + body (PAC + chars + MRs).  EOC
                        // pair lands at and after startFrame.
                        firstFrame = startFrame - static_cast<int64_t>(2 + body.bytes.size());
                        break;
                case Mode::PaintOn:
                        // Pre-roll = 4 (RDC + PAC), chars stream live from startFrame.
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
                const CueBody body = buildCueBody(cue);
                size_t        preRoll = 0;
                int64_t       lastCharFrame = INT64_MIN;
                bool          charsOverrun = false;
                size_t        charCount = 0;
                if (body.bytes.size() >= 2) charCount = body.bytes.size() - 2;
                switch (_d->cfg.mode) {
                        case Mode::PopOn:
                                preRoll = 2 + body.bytes.size();
                                lastCharFrame = startFrame + 1; // EOC doubled at startFrame, startFrame+1
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
