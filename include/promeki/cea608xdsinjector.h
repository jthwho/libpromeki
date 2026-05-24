/**
 * @file      cea608xdsinjector.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/config.h>
#if PROMEKI_ENABLE_PROAV
#include <cstdint>
#include <promeki/cea608xds.h>
#include <promeki/cea708cdp.h>
#include <promeki/list.h>
#include <promeki/namespace.h>
#include <promeki/sharedptr.h>

PROMEKI_NAMESPACE_BEGIN

struct Cea608XdsInjectorImpl; // Pimpl — defined in cea608xdsinjector.cpp.

/**
 * @brief CEA-608-E Annex E XDS encoder / scheduler.
 * @ingroup proav
 *
 * Companion to @ref Cea608XdsExtractor: holds a set of registered
 * XDS packet sources (each keyed by @c (class, type)) and emits
 * their wire byte pairs on a per-frame schedule suitable for
 * muxing into a field-2 caption stream.  This implements the
 * encoder side of CEA-608-E §9 and the Annex E "Encoder Manufacturers"
 * recommendations (basic subset — see "Scope" below).
 *
 * @par Usage
 *
 * A typical broadcaster builds an injector at session start,
 * registers the packets they want to transmit (Network Name,
 * Call Letters, Time of Day, Content Advisory, …), and then on
 * every frame calls @ref nextPair to get the F2 byte pair to
 * emit.  Captions take priority on the F2 channel — when the
 * upstream caption encoder has scheduled a byte pair for the
 * current frame, the injector's pair is held back.  Idle F2
 * frames (no caption pair due) carry the injector's output.
 *
 * @code
 * Cea608XdsInjector inj;
 *
 * // Register Network Name "ACME" — refresh every 60 frames (2 s @ 30 fps).
 * Cea608XdsPacket nwn;
 * nwn.class_ = Cea608XdsClass::Channel;
 * nwn.type   = 0x01;
 * nwn.payload.pushToBack('A');
 * nwn.payload.pushToBack('C');
 * nwn.payload.pushToBack('M');
 * nwn.payload.pushToBack('E');
 * inj.setPacket(nwn, 60);
 *
 * for (int64_t f = 0; ; ++f) {
 *     // Tell the injector whether captioning has already claimed
 *     // this frame's F2 slot — when true the scheduler holds the
 *     // in-flight cursor instead of consuming the next byte pair.
 *     const bool captionsWonF2 = captionScheduler.hasPairForFrame(f);
 *     Cea708Cdp::CcData xdsPair = inj.nextPairWithParity(captionsWonF2);
 *     if (xdsPair.valid) {
 *         // ... mux into the F2 cc_data slot for this frame.
 *     }
 * }
 * @endcode
 *
 * @par Scheduling policy
 *
 * Per CEA-608-E §E.10: sources are first filtered by eligibility
 * (those whose @c lastEmittedFrame + @c repetitionFrames is at or
 * before the current frame) and then ranked by an inherent
 * priority derived from the packet's @c (class_, type).  Priority
 * tiers, highest first:
 *
 *  - **Time of Day** (Misc / 0x01) — synchronisation-critical.
 *  - **Content Advisory** (Current / 0x05) — V-chip / parental
 *    control gating; spec mandates ≤ 3 s refresh.
 *  - **NWS WRSAME / NWS Message** (Public / 0x01, 0x02) —
 *    emergency notification; §9.6.2.13 places these above
 *    copy-control signalling.
 *  - **CGMS-A** (Current / 0x08) — copy-control state;
 *    spec mandates ≤ 5 s refresh.
 *  - **Current Class core fields** (Program ID 0x01, Length 0x04,
 *    Name 0x03).
 *  - All other Current Class.
 *  - Channel Class (Network Name, Call Letters, TSID, Tape Delay).
 *  - Misc Class (other than Time of Day).
 *  - Future Class shadow of the above.
 *  - Public Service Class (non-NWS).
 *  - Reserved / Private classes.
 *
 * Same-priority ties go to the longest-overdue source so peer
 * sources rotate fairly.  Callers can fine-tune effective
 * bandwidth share via per-source @c repetitionFrames (lower =
 * more bandwidth).
 *
 * @par Bandwidth
 *
 * §E.3 caps field-2 at 60 characters / second (one byte pair per
 * frame at 30 fps, two characters per byte pair, so 60 chars/sec).
 * The injector emits at most one byte pair per @ref nextPair call;
 * the caller's frame rate dictates effective bandwidth.  If the
 * injector consistently has more packets queued than can fit, the
 * repetition period for low-priority packets will naturally
 * stretch.
 *
 * @par Scope (what this minimal subset implements)
 *
 *  - Per-source byte-pair scheduling with configurable repetition.
 *  - Wire-format encoding via @ref Cea608XdsPacket::encode (Start,
 *    Type, Informational pairs, End + Checksum).
 *  - @ref Cea608::withOddParity stamping in @ref nextPairWithParity.
 *  - Empty-when-idle: @ref nextPair returns @c valid=false when no
 *    sources are due.
 *
 * @par What this does NOT implement (deferred from §E.7–§E.10)
 *
 *  - §E.4 packet integration with upstream XDS data (caller is
 *    expected to merge upstream and locally-generated XDS into a
 *    single injector or to drop one stream).
 *  - §E.8.4.4 Composite-1 / Composite-2 packing — caller must
 *    build composite payloads directly if desired.
 *  - §E.5 field reversal correction — the injector emits whatever
 *    cc_type==1 the caller asked for; if the upstream signal has
 *    F1/F2 swapped, the caller resolves it before this layer.
 *  - §E.7.1 re-encoding delays — the injector is a fresh source,
 *    not a re-encoder.
 *
 * @par Storage and copy semantics
 *
 * Stateful worker, pimpl-backed.  Copies share state.  Construct
 * one per encoding session.
 *
 * @par Thread Safety
 *
 * Not thread-safe.  @ref nextPair / @ref setPacket / @ref removePacket
 * must be called serially.
 *
 * @see Cea608XdsExtractor, Cea608XdsPacket
 */
