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
                /// @brief Start timestamp of the currently in-flight
                ///        cue (valid only when @ref prevVisible is
                ///        non-empty).
                TimeStamp currentCueStart;

                /// @brief Cues emitted so far.
                SubtitleList cues;

                /// @brief Dispatches a complete DTVCC packet through
                ///        the window-state machine.  Filters service
                ///        blocks by the configured service number.
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
                        for (size_t i = 0; i < blocks.size(); ++i) {
                                const Cea708Service &svc = blocks[i];
                                if (svc.isNull()) break;
                                if (svc.serviceNumber() != cfg.serviceNumber) continue;
                                windows.processServiceBytes(svc);
                        }
                }

                /// @brief Compares the current visible text against
                ///        the cached snapshot and records cue
                ///        boundaries against the given timestamp.
                void recordCueBoundaries(const TimeStamp &ts) {
                        const String now = windows.visibleText();
                        if (now == prevVisible) return;
                        // Content changed.  If we were displaying
                        // something, finalise it.
                        if (!prevVisible.isEmpty()) {
                                Subtitle s(currentCueStart, ts, prevVisible);
                                cues.append(s);
                        }
                        if (!now.isEmpty()) {
                                currentCueStart = ts;
                        } else {
                                currentCueStart = TimeStamp();
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
        d->currentCueStart = TimeStamp();
        d->cues = SubtitleList();
}

String Cea708Decoder::displayedText() const { return _d->prevVisible; }

Subtitle Cea708Decoder::displayedCue() const {
        if (_d->prevVisible.isEmpty()) return Subtitle();
        return Subtitle(_d->currentCueStart, _d->lastFrameTs, _d->prevVisible);
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
                Subtitle s(d->currentCueStart, d->lastFrameTs, d->prevVisible);
                d->cues.append(s);
        }
        SubtitleList out = d->cues;
        // Reset for re-use.
        d->windows.reset();
        d->pending = Cea708Cdp::CcDataList();
        d->pendingRemaining = 0;
        d->lastFrameTs = TimeStamp();
        d->prevVisible = String();
        d->currentCueStart = TimeStamp();
        d->cues = SubtitleList();
        return out;
}

PROMEKI_NAMESPACE_END
