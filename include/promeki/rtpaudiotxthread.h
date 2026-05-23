/**
 * @file      rtpaudiotxthread.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_NETWORK
#include <cstddef>
#include <cstdint>
#include <promeki/atomic.h>
#include <promeki/audiodesc.h>
#include <promeki/buffer.h>
#include <promeki/cadence.h>
#include <promeki/namespace.h>
#include <promeki/pcmsilencefiller.h>
#include <promeki/queue.h>
#include <promeki/rtppayload.h>
#include <promeki/rtpsession.h>
#include <promeki/rtptxthread.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Static dependencies handed to @ref RtpAudioTxThread at
 *        construction time.
 * @ingroup network
 *
 * The TX thread does not need a full @c RtpMediaIO to function —
 * it needs the AES67 cadence shape (sample / byte / time per
 * packet), a wire-format @c AudioDesc to seed the silence filler,
 * the @c RtpSession + @c RtpPayload that own the wire-side
 * packing and emission, and a small set of TX-side
 * @c Atomic<int64_t> counters.  This struct bundles those
 * dependencies so the unit test can construct the TX thread
 * against a synthetic harness without touching the surrounding
 * @c RtpMediaIO.
 *
 * @par Lifetime
 * Every pointer in this struct must outlive the TX thread.  The
 * TX thread does not take ownership of any of them.
 *
 * @par Counter pointers
 * Each counter pointer may be @c nullptr in tests — when so the
 * thread skips the bump.  In the production path the surrounding
 * @c RtpMediaIO populates every counter, so the bump is always
 * performed.
 */
struct RtpAudioTxContext {
                /// @brief Wire-format @c AudioDesc used to build the
                ///        silence filler.  @c PcmSilenceFiller will
                ///        produce @c packetBytes of format-correct
                ///        silence (signed PCM = zeros, unsigned PCM
                ///        = midpoint, float = zeros).  Must be
                ///        valid.
                AudioDesc storageDesc;

                /// @brief Samples per AES67 packet.  Used to advance
                ///        the per-stream RTP-TS cursor on every
                ///        emission (real or silence).  Must be > 0.
                size_t packetSamples = 0;

                /// @brief Bytes per AES67 packet.  Used to validate
                ///        every payload chunk before it goes on the
                ///        wire and to size the silence filler.  Must
                ///        be > 0.
                size_t packetBytes = 0;

                /// @brief Resolved packet time in microseconds.
                ///        Drives the @ref Cadence interval.  Must be
                ///        > 0.
                int packetTimeUs = 0;

                /// @brief @c RtpSession the TX thread emits on.
                ///        Must outlive the TX thread.  Used to call
                ///        @c sendPackets (per emission) and
                ///        @c noteRtpEmission (which flips
                ///        @c hasEmissionRecord).
                RtpSession *session = nullptr;

                /// @brief @c RtpPayload (typically @c RtpPayloadL16)
                ///        used to pack each AES67 chunk into one
                ///        @c RtpPacket.  Must outlive the TX thread.
                RtpPayload *payload = nullptr;

                /// @brief Per-stream RTP clock rate.  Stamped onto
                ///        every emitted @c RtpPacketBatch so
                ///        downstream pacing stages can correlate
                ///        wire RTP-TS with elapsed wall time.
                uint32_t clockRate = 0;

                /// @brief Cumulative RTP packet counter the TX
                ///        thread bumps on every successful emission
                ///        (real or silence).  @c nullptr disables.
                Atomic<int64_t> *packetsSent = nullptr;

                /// @brief Cumulative RTP byte counter.  @c nullptr
                ///        disables.
                Atomic<int64_t> *bytesSent = nullptr;

                /// @brief Cumulative RTP payload byte counter (no
                ///        header).  Populates the SR sender info.
                ///        @c nullptr disables.
                Atomic<int64_t> *senderOctets = nullptr;

                /// @brief Cumulative count of silence packets
                ///        emitted — bumped when the input queue was
                ///        empty at a cadence deadline.  @c nullptr
                ///        disables.
                Atomic<int64_t> *silencePacketsEmitted = nullptr;

                /// @brief Cumulative samples emitted as silence
                ///        (counts per silence packet ×
                ///        @c packetSamples).  @c nullptr disables.
                Atomic<int64_t> *silenceSamplesEmitted = nullptr;

                /// @brief Initial RTP-TS for the very first emitted
                ///        packet (Phase D3).  Sourced from the
                ///        per-stream @ref RtpMediaClock so a
                ///        PTP-anchored audio writer rides the SMPTE
                ///        Epoch grid (`mediaclk:direct=0` interop)
                ///        instead of starting at zero.  The TX
                ///        thread copies this into its internal cursor
                ///        at construction time; subsequent advances
                ///        add @c packetSamples per emission.  Default
                ///        @c 0 preserves the pre-D3 behaviour.
                uint32_t initialRtpTs = 0;
};

