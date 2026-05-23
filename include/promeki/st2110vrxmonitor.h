/**
 * @file      st2110vrxmonitor.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_NETWORK
#include <cstdint>
#include <promeki/duration.h>
#include <promeki/namespace.h>
#include <promeki/timestamp.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Runtime ST 2110-21 VRX leaky-bucket monitor.
 * @ingroup network
 *
 * ST 2110-21:2022 §7.1 defines the Virtual Receive buffer (VRX)
 * model: a fictional receiver-side leaky bucket that drains at the
 * stream's wire rate.  A conformant sender must not push the bucket
 * occupancy past @c VRX_FULL bytes, and must not emit a sustained
 * burst exceeding @c CMAX packets.  This class implements the
 * sender-side observation half of that model — every emitted packet
 * is fed in via @ref observePacket, and the monitor reports peak
 * occupancy + CMAX-violation counts for diagnostic surfacing.
 *
 * @par What this is for
 * Not a wire-level shaper.  The library's TX schedulers
 * (@ref BurstPacketScheduler, @ref CadencePacketScheduler,
 * @ref KernelFqPacketScheduler, @ref TxTimePacketScheduler) own the
 * actual packet release timing.  This monitor sits @em after the
 * scheduler and audits whether the resulting traffic stays inside
 * the VRX_FULL / CMAX envelope the sender's SDP @c TP fmtp value
 * (@c 2110TPN / @c 2110TPNL / @c 2110TPW) advertises.  A
 * @ref violationCount &gt; 0 means the sender's labelled type
 * doesn't match its observed behaviour — a deployment-quality
 * signal, not a hot-path correctness gate.
 *
 * @par Algorithm
 * Each call to @ref observePacket adds @p sizeBytes to the bucket
 * and advances the drain by @c (timestamp - lastTimestamp) ×
 * @c drainRateBps / 1e9 bytes.  The bucket is clamped at @c 0
 * from below; peak occupancy is the maximum bucket value across
 * all observed packets.  CMAX is a separate sustained-burst counter
 * that increments on every packet whose arrival is within the
 * configured @ref burstWindow of the previous one, and resets when
 * a gap larger than the window is seen.
 *
 * @par Units
 *  - Timestamps: caller-supplied, must be monotone non-decreasing.
 *    Typically @ref TimeStamp::nanoseconds() from each packet's
 *    intended TX-time deadline (or @c TimeStamp::now() at emission
 *    when no deadline is available).
 *  - Sizes: bytes per packet (the whole UDP payload — RTP header
 *    plus payload).
 *  - Drain rate: bytes per second.  Pass the stream's wire rate —
 *    the leaky bucket simulates a receiver pulling bytes off the
 *    network at that rate.
 *
 * @par Thread Safety
 * The monitor is thread-affine; concurrent calls to
 * @ref observePacket are not safe.  In typical use one TX thread
 * owns the monitor and observes its own emissions.
 */
class St2110VrxMonitor {
        public:
                /**
                 * @brief Default-constructs a monitor in the "unconfigured"
                 *        state — every @ref observePacket call is a no-op
                 *        until @ref configure has been invoked.
                 */
                St2110VrxMonitor() = default;

                /**
                 * @brief Configures the monitor's drain rate +
                 *        VRX_FULL bound + CMAX burst-window.
                 *
                 * @param drainRateBytesPerSec   Wire rate the
                 *                               virtual receiver
                 *                               drains at.  Pass
                 *                               the stream's
                 *                               negotiated rate.
                 * @param vrxFullBytes           ST 2110-21 §7.1
                 *                               VRX_FULL bound from
                 *                               @ref St2110Tx.
                 * @param cmaxPackets            ST 2110-21 §7.1
                 *                               CMAX informational
                 *                               burst limit.
                 * @param burstWindow            Inter-arrival
                 *                               threshold for the
                 *                               CMAX counter — a
                 *                               packet within this
                 *                               window of its
                 *                               predecessor counts
                 *                               as part of the
                 *                               current burst.
                 *                               Defaults to 5 µs
                 *                               (an order of
                 *                               magnitude under the
                 *                               minimum AES67
                 *                               packet time so
                 *                               adjacent
                 *                               narrow-timing
                 *                               packets get
                 *                               counted as a single
                 *                               burst).
                 */
                void configure(uint64_t drainRateBytesPerSec, int64_t vrxFullBytes,
                               int cmaxPackets,
                               const Duration &burstWindow = Duration::fromMicroseconds(5));

