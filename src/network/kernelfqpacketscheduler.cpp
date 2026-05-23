/**
 * @file      kernelfqpacketscheduler.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/kernelfqpacketscheduler.h>

#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

Error KernelFqPacketScheduler::configure(const Spec &spec) {
        Error err = PacketScheduler::configure(spec);
        if (err.isError()) return err;
        return applyRate();
}

Error KernelFqPacketScheduler::applyRate() {
        if (_transport == nullptr) return Error::NotOpen;
        // Deduplicate against the last applied rate so VBR video
        // doesn't storm @c setsockopt(SO_MAX_PACING_RATE) on every
        // batch when the rate happens to be unchanged.
        if (_everApplied && _spec.bytesPerSec == _lastAppliedRate) return Error::Ok;
        Error rateErr = _transport->setPacingRate(_spec.bytesPerSec);
        if (rateErr.isError() && rateErr != Error::NotSupported) {
                return rateErr;
        }
        _lastAppliedRate = _spec.bytesPerSec;
        _everApplied = true;
        return Error::Ok;
}

int KernelFqPacketScheduler::enqueue(const PacketTransport::DatagramList &datagrams) {
        if (_transport == nullptr) return -1;
        if (datagrams.isEmpty()) return 0;

        // Loop on partial accept just like burst — the kernel @c fq
        // qdisc handles the per-packet timing once the bytes are in
        // its queue.
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
                                "KernelFqPacketScheduler: transport->sendPackets failed (offset=%zu remaining=%zu)",
                                offset, remaining);
                        return -1;
                }
                if (sent == 0) {
                        promekiWarnThrottled(
                                1000,
                                "KernelFqPacketScheduler: stalled — no progress (offset=%zu remaining=%zu)",
                                offset, remaining);
                        return -1;
                }
                offset += static_cast<size_t>(sent);
        }
        return static_cast<int>(datagrams.size());
}

int KernelFqPacketScheduler::predictedTxDelayUs() const {
        // The kernel paces submitted bytes at the requested rate.
        // Worst-case D_TX is bounded by one frame interval (the
        // submission unit) when both terms are known.
        if (_spec.bytesPerSec == 0) return 0;
        if (!_spec.frameInterval.isValid()) return 0;
        return static_cast<int>(_spec.frameInterval.microseconds());
}

PROMEKI_NAMESPACE_END
