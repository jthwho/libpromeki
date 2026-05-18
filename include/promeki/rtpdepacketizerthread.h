/**
 * @file      rtpdepacketizerthread.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_NETWORK
#include <cstdint>
#include <promeki/atomic.h>
#include <promeki/duration.h>
#include <promeki/error.h>
#include <promeki/namespace.h>
#include <promeki/queue.h>
#include <promeki/rtppacket.h>
#include <promeki/string.h>
#include <promeki/thread.h>
#include <promeki/timestamp.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Stream-anchor state for pre-SR captureTime interpolation.
 * @ingroup network
 *
 * Each per-stream depacketizer thread maintains a @ref StreamAnchor
 * that pins the relationship between the local steady clock and the
 * stream's RTP timestamp space.  The anchor is set once on the
 * first arriving packet (using its @c arrivalSteady stamp and RTP
 * timestamp), and used to derive a per-frame @c captureTime via
 *
 * @code
 *   captureTime = arrivalT0 + (rtpTs - rtpTs0) / clockRate
 * @endcode
 *
 * with modular @c uint32_t subtraction for wraparound safety
 * (matching @ref RtpStreamClock).  Once an SR has arrived the
 * depacketizer switches from anchor-derived to wallclock-derived
 * captureTime; the transition is documented as an intentional
 * one-time step.
 *
 * Reset on SSRC change.  Thread-affinity is the depacketizer
 * thread's; no synchronization is needed because only one thread
 * touches it.
 */
struct StreamAnchor {
                /// @brief @c recvfrom-return TimeStamp of the first
                ///        packet observed for this anchor epoch.
                ///        Set to a default-constructed (epoch)
                ///        TimeStamp when the anchor is invalid.
                TimeStamp arrivalT0;

                /// @brief RTP timestamp on the first packet.
                uint32_t rtpTs0 = 0;

                /// @brief Per-stream RTP clock rate in Hz.  Plumbed
                ///        in from @c StreamReceiver::clockRateHz.
                uint32_t clockRate = 0;

                /// @brief @c true once the anchor has been seeded.
                bool valid = false;

                /// @brief Resets the anchor to its uninitialised
                ///        state (called on SSRC reset).
                void reset() {
                        *this = StreamAnchor{};
                }

                /// @brief Returns @c true if the anchor is ready to
                ///        interpolate.
                bool isValid() const { return valid && clockRate > 0; }

                /**
                 * @brief Interpolates a per-packet @c captureTime
                 *        from the anchor.
                 *
                 * @param rtpTs The packet's 32-bit RTP timestamp.
                 * @return Interpolated steady-clock TimeStamp, or an
                 *         invalid @ref TimeStamp if the anchor is
                 *         not yet valid — callers detect that via
                 *         @c isValid.
                 */
                TimeStamp captureTimeFor(uint32_t rtpTs) const {
                        if (!isValid()) return TimeStamp();
                        const uint32_t delta = rtpTs - rtpTs0;
                        const int64_t  ns = (static_cast<int64_t>(delta) * 1'000'000'000) /
                                            static_cast<int64_t>(clockRate);
                        return arrivalT0 + Duration::fromNanoseconds(ns);
                }
};

/**
 * @brief Base class for per-stream RTP depacketizer threads.
 * @ingroup network
 *
 * One @c RtpDepacketizerThread instance lives between an
 * @c RtpSession's per-stream post-reorder @c RtpPacket::Queue and
 * a per-stream @c PayloadQueue (RxVideoFrame / RxAudioChunk /
 * RxDataMessage).  It owns the pop loop that drains the post-
 * reorder queue, calls into the subclass's @ref handlePacket on
 * every packet, and propagates cancellation through both queues
 * on shutdown.
 *
 * Subclasses live nested inside @c rtpmediaio.cpp so they can
 * reach into per-stream RtpMediaIO state without exposing it
 * through extra public surface.  See @c VideoDepacketizerThread,
 * @c AudioDepacketizerThread, @c DataDepacketizerThread inside
 * that file.
 *
 * @par Backpressure and shutdown
 * The post-reorder queue is filled by the recv socket thread via
 * @c Queue::pushDropOldest, so it never back-pressures the recv
 * thread.  This thread's pop loop uses a short timeout so the
 * cancellation path is responsive.  Shutdown is sentinel-free —
 * the owning RtpMediaIO calls @ref requestStop, which sets the
 * stop flag and invokes @c Queue::cancelWaiters on @ref inputQueue.
 *
 * @par Thread safety
 * One producer (the recv thread) and one consumer (this thread)
 * on @ref inputQueue.  External callers may call @ref requestStop
 * from any thread.
 */
class RtpDepacketizerThread : public Thread {
        public:
                /// @brief Default depth of the post-reorder
                ///        @c RtpPacket::Queue bound.  Sized to
                ///        match @c RtpSeqReorderBuffer::Config's
                ///        @c maxWindow default.  Drop-oldest fires
                ///        at this depth when the depacketizer
                ///        falls behind, surfacing as
                ///        @c reorderOutputDropped on the stream's
                ///        stats.
                static constexpr size_t DefaultInputQueueDepth = 64;

