/**
 * @file      rtppacketizerthread.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_NETWORK
#include <promeki/atomic.h>
#include <promeki/error.h>
#include <promeki/frame.h>
#include <promeki/framenumber.h>
#include <promeki/namespace.h>
#include <promeki/queue.h>
#include <promeki/string.h>
#include <promeki/thread.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief One unit of work the strand pushes onto an
 *        @ref RtpPacketizerThread's payload queue.
 * @ingroup network
 *
 * Carries the full @ref Frame (CoW handle — copying is a refcount
 * bump) plus the strand-side frame index.  The packetizer derives
 * the wire RTP timestamp from @ref frameIndex and the per-stream
 * clock rate, so the work-item ferries the index across the
 * thread boundary without forcing a metadata write on the Frame.
 */
struct RtpFrameWork {
                Frame       frame;
                FrameNumber frameIndex;
};

/**
 * @brief Base class for per-stream RTP packetizer threads.
 * @ingroup network
 *
 * One @c RtpPacketizerThread instance lives between an
 * @c RtpMediaIO strand and a per-stream TX thread.  It owns the
 * @ref payloadQueue (an inbound, bounded @c Queue<Frame> the
 * strand pushes to) and a pop loop that hands each Frame to the
 * subclass's @ref packetize.  Subclasses are responsible for
 * locating their own essence inside the @ref Frame
 * (@c frame.videoPayloads(), @c frame.audioPayloads(),
 * @c frame.metadata()), packing it into payload bytes, and
 * forwarding the result to their TX thread via whatever
 * sink-side queue is appropriate (an @c RtpPacketBatch queue for
 * video / data; a @c Buffer queue for audio).  RTP header bytes
 * are deliberately *not* set during packetization — the TX thread
 * stamps version / sequence number / SSRC / payload type / marker
 * / RTP timestamp at emission time, which is what keeps audio's
 * silence-fill rule consistent with the per-stream RTP-TS
 * counter (one owner of RTP-TS, one owner of payload bytes).
 *
 * @par Backpressure and shutdown
 * The @ref payloadQueue is bounded (typically depth 2) so the
 * strand's @c pushBlocking call applies backpressure to the
 * upstream pipeline whenever the packetizer falls behind.
 * Shutdown is sentinel-free: the owning @c RtpMediaIO calls
 * @ref requestStop, which sets the stop flag and invokes
 * @c Queue::cancelWaiters on @ref payloadQueue.  The pop loop
 * observes @c Error::Cancelled, breaks out, and the thread
 * exits.  Subclasses can call @ref propagateCancel from their
 * destructor to forward the cancel onto whichever sink-side
 * queue they own, so the TX thread on the far side sees the
 * shutdown without having to hear about it from the owning
 * RtpMediaIO directly.
 *
 * @par Thread safety
 * Each @c RtpPacketizerThread instance is meant to be driven by
 * exactly one producer (the strand) on @ref payloadQueue and one
 * consumer (this thread itself) inside @ref run.  External
 * callers may call @ref requestStop concurrently from any
 * thread.
 */
class RtpPacketizerThread : public Thread {
        public:
                /// @brief Default soft-cap on the strand→packetizer
                ///        queue depth.  Two slots is enough to let
                ///        the strand push one frame ahead while the
                ///        packetizer drains the previous one;
                ///        anything larger masks pipeline pacing
                ///        problems.
                static constexpr size_t DefaultPayloadQueueDepth = 2;

                /**
                 * @brief Constructs an unstarted packetizer thread
                 *        with @p name as the OS-level thread name
                 *        (visible in @c top / @c ps / perf
                 *        traces) and @p depth as the bounded
                 *        @ref payloadQueue cap.
                 *
                 * @param name  Short OS thread name (≤ 15 chars on
                 *              Linux).
                 * @param depth Maximum inbound queue depth; @c 0
                 *              means unbounded (use sparingly —
                 *              backpressure breaks).  Defaults to
                 *              @ref DefaultPayloadQueueDepth.
                 */
                RtpPacketizerThread(const String &name,
                                    size_t        depth = DefaultPayloadQueueDepth);

                /// @brief Destructor.  Issues @ref requestStop and
                ///        joins the worker so subclasses don't
                ///        race the vtable slice.
                ~RtpPacketizerThread() override;

                RtpPacketizerThread(const RtpPacketizerThread &) = delete;
                RtpPacketizerThread &operator=(const RtpPacketizerThread &) = delete;

                /**
                 * @brief Pushes one work item (Frame + frame index)
                 *        onto @ref payloadQueue, blocking up to
                 *        @p timeoutMs if the queue is full.
                 *
                 * Returns @c Error::Timeout if the queue stayed
                 * full for the full timeout, @c Error::Cancelled
                 * if the queue was cancelled mid-wait, or
                 * @c Error::Ok on success.  The strand calls this
                 * with @c timeoutMs == 0 (wait indefinitely) by
                 * default — backpressure is the whole point of
                 * the bound.
                 */
                Error pushWork(const RtpFrameWork &work, unsigned int timeoutMs = 0);

                /**
                 * @brief Marks the worker for shutdown and wakes
                 *        any pending @ref pushFrame /
                 *        @c payloadQueue.pop calls.
                 *
                 * Idempotent — calling it twice is a no-op.  After
                 * @ref requestStop the worker will exit on the
                 * next iteration of its pop loop and any further
                 * @ref pushFrame returns @c Error::Cancelled.
                 */
                void requestStop();

                /// @brief Returns @c true once @ref requestStop has
                ///        been called.
                bool isStopRequested() const { return _stopRequested.value(); }

                /// @brief Direct access to the inbound queue.  Used
                ///        by tests and by the strand-router to call
                ///        @c pushBlocking / @c cancelWaiters
                ///        directly.
                Queue<RtpFrameWork> &payloadQueue() { return _payloadQueue; }

                /// @brief See above (const overload).
                const Queue<RtpFrameWork> &payloadQueue() const { return _payloadQueue; }

        protected:
                /**
                 * @brief Subclass hook: process one inbound work
                 *        item.
                 *
                 * Called once per item the strand pushes through
                 * @ref pushWork.  Implementations locate their
                 * essence inside @c work.frame, packetize it, and
                 * push the result to whichever sink-side queue
                 * they own.  The strand-side @c work.frameIndex
                 * is the value the TX thread will combine with
                 * the stream's clock rate to derive the wire
                 * RTP-TS.  Errors are logged at the call site and
                 * do not stop the packetizer thread — a single
                 * bad frame must not take the wire down.
                 */
                virtual void packetize(const RtpFrameWork &work) = 0;

                /// @brief Subclass hook called once before the
                ///        first @ref packetize.  Default is no-op;
                ///        override to set up per-thread state that
                ///        depends on the thread's identity (RNG
                ///        seeding, scratch buffers, etc.).
                virtual void onStart() {}

                /// @brief Subclass hook called once after the
                ///        last @ref packetize, just before the
                ///        thread exits.  Default is no-op; override
                ///        to flush sink-side queues if appropriate.
                virtual void onStop() {}

                void run() override;

        private:
                Atomic<bool>        _stopRequested;
                Queue<RtpFrameWork> _payloadQueue;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NETWORK
