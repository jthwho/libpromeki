/**
 * @file      rtpaudiodepacketizerthread.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstdint>
#include <functional>
#include <promeki/function.h>
#include <promeki/atomic.h>
#include <promeki/audiodesc.h>
#include <promeki/clockdomain.h>
#include <promeki/namespace.h>
#include <promeki/queue.h>
#include <promeki/rtpdepacketizerthread.h>
#include <promeki/rtppacket.h>
#include <promeki/rtpstreamclock.h>
#include <promeki/rxpayloadbundle.h>
#include <promeki/string.h>
#include <promeki/timestamp.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Static dependencies handed to @ref RtpAudioDepacketizerThread
 *        at construction time.
 * @ingroup network
 *
 * The depacketizer needs the per-stream output queue, a handful of
 * stat counters, and a small set of read-through pointers into the
 * owning reader stream's mutable state (the wire-format desc the
 * audio depacketizer also helps populate, the stream clock and
 * @c hasSr flag for wallclock-derived captureTime, the SSRC reset
 * epoch).  It also needs two callbacks back into the owning
 * @c RtpMediaIO: one to refresh the stream clock from a freshly
 * arrived SR, one to convert NTP timestamps to local steady time.
 *
 * @par Lifetime
 * Every pointer in this struct must outlive the depacketizer
 * thread.
 *
 * @par Test usage
 * Unit tests stand the depacketizer up against synthetic queues +
 * counters by populating this struct directly.  The
 * @ref refreshStreamClock / @ref ntpToSteady callbacks may be left
 * empty when the test only exercises the anchor-derived
 * captureTime path.
 */
struct RtpAudioDepacketizerContext {
                /// @brief Output queue the depacketizer pushes
                ///        @ref RxAudioChunk bundles onto.  Must be
                ///        set; pushes use @c pushBlocking, so a
                ///        bounded queue back-pressures the
                ///        depacketizer rather than dropping bundles.
                Queue<RxAudioChunk> *payloadQueue = nullptr;

                /// @brief Per-stream RFC 3550 SSRC reset epoch.
                ///        The depacketizer compares against its
                ///        last-observed value at the top of every
                ///        @ref RtpAudioDepacketizerThread::handlePacket
                ///        and on mismatch resets its anchor.
                ///        May be @c nullptr in tests that don't
                ///        exercise SSRC reset.
                const Atomic<uint32_t> *resetEpoch = nullptr;

                /// @brief @c true once the surrounding
                ///        @c RtpMediaIO has activated this stream.
                ///        Read at the top of every packet — when
                ///        @c false the depacketizer drops the
                ///        packet silently (the open path hasn't
                ///        finished wiring up the stream yet).
                ///        May be @c nullptr in tests that drive
                ///        the depacketizer directly; @c nullptr is
                ///        treated as "always active".
                const bool *active = nullptr;

                /// @brief Wire-format description of the L16 / L24
                ///        PCM stream.  The depacketizer reads
                ///        @c channels and @c sampleRate from this
                ///        to compute the per-packet sample count.
                ///        Must be set and valid.
                const AudioDesc *readerAudioDesc = nullptr;

                /// @brief @c true once the first SR for this
                ///        stream has seeded @ref streamClock.
                ///        Read with @ref streamClock to decide
                ///        whether wallclock alignment is in effect.
                ///        @c nullptr in tests that don't exercise
                ///        the wallclock path.
                const bool *hasSr = nullptr;

                /// @brief Per-stream RTP <-> NTP clock mapping.
                ///        Used when @ref hasSr indicates a valid
                ///        SR has been folded in.  @c nullptr in
                ///        anchor-only tests.
                const RtpStreamClock *streamClock = nullptr;

                /// @brief Stat counters — non-owning pointers.
                ///        Each may be @c nullptr in tests; the
                ///        depacketizer skips the bump when so.
                Atomic<int64_t> *packetsReceived = nullptr;
                Atomic<int64_t> *bytesReceived = nullptr;
                Atomic<int64_t> *lastPacketArrivalNs = nullptr;
                Atomic<int64_t> *framesReassembled = nullptr;

                /// @brief Bumps the @c FrameCount on the owning
                ///        ReaderStream.  @c FrameCount is not
                ///        atomic, so this is invoked through a
                ///        callback the owner provides.  May be
                ///        @c nullptr in tests.
                Function<void()> noteFrameReceived;

                /// @brief Refreshes @ref streamClock from any
                ///        newly arrived SR on the per-stream
                ///        RtpSession.  Called once per packet at
                ///        the top of @c handlePacket.  May be
                ///        empty in tests.
                Function<void()> refreshStreamClock;

                /// @brief Converts an NTP timestamp to local
                ///        steady time using the owning
                ///        @c RtpMediaIO 's anchor.  Called when
                ///        @ref hasSr is true.  Returns a default
                ///        TimeStamp when the anchor is not yet
                ///        valid (the depacketizer falls back to
                ///        the anchor-derived captureTime in that
                ///        case).  May be empty in tests.
                Function<TimeStamp(const NtpTime &)> ntpToSteady;
};

/**
 * @brief Per-stream depacketizer for L16 / L24 PCM audio (RFC 3551 /
 *        AES67).
 * @ingroup network
 *
 * One instance per active audio reader stream.  Pops
 * post-reorder @ref RtpPacket bundles off
 * @ref RtpDepacketizerThread::inputQueue, slices each packet's PCM
 * payload into a per-packet @ref RxAudioChunk, stamps a per-chunk
 * @c captureTime (wallclock-derived when @c hasSr is set,
 * anchor-derived otherwise), and pushes the chunk onto the per-
 * stream @c payloadQueue the @ref RtpAggregatorThread drains.
 *
 * @par Threading
 * Runs on its own worker thread (inherits @ref Thread via
 * @ref RtpDepacketizerThread).  All per-packet state lives on
 * the worker; the only cross-thread mutations are the pushes onto
 * the output queue and the bumps of the supplied @c Atomic
 * counters.
 *
 * @par Shutdown
 * The base @ref RtpDepacketizerThread::requestStop wakes the
 * input queue's @c pop and the destructor joins the worker.
 */
class RtpAudioDepacketizerThread : public RtpDepacketizerThread {
        public:
                /**
                 * @brief Constructs an unstarted audio depacketizer.
                 *
                 * @param ctx  Static dependencies — see
                 *             @ref RtpAudioDepacketizerContext.
                 * @param name Short OS thread name (≤ 15 chars on
                 *             Linux), e.g. @c "RtpAudDepkt".
                 * @param clockRateHz Per-stream RTP clock rate
                 *             (used by the base @ref StreamAnchor).
                 */
                RtpAudioDepacketizerThread(RtpAudioDepacketizerContext ctx,
                                           const String &name,
                                           uint32_t clockRateHz);

                ~RtpAudioDepacketizerThread() override;

                RtpAudioDepacketizerThread(const RtpAudioDepacketizerThread &) = delete;
                RtpAudioDepacketizerThread &operator=(const RtpAudioDepacketizerThread &) = delete;

                /// @brief Direct entry point for unit tests — runs
                ///        @ref handlePacket once on the test thread.
                void handlePacketForTest(const RtpPacket &pkt) { handlePacket(pkt); }

        protected:
                void handlePacket(const RtpPacket &pkt) override;

        private:
                RtpAudioDepacketizerContext _ctx;
                uint32_t                    _lastEpoch = 0;
};

PROMEKI_NAMESPACE_END