/**
 * @brief Per-stream cadence-paced AES67 emitter with silence-fill.
 * @ingroup network
 *
 * One thread per active audio writer stream.  Owns the per-stream
 * RTP-TS cursor and the inbound @c Queue<Buffer> the upstream
 * @ref RtpAudioPacketizerThread pushes AES67-aligned chunks onto.
 * Every tick of the configured cadence the TX thread either pops
 * one chunk and emits it, or emits a silence packet built from
 * the @ref RtpAudioTxContext::storageDesc when the queue was
 * empty at the deadline.  The wire RTP-TS series is therefore
 * contiguous regardless of source stalls — receivers see no
 * discontinuity gap on the source side, and sync-aware
 * sessions (RTCP SR / lip-sync) keep flowing for an audio
 * stream that is currently producing only silence.
 *
 * @par Backpressure
 * The inbound queue is bounded at construction time
 * (default ~1 second of headroom — see @ref HeadroomUs).  When
 * the wire cadence falls behind the packetizer, the packetizer's
 * blocking push backpressures upstream rather than dropping
 * chunks.
 *
 * @par Shutdown
 * @ref RtpTxThread::requestStop sets the stop flag; the
 * @ref onShutdown override calls @c cancelWaiters on the inbound
 * queue so a blocking @c tryPop wakes promptly.  After the steady-
 * state pop loop exits a drain phase pulls remaining chunks via
 * @c tryPop and emits them unpaced; the rationale matches
 * @ref RtpPacketizerThread::run — chunks already produced upstream
 * must reach the wire even if the cadence has been short-
 * circuited.  Drained chunks advance the same RTP-TS cursor so
 * the receiver reconstructs wire timing from RTP-TS.
 *
 * @par Test entry point
 * @ref runOnceForTest performs one iteration of the cadence body
 * synchronously (no @c sleep_until) so unit tests can drive the
 * thread step-by-step without a real-wall-time wait.
 */
class RtpAudioTxThread : public RtpTxThread {
        public:
                /// @brief Default headroom in microseconds for the
                ///        inbound packet queue.  @c HeadroomUs /
                ///        @c packetTimeUs gives the queue depth in
                ///        chunks; one second of headroom matches the
                ///        FIFO reserve on
                ///        @ref RtpAudioPacketizerThread.
                static constexpr int64_t HeadroomUs = 1'000'000;

                /// @brief Floor for the inbound queue depth — a
                ///        sane fallback when @c packetTimeUs is
                ///        unset / non-positive.
                static constexpr size_t MinDepth = 8;

                /// @brief Long-stall threshold expressed as a
                ///        multiple of @c packetTimeUs.  When the
                ///        wakeup time has run further than this past
                ///        the cadence deadline (process suspended,
                ///        scheduler hiccup), the cadence is
                ///        re-anchored to @c now rather than burst-
                ///        emitting a flood of catch-up packets.
                static constexpr int64_t StallReanchorMultiplier = 16;

                /**
                 * @brief Constructs an unstarted TX thread.
                 *
                 * @param ctx  Static dependencies — see
                 *             @ref RtpAudioTxContext.
                 * @param name Short OS thread name (≤ 15 chars on
                 *             Linux).  Defaults to @c "RtpAudTx".
                 */
                explicit RtpAudioTxThread(RtpAudioTxContext ctx,
                                          const String       &name = String("RtpAudTx"));

                /// @brief Destructor.  Issues @ref requestStop and
                ///        joins the worker.
                ~RtpAudioTxThread() override;

                RtpAudioTxThread(const RtpAudioTxThread &) = delete;
                RtpAudioTxThread &operator=(const RtpAudioTxThread &) = delete;

                /// @brief Direct access to the inbound chunk queue.
                ///        The upstream packetizer pushes one
                ///        @c packetBytes-sized @ref Buffer per
                ///        AES67 packet via @c pushBlocking;
                ///        backpressure surfaces upstream when the
                ///        queue fills.
                Queue<Buffer> &packetQueue() { return _packetQueue; }

                /// @brief Const accessor for tests / introspection.
                const Queue<Buffer> &packetQueue() const { return _packetQueue; }

                /// @brief Read-only access to the per-stream RTP-TS
                ///        cursor.  Advances by exactly
                ///        @c ctx.packetSamples on every emission.
                ///        Intended for unit tests that need to
                ///        verify wire RTP-TS contiguity.
                uint32_t rtpTsCursor() const { return _rtpTs; }

                /**
                 * @brief Drives one iteration of the steady-state
                 *        cadence body without the @c sleep_until.
                 *
                 * Pops one chunk from @ref packetQueue if available;
                 * else builds a silence packet from
                 * @ref RtpAudioTxContext::storageDesc.  Stamps the
                 * RTP-TS at the current cursor, packs via
                 * @c ctx.payload, and dispatches via
                 * @c ctx.session->sendPackets.  Bumps the configured
                 * counters identically to the real run loop.
                 *
                 * @return @c true if a packet was emitted (real or
                 *         silence) — i.e. the iteration produced
                 *         output; @c false on a hard error
                 *         (sendPackets failed, packing produced
                 *         multiple packets, etc).  @c true with
                 *         silence is the common case when the queue
                 *         is empty.
                 *
                 * @note The cadence is *not* advanced — tests can
                 *       call this in a tight loop without time
                 *       passing.
                 */
                bool runOnceForTest();

        protected:
                void onShutdown() override { _packetQueue.cancelWaiters(); }
                void run() override;

        private:
                /// @brief Single-iteration emit body shared by
                ///        @ref run and @ref runOnceForTest.
                ///        @p forceSilence skips the queue pop (used
                ///        by the silence-only path tests exercise).
                bool emitOne(const Buffer &silenceBuf);

                /// @brief Builds the silence buffer once on first
                ///        access.
                Buffer silenceBuffer();

                RtpAudioTxContext _ctx;
                Queue<Buffer>     _packetQueue;
                uint32_t          _rtpTs = 0;
                Buffer            _silenceBuf;
                bool              _silenceBuilt = false;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NETWORK
