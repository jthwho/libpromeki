/**
 * @file      ancframesync.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_PROAV
#include <promeki/namespace.h>
#include <promeki/ancpacket.h>
#include <promeki/anctranslateconfig.h>
#include <promeki/anctranslator.h>
#include <promeki/ancformat.h>
#include <promeki/frame.h>
#include <promeki/framesyncdisposition.h>
#include <promeki/list.h>
#include <promeki/map.h>
#include <promeki/result.h>
#include <promeki/set.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Pipeline stage that applies a @ref FrameSyncDisposition to
 *        the ANC payload of one frame.
 * @ingroup proav
 *
 * @c AncFrameSync is the ANC-side companion to a video frame sync
 * (software-paced or hardware-paced).  Given an input @ref Frame and
 * the per-slot @ref FrameSyncDisposition the surrounding stage
 * decided on, it walks every ANC packet on the frame, dispatches each
 * through @ref AncTranslator::applySyncPolicy, and emits 0..N output
 * frames whose @ref AncPayload entries reflect the policy's per-format
 * decisions:
 *
 *  - @c Drop     — produces no output frame.  Every ANC packet on the
 *                  input frame is still dispatched through its sync
 *                  policy with disposition @c Drop; any packets the
 *                  policy returns (the SCTE-104 stash-and-forward path
 *                  is the canonical example) are captured into the
 *                  internal stash, keyed by @c AncFormat::ID.
 *  - @c Play     — produces one output frame; every packet runs
 *                  through its sync policy with @c repeatIndex = 0.
 *                  After the policy walk, every stashed packet is
 *                  appended to the frame's first @ref AncPayload
 *                  (creating one if the frame had none), and the
 *                  stash is cleared.
 *  - @c Repeat[N]— produces N output frames whose AncPayload entries
 *                  are mutated independently with @c repeatIndex =
 *                  @c 0, @c 1, …, @c N-1.  Video and audio payloads
 *                  are shared across the N frames via @ref Frame
 *                  copy-on-write — only the AncPayload detaches per
 *                  slot.  The stash drains onto output frame @c 0
 *                  only — subsequent slots in the same Repeat run see
 *                  no stashed packets.
 *
 * @par Stash semantics
 *
 * The stash is bounded at one packet per @c AncFormat::ID
 * (latest-wins replacement on a second drop while the same format is
 * already stashed, with a one-shot warning).  This matches the
 * spec's "one packet per stash-eligible category."  The stash is
 * cleared on every drain; codecs that need a deeper queue (a future
 * SCTE-104 cue with non-zero @c splice_time) can revisit when a real
 * workflow demands it.
 *
 * Per-format policy comes from the @ref AncTranslator::SyncPolicyFn
 * registry; codecs without a registered policy fall through to
 * @ref AncTranslator's default (@c Drop = drop, @c Play / @c Repeat =
 * copy through).  @c AncFrameSync logs that fallback once per
 * unregistered format per instance — exactly the place per-instance
 * state belongs, since @c AncTranslator's dispatch path is otherwise
 * stateless.
 *
 * @par Hardware vs software frame sync
 *
 * @c AncFrameSync is independent of who is making the disposition
 * decision.  A software frame sync computes the disposition from
 * input/output rate ratios + drift; a hardware frame sync (AJA
 * Genlock, NDI internal sync) reads the disposition from the card.
 * In either case, the consumer is the only thing that changes:
 * software paths emit the resulting @c Frame::List downstream;
 * hardware paths take each frame's AncPayload and hand the packet
 * list to the card to inject on output frame N.
 *
 * @par Construction
 *
 * Constructed with an @ref AncTranslateConfig that is held by value
 * inside a contained @ref AncTranslator.  The translator is owned by
 * the @c AncFrameSync (not injected) so per-instance state — config
 * and the once-per-format-fallback dedupe set — has a clean lifetime.
 *
 * @par Thread safety
 *
 * Distinct @c AncFrameSync instances are independent; concurrent calls
 * to @ref apply on the same instance are not safe (the dedupe set and
 * the held config are not synchronised).  The canonical scope is one
 * instance per pipeline strand.
 */
