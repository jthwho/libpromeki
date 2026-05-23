/**
 * @file      rtpancpacketizerthread.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_NETWORK
#include <cstddef>
#include <cstdint>
#include <promeki/ancpacket.h>
#include <promeki/duration.h>
#include <promeki/namespace.h>
#include <promeki/queue.h>
#include <promeki/rtpmediaclock.h>
#include <promeki/rtppacketbatch.h>
#include <promeki/rtppacketizerthread.h>
#include <promeki/rtppayloadanc.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

class RtpPayloadAnc;

/**
 * @brief Static dependencies handed to @ref RtpAncPacketizerThread at
 *        construction time.
 * @ingroup network
 *
 * The ANC packetizer locates this stream's @ref AncPayload inside the
 * inbound @ref RtpFrameWork, calls
 * @ref RtpPayloadAnc::packAncFrame to produce a list of RFC 8331 RTP
 * packets, wraps the result in an @ref RtpPacketBatch (carrying the
 * strand-side frame index + clock rate), and pushes the batch onto
 * the sink-side TX queue.  The TX thread re-stamps the RTP timestamp
 * at emit time via @c FrameRate::cumulativeTicks — matching the
 * video pattern — so both directions of timing math live in one
 * place.
 *
 * @par Lifetime
 * Every pointer in this struct must outlive the packetizer thread.
 * The packetizer does not take ownership of any of them.
 */
struct RtpAncPacketizerContext {
                /// @brief Index of this stream's ANC essence inside
                ///        the incoming @c Frame::ancPayloads list.
                ///        Each @c m=video smpte291/90000 section in
                ///        the SDP (RFC 8331 §3.1 / ST 2110-40 §7) gets
                ///        a distinct index.
                size_t streamIdx = 0;

                /// @brief Per-stream RTP clock rate in Hz.  RFC 8331
                ///        mandates 90000.  Plumbed onto every produced
                ///        @ref RtpPacketBatch for the TX thread's
                ///        @c FrameRate::cumulativeTicks math.
                uint32_t clockRateHz = 90000;

                /// @brief The @ref RtpPayloadAnc that owns
                ///        @c packAncFrame.  Must be set.
                RtpPayloadAnc *payload = nullptr;

                /// @brief Sink-side TX queue.  Bounded; the
                ///        @c pushBlocking call back-pressures the
                ///        packetizer (and through it the strand)
                ///        when the wire falls behind.  Must be set.
                Queue<RtpPacketBatch> *txPacketQueue = nullptr;

                /// @brief F-bit value for ST 2110-40 §5.5 keep-alive RTP
                ///        packets (ANC_Count=0, Length=0).  The
                ///        packetizer calls
                ///        @c RtpPayloadAnc::setKeepAliveField with this
                ///        at construction; sessions that pair an ANC
                ///        stream with an interlaced video stream stamp
                ///        @c RtpPayloadAnc::FieldIndication::InterlacedField1 /
                ///        @c InterlacedField2 here as appropriate.
                ///        Default is @c Progressive (RFC 8331 §2.1 0b00).
                ///        Appended at the tail of the struct so existing
                ///        positional brace initialisers in tests / call
                ///        sites stay valid.
                RtpPayloadAnc::FieldIndication keepAliveField =
                        RtpPayloadAnc::FieldIndication::Progressive;

                /// @brief @c true when ST 2110-40 §6.4 LLTM TX-time
                ///        deadlines should be stamped on each batch.
                ///
                ///        When set, the packetizer computes
                ///        @c T_FST + T_EPO + T_D and stamps the
                ///        resulting @c CLOCK_TAI nanosecond deadline
                ///        onto @ref RtpPacketBatch::deadlineTaiNs so
                ///        the kernel's ETF qdisc releases the whole
                ///        frame's worth of ANC by that instant.
                ///        Requires @ref mediaClock to be valid and
                ///        PTP-anchored; @em silently degrades to
                ///        Compatible-mode pacing (no per-batch
                ///        deadline) when the anchor is missing.
                bool lltmEnabled = false;

