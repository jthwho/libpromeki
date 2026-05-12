/**
 * @file      rtpancdepacketizerthread.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/rtpancdepacketizerthread.h>

#include <utility>

#include <promeki/error.h>
#include <promeki/logger.h>
#include <promeki/mediatimestamp.h>
#include <promeki/rtppayloadanc.h>

PROMEKI_NAMESPACE_BEGIN

RtpAncDepacketizerThread::RtpAncDepacketizerThread(
        RtpAncDepacketizerContext ctx, const String &name, uint32_t clockRateHz)
    : RtpDepacketizerThread(name, clockRateHz), _ctx(std::move(ctx)) {}

RtpAncDepacketizerThread::~RtpAncDepacketizerThread() {
        requestStop();
        if (!isCurrentThread()) wait();
}

void RtpAncDepacketizerThread::handlePacket(const RtpPacket &pkt) {
        if (_ctx.active != nullptr && !*_ctx.active) return;

        if (_ctx.resetEpoch != nullptr) {
                const uint32_t epoch = _ctx.resetEpoch->value();
                if (epoch != _lastEpoch) {
                        _lastEpoch = epoch;
                        if (!_reasmPackets.isEmpty()) {
                                if (_ctx.framesDroppedSsrcReset != nullptr) {
                                        _ctx.framesDroppedSsrcReset->fetchAndAdd(1);
                                }
                        }
                        _reasmPackets.clear();
                        _reasmHasTimestamp = false;
                        _reasmTimestamp = 0;
                        resetAnchor();
                }
        }

        if (_ctx.packetsReceived != nullptr) _ctx.packetsReceived->fetchAndAdd(1);
        if (_ctx.bytesReceived != nullptr) {
                _ctx.bytesReceived->fetchAndAdd(static_cast<int64_t>(pkt.size()));
        }
        if (_ctx.lastPacketArrivalNs != nullptr) {
                _ctx.lastPacketArrivalNs->setValue(pkt.arrivalSteady.nanoseconds());
        }

        if (_ctx.refreshStreamClock) _ctx.refreshStreamClock();
        ensureAnchor(pkt.timestamp(), pkt.arrivalSteady);

        if (_reasmHasTimestamp && _reasmTimestamp != pkt.timestamp() &&
            !_reasmPackets.isEmpty()) {
                emitFrame();
        }

        _reasmPackets.pushToBack(pkt);
        _reasmTimestamp = pkt.timestamp();
        _reasmHasTimestamp = true;

        if (pkt.marker()) emitFrame();
}

void RtpAncDepacketizerThread::onStop() {
        _reasmPackets.clear();
        _reasmHasTimestamp = false;
}

void RtpAncDepacketizerThread::emitFrame() {
        if (_reasmPackets.isEmpty()) return;
        if (_ctx.payload == nullptr) {
                _reasmPackets.clear();
                _reasmHasTimestamp = false;
                return;
        }

        const uint32_t  ancRtpTimestamp = _reasmTimestamp;
        const int32_t   packetCount = static_cast<int32_t>(_reasmPackets.size());
        const TimeStamp firstPktArrival = _reasmPackets[0].arrivalSteady;

        AncPacket::List packets;
        Error           err = _ctx.payload->unpackAncPackets(_reasmPackets, packets);
        _reasmPackets.clear();
        _reasmHasTimestamp = false;
        if (err.isError()) {
                promekiWarn("RtpAncDepacketizerThread: dropping malformed ANC frame: %s",
                            err.desc().cstr());
                return;
        }
        if (packets.isEmpty()) {
                // Every RTP packet in this batch advertised ANC_Count=0;
                // RFC 8331-aware receivers drop such payloads.  Silent
                // skip — no warning.
                return;
        }

        TimeStamp  captureTime;
        NtpTime    wallclockNtp;
        const bool hasSr = _ctx.hasSr != nullptr && *_ctx.hasSr;
        const bool clockValid =
                _ctx.streamClock != nullptr && _ctx.streamClock->isValid();
        if (hasSr && clockValid) {
                wallclockNtp = _ctx.streamClock->toNtp(ancRtpTimestamp);
                TimeStamp steady;
                if (_ctx.ntpToSteady) steady = _ctx.ntpToSteady(wallclockNtp);
                if (steady.nanoseconds() != 0) captureTime = steady;
        }
        if (captureTime.nanoseconds() == 0) {
                captureTime = captureTimeForRtpTs(ancRtpTimestamp);
        }
        if (captureTime.nanoseconds() == 0) captureTime = firstPktArrival;
        if (captureTime.nanoseconds() == 0) captureTime = TimeStamp::now();

        RxAncFrame bundle;
        bundle.desc = _ctx.desc;
        bundle.packets = std::move(packets);
        bundle.rtpTimestamp = ancRtpTimestamp;
        bundle.packetCount = packetCount;
        bundle.wallclockNtp = wallclockNtp;
        bundle.captureTime = captureTime;
        bundle.firstPacketArrival = firstPktArrival;

        if (_ctx.payloadQueue != nullptr) {
                Error perr = _ctx.payloadQueue->pushBlocking(std::move(bundle));
                if (perr.isError() && perr != Error::Cancelled) {
                        promekiWarn(
                                "RtpAncDepacketizerThread: payload queue push failed: %s",
                                perr.desc().cstr());
                        return;
                }
        }
        if (_ctx.noteFrameReceived) _ctx.noteFrameReceived();
        if (_ctx.framesReassembled != nullptr) {
                _ctx.framesReassembled->fetchAndAdd(1);
        }
}

PROMEKI_NAMESPACE_END
