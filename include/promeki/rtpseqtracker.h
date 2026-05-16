/**
 * @file      rtpseqtracker.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_NETWORK
#include <cstdint>
#include <promeki/mutex.h>
#include <promeki/namespace.h>
#include <promeki/timestamp.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief RFC 3550 per-source RTP sequence tracker (§A.1, §A.3, §A.8).
 * @ingroup network
 *
 * One @c RtpSeqTracker tracks all the per-source state RFC 3550
 * mandates for a single SSRC: probation handshake, 16-bit cycles
 * counter for extended-sequence-number arithmetic, duplicate /
 * reorder detection, expected-vs-received accounting, RR-aligned
 * 8-bit fraction-lost, and the §A.8 interarrival-jitter EWMA in RTP
 * timestamp units.  The post-Phase-2 RX socket thread updates one
 * instance per active reader stream by calling @ref observe on
 * every arriving RTP packet.
 *
 * @par Probation policy
 * The tracker requires @c MIN_SEQUENTIAL = 2 sequential packets
 * before it considers a new SSRC "valid" (matching RFC 3550 §A.1).
 * During the probation window @ref observe still reports the packet
 * with @c duplicate=false so the recv thread can deliver
 * optimistically into the downstream reorder buffer — matching the
 * way the legacy callback path forwards every packet without
 * validation.  Probation purely flags the stats; it never gates
 * downstream delivery.
 *
 * @par Implicit init
 * The first @ref observe call on a fresh tracker (or after
 * @ref reset) initialises the source automatically — callers do
 * not need to invoke @ref initSource explicitly.  When that happens
 * @ref ObserveResult::ssrcInit is set so the recv thread can pin
 * the SSRC, etc.
 *
 * @par Thread safety
 * The recv thread is the only writer to a given tracker instance.
 * @ref snapshot is intended to be called from a different thread
 * (the RTCP scheduler that builds Receiver Reports), so a small
 * mutex guards every public call.  No per-packet allocation
 * happens on the recv thread.
 *
 * @par Reset on SSRC change
 * @ref reset zeroes every counter (received, expected,
 * cumulativeLost, duplicatePackets, reorderedPackets,
 * interarrivalJitter, extendedHighestSeq, cycles) and rearms
 * probation.  Stat consumers measuring deltas across an SSRC change
 * must therefore use @c ReaderStream::ssrcChanges as the boundary
 * marker — it accumulates across resets.
 *
 * @par Jitter units
 * Interarrival jitter is reported in RTP timestamp units (so the
 * value is directly the RR @c jitter field per RFC 3550 §6.4.1).
 * Conversion to wall-clock requires dividing by the per-stream
 * @c clockRateHz.
 */
class RtpSeqTracker {
        public:
                /// @brief RFC 3550 §A.1 — probation length.
                static constexpr unsigned int MinSequential = 2;

                /// @brief RFC 3550 §A.1 — sequential reorder
                ///        threshold (packets within this distance
                ///        of @c max_seq are treated as in-order).
                static constexpr uint32_t MaxDropout = 3000;

                /// @brief RFC 3550 §A.1 — backwards-jump threshold.
                ///        Packets that fall outside
                ///        @c [-MaxMisorder, +MaxDropout) of
                ///        @c max_seq are candidate restarts.
                static constexpr uint32_t MaxMisorder = 100;

                /// @brief 16-bit RTP sequence space modulus.
                static constexpr uint32_t SeqMod = 1u << 16;

                /**
                 * @brief Per-packet observation result.
                 *
                 * Populated by @ref observe and returned to the
                 * caller (the RX socket thread) so it can decide
                 * whether to insert the packet into the reorder
                 * buffer (always, today: duplicates are the only
                 * case where the recv thread short-circuits, and
                 * even those still go through the reorder buffer's
                 * own duplicate filter for stats consistency).
                 */
                struct ObserveResult {
                                /// @brief @c true if the source is
                                ///        still in the §A.1
                                ///        probation window.  Pure
                                ///        flag; callers should not
                                ///        gate delivery on it.
                                bool probation = false;

                                /// @brief @c true if this seq has
                                ///        already been seen on this
                                ///        SSRC (RFC 3550 §A.1 dup
                                ///        detection — pre-window
                                ///        seq with no restart).
                                bool duplicate = false;

                                /// @brief @c true if this is the
                                ///        first packet for a fresh
                                ///        tracker.  The recv thread
                                ///        pins SSRC on this edge.
                                bool ssrcInit = false;

                                /// @brief 32-bit extended sequence:
                                ///        @c (cycles << 16) | seq.
                                ///        Wraparound-safe; the
                                ///        reorder buffer keys on
                                ///        this.
                                uint32_t extendedSeq = 0;

                                /// @brief Current §A.8 jitter
                                ///        estimate in RTP-TS units
                                ///        — same value the next RR
                                ///        will publish.
                                uint32_t jitterRtpTsUnits = 0;
                };

                /**
                 * @brief Cumulative stats snapshot for this source.
                 *
                 * Built by @ref snapshot and consumed by the RTCP
                 * scheduler when it builds an RR.  All fields
                 * accumulate from the last @ref reset (i.e. across
                 * the lifetime of the current SSRC); deltas across
                 * SSRC change must use @c ReaderStream::ssrcChanges
                 * as the boundary marker.
                 */
                struct Stats {
                                /// @brief @c (cycles << 16) | maxSeq.  The
                                ///        RR @c extended-highest-seq
                                ///        field per RFC 3550 §6.4.1.
                                uint32_t extendedHighestSeq = 0;

                                /// @brief Cumulative count of seq-
                                ///        number wraparounds, scaled
                                ///        by @c SeqMod (matches the
                                ///        RFC's accumulator shape
                                ///        where adding it to
                                ///        @c maxSeq yields the
                                ///        extended seq).
                                uint32_t cycles = 0;

