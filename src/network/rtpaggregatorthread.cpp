/**
 * @file      rtpaggregatorthread.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/rtpaggregatorthread.h>

#include <cstring>
#include <utility>

#include <promeki/ancpayload.h>
#include <promeki/buffer.h>
#include <promeki/bufferview.h>
#include <promeki/error.h>
#include <promeki/logger.h>
#include <promeki/mediatimestamp.h>
#include <promeki/metadata.h>
#include <promeki/pcmaudiopayload.h>
#include <promeki/result.h>

PROMEKI_NAMESPACE_BEGIN

RtpAggregatorThread::RtpAggregatorThread(RtpAggregatorContext ctx, Mode mode,
                                         const String &name)
    : _ctx(std::move(ctx)), _mode(mode) {
        _stopRequested.setValue(false);
        Thread::setName(name);
}

RtpAggregatorThread::~RtpAggregatorThread() {
        requestStop();
        if (!isCurrentThread()) wait();
}

void RtpAggregatorThread::requestStop() {
        _stopRequested.setValue(true);
        if (_ctx.video.payloadQueue != nullptr) {
                _ctx.video.payloadQueue->cancelWaiters();
        }
        if (_ctx.audio.payloadQueue != nullptr) {
                _ctx.audio.payloadQueue->cancelWaiters();
        }
        if (_ctx.data.payloadQueue != nullptr) {
                _ctx.data.payloadQueue->cancelWaiters();
        }
        if (_ctx.anc.payloadQueue != nullptr) {
                _ctx.anc.payloadQueue->cancelWaiters();
        }
        if (_ctx.readerQueue != nullptr) {
                _ctx.readerQueue->cancelWaiters();
        }
}

void RtpAggregatorThread::run() {
        switch (_mode) {
                case Mode::Video:     runVideoMode(); break;
                case Mode::AudioOnly: runAudioOnlyMode(); break;
                case Mode::DataOnly:  runDataOnlyMode(); break;
                case Mode::AncOnly:   runAncOnlyMode(); break;
        }
}

void RtpAggregatorThread::runOnce(unsigned int popMs) {
        switch (_mode) {
                case Mode::Video:     stepVideoMode(popMs); break;
                case Mode::AudioOnly: stepAudioOnlyMode(popMs); break;
                case Mode::DataOnly:  stepDataOnlyMode(popMs); break;
                case Mode::AncOnly:   stepAncOnlyMode(popMs); break;
        }
}

Duration RtpAggregatorThread::frameDuration() const {
        if (!_ctx.frameRate.isValid()) return Duration();
        const double fps = _ctx.frameRate.toDouble();
        if (fps <= 0.0) return Duration();
        return Duration::fromNanoseconds(
                static_cast<int64_t>(1'000'000'000.0 / fps));
}

unsigned int RtpAggregatorThread::videoPopMs(const Duration &fd) const {
        const bool watchdogEnabled =
                _ctx.videoWatchdogEnabled && fd.nanoseconds() > 0;
        if (!watchdogEnabled) return kPopCapMs;
        const int64_t halfMs = fd.nanoseconds() / 2'000'000;
        unsigned int  popMs =
                halfMs > 0 ? static_cast<unsigned int>(halfMs) : kPopFloorMs;
        if (popMs > kPopCapMs) popMs = kPopCapMs;
        return popMs;
}

bool RtpAggregatorThread::ensureAudioFifo(const AudioDesc &wireDesc) {
        if (_audioFifo.format().isValid()) return true;
        if (!wireDesc.isValid()) return false;
        _audioFifo.setFormat(wireDesc);
        _audioFifo.setInputFormat(wireDesc);
        const size_t headroom =
                static_cast<size_t>(wireDesc.sampleRate() * 2.0f);
        if (headroom > 0) {
                Error err = _audioFifo.reserve(headroom);
                if (err.isError()) {
                        promekiWarn("RtpAggregatorThread: audio FIFO reserve(%zu) failed: %s",
                                    headroom, err.desc().cstr());
                        return false;
                }
        }
        return true;
}

void RtpAggregatorThread::drainAudioIntoFifoBefore(const TimeStamp &windowEnd) {
        if (_ctx.audio.payloadQueue == nullptr) return;
        if (!_ctx.audio.active) return;
        for (;;) {
                if (_stopRequested.value()) break;
                Result<RxAudioChunk> r = _ctx.audio.payloadQueue->tryPop();
                if (r.second().isError()) break;
                const RxAudioChunk &c = r.first();
                if (!ensureAudioFifo(c.wireDesc)) {
                        return;
                }
                const uint32_t pktRtpTs = c.rtpTimestamp;
                const size_t   fifoBefore = _audioFifo.available();
                if (!_audioFifoHasFront) {
                        _audioFifoFrontRtpTs = pktRtpTs;
                        _audioFifoHasFront = true;
                } else {
                        const uint32_t expectedNext =
                                _audioFifoFrontRtpTs +
                                static_cast<uint32_t>(fifoBefore);
                        if (pktRtpTs != expectedNext) {
                                _audioFifo.clear();
                                _audioFifoFrontRtpTs = pktRtpTs;
                        }
                }
                Error perr = _audioFifo.push(c.pcmBytes.data(), c.sampleCount, c.wireDesc);
                if (perr.isError()) {
                        promekiWarn("RtpAggregatorThread: audio FIFO push failed: %s",
                                    perr.desc().cstr());
                        return;
                }
                if (windowEnd.nanoseconds() != 0 && c.captureTime.nanoseconds() != 0 &&
                    (c.captureTime - windowEnd).nanoseconds() >= 0) {
                        break;
                }
        }
}

bool RtpAggregatorThread::drainAncBefore(const TimeStamp &windowEnd,
                                         RxAncFrame &out) {
        bool have = false;
        if (_hasPendingAnc) {
                const bool windowOpen = windowEnd.nanoseconds() == 0;
                const bool pendingUnstamped =
                        _pendingAnc.captureTime.nanoseconds() == 0;
                const bool pendingFitsWindow =
                        windowOpen || pendingUnstamped ||
                        (_pendingAnc.captureTime - windowEnd).nanoseconds() < 0;
                if (pendingFitsWindow) {
                        out = std::move(_pendingAnc);
                        have = true;
                        _hasPendingAnc = false;
                } else {
                        return false;
                }
        }
        if (_ctx.anc.payloadQueue == nullptr) return have;
        if (!_ctx.anc.active) return have;
        for (;;) {
                Result<RxAncFrame> r = _ctx.anc.payloadQueue->tryPop();
                if (r.second().isError()) break;
                if (windowEnd.nanoseconds() != 0 &&
                    r.first().captureTime.nanoseconds() != 0 &&
                    (r.first().captureTime - windowEnd).nanoseconds() >= 0) {
                        _pendingAnc = std::move(r.first());
                        _hasPendingAnc = true;
                        break;
                }
                out = std::move(r.first());
                have = true;
        }
        return have;
}

bool RtpAggregatorThread::drainDataBefore(const TimeStamp &windowEnd,
                                          RxDataMessage &out) {
        bool have = false;
        if (_hasPendingData) {
                const bool windowOpen = windowEnd.nanoseconds() == 0;
                const bool pendingUnstamped =
                        _pendingData.captureTime.nanoseconds() == 0;
                const bool pendingFitsWindow =
                        windowOpen || pendingUnstamped ||
                        (_pendingData.captureTime - windowEnd).nanoseconds() < 0;
                if (pendingFitsWindow) {
                        out = _pendingData;
                        have = true;
                        _hasPendingData = false;
                } else {
                        return false;
                }
        }
        if (_ctx.data.payloadQueue == nullptr) return have;
        if (!_ctx.data.active) return have;
        for (;;) {
                Result<RxDataMessage> r = _ctx.data.payloadQueue->tryPop();
                if (r.second().isError()) break;
                if (windowEnd.nanoseconds() != 0 &&
                    r.first().captureTime.nanoseconds() != 0 &&
                    (r.first().captureTime - windowEnd).nanoseconds() >= 0) {
                        _pendingData = std::move(r.first());
                        _hasPendingData = true;
                        break;
                }
                out = std::move(r.first());
                have = true;
        }
        return have;
}

void RtpAggregatorThread::runVideoMode() {
        if (_ctx.video.payloadQueue == nullptr) return;

        while (!_stopRequested.value()) {
                const Duration     fd = frameDuration();
                const unsigned int popMs = videoPopMs(fd);
                if (!stepVideoMode(popMs)) {
                        // No emission this iteration — either a
                        // pop timeout (watchdog already evaluated
                        // inside @ref stepVideoMode) or cancellation.
                        if (_stopRequested.value()) break;
                }
        }
}

bool RtpAggregatorThread::stepVideoMode(unsigned int popMs) {
        if (_ctx.video.payloadQueue == nullptr) return false;
        const Duration fd = frameDuration();
        const bool     watchdogEnabled =
                _ctx.videoWatchdogEnabled && fd.nanoseconds() > 0;

        Result<RxVideoFrame> r = _ctx.video.payloadQueue->pop(popMs);
        if (r.second() == Error::Cancelled) return false;
        if (r.second() == Error::Ok) {
                _firstVideoSeen = true;
                emitFrameForVideo(std::move(r.first()), fd);
                _inWatchdog = false;
                return true;
        }
        if (r.second() != Error::Timeout) return false;
        if (!_firstVideoSeen || !watchdogEnabled) return false;
        if (_ctx.video.lastPacketArrivalNs == nullptr) return false;
        const int64_t lastArrivalNs = _ctx.video.lastPacketArrivalNs->value();
        if (lastArrivalNs == 0) return false;
        const int64_t nowNs = TimeStamp::now().nanoseconds();
        const int64_t silenceNs = nowNs - lastArrivalNs;
        if (silenceNs < kStallNFrames * fd.nanoseconds()) return false;
        if (_emittedFrameCursor.nanoseconds() != 0 &&
            nowNs < (_emittedFrameCursor.nanoseconds() + fd.nanoseconds())) {
                return false;
        }
        emitWatchdogFrame(fd);
        _inWatchdog = true;
        return true;
}

void RtpAggregatorThread::emitFrameForVideo(RxVideoFrame video,
                                            const Duration &fd) {
        // On resume from a watchdog interval, snap the popped
        // frame's captureTime forward to one frame past the cursor
        // so downstream consumers see monotonic stamps across the
        // stall.
        if (_emittedFrameCursor.nanoseconds() != 0 &&
            video.captureTime.nanoseconds() != 0 &&
            (video.captureTime - _emittedFrameCursor).nanoseconds() <= 0 &&
            fd.nanoseconds() > 0) {
                video.captureTime = _emittedFrameCursor + fd;
        }
        TimeStamp windowEnd;
        if (video.captureTime.nanoseconds() != 0 && fd.nanoseconds() > 0) {
                windowEnd = video.captureTime + fd;
        }
        drainAudioIntoFifoBefore(windowEnd);

        Frame frame = Frame();
        frame.addPayload(video.payload);
        frame.setCaptureTime(MediaTimeStamp(video.captureTime,
                                            ClockDomain::SystemMonotonic));

        if (_ctx.audio.payloadQueue != nullptr && _ctx.audio.active &&
            _ctx.audio.readerAudioDesc != nullptr &&
            _ctx.audio.readerAudioDesc->isValid()) {
                const AudioDesc &audioDesc = *_ctx.audio.readerAudioDesc;
                const size_t needed = _ctx.frameRate.samplesPerFrame(
                        static_cast<int64_t>(audioDesc.sampleRate()),
                        _videoFrameIndex.value());
                const bool hasSr = _ctx.audio.hasSr != nullptr && *_ctx.audio.hasSr;
                const bool clockValid = _ctx.audio.streamClock != nullptr &&
                                        _ctx.audio.streamClock->isValid();
                const bool wallclockReady =
                        video.wallclockNtp.isValid() && hasSr && clockValid &&
                        _audioFifoHasFront;
                if (wallclockReady && needed > 0) {
                        const uint32_t target =
                                _ctx.audio.streamClock->toRtpTs(video.wallclockNtp);
                        const uint32_t rawDelta = target - _audioFifoFrontRtpTs;
                        const int32_t  signedDelta = static_cast<int32_t>(rawDelta);
                        if (signedDelta > 0) {
                                size_t toDrop = static_cast<size_t>(signedDelta);
                                if (toDrop > _audioFifo.available()) {
                                        toDrop = _audioFifo.available();
                                }
                                if (toDrop > 0) {
                                        auto [dropped, derr] = _audioFifo.drop(toDrop);
                                        (void)derr;
                                        _audioFifoFrontRtpTs +=
                                                static_cast<uint32_t>(dropped);
                                }
                        }
                }
                if (needed > 0 && _audioFifo.available() >= needed) {
                        const size_t bufBytes = audioDesc.bufferSize(needed);
                        Buffer       pcm = Buffer(bufBytes);
                        auto [got, err] = _audioFifo.pop(pcm.data(), needed);
                        if (got > 0) {
                                _audioFifoFrontRtpTs += static_cast<uint32_t>(got);
                                size_t usedBytes = audioDesc.bufferSize(got);
                                pcm.setSize(usedBytes);
                                BufferView view(pcm, 0, usedBytes);
                                auto       audioPayload = PcmAudioPayload::Ptr::create(
                                        audioDesc, got, view);
                                MediaTimeStamp audMts = frame.captureTime();
                                audioPayload.modify()->desc().metadata().set(
                                        Metadata::CaptureTime, audMts);
                                audioPayload.modify()->setPts(audMts);
                                frame.addPayload(audioPayload);
                        }
                }
        }
        ++_videoFrameIndex;

        RxDataMessage data;
        if (drainDataBefore(windowEnd, data)) {
                frame.metadata() = data.metadata;
        }
        RxAncFrame anc;
        if (drainAncBefore(windowEnd, anc) && !anc.packets.isEmpty()) {
                auto ap = AncPayload::Ptr::create(anc.desc, std::move(anc.packets));
                frame.addPayload(ap);
        }
        _emittedFrameCursor = video.captureTime;
        if (_ctx.pushFrame) _ctx.pushFrame(std::move(frame));
}

void RtpAggregatorThread::emitWatchdogFrame(const Duration &fd) {
        if (_emittedFrameCursor.nanoseconds() == 0) {
                _emittedFrameCursor = TimeStamp::now();
        } else {
                _emittedFrameCursor += fd;
        }
        const TimeStamp cursorT = _emittedFrameCursor;
        const TimeStamp windowEnd = cursorT + fd;
        drainAudioIntoFifoBefore(windowEnd);

        Frame frame = Frame();
        frame.setCaptureTime(MediaTimeStamp(cursorT,
                                            ClockDomain::SystemMonotonic));

        if (_ctx.audio.payloadQueue != nullptr && _ctx.audio.active &&
            _ctx.audio.readerAudioDesc != nullptr &&
            _ctx.audio.readerAudioDesc->isValid()) {
                const AudioDesc &audioDesc = *_ctx.audio.readerAudioDesc;
                const size_t needed = _ctx.frameRate.samplesPerFrame(
                        static_cast<int64_t>(audioDesc.sampleRate()),
                        _videoFrameIndex.value());
                if (needed > 0 && _audioFifo.available() >= needed) {
                        const size_t bufBytes = audioDesc.bufferSize(needed);
                        Buffer       pcm = Buffer(bufBytes);
                        auto [got, err] = _audioFifo.pop(pcm.data(), needed);
                        if (got > 0) {
                                _audioFifoFrontRtpTs += static_cast<uint32_t>(got);
                                size_t usedBytes = audioDesc.bufferSize(got);
                                pcm.setSize(usedBytes);
                                BufferView view(pcm, 0, usedBytes);
                                auto       audioPayload = PcmAudioPayload::Ptr::create(
                                        audioDesc, got, view);
                                MediaTimeStamp audMts = frame.captureTime();
                                audioPayload.modify()->desc().metadata().set(
                                        Metadata::CaptureTime, audMts);
                                audioPayload.modify()->setPts(audMts);
                                frame.addPayload(audioPayload);
                        }
                }
        }
        ++_videoFrameIndex;

        RxDataMessage data;
        if (drainDataBefore(windowEnd, data)) {
                frame.metadata() = data.metadata;
        }
        RxAncFrame anc;
        if (drainAncBefore(windowEnd, anc) && !anc.packets.isEmpty()) {
                auto ap = AncPayload::Ptr::create(anc.desc, std::move(anc.packets));
                frame.addPayload(ap);
        }
        if (_ctx.pushFrame) _ctx.pushFrame(std::move(frame));
}

void RtpAggregatorThread::runAudioOnlyMode() {
        if (_ctx.audio.payloadQueue == nullptr) return;
        while (!_stopRequested.value()) {
                if (!stepAudioOnlyMode(kPopCapMs)) {
                        if (_stopRequested.value()) break;
                }
        }
}

bool RtpAggregatorThread::stepAudioOnlyMode(unsigned int popMs) {
        if (_ctx.audio.payloadQueue == nullptr) return false;
        Result<RxAudioChunk> r = _ctx.audio.payloadQueue->pop(popMs);
        if (r.second() == Error::Cancelled) return false;
        if (r.second() == Error::Timeout) return false;
        if (r.second() != Error::Ok) return false;
        const RxAudioChunk &c = r.first();
        if (!ensureAudioFifo(c.wireDesc)) return false;
        const bool haveFps =
                _ctx.frameRate.isValid() && _ctx.frameRate.toDouble() > 0.0;
        if (haveFps) {
                emitAudioOnlyAtFrameRate(c);
        } else {
                emitAudioOnlyPerChunk(c);
        }
        return true;
}

void RtpAggregatorThread::emitAudioOnlyAtFrameRate(const RxAudioChunk &c) {
        if (_ctx.audio.readerAudioDesc == nullptr ||
            !_ctx.audio.readerAudioDesc->isValid()) return;
        const AudioDesc &audioDesc = *_ctx.audio.readerAudioDesc;

        const size_t fifoBefore = _audioFifo.available();
        if (!_audioFifoHasFront) {
                _audioFifoFrontRtpTs = c.rtpTimestamp;
                _audioFifoHasFront = true;
        } else {
                const uint32_t expectedNext =
                        _audioFifoFrontRtpTs +
                        static_cast<uint32_t>(fifoBefore);
                if (c.rtpTimestamp != expectedNext) {
                        _audioFifo.clear();
                        _audioFifoFrontRtpTs = c.rtpTimestamp;
                }
        }
        Error perr = _audioFifo.push(c.pcmBytes.data(), c.sampleCount, c.wireDesc);
        if (perr.isError()) {
                promekiWarn("RtpAggregatorThread: audio FIFO push failed: %s",
                            perr.desc().cstr());
                return;
        }
        const double fps = _ctx.frameRate.toDouble();
        const size_t spf =
                static_cast<size_t>(audioDesc.sampleRate() / fps);
        if (spf == 0) return;
        while (_audioFifo.available() >= spf && !_stopRequested.value()) {
                const size_t bufBytes = audioDesc.bufferSize(spf);
                Buffer       pcm = Buffer(bufBytes);
                auto [got, popErr] = _audioFifo.pop(pcm.data(), spf);
                if (popErr.isError() || got == 0) break;
                _audioFifoFrontRtpTs += static_cast<uint32_t>(got);
                const size_t usedBytes = audioDesc.bufferSize(got);
                pcm.setSize(usedBytes);
                BufferView     view(pcm, 0, usedBytes);
                auto           audioPayload =
                        PcmAudioPayload::Ptr::create(audioDesc, got, view);
                MediaTimeStamp capMts(c.captureTime, _ctx.audio.clockDomain);
                audioPayload.modify()->desc().metadata().set(
                        Metadata::CaptureTime, capMts);
                audioPayload.modify()->setPts(capMts);
                Frame frame = Frame();
                frame.addPayload(audioPayload);
                frame.setCaptureTime(capMts);
                if (_ctx.pushFrame) _ctx.pushFrame(std::move(frame));
        }
}

void RtpAggregatorThread::emitAudioOnlyPerChunk(const RxAudioChunk &c) {
        if (_ctx.audio.readerAudioDesc == nullptr ||
            !_ctx.audio.readerAudioDesc->isValid()) return;
        if (c.sampleCount == 0) return;
        const AudioDesc &audioDesc = *_ctx.audio.readerAudioDesc;
        const size_t bufBytes = audioDesc.bufferSize(c.sampleCount);
        Buffer       pcm = Buffer(bufBytes);
        std::memcpy(pcm.data(), c.pcmBytes.data(), bufBytes);
        pcm.setSize(bufBytes);
        BufferView     view(pcm, 0, bufBytes);
        auto           audioPayload =
                PcmAudioPayload::Ptr::create(audioDesc, c.sampleCount, view);
        MediaTimeStamp capMts(c.captureTime, _ctx.audio.clockDomain);
        audioPayload.modify()->desc().metadata().set(Metadata::CaptureTime, capMts);
        audioPayload.modify()->setPts(capMts);
        Frame frame = Frame();
        frame.addPayload(audioPayload);
        frame.setCaptureTime(capMts);
        if (_ctx.pushFrame) _ctx.pushFrame(std::move(frame));
}

void RtpAggregatorThread::runDataOnlyMode() {
        if (_ctx.data.payloadQueue == nullptr) return;
        while (!_stopRequested.value()) {
                if (!stepDataOnlyMode(kPopCapMs)) {
                        if (_stopRequested.value()) break;
                }
        }
}

bool RtpAggregatorThread::stepDataOnlyMode(unsigned int popMs) {
        if (_ctx.data.payloadQueue == nullptr) return false;
        Result<RxDataMessage> r = _ctx.data.payloadQueue->pop(popMs);
        if (r.second() == Error::Cancelled) return false;
        if (r.second() == Error::Timeout) return false;
        if (r.second() != Error::Ok) return false;
        Frame frame = Frame();
        frame.metadata() = r.first().metadata;
        frame.setCaptureTime(MediaTimeStamp(r.first().captureTime,
                                            _ctx.data.clockDomain));
        if (_ctx.pushFrame) _ctx.pushFrame(std::move(frame));
        return true;
}

void RtpAggregatorThread::runAncOnlyMode() {
        if (_ctx.anc.payloadQueue == nullptr) return;
        while (!_stopRequested.value()) {
                if (!stepAncOnlyMode(kPopCapMs)) {
                        if (_stopRequested.value()) break;
                }
        }
}

bool RtpAggregatorThread::stepAncOnlyMode(unsigned int popMs) {
        if (_ctx.anc.payloadQueue == nullptr) return false;
        Result<RxAncFrame> r = _ctx.anc.payloadQueue->pop(popMs);
        if (r.second() == Error::Cancelled) return false;
        if (r.second() == Error::Timeout) return false;
        if (r.second() != Error::Ok) return false;
        RxAncFrame &anc = r.first();
        Frame       frame = Frame();
        frame.setCaptureTime(MediaTimeStamp(anc.captureTime, _ctx.anc.clockDomain));
        if (!anc.packets.isEmpty()) {
                auto ap = AncPayload::Ptr::create(anc.desc, std::move(anc.packets));
                frame.addPayload(ap);
        }
        if (_ctx.pushFrame) _ctx.pushFrame(std::move(frame));
        return true;
}

PROMEKI_NAMESPACE_END