                /**
                 * @brief Resets the running counters but keeps the
                 *        configured drain rate / VRX_FULL / CMAX.
                 */
                void reset();

                /**
                 * @brief Feeds one packet observation into the
                 *        bucket.
                 *
                 * Advances the drain to @p timestamp, then adds
                 * @p sizeBytes to the bucket and records the new
                 * peak.  CMAX counter is incremented when @p
                 * timestamp is within @ref burstWindow of the
                 * previous packet's timestamp; reset otherwise.
                 *
                 * No-op when the monitor has not been configured.
                 *
                 * @param timestamp Steady-clock-equivalent
                 *                  nanoseconds (any monotone
                 *                  non-decreasing scale works; what
                 *                  matters is consistency across
                 *                  calls).  Going backwards is
                 *                  treated as @c 0 advance.
                 * @param sizeBytes Packet size in bytes.  Ignored
                 *                  when ≤ 0.
                 */
                void observePacket(int64_t timestamp, int sizeBytes);

                /// @brief @c true once @ref configure has been called with a positive drain rate.
                bool isConfigured() const { return _drainRateBytesPerSec > 0; }

                /// @brief Configured drain rate in bytes / second.
                uint64_t drainRateBytesPerSec() const { return _drainRateBytesPerSec; }

                /// @brief Configured VRX_FULL bound in bytes.
                int64_t vrxFullBytes() const { return _vrxFullBytes; }

                /// @brief Configured CMAX bound in packets.
                int cmaxPackets() const { return _cmaxPackets; }

                /// @brief Current bucket occupancy in bytes.
                int64_t occupancyBytes() const { return _occupancyBytes; }

                /// @brief Highest bucket occupancy observed since
                ///        the most recent @ref reset / @ref configure.
                int64_t peakOccupancyBytes() const { return _peakOccupancyBytes; }

                /// @brief Highest sustained burst observed since
                ///        the most recent @ref reset / @ref configure.
                int peakBurstPackets() const { return _peakBurstPackets; }

                /// @brief Number of times @ref peakOccupancyBytes
                ///        exceeded @ref vrxFullBytes since the
                ///        most recent @ref reset / @ref configure.
                int64_t vrxViolations() const { return _vrxViolations; }

                /// @brief Number of times the running burst counter
                ///        exceeded @ref cmaxPackets since the most
                ///        recent @ref reset / @ref configure.
                int64_t cmaxViolations() const { return _cmaxViolations; }

                /// @brief Total packet count observed since the
                ///        most recent @ref reset / @ref configure.
                int64_t observedPackets() const { return _observedPackets; }

                /// @brief Total bytes observed since the most
                ///        recent @ref reset / @ref configure.
                int64_t observedBytes() const { return _observedBytes; }

                /**
                 * @brief Convenience: @c true when both
                 *        @ref vrxViolations and @ref cmaxViolations
                 *        are zero — the observed traffic stayed
                 *        within the configured VRX_FULL / CMAX
                 *        envelope.
                 */
                bool isConformant() const { return _vrxViolations == 0 && _cmaxViolations == 0; }

        private:
                uint64_t _drainRateBytesPerSec = 0;
                int64_t  _vrxFullBytes = 0;
                int      _cmaxPackets = 0;
                int64_t  _burstWindowNs = 5'000;

                int64_t _occupancyBytes = 0;
                int64_t _peakOccupancyBytes = 0;
                int     _currentBurstPackets = 0;
                int     _peakBurstPackets = 0;
                int64_t _vrxViolations = 0;
                int64_t _cmaxViolations = 0;
                int64_t _observedPackets = 0;
                int64_t _observedBytes = 0;
                int64_t _lastTimestampNs = -1;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NETWORK
