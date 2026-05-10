/**
 * @file      rtpseqreorderbuffer.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstdint>
#include <map>
#include <promeki/duration.h>
#include <promeki/error.h>
#include <promeki/mutex.h>
#include <promeki/namespace.h>
#include <promeki/queue.h>
#include <promeki/rtppacket.h>
#include <promeki/timestamp.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Windowed RTP packet reorder buffer keyed by 32-bit
 *        extended sequence number.
 * @ingroup network
 *
 * Sits between the per-source @ref RtpSeqTracker and the per-stream
 * depacketizer thread.  The reorder buffer's job is to convert
 * arrival-order packet delivery into extended-sequence-order
 * delivery while bounding both latency (via a configurable
 * @c playoutDelay deadline) and memory (via a configurable maximum
 * window).
 *
 * @par Emission rules
 * @ref insert may synchronously emit zero or more packets to the
 * supplied output @c Queue<RtpPacket>:
 * - **In-order delivery**: as soon as the next-expected extended
 *   seq fills, emit it (and any contiguous tail) immediately.
 * - **Deadline-driven gap-fill**: when the oldest buffered
 *   packet's @c (arrivalSteady + playoutDelay) deadline expires,
 *   that packet is emitted regardless of the gap before it; the
 *   gap is treated as a permanent loss.  Counted as
 *   @c emittedOnDeadline (a *successful* emission, not a drop).
 * - **Drop-oldest on overflow**: if @ref insert would push the
 *   window past @c maxWindow, the oldest buffered entry is
 *   dropped (counted as @c droppedOnOverflow) before the new
 *   entry is admitted.
 * - **Silent dup discard**: if @ref insert receives an extended
 *   seq that is already buffered or has already been emitted,
 *   the packet is silently dropped (counted as
 *   @c droppedAsDuplicate).
 *
 * @par Output back-pressure
 * Output is via @c Queue::pushDropOldest, so a slow depacketizer
 * causes drops at the post-reorder stage rather than back-pressure
 * into the reorder buffer (which would in turn back-pressure into
 * the recv socket thread, where blocking would let the kernel UDP
 * ring overflow on the next datagram).  Drops at the queue stage
 * are the post-reorder drop counter on the stream, distinct from
 * the @c emittedOnDeadline statistic.
 *
 * @par Thread safety
 * Single-writer / single-flusher.  The recv socket thread owns the
 * @ref insert / @ref flush calls; @ref snapshot may be called from
 * any thread.  All public calls take the buffer's internal mutex.
 *
 * @par Default configuration
 * @c maxWindow = 64 packets (≈ tens of ms of wire jitter at
 * typical AES67 / RFC 4175 cadences); @c playoutDelay = 0ms
 * (immediate gap-fill emission, preserving the legacy "deliver
 * as soon as it arrives" latency on a clean LAN).  Bumping
 * @c playoutDelay trades latency for reorder tolerance on the
 * open internet.
 */
class RtpSeqReorderBuffer {
        public:
                /// @brief Tunable configuration.
                struct Config {
                                /// @brief Maximum number of packets
                                ///        held in the buffer at once.
                                ///        Drop-oldest fires once
                                ///        an insert would exceed
                                ///        this.
                                size_t maxWindow = 64;

                                /// @brief Wall-clock deadline beyond
                                ///        the oldest buffered
                                ///        packet's @c arrivalSteady
                                ///        before that packet's gap
                                ///        is treated as permanent
                                ///        loss and the packet is
                                ///        emitted anyway.  Zero
                                ///        means "emit immediately
                                ///        on insert if no gap" —
                                ///        the legacy LAN behaviour.
                                Duration playoutDelay = Duration::fromMilliseconds(0);
                };

                /// @brief Cumulative stats publishable through
                ///        @c ReaderStream::Stats.
                struct Stats {
                                uint64_t inserted = 0;
                                uint64_t emittedInOrder = 0;
                                uint64_t emittedOnDeadline = 0;
                                uint64_t droppedOnOverflow = 0;
                                uint64_t droppedAsDuplicate = 0;
                };

                RtpSeqReorderBuffer() = default;
                explicit RtpSeqReorderBuffer(const Config &c);

                /**
                 * @brief Insert a packet into the buffer.
                 *
                 * May synchronously emit zero or more packets to
                 * @p out.  See class-level docs for the emission
                 * rules.  Idempotent on duplicate seq numbers.
                 *
                 * @param pkt           The packet to insert.
                 * @param extendedSeq   Wraparound-safe seq from
                 *                      @ref RtpSeqTracker.
                 * @param arrivalSteady Per-packet arrival anchor.
                 * @param out           Per-stream output queue
                 *                      that the depacketizer
                 *                      consumes.  Emissions use
                 *                      @c Queue::pushDropOldest so
                 *                      slow consumers shed at this
                 *                      stage rather than backing
                 *                      pressure upstream.
                 */
                void insert(RtpPacket pkt, uint32_t extendedSeq,
                            const TimeStamp &arrivalSteady, Queue<RtpPacket> &out);

                /**
                 * @brief Drain everything still buffered.
                 *
                 * Used by the shutdown path so any in-flight
                 * packets are released to the depacketizer for
                 * orderly drain.  Emits in extended-seq order.
                 * Each emission counts as @c emittedOnDeadline
                 * since the gap (if any) is closed by force at
                 * flush time.
                 */
                void flush(Queue<RtpPacket> &out);

                /**
                 * @brief Drop everything currently buffered without
                 *        emitting.  Used on SSRC reset where
                 *        downstream state is also being torn down.
                 */
                void clear();

                /// @brief Returns a snapshot of the cumulative
                ///        emission / drop counters.
                Stats snapshot() const;

                /// @brief Returns the configured tuning.
                Config config() const;

                /// @brief Returns the current depth of the buffer.
                size_t size() const;

        private:
                struct Entry {
                                RtpPacket pkt;
                                TimeStamp arrival;
                };

                // Drains any in-order tail starting at the next-
                // expected seq.  Caller must hold @c _mutex.
                void drainInOrderLocked(Queue<RtpPacket> &out);

                // Emits the head entry as a deadline-fill case.
                // Caller must hold @c _mutex.
                void emitHeadLocked(Queue<RtpPacket> &out, bool deadline);

                mutable Mutex                _mutex;
                Config                       _config;
                std::map<uint32_t, Entry>    _buf;
                bool                         _haveExpected = false;
                uint32_t                     _expectedSeq = 0;
                Stats                        _stats;
};

PROMEKI_NAMESPACE_END