class Cea608XdsInjector {
        public:
                /// @brief Default repetition period in frames — 60 frames
                ///        is 2 seconds at 30 fps, a reasonable cadence
                ///        for non-urgent metadata like Network Name.
                static constexpr int DefaultRepetitionFrames = 60;

                Cea608XdsInjector();

                /// @brief Registers or replaces a packet source.
                ///
                /// @param packet           The packet to emit.  Its @c class_,
                ///                         @c type, and @c payload are
                ///                         captured; the encoded byte
                ///                         stream is regenerated each
                ///                         emission via @ref Cea608XdsPacket::encode.
                /// @param repetitionFrames Minimum frames between
                ///                         successive emissions of this
                ///                         packet.  Zero or negative
                ///                         means "emit once and don't
                ///                         repeat."  Default
                ///                         @ref DefaultRepetitionFrames.
                ///
                /// Calling @ref setPacket again with the same
                /// @c (class_, type) replaces the prior payload and
                /// repetition setting; the source's "last emitted"
                /// counter is preserved so the new payload doesn't
                /// monopolise the channel.
                void setPacket(const Cea608XdsPacket &packet,
                               int repetitionFrames = DefaultRepetitionFrames);

                /// @brief Removes the registered source for the given
                ///        @c (class_, type).  No-op when no such source
                ///        is registered.
                void removePacket(Cea608XdsClass class_, uint8_t type);

                /// @brief Returns the number of currently-registered
                ///        sources.
                size_t sourceCount() const;

                /// @brief Advances the scheduler by one frame and
                ///        returns the next byte pair to emit (or an
                ///        invalid @c CcData when the injector is idle).
                ///
                /// @param hasCaptionPair  @c true when the upstream
                ///        caption encoder has already claimed this
                ///        frame's F2 byte pair (CC3 / CC4 captioning
                ///        or T3 / T4 text data).  Per CEA-608-E §E.10
                ///        captions take precedence on the F2 channel —
                ///        when this is true the injector advances the
                ///        frame counter but does NOT consume the next
                ///        byte pair from any in-flight wire stream
                ///        and returns @c valid=false.  This preserves
                ///        in-flight emission cursors across caption
                ///        bursts so a multi-pair XDS packet doesn't
                ///        lose bytes when the caption channel briefly
                ///        wins arbitration.
                ///
                /// The returned @c CcData carries pre-parity bytes
                /// (0x00..0x7F).  @c type is always @c 1 (field 2)
                /// when @c valid is @c true.  Use @ref nextPairWithParity
                /// when integrating with a parity-stamped CDP wire.
                ///
                /// Idle behaviour: when no source is due, the returned
                /// pair has @c valid=false.  Callers should emit a
                /// null pair (or whatever F2 caption byte is otherwise
                /// scheduled) in that case.
                Cea708Cdp::CcData nextPair(bool hasCaptionPair = false);

                /// @brief As @ref nextPair, but stamps odd parity on
                ///        the result's @c b1 / @c b2 so it can be
                ///        muxed directly into a parity-stamped CDP
                ///        stream.
                Cea708Cdp::CcData nextPairWithParity(bool hasCaptionPair = false);

                /// @brief Clears all registered sources and resets the
                ///        frame counter / scheduling state.
                void reset();

        private:
                SharedPtr<Cea608XdsInjectorImpl> _d;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_PROAV
