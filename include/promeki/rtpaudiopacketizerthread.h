/**
 * @file      rtpaudiopacketizerthread.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <promeki/audiobuffer.h>
#include <promeki/audiodesc.h>
#include <promeki/buffer.h>
#include <promeki/list.h>
#include <promeki/namespace.h>
#include <promeki/queue.h>
#include <promeki/rtppacketizerthread.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Static dependencies handed to @ref RtpAudioPacketizerThread
 *        at construction time.
 * @ingroup network
 *
 * The packetizer needs the wire-format storage descriptor (which
 * doubles as the FIFO format), the AES67 packet shape, the
 * preroll gate, the index of the audio essence inside an
 * incoming @c Frame, and a sink-side queue (the matching
 * @ref RtpAudioTxThread::packetQueue) to push completed
 * @c packetBytes-sized chunks onto.  This struct bundles those
 * dependencies so unit tests can stand the packetizer up against
 * a synthetic harness without touching the surrounding
 * @c RtpMediaIO.
 *
 * @par Lifetime
 * Every pointer in this struct must outlive the packetizer
 * thread.  The packetizer does not take ownership of any of them.
 */
struct RtpAudioPacketizerContext {
                /// @brief Wire / storage @c AudioDesc.  Used to
                ///        build the per-thread @c AudioBuffer FIFO
                ///        and to seed format conversion on
                ///        @c push.  Must be valid.
                AudioDesc storageDesc;

                /// @brief Samples per AES67 packet, after MTU
                ///        clamping.  Must be > 0.
                size_t packetSamples = 0;

                /// @brief Bytes per AES67 packet (samples × channels
                ///        × bytes-per-sample).  Must be > 0.
                size_t packetBytes = 0;

                /// @brief Number of samples the packetizer waits
                ///        for in the FIFO before producing the
                ///        first chunk.  Resolved from
                ///        @c MediaConfig::AudioRtpPrerollMs at
                ///        configure time.  @c 0 disables preroll
                ///        (begin emitting on first source push).
                size_t prerollSamples = 0;

                /// @brief Index of this stream's audio essence
                ///        inside the incoming @c Frame's
                ///        @c audioPayloads() list.  Each
                ///        @c m=audio line in the SDP gets a
                ///        distinct index.
                size_t streamIdx = 0;

                /// @brief Sink the packetizer pushes
                ///        @c packetBytes-sized @c Buffer chunks
                ///        onto.  Typically the @c packetQueue() of
                ///        the matching
                ///        @ref RtpAudioTxThread.  Must be set.
                ///        Pushes use @c pushBlocking, so a bounded
                ///        queue back-pressures the packetizer
                ///        rather than dropping chunks.
                Queue<Buffer> *txPacketQueue = nullptr;
};

/**
 * @brief Per-stream RTP audio packetizer.
 * @ingroup network
 *
 * One thread per active audio writer stream.  Subclass of
 * @ref RtpPacketizerThread.  The strand pushes inbound
 * @ref RtpFrameWork items onto the base's bounded
 * @c payloadQueue; the worker's @c run loop pops one work item
 * at a time and hands it to @ref packetize.  @ref packetize
 * locates this stream's @c PcmAudioPayload inside the Frame
 * (via @c streamIdx), pushes its samples into a per-thread
 * @c AudioBuffer FIFO, and drains complete AES67-aligned chunks
 * onto the @ref RtpAudioPacketizerContext::txPacketQueue.
 *
 * @par FIFO format conversion
 * Format conversion happens implicitly inside
 * @c AudioBuffer::push — the FIFO is in
 * @c ctx.storageDesc.format() (typically @c PCMI_S16BE for L16)
 * and accepts any compatible input format.  Tests can drive the
 * conversion path by feeding mismatched-format payloads.
 *
 * @par Preroll
 * The first chunk is only produced once the FIFO has accumulated
 * @c prerollSamples worth of samples — the matching TX thread
 * emits silence in the interim so wire continuity is preserved.
 * Once preroll is satisfied for a given session it stays
 * satisfied for the rest of the session's lifetime; restart
 * requires re-construction.
 *
 * @par Backpressure
 * The push onto @c txPacketQueue is blocking
 * (@c pushBlocking).  If the wire cadence falls behind FIFO
 * drain the packetizer parks on the push; the strand's bounded
 * payload queue then back-pressures the upstream pipeline.
 * Cancellation (shutdown) breaks the drain loop —
 * @c Error::Cancelled returns from @c pushBlocking, the loop
 * breaks, and the next @ref packetize call sees the stop flag
 * and exits.
 *
 * @par Test entry point
 * @ref packetizeForTest invokes @ref packetize directly on the
 * test thread, bypassing the base's @c payloadQueue pop loop.
 * Unit tests use it to drive deterministic state transitions
 * against a synthetic Frame + harness queue.
 */
class RtpAudioPacketizerThread : public RtpPacketizerThread {
        public:
                /**
                 * @brief Constructs an unstarted audio packetizer.
                 *
                 * @param ctx  Static dependencies — see
                 *             @ref RtpAudioPacketizerContext.
                 * @param name Short OS thread name (≤ 15 chars on
                 *             Linux).  Defaults to
                 *             @c "RtpAudPkt".
                 * @param depth Max inbound payload queue depth;
                 *             defaults to
                 *             @c RtpPacketizerThread::DefaultPayloadQueueDepth.
                 */
                RtpAudioPacketizerThread(
                        RtpAudioPacketizerContext ctx,
                        const String              &name = String("RtpAudPkt"),
                        size_t                     depth = DefaultPayloadQueueDepth);

                ~RtpAudioPacketizerThread() override;

                RtpAudioPacketizerThread(const RtpAudioPacketizerThread &) = delete;
                RtpAudioPacketizerThread &operator=(const RtpAudioPacketizerThread &) = delete;

                /// @brief Direct entry point for unit tests — runs
                ///        @ref packetize on the test thread.
                ///        Tests must call @ref openForTest before
                ///        the first @ref packetizeForTest so the
                ///        FIFO is reserved (the base class would
                ///        normally do this in @c onStart).
                void packetizeForTest(const RtpFrameWork &work) { packetize(work); }

                /// @brief Direct entry point for unit tests — runs
                ///        the @c onStart logic synchronously
                ///        (FIFO reservation + drained-buffer sizing)
                ///        without spawning the worker.
                void openForTest() { onStart(); }

                /// @brief Read-only access to the FIFO.  Useful
                ///        for tests that want to inspect the
                ///        sample count between drains.
                const AudioBuffer &fifo() const { return _fifo; }

                /// @brief @c true once preroll has been satisfied.
                bool isPrerollDone() const { return _prerollDone; }

        protected:
                void onStart() override;
                void packetize(const RtpFrameWork &work) override;

        private:
                RtpAudioPacketizerContext _ctx;
                AudioBuffer               _fifo;
                List<uint8_t>             _drained;
                bool                      _prerollDone = false;
};

PROMEKI_NAMESPACE_END
