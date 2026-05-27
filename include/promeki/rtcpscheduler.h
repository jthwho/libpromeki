/**
 * @file      rtcpscheduler.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_NETWORK
#include <cstdint>
#include <functional>
#include <promeki/function.h>
#include <promeki/atomic.h>
#include <promeki/list.h>
#include <promeki/mutex.h>
#include <promeki/namespace.h>
#include <promeki/rtcppacket.h>
#include <promeki/rtpseqtracker.h>
#include <promeki/rtpsession.h>
#include <promeki/string.h>
#include <promeki/thread.h>
#include <promeki/waitcondition.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief View of one writer stream the scheduler emits SR / BYE for.
 * @ingroup network
 *
 * Pierced into @ref RtpMediaIO::WriterStream at scheduler-spawn
 * time so the scheduler can stay decoupled from the surrounding
 * RtpMediaIO type.  All pointer fields are non-owning — the
 * underlying objects must outlive the scheduler thread.
 */
struct RtcpSchedulerWriterStream {
                /// @brief @c true once the surrounding RtpMediaIO
                ///        has activated this stream (i.e. opened
                ///        its session).  Set at spawn time and
                ///        never modified after.
                bool active = false;

                /// @brief Short label used in log messages.  Free-
                ///        form — typically @c "video" / @c "audio"
                ///        / @c "data".
                String mediaType;

                /// @brief RtpSession the scheduler emits RTCP on.
                ///        @c nullptr or @ref active @c false skips
                ///        this entry on every emit cycle.
                RtpSession *session = nullptr;

                /// @brief Cumulative RTP packet count maintained by
                ///        this stream's TX thread; published into
                ///        the SR sender info.
                const Atomic<int64_t> *packetsSent = nullptr;

                /// @brief Cumulative RTP payload byte count
                ///        (excludes header) maintained by this
                ///        stream's TX thread; published into the
                ///        SR sender info.
                const Atomic<int64_t> *senderOctets = nullptr;
};

/**
 * @brief View of one reader stream the scheduler emits RR / BYE for
 *        and runs the wire-silence watchdog over.
 * @ingroup network
 *
 * Pierced into @ref RtpMediaIO::ReaderStream at scheduler-spawn
 * time.  All pointer fields are non-owning — the underlying
 * objects must outlive the scheduler thread.
 *
 * @c wireSilenceEosSignaled is the only mutable field exposed
 * through the view: the scheduler latches it to @c true exactly
 * once when the watchdog first trips so subsequent ticks do not
 * re-fire @ref RtcpSchedulerContext::onWireSilenceEos for the
 * same stream.
 */
struct RtcpSchedulerReaderStream {
                /// @brief @c true once the surrounding RtpMediaIO
                ///        has activated this stream.  Set at
                ///        spawn time and never modified after.
                bool active = false;

                /// @brief Short label used in log messages.  Free-
                ///        form — typically @c "video" / @c "audio"
                ///        / @c "data".
                String mediaType;

                /// @brief RtpSession the scheduler emits RTCP on.
                ///        @c nullptr or @ref active @c false skips
                ///        this entry on every emit cycle.
                RtpSession *session = nullptr;

                /// @brief Per-source RFC 3550 §A seq / loss / jitter
                ///        tracker.  @c nullptr until the surrounding
                ///        RtpMediaIO wires it up in
                ///        @c openReaderStream; the scheduler skips
                ///        RR emission for this stream until then.
                const RtpSeqTracker *seqTracker = nullptr;

                /// @brief Nanoseconds-since-epoch of the most
                ///        recent packet seen on this stream's
                ///        depacketizer.  Updated by the depacketizer
                ///        thread on every observed packet; the
                ///        scheduler's wire-silence watchdog reads
                ///        it on every tick.  Zero until the first
                ///        packet arrives — the watchdog skips
                ///        streams that have never observed a
                ///        packet.
                const Atomic<int64_t> *lastPacketArrivalNs = nullptr;

                /// @brief Wire-silence latch.  Set to @c true by
                ///        the scheduler exactly once when the
                ///        watchdog first trips for this stream.
                ///        Caller (@ref RtpMediaIO) initialises it
                ///        to @c false; the scheduler is the sole
                ///        writer thereafter.
                bool *wireSilenceEosSignaled = nullptr;
};

