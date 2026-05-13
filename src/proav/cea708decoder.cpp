/**
 * @file      cea708decoder.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdint>
#include <promeki/cea708cdp.h>
#include <promeki/cea708decoder.h>
#include <promeki/cea708service.h>
#include <promeki/cea708windowstate.h>
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

        /// @brief Maps a CEA-708 DefineWindow @c anchor_point (1..9,
        ///        reading-order convention: 1=TopLeft .. 9=BottomRight)
        ///        back onto a @ref SubtitleAnchor (numpad / @c {\anN}
        ///        convention: 1=BottomLeft .. 9=TopRight).  The
        ///        horizontal axis matches; vertical flips.
        int subtitleAnchorFrom708(uint8_t anchorPoint) {
                switch (anchorPoint) {
                        case 1: return SubtitleAnchor::TopLeft.value();
                        case 2: return SubtitleAnchor::TopCenter.value();
                        case 3: return SubtitleAnchor::TopRight.value();
                        case 4: return SubtitleAnchor::MiddleLeft.value();
                        case 5: return SubtitleAnchor::MiddleCenter.value();
                        case 6: return SubtitleAnchor::MiddleRight.value();
                        case 7: return SubtitleAnchor::BottomLeft.value();
                        case 8: return SubtitleAnchor::BottomCenter.value();
                        case 9: return SubtitleAnchor::BottomRight.value();
                        default: return SubtitleAnchor::Default.value();
                }
        }

} // namespace

// ============================================================================
// Pimpl
// ============================================================================

struct Cea708DecoderImpl {
                PROMEKI_SHARED_FINAL(Cea708DecoderImpl)

                Cea708Decoder::Config cfg;

                Cea708WindowState        windows;
                /// @brief Triples accumulated for the in-flight DTVCC
                ///        packet (starts on a @c cc_type=2 triple,
                ///        continues until the packet's byte budget is
                ///        consumed or the next @c cc_type=2 triple
                ///        arrives).
                Cea708Cdp::CcDataList    pending;
                /// @brief Bytes remaining in the current packet
                ///        (computed from the header byte's
                ///        @c packet_size_code).  Zero means "no
                ///        in-flight packet".
                size_t                   pendingRemaining = 0;

                /// @brief Most-recent pushFrame timestamp.
                TimeStamp lastFrameTs;
                /// @brief Cached visible-text snapshot from the
                ///        previous frame; drives the
                ///        "did the cue content change?" comparison.
                String prevVisible;
                /// @brief Snapshot of @ref Cea708WindowState::visibleSpans
                ///        captured while the in-flight cue was on
                ///        screen.  Reused at cue-finalise time because
                ///        the live state has already moved on (e.g.
                ///        HideWindow flipped @c visible=false so a fresh
                ///        @c visibleSpans would return empty).
                SubtitleSpan::List prevSpans;
                /// @brief Captured caption mode for the in-flight cue,
                ///        snapshotted alongside @ref prevSpans so a
                ///        post-Hide finalise still reports the mode
                ///        the cue was authored in.
                CaptionMode prevMode = CaptionMode::Default;
                /// @brief Captured anchor for the in-flight cue,
                ///        recovered from the visible window's
                ///        @c anchor_point at the moment content was
                ///        first written.  Persists past HideWindow.
                SubtitleAnchor prevAnchor = SubtitleAnchor::Default;
                /// @brief Start timestamp of the currently in-flight
                ///        cue (valid only when @ref prevVisible is
                ///        non-empty).
                TimeStamp currentCueStart;

                /// @brief Cues emitted so far.
                SubtitleList cues;

                /// @brief Dispatches a complete DTVCC packet through
                ///        the window-state machine.  Per CEA-708, every
                ///        service block in the packet that targets the
                ///        configured @ref Cea708Decoder::Config::serviceNumber
                ///        is concatenated into one byte stream, then
                ///        handed to @ref Cea708WindowState::processBytes
                ///        in a single call.  Concatenating before the
                ///        per-byte parser runs is what allows multi-byte
                ///        commands (e.g. DefineWindow's 7-byte form) to
                ///        be split across the 31-byte block boundary —
                ///        @ref Cea708WindowState::processBytes does not
                ///        carry partial-command state across calls.
                void processCompletePacket() {
                        if (pending.isEmpty()) return;
                        Result<Cea708DtvccPacket> r = Cea708DtvccPacket::fromCcData(pending);
                        pending = Cea708Cdp::CcDataList();
                        pendingRemaining = 0;
                        if (r.second().isError()) {
                                // Malformed packet — drop it silently; the
                                // window state stays where it was.
                                return;
                        }
                        const Cea708DtvccPacket &pkt = r.first();
                        const auto              &blocks = pkt.serviceBlocks();
                        // First pass: total size for the configured
                        // service number (stop at the null block, which
                        // terminates the packet's block list).
                        size_t total = 0;
                        for (size_t i = 0; i < blocks.size(); ++i) {
                                const Cea708Service &svc = blocks[i];
                                if (svc.isNull()) break;
                                if (svc.serviceNumber() != cfg.serviceNumber) continue;
                                total += svc.data().size();
                        }
                        if (total == 0) return;
                        // Second pass: concatenate the matching blocks'
                        // bytes into a single contiguous buffer.
                        std::vector<uint8_t> bytes;
                        bytes.reserve(total);
                        for (size_t i = 0; i < blocks.size(); ++i) {
                                const Cea708Service &svc = blocks[i];
                                if (svc.isNull()) break;
                                if (svc.serviceNumber() != cfg.serviceNumber) continue;
                                const auto *p = static_cast<const uint8_t *>(svc.data().data());
                                const size_t n = svc.data().size();
                                for (size_t k = 0; k < n; ++k) bytes.push_back(p[k]);
                        }
                        windows.processBytes(bytes.data(), bytes.size());
                }

                /// @brief Best-effort @ref CaptionMode recovery from
                ///        the current window state.  Multi-row windows
                ///        with no Hide pending decode as @c RollUp;
                ///        single-row windows default to @c PopOn (the
                ///        common broadcast case) — the wire layer
                ///        cannot reliably distinguish pop-on from
                ///        paint-on without temporal context the
                ///        decoder doesn't currently retain.
                CaptionMode currentCaptionMode() const {
                        const Cea708Window &w = windows.currentWindow();
                        if (w.rowCount > 1) return CaptionMode::RollUp;
                        return CaptionMode::PopOn;
                }

                /// @brief Builds a @ref Subtitle from @p text + the
                ///        decoder's current mode + the visible
                ///        @ref SubtitleSpan list reconstructed from
                ///        every cell's pen state.  Multi-style cues
                ///        (multiple SPA / SPC commands within one
                ///        cue's character stream) recover with full
                ///        span boundaries — consecutive cells whose
                ///        pen state matches collapse into a single
                ///        span; runs whose pen differs become separate
                ///        spans.
                /// @brief Builds a @ref Subtitle from @p text and a
                ///        captured @p spans / @p mode snapshot.  The
                ///        snapshot is taken while the cue is on screen
                ///        because the live window state may have
                ///        already moved past (HideWindow, etc.) by the
                ///        time @ref recordCueBoundaries fires.
                Subtitle makeCue(const TimeStamp &start, const TimeStamp &end, const String &text,
                                  const SubtitleSpan::List &spans, const CaptionMode &mode,
                                  const SubtitleAnchor &anchor) const {
                        Subtitle s(start, end, text);
                        s.setMode(mode);
                        s.setAnchor(anchor);
                        if (!spans.isEmpty()) s.setSpans(spans);
                        return s;
                }

                /// @brief Recovers the @ref SubtitleAnchor of the
                ///        currently-visible window (the first one in
                ///        priority order with content) by mapping its
                ///        @c anchor_point through @ref subtitleAnchorFrom708.
                ///        Returns @c SubtitleAnchor::Default when no
                ///        visible window carries content.
                SubtitleAnchor currentAnchor() const {
                        const Cea708Window &w = windows.currentWindow();
                        return SubtitleAnchor(subtitleAnchorFrom708(static_cast<uint8_t>(w.anchorPoint)));
                }

                /// @brief Compares the current visible text against
                ///        the cached snapshot and records cue
                ///        boundaries against the given timestamp.
                void recordCueBoundaries(const TimeStamp &ts) {
                        const String now = windows.visibleText();
                        if (now == prevVisible) {
                                // Content unchanged, but styling may have
                                // — refresh the cached snapshot so SPA /
                                // SPC commands fired mid-cue land on the
                                // recovered span.
                                if (!now.isEmpty()) {
                                        prevSpans = windows.visibleSpans();
                                        prevMode = currentCaptionMode();
                                        prevAnchor = currentAnchor();
                                }
                                return;
                        }
                        // Content changed.  If we were displaying
                        // something, finalise it with the snapshot
                        // captured while the cue was on screen.
                        if (!prevVisible.isEmpty()) {
                                cues.append(makeCue(currentCueStart, ts, prevVisible, prevSpans,
                                                     prevMode, prevAnchor));
                        }
                        if (!now.isEmpty()) {
                                currentCueStart = ts;
                                prevSpans = windows.visibleSpans();
                                prevMode = currentCaptionMode();
                                prevAnchor = currentAnchor();
                        } else {
                                currentCueStart = TimeStamp();
                                prevSpans = SubtitleSpan::List();
                                prevMode = CaptionMode::Default;
                                prevAnchor = SubtitleAnchor::Default;
                        }
                        prevVisible = now;
                }

                /// @brief Inserts the bytes of a single cc_data triple
                ///        into the in-flight packet buffer.  When the
                ///        packet completes (byte budget reached) it
                ///        gets dispatched to the window state.
                void pushTriple(const Cea708Cdp::CcData &t) {
                        if (t.type == Cea708DtvccPacket::CcTypePacketStart) {
                                // Boundary: flush any in-flight packet first
                                // (a new packet-start triple implicitly ends
                                // the previous packet — typically the previous
                                // one was complete already, but a producer that
                                // pads early is permitted).
                                if (pendingRemaining > 0) {
                                        processCompletePacket();
                                }
                                pending = Cea708Cdp::CcDataList();
                                pending.pushToBack(t);
                                // Compute the packet's total wire byte count
                                // from the header: packet_size_code in the
                                // low 6 bits of b1.  Wire 0 means "128 bytes";
                                // non-zero means the exact count.
                                const uint8_t sizeCode = static_cast<uint8_t>(t.b1 & 0x3F);
                                const size_t  packetSize = (sizeCode == 0) ? 128u : sizeCode;
                                // First triple already carries 2 wire bytes
                                // (the header + the first payload byte).
                                if (packetSize <= 2) {
                                        // Single-triple packet — process now.
                                        processCompletePacket();
                                } else {
                                        pendingRemaining = packetSize - 2;
                                }
                                return;
                        }
                        if (t.type != Cea708DtvccPacket::CcTypePacketData) {
                                // Not a DTVCC triple — caller's filter
                                // should have stripped this already.
                                return;
                        }
                        if (pendingRemaining == 0) {
                                // Stray data triple with no in-flight
                                // packet — drop it.
                                return;
                        }
                        pending.pushToBack(t);
                        // Each data triple carries 2 wire bytes.
                        if (pendingRemaining <= 2) {
                                pendingRemaining = 0;
                                processCompletePacket();
                        } else {
                                pendingRemaining -= 2;
                        }
                }
};

// ============================================================================
// Cea708Decoder
// ============================================================================

Cea708Decoder::Cea708Decoder() : _d(SharedPtr<Cea708DecoderImpl>::create()) {}

Cea708Decoder::Cea708Decoder(Config cfg) : _d(SharedPtr<Cea708DecoderImpl>::create()) {
        _d.modify()->cfg = cfg;
}

Cea708Decoder::~Cea708Decoder() = default;

const Cea708Decoder::Config &Cea708Decoder::config() const { return _d->cfg; }

void Cea708Decoder::reset() {
        auto *d = _d.modify();
        d->windows.reset();
        d->pending = Cea708Cdp::CcDataList();
        d->pendingRemaining = 0;
        d->lastFrameTs = TimeStamp();
        d->prevVisible = String();
        d->prevSpans = SubtitleSpan::List();
        d->prevMode = CaptionMode::Default;
        d->prevAnchor = SubtitleAnchor::Default;
        d->currentCueStart = TimeStamp();
        d->cues = SubtitleList();
}

String Cea708Decoder::displayedText() const { return _d->prevVisible; }

Subtitle Cea708Decoder::displayedCue() const {
        if (_d->prevVisible.isEmpty()) return Subtitle();
        return _d->makeCue(_d->currentCueStart, _d->lastFrameTs, _d->prevVisible, _d->prevSpans,
                           _d->prevMode, _d->prevAnchor);
}

const Cea708WindowState &Cea708Decoder::windowState() const { return _d->windows; }

void Cea708Decoder::pushFrame(FrameNumber /*frame*/, TimeStamp ts, const Cea708Cdp::CcDataList &data) {
        auto *d = _d.modify();
        d->lastFrameTs = ts;
        for (size_t i = 0; i < data.size(); ++i) {
                const Cea708Cdp::CcData &t = data[i];
                if (!t.valid) continue;
                if (t.type != Cea708DtvccPacket::CcTypePacketStart
                    && t.type != Cea708DtvccPacket::CcTypePacketData) {
                        // CEA-608 triple — skip.
                        continue;
                }
                d->pushTriple(t);
        }
        d->recordCueBoundaries(ts);
}

SubtitleList Cea708Decoder::finalize() {
        auto *d = _d.modify();
        // Close any still-displayed cue at the last-pushed timestamp.
        if (!d->prevVisible.isEmpty()) {
                Subtitle s = d->makeCue(d->currentCueStart, d->lastFrameTs, d->prevVisible,
                                         d->prevSpans, d->prevMode, d->prevAnchor);
                d->cues.append(s);
        }
        SubtitleList out = d->cues;
        // Reset for re-use.
        d->windows.reset();
        d->pending = Cea708Cdp::CcDataList();
        d->pendingRemaining = 0;
        d->lastFrameTs = TimeStamp();
        d->prevVisible = String();
        d->prevSpans = SubtitleSpan::List();
        d->prevMode = CaptionMode::Default;
        d->prevAnchor = SubtitleAnchor::Default;
        d->currentCueStart = TimeStamp();
        d->cues = SubtitleList();
        return out;
}

PROMEKI_NAMESPACE_END
