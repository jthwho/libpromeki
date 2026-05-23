/**
 * @file      rtpdatadepacketizerthread.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/rtpdatadepacketizerthread.h>

#include <utility>

#include <promeki/buffer.h>
#include <promeki/error.h>
#include <promeki/json.h>
#include <promeki/logger.h>
#include <promeki/mediatimestamp.h>
#include <promeki/metadata.h>
#include <promeki/result.h>

PROMEKI_NAMESPACE_BEGIN

RtpDataDepacketizerThread::RtpDataDepacketizerThread(
        RtpDataDepacketizerContext ctx, const String &name,
        uint32_t clockRateHz, size_t queueDepth)
    : RtpDepacketizerThread(name, clockRateHz, queueDepth), _ctx(std::move(ctx)) {}

RtpDataDepacketizerThread::~RtpDataDepacketizerThread() {
        requestStop();
        if (!isCurrentThread()) wait();
}

void RtpDataDepacketizerThread::handlePacket(const RtpPacket &pkt) {
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
                emitMessage();
        }

        _reasmPackets.pushToBack(pkt);
        _reasmTimestamp = pkt.timestamp();
        _reasmHasTimestamp = true;

        if (pkt.marker()) emitMessage();
}

void RtpDataDepacketizerThread::onStop() {
        _reasmPackets.clear();
        _reasmHasTimestamp = false;
}

void RtpDataDepacketizerThread::emitMessage() {
        if (_reasmPackets.isEmpty()) return;
        if (_ctx.payload == nullptr) {
                _reasmPackets.clear();
                _reasmHasTimestamp = false;
                return;
        }
        const uint32_t  dataRtpTimestamp = _reasmTimestamp;
        const int32_t   dataPacketCount = static_cast<int32_t>(_reasmPackets.size());
        const TimeStamp firstPktArrival = _reasmPackets[0].arrivalSteady;

        Buffer bytes = _ctx.payload->unpack(_reasmPackets);
        _reasmPackets.clear();
        _reasmHasTimestamp = false;
        if (bytes.size() == 0) return;

        String     jsonText(static_cast<const char *>(bytes.data()), bytes.size());
        Error      jerr;
        JsonObject obj = JsonObject::parse(jsonText, &jerr);
        if (jerr.isError()) {
                promekiWarn("RtpDataDepacketizerThread: dropping malformed metadata JSON: %s",
                            jerr.desc().cstr());
                return;
        }
        Metadata m = Metadata::fromJson(obj);

        TimeStamp  captureTime;
        NtpTime    wallclockNtp;
        const bool hasSr = _ctx.hasSr != nullptr && *_ctx.hasSr;
        const bool clockValid = _ctx.streamClock != nullptr &&
                                _ctx.streamClock->isValid();
        if (hasSr && clockValid) {
                wallclockNtp = _ctx.streamClock->toNtp(dataRtpTimestamp);
                TimeStamp steady;
                if (_ctx.ntpToSteady) steady = _ctx.ntpToSteady(wallclockNtp);
                if (steady.isValid()) captureTime = steady;
        }
        if (!captureTime.isValid()) captureTime = captureTimeForRtpTs(dataRtpTimestamp);
        if (!captureTime.isValid()) captureTime = firstPktArrival;
        if (!captureTime.isValid()) captureTime = TimeStamp::now();

        MediaTimeStamp capMts(captureTime, _ctx.clockDomain);
        m.set(Metadata::CaptureTime, capMts);
        m.set(Metadata::RtpTimestamp, dataRtpTimestamp);
        m.set(Metadata::RtpPacketCount, dataPacketCount);

        RxDataMessage bundle;
        bundle.metadata = std::move(m);
        bundle.rtpTimestamp = dataRtpTimestamp;
        bundle.packetCount = dataPacketCount;
        bundle.wallclockNtp = wallclockNtp;
        bundle.captureTime = captureTime;
        bundle.firstPacketArrival = firstPktArrival;

        if (_ctx.payloadQueue != nullptr) {
                Error perr = _ctx.payloadQueue->pushBlocking(std::move(bundle));
                if (perr.isError() && perr != Error::Cancelled) {
                        promekiWarn("RtpDataDepacketizerThread: payload queue push failed: %s",
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
