/**
 * @file      rtpaudiodepacketizerthread.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/rtpaudiodepacketizerthread.h>

#include <cstring>
#include <utility>

#include <promeki/audioformat.h>
#include <promeki/buffer.h>
#include <promeki/error.h>
#include <promeki/logger.h>
#include <promeki/result.h>

PROMEKI_NAMESPACE_BEGIN

RtpAudioDepacketizerThread::RtpAudioDepacketizerThread(
        RtpAudioDepacketizerContext ctx, const String &name,
        uint32_t clockRateHz)
    : RtpDepacketizerThread(name, clockRateHz), _ctx(std::move(ctx)) {}

RtpAudioDepacketizerThread::~RtpAudioDepacketizerThread() {
        requestStop();
        if (!isCurrentThread()) wait();
}

void RtpAudioDepacketizerThread::handlePacket(const RtpPacket &pkt) {
        if (_ctx.active != nullptr && !*_ctx.active) return;

        // SSRC reset epoch.  Audio carries no per-frame reassembly
        // state, so the only thing to reset is the stream anchor —
        // the next packet establishes a fresh one.
        if (_ctx.resetEpoch != nullptr) {
                const uint32_t epoch = _ctx.resetEpoch->value();
                if (epoch != _lastEpoch) {
                        _lastEpoch = epoch;
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

        if (_ctx.readerAudioDesc == nullptr || !_ctx.readerAudioDesc->isValid()) return;
        if (pkt.payloadSize() == 0) return;

        const AudioDesc   &desc = *_ctx.readerAudioDesc;
        const unsigned int ch = desc.channels();
        constexpr size_t   bytesPerSample = 2;
        const size_t       frameBytes = ch * bytesPerSample;
        if (frameBytes == 0) return;
        const size_t samples = pkt.payloadSize() / frameBytes;
        if (samples == 0) return;

        AudioDesc wireDesc(AudioFormat::PCMI_S16BE, desc.sampleRate(), ch);

        // Copy the packet's PCM payload into a fresh Buffer so the
        // chunk lives independently of the RtpPacket's backing
        // storage.  Per-packet PCM is small (≤ AES67 packet time ×
        // sample rate × channels × 2 bytes — at worst ≈ 1500 B for
        // 4 ms @ 48 kHz × 8 ch), so the copy is negligible.
        const size_t pcmSize = pkt.payloadSize();
        Buffer       pcm(pcmSize);
        std::memcpy(pcm.data(), pkt.payload(), pcmSize);
        pcm.setSize(pcmSize);

        RxAudioChunk chunk;
        chunk.pcmBytes = std::move(pcm);
        chunk.wireDesc = wireDesc;
        chunk.rtpTimestamp = pkt.timestamp();
        chunk.sampleCount = samples;

        const bool hasSr = _ctx.hasSr != nullptr && *_ctx.hasSr;
        const bool clockValid = _ctx.streamClock != nullptr &&
                                _ctx.streamClock->isValid();
        if (hasSr && clockValid) {
                chunk.wallclockNtp = _ctx.streamClock->toNtp(pkt.timestamp());
                TimeStamp steady;
                if (_ctx.ntpToSteady) steady = _ctx.ntpToSteady(chunk.wallclockNtp);
                chunk.captureTime = (steady.nanoseconds() != 0)
                                            ? steady
                                            : captureTimeForRtpTs(pkt.timestamp());
        } else {
                chunk.captureTime = captureTimeForRtpTs(pkt.timestamp());
        }
        chunk.firstPacketArrival = pkt.arrivalSteady;
        if (chunk.captureTime.nanoseconds() == 0) {
                chunk.captureTime = TimeStamp::now();
        }

        if (_ctx.payloadQueue != nullptr) {
                Error perr = _ctx.payloadQueue->pushBlocking(std::move(chunk));
                if (perr.isError() && perr != Error::Cancelled) {
                        promekiWarn("RtpAudioDepacketizerThread: payload queue push failed: %s",
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