                                /// @brief Signed cumulative loss per
                                ///        RFC 3550 §6.4.1 — 24-bit
                                ///        signed range, here held
                                ///        as @c int32_t with values
                                ///        clamped to that range
                                ///        before transmission.
                                int32_t cumulativeLost = 0;

                                /// @brief 8-bit fraction-lost since
                                ///        the last @ref commitRrInterval
                                ///        — the RR @c fraction-lost
                                ///        field.  Reads zero before
                                ///        the first commit.
                                uint8_t fractionLost = 0;

                                /// @brief Total expected packets
                                ///        (extendedHighestSeq −
                                ///        baseSeq + 1).
                                uint32_t expectedPackets = 0;

                                /// @brief Total received packets
                                ///        (counts dups + reorders;
                                ///        both arrive on the wire).
                                uint32_t receivedPackets = 0;

                                /// @brief Count of seq numbers seen
                                ///        more than once on this
                                ///        SSRC (the §A.1 dup case).
                                uint32_t duplicatePackets = 0;

                                /// @brief Count of packets whose
                                ///        seq fell behind
                                ///        @c maxSeq within the
                                ///        §A.1 reorder window.
                                uint32_t reorderedPackets = 0;

                                /// @brief §A.8 EWMA jitter, RTP-TS
                                ///        units.
                                uint32_t interarrivalJitter = 0;
                };

                RtpSeqTracker() = default;

                /**
                 * @brief Manually pins the source-init state.
                 *
                 * Optional — @ref observe initialises a fresh
                 * tracker on the first call automatically.  Provided
                 * so callers that already know the SSRC's clock
                 * rate (e.g. via SDP) can wire it before any packet
                 * arrives, which lets @ref observe compute jitter
                 * correctly on the very first packet.
                 *
                 * @param seq         The seq number to seed.
                 * @param clockRateHz The stream's RTP clock rate in Hz.
                 */
                void initSource(uint16_t seq, uint32_t clockRateHz);

                /**
                 * @brief Updates the tracker from one received packet.
                 *
                 * Implements the RFC 3550 §A.1 @c update_seq
                 * algorithm verbatim, including the @c bad_seq
                 * restart heuristic (§A.1 covers the case of a
                 * sender that drops off and returns with a fresh
                 * seq window — the tracker resets on the second
                 * sequential packet from the new window rather than
                 * on the first stray).  Jitter is updated via the
                 * §A.8 EWMA on every accepted (non-duplicate)
                 * packet.
                 *
                 * @param seq            16-bit RTP sequence number.
                 * @param rtpTs          32-bit RTP timestamp.
                 * @param arrivalSteady  Per-packet arrival anchor
                 *                       stamped at @c recvfrom
                 *                       return on the recv socket
                 *                       thread.
                 * @return @ref ObserveResult capturing
                 *         duplicate / probation status, the
                 *         extended seq, and the current jitter
                 *         estimate.
                 */
                ObserveResult observe(uint16_t seq, uint32_t rtpTs, const TimeStamp &arrivalSteady);

                /**
                 * @brief Returns a snapshot of the current stats.
                 *
                 * Non-mutating.  The RTCP scheduler calls this on
                 * its tick to populate the RR fields; it should
                 * call @ref commitRrInterval immediately after to
                 * lock in the @c fractionLost interval boundary
                 * for the next snapshot.
                 */
                Stats snapshot() const;

                /**
                 * @brief Closes the current RR interval.
                 *
                 * Computes the 8-bit @c fractionLost over the
                 * delta from the last commit (RFC 3550 §A.3) and
                 * advances the priors.  Subsequent @ref snapshot
                 * calls report the freshly-computed value until
                 * the next commit.
                 */
                void commitRrInterval();

                /**
                 * @brief Hard reset — used on SSRC change.
                 *
                 * Zeroes every counter and rearms probation.  The
                 * next @ref observe call re-pins the source and
                 * sets @ref ObserveResult::ssrcInit to @c true.
                 * Note that this clears even the EWMA jitter state
                 * — the new source's clock relationship is
                 * unrelated to the old one's.
                 */
                void reset();

                /**
                 * @brief Returns the per-stream RTP clock rate the
                 *        tracker is using for §A.8 jitter.  Zero
                 *        until the first call to
                 *        @ref setClockRateHz or @ref initSource.
                 */
                uint32_t clockRateHz() const;

                /**
                 * @brief Sets the per-stream RTP clock rate without
                 *        otherwise mutating tracker state.
                 *
                 * Provided so callers that have the rate from SDP
                 * but don't yet know the first seq can plumb the
                 * jitter machinery before any packet arrives.  Safe
                 * to call repeatedly; setting the same value is a
                 * no-op.
                 */
                void setClockRateHz(uint32_t rate);

        private:
                void initSourceLocked(uint16_t seq);
                void updateJitterLocked(uint32_t rtpTs, const TimeStamp &arrivalSteady);

                mutable Mutex _mutex;
                bool          _initialised = false;
                unsigned int  _probation = MinSequential;
                uint16_t      _baseSeq = 0;
                uint16_t      _maxSeq = 0;
                uint32_t      _badSeq = SeqMod + 1;
                uint32_t      _cycles = 0;
                uint32_t      _received = 0;
                uint32_t      _expectedPrior = 0;
                uint32_t      _receivedPrior = 0;
                uint32_t      _duplicates = 0;
                uint32_t      _reordered = 0;
                uint8_t       _fractionLost = 0;
                uint32_t      _clockRateHz = 0;
                uint32_t      _jitter = 0;
                bool          _haveJitterPrev = false;
                uint32_t      _prevTransit = 0;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NETWORK
