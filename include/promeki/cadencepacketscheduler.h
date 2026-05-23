/**
 * @file      cadencepacketscheduler.h
 * @copyright Howard Logic. All rights reserved.
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
 * @brief Userspace cadence scheduler — @c sleep_until between packets.
 * @ingroup network
 *
 * The default for @c RtpPacingMode::Userspace.  Two cadence
 * semantics are exposed via @ref PacketScheduler::CadenceMode:
 *
 * - **PerBatch** (the default and right answer for video / data):
 *   Each @ref enqueue call spreads its batch across one
 *   @c frameInterval.  The cadence is anchored at
 *   @c TimeStamp::now() inside the call, so per-frame jitter on the
 *   strand absorbs into a local re-anchoring rather than
 *   accumulating across frames.  Per-packet interval is
 *   @c frameInterval / @c batch.size() — variable-bitrate batches
 *   automatically spread the right way.
 *
 * - **Streamwide** (right for audio): The cadence is anchored once at
 *   @ref configure time and tick-incremented per @ref enqueue.  Each
 *   batch is expected to be a single packet that lands on the next
 *   tick.  A long stall (> @c stallReanchorMultiplier × interval)
 *   re-anchors the cadence to "now" so the scheduler doesn't burst
 *   a flood of catch-up packets after recovering — matches the
 *   reanchoring policy the prior audio TX thread enforced inline.
 *
 * @par predictedTxDelayUs
 *
 * Returns the worst-case D_TX for the configured mode:
 *
 * - PerBatch: @c (packetsPerFrame − 1) × @c frameInterval / @c packetsPerFrame.
 *   For @c packetsPerFrame == 0 (the auto-derive case), returns the
 *   full @c frameInterval as a conservative upper bound.
 *
 * - Streamwide: 0 — every packet leaves exactly on its cadence tick,
 *   so there is no scheduler-introduced delay between RTP-TS and TX.
 */
class CadencePacketScheduler : public PacketScheduler {
        public:
                /** @brief Default stall-reanchor multiplier (10 intervals). */
                static constexpr int DefaultStallReanchorMultiplier = 10;

                CadencePacketScheduler();
                ~CadencePacketScheduler() override = default;

                Error configure(const Spec &spec) override;
                int   enqueue(const PacketTransport::DatagramList &datagrams) override;
                int   predictedTxDelayUs() const override;

                /**
                 * @brief Sets the @c stallReanchorMultiplier for the
                 *        @c Streamwide cadence (default 10 × interval).
                 */
                void setStallReanchorMultiplier(int n) { _stallMultiplier = n; }

        private:
                /// @brief Sends one datagram synchronously through the
                ///        bound transport, looping on partial accept.
                ///        Returns -1 on transport failure, 1 on success.
                int sendOne(const PacketTransport::Datagram &d);

                Cadence _streamCadence{Duration()};
                bool    _streamAnchored = false;
                int     _stallMultiplier = DefaultStallReanchorMultiplier;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NETWORK
