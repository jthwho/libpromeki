/**
 * @file      rtpvideodepacketizerthread.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/rtpvideodepacketizerthread.h>

#include <cstring>
#include <utility>

#include <promeki/buffer.h>
#include <promeki/bufferview.h>
#include <promeki/compressedvideopayload.h>
#include <promeki/error.h>
#include <promeki/h264bitstream.h>
#include <promeki/hevcbitstream.h>
#include <promeki/logger.h>
#include <promeki/mediatimestamp.h>
#include <promeki/metadata.h>
#include <promeki/result.h>
#include <promeki/uncompressedvideopayload.h>

PROMEKI_NAMESPACE_BEGIN

RtpVideoDepacketizerThread::RtpVideoDepacketizerThread(
        RtpVideoDepacketizerContext ctx, const String &name,
        uint32_t clockRateHz, size_t queueDepth)
    : RtpDepacketizerThread(name, clockRateHz, queueDepth), _ctx(std::move(ctx)) {}

RtpVideoDepacketizerThread::~RtpVideoDepacketizerThread() {
        requestStop();
        if (!isCurrentThread()) wait();
}

void RtpVideoDepacketizerThread::handlePacket(const RtpPacket &pkt) {
        if (_ctx.active != nullptr && !*_ctx.active) return;

        if (_ctx.resetEpoch != nullptr) {
                const uint32_t epoch = _ctx.resetEpoch->value();
                if (epoch != _lastEpoch) {
                        _lastEpoch = epoch;
                        if (!_reasmPackets.isEmpty() &&
                            _ctx.framesDroppedSsrcReset != nullptr) {
                                _ctx.framesDroppedSsrcReset->fetchAndAdd(1);
                        }
                        _reasmPackets.clear();
                        _reasmHasTimestamp = false;
                        _reasmTimestamp = 0;
                        _hasFrameStart = false;
                        _frameStartTime = TimeStamp();
                        _hasLastPacket = false;
                        _hasLastFrame = false;
                        _streamFrameIndex = FrameNumber(0);
                        resetAnchor();
                        _jpegProbe.reset();
                        if (_ctx.payload != nullptr) _ctx.payload->clearParamSets();
                        // Do NOT clear @c readerImageDesc here.  For
                        // codec-derived geometry (JPEG, H.264, HEVC)
                        // the next probe / IDR re-populates it on the
                        // first post-reset frame whether or not we
                        // cleared.  For raw RFC 4175, geometry was
                        // pinned from @c VideoSize + @c VideoPixelFormat
                        // at @c configureVideoStream time and never
                        // arrives over the wire; clearing it on every
                        // SSRC change would permanently disable the
                        // raw receiver — @c emitFrame's
                        // @c !idesc.isValid() guard would drop every
                        // post-reset frame outright.
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

        const TimeStamp now = TimeStamp::now();
        if (_hasLastPacket && _ctx.rxPacketInterval != nullptr) {
                const Duration delta = now - _lastPacketTime;
                _ctx.rxPacketInterval->addSample(delta.microseconds());
        }
        _lastPacketTime = now;
        _hasLastPacket = true;

        if (_reasmHasTimestamp && _reasmTimestamp != pkt.timestamp() &&
            !_reasmPackets.isEmpty()) {
                emitFrame();
        }

        if (_reasmPackets.isEmpty()) {
                _frameStartTime = now;
                _hasFrameStart = true;
        }

        _reasmPackets.pushToBack(pkt);
        _reasmTimestamp = pkt.timestamp();
        _reasmHasTimestamp = true;

        if (pkt.marker()) emitFrame();
}

void RtpVideoDepacketizerThread::onStop() {
        _reasmPackets.clear();
        _reasmHasTimestamp = false;
}

void RtpVideoDepacketizerThread::emitFrame() {
        if (_reasmPackets.isEmpty()) return;
        if (_ctx.payload == nullptr) {
                _reasmPackets.clear();
                _reasmHasTimestamp = false;
                return;
        }

        // RFC 2435 Type byte (subsampling) lives at byte 4 of the
        // 8-byte JPEG payload header.  Captured before unpack()
        // consumes the packet list.
        uint8_t rfc2435Type = 0;
        if (!_reasmPackets.isEmpty()) {
                const RtpPacket &first = _reasmPackets[0];
                if (!first.isNull() && first.payloadSize() >= 8) {
                        rfc2435Type = first.payload()[4];
                }
        }

        const uint32_t  frameRtpTimestamp = _reasmTimestamp;
        const int32_t   framePacketCount = static_cast<int32_t>(_reasmPackets.size());
        const TimeStamp firstPktArrival = _reasmPackets[0].arrivalSteady;

        Buffer reassembled = _ctx.payload->unpack(_reasmPackets);
        _reasmPackets.clear();
        _reasmHasTimestamp = false;
        if (reassembled.size() == 0) return;

        const RtpPayload::ValidateResult vr = _ctx.payload->validate(reassembled);
        if (vr == RtpPayload::ValidateResult::DropSilently) {
                if (_ctx.framesDroppedValidate != nullptr) {
                        _ctx.framesDroppedValidate->fetchAndAdd(1);
                }
                return;
        }
        if (vr == RtpPayload::ValidateResult::Wait) {
                if (_ctx.framesWaitingParamSets != nullptr) {
                        _ctx.framesWaitingParamSets->fetchAndAdd(1);
                }
                return;
        }

        ImageDesc idesc;
        if (_ctx.readerImageDesc != nullptr) idesc = *_ctx.readerImageDesc;
        if (_ctx.payload->isJpeg()) {
                const JpegGeometryProbe::Result &probe =
                        _jpegProbe.probe(reassembled, rfc2435Type, _ctx.fmtp);
                if (!probe.valid) return;
                if (_ctx.readerImageDesc != nullptr &&
                    (!_ctx.readerImageDesc->isValid() ||
                     _ctx.readerImageDesc->size() != probe.size ||
                     _ctx.readerImageDesc->pixelFormat() != probe.pixelFormat)) {
                        idesc = probe.imageDesc();
                        *_ctx.readerImageDesc = idesc;
                        promekiInfo("RtpVideoDepacketizerThread: JPEG reader discovered "
                                    "%ux%u %s",
                                    static_cast<unsigned>(probe.size.width()),
                                    static_cast<unsigned>(probe.size.height()),
                                    probe.pixelFormat.name().cstr());
                }
        }
        if (!idesc.isValid()) return;

        Buffer plane = Buffer(reassembled.size());
        std::memcpy(plane.data(), reassembled.data(), reassembled.size());
        plane.setSize(reassembled.size());
        const PixelFormat &pd = idesc.pixelFormat();

        if (_ctx.noteFrameReceived) _ctx.noteFrameReceived();

        const TimeStamp emitTime = TimeStamp::now();
        if (_hasFrameStart && _ctx.rxFrameAssembleTime != nullptr) {
                const Duration assemble = emitTime - _frameStartTime;
                _ctx.rxFrameAssembleTime->addSample(assemble.microseconds());
        }
        _hasFrameStart = false;
        if (_hasLastFrame && _ctx.rxFrameInterval != nullptr) {
                const Duration delta = emitTime - _lastFrameTime;
                _ctx.rxFrameInterval->addSample(delta.microseconds());
        }
        _lastFrameTime = emitTime;
        _hasLastFrame = true;

        TimeStamp  captureTime;
        NtpTime    wallclockNtp;
        const bool hasSr = _ctx.hasSr != nullptr && *_ctx.hasSr;
        const bool clockValid = _ctx.streamClock != nullptr &&
                                _ctx.streamClock->isValid();
        if (hasSr && clockValid) {
                wallclockNtp = _ctx.streamClock->toNtp(frameRtpTimestamp);
                TimeStamp steady;
                if (_ctx.ntpToSteady) steady = _ctx.ntpToSteady(wallclockNtp);
                if (steady.isValid()) captureTime = steady;
        }
        if (!captureTime.isValid()) captureTime = captureTimeForRtpTs(frameRtpTimestamp);
        if (!captureTime.isValid()) captureTime = firstPktArrival;
        if (!captureTime.isValid()) captureTime = emitTime;

        MediaTimeStamp capMts(captureTime, _ctx.clockDomain);
        {
                Metadata &m = idesc.metadata();
                m.set(Metadata::CaptureTime, capMts);
                m.set(Metadata::RtpTimestamp, frameRtpTimestamp);
                m.set(Metadata::RtpPacketCount, framePacketCount);
                if (!_ctx.ptpGrandmaster.isNull()) {
                        m.set(Metadata::PtpGrandmasterId, _ctx.ptpGrandmaster);
                }
        }

        VideoPayload::Ptr videoPayload;
        bool              keyframe = true;
        if (pd.isCompressed()) {
                const VideoCodec &codec = pd.videoCodec();
                if (codec.isValid() && codec.codingType() == VideoCodec::CodingTemporal) {
                        BufferView auView(plane, 0, plane.size());
                        if (codec.id() == VideoCodec::H264) {
                                keyframe = AvcDecoderConfig::isIdrAnnexB(auView);
                        } else if (codec.id() == VideoCodec::HEVC) {
                                keyframe = HevcDecoderConfig::isIrapAnnexB(auView);
                        } else {
                                keyframe = false;
                        }
                }
                auto cvp = CompressedVideoPayload::Ptr::create(idesc, plane);
                cvp.modify()->setPts(capMts);
                cvp.modify()->setDts(capMts);
                if (keyframe) cvp.modify()->addFlag(MediaPayload::Keyframe);
                videoPayload = cvp;
        } else {
                BufferView planes;
                planes.pushToBack(plane, 0, plane.size());
                auto uvp = UncompressedVideoPayload::Ptr::create(idesc, planes);
                uvp.modify()->setPts(capMts);
                videoPayload = uvp;
        }

        if (!videoPayload.isValid()) {
                promekiWarn("RtpVideoDepacketizerThread: reassembled video frame is invalid");
                return;
        }

        RxVideoFrame bundle;
        bundle.payload = std::move(videoPayload);
        bundle.imageDesc = idesc;
        bundle.rtpTimestamp = frameRtpTimestamp;
        bundle.packetCount = framePacketCount;
        bundle.wallclockNtp = wallclockNtp;
        bundle.captureTime = captureTime;
        bundle.keyframe = keyframe;
        bundle.firstPacketArrival = firstPktArrival;
        bundle.streamFrameIndex = _streamFrameIndex;
        ++_streamFrameIndex;

        if (_ctx.payloadQueue != nullptr) {
                Error perr = _ctx.payloadQueue->pushBlocking(std::move(bundle));
                if (perr.isError() && perr != Error::Cancelled) {
                        promekiWarn("RtpVideoDepacketizerThread: payload queue push failed: %s",
                                    perr.desc().cstr());
                        return;
                }
        }
        if (_ctx.framesReassembled != nullptr) {
                _ctx.framesReassembled->fetchAndAdd(1);
        }
}

PROMEKI_NAMESPACE_END
