/**
 * @file      burstpacketscheduler.h
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
 * @brief No-pacing scheduler — hand every batch straight to the transport.
 * @ingroup network
 *
 * The default for @c RtpPacingMode::None.  Submits the entire batch
 * to @ref PacketTransport::sendPackets in a single sendmmsg-style
 * call, looping until every datagram is accepted by the kernel or a
 * transport error stops the drain.  No userspace cadence, no kernel
 * rate cap, no @c SO_TXTIME deadlines.
 *
 * @par When to use it
 *
 * - Bring-up + diagnostics: fastest path, no pacing variables to
 *   debug.
 * - Streams that can tolerate microburst behaviour on the receiver
 *   side (intra-LAN unicast tests, lab loopback).
 * - Receivers running on the same host (kernel loopback collapses
 *   any pacing anyway).
 *
 * @par Why not for production ST 2110
 *
 * A burst sender hands the kernel an entire frame's worth of packets
 * within microseconds.  The kernel @c fq qdisc then emits them
 * back-to-back at line rate — line-rate bursts violate every
 * ST 2110-21 type (N / NL / W) and saturate the receiver's RX queue
 * intermittently.  Use @ref KernelFqPacketScheduler or
 * @ref CadencePacketScheduler for production.
 */
class BurstPacketScheduler : public PacketScheduler {
        public:
                BurstPacketScheduler() = default;
                ~BurstPacketScheduler() override = default;

                int enqueue(const PacketTransport::DatagramList &datagrams) override;

                int predictedTxDelayUs() const override { return 0; }
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NETWORK
