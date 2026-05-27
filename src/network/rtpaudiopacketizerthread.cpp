/**
 * @file      rtpaudiopacketizerthread.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/rtpaudiopacketizerthread.h>

#include <cstring>
#include <utility>

#include <promeki/error.h>
#include <promeki/logger.h>
#include <promeki/pcmaudiopayload.h>
#include <promeki/sharedptr.h>

PROMEKI_NAMESPACE_BEGIN

RtpAudioPacketizerThread::RtpAudioPacketizerThread(
        RtpAudioPacketizerContext ctx, const String &name, size_t depth)
    : RtpPacketizerThread(name, depth), _ctx(std::move(ctx)) {}

RtpAudioPacketizerThread::~RtpAudioPacketizerThread() {
        requestStop();
        if (!isCurrentThread()) wait();
}

void RtpAudioPacketizerThread::onStart() {
        // Build the per-thread FIFO once the run loop is live (or
        // when @ref openForTest is called from a unit test) so the
        // FIFO is owned by the right thread.  Reserve enough
        // headroom for one second of source samples plus the
        // configured preroll.
        if (!_ctx.storageDesc.isValid()) return;
        _fifo = AudioBuffer(_ctx.storageDesc);
        const size_t reserveSamples =
                static_cast<size_t>(_ctx.storageDesc.sampleRate()) + _ctx.prerollSamples;
        Error rsvErr = _fifo.reserve(reserveSamples);
        if (rsvErr.isError()) {
                promekiErr("RtpAudioPacketizerThread: failed to reserve FIFO: %s",
                           rsvErr.desc().cstr());
        }
        _drained.resize(_ctx.packetBytes);
}

void RtpAudioPacketizerThread::packetize(const RtpFrameWork &work) {
        if (_ctx.txPacketQueue == nullptr) return;
        if (_ctx.packetSamples == 0 || _ctx.packetBytes == 0) return;

        // Locate this stream's audio essence.  Strand pushes the
        // same Frame to every audio packetizer; each packetizer
        // pulls only the payload at its own stream index.
        auto auds = work.frame.audioPayloads();
        if (_ctx.streamIdx >= auds.size() || !auds[_ctx.streamIdx].isValid()) return;
        auto pcm = sharedPointerCast<PcmAudioPayload>(auds[_ctx.streamIdx]);
        if (!pcm.isValid() || pcm->sampleCount() == 0 || pcm->planeCount() == 0) return;

        // Format conversion happens implicitly inside
        // AudioBuffer::push — the FIFO is in the storage format
        // (typically PCMI_S16BE) and accepts any compatible input.
        // The payload-aware overload also threads the PTS through
        // the FIFO's anchor queue automatically.
        Error pushErr = _fifo.push(*pcm);
        if (pushErr.isError()) {
                promekiErr("RtpAudioPacketizerThread: FIFO push failed: %s",
                           pushErr.desc().cstr());
                return;
        }

        // Drain whole AES67-aligned chunks.  Leftover tail samples
        // remain in the FIFO until the next push fills out a
        // chunk.  Preroll: hold off emitting until the FIFO has
        // accumulated prerollSamples — the TX thread emits silence
        // in the interim.
        if (!_prerollDone) {
                if (_fifo.available() < _ctx.prerollSamples) return;
                _prerollDone = true;
        }
        while (_fifo.available() >= _ctx.packetSamples) {
                auto [popped, popErr] = _fifo.pop(_drained.data(), _ctx.packetSamples);
                if (popErr.isError() || popped < _ctx.packetSamples) break;
                Buffer chunk(_ctx.packetBytes);
                chunk.setSize(_ctx.packetBytes);
                std::memcpy(chunk.data(), _drained.data(), _ctx.packetBytes);
                // Blocking push so the strand backpressures when
                // the AES67 wire cadence falls behind FIFO drain.
                // Cancellation breaks the drain loop — the next
                // packetize call would see _stopRequested and exit.
                Error pushErr2 = _ctx.txPacketQueue->pushBlocking(std::move(chunk));
                if (pushErr2.isError()) {
                        if (pushErr2 != Error::Cancelled) {
                                promekiWarn("RtpAudioPacketizerThread: TX queue pushBlocking "
                                            "failed: %s",
                                            pushErr2.desc().cstr());
                        }
                        break;
                }
        }
}

PROMEKI_NAMESPACE_END
