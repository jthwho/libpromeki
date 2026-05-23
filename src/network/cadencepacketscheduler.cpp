/**
 * @file      cadencepacketscheduler.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/cadencepacketscheduler.h>

#include <promeki/logger.h>
#include <promeki/timestamp.h>

PROMEKI_NAMESPACE_BEGIN

CadencePacketScheduler::CadencePacketScheduler() = default;

Error CadencePacketScheduler::configure(const Spec &spec) {
        Error err = PacketScheduler::configure(spec);
        if (err.isError()) return err;

        // The stream-wide cadence anchor is established lazily on the
        // first enqueue so the @c TimeStamp::now reference instant is
        // the *real* stream start rather than the configure-time
        // instant (which may precede the first packet by an unknown
        // amount of bring-up latency).
        _streamAnchored = false;
        _streamCadence = Cadence(spec.frameInterval);
        return Error::Ok;
}

int CadencePacketScheduler::sendOne(const PacketTransport::Datagram &d) {
        if (_transport == nullptr) return -1;
        PacketTransport::DatagramList one;
        one.pushToBack(d);
        int sent = _transport->sendPackets(one);
        if (sent < 0) {
                promekiWarnThrottled(1000, "CadencePacketScheduler: transport->sendPackets failed");
                return -1;
        }
        if (sent == 0) {
                promekiWarnThrottled(1000, "CadencePacketScheduler: transport accepted 0 datagrams");
                return -1;
        }
        return 1;
}

int CadencePacketScheduler::enqueue(const PacketTransport::DatagramList &datagrams) {
        if (_transport == nullptr) return -1;
        if (datagrams.isEmpty()) return 0;

        const size_t   n = datagrams.size();
        const Duration interval = _spec.frameInterval;

        if (!interval.isValid()) {
                // No temporal budget configured — degrade to burst.
                int sent = _transport->sendPackets(datagrams);
                return sent;
        }

        if (cadenceMode() == CadenceMode::Streamwide) {
                // Audio-style: per-batch sizes are typically 1, anchor
                // is stream-long, stall recovery re-anchors.
                if (!_streamAnchored) {
                        _streamCadence = Cadence(interval);
                        _streamCadence.anchor(TimeStamp::now());
                        _streamAnchored = true;
                }
                const Duration stallThreshold = interval * _stallMultiplier;
                int            accepted = 0;
                for (size_t i = 0; i < n; i++) {
                        TimeStamp deadline = _streamCadence.next();
                        deadline.sleepUntil();
                        const TimeStamp wakeTime = TimeStamp::now();
                        if (wakeTime - deadline > stallThreshold) {
                                _streamCadence.reanchor(wakeTime);
                        }
                        int s = sendOne(datagrams[i]);
                        if (s < 0) return accepted > 0 ? accepted : -1;
                        accepted += s;
                }
                return accepted;
        }

        // PerBatch: spread N packets across one frameInterval.
        // First packet leaves immediately; remaining packets are
        // delayed by k × (frameInterval / N).  Matches the per-frame
        // anchor-to-now behaviour that the prior video TX thread
        // used inline.
        const Duration perPacket = n > 1 ? interval / static_cast<int64_t>(n) : interval;
        Cadence        pacer(perPacket);
        pacer.anchor(TimeStamp::now());
        int accepted = 0;
        for (size_t i = 0; i < n; i++) {
                pacer.next().sleepUntil();
                int s = sendOne(datagrams[i]);
                if (s < 0) return accepted > 0 ? accepted : -1;
                accepted += s;
        }
        return accepted;
}

int CadencePacketScheduler::predictedTxDelayUs() const {
        const Duration interval = _spec.frameInterval;
        if (!interval.isValid()) return 0;
        if (cadenceMode() == CadenceMode::Streamwide) {
                // Streamwide cadence emits exactly on the tick; the
                // egress delay relative to the cadence anchor is
                // bounded by sleep precision, which is negligible at
                // SDP-fmtp granularity.
                return 0;
        }
        // Per-batch spread: worst-case D_TX is the last packet's
        // (N − 1) × perPacket delay.  When packetsPerFrame is unset,
        // be conservative and assume the full interval.
        if (_spec.packetsPerFrame <= 1) {
                return static_cast<int>(interval.microseconds());
        }
        const int64_t perPacketUs = interval.microseconds() / _spec.packetsPerFrame;
        return static_cast<int>(perPacketUs * (_spec.packetsPerFrame - 1));
}

PROMEKI_NAMESPACE_END