                /// @brief Per-stream @ref RtpMediaClock providing
                ///        @c T_FST = @c tvdUtcNs(frameIndex) — the
                ///        UTC wallclock of frame @c N's first sample
                ///        on the SMPTE Epoch grid.
                ///
                ///        Borrowed pointer; must outlive the
                ///        packetizer thread.  @c nullptr disables LLTM
                ///        deadline emission regardless of
                ///        @ref lltmEnabled.
                const RtpMediaClock *mediaClock = nullptr;

                /// @brief ST 2110-40 §6.4 @c T_EPO (Epoch Offset).
                ///
                ///        Same semantics as ST 2110-10 §7.4 TR_OFFSET
                ///        but namespaced to ANC's per-stream
                ///        adjustment.  Added to the @c T_FST instant
                ///        before the §6.4 @c T_D slack window.
                ///        Default @c Duration::zero — no offset.
                Duration trOffset = Duration::zero();

                /// @brief ST 2110-40 §6.4 @c T_D = 8 / (FrameRate ×
                ///        TotalLines).
                ///
                ///        Length of the LLTM egress slack window
                ///        relative to @c T_FST + @c T_EPO.  At 30 fps
                ///        × 1125 lines this is ≈ 237 µs; at 60 fps ×
                ///        2250 lines (UHD) it is ≈ 59 µs.  Default
                ///        @c Duration::zero — disables the LLTM
                ///        margin (deadline collapses to
                ///        @c T_FST + @c T_EPO).
                Duration tD = Duration::zero();
};

/**
 * @brief Per-stream RTP ANC packetizer thread.
 * @ingroup network
 *
 * One instance per active ANC writer stream.  Subclass of
 * @ref RtpPacketizerThread.  The strand pushes
 * @ref RtpFrameWork items onto the base's bounded payload queue;
 * the worker pops one at a time and hands it to @ref packetize.
 * @ref packetize locates this stream's @ref AncPayload inside the
 * frame (via @c streamIdx), runs
 * @c RtpPayloadAnc::packAncFrame to build a list of RFC 8331 RTP
 * packets, and pushes a single @ref RtpPacketBatch onto
 * @ref RtpAncPacketizerContext::txPacketQueue.
 *
 * @par Non-St291 packets are skipped
 * @ref RtpPayloadAnc::packAncFrame silently drops packets whose
 * transport is not @c St291 (the only transport ST 2110-40 carries).
 * Non-St291 entries should have been translated upstream by the
 * @c AncTranslator before reaching this thread; a stray one here
 * is therefore a programming error but does not crash the wire.
 *
 * @par Backpressure
 * The push onto @c txPacketQueue is blocking
 * (@c pushBlocking).  If the wire falls behind, the packetizer
 * parks on the push; the strand's bounded payload queue then
 * back-pressures the upstream pipeline.  Cancellation breaks the
 * push and the worker exits cleanly.
 *
 * @par Test entry point
 * @ref packetizeForTest invokes @ref packetize directly on the
 * test thread, bypassing the base's payload-queue pop loop.
 */
class RtpAncPacketizerThread : public RtpPacketizerThread {
        public:
                /**
                 * @brief Constructs an unstarted ANC packetizer
                 *        thread.
                 *
                 * @param ctx   Static dependencies — see
                 *              @ref RtpAncPacketizerContext.
                 * @param name  Short OS thread name; defaults to
                 *              @c "RtpAncPkt".
                 * @param depth Max inbound payload-queue depth.
                 *              Defaults to
                 *              @c RtpPacketizerThread::DefaultPayloadQueueDepth.
                 */
                RtpAncPacketizerThread(
                        RtpAncPacketizerContext ctx,
                        const String           &name = String("RtpAncPkt"),
                        size_t                  depth = DefaultPayloadQueueDepth);

                ~RtpAncPacketizerThread() override;

                RtpAncPacketizerThread(const RtpAncPacketizerThread &) = delete;
                RtpAncPacketizerThread &operator=(const RtpAncPacketizerThread &) = delete;

                /// @brief Direct test entry — runs @ref packetize on
                ///        the caller's thread.
                void packetizeForTest(const RtpFrameWork &work) { packetize(work); }

                /// @brief Read-only access to the held context.
                const RtpAncPacketizerContext &context() const { return _ctx; }

        protected:
                void packetize(const RtpFrameWork &work) override;

        private:
                RtpAncPacketizerContext _ctx;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NETWORK
