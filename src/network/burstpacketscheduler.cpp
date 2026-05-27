/**
 * @file      burstpacketscheduler.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/burstpacketscheduler.h>

#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

int BurstPacketScheduler::enqueue(const PacketTransport::DatagramList &datagrams) {
        if (_transport == nullptr) return -1;
        if (datagrams.isEmpty()) return 0;

        // sendmmsg may not accept all datagrams in one syscall when the
        // socket buffer is full (large uncompressed video frames pack
        // thousands of packets).  Loop to drain the remainder, just
        // like the prior @ref RtpSession::sendPackets did inline.
        size_t offset = 0;
        while (offset < datagrams.size()) {
                PacketTransport::DatagramList sub;
                size_t                        remaining = datagrams.size() - offset;
                sub.reserve(remaining);
                for (size_t i = offset; i < datagrams.size(); i++) {
                        sub.pushToBack(datagrams[i]);
                }
                int sent = _transport->sendPackets(sub);
                if (sent < 0) {
                        promekiWarnThrottled(
                                1000,
                                "BurstPacketScheduler::enqueue transport->sendPackets failed (offset=%zu remaining=%zu)",
                                offset, remaining);
                        return -1;
                }
                if (sent == 0) {
                        promekiWarnThrottled(
                                1000,
                                "BurstPacketScheduler::enqueue stalled — no progress (offset=%zu remaining=%zu)",
                                offset, remaining);
                        return -1;
                }
                offset += static_cast<size_t>(sent);
        }
        return static_cast<int>(datagrams.size());
}

PROMEKI_NAMESPACE_END
