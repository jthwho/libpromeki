/**
 * @file      rtptxthread.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_NETWORK
#include <promeki/atomic.h>
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/thread.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Base class for per-stream RTP TX threads.
 * @ingroup network
 *
 * The TX thread is the single owner of the per-stream RTP
 * timestamp counter and the place where the full RTP header
 * (version / sequence number / SSRC / payload type / marker /
 * RTP-TS) is stamped on each outbound packet.  It dequeues
 * payload-bytes-only work items from a sink-side queue
 * (populated by the upstream packetizer thread), stamps the
 * header at emission time, applies the configured pacing
 * strategy (@c KernelFq, @c Userspace, future @c TxTime,
 * @c None), and dispatches the packets via @c RtpSession.
 *
 * @par Why a separate base from @ref RtpPacketizerThread
 * The packetizer base owns a @c Queue<Frame> uniformly for all
 * three streams, but the TX-side queue type differs by stream
 * (audio uses @c Queue<Buffer> for one-AES67-chunk-per-packet,
 * video and data use @c Queue<RtpPacketBatch>).  Templating one
 * shared base over the input type would just push divergent
 * cadence / silence-fill / pacing logic into specialisations
 * that share little code, so a thin Thread-derived base with
 * a stop flag and a stable thread name is what's actually
 * shared.  Concrete subclasses own their own queue and run
 * loop.
 *
 * @par Thread safety
 * @ref requestStop is safe to call from any thread.  Subclasses
 * are responsible for cancelling their own input queue (via
 * @c Queue::cancelWaiters) inside @ref onShutdown so the run
 * loop wakes immediately.
 */
class RtpTxThread : public Thread {
        public:
                /**
                 * @brief Constructs an unstarted TX thread with
                 *        @p name as the OS-level thread name.
                 *
                 * @param name Short OS thread name (≤ 15 chars on
                 *             Linux).  Used by perf / @c top /
                 *             @c ps to identify the per-stream
                 *             worker.
                 */
                explicit RtpTxThread(const String &name);

                /// @brief Destructor.  Issues @ref requestStop and
                ///        joins the worker so subclasses don't
                ///        race the vtable slice.
                ~RtpTxThread() override;

                RtpTxThread(const RtpTxThread &) = delete;
                RtpTxThread &operator=(const RtpTxThread &) = delete;

                /**
                 * @brief Marks the worker for shutdown.
                 *
                 * Sets the stop flag and invokes @ref onShutdown
                 * so subclasses can cancel their input queue and
                 * any other long-blocking primitive they own
                 * (typically a @c Queue::cancelWaiters call on
                 * the sink-side queue).  After this returns the
                 * worker will exit on the next iteration of its
                 * run loop.  Idempotent.
                 */
                void requestStop();

                /// @brief Returns @c true once @ref requestStop
                ///        has been called.
                bool isStopRequested() const { return _stopRequested.value(); }

        protected:
                /// @brief Subclass hook: shutdown signalling that
                ///        runs synchronously inside
                ///        @ref requestStop.  Default is no-op;
                ///        override to call @c cancelWaiters on
                ///        the input queue.  Must be safe to call
                ///        more than once (since @ref requestStop
                ///        is idempotent).
                virtual void onShutdown() {}

        private:
                Atomic<bool> _stopRequested;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NETWORK
