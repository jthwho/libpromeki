/**
 * @file      rtppacketbatch.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_NETWORK
#include <cstdint>
#include <promeki/framenumber.h>
#include <promeki/namespace.h>
#include <promeki/rtppacket.h>
#include <promeki/timestamp.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief One frame's worth of RTP packets en route from a packetizer
 *        thread to its TX thread.
 * @ingroup network
 *
 * The packetizer thread fills @ref packets with payload bytes only —
 * no RTP header field is set during packetization.  The TX thread is
 * the single owner of the per-packet RTP header (version, sequence
 * number, SSRC, payload type, marker bit, RTP timestamp): it reads
 * @ref frameIndex and @ref clockRate, computes
 * @c FrameRate::cumulativeTicks(clockRate, frameIndex) once for the
 * batch, stamps that timestamp on every packet, sets @c marker on
 * the last packet iff @ref markerOnLast, and dispatches the packets
 * to the wire via @ref RtpSession.  Deferring the timestamp to the
 * TX thread is what keeps audio's silence-fill rule consistent with
 * the per-stream RTP-TS counter — there is exactly one RTP-TS owner
 * per stream.
 *
 * @par Why audio uses @c Queue<Buffer> instead
 * Audio streams hand AES67-aligned PCM payload chunks (one chunk per
 * AES67 packet) to their TX thread, which assembles each chunk into
 * an @ref RtpPacket at the cadence-driven emission instant.  There
 * is no equivalent of @ref frameIndex / @ref clockRate to ferry
 * across that handoff — the audio TX thread keeps its own monotonic
 * sample-counter and stamps RTP-TS = cursor at emission time.  Video
 * and data, by contrast, are frame-shaped (one RTP-TS per access
 * unit / metadata blob), and @c RtpPacketBatch is the natural
 * carrier for that handoff.
 *
 * @par Per-batch TXTIME deadline
 * @ref deadlineTaiNs carries an optional ST 2110-40 §6.4 LLTM-style
 * per-batch TX deadline through to the wire.  When non-zero,
 * @ref RtpSession::sendPackets stamps every datagram in this batch
 * with @c txTimeNs = deadlineTaiNs before the scheduler runs; the
 * @ref TxTimePacketScheduler then passes the pre-stamped values
 * through to the kernel's @c SCM_TXTIME path instead of computing
 * its own deadlines.  The @em per-packet @c SCM_TXTIME field still
 * lives on @ref PacketTransport::Datagram::txTimeNs (zero on entry
 * unless this batch-level deadline is in play); keeping the batch
 * flat means @c RtpPacketBatch stays a plain value type that can be
 * copied cheaply and stored in @c Queue<RtpPacketBatch> without
 * inflating the queue node size.
 *
 * @par Thread safety
 * @c RtpPacketBatch itself carries no synchronization.  Producer-
 * to-consumer ordering across @ref Queue handoff provides the
 * happens-before relationship; the underlying @c RtpPacket buffers
 * are written disjointly (packetizer writes payload bytes, TX
 * thread later writes header bytes) so no per-byte atomic ops are
 * needed even though both threads share the buffer.
 */
struct RtpPacketBatch {
                /// @brief One frame's RTP packets, payload bytes
                ///        filled, RTP header zeroed.  Allocated via
                ///        @ref RtpPacket::createList so all packets
                ///        share one underlying @ref Buffer.
                RtpPacket::List packets;

                /// @brief Zero-based frame index for this batch.
                ///        TX thread feeds it into
                ///        @c FrameRate::cumulativeTicks(clockRate,
                ///        frameIndex) to derive a wraparound-safe
                ///        per-frame RTP timestamp.  Default-
                ///        constructed @c Unknown so an unset batch
                ///        is detectable; the packetizer is
                ///        expected to overwrite this before
                ///        pushing the batch onto the queue.
                FrameNumber frameIndex;

                /// @brief RTP timestamp clock rate in Hz (the
                ///        stream's @c rtpmap clock).  Combined with
                ///        @ref frameIndex to derive the timestamp.
                ///        Zero is invalid; the TX thread treats a
                ///        zero clock rate as a programming error and
                ///        drops the batch.
                uint32_t clockRate = 0;

                /// @brief When @c true the TX thread sets the marker
                ///        bit on the last packet of @ref packets and
                ///        leaves it cleared on the rest.  @c false
                ///        for non-AU-final batches (e.g. spread of
                ///        an access unit across multiple batches);
                ///        marker stays cleared everywhere.
                bool markerOnLast = true;

                /// @brief Per-frame transmit-rate cap in bits per
                ///        second, applied via
                ///        @ref RtpSession::setPacingRate before this
                ///        batch is dispatched.  Recomputed by the
                ///        VBR video packetizer from each frame's
                ///        actual packed byte count.  Zero means
                ///        "leave the rate cap at whatever was
                ///        previously configured" — used by CBR /
                ///        uncompressed video and by data / audio.
                uint64_t rateCapBps = 0;

                /// @brief Steady-clock instant the packetizer pushed
                ///        the batch onto the @c PacketQueue.  The
                ///        TX thread subtracts emission-time from
                ///        this to record queue-latency histograms;
                ///        defaulted to @c TimeStamp() so callers
                ///        that don't care just leave it.
                TimeStamp enqueuedAt;

                /// @brief Optional ST 2110-40 §6.4 LLTM-style
                ///        @c CLOCK_TAI deadline in nanoseconds since
                ///        the Unix epoch (TAI timescale).
                ///
                ///        When non-zero, @ref RtpSession::sendPackets
                ///        stamps every datagram in this batch with
                ///        @c txTimeNs = @ref deadlineTaiNs +
                ///        j × @ref deadlineStrideNs before handing
                ///        the list to the scheduler.  The
                ///        @ref TxTimePacketScheduler then passes the
                ///        pre-stamped values through to the kernel's
                ///        ETF qdisc instead of computing its own
                ///        deadlines.
                ///
                ///        ST 2110-40 LLTM ANC packetizers fill this
                ///        with @c T_FST + T_EPO + T_D and leave
                ///        @ref deadlineStrideNs at @c 0 (every
                ///        packet of the frame shares the same
                ///        deadline).  ST 2110-21 narrow-timing
                ///        video TX paths fill this with @c T_VD
                ///        and set @ref deadlineStrideNs to
                ///        @c T_RS so the kernel receives per-packet
                ///        @c TPR_j deadlines.  Video / data / audio
                ///        TX paths that don't engage either model
                ///        leave both fields at @c 0 to keep the
                ///        scheduler in charge of pacing.
                uint64_t deadlineTaiNs = 0;

                /// @brief Optional ST 2110-21 §7.1 @c T_RS stride
                ///        between successive packets' deadlines, in
                ///        nanoseconds.  0 = every packet shares
                ///        @ref deadlineTaiNs (LLTM ANC behaviour);
                ///        non-zero = per-packet
                ///        @c TPR_j = deadlineTaiNs + j × stride
                ///        (ST 2110-21 narrow-timing video).
                ///
                ///        Only consulted when @ref deadlineTaiNs is
                ///        non-zero — there's no concept of a stride
                ///        without an anchor.
                uint64_t deadlineStrideNs = 0;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NETWORK
