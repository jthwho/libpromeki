/**
 * @file      cea608xdsinjector.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <climits>
#include <promeki/cea608.h>
#include <promeki/cea608xds.h>
#include <promeki/cea608xdsinjector.h>
#include <promeki/list.h>
#include <promeki/sharedptr.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

        /// @brief Composes a 16-bit key for a @c (class_, type) pair
        ///        used to index the source map.
        uint16_t makeKey(Cea608XdsClass class_, uint8_t type) {
                return static_cast<uint16_t>(
                        (static_cast<uint16_t>(class_) << 8) | static_cast<uint16_t>(type));
        }

        /// @brief CEA-608-E §E.10 priority arbitration — higher
        ///        value = higher urgency when the F2 channel is
        ///        contested by multiple eligible sources.
        ///
        /// The spec's tree (highest first):
        ///
        ///   1.  **Time of Day** (Misc / 0x01) — synchronisation-
        ///       critical, all receivers may rely on it for clocks
        ///       + EPG anchoring.
        ///   2.  **Content Advisory** (Current/Future / 0x05) —
        ///       V-chip / parental-control gating; the spec mandates
        ///       refresh every ≤ 3 seconds.
        ///   3.  **NWS WRSAME / Message** (Public / 0x01, 0x02) —
        ///       emergency notification; §9.6.2.13 places these
        ///       above copy-control signalling.
        ///   4.  **CGMS-A** (Current/Future / 0x08) — copy-control
        ///       state; spec mandates refresh every ≤ 5 seconds.
        ///   5.  **Current Class core fields** (Program ID 0x01,
        ///       Length 0x04, Name 0x03) — program identity, EPG.
        ///   6.  **All other Current Class**.
        ///   7.  **Channel Class** (Network Name, Call Letters,
        ///       TSID, Tape Delay).
        ///   8.  **Misc Class** (other than ToD).
        ///   9.  **Future Class**.
        ///   10. **Public Service Class** (non-NWS).
        ///   11. **Reserved Class**.
        ///   12. **Private Class**.
        ///
        /// Numbers were picked with deliberate gaps between tiers so
        /// future spec revisions can slot in between without
        /// re-numbering.
        int xdsPriority(Cea608XdsClass cls, uint8_t type) {
                switch (cls) {
                        case Cea608XdsClass::Misc:
                                if (type == 0x01) return 1100; // Time of Day
                                return 400;
                        case Cea608XdsClass::Current:
                                if (type == 0x05) return 1000; // Content Advisory
                                if (type == 0x01) return 750;  // Program ID
                                if (type == 0x04) return 750;  // Length / Time-in-Show
                                if (type == 0x03) return 700;  // Program Name
                                if (type == 0x08) return 800;  // CGMS-A (below NWS, above Current core)
                                return 600;
                        case Cea608XdsClass::Future:
                                // Future-class shadows Current with the
                                // same type-priority shape but at a
                                // lower base — the spec wants Current
                                // ahead of Future for the same metadata.
                                if (type == 0x05) return 550;  // Content Advisory (future)
                                if (type == 0x08) return 500;  // CGMS-A (future)
                                return 300;
                        case Cea608XdsClass::PublicSvc:
                                // §9.6.2.13: emergency notifications
                                // take precedence over copy-control
                                // signalling — WRSAME / NWS Message
                                // sit above CGMS-A.
                                if (type == 0x01) return 950;  // NWS WRSAME
                                if (type == 0x02) return 950;  // NWS Message
                                return 250;
                        case Cea608XdsClass::Channel:    return 500;
                        case Cea608XdsClass::Reserved:   return 100;
                        case Cea608XdsClass::PrivateData: return 50;
                        case Cea608XdsClass::Unknown:    return 0;
                }
                return 0;
        }

} // namespace

struct Cea608XdsInjectorImpl {
                PROMEKI_SHARED_FINAL(Cea608XdsInjectorImpl)

                struct Source {
                                Cea608XdsPacket  packet;
                                int              repetitionFrames = Cea608XdsInjector::DefaultRepetitionFrames;
                                /// @brief Frame at which this source last completed an emission.
                                ///        @c INT64_MIN means "never emitted yet" — guarantees a
                                ///        first emission whenever the scheduler ticks.
                                int64_t          lastEmittedFrame = INT64_MIN;
                                /// @brief @c true when the source has completed at least one
                                ///        emission AND @c repetitionFrames <= 0 (emit-once mode).
                                ///        Such sources are skipped by the scheduler thereafter.
                                bool             oneShotConsumed = false;
                };

                /// @brief Active sources keyed by (class, type).
                List<Source>       sources;
                /// @brief Current frame counter, advanced by @ref Cea608XdsInjector::nextPair.
                int64_t            currentFrame = -1;
                /// @brief Index into @ref sources of the currently in-flight source
                ///        (or -1 when no emission is active).
                int                inflightIndex = -1;
                /// @brief Pre-encoded wire byte stream for the in-flight source —
                ///        Start + Type + Informational pairs + End + Checksum.
                List<uint8_t>      inflightBytes;
                /// @brief Index into @ref inflightBytes of the next byte to emit.
                size_t             inflightCursor = 0;
};

Cea608XdsInjector::Cea608XdsInjector()
        : _d(SharedPtr<Cea608XdsInjectorImpl>::create()) {}

void Cea608XdsInjector::setPacket(const Cea608XdsPacket &packet, int repetitionFrames) {
        auto *d = _d.modify();
        const uint16_t key = makeKey(packet.class_, packet.type);
        // Replace existing source with the same key, preserving its
        // lastEmittedFrame counter so the new payload doesn't get an
        // unfair head start over peers.  If the source being replaced
        // is currently in-flight, abort the in-flight wire stream —
        // it still references the OLD payload's bytes and continuing
        // to emit them would mix two different packets on the wire.
        for (size_t i = 0; i < d->sources.size(); ++i) {
                if (makeKey(d->sources[i].packet.class_, d->sources[i].packet.type) == key) {
                        if (d->inflightIndex == static_cast<int>(i)) {
                                d->inflightIndex = -1;
                                d->inflightBytes = List<uint8_t>();
                                d->inflightCursor = 0;
                        }
                        d->sources[i].packet = packet;
                        d->sources[i].repetitionFrames = repetitionFrames;
                        d->sources[i].oneShotConsumed = false;
                        return;
                }
        }
        Cea608XdsInjectorImpl::Source src;
        src.packet = packet;
        src.repetitionFrames = repetitionFrames;
        d->sources.pushToBack(src);
}

void Cea608XdsInjector::removePacket(Cea608XdsClass class_, uint8_t type) {
        auto *d = _d.modify();
        const uint16_t key = makeKey(class_, type);
        for (size_t i = 0; i < d->sources.size(); ++i) {
                if (makeKey(d->sources[i].packet.class_, d->sources[i].packet.type) == key) {
                        // If the source we're removing is in flight,
                        // abort the in-flight emission so the next
                        // @ref nextPair picks a fresh source.
                        if (d->inflightIndex == static_cast<int>(i)) {
                                d->inflightIndex = -1;
                                d->inflightBytes = List<uint8_t>();
                                d->inflightCursor = 0;
                        } else if (d->inflightIndex > static_cast<int>(i)) {
                                // Removing an earlier element shifts the
                                // in-flight index down by one.
                                --d->inflightIndex;
                        }
                        d->sources.remove(i);
                        return;
                }
        }
}

size_t Cea608XdsInjector::sourceCount() const { return _d->sources.size(); }

void Cea608XdsInjector::reset() {
        auto *d = _d.modify();
        d->sources = List<Cea608XdsInjectorImpl::Source>();
        d->currentFrame = -1;
        d->inflightIndex = -1;
        d->inflightBytes = List<uint8_t>();
        d->inflightCursor = 0;
}

Cea708Cdp::CcData Cea608XdsInjector::nextPair(bool hasCaptionPair) {
        auto *d = _d.modify();
        ++d->currentFrame;
        Cea708Cdp::CcData out;
        out.valid = false;
        out.type = 1; // F2
        out.b1 = 0x00;
        out.b2 = 0x00;
        // §E.10: captions take precedence on F2.  When the caller
        // signals "captions claimed this frame", advance the frame
        // counter (so eligibility windows still tick) but hold any
        // in-flight wire stream's cursor and emit no XDS pair.
        if (hasCaptionPair) {
                return out;
        }
        // If no emission is in flight, pick the best-ranked source.
        // Selection follows CEA-608-E §E.10:
        //   1. Eligibility — only sources past their next-repetition
        //      window compete (those whose `lastEmittedFrame +
        //      repetitionFrames` is at or before `currentFrame`).
        //      One-shot sources that already fired are skipped.
        //   2. Priority — among eligible sources, highest
        //      @ref xdsPriority wins (see the priority table above).
        //   3. Overdue tiebreak — same-priority ties go to the
        //      longest-overdue source, so peer sources rotate fairly.
        if (d->inflightIndex < 0) {
                int      bestIdx = -1;
                int      bestPriority = INT_MIN;
                int64_t  bestOverdue = INT64_MIN;
                for (size_t i = 0; i < d->sources.size(); ++i) {
                        const auto &src = d->sources[i];
                        if (src.oneShotConsumed) continue;
                        int64_t overdue;
                        if (src.lastEmittedFrame == INT64_MIN) {
                                overdue = INT64_MAX / 2; // never emitted — max-overdue
                        } else {
                                overdue = d->currentFrame
                                        - (src.lastEmittedFrame
                                           + static_cast<int64_t>(src.repetitionFrames));
                        }
                        if (overdue < 0) continue;
                        int prio = xdsPriority(src.packet.class_, src.packet.type);
                        // §E.10 (informative): once a source is overdue
                        // by more than 2× its repetition period, escalate
                        // its tier so it stops being starved by higher-
                        // priority peers that keep claiming the slot.
                        // Skipped for never-emitted sources (overdue is
                        // already saturated) and for one-shot sources
                        // (repetitionFrames <= 0 ⇒ escalation is moot).
                        if (src.lastEmittedFrame != INT64_MIN
                            && src.repetitionFrames > 0
                            && overdue > 2 * static_cast<int64_t>(src.repetitionFrames)) {
                                prio += 200;
                        }
                        if (prio > bestPriority
                            || (prio == bestPriority && overdue > bestOverdue)) {
                                bestPriority = prio;
                                bestOverdue = overdue;
                                bestIdx = static_cast<int>(i);
                        }
                }
                if (bestIdx < 0) {
                        // No sources are due this frame — emit nothing.
                        return out;
                }
                // Begin emission of the chosen source.
                d->inflightIndex = bestIdx;
                d->inflightBytes = d->sources[bestIdx].packet.encode();
                d->inflightCursor = 0;
                if (d->inflightBytes.isEmpty()) {
                        // Encoder rejected the packet (Unknown class or
                        // type with bit 7 set) — drop the in-flight
                        // state and treat as no-emit.  The source stays
                        // registered but its @c lastEmittedFrame stays
                        // at INT64_MIN so subsequent frames will retry
                        // (typical caller will fix the registration).
                        d->inflightIndex = -1;
                        return out;
                }
        }
        // Emit the next byte pair from the in-flight wire stream.
        const size_t cursor = d->inflightCursor;
        if (cursor + 1 >= d->inflightBytes.size()) {
                // Defensive — shouldn't happen since encode() always
                // returns an even-byte count, but guard anyway.
                d->inflightIndex = -1;
                d->inflightBytes = List<uint8_t>();
                d->inflightCursor = 0;
                return out;
        }
        out.valid = true;
        out.b1 = d->inflightBytes[cursor];
        out.b2 = d->inflightBytes[cursor + 1];
        d->inflightCursor = cursor + 2;
        // If we've consumed the entire wire stream, mark the source
        // as just-completed.  Crucially we update lastEmittedFrame
        // and oneShotConsumed *only* at the end of a full emission —
        // if the in-flight cursor is interrupted by reset(),
        // removePacket(), or a setPacket() replacement, the source
        // stays in its "not yet emitted" state so the next eligible
        // tick restarts it from the Start byte.
        if (d->inflightCursor >= d->inflightBytes.size()) {
                const int idx = d->inflightIndex;
                if (idx >= 0 && idx < static_cast<int>(d->sources.size())) {
                        d->sources[idx].lastEmittedFrame = d->currentFrame;
                        if (d->sources[idx].repetitionFrames <= 0) {
                                d->sources[idx].oneShotConsumed = true;
                        }
                }
                d->inflightIndex = -1;
                d->inflightBytes = List<uint8_t>();
                d->inflightCursor = 0;
        }
        return out;
}

Cea708Cdp::CcData Cea608XdsInjector::nextPairWithParity(bool hasCaptionPair) {
        Cea708Cdp::CcData p = nextPair(hasCaptionPair);
        if (p.valid) {
                p.b1 = Cea608::withOddParity(p.b1);
                p.b2 = Cea608::withOddParity(p.b2);
        }
        return p;
}

PROMEKI_NAMESPACE_END