/**
 * @brief Static dependencies handed to @ref RtcpScheduler at
 *        construction time.
 * @ingroup network
 *
 * The scheduler does not need a full @c RtpMediaIO to function —
 * it needs the per-stream views, the cadence, and a callback
 * for the wire-silence EoS path.  This struct bundles those
 * dependencies so the unit test can construct the scheduler
 * against a synthetic harness without touching the surrounding
 * RtpMediaIO.
 *
 * @par Lifetime
 * Every pointer reachable through @ref writers / @ref readers
 * (sessions, atomic counters, seq trackers, latches) must
 * outlive the scheduler thread.  The scheduler does not take
 * ownership of any of them.
 *
 * @par Population
 * @ref RtpMediaIO populates this at the end of
 * @c executeCmd(MediaIOCommandOpen) — when the per-stream
 * counters / seq trackers / sessions exist — and hands it to
 * the scheduler's constructor.
 */
struct RtcpSchedulerContext {
                /// @brief Steady-state RTCP cadence in
                ///        milliseconds.  Values @c <= 0 are
                ///        interpreted as the RFC 3550 default of
                ///        5000.
                int intervalMs = 5000;

                /// @brief Wire-silence EoS threshold in
                ///        milliseconds for reader streams.
                ///        @c <= 0 means derive as
                ///        @c 10 × intervalMs (the RFC 3550 §6.3.5
                ///        recommended boundary scaled by the
                ///        observation that 10 missed RTCP
                ///        intervals indicates a dead transport).
                int64_t wireSilenceTimeoutMs = 0;

                /// @brief Per-stream writer views.  One entry per
                ///        active writer stream
                ///        (@c VideoStream / @c AudioStream /
                ///        @c DataStream in the surrounding
                ///        RtpMediaIO).
                List<RtcpSchedulerWriterStream> writers;

                /// @brief Per-stream reader views.  One entry per
                ///        active reader stream
                ///        (@c VideoReaderStream /
                ///        @c AudioReaderStream /
                ///        @c DataReaderStream).
                List<RtcpSchedulerReaderStream> readers;

                /// @brief Wire-silence EoS callback.  Invoked from
                ///        the scheduler thread on the first tick
                ///        whose computed gap exceeds the threshold
                ///        for a given reader stream.  The scheduler
                ///        sets the stream's
                ///        @c wireSilenceEosSignaled latch to
                ///        @c true before invoking the callback,
                ///        so the callback can safely tear down
                ///        downstream state.  Idempotent: never
                ///        invoked twice for the same stream.
                ///        @c nullptr disables the callback (the
                ///        latch is still flipped, the call is just
                ///        skipped).  The callback is passed the
                ///        stream view and the gap in nanoseconds.
                Function<void(RtcpSchedulerReaderStream &, int64_t gapNs)>
                        onWireSilenceEos;
};

/**
 * @brief Per-RtpMediaIO RTCP scheduler.
 * @ingroup network
 *
 * One thread per RtpMediaIO instance.  Wakes every
 * @ref RtcpSchedulerContext::intervalMs and asks each active
 * writer stream's RtpSession to emit an SR + SDES compound (RTP
 * and RTCP share one port via rtcp-mux), and each active reader
 * stream's RtpSession to emit an RR.  Also runs the wire-silence
 * watchdog on every reader stream — when the gap between
 * @c TimeStamp::now and the depacketizer's
 * @c lastPacketArrivalNs exceeds the configured threshold, the
 * scheduler latches the stream's
 * @c wireSilenceEosSignaled flag and invokes
 * @ref RtcpSchedulerContext::onWireSilenceEos.
 *
 * @par Two-phase startup
 * The first second after @ref start uses an early-emit poll —
 * every @ref kStartupPollMs the scheduler emits SR / RR for any
 * stream that has produced data, until @ref allStreamsHaveEmitted
 * returns @c true (or the startup deadline expires).  Without
 * the early-emit phase the very first SR for each stream would
 * wait until @c intervalMs into the session, and sync-aware
 * receivers (ffplay, GStreamer) would play whatever audio
 * arrived in the gap unsynced.  Once startup completes the
 * scheduler settles to the configured cadence.
 *
 * @par Best-effort emission
 * Send failures on a stream's RtpSession are logged once per
 * stream and the scheduler keeps running.  An RTCP send that
 * fails does not affect RTP transport — it just delays sync
 * convergence at the receiver until the next successful emit.
 *
 * @par Threading
 * One worker thread.  The scheduler is the sole writer of every
 * @c wireSilenceEosSignaled latch.  Read paths
 * (@c packetsSent / @c senderOctets / @c lastPacketArrivalNs
 * atomics, @c RtpSeqTracker::snapshot, @c RtpSession's RTCP
 * methods) are all thread-safe by construction.
 *
 * @par Shutdown
 * @ref requestStop sets the stop flag and wakes the cv-sleep so
 * the scheduler exits within at most one @ref kStartupPollMs.
 * @ref emitByeForAll is intended to be called once before
 * @ref requestStop on a clean shutdown so a BYE goes out for
 * every active SSRC.
 *
 * @par Test-friendly entry point
 * @ref runOnce executes a single emit cycle synchronously
 * without spawning the worker.  Unit tests use it to drive
 * deterministic state transitions against a synthetic harness.
 */
