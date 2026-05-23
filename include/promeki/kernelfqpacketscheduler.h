/**
 * @file      kernelfqpacketscheduler.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_NETWORK
#include <promeki/namespace.h>
#include <promeki/packetscheduler.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Kernel @c fq qdisc pacing via @c SO_MAX_PACING_RATE.
 * @ingroup network
 *
 * The default for @c RtpPacingMode::KernelFq.  Hands a byte-rate cap
 * to the bound transport (@ref PacketTransport::setPacingRate) at
 * @ref configure time and on every @ref setRate update, then submits
 * each batch unpaced via @c sendmmsg.  The kernel's @c fq qdisc (the
 * default scheduler on modern Linux) spaces the actual emissions to
 * the requested rate with no per-packet CPU cost.
 *
 * @par When to use it
 *
 * - Cross-host production with shared NIC bandwidth: the kernel
 *   absorbs the per-packet timing, the userspace process only
 *   submits.
 * - VBR compressed video where @c batch.rateCapBps changes per
 *   frame; the per-frame @ref setRate is cheap (a single
 *   setsockopt).
 * - ST 2110-21 Type W compliance on stock Linux without DPDK /
 *   hardware TXTIME.
 *
 * @par What it does NOT do
 *
 * - **Type N narrow timing** — that requires per-packet hardware
 *   pacing (i210 / i225 / Mellanox CX6+).  Use
 *   @ref TxTimePacketScheduler when the NIC supports it; otherwise
 *   accept Type W self-labelling.
 *
 * @par predictedTxDelayUs
 *
 * Returns one frame interval (worst case) when both @c frameInterval
 * and @c bytesPerSec are set.  Returns 0 otherwise — without enough
 * config we can't predict; SDP TSDELAY stays omitted.
 */
class KernelFqPacketScheduler : public PacketScheduler {
        public:
                KernelFqPacketScheduler() = default;
                ~KernelFqPacketScheduler() override = default;

                Error configure(const Spec &spec) override;
                int   enqueue(const PacketTransport::DatagramList &datagrams) override;
                int   predictedTxDelayUs() const override;

        private:
                /// @brief Applies the current @c _spec.bytesPerSec to
                ///        the transport.  Idempotent — schedulers
                ///        deduplicate against the most-recent value
                ///        to avoid setsockopt storms on per-frame VBR
                ///        rate updates.
                Error applyRate();

                uint64_t _lastAppliedRate = 0;
                bool     _everApplied = false;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NETWORK
