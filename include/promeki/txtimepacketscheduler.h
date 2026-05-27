/**
 * @file      txtimepacketscheduler.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_NETWORK
#include <promeki/cadence.h>
#include <promeki/duration.h>
#include <promeki/namespace.h>
#include <promeki/packetscheduler.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Per-packet @c SO_TXTIME / @c SCM_TXTIME hardware pacing.
 * @ingroup network
 *
 * The default for @c RtpPacingMode::TxTime.  Computes a nanosecond
 * deadline for every datagram in the batch, stamps it onto
 * @ref PacketTransport::Datagram::txTimeNs, and submits the whole
 * batch in one @c sendmmsg call.  The kernel ETF qdisc + the NIC
 * holds each packet until its target time, achieving sub-microsecond
 * pacing without userspace sleeps.
 *
 * Requires the bound transport to support @c SO_TXTIME.  The
 * scheduler calls @ref PacketTransport::setTxTime(true) at
 * @ref configure time; a transport that returns
 * @c Error::NotSupported makes @c configure fail and any subsequent
 * @c enqueue fall back to burst dispatch (with a warning) so a host
 * without TXTIME support degrades gracefully.
 *
 * @par Deadline computation
 *
 * - **PerBatch** (video / data): The first datagram's deadline is
 *   @c TimeStamp::now() of the enqueue call.  Subsequent deadlines
 *   are @c base + i × interval where
 *   @c interval = frameInterval / batch.size().
 *
 * - **Streamwide** (audio): A long-running cadence anchored at
 *   @c configure time produces the deadline of every datagram.  A
 *   long stall re-anchors to @c now() so the kernel doesn't try to
 *   emit a burst of past-due packets.
 *
 * - **Pre-stamped** (ST 2110-40 §6.4 LLTM ANC, future ST 2110-21
 *   narrow-timing video): When the @ref enqueue caller supplies a
 *   non-zero @ref PacketTransport::Datagram::txTimeNs on every
 *   datagram (typically via
 *   @ref RtpPacketBatch::deadlineTaiNs propagating through
 *   @ref RtpSession::sendPackets), the scheduler bypasses its own
 *   deadline derivation and passes the batch straight to the
 *   transport so the kernel honours the caller's deadline.  The
 *   first datagram's @c txTimeNs is the gating check — partial
 *   pre-stamps are treated as a programming error.
 *
 * @par Clock domain
 *
 * Deadlines are CLOCK_TAI nanoseconds since 1970-01-01 TAI.  The
 * scheduler converts the @ref TimeStamp::now() steady-clock value to
 * TAI via the system's @c clock_gettime(CLOCK_TAI) at configure
 * time, recomputing the offset whenever @c configure is re-called.
 * The PTP-aware variant comes with Phase D2 of
 * @c devplan/network/2110.md once @c ClockDomain::Ptp is wired.
 *
 * @par predictedTxDelayUs
 *
 * Same as @ref CadencePacketScheduler: per-batch returns the
 * worst-case packet position × per-packet interval, streamwide
 * returns 0 (every packet leaves exactly on its TXTIME deadline).
 */
class TxTimePacketScheduler : public PacketScheduler {
        public:
                TxTimePacketScheduler() = default;
                ~TxTimePacketScheduler() override = default;

                Error configure(const Spec &spec) override;
                int   enqueue(const PacketTransport::DatagramList &datagrams) override;
                int   predictedTxDelayUs() const override;

        private:
                /// @brief Computes the CLOCK_TAI nanoseconds-since-epoch
                ///        value corresponding to @c TimeStamp::now().
                ///        Falls back to @c CLOCK_MONOTONIC + cached
                ///        offset on platforms without CLOCK_TAI.
                uint64_t taiNanosNow() const;

                Cadence  _streamCadence{Duration()};
                bool     _streamAnchored = false;
                bool     _txTimeEnabled = false;
                int64_t  _taiOffsetNs = 0; ///< @brief CLOCK_TAI − steady-clock nanosecond offset.
                int      _stallMultiplier = 10;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NETWORK