                /**
                 * @brief Constructs an unstarted depacketizer
                 *        thread.
                 *
                 * @param name        Short OS thread name
                 *                    (≤ 15 chars on Linux), e.g.
                 *                    @c "RtpVidDepkt/0".
                 * @param clockRateHz Per-stream RTP clock rate
                 *                    plumbed into the
                 *                    @ref StreamAnchor on first
                 *                    packet.
                 * @param depth       Maximum post-reorder queue
                 *                    depth.  @c 0 means unbounded.
                 */
                RtpDepacketizerThread(const String &name, uint32_t clockRateHz,
                                      size_t depth = DefaultInputQueueDepth);

                /// @brief Destructor.  Issues @ref requestStop and
                ///        joins the worker.
                ~RtpDepacketizerThread() override;

                RtpDepacketizerThread(const RtpDepacketizerThread &) = delete;
                RtpDepacketizerThread &operator=(const RtpDepacketizerThread &) = delete;

                /// @brief Direct access to the inbound queue.  The
                ///        recv thread pushes via
                ///        @c pushDropOldest; this thread pops in
                ///        @ref run.
                RtpPacket::Queue &inputQueue() { return _inputQueue; }

                /// @brief See above (const overload).
                const RtpPacket::Queue &inputQueue() const { return _inputQueue; }

                /**
                 * @brief Marks the worker for shutdown and wakes
                 *        any pending @c inputQueue.pop calls.
                 *
                 * Idempotent — calling it twice is a no-op.  After
                 * @ref requestStop the worker will exit on the
                 * next iteration of its pop loop.
                 */
                void requestStop();

                /// @brief Returns @c true once @ref requestStop
                ///        has been called.
                bool isStopRequested() const { return _stopRequested.value(); }

                /// @brief Returns the per-stream RTP clock rate
                ///        the thread was constructed with.
                uint32_t clockRateHz() const { return _clockRateHz; }

                /// @brief Returns a const reference to the current
                ///        anchor state.  Used by tests to verify
                ///        the anchor was seeded on the first
                ///        packet.
                const StreamAnchor &anchor() const { return _anchor; }

        protected:
                /**
                 * @brief Subclass hook: process one inbound packet.
                 *
                 * Called from @ref run on every packet popped off
                 * the post-reorder queue.  Implementations are
                 * responsible for reassembly state (across
                 * @c handlePacket calls), running
                 * @c payload->unpack at frame boundaries,
                 * computing per-Frame @c captureTime via the
                 * anchor (see @ref captureTimeForRtpTs), and
                 * pushing onto whichever sink-side
                 * @c PayloadQueue they own.
                 */
                virtual void handlePacket(const RtpPacket &pkt) = 0;

                /// @brief Subclass hook called once before the
                ///        first @ref handlePacket.  Default is
                ///        no-op.
                virtual void onStart() {}

                /// @brief Subclass hook called once after the last
                ///        @ref handlePacket, just before the
                ///        thread exits.  Default is no-op; override
                ///        to flush sink-side queues if appropriate.
                virtual void onStop() {}

                /**
                 * @brief Seeds @ref StreamAnchor from the first
                 *        packet observed.
                 *
                 * Subclass calls this on every packet; the call
                 * is idempotent once the anchor is valid (no-op
                 * on subsequent invocations until @ref resetAnchor
                 * is called).  Returns the (possibly newly-seeded)
                 * anchor.
                 */
                void ensureAnchor(uint32_t rtpTs, const TimeStamp &arrivalSteady);

                /// @brief Resets the anchor (called on SSRC reset
                ///        epoch bump from the recv thread).
                void resetAnchor();

                /**
                 * @brief Convenience: interpolated captureTime for
                 *        the given RTP timestamp.  Returns a
                 *        default TimeStamp when the anchor is
                 *        not valid.
                 */
                TimeStamp captureTimeForRtpTs(uint32_t rtpTs) const;

                /**
                 * @brief The post-reorder pop loop's timeout in
                 *        milliseconds.  Short enough that the
                 *        cancellation path remains responsive.
                 */
                static constexpr unsigned int PopTimeoutMs = 50;

                void run() override;

        private:
                Atomic<bool>      _stopRequested;
                RtpPacket::Queue  _inputQueue;
                uint32_t          _clockRateHz;
                StreamAnchor      _anchor;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NETWORK
