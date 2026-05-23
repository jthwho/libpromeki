/**
 * @file      rtpaudiotxthread.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/rtpaudiotxthread.h>

#include <algorithm>
#include <utility>

#include <promeki/duration.h>
#include <promeki/error.h>
#include <promeki/logger.h>
#include <promeki/rtppacket.h>
#include <promeki/rtppacketbatch.h>
#include <promeki/timestamp.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

size_t computeQueueDepth(int packetTimeUs) {
        if (packetTimeUs <= 0) return RtpAudioTxThread::MinDepth;
        const int64_t depth = RtpAudioTxThread::HeadroomUs / packetTimeUs;
        return std::max<size_t>(RtpAudioTxThread::MinDepth, static_cast<size_t>(depth));
}

} // namespace

RtpAudioTxThread::RtpAudioTxThread(RtpAudioTxContext ctx, const String &name)
    : RtpTxThread(name), _ctx(std::move(ctx)) {
        _packetQueue.setMaxSize(computeQueueDepth(_ctx.packetTimeUs));
        // Phase D3 — seed the RTP-TS cursor from the per-stream
        // RtpMediaClock anchor.  Default 0 preserves the pre-D3
        // "start at zero" behaviour; a PTP-anchored writer hands in
        // the SMPTE-Epoch-grid projection of the open-time wallclock.
        _rtpTs = _ctx.initialRtpTs;
}

RtpAudioTxThread::~RtpAudioTxThread() {
        requestStop();
        if (!isCurrentThread()) wait();
}

Buffer RtpAudioTxThread::silenceBuffer() {
        if (!_silenceBuilt) {
                PcmSilenceFiller filler(_ctx.storageDesc, _ctx.packetSamples);
                _silenceBuf = filler.payload();
                if (_silenceBuf.size() != _ctx.packetBytes) {
                        promekiWarn("RtpAudioTxThread: silence filler size %zu != packetBytes %zu",
                                    _silenceBuf.size(), _ctx.packetBytes);
                }
                _silenceBuilt = true;
        }
        return _silenceBuf;
}

bool RtpAudioTxThread::emitOne(const Buffer &silenceBuf) {
        if (_ctx.session == nullptr || _ctx.payload == nullptr) return false;

        // Pop one chunk if available; else emit silence.  The
        // packetQueue is sized for ~1 second of headroom, so the
        // packetizer has plenty of room to push ahead of the wire.
        auto popped = _packetQueue.tryPop();
        Buffer payloadChunk;
        bool   isSilence = false;
        if (popped.second().isOk()) {
                payloadChunk = std::move(popped.first());
        } else {
                payloadChunk = silenceBuf;
                isSilence = true;
        }
        if (payloadChunk.size() != _ctx.packetBytes) {
                promekiWarn("RtpAudioTxThread: payload size %zu != packetBytes %zu — skipping",
                            payloadChunk.size(), _ctx.packetBytes);
                _rtpTs += static_cast<uint32_t>(_ctx.packetSamples);
                return false;
        }

        RtpPacketBatch audioBatch;
        audioBatch.packets =
                _ctx.payload->pack(payloadChunk.data(), _ctx.packetBytes);
        if (audioBatch.packets.size() != 1) {
                promekiErr("RtpAudioTxThread: payload pack produced %zu packets, expected 1",
                           audioBatch.packets.size());
                _rtpTs += static_cast<uint32_t>(_ctx.packetSamples);
                return false;
        }
        // PCM has no talkspurt model — marker is always cleared on
        // AES67 packets.
        audioBatch.packets[0].setTimestamp(_rtpTs);
        audioBatch.packets[0].setMarker(false);
        audioBatch.markerOnLast = false;
        audioBatch.clockRate = _ctx.clockRate;

        Error err = _ctx.session->sendPackets(audioBatch);
        if (err.isError()) {
                promekiErr("RtpAudioTxThread: sendPackets failed: %s", err.desc().cstr());
                _rtpTs += static_cast<uint32_t>(_ctx.packetSamples);
                return false;
        }
        _ctx.session->noteRtpEmission(_rtpTs);
        _rtpTs += static_cast<uint32_t>(_ctx.packetSamples);
        if (_ctx.packetsSent != nullptr) _ctx.packetsSent->fetchAndAdd(1);
        const size_t pktSize = audioBatch.packets[0].size();
        if (_ctx.bytesSent != nullptr) {
                _ctx.bytesSent->fetchAndAdd(static_cast<int64_t>(pktSize));
        }
        if (_ctx.senderOctets != nullptr && pktSize > RtpPacket::HeaderSize) {
                _ctx.senderOctets->fetchAndAdd(
                        static_cast<int64_t>(pktSize - RtpPacket::HeaderSize));
        }
        if (isSilence) {
                if (_ctx.silencePacketsEmitted != nullptr) {
                        _ctx.silencePacketsEmitted->fetchAndAdd(1);
                }
                if (_ctx.silenceSamplesEmitted != nullptr) {
                        _ctx.silenceSamplesEmitted->fetchAndAdd(
                                static_cast<int64_t>(_ctx.packetSamples));
                }
        }
        return true;
}

bool RtpAudioTxThread::runOnceForTest() {
        return emitOne(silenceBuffer());
}

void RtpAudioTxThread::run() {
        if (_ctx.packetSamples == 0 || _ctx.packetBytes == 0 || _ctx.packetTimeUs <= 0) {
                promekiErr("RtpAudioTxThread: zero packet shape (samples=%zu bytes=%zu us=%d) — not emitting",
                           _ctx.packetSamples, _ctx.packetBytes, _ctx.packetTimeUs);
                return;
        }

        const Buffer silenceBuf = silenceBuffer();

        Cadence cadence(Duration::fromMicroseconds(_ctx.packetTimeUs));
        cadence.anchor(TimeStamp::now());

        const Duration stallReanchor = Duration::fromMicroseconds(
                static_cast<int64_t>(_ctx.packetTimeUs) * StallReanchorMultiplier);

        while (!isStopRequested()) {
                TimeStamp deadline = cadence.next();
                deadline.sleepUntil();
                if (isStopRequested()) break;

                // Long-stall recovery: if the wallclock has run far
                // ahead of our anchored cadence, reanchor so we
                // don't burst a flood of catch-up emissions.
                const TimeStamp wakeTime = TimeStamp::now();
                if (wakeTime - deadline > stallReanchor) {
                        cadence.reanchor(wakeTime);
                }

                emitOne(silenceBuf);
        }

        // Drain phase — any chunks queued by the packetizer when
        // the cancel latch flipped are pushed to the wire unpaced.
        // Receiver reconstructs wire timing from RTP-TS, not arrival.
        while (true) {
                auto popped = _packetQueue.tryPop();
                if (popped.second().isError()) break;
                if (_ctx.session == nullptr || _ctx.payload == nullptr) continue;
                Buffer payloadChunk = std::move(popped.first());
                if (payloadChunk.size() != _ctx.packetBytes) {
                        _rtpTs += static_cast<uint32_t>(_ctx.packetSamples);
                        continue;
                }
                RtpPacketBatch audioBatch;
                audioBatch.packets =
                        _ctx.payload->pack(payloadChunk.data(), _ctx.packetBytes);
                if (audioBatch.packets.size() != 1) {
                        _rtpTs += static_cast<uint32_t>(_ctx.packetSamples);
                        continue;
                }
                audioBatch.packets[0].setTimestamp(_rtpTs);
                audioBatch.packets[0].setMarker(false);
                audioBatch.markerOnLast = false;
                audioBatch.clockRate = _ctx.clockRate;
                Error err = _ctx.session->sendPackets(audioBatch);
                if (err.isError()) {
                        _rtpTs += static_cast<uint32_t>(_ctx.packetSamples);
                        continue;
                }
                _ctx.session->noteRtpEmission(_rtpTs);
                _rtpTs += static_cast<uint32_t>(_ctx.packetSamples);
                if (_ctx.packetsSent != nullptr) _ctx.packetsSent->fetchAndAdd(1);
                const size_t pktSize = audioBatch.packets[0].size();
                if (_ctx.bytesSent != nullptr) {
                        _ctx.bytesSent->fetchAndAdd(static_cast<int64_t>(pktSize));
                }
                if (_ctx.senderOctets != nullptr && pktSize > RtpPacket::HeaderSize) {
                        _ctx.senderOctets->fetchAndAdd(
                                static_cast<int64_t>(pktSize - RtpPacket::HeaderSize));
                }
        }
}

PROMEKI_NAMESPACE_END