class AncFrameSync {
        public:
                /** @brief Constructs with a default-constructed @ref AncTranslateConfig. */
                AncFrameSync() = default;

                /** @brief Constructs with the given config; held internally inside an owned @ref AncTranslator. */
                explicit AncFrameSync(AncTranslateConfig cfg) : _translator(std::move(cfg)) {}

                /** @brief Returns the held config. */
                const AncTranslateConfig &config() const { return _translator.config(); }

                /** @brief Replaces the held config. */
                void setConfig(AncTranslateConfig cfg) { _translator.setConfig(std::move(cfg)); }

                /**
                 * @brief Applies the disposition to one input frame.
                 *
                 * @param in           The input frame.  Passed by value so the
                 *                     caller can move; copies are CoW-cheap.
                 * @param disposition  The frame-sync disposition for this slot.
                 * @return @c Drop     → empty list (any packets the per-format
                 *                       Drop policy returns are stashed for
                 *                       the next surviving frame).
                 *         @c Play     → one-element list whose ANC payloads
                 *                       have been re-emitted through the sync
                 *                       policy with @c repeatIndex = 0; any
                 *                       previously-stashed packets are
                 *                       appended at the end of the first
                 *                       AncPayload.
                 *         @c Repeat[N]→ N-element list whose ANC payloads have
                 *                       been independently re-emitted with
                 *                       @c repeatIndex = @c 0..N-1.  Video /
                 *                       audio payloads are shared across the
                 *                       output frames via Frame copy-on-write.
                 *                       The stash drains onto output frame
                 *                       @c 0 only.
                 */
                Result<::promeki::List<Frame>> apply(Frame in, FrameSyncDisposition disposition);

                /**
                 * @brief Total number of packets currently held in the stash
                 *        across all formats.  Test / introspection helper.
                 */
                size_t stashedPacketCount() const;

        private:
                // Walks `frame`'s payload list and dispatches every ANC packet
                // through AncTranslator::applySyncPolicy with the given
                // disposition + repeatIndex.  Mutates the frame in place via
                // CoW: detaches the Frame::Data, then detaches each
                // AncPayload::Ptr via .modify(), then replaces the packet
                // list with the policy's output.  Non-ANC payloads are
                // untouched, so a downstream observer can verify that
                // VideoPayload / AudioPayload entries are still aliased
                // across repeats.
                Error applyToFrame(Frame &frame, FrameSyncDisposition disposition, uint8_t repeatIndex);

                // Logs a once-per-format warning the first time we see an ANC
                // packet whose format has no registered SyncPolicy.  The
                // dedupe set is per-instance, matching the spec.
                void warnFallbackOnce(AncFormat::ID id, const String &name);

                // Walks every ANC packet on the (read-only) input frame and
                // dispatches each through applySyncPolicy with disposition
                // = Drop.  Any packets the per-format policy returns
                // (SCTE-104 cues, etc.) are stuffed into the stash.
                Error applyDropAndStash(const Frame &frame);

                // Latest-wins insert into the stash, keyed on the input
                // packet's format.  When a Drop policy returns multiple
                // packets they all stash atomically under that format
                // (next Play emits all of them).  A second Drop hitting the
                // same format replaces the previously-stashed list and
                // warns once.
                void stashPackets(AncFormat::ID id, const AncPacket::List &pkts);

                // Appends every stashed packet to @p frame's first AncPayload
                // (creating an empty AncPayload on the frame if none exists).
                // Clears the stash on completion.  No-op when the stash is
                // empty.
                void drainStashInto(Frame &frame);

                AncTranslator                                       _translator;
                Set<AncFormat::ID>                                  _fallbackWarned;
                Map<AncFormat::ID, AncPacket::List>      _stash;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_PROAV