class RtcpScheduler : public Thread {
        public:
                /// @brief Early-emit poll cadence in milliseconds.
                ///        Tight enough that the first SR / RR for
                ///        every stream goes out within ~50 ms of
                ///        the first observed RTP traffic.
                static constexpr unsigned int kStartupPollMs = 50;

                /// @brief Default RTCP cadence in milliseconds.
                ///        Matches RFC 3550 §6.2's recommended
                ///        minimum.  Used when
                ///        @ref RtcpSchedulerContext::intervalMs is
                ///        @c <= 0.
                static constexpr int kDefaultIntervalMs = 5000;

                /**
                 * @brief Constructs an unstarted scheduler.
                 *
                 * @param ctx  Static dependencies — see
                 *             @ref RtcpSchedulerContext.
                 * @param name Short OS thread name (≤ 15 chars on
                 *             Linux).  Defaults to @c "rtp-rtcp".
                 */
                explicit RtcpScheduler(RtcpSchedulerContext ctx,
                                       const String        &name = String("rtp-rtcp"));

                /// @brief Destructor.  Issues @ref requestStop and
                ///        joins the worker.
                ~RtcpScheduler() override;

                RtcpScheduler(const RtcpScheduler &) = delete;
                RtcpScheduler &operator=(const RtcpScheduler &) = delete;

                /**
                 * @brief Marks the worker for shutdown and wakes
                 *        the cv-sleep.
                 *
                 * Idempotent.  The worker exits within at most one
                 * @ref kStartupPollMs of this call.  Does not block.
                 */
                void requestStop();

                /// @brief @c true once @ref requestStop has been
                ///        called.
                bool isStopRequested() const { return _stopRequested.value(); }

                /**
                 * @brief Emits a BYE for every active stream
                 *        (writer or reader).
                 *
                 * Intended to be called once before @ref requestStop
                 * on a clean shutdown so receivers get notified
                 * promptly.  Best-effort — send failures are
                 * logged and ignored.  Safe to call from any
                 * thread; the scheduler thread itself does not
                 * race on this path.
                 */
                void emitByeForAll();

                /**
                 * @brief Drives one emit cycle synchronously without
                 *        spawning the worker.
                 *
                 * Intended for unit tests that prefer deterministic
                 * execution.  Does the same work as one steady-
                 * state iteration of @ref run: emit SR for every
                 * writer with @c hasEmissionRecord, emit RR for
                 * every reader with @c receivedPackets > 0, run
                 * the wire-silence watchdog on every reader.
                 */
                void runOnce();

        protected:
                /// @brief Thread entry point — runs the two-phase
                ///        scheduler loop.
                void run() override;

        private:
                void emitOnce();
                void checkWireSilence(RtcpSchedulerReaderStream &s);
                bool allStreamsHaveEmitted() const;
                void cvSleep(unsigned int ms);

                static void emitForStream(RtcpSchedulerWriterStream &s);
                static void emitRrForStream(RtcpSchedulerReaderStream &s);
                static void emitByeForWriter(RtcpSchedulerWriterStream &s);
                static void emitByeForReader(RtcpSchedulerReaderStream &s);

                RtcpSchedulerContext _ctx;
                int                  _intervalMs;
                Atomic<bool>         _stopRequested;
                Mutex                _mutex;
                WaitCondition        _cv;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NETWORK
