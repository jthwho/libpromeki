/**
 * @file      rtpmediaio.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdint>
#include <cstring>
#include <functional>

#include <promeki/audiobuffer.h>
#include <promeki/audiodesc.h>
#include <promeki/audiopayload.h>
#include <promeki/base64.h>
#include <promeki/buffer.h>
#include <promeki/cadence.h>
#include <promeki/clockdomain.h>
#include <promeki/compressedvideopayload.h>
#include <promeki/enums.h>
#include <promeki/eui64.h>
#include <promeki/file.h>
#include <promeki/filepath.h>
#include <promeki/frame.h>
#include <promeki/h264bitstream.h>
#include <promeki/hevcbitstream.h>
#include <promeki/imagedesc.h>
#include <promeki/iodevice.h>
#include <promeki/json.h>
#include <promeki/logger.h>
#include <promeki/mediaconfig.h>
#include <promeki/mediadesc.h>
#include <promeki/mediaiocommand.h>
#include <promeki/mediaioportgroup.h>
#include <promeki/mediaiorequest.h>
#include <promeki/mediatimestamp.h>
#include <promeki/metadata.h>
#include <promeki/pcmaudiopayload.h>
#include <promeki/pcmsilencefiller.h>
#include <promeki/pixelformat.h>
#include <promeki/rtpmediaio.h>
#include <promeki/rtppacketbatch.h>
#include <promeki/rtppayload.h>
#include <promeki/rtpsession.h>
#include <promeki/rtpstreamclock.h>
#include <promeki/mutex.h>
#include <promeki/sdpsession.h>
#include <promeki/system.h>
#include <promeki/thread.h>
#include <promeki/waitcondition.h>
#include <unistd.h>
#include <promeki/udpsocket.h>
#include <promeki/udpsockettransport.h>
#include <promeki/uncompressedvideopayload.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_DEBUG(RtpMediaIO)
PROMEKI_REGISTER_MEDIAIO_FACTORY(RtpFactory)

// ----------------------------------------------------------------------------
// Per-stream packetizer + TX threads.
//
// Phase 2 of the RTP TX restructure splits the strand-driven send
// path into a clean two-stage per-stream pipeline:
//
//   strand → PayloadQueue<RtpFrameWork> → packetizer thread
//          → PacketQueue (typed per stream) → TX thread → wire
//
// The strand never blocks on wire I/O.  It runs the pacing gate,
// then pushes the same Frame (CoW handle) onto every active
// stream's PayloadQueue and returns.  Each per-stream packetizer
// drains its PayloadQueue, extracts its own essence
// (frame.videoPayloads / frame.audioPayloads / frame.metadata),
// builds payload-bytes-only RtpPackets, and pushes them onto its
// PacketQueue.  The TX thread fills the full RTP header (version
// / seq / SSRC / PT / marker / RTP-TS) at emission time and
// dispatches to the wire.
//
// What's local to which thread:
//   - Packetizer: parameter-set cache (video), AudioBuffer FIFO
//                 (audio), JSON string buffer (data).
//   - TX:         per-stream RTP-TS counter (audio cursor /
//                 video frame-index resolution), Cadence pacer
//                 (audio, future userspace video), pacing-mode
//                 dispatch.
// ----------------------------------------------------------------------------

class RtpMediaIO::VideoTxThread : public RtpTxThread {
        public:
                /// Max RtpPacketBatch backlog the packetizer can stage
                /// ahead of the TX thread.  Devplan §"Per-stream queue
                /// depths and types" calls for 2-3 frames worth — keep
                /// it tight so backpressure flows back up the strand
                /// instead of the queue absorbing rate mismatches.
                static constexpr size_t PacketQueueDepth = 3;

                VideoTxThread(RtpMediaIO *owner)
                    : RtpTxThread(String("RtpVidTx")), _owner(owner) {
                        _packetQueue.setMaxSize(PacketQueueDepth);
                }

                Queue<RtpPacketBatch> &packetQueue() { return _packetQueue; }

        protected:
                void onShutdown() override { _packetQueue.cancelWaiters(); }

                void run() override {
                        if (_owner->_videos.isEmpty()) return;
                        VideoStream     &vs = _owner->_videos[0];
                        const FrameRate &rate = _owner->_frameRate;
                        while (!isStopRequested()) {
                                auto r = _packetQueue.pop();
                                if (r.second().isError()) break;
                                RtpPacketBatch batch = std::move(r.first());
                                if (batch.packets.isEmpty() || vs.session == nullptr) continue;

                                // RTP timestamp via cumulativeTicks() —
                                // exact 64-bit rational math against the
                                // stream's clock rate, so integer + NTSC
                                // fractional rates both stay drift-free.
                                const uint32_t ts = static_cast<uint32_t>(
                                        rate.cumulativeTicks(batch.clockRate, batch.frameIndex.value()));

                                // Stamp ts + marker on every packet
                                // before handoff.  The session is
                                // responsible only for the transport-
                                // owned fields (version / seq / SSRC /
                                // PT) plus the @c rateCapBps update.
                                const size_t nPackets = batch.packets.size();
                                for (size_t i = 0; i < nPackets; i++) {
                                        const bool isLast = (i + 1 == nPackets);
                                        batch.packets[i].setTimestamp(ts);
                                        batch.packets[i].setMarker(isLast && batch.markerOnLast);
                                }

                                Error err;
                                const bool wantSpread = rate.isValid() &&
                                                        _owner->_pacingMode.value() != RtpPacingMode::None.value();
                                if (wantSpread) {
                                        // Userspace pacing: spread the
                                        // batch's packets across one
                                        // frame interval via @c Cadence.
                                        // Replaces @c sendPacketsPaced
                                        // (which is gone in Phase 3) so
                                        // pacing strategy stays local
                                        // to the TX thread instead of
                                        // hiding inside @c RtpSession.
                                        // The deadline scheme is
                                        // anchored at this frame's
                                        // start — drift across frames
                                        // is bounded by the strand's
                                        // own per-frame cadence and
                                        // does not accumulate.
                                        const Duration interval = rate.frameDuration();
                                        const Duration perPacket = nPackets > 1
                                                ? Duration::fromNanoseconds(interval.nanoseconds() /
                                                                            static_cast<int64_t>(nPackets))
                                                : interval;
                                        Cadence pacer(perPacket);
                                        pacer.anchor(TimeStamp::now());
                                        err = Error::Ok;
                                        // Per-packet pacing: build a
                                        // single-packet sub-batch each
                                        // tick.  rateCapBps applies
                                        // once on the first sub-batch;
                                        // subsequent ones leave the
                                        // cap alone so the kernel
                                        // doesn't see a setPacingRate
                                        // storm.
                                        for (size_t i = 0; i < nPackets; i++) {
                                                pacer.next().sleepUntil();
                                                RtpPacketBatch one;
                                                one.packets.pushToBack(batch.packets[i]);
                                                one.frameIndex = batch.frameIndex;
                                                one.clockRate = batch.clockRate;
                                                one.markerOnLast = batch.markerOnLast;
                                                if (i == 0) one.rateCapBps = batch.rateCapBps;
                                                Error e = vs.session->sendPackets(one);
                                                if (e.isError()) {
                                                        err = e;
                                                        break;
                                                }
                                        }
                                } else {
                                        err = vs.session->sendPackets(batch);
                                }
                                if (err.isError()) {
                                        promekiErr("RtpMediaIO::VideoTxThread: sendPackets failed: %s",
                                                   err.desc().cstr());
                                        continue;
                                }

                                vs.session->noteRtpEmission(ts);
                                vs.packetsSent.fetchAndAdd(static_cast<int64_t>(batch.packets.size()));
                                for (size_t i = 0; i < batch.packets.size(); i++) {
                                        const size_t pktSize = batch.packets[i].size();
                                        vs.bytesSent.fetchAndAdd(static_cast<int64_t>(pktSize));
                                        if (pktSize > RtpPacket::HeaderSize) {
                                                vs.senderOctets.fetchAndAdd(
                                                        static_cast<int64_t>(pktSize - RtpPacket::HeaderSize));
                                        }
                                }
                                vs.txSendDuration.addSample(
                                        (TimeStamp::now() - batch.enqueuedAt).microseconds());
                        }
                }

        private:
                RtpMediaIO          *_owner;
                Queue<RtpPacketBatch> _packetQueue;
};

class RtpMediaIO::VideoPacketizerThread : public RtpPacketizerThread {
        public:
                VideoPacketizerThread(RtpMediaIO *owner)
                    : RtpPacketizerThread(String("RtpVidPkt")), _owner(owner) {}

        protected:
                void packetize(const RtpFrameWork &work) override {
                        if (_owner->_videos.isEmpty()) return;
                        VideoStream &vs = _owner->_videos[0];
                        if (!vs.active || vs.payload == nullptr) return;
                        auto vids = work.frame.videoPayloads();
                        if (vids.isEmpty() || !vids[0].isValid()) return;
                        const VideoPayload &payload = *vids[0];

                        // Out-of-band parameter-set cache update via
                        // Metadata::CodecParameterSets — feeds
                        // injectParameterSets so the per-stream cache
                        // is current before the access unit is
                        // packetised.  Safe on every frame; the
                        // metadata blob is parameter-set only and
                        // never produces a healed access unit.
                        const String paramSets =
                                work.frame.metadata().getAs<String>(Metadata::CodecParameterSets, String());
                        if (!paramSets.isEmpty()) {
                                Buffer ignored;
                                _owner->injectParameterSets(
                                        reinterpret_cast<const uint8_t *>(paramSets.cstr()),
                                        paramSets.size(), ignored);
                        }

                        // Wire-format CSC for raw video when the
                        // input pixel format does not match RFC 4175
                        // expectations.  Compressed payloads are
                        // shipped verbatim.
                        const VideoPayload           *src = &payload;
                        UncompressedVideoPayload::Ptr converted;
                        if (_owner->_videoWirePixelFormat.isValid() && !payload.isCompressed() &&
                            payload.desc().pixelFormat().id() != _owner->_videoWirePixelFormat.id()) {
                                const auto *uvp = payload.as<UncompressedVideoPayload>();
                                if (uvp == nullptr) return;
                                converted = uvp->convert(_owner->_videoWirePixelFormat, Metadata());
                                if (!converted.isValid()) return;
                                src = converted.ptr();
                        }
                        if (src->planeCount() == 0) return;
                        auto plane0 = src->plane(0);
                        if (!plane0.isValid() || plane0.size() == 0) return;

                        // Self-healing parameter-set injection on the
                        // bitstream itself — fires only on H.264 /
                        // HEVC compressed payloads.
                        Buffer         healed;
                        const uint8_t *bsData = static_cast<const uint8_t *>(plane0.data());
                        size_t         bsSize = plane0.size();
                        if (payload.isCompressed()) {
                                _owner->injectParameterSets(bsData, bsSize, healed);
                                if (healed.isValid() && healed.size() > 0) {
                                        bsData = static_cast<const uint8_t *>(healed.data());
                                        bsSize = healed.size();
                                }
                        }

                        auto packets = vs.payload->pack(bsData, bsSize);
                        if (packets.isEmpty()) return;

                        RtpPacketBatch batch;
                        batch.packets = std::move(packets);
                        batch.frameIndex = work.frameIndex;
                        batch.clockRate = vs.clockRate;
                        batch.markerOnLast = true;
                        batch.enqueuedAt = TimeStamp::now();

                        // VBR compressed: per-frame rate cap recomputed
                        // from the actual packed byte count.  The TX
                        // thread reads this and calls setPacingRate
                        // before dispatch.
                        if (_owner->_pacingMode.value() == RtpPacingMode::KernelFq.value() &&
                            _owner->_frameRate.isValid() && payload.isCompressed()) {
                                size_t frameBytes = 0;
                                for (size_t i = 0; i < batch.packets.size(); i++) {
                                        frameBytes += batch.packets[i].size();
                                }
                                if (frameBytes > 0) {
                                        const double fps = _owner->_frameRate.toDouble();
                                        batch.rateCapBps = static_cast<uint64_t>(
                                                static_cast<double>(frameBytes * 8u) * fps);
                                }
                        }

                        // Diagnostic frame-interval histogram, owned
                        // by the packetizer (which sees one push per
                        // frame).  The TX thread tracks send duration
                        // separately.
                        const TimeStamp now = TimeStamp::now();
                        if (vs.txHasLastSend) {
                                const Duration delta = now - vs.txLastSendStart;
                                vs.txFrameInterval.addSample(delta.microseconds());
                        }
                        vs.txLastSendStart = now;
                        vs.txHasLastSend = true;

                        // Blocking push so the packetizer (and through
                        // the strand-bounded PayloadQueue, the upstream
                        // pipeline) backpressures when the TX thread
                        // falls behind the wire.  Cancellation during
                        // shutdown surfaces as Error::Cancelled — the
                        // run loop's stop check will pick it up on the
                        // next iteration.
                        Error pushErr = _tx->packetQueue().pushBlocking(std::move(batch));
                        if (pushErr.isError() && pushErr != Error::Cancelled) {
                                promekiWarn("RtpMediaIO::VideoPacketizerThread: TX queue "
                                            "pushBlocking failed: %s",
                                            pushErr.desc().cstr());
                        }
                }

        public:
                void setTx(VideoTxThread *tx) { _tx = tx; }

        private:
                RtpMediaIO    *_owner;
                VideoTxThread *_tx = nullptr;
};

class RtpMediaIO::DataTxThread : public RtpTxThread {
        public:
                /// Devplan §"Per-stream queue depths and types" calls
                /// for 8 batches of metadata headroom.  Same backpressure
                /// rationale as VideoTxThread::PacketQueueDepth.
                static constexpr size_t PacketQueueDepth = 8;

                DataTxThread(RtpMediaIO *owner)
                    : RtpTxThread(String("RtpDatTx")), _owner(owner) {
                        _packetQueue.setMaxSize(PacketQueueDepth);
                }

                Queue<RtpPacketBatch> &packetQueue() { return _packetQueue; }

        protected:
                void onShutdown() override { _packetQueue.cancelWaiters(); }

                void run() override {
                        if (_owner->_datas.isEmpty()) return;
                        DataStream &ds = _owner->_datas[0];
                        while (!isStopRequested()) {
                                auto r = _packetQueue.pop();
                                if (r.second().isError()) break;
                                RtpPacketBatch batch = std::move(r.first());
                                if (batch.packets.isEmpty() || ds.session == nullptr) continue;
                                const double fps = _owner->_frameRate.isValid()
                                                           ? _owner->_frameRate.toDouble()
                                                           : 30.0;
                                const uint32_t ts = static_cast<uint32_t>(
                                        static_cast<double>(batch.frameIndex.value()) *
                                        static_cast<double>(batch.clockRate) / fps);
                                // Stamp ts + marker on every packet
                                // before the session dispatches.
                                for (size_t i = 0; i < batch.packets.size(); i++) {
                                        const bool isLast = (i + 1 == batch.packets.size());
                                        batch.packets[i].setTimestamp(ts);
                                        batch.packets[i].setMarker(isLast && batch.markerOnLast);
                                }
                                Error err = ds.session->sendPackets(batch);
                                if (err.isError()) {
                                        promekiErr("RtpMediaIO::DataTxThread: sendPackets failed: %s",
                                                   err.desc().cstr());
                                        continue;
                                }
                                ds.session->noteRtpEmission(ts);
                                ds.packetsSent.fetchAndAdd(static_cast<int64_t>(batch.packets.size()));
                                for (size_t i = 0; i < batch.packets.size(); i++) {
                                        const size_t pktSize = batch.packets[i].size();
                                        ds.bytesSent.fetchAndAdd(static_cast<int64_t>(pktSize));
                                        if (pktSize > RtpPacket::HeaderSize) {
                                                ds.senderOctets.fetchAndAdd(
                                                        static_cast<int64_t>(pktSize - RtpPacket::HeaderSize));
                                        }
                                }
                        }
                }

        private:
                RtpMediaIO            *_owner;
                Queue<RtpPacketBatch>  _packetQueue;
};

class RtpMediaIO::DataPacketizerThread : public RtpPacketizerThread {
        public:
                DataPacketizerThread(RtpMediaIO *owner)
                    : RtpPacketizerThread(String("RtpDatPkt")), _owner(owner) {}

                void setTx(DataTxThread *tx) { _tx = tx; }

        protected:
                void packetize(const RtpFrameWork &work) override {
                        if (_owner->_datas.isEmpty()) return;
                        DataStream &ds = _owner->_datas[0];
                        if (!ds.active || ds.payload == nullptr) return;
                        // Only the JsonMetadata format is wired up; the
                        // ST 2110-40 branch is rejected at configure time.
                        JsonObject obj = work.frame.metadata().toJson();
                        const String json = obj.toString(0);  // compact
                        if (json.isEmpty()) return;
                        auto packets = ds.payload->pack(json.cstr(), json.size());
                        if (packets.isEmpty()) return;
                        RtpPacketBatch batch;
                        batch.packets = std::move(packets);
                        batch.frameIndex = work.frameIndex;
                        batch.clockRate = ds.clockRate;
                        batch.markerOnLast = true;
                        batch.enqueuedAt = TimeStamp::now();
                        // Blocking push: same backpressure contract as
                        // VideoPacketizerThread.
                        Error pushErr = _tx->packetQueue().pushBlocking(std::move(batch));
                        if (pushErr.isError() && pushErr != Error::Cancelled) {
                                promekiWarn("RtpMediaIO::DataPacketizerThread: TX queue "
                                            "pushBlocking failed: %s",
                                            pushErr.desc().cstr());
                        }
                }

        private:
                RtpMediaIO    *_owner;
                DataTxThread *_tx = nullptr;
};

// ---------------------------------------------------------------------------
// AudioTxThread — cadence-paced AES67 emitter with silence-fill.
//
// Owns the per-stream RTP-TS cursor: every tick of the configured
// AES67 cadence advances the cursor by `packetSamples` regardless
// of whether real content was available, so the wire RTP-TS series
// is contiguous even when the source stalls.  When the inbound
// PacketQueue is empty at a tick, the TX thread emits a packet
// over the @ref PcmSilenceFiller-supplied silence buffer instead.
// Receivers see a continuous wire timeline with no discontinuity
// gap on the source side — silence packets count as real
// emissions for noteRtpEmission / hasEmissionRecord, so SRs keep
// flowing for an audio session that's currently producing only
// silence.
// ---------------------------------------------------------------------------
class RtpMediaIO::AudioTxThread : public RtpTxThread {
        public:
                /// Default headroom in microseconds for the audio
                /// packetizer→TX queue.  Devplan §"Per-stream queue
                /// depths and types" calls for ≈ 1 second of packets,
                /// matching the AudioPacketizerThread FIFO reserve.
                /// The actual depth in chunks = HeadroomUs /
                /// packetTimeUs (rounded up, with a sane floor).
                static constexpr int64_t HeadroomUs = 1'000'000;

                AudioTxThread(RtpMediaIO *owner, size_t streamIdx)
                    : RtpTxThread(String("RtpAudTx") + String("/") + String::number(streamIdx)),
                      _owner(owner), _streamIdx(streamIdx) {
                        // Bound the queue from the AudioStream's
                        // configured cadence.  configureAudioStream
                        // runs before this constructor at the
                        // executeCmd(Open) call site, so packetTimeUs
                        // is already populated.  A zero / negative
                        // value is a config bug — fall back to a
                        // small safe depth so we still apply
                        // backpressure rather than going unbounded.
                        const int packetTimeUs = owner->_audios[streamIdx].packetTimeUs;
                        const size_t depth = packetTimeUs > 0
                                ? std::max<size_t>(8, static_cast<size_t>(HeadroomUs / packetTimeUs))
                                : 8;
                        _packetQueue.setMaxSize(depth);
                }

                Queue<Buffer> &packetQueue() { return _packetQueue; }

        protected:
                void onShutdown() override { _packetQueue.cancelWaiters(); }

                void run() override {
                        AudioStream &as = _owner->_audios[_streamIdx];
                        const size_t packetSamples = as.packetSamples;
                        const size_t packetBytes = as.packetBytes;
                        const int    packetTimeUs = as.packetTimeUs;
                        if (packetSamples == 0 || packetBytes == 0 || packetTimeUs <= 0) {
                                promekiErr("RtpMediaIO::AudioTxThread: stream %zu has zero packet "
                                           "shape (samples=%zu bytes=%zu us=%d) — not emitting",
                                           _streamIdx, packetSamples, packetBytes, packetTimeUs);
                                return;
                        }

                        // Build the silence buffer once.  Identical
                        // bytes for every silence emission — the
                        // filler caches the buffer internally so
                        // every emit is a refcount bump.
                        PcmSilenceFiller silenceFiller(as.storageDesc, packetSamples);
                        if (silenceFiller.size() != packetBytes) {
                                promekiWarn("RtpMediaIO::AudioTxThread: silence filler size "
                                            "%zu != packetBytes %zu",
                                            silenceFiller.size(), packetBytes);
                        }

                        Cadence cadence(Duration::fromMicroseconds(packetTimeUs));
                        cadence.anchor(TimeStamp::now());

                        // Per-stream RTP-TS cursor — advances by
                        // exactly packetSamples every tick (real
                        // content or silence).  Wire timeline stays
                        // contiguous regardless of source stalls.
                        uint32_t rtpTs = 0;
                        // Long-stall threshold for cadence reanchor:
                        // if we wake to find the deadline more than
                        // this many intervals in the past, reanchor
                        // rather than burst-emit to catch up.
                        const Duration stallReanchor = Duration::fromMicroseconds(
                                static_cast<int64_t>(packetTimeUs) * 16);

                        while (!isStopRequested()) {
                                TimeStamp deadline = cadence.next();
                                deadline.sleepUntil();
                                if (isStopRequested()) break;

                                AudioStream &asNow = _owner->_audios[_streamIdx];
                                if (!asNow.active || asNow.session == nullptr || asNow.payload == nullptr) {
                                        continue;
                                }

                                // Long-stall recovery: if the
                                // wallclock has run far ahead of
                                // our anchored cadence (process
                                // suspended, scheduler hiccup,
                                // …), reanchor so we don't burst
                                // a flood of catch-up emissions.
                                const TimeStamp wakeTime = TimeStamp::now();
                                if (wakeTime - deadline > stallReanchor) {
                                        cadence.reanchor(wakeTime);
                                }

                                // Pop one chunk if available; else
                                // emit silence.  The PacketQueue is
                                // 1 second deep so the packetizer
                                // has plenty of headroom to push
                                // ahead of the wire.
                                auto popped = _packetQueue.tryPop();
                                Buffer payloadChunk;
                                bool   isSilence = false;
                                if (popped.second().isOk()) {
                                        payloadChunk = std::move(popped.first());
                                } else {
                                        payloadChunk = silenceFiller.payload();
                                        isSilence = true;
                                }
                                if (payloadChunk.size() != packetBytes) {
                                        promekiWarn("RtpMediaIO::AudioTxThread: payload size %zu "
                                                    "!= packetBytes %zu — skipping",
                                                    payloadChunk.size(), packetBytes);
                                        rtpTs += static_cast<uint32_t>(packetSamples);
                                        continue;
                                }

                                RtpPacketBatch audioBatch;
                                audioBatch.packets =
                                        asNow.payload->pack(payloadChunk.data(), packetBytes);
                                if (audioBatch.packets.size() != 1) {
                                        promekiErr("RtpMediaIO::AudioTxThread: payload pack produced "
                                                   "%zu packets, expected 1",
                                                   audioBatch.packets.size());
                                        rtpTs += static_cast<uint32_t>(packetSamples);
                                        continue;
                                }
                                // PCM has no talkspurt model — marker
                                // is always cleared on AES67 packets.
                                audioBatch.packets[0].setTimestamp(rtpTs);
                                audioBatch.packets[0].setMarker(false);
                                audioBatch.markerOnLast = false;
                                audioBatch.clockRate = asNow.clockRate;

                                Error err = asNow.session->sendPackets(audioBatch);
                                if (err.isError()) {
                                        promekiErr("RtpMediaIO::AudioTxThread: sendPackets failed: %s",
                                                   err.desc().cstr());
                                        rtpTs += static_cast<uint32_t>(packetSamples);
                                        continue;
                                }
                                asNow.session->noteRtpEmission(rtpTs);
                                rtpTs += static_cast<uint32_t>(packetSamples);
                                asNow.packetsSent.fetchAndAdd(1);
                                const size_t pktSize = audioBatch.packets[0].size();
                                asNow.bytesSent.fetchAndAdd(static_cast<int64_t>(pktSize));
                                if (pktSize > RtpPacket::HeaderSize) {
                                        asNow.senderOctets.fetchAndAdd(
                                                static_cast<int64_t>(pktSize - RtpPacket::HeaderSize));
                                }
                                if (isSilence) {
                                        asNow.silencePacketsEmitted.fetchAndAdd(1);
                                        asNow.silenceSamplesEmitted.fetchAndAdd(
                                                static_cast<int64_t>(packetSamples));
                                }
                        }
                }

        private:
                RtpMediaIO   *_owner;
                size_t        _streamIdx;
                Queue<Buffer> _packetQueue;
};

class RtpMediaIO::AudioPacketizerThread : public RtpPacketizerThread {
        public:
                AudioPacketizerThread(RtpMediaIO *owner, size_t streamIdx)
                    : RtpPacketizerThread(String("RtpAudPkt") + String("/") + String::number(streamIdx)),
                      _owner(owner), _streamIdx(streamIdx) {}

                void setTx(AudioTxThread *tx) { _tx = tx; }

        protected:
                void onStart() override {
                        // Build the per-thread FIFO once the run
                        // loop is live, so the AudioStream's storage
                        // descriptor and reserve sizing are visible
                        // from the right thread.  Reserve enough
                        // headroom for one second of source samples
                        // plus the configured preroll.
                        AudioStream &as = _owner->_audios[_streamIdx];
                        if (!as.storageDesc.isValid()) return;
                        _fifo = AudioBuffer(as.storageDesc);
                        const size_t reserveSamples = static_cast<size_t>(as.storageDesc.sampleRate()) +
                                                      as.prerollSamples;
                        Error rsvErr = _fifo.reserve(reserveSamples);
                        if (rsvErr.isError()) {
                                promekiErr("RtpMediaIO::AudioPacketizerThread: failed to reserve FIFO: %s",
                                           rsvErr.desc().cstr());
                        }
                        _drained.resize(as.packetBytes);
                }

                void packetize(const RtpFrameWork &work) override {
                        AudioStream &as = _owner->_audios[_streamIdx];
                        if (!as.active) return;

                        // Locate this stream's audio essence.  Strand
                        // pushes the same Frame to every audio
                        // packetizer; each packetizer pulls only the
                        // payload at its own stream index.
                        auto auds = work.frame.audioPayloads();
                        if (_streamIdx >= auds.size() || !auds[_streamIdx].isValid()) return;
                        auto pcm = sharedPointerCast<PcmAudioPayload>(auds[_streamIdx]);
                        if (!pcm.isValid() || pcm->sampleCount() == 0 || pcm->planeCount() == 0) return;

                        // Format conversion happens implicitly inside
                        // AudioBuffer::push — the FIFO is in the
                        // wire format (PCMI_S16BE) and accepts any
                        // compatible input.
                        auto planeView = pcm->plane(0);
                        if (planeView.size() == 0) return;
                        Error pushErr = _fifo.push(planeView.data(), pcm->sampleCount(), pcm->desc());
                        if (pushErr.isError()) {
                                promekiErr("RtpMediaIO::AudioPacketizerThread: FIFO push failed: %s",
                                           pushErr.desc().cstr());
                                return;
                        }

                        // Drain whole AES67-aligned chunks.  Leftover
                        // tail samples remain in the FIFO until the
                        // next push fills out a chunk.  Preroll: hold
                        // off emitting until the FIFO has accumulated
                        // prerollSamples worth — the TX thread will
                        // emit silence in the interim.
                        const size_t packetSamples = as.packetSamples;
                        const size_t packetBytes = as.packetBytes;
                        if (packetSamples == 0 || packetBytes == 0) return;
                        if (!_prerollDone) {
                                if (_fifo.available() < as.prerollSamples) return;
                                _prerollDone = true;
                        }
                        while (_fifo.available() >= packetSamples) {
                                auto [popped, popErr] = _fifo.pop(_drained.data(), packetSamples);
                                if (popErr.isError() || popped < packetSamples) break;
                                Buffer chunk(packetBytes);
                                chunk.setSize(packetBytes);
                                std::memcpy(chunk.data(), _drained.data(), packetBytes);
                                // Blocking push so the strand
                                // backpressures when the AES67 wire
                                // cadence falls behind FIFO drain.
                                // Cancellation (shutdown) breaks the
                                // drain loop — the next packetize call
                                // would see _stopRequested and exit.
                                Error pushErr = _tx->packetQueue().pushBlocking(std::move(chunk));
                                if (pushErr.isError()) {
                                        if (pushErr != Error::Cancelled) {
                                                promekiWarn("RtpMediaIO::AudioPacketizerThread: "
                                                            "TX queue pushBlocking failed: %s",
                                                            pushErr.desc().cstr());
                                        }
                                        break;
                                }
                        }
                }

                void onStop() override { _fifo.clear(); }

        private:
                RtpMediaIO     *_owner;
                size_t          _streamIdx;
                AudioTxThread  *_tx = nullptr;
                AudioBuffer     _fifo;
                List<uint8_t>   _drained;
                bool            _prerollDone = false;
};

// ----------------------------------------------------------------------------
// RtcpScheduler — periodic RTCP Sender Report driver.
//
// One thread per RtpMediaIO.  Wakes every interval and asks each
// active stream's RtpSession to emit an SR + SDES compound packet on
// its socket (RTP and RTCP share one port via rtcp-mux).  The SR
// carries the (NTP, RTP-timestamp) pair the receiver needs to
// correlate this stream's RTP clock with wall-clock time, and the
// SDES carries the CNAME so receivers can identify which streams come
// from the same sender for sync purposes.
//
// Best-effort: send failures are logged once per stream and the
// scheduler keeps running.  An RTCP send that fails does not affect
// RTP transport — it just delays sync convergence at the receiver
// until the next successful SR.
// ----------------------------------------------------------------------------
class RtpMediaIO::RtcpScheduler : public Thread {
        public:
                RtcpScheduler(RtpMediaIO *owner, int intervalMs)
                    : _owner(owner), _intervalMs(intervalMs <= 0 ? 5000 : intervalMs) {
                        _stopRequested.setValue(false);
                        Thread::setName("rtp-rtcp");
                }

                ~RtcpScheduler() override {
                        requestStop();
                        if (!isCurrentThread()) wait();
                }

                void requestStop() {
                        // Set the flag and wake the waiter — without
                        // the wake, requestStop has to wait up to one
                        // full RTCP interval (5 s by default) before
                        // the scheduler thread re-evaluates the flag.
                        _stopRequested.setValue(true);
                        Mutex::Locker lock(_mutex);
                        _cv.wakeAll();
                }

        protected:
                void run() override {
                        const unsigned int intervalMs = static_cast<unsigned int>(_intervalMs);
                        // Two-phase schedule.  Phase 1: poll briefly
                        // for first emissions on each stream so the
                        // FIRST SR for each stream goes out as soon
                        // as that stream's first packet has flown.
                        // Phase 2 (steady state): one emission cycle
                        // per @c _intervalMs.
                        //
                        // Receivers can't compute lip-sync until they
                        // have an SR for both streams.  Without phase
                        // 1, the very first SR for each stream would
                        // wait until @c _intervalMs into the session,
                        // and ffplay (and other sync-aware receivers)
                        // would play whatever audio arrived in the
                        // gap unsynced.  Phase 1's tight poll closes
                        // that window to a few hundred ms.
                        const unsigned int kStartupPollMs = 50;
                        const auto         startupDeadline =
                                std::chrono::steady_clock::now() + std::chrono::milliseconds(intervalMs);
                        while (!_stopRequested.value() && std::chrono::steady_clock::now() < startupDeadline) {
                                emitOnce();
                                if (allStreamsHaveEmitted()) break;
                                cvSleep(kStartupPollMs);
                        }
                        // Phase 2: steady-state cadence.  cvSleep
                        // returns early on requestStop, so close
                        // doesn't block waiting for an interval-long
                        // sleep_until to expire.
                        while (!_stopRequested.value()) {
                                cvSleep(intervalMs);
                                if (_stopRequested.value()) break;
                                emitOnce();
                        }
                }

        private:
                void emitOnce() {
                        for (VideoStream &vs : _owner->_videos) {
                                emitForStream(vs);
                        }
                        for (AudioStream &as : _owner->_audios) {
                                emitForStream(as);
                        }
                        for (DataStream &ds : _owner->_datas) {
                                emitForStream(ds);
                        }
                }

                static void emitForStream(WriterStream &s) {
                        if (!s.active || s.session == nullptr) return;
                        // Skip streams that have not emitted any RTP
                        // packet yet — an SR with garbage (NTP, RTP)
                        // fields gives receivers a worse starting
                        // point than waiting for a real one.
                        if (!s.session->hasEmissionRecord()) return;
                        // packetCount / octetCount are mutated by the
                        // per-stream TX thread; @c Atomic<int64_t>
                        // gives an aligned acquire-load here so the
                        // scheduler reads a coherent snapshot rather
                        // than a partially-updated half.
                        const uint32_t pkts = static_cast<uint32_t>(s.packetsSent.value() & 0xFFFFFFFFu);
                        const uint32_t octs = static_cast<uint32_t>(s.senderOctets.value() & 0xFFFFFFFFu);
                        Error          err = s.session->emitRtcpSr(pkts, octs);
                        if (err.isError()) {
                                promekiWarn("RtpMediaIO: RTCP SR send failed on %s stream: %s",
                                            s.mediaType.cstr(), err.desc().cstr());
                        }
                }

                bool allStreamsHaveEmitted() const {
                        auto needs = [](const Stream &s) {
                                return s.active && s.session != nullptr && !s.session->hasEmissionRecord();
                        };
                        for (const VideoStream &vs : _owner->_videos) {
                                if (needs(vs)) return false;
                        }
                        for (const AudioStream &as : _owner->_audios) {
                                if (needs(as)) return false;
                        }
                        for (const DataStream &ds : _owner->_datas) {
                                if (needs(ds)) return false;
                        }
                        return true;
                }

                // Sleeps for up to @p ms milliseconds, returning
                // early when @c requestStop is called.  Implemented
                // via a WaitCondition the requestStop side wakes.
                void cvSleep(unsigned int ms) {
                        Mutex::Locker lock(_mutex);
                        if (_stopRequested.value()) return;
                        (void)_cv.wait(_mutex, [this]() { return _stopRequested.value(); }, ms);
                }

                RtpMediaIO   *_owner;
                int           _intervalMs;
                Atomic<bool>  _stopRequested;
                Mutex         _mutex;
                WaitCondition _cv;
};

// ----- RtpFactory -----

// Content probe for SDP files.  RFC 4566 mandates every SDP session
// description starts with a v= version line, conventionally "v=0"
// followed by a newline.  Accept leading whitespace / BOM so SDP
// files produced by tools that prefix either still parse.
static bool probeSdpDevice(IODevice *device) {
        uint8_t buf[16] = {};
        int64_t n = device->read(buf, sizeof(buf));
        if (n < 3) return false;
        int i = 0;
        if (n >= 3 && buf[0] == 0xEF && buf[1] == 0xBB && buf[2] == 0xBF) {
                i = 3; // UTF-8 BOM
        }
        while (i < n && (buf[i] == ' ' || buf[i] == '\t' || buf[i] == '\r' || buf[i] == '\n')) {
                i++;
        }
        return (i + 2 < n && buf[i] == 'v' && buf[i + 1] == '=' && buf[i + 2] == '0');
}

bool RtpFactory::canHandleDevice(IODevice *device) const {
        return probeSdpDevice(device);
}

RtpFactory::Config::SpecMap RtpFactory::configSpecs() const {
        Config::SpecMap specs;
        auto            s = [&specs](MediaConfig::ID id, const Variant &def) {
                const VariantSpec *gs = MediaConfig::spec(id);
                specs.insert(id, gs ? VariantSpec(*gs).setDefault(def) : VariantSpec().setDefault(def));
        };
        // Media descriptor knobs.
        s(MediaConfig::FrameRate, FrameRate(FrameRate::FPS_30));
        s(MediaConfig::VideoSize, Size2Du32());
        s(MediaConfig::VideoPixelFormat, PixelFormat());
        s(MediaConfig::AudioRate, 48000.0f);
        s(MediaConfig::AudioChannels, int32_t(2));
        // Transport-global defaults.
        s(MediaConfig::RtpLocalAddress, SocketAddress::any(0));
        s(MediaConfig::RtpSessionName, String("promeki RTP stream"));
        s(MediaConfig::RtpSessionOrigin, String("-"));
        s(MediaConfig::RtpPacingMode, RtpPacingMode::Auto);
        s(MediaConfig::RtpMulticastTTL, int32_t(64));
        s(MediaConfig::RtpMulticastInterface, String());
        s(MediaConfig::RtpSaveSdpPath, String());
        s(MediaConfig::RtpSdp, String());
        s(MediaConfig::RtpRtcpEnabled, true);
        s(MediaConfig::RtpRtcpIntervalMs, int32_t(5000));
        s(MediaConfig::RtpRtcpCname, String());
        s(MediaConfig::RtpJitterMs, int32_t(50));
        s(MediaConfig::RtpMaxReadQueueDepth, int32_t(4));
        s(MediaConfig::RtpRecvBufferBytes, int32_t(8 * 1024 * 1024));
        s(MediaConfig::RtpSendBufferBytes, int32_t(8 * 1024 * 1024));
        // Per-stream defaults.
        s(MediaConfig::VideoRtpDestination, SocketAddress());
        s(MediaConfig::VideoRtpPayloadType, int32_t(96));
        s(MediaConfig::VideoRtpClockRate, int32_t(90000));
        s(MediaConfig::VideoRtpSsrc, int32_t(0));
        s(MediaConfig::VideoRtpDscp, int32_t(46));
        s(MediaConfig::VideoRtpTargetBitrate, int32_t(0));
        s(MediaConfig::AudioRtpDestination, SocketAddress());
        s(MediaConfig::AudioRtpPayloadType, int32_t(96));
        s(MediaConfig::AudioRtpClockRate, int32_t(0));
        s(MediaConfig::AudioRtpSsrc, int32_t(0));
        s(MediaConfig::AudioRtpDscp, int32_t(34));
        s(MediaConfig::AudioRtpPacketTimeUs, int32_t(1000));
        s(MediaConfig::DataEnabled, false);
        s(MediaConfig::DataRtpDestination, SocketAddress());
        s(MediaConfig::DataRtpPayloadType, int32_t(98));
        s(MediaConfig::DataRtpClockRate, int32_t(90000));
        s(MediaConfig::DataRtpSsrc, int32_t(0));
        s(MediaConfig::DataRtpDscp, int32_t(34));
        s(MediaConfig::DataRtpFormat, MetadataRtpFormat::JsonMetadata);
        return specs;
}

MediaIO *RtpFactory::create(const Config &config, ObjectBase *parent) const {
        auto *io = new RtpMediaIO(parent);
        io->setConfig(config);
        return io;
}

// ----- Ctor / dtor -----

RtpMediaIO::RtpMediaIO(ObjectBase *parent) : DedicatedThreadMediaIO(parent) {
        // Streams of all three kinds are appended on demand by the
        // matching @c configureVideoStream / @c configureAudioStream /
        // @c configureDataStream (one per @c *RtpDestination today, N
        // per multi-stream session in future).  Each pushed entry
        // sets its own @c mediaType so the SDP builder, RTCP
        // scheduler, and reader-side dispatcher can identify it
        // without dynamic dispatch.
}

RtpMediaIO::~RtpMediaIO() {
        if (isOpen()) (void)close().wait();
        resetAll();
}

// ----- Helpers -----

void RtpMediaIO::resetStreamCommon(Stream &s) {
        // Identity teardown shared by writer and reader modes.
        // Caller has already stopped the per-mode worker threads
        // (resetWriterStream stops the packetizer + TX threads;
        // resetReaderStream stops the RX thread via session
        // stopReceiving) before we get here, so the session +
        // transport tear-down below races no live producer.
        if (s.session != nullptr) {
                // stopReceiving() first so any reader-side receive
                // thread is guaranteed idle before we tear down the
                // transport it is pumping.  Idempotent in writer
                // mode (no receive thread is ever started).
                s.session->stopReceiving();
                s.session->stop();
                delete s.session;
                s.session = nullptr;
        }
        if (s.transport != nullptr) {
                s.transport->close();
                delete s.transport;
                s.transport = nullptr;
        }
        delete s.payload;
        s.payload = nullptr;
        s.destination = SocketAddress();
        s.rtpmap.clear();
        s.fmtp.clear();
        s.active = false;
}

void RtpMediaIO::resetWriterStream(WriterStream &s) {
        // Phase-2 cleanup ordering:
        //   1. Stop accepting new pushes (packetizer's PayloadQueue
        //      gets cancelled by requestStop's onShutdown).
        //   2. Wait for the packetizer to exit — its pop loop
        //      breaks out on Error::Cancelled.
        //   3. Cancel the TX thread's PacketQueue and wait for the
        //      TX thread to exit.
        // The session and transport are only torn down after both
        // workers have joined, so any in-flight send finishes
        // against valid objects.
        if (s.packetizer != nullptr) {
                s.packetizer->requestStop();
                s.packetizer->wait();
                delete s.packetizer;
                s.packetizer = nullptr;
        }
        if (s.tx != nullptr) {
                s.tx->requestStop();
                s.tx->wait();
                delete s.tx;
                s.tx = nullptr;
        }
        resetStreamCommon(s);
        s.packetsSent.setValue(0);
        s.bytesSent.setValue(0);
        s.senderOctets.setValue(0);
        s.txFrameInterval.reset();
        s.txSendDuration.reset();
        s.txHasLastSend = false;
        s.txLastSendStart = TimeStamp();
}

void RtpMediaIO::resetReaderStream(ReaderStream &s) {
        // Reader-mode tear-down: the per-session RX thread is owned
        // by RtpSession itself, so the call to session->stopReceiving
        // inside resetStreamCommon below quiesces it; we just clear
        // out the reassembly + RX-side state afterwards.
        resetStreamCommon(s);
        s.packetsReceived.setValue(0);
        s.bytesReceived.setValue(0);
        s.framesReceived = 0;
        s.packetsLost.setValue(0);
        s.readerImageDesc = ImageDesc();
        s.readerAudioDesc = AudioDesc();
        s.reasmTimestamp = 0;
        s.reasmHasTimestamp = false;
        s.reasmLastSeq = 0;
        s.reasmHaveLastSeq = false;
        s.reasmSynced = false;
        s.reasmPackets.clear();
        s.rxPacketInterval.reset();
        s.rxFrameInterval.reset();
        s.rxFrameAssembleTime.reset();
        s.rxHasLastPacket = false;
        s.rxHasLastFrame = false;
        s.rxHasFrameStart = false;
        s.rxLastPacketTime = TimeStamp();
        s.rxLastFrameTime = TimeStamp();
        s.rxFrameStartTime = TimeStamp();
        s.streamClock = RtpStreamClock();
        s.lastSrArrivedAt = TimeStamp();
        s.hasSr = false;
}

void RtpMediaIO::resetAll() {
        // Stop the RTCP scheduler first — it dispatches on every
        // session, so it must be quiesced before we tear sessions
        // down below.
        if (_rtcpScheduler != nullptr) {
                _rtcpScheduler->requestStop();
                _rtcpScheduler->wait();
                delete _rtcpScheduler;
                _rtcpScheduler = nullptr;
        }
        // Writer-side tear-down.  resetWriterStream stops the
        // packetizer + TX threads, then runs the common identity /
        // session / transport tear-down, then wipes writer stats.
        // Per-kind subclass extras (VideoStream parameter-set
        // cache, AudioStream AES67 sizing) are wiped here too
        // since they're not visible from the WriterStream& level.
        for (VideoStream &vs : _videos) {
                resetWriterStream(vs);
                vs.cachedSps = Buffer();
                vs.cachedPps = Buffer();
                vs.cachedVps = Buffer();
        }
        _videos.clear();
        for (AudioStream &as : _audios) {
                resetWriterStream(as);
                as.storageDesc = AudioDesc();
                as.packetSamples = 0;
                as.packetBytes = 0;
                as.packetTimeUs = 0;
                as.prerollSamples = 0;
                as.silencePacketsEmitted.setValue(0);
                as.silenceSamplesEmitted.setValue(0);
        }
        _audios.clear();
        for (DataStream &ds : _datas) {
                resetWriterStream(ds);
        }
        _datas.clear();
        // Reader-side tear-down.  Empty in writer mode; populated
        // in reader mode.  resetReaderStream calls
        // session->stopReceiving inside resetStreamCommon so the
        // RX thread is joined before the session is freed.
        for (VideoReaderStream &vrs : _videoReaders) {
                resetReaderStream(vrs);
        }
        _videoReaders.clear();
        for (AudioReaderStream &ars : _audioReaders) {
                resetReaderStream(ars);
                ars.fifo.clear();
        }
        _audioReaders.clear();
        for (DataReaderStream &drs : _dataReaders) {
                resetReaderStream(drs);
        }
        _dataReaders.clear();
        _frameCount = 0;
        _framesSent = 0;
        _readerFramesReceived = 0;
        _readerMode = false;
        _videoWirePixelFormat = PixelFormat();
        _readerQueue.clear();
        _sdpSession = SdpSession();
        _readerAgg.audioFifo.clear();
        _readerAgg.videoFrameIndex = 0;
        _readerAgg.hasMetadata = false;
        _readerAgg.pendingMetadata = Metadata();
        _readerAgg.audioFifoHasFront = false;
        _readerAgg.audioFifoFrontRtpTs = 0;
        _readerAgg.steadyAnchor = TimeStamp();
        _readerAgg.ntpAnchor = NtpTime();
        _readerAgg.hasAnchor = false;
        _h264SpropParameterSets.clear();
        _h265SpropVps.clear();
        _h265SpropSps.clear();
        _h265SpropPps.clear();
        _rtcpEnabled = true;
        _rtcpIntervalMs = 5000;
        _rtcpCname.clear();
        // Re-arm anchor seeding so a subsequent re-open uses a
        // fresh @c (steady, wall) reference instant when the first
        // frame arrives.
        _anchorSeeded.setValue(false);
}

static bool isJpegPixelFormat(const PixelFormat &pd) {
        // Any PixelFormat whose codec is "JPEG" is a valid RtpPayloadJpeg
        // input — the codec registry is the single source of truth
        // for what's in the JPEG family, and it's cheaper than
        // enumerating every (subsampling × matrix × range) variant
        // by hand.  Non-compressed formats and non-JPEG compressed
        // formats (H.264, HEVC, ProRes, ...) all fall through.
        return pd.isValid() && pd.isCompressed() && pd.videoCodec().id() == VideoCodec::JPEG;
}

static bool isJpegXsPixelFormat(const PixelFormat &pd) {
        return pd.isValid() && pd.isCompressed() && pd.videoCodec().id() == VideoCodec::JPEG_XS;
}

static bool isH264PixelFormat(const PixelFormat &pd) {
        return pd.isValid() && pd.isCompressed() && pd.videoCodec().id() == VideoCodec::H264;
}

static bool isHevcPixelFormat(const PixelFormat &pd) {
        return pd.isValid() && pd.isCompressed() && pd.videoCodec().id() == VideoCodec::HEVC;
}

Error RtpMediaIO::openStream(WriterStream &s, bool enableMulticastLoopback) {
        if (s.destination.isNull() || !s.destination.isIPv4()) {
                resetWriterStream(s);
                return Error::Ok; // Nothing to open — stream disabled.
        }

        s.transport = new UdpSocketTransport();
        s.transport->setLocalAddress(_localAddress);
        s.transport->setDscp(static_cast<uint8_t>(s.dscp & 0x3F));
        if (_multicastTTL > 0) s.transport->setMulticastTTL(_multicastTTL);
        if (!_multicastInterface.isEmpty()) {
                s.transport->setMulticastInterface(_multicastInterface);
        }
        if (enableMulticastLoopback) s.transport->setMulticastLoopback(true);
        s.transport->setSendBufferSize(_sendBufferBytes);
        s.transport->setReceiveBufferSize(_recvBufferBytes);

        Error err = s.transport->open();
        if (err.isError()) {
                promekiErr("RtpMediaIO: failed to open %s transport: %s", s.mediaType.cstr(), err.desc().cstr());
                resetWriterStream(s);
                return err;
        }

        s.session = new RtpSession();
        s.session->setClockRate(s.clockRate);
        s.session->setPayloadType(s.payloadType);
        if (s.ssrc != 0) s.session->setSsrc(s.ssrc);
        s.session->setRemote(s.destination);

        err = s.session->start(s.transport);
        if (err.isError()) {
                promekiErr("RtpMediaIO: failed to start %s session: %s", s.mediaType.cstr(), err.desc().cstr());
                resetWriterStream(s);
                return err;
        }

        // Spawn the packetizer + TX threads for video and data
        // here.  Audio streams get their packetizer + TX wired in
        // executeCmd(Open) (see the audio-specific path in
        // openStream's caller) because they need the per-stream
        // index that AudioPacketizerThread / AudioTxThread embed
        // in their thread names and FIFO setup.
        if (s.mediaType == "video") {
                auto *tx = new VideoTxThread(this);
                auto *pkt = new VideoPacketizerThread(this);
                pkt->setTx(tx);
                s.tx = tx;
                s.packetizer = pkt;
                tx->start();
                pkt->start();
        } else if (s.mediaType == "application") {
                auto *tx = new DataTxThread(this);
                auto *pkt = new DataPacketizerThread(this);
                pkt->setTx(tx);
                s.tx = tx;
                s.packetizer = pkt;
                tx->start();
                pkt->start();
        }

        // Name the diagnostic histograms so toString() / log dumps
        // identify which stream they came from.  Units are
        // microseconds throughout.
        s.txFrameInterval.setName(String("rtp-tx-") + s.mediaType + "-frame-interval");
        s.txFrameInterval.setUnit("us");
        s.txSendDuration.setName(String("rtp-tx-") + s.mediaType + "-send-duration");
        s.txSendDuration.setUnit("us");

        s.active = true;
        return Error::Ok;
}

Error RtpMediaIO::openReaderStream(ReaderStream &s, bool /*enableMulticastLoopback*/) {
        if (s.destination.isNull() || !s.destination.isIPv4()) {
                resetReaderStream(s);
                return Error::Ok; // Nothing to open — stream disabled.
        }

        // Reader-side transport binds to the stream port so packets
        // arriving at that port land on our socket.  For a multicast
        // stream we bind to the group port on any interface
        // (0.0.0.0) and join the group below.  For a unicast stream
        // we bind to the specific interface if the user asked for
        // one via RtpLocalAddress, otherwise 0.0.0.0.
        const bool    isMulticast = s.destination.isMulticast();
        SocketAddress bindAddr;
        if (isMulticast) {
                bindAddr = SocketAddress::any(s.destination.port());
        } else {
                // Honour RtpLocalAddress if set; otherwise bind to
                // any interface on the requested stream port.
                if (!_localAddress.isNull() && _localAddress.port() != 0) {
                        bindAddr = _localAddress;
                } else {
                        bindAddr = SocketAddress::any(s.destination.port());
                }
        }

        s.transport = new UdpSocketTransport();
        s.transport->setLocalAddress(bindAddr);
        s.transport->setReuseAddress(true);
        if (!_multicastInterface.isEmpty()) {
                s.transport->setMulticastInterface(_multicastInterface);
        }
        s.transport->setReceiveBufferSize(_recvBufferBytes);
        s.transport->setSendBufferSize(_sendBufferBytes);
        // Loopback is a sender-side concept for multicast; receivers
        // always see their own host's outgoing packets as long as the
        // sender enabled loopback, so nothing to configure here.

        Error err = s.transport->open();
        if (err.isError()) {
                promekiErr("RtpMediaIO: failed to open %s reader transport: %s", s.mediaType.cstr(),
                           err.desc().cstr());
                resetReaderStream(s);
                return err;
        }

        if (isMulticast) {
                UdpSocket *sock = s.transport->socket();
                if (sock == nullptr) {
                        promekiErr("RtpMediaIO: %s transport has no socket", s.mediaType.cstr());
                        resetReaderStream(s);
                        return Error::Invalid;
                }
                Error jerr = _multicastInterface.isEmpty()
                                     ? sock->joinMulticastGroup(s.destination)
                                     : sock->joinMulticastGroup(s.destination, _multicastInterface);
                if (jerr.isError()) {
                        promekiErr("RtpMediaIO: join %s on %s failed: %s", s.destination.toString().cstr(),
                                   s.mediaType.cstr(), jerr.desc().cstr());
                        resetReaderStream(s);
                        return jerr;
                }
        }

        s.session = new RtpSession();
        s.session->setClockRate(s.clockRate);
        s.session->setPayloadType(s.payloadType);
        if (s.ssrc != 0) s.session->setSsrc(s.ssrc);
        // remote is meaningless for reader sessions but we set it
        // anyway so the session object is self-consistent.
        s.session->setRemote(s.destination);

        err = s.session->start(s.transport);
        if (err.isError()) {
                promekiErr("RtpMediaIO: failed to start %s reader session: %s", s.mediaType.cstr(),
                           err.desc().cstr());
                resetReaderStream(s);
                return err;
        }

        // Wire up the per-stream receive callback.  Each stream has
        // its own RtpSession and its own receive thread, so the
        // callbacks never race against each other on the same
        // reassembler state.  The thread name is "rtp-rx-video" /
        // "rtp-rx-audio" / "rtp-rx-data" so that per-stream CPU
        // usage shows up distinctly in `top -H`.
        String threadName = String("rtp-rx-") + s.mediaType;
        Error  recvErr;
        // Dispatch by mediaType — works for any audio stream in
        // _audios (each routes through onAudioPacket today; future
        // multi-audio plumbing will pass the stream identity through
        // the callback so the right AudioStream is updated).
        if (s.mediaType == "video") {
                recvErr = s.session->startReceiving(
                        [this](const RtpPacket &pkt, const SocketAddress &) { onVideoPacket(pkt); }, threadName);
        } else if (s.mediaType == "audio") {
                recvErr = s.session->startReceiving(
                        [this](const RtpPacket &pkt, const SocketAddress &) { onAudioPacket(pkt); }, threadName);
        } else {
                recvErr = s.session->startReceiving(
                        [this](const RtpPacket &pkt, const SocketAddress &) { onDataPacket(pkt); }, threadName);
        }
        if (recvErr.isError()) {
                promekiErr("RtpMediaIO: startReceiving on %s failed: %s", s.mediaType.cstr(),
                           recvErr.desc().cstr());
                resetReaderStream(s);
                return recvErr;
        }

        // Name the diagnostic histograms so toString() / log dumps
        // identify which stream they came from.  Units are
        // microseconds throughout.
        s.rxPacketInterval.setName(String("rtp-rx-") + s.mediaType + "-packet-interval");
        s.rxPacketInterval.setUnit("us");
        s.rxFrameInterval.setName(String("rtp-rx-") + s.mediaType + "-frame-interval");
        s.rxFrameInterval.setUnit("us");
        s.rxFrameAssembleTime.setName(String("rtp-rx-") + s.mediaType + "-frame-assemble");
        s.rxFrameAssembleTime.setUnit("us");

        s.active = true;
        return Error::Ok;
}

// ----- Per-stream configuration -----

Error RtpMediaIO::configureVideoStream(const MediaIO::Config &cfg, const MediaDesc &mediaDesc) {
        // Push a video stream entry on the per-mode list if applySdp
        // didn't already do it.  Reader and writer modes go through
        // exactly the same configure logic against a @c Stream &
        // reference @c v that points at the right entry; reader-only
        // fields like @c readerImageDesc and writer-only fields like
        // @c imageDesc go through narrower references resolved by
        // mode below.
        Stream            *base = nullptr;
        VideoStream       *vs = nullptr;
        VideoReaderStream *vrs = nullptr;
        if (_readerMode) {
                if (_videoReaders.isEmpty()) {
                        _videoReaders.pushToBack(VideoReaderStream{});
                        _videoReaders.back().mediaType = "video";
                }
                vrs = &_videoReaders.back();
                base = vrs;
        } else {
                if (_videos.isEmpty()) {
                        _videos.pushToBack(VideoStream{});
                        _videos.back().mediaType = "video";
                }
                vs = &_videos.back();
                base = vs;
        }
        Stream &v = *base;

        SocketAddress dest = cfg.getAs<SocketAddress>(MediaConfig::VideoRtpDestination, SocketAddress());
        if (dest.isNull()) return Error::Ok; // Disabled.

        // Try to pull a video descriptor from three sources in
        // priority order:
        //   1. The caller-supplied MediaDesc (setMediaDesc /
        //      pendingMediaDesc).
        //   2. For the reader, the fields that loadSdp() populated
        //      onto the working MediaDesc (JPEG XS fmtp geometry,
        //      etc.).  Already merged into mediaDesc at this point.
        //   3. The stand-alone VideoSize + VideoPixelFormat config
        //      keys, matching how TPG and the other generator
        //      backends let callers describe the format via plain
        //      --ic flags rather than a full MediaDesc object.
        ImageDesc img;
        if (!mediaDesc.imageList().isEmpty()) {
                img = mediaDesc.imageList()[0];
        }
        if (!img.isValid()) {
                Size2Du32   size = cfg.getAs<Size2Du32>(MediaConfig::VideoSize, Size2Du32());
                PixelFormat pd = cfg.getAs<PixelFormat>(MediaConfig::VideoPixelFormat, PixelFormat());
                if (size.isValid() && pd.isValid()) {
                        img = ImageDesc(size, pd);
                }
        }
        v.destination = dest;
        v.payloadType = static_cast<uint8_t>(cfg.getAs<int>(MediaConfig::VideoRtpPayloadType, 96) & 0x7F);
        v.clockRate = static_cast<uint32_t>(cfg.getAs<int>(MediaConfig::VideoRtpClockRate, 90000));
        v.ssrc = static_cast<uint32_t>(cfg.getAs<uint32_t>(MediaConfig::VideoRtpSsrc, 0));
        v.dscp = cfg.getAs<int>(MediaConfig::VideoRtpDscp, 46);

        // Reader-mode JPEG: the SDP carries no geometry — RFC 2435
        // puts width/height in each packet header.  Detect JPEG from
        // the payload type (static PT 26) and create the payload
        // handler with placeholder dimensions; readerImageDesc will
        // be populated lazily by emitVideoFrame() from the first
        // reassembled frame.  Stash the fmtp string so the deferred
        // geometry code can read colorimetry and RANGE from it.
        if (!img.isValid() && _readerMode && v.payloadType == 26) {
                v.payload = new RtpPayloadJpeg(0, 0);
                v.rtpmap = String("JPEG/") + String::number(v.clockRate);
                v.fmtp = cfg.getAs<String>(MediaConfig::VideoRtpFmtp, String());
                return Error::Ok;
        }

        // Reader-mode H.264 / H.265: the SDP carries no geometry —
        // width and height live in the bitstream's SPS, which the
        // sender may not publish via a (non-standard) fmtp parameter.
        // Identify the codec by the rtpmap encoding name that
        // applySdp stashed and create the payload handler with a
        // placeholder ImageDesc (size 0×0).  The reader treats
        // @c readerImageDesc as authoritative once it has been
        // populated; until then emitVideoFrame ships frames with the
        // placeholder descriptor so downstream consumers (decoder /
        // pipeline planner) still see the codec identity.
        if (!img.isValid() && _readerMode) {
                String enc = cfg.getAs<String>(MediaConfig::VideoRtpEncoding, String());
                if (enc == "H264" || enc == "h264") {
                        v.payload = new RtpPayloadH264(v.payloadType);
                        v.clockRate = RtpPayloadH264::ClockRate;
                        v.rtpmap = String("H264/90000");
                        v.fmtp = cfg.getAs<String>(MediaConfig::VideoRtpFmtp, String());
                        vrs->readerImageDesc = ImageDesc(Size2Du32(0, 0), PixelFormat(PixelFormat::H264));
                        return Error::Ok;
                }
                if (enc == "H265" || enc == "h265" || enc == "HEVC" || enc == "hevc") {
                        v.payload = new RtpPayloadH265(v.payloadType);
                        v.clockRate = RtpPayloadH265::ClockRate;
                        v.rtpmap = String("H265/90000");
                        v.fmtp = cfg.getAs<String>(MediaConfig::VideoRtpFmtp, String());
                        vrs->readerImageDesc = ImageDesc(Size2Du32(0, 0), PixelFormat(PixelFormat::HEVC));
                        return Error::Ok;
                }
        }

        if (!img.isValid()) {
                promekiErr("RtpMediaIO: VideoRtpDestination set but no "
                           "video track in media descriptor (set VideoSize + "
                           "VideoPixelFormat, or supply a MediaDesc)");
                return Error::InvalidArgument;
        }

        // Stash the source image descriptor on the per-mode subclass.
        // Writer mode uses it later for codec lookup in the SDP
        // sprop refresh; reader mode uses it as the initial guess
        // for downstream planners until the JPEG-discovered desc
        // overwrites it.
        if (vs) vs->imageDesc = img;
        if (vrs) vrs->readerImageDesc = img;
        const PixelFormat &pd = img.pixelFormat();

        // Use ImageDesc::toSdp() to derive the rtpmap, fmtp
        // (including colorimetry + RANGE), and encoding name.
        // This keeps all PixelFormat → SDP mapping in one place.
        SdpMediaDescription sdpMd = img.toSdp(v.payloadType);
        if (sdpMd.mediaType().isEmpty()) {
                promekiErr("RtpMediaIO: unsupported video pixel format '%s'", pd.name().cstr());
                return Error::NotSupported;
        }

        // Extract rtpmap and fmtp from the SdpMediaDescription.
        for (size_t i = 0; i < sdpMd.attributes().size(); i++) {
                const String &key = sdpMd.attributes()[i].first();
                const String &val = sdpMd.attributes()[i].second();
                if (key == "rtpmap") {
                        // Strip the "<pt> " prefix — v.rtpmap
                        // stores just the encoding/clock part.
                        size_t sp = val.find(' ');
                        v.rtpmap = (sp != String::npos) ? val.mid(sp + 1) : val;
                } else if (key == "fmtp") {
                        size_t sp = val.find(' ');
                        v.fmtp = (sp != String::npos) ? val.mid(sp + 1) : val;
                }
        }

        // toSdp may remap the payload type (e.g. 96 → 26 for JPEG).
        if (!sdpMd.payloadTypes().isEmpty()) {
                v.payloadType = sdpMd.payloadTypes()[0];
        }

        // Pick payload class from the pixel descriptor family.
        if (isJpegPixelFormat(pd)) {
                v.payload = new RtpPayloadJpeg(static_cast<int>(img.width()), static_cast<int>(img.height()));
        } else if (isJpegXsPixelFormat(pd)) {
                auto *jxs = new RtpPayloadJpegXs(static_cast<int>(img.width()), static_cast<int>(img.height()),
                                                 v.payloadType);
                v.payload = jxs;
                v.clockRate = RtpPayloadJpegXs::ClockRate;
        } else if (isH264PixelFormat(pd)) {
                // RFC 6184 H.264.  Encoders feed the writer Annex-B
                // bitstreams and RtpPayloadH264 fragments them into
                // single-NAL or FU-A packets with a 90 kHz clock.
                // Geometry is in the bitstream (SPS) — width/height
                // here are kept on the descriptor for SDP hints but
                // the payload class itself does not need them.
                v.payload = new RtpPayloadH264(v.payloadType);
                v.clockRate = RtpPayloadH264::ClockRate;
                // No CSC: compressed bitstreams ship verbatim.
                _videoWirePixelFormat = PixelFormat();
        } else if (isHevcPixelFormat(pd)) {
                // RFC 7798 H.265 / HEVC.
                v.payload = new RtpPayloadH265(v.payloadType);
                v.clockRate = RtpPayloadH265::ClockRate;
                _videoWirePixelFormat = PixelFormat();
        } else if (!pd.isCompressed()) {
                const PixelMemLayout &pf = pd.memLayout();
                if (pf.planeCount() > 1) {
                        promekiErr("RtpMediaIO: planar pixel formats are not supported "
                                   "for RFC 4175 raw video (use an interleaved format)");
                        return Error::NotSupported;
                }
                size_t ppb = pf.pixelsPerBlock();
                size_t bpb = pf.bytesPerBlock();
                int    bpp = (ppb > 0) ? static_cast<int>((8 * bpb) / ppb) : 0;
                if (bpp == 0) {
                        promekiErr("RtpMediaIO: video pixel desc has zero bits-per-pixel");
                        return Error::InvalidArgument;
                }
                // RFC 4175 mandates Cb-Y-Cr-Y (UYVY) component order
                // for YCbCr 4:2:2 on the wire.  When the input uses
                // a different component order (e.g. YUYV), store the
                // corresponding UYVY PixelFormat so sendVideo() can call
                // UncompressedVideoPayload::convert() before packing.
                if (pf.id() == PixelMemLayout::I_422_3x8) {
                        if (pd.id() == PixelFormat::YUV8_422_Rec709)
                                _videoWirePixelFormat = PixelFormat(PixelFormat::YUV8_422_UYVY_Rec709);
                        else if (pd.id() == PixelFormat::YUV8_422_Rec601)
                                _videoWirePixelFormat = PixelFormat(PixelFormat::YUV8_422_UYVY_Rec601);
                        else
                                _videoWirePixelFormat = PixelFormat();
                } else {
                        _videoWirePixelFormat = PixelFormat();
                }
                v.payload = new RtpPayloadRawVideo(static_cast<int>(img.width()), static_cast<int>(img.height()),
                                                   bpp, static_cast<int>(bpb));
        }

        return Error::Ok;
}

Error RtpMediaIO::configureAudioStream(const MediaIO::Config &cfg, const MediaDesc &mediaDesc) {
        SocketAddress dest = cfg.getAs<SocketAddress>(MediaConfig::AudioRtpDestination, SocketAddress());
        if (dest.isNull()) return Error::Ok; // Disabled.

        // Same three-source fallback as configureVideoStream:
        // caller-supplied MediaDesc first, then loadSdp()-populated
        // entries (already merged into mediaDesc), then
        // AudioRate + AudioChannels config keys as a last resort.
        AudioDesc ad;
        if (!mediaDesc.audioList().isEmpty()) {
                ad = mediaDesc.audioList()[0];
        }
        if (!ad.isValid()) {
                float        rate = cfg.getAs<float>(MediaConfig::AudioRate, 0.0f);
                unsigned int channels = cfg.getAs<unsigned int>(MediaConfig::AudioChannels, 0u);
                if (rate > 0.0f && channels > 0) {
                        // Reader-mode default storage format is
                        // PCMI_S16BE (the L16 wire format).  The
                        // writer path overrides this explicitly so
                        // that AudioBuffer can convert whatever the
                        // producer supplies into the wire format.
                        AudioFormat::ID dt = _readerMode ? AudioFormat::PCMI_S16BE : AudioFormat::PCMI_S16LE;
                        ad = AudioDesc(dt, rate, channels);
                }
        }
        if (!ad.isValid()) {
                promekiErr("RtpMediaIO: AudioRtpDestination set but no "
                           "audio track in media descriptor (set AudioRate + "
                           "AudioChannels, or supply a MediaDesc)");
                return Error::InvalidArgument;
        }

        // Push to the per-mode list and bind a Stream& reference for
        // identity-field setup.  Writer-side AES67 packetisation
        // params (storageDesc, packetSamples, etc.) live on
        // AudioStream; reader-side fifo + readerAudioDesc live on
        // AudioReaderStream.
        Stream            *base = nullptr;
        AudioStream       *aw = nullptr;
        AudioReaderStream *ar = nullptr;
        if (_readerMode) {
                if (_audioReaders.isEmpty()) {
                        _audioReaders.pushToBack(AudioReaderStream{});
                        _audioReaders.back().mediaType = "audio";
                }
                ar = &_audioReaders.back();
                base = ar;
        } else {
                if (_audios.isEmpty()) {
                        _audios.pushToBack(AudioStream{});
                        _audios.back().mediaType = "audio";
                }
                aw = &_audios.back();
                base = aw;
        }
        Stream &a = *base;

        if (ar) ar->readerAudioDesc = ad;
        a.destination = dest;
        a.payloadType = static_cast<uint8_t>(cfg.getAs<int>(MediaConfig::AudioRtpPayloadType, 96) & 0x7F);
        a.ssrc = cfg.getAs<uint32_t>(MediaConfig::AudioRtpSsrc, 0);
        a.dscp = cfg.getAs<int>(MediaConfig::AudioRtpDscp, 34);

        int cfgClockRate = cfg.getAs<int>(MediaConfig::AudioRtpClockRate, 0);
        a.clockRate =
                cfgClockRate > 0 ? static_cast<uint32_t>(cfgClockRate) : static_cast<uint32_t>(ad.sampleRate());

        const uint32_t     sr = static_cast<uint32_t>(ad.sampleRate());
        const unsigned int ch = ad.channels();
        if (sr == 0 || ch == 0) {
                promekiErr("RtpMediaIO: audio sample rate or channel count is zero");
                return Error::InvalidArgument;
        }

        // -- AES67 packet sizing --
        //
        // packetSamples = sampleRate × packetTimeUs / 1e6 samples per
        // channel.  That must fit in the transport MTU after the RTP
        // header, so there is an upper bound of
        // (maxPayload / (channels × bytesPerSample)) samples.  If the
        // requested packet time exceeds the cap we fall back through
        // the standard AES67 intervals (4ms → 1ms → 333µs → 250µs →
        // 125µs) until we find one that fits.
        //
        // L16 storage is 2 bytes per sample per channel; channels can
        // be anything up to 64 (AES67 allows 1-8 typically).
        static constexpr size_t kMaxBytesPerPacket = 1200;
        static constexpr size_t kStorageBytesPerSample = 2; // L16
        const size_t maxSamplesPerPacket = kMaxBytesPerPacket / (static_cast<size_t>(ch) * kStorageBytesPerSample);
        if (maxSamplesPerPacket == 0) {
                promekiErr("RtpMediaIO: %u audio channels at L16 will not fit in %zu-byte MTU", ch,
                           kMaxBytesPerPacket);
                return Error::InvalidArgument;
        }

        const int requestedUs = cfg.getAs<int>(MediaConfig::AudioRtpPacketTimeUs, 1000);
        auto      samplesForUs = [sr](int us) -> size_t {
                return (static_cast<uint64_t>(sr) * static_cast<uint64_t>(us)) / 1'000'000ull;
        };

        size_t resolvedSamples = samplesForUs(requestedUs);
        int    resolvedUs = requestedUs;
        if (resolvedSamples == 0 || resolvedSamples > maxSamplesPerPacket) {
                // Fall back through the AES67 standard set, largest
                // first so we keep packet counts low when possible.
                static constexpr int kAes67Intervals[] = {4000, 1000, 333, 250, 125};
                resolvedSamples = 0;
                for (int us : kAes67Intervals) {
                        size_t s = samplesForUs(us);
                        if (s > 0 && s <= maxSamplesPerPacket) {
                                resolvedSamples = s;
                                resolvedUs = us;
                                break;
                        }
                }
                if (resolvedSamples == 0) {
                        // Last resort: one sample per packet.
                        resolvedSamples = 1;
                        resolvedUs = static_cast<int>((1'000'000ull + sr - 1) / sr);
                }
                promekiWarn("RtpMediaIO: audio packet time %dus exceeds MTU for %u channels; clamped to %dus (%zu "
                            "samples/packet)",
                            requestedUs, ch, resolvedUs, resolvedSamples);
        }

        const size_t packetBytes =
                resolvedSamples * static_cast<size_t>(ch) * kStorageBytesPerSample;

        // -- Writer-only: AES67 sizing + storage descriptor --
        if (aw) {
                aw->packetSamples = resolvedSamples;
                aw->packetBytes = packetBytes;
                aw->packetTimeUs = resolvedUs;

                // Preroll: wait for this many source samples to
                // accumulate in the FIFO before AudioTxThread starts
                // emission.  Matches upstream video latency at
                // startup and absorbs strand stalls (heavy IDR
                // encodes etc.) without producing wire dropouts.
                const int prerollMs = cfg.getAs<int>(MediaConfig::AudioRtpPrerollMs, 0);
                aw->prerollSamples = prerollMs > 0
                        ? static_cast<size_t>((static_cast<uint64_t>(sr) *
                                               static_cast<uint64_t>(prerollMs)) /
                                              1000ull)
                        : 0;

                // Storage descriptor for the per-stream wire format.
                // The AudioPacketizerThread owns the FIFO itself
                // (created in its onStart hook so the storage gets
                // allocated on the packetizer thread); we just record
                // the descriptor here so it survives across re-opens
                // and can be inspected by tests / stats.
                AudioDesc storageDesc(AudioFormat::PCMI_S16BE, ad.sampleRate(),
                                      ad.channels());
                if (!storageDesc.isValid()) {
                        promekiErr(
                                "RtpMediaIO: could not build L16 storage descriptor (%.1f Hz, %u ch)",
                                ad.sampleRate(), ad.channels());
                        return Error::InvalidArgument;
                }
                aw->storageDesc = storageDesc;
        }

        // -- Payload handler (both modes share the wire format) --
        //
        // L16 only for this first pass.  Clamp the payload's
        // max-payload-size to exactly packetBytes so pack() emits one
        // RtpPacket per AES67 packet (instead of trying to pack more
        // into a single datagram).
        auto *pl16 = new RtpPayloadL16(sr, static_cast<int>(ch));
        pl16->setPayloadType(a.payloadType);
        pl16->setMaxPayloadSize(packetBytes);
        a.payload = pl16;
        a.rtpmap = String("L16/") + String::number(a.clockRate) + String("/") + String::number(ch);

        return Error::Ok;
}

Error RtpMediaIO::configureDataStream(const MediaIO::Config &cfg) {
        // Push a data-stream entry on the per-mode list and bind a
        // Stream& reference for setup.  applySdp() may have already
        // pushed an entry for this m=application (so the ts-refclk
        // fields land on it); reuse when present.
        Stream *base = nullptr;
        if (_readerMode) {
                if (_dataReaders.isEmpty()) {
                        _dataReaders.pushToBack(DataReaderStream{});
                        _dataReaders.back().mediaType = "application";
                }
                base = &_dataReaders.back();
        } else {
                if (_datas.isEmpty()) {
                        _datas.pushToBack(DataStream{});
                        _datas.back().mediaType = "application";
                }
                base = &_datas.back();
        }
        Stream &d = *base;

        bool          enabled = cfg.getAs<bool>(MediaConfig::DataEnabled, false);
        SocketAddress dest = cfg.getAs<SocketAddress>(MediaConfig::DataRtpDestination, SocketAddress());
        if (!enabled || dest.isNull()) return Error::Ok; // Disabled.

        d.destination = dest;
        d.payloadType = static_cast<uint8_t>(cfg.getAs<int>(MediaConfig::DataRtpPayloadType, 98) & 0x7F);
        d.clockRate = static_cast<uint32_t>(cfg.getAs<int>(MediaConfig::DataRtpClockRate, 90000));
        d.ssrc = static_cast<uint32_t>(cfg.getAs<uint32_t>(MediaConfig::DataRtpSsrc, 0));
        d.dscp = cfg.getAs<int>(MediaConfig::DataRtpDscp, 34);

        Error fmtErr;
        Enum  fmt = cfg.get(MediaConfig::DataRtpFormat).asEnum(MetadataRtpFormat::Type, &fmtErr);
        if (fmtErr.isError() || !fmt.hasListedValue()) {
                promekiErr("RtpMediaIO: unknown metadata RTP format");
                return Error::InvalidArgument;
        }
        _dataFormat = fmt;

        if (fmt.value() == MetadataRtpFormat::JsonMetadata.value()) {
                auto *p = new RtpPayloadJson(d.payloadType, d.clockRate);
                d.payload = p;
                d.rtpmap = String("x-promeki-metadata-json/") + String::number(d.clockRate);
        } else {
                promekiErr("RtpMediaIO: metadata format %s is not yet implemented", fmt.toString().cstr());
                return Error::NotSupported;
        }
        return Error::Ok;
}

// ----- SDP -----

void RtpMediaIO::buildSdp() {
        _sdpSession = SdpSession();
        _sdpSession.setSessionName(_sessionName);
        _sdpSession.setOrigin(_sessionOrigin, 0, 0, "IN", "IP4", "0.0.0.0");

        auto addStream = [&](const Stream &s) {
                if (!s.active) return;
                SdpMediaDescription md;
                md.setMediaType(s.mediaType);
                md.setPort(s.destination.port());
                md.setProtocol("RTP/AVP");
                md.addPayloadType(s.payloadType);
                if (!s.rtpmap.isEmpty()) {
                        md.setAttribute("rtpmap", String::number(s.payloadType) + String(" ") + s.rtpmap);
                }
                String fmtp = s.fmtp;
                // Inject sprop-* parameter-set strings into the video
                // fmtp once the first IDR / IRAP has flown past and
                // populated the cache.  Without this, ffmpeg / ffplay
                // fail their initial codec probe when the receiver
                // joins after the encoder's first frame and never
                // recover — even though parameter sets repeat on
                // every IDR, the demuxer relies on the SDP sprop to
                // populate the H.264 extradata up-front.
                if (s.mediaType == "video") {
                        auto appendParam = [&](const String &kv) {
                                if (kv.isEmpty()) return;
                                if (!fmtp.isEmpty()) fmtp += String(";");
                                fmtp += kv;
                        };
                        if (!_h264SpropParameterSets.isEmpty()) {
                                appendParam(String("sprop-parameter-sets=") + _h264SpropParameterSets);
                        }
                        if (!_h265SpropVps.isEmpty()) {
                                appendParam(String("sprop-vps=") + _h265SpropVps);
                        }
                        if (!_h265SpropSps.isEmpty()) {
                                appendParam(String("sprop-sps=") + _h265SpropSps);
                        }
                        if (!_h265SpropPps.isEmpty()) {
                                appendParam(String("sprop-pps=") + _h265SpropPps);
                        }
                }
                if (!fmtp.isEmpty()) {
                        md.setAttribute("fmtp", String::number(s.payloadType) + String(" ") + fmtp);
                }
                // Stamp frame rate on the video m= section so the RX
                // side can recover the cadence — RFC 2435 and RFC 4175
                // leave it out of @c rtpmap, and the planner / audio
                // aggregator silently fall back to 29.97 without it.
                // Rational form is used so NTSC rates (60000/1001 etc.)
                // round-trip exactly.
                if (s.mediaType == "video" && _frameRate.isValid()) {
                        md.setAttribute("framerate", _frameRate.toString());
                }
                // RFC 5761 rtcp-mux: tells receivers that RTP and
                // RTCP share a single port for this stream, so the
                // receiver does not open a separate socket at RTP
                // port + 1.  Without this, ffplay (and any other SDP
                // consumer that honours the classic RTCP-on-next-port
                // convention) tries to bind port + 1 for RTCP, which
                // collides when the user has two streams on adjacent
                // ports (e.g. video 10000 / audio 10001).  We do
                // transmit RTCP SR / SDES from the @c RtcpScheduler
                // — this attribute is what tells receivers to
                // demux RTP and RTCP on the same port via the PT
                // field instead of opening a separate RTCP socket.
                md.setAttribute("rtcp-mux", String());
                // Write clock reference attributes when the stream has
                // an absolute (PTP/GPS) clock domain.
                if (s.clockDomain.isValid() && s.clockDomain.isCrossMachineComparable()) {
                        // Extract profile from domain name
                        // (e.g. "ptp.IEEE1588-2008" -> "IEEE1588-2008")
                        String domainName = s.clockDomain.name();
                        String tsRefClk;
                        if (domainName.startsWith("ptp.")) {
                                String profile = domainName.mid(4);
                                tsRefClk = String("ptp=") + profile;
                                if (!s.ptpGrandmaster.isNull()) {
                                        tsRefClk += String(":") + s.ptpGrandmaster.toString();
                                }
                        }
                        if (!tsRefClk.isEmpty()) {
                                md.setAttribute("ts-refclk", tsRefClk);
                                md.setAttribute("mediaclk", "direct=0");
                        }
                }
                md.setConnectionAddress(s.destination.address().toString());
                _sdpSession.addMediaDescription(md);
        };
        for (const VideoStream &vs : _videos) addStream(vs);
        for (const AudioStream &as : _audios) addStream(as);
        for (const DataStream &ds : _datas) addStream(ds);
}

Error RtpMediaIO::writeSdpFile(const String &path) {
        if (path.isEmpty()) return Error::Ok;
        return _sdpSession.toFile(path);
}

void RtpMediaIO::refreshSdpSprop() {
        // Collapse the cached parameter-set NALs into base64-encoded
        // SDP fmtp strings.  H.264 packs SPS+PPS into a single
        // comma-separated @c sprop-parameter-sets value (RFC 6184
        // §8.1).  HEVC uses three separate values per RFC 7798 §7.1.
        if (_videos.isEmpty()) return;
        VideoStream &vs = _videos[0];
        const VideoCodec::ID codec = vs.imageDesc.pixelFormat().videoCodec().id();
        String newH264, newH265Vps, newH265Sps, newH265Pps;
        if (codec == VideoCodec::H264 && vs.cachedSps.isValid() && vs.cachedPps.isValid()) {
                String sps = Base64::encode(vs.cachedSps);
                String pps = Base64::encode(vs.cachedPps);
                newH264 = sps + String(",") + pps;
        } else if (codec == VideoCodec::HEVC) {
                if (vs.cachedVps.isValid()) newH265Vps = Base64::encode(vs.cachedVps);
                if (vs.cachedSps.isValid()) newH265Sps = Base64::encode(vs.cachedSps);
                if (vs.cachedPps.isValid()) newH265Pps = Base64::encode(vs.cachedPps);
        }

        const bool changed = (newH264 != _h264SpropParameterSets) || (newH265Vps != _h265SpropVps) ||
                             (newH265Sps != _h265SpropSps) || (newH265Pps != _h265SpropPps);
        if (!changed) return;

        _h264SpropParameterSets = newH264;
        _h265SpropVps = newH265Vps;
        _h265SpropSps = newH265Sps;
        _h265SpropPps = newH265Pps;

        // Rebuild the SDP session and rewrite the on-disk file (if
        // configured) so a receiver reading the file picks up the
        // new sprop values.  An external process may already have
        // read the file before this rewrite — that's fine for the
        // common case where the receiver waits a moment after the
        // sender starts; if your workflow requires the receiver to
        // start instantly, populate
        // @c MediaConfig::VideoSpropParameterSets at open time
        // instead (planned).
        buildSdp();
        if (!_sdpPath.isEmpty()) {
                Error werr = writeSdpFile(_sdpPath);
                if (werr.isError()) {
                        promekiWarn("RtpMediaIO: failed to rewrite SDP with sprop-* "
                                    "parameter sets at '%s': %s",
                                    _sdpPath.cstr(), werr.desc().cstr());
                } else {
                        promekiInfo("RtpMediaIO: SDP at '%s' updated with %s sprop "
                                    "parameter sets — receivers reading the file from "
                                    "this point on will populate decoder extradata "
                                    "before probing.",
                                    _sdpPath.cstr(),
                                    codec == VideoCodec::H264 ? "H.264" : "HEVC");
                }
        }
}

// ----- Reader-side SDP ingest -----
//
// applySdp merges a pre-parsed SdpSession into the reader's
// working MediaConfig and MediaDesc.  The RTP-transport bits
// (destinations, payload types, clock rates, channel counts)
// come out of each @c m= section via @ref SdpMediaDescription
// accessors.  The media-format bits (ImageDesc for video,
// AudioDesc for audio) are derived centrally by
// @ref MediaDesc::fromSdp, which in turn delegates to
// @ref ImageDesc::fromSdp / @ref AudioDesc::fromSdp on the
// per-stream descriptors.  That keeps the RTP-payload-encoding
// interpretation (L16 → PCMI_S16BE, jxsv fmtp → JPEG_XS_*, etc.)
// in one place instead of scattering it across the task.
Error RtpMediaIO::applySdp(const SdpSession &sdp, MediaIO::Config &cfg, MediaDesc &mediaDesc) {
        // SDP connection addresses may appear at the session (c=)
        // level or inside each media description; the media-level
        // override wins per RFC 4566.
        String sessionConnection = sdp.connectionAddress();

        // Merge the SDP's media-format view into our working
        // MediaDesc without clobbering anything the caller
        // already put there.  MediaDesc::fromSdp() walks every
        // m= section and populates image + audio lists via the
        // per-type fromSdp() factories; we then append only the
        // entries the caller did not already provide.
        MediaDesc sdpMd = MediaDesc::fromSdp(sdp);
        if (mediaDesc.imageList().isEmpty() && !sdpMd.imageList().isEmpty()) {
                for (size_t i = 0; i < sdpMd.imageList().size(); i++) {
                        mediaDesc.imageList().pushToBack(sdpMd.imageList()[i]);
                }
        }
        if (mediaDesc.audioList().isEmpty() && !sdpMd.audioList().isEmpty()) {
                for (size_t i = 0; i < sdpMd.audioList().size(); i++) {
                        mediaDesc.audioList().pushToBack(sdpMd.audioList()[i]);
                }
        }
        // Frame rate: prefer the SDP-derived rate over whatever the
        // caller's pendingMediaDesc started with.  RFC 2435 / RFC 4175
        // do not carry frame rate in their @c rtpmap; @ref MediaDesc::toSdp
        // emits @c a=framerate so a same-host RX can recover it
        // exactly, but the only signal the caller has at open time is
        // the config default (29.97), which is wrong for any non-29.97
        // stream and silently breaks the per-frame audio aggregation
        // math downstream.
        if (sdpMd.frameRate().isValid()) {
                mediaDesc.setFrameRate(sdpMd.frameRate());
        }

        // Walk the m= sections a second time for the RTP-transport
        // plumbing (destinations, payload types, clock rates,
        // audio channel counts).  These keys are MediaConfig-
        // specific, so they cannot live on SdpMediaDescription
        // — that would require dragging the MediaConfig catalog
        // into the network layer.
        for (size_t i = 0; i < sdp.mediaDescriptions().size(); i++) {
                const SdpMediaDescription &md = sdp.mediaDescriptions()[i];
                MediaConfig::ID            destKey = MediaConfig::VideoRtpDestination;
                MediaConfig::ID            ptKey = MediaConfig::VideoRtpPayloadType;
                MediaConfig::ID            rateKey = MediaConfig::VideoRtpClockRate;
                if (md.mediaType() == "audio") {
                        destKey = MediaConfig::AudioRtpDestination;
                        ptKey = MediaConfig::AudioRtpPayloadType;
                        rateKey = MediaConfig::AudioRtpClockRate;
                } else if (md.mediaType() != "video") {
                        destKey = MediaConfig::DataRtpDestination;
                        ptKey = MediaConfig::DataRtpPayloadType;
                        rateKey = MediaConfig::DataRtpClockRate;
                        cfg.set(MediaConfig::DataEnabled, true);
                }

                // Destination: only fill in if the caller did not
                // already set one explicitly.
                SocketAddress existingDest = cfg.getAs<SocketAddress>(destKey, SocketAddress());
                if (existingDest.isNull()) {
                        String connection =
                                md.connectionAddress().isEmpty() ? sessionConnection : md.connectionAddress();
                        if (!connection.isEmpty()) {
                                Result<NetworkAddress> nr = NetworkAddress::fromString(connection);
                                if (nr.second().isOk()) {
                                        SocketAddress derived(nr.first(), md.port());
                                        cfg.set(destKey, derived);
                                }
                        }
                }

                // Payload type, clock rate, audio channel count.
                SdpMediaDescription::RtpMap rm = md.rtpMap();
                if (rm.valid) {
                        cfg.set(ptKey, static_cast<int>(rm.payloadType));
                        cfg.set(rateKey, static_cast<int>(rm.clockRate));
                        if (md.mediaType() == "video") {
                                // Stash the rtpmap encoding so
                                // configureVideoStream can pick the right
                                // payload class even when the SDP carries
                                // no geometry (H.264 / H.265 — width and
                                // height live in the bitstream's SPS).
                                cfg.set(MediaConfig::VideoRtpEncoding, rm.encoding);
                        } else if (md.mediaType() == "audio") {
                                if (cfg.getAs<float>(MediaConfig::AudioRate, 0.0f) <= 0.0f) {
                                        cfg.set(MediaConfig::AudioRate, static_cast<float>(rm.clockRate));
                                }
                                if (cfg.getAs<int>(MediaConfig::AudioChannels, 0) <= 0) {
                                        cfg.set(MediaConfig::AudioChannels, static_cast<int>(rm.channels));
                                }
                        }
                }

                // Parse ts-refclk into a ClockDomain for this stream.
                // RFC 7273 defines the attribute; ST 2110-10 mandates
                // ptp=IEEE1588-2008 or ptp=IEEE1588-2019.  If absent,
                // fall back to SystemMonotonic (library wall clock).
                {
                        Stream *stream = nullptr;
                        // Each m= in the SDP maps to one entry on the
                        // matching list (writer-side or reader-side
                        // depending on @c _readerMode).  applySdp runs
                        // before the per-kind configure helpers, so
                        // the lists are empty on first contact; push
                        // a placeholder entry so the ts-refclk fields
                        // can land on the right object.
                        // configure*Stream will populate the rest of
                        // the entry.
                        if (md.mediaType() == "video") {
                                if (_readerMode) {
                                        if (_videoReaders.isEmpty()) {
                                                _videoReaders.pushToBack(VideoReaderStream{});
                                                _videoReaders.back().mediaType = "video";
                                        }
                                        stream = &_videoReaders.back();
                                } else {
                                        if (_videos.isEmpty()) {
                                                _videos.pushToBack(VideoStream{});
                                                _videos.back().mediaType = "video";
                                        }
                                        stream = &_videos.back();
                                }
                        } else if (md.mediaType() == "audio") {
                                if (_readerMode) {
                                        if (_audioReaders.isEmpty()) {
                                                _audioReaders.pushToBack(AudioReaderStream{});
                                                _audioReaders.back().mediaType = "audio";
                                        }
                                        stream = &_audioReaders.back();
                                } else {
                                        if (_audios.isEmpty()) {
                                                _audios.pushToBack(AudioStream{});
                                                _audios.back().mediaType = "audio";
                                        }
                                        stream = &_audios.back();
                                }
                        } else {
                                if (_readerMode) {
                                        if (_dataReaders.isEmpty()) {
                                                _dataReaders.pushToBack(DataReaderStream{});
                                                _dataReaders.back().mediaType = "application";
                                        }
                                        stream = &_dataReaders.back();
                                } else {
                                        if (_datas.isEmpty()) {
                                                _datas.pushToBack(DataStream{});
                                                _datas.back().mediaType = "application";
                                        }
                                        stream = &_datas.back();
                                }
                        }
                        String tsRefClk = md.attribute("ts-refclk");
                        if (!tsRefClk.isEmpty() && tsRefClk.startsWith("ptp=")) {
                                // "ptp=IEEE1588-2008:AA-BB-CC-DD-EE-FF-00-11"
                                // or "ptp=IEEE1588-2008" (no grandmaster ID)
                                // Domain identity is the PTP profile; the
                                // grandmaster is per-essence metadata that can
                                // change due to BMCA failover.
                                String     ptpValue = tsRefClk.split("=")[1];
                                String     profile = ptpValue;
                                StringList parts = ptpValue.split(":");
                                if (parts.size() == 2) {
                                        profile = parts[0];
                                        auto [gm, gmErr] = EUI64::fromString(parts[1]);
                                        if (gmErr.isOk()) {
                                                stream->ptpGrandmaster = gm;
                                        }
                                }
                                ClockDomain::ID cdId = ClockDomain::registerDomain(
                                        String("ptp.") + profile, String("PTP reference clock (") + tsRefClk + ")",
                                        ClockEpoch::Absolute);
                                stream->clockDomain = ClockDomain(cdId);
                        } else if (!tsRefClk.isEmpty() && tsRefClk.startsWith("local")) {
                                stream->clockDomain = ClockDomain(ClockDomain::registerDomain(
                                        "local", "SDP ts-refclk:local", ClockEpoch::Correlated));
                        } else {
                                stream->clockDomain = ClockDomain::SystemMonotonic;
                        }
                }

                // Stash the raw fmtp for the video stream so the
                // deferred JPEG geometry path can read colorimetry
                // and RANGE from it.
                if (md.mediaType() == "video") {
                        auto fmtp = md.fmtpParameters();
                        // Rebuild a semicolon-separated string from
                        // the parsed key=value pairs so the
                        // downstream code can re-parse it without
                        // needing the original SdpMediaDescription.
                        String fmtpStr;
                        for (auto it = fmtp.begin(); it != fmtp.end(); ++it) {
                                if (!fmtpStr.isEmpty()) fmtpStr += String(";");
                                fmtpStr += it->first;
                                if (!it->second.isEmpty()) {
                                        fmtpStr += String("=") + it->second;
                                }
                        }
                        if (!fmtpStr.isEmpty()) {
                                cfg.set(MediaConfig::VideoRtpFmtp, fmtpStr);
                        }
                }
        }
        return Error::Ok;
}

// ----- Reader packet callbacks (called on per-stream RX thread) -----

void RtpMediaIO::refreshStreamClock(ReaderStream &s) {
        if (s.session == nullptr) return;
        const RtpSession::ReceivedSr sr = s.session->receivedSr();
        if (!sr.valid) return;
        // Cheap "has a fresh SR landed?" check — the RX thread stamps
        // arrivedAt on every parsed SR, so a non-equal timestamp
        // means new SR data we haven't folded into the stream clock
        // yet.  This avoids rebuilding the RtpStreamClock on every
        // packet for the steady-state case where SRs are typically
        // 5 seconds apart.
        if (s.hasSr && sr.arrivedAt == s.lastSrArrivedAt) return;
        s.streamClock = RtpStreamClock(sr.ntp, sr.rtpTs, s.session->clockRate());
        s.lastSrArrivedAt = sr.arrivedAt;
        s.hasSr = true;
}

TimeStamp RtpMediaIO::ntpToSteady(const NtpTime &ntp) const {
        if (!_readerAgg.hasAnchor) return TimeStamp();
        // Compute the signed difference (ntp - ntpAnchor) in 1/2^32 s
        // units, then convert to nanoseconds.  Using @c uint64_t
        // subtraction with a deliberate cast to @c int64_t lets the
        // sign survive when @p ntp predates the anchor (which can
        // happen for low-latency receivers picking up the very first
        // SR while the open path is still settling).
        const uint64_t a = (static_cast<uint64_t>(_readerAgg.ntpAnchor.seconds()) << 32) |
                           static_cast<uint64_t>(_readerAgg.ntpAnchor.fraction());
        const uint64_t t = (static_cast<uint64_t>(ntp.seconds()) << 32) |
                           static_cast<uint64_t>(ntp.fraction());
        const int64_t diffFractional = static_cast<int64_t>(t - a);
        // Split into whole-second and sub-second halves so the
        // multiply by 1e9 doesn't overflow on a multi-decade offset.
        const int64_t  wholeSec = diffFractional >> 32; // arithmetic shift
        const uint32_t subFracU = static_cast<uint32_t>(diffFractional & 0xFFFFFFFFu);
        const int64_t  subNs = (static_cast<int64_t>(subFracU) * 1'000'000'000LL) >> 32;
        const int64_t  totalNs = wholeSec * 1'000'000'000LL + subNs;
        return _readerAgg.steadyAnchor + Duration::fromNanoseconds(totalNs);
}

void RtpMediaIO::onVideoPacket(const RtpPacket &pkt) {
        if (_videoReaders.isEmpty()) return;
        VideoReaderStream &v = _videoReaders[0];
        if (!v.active) return;

        v.packetsReceived.fetchAndAdd(1);
        v.bytesReceived.fetchAndAdd(static_cast<int64_t>(pkt.size()));

        // Pick up any newly-arrived SR for this stream so the next
        // emitVideoFrame stamps Frame::captureTime from a current
        // mapping.  Cheap; runs on the video RX thread.
        refreshStreamClock(v);

        // Sync gate: when the receiver joins a stream that's
        // already in progress, the first packets it sees belong to
        // the tail of an incomplete frame.  Passing those to
        // emitVideoFrame produces a truncated bitstream that the
        // codec rejects ("decoder_init failed").  We stay in a
        // pre-sync state until the first marker bit arrives.  At
        // that point we transition to synced but let the normal
        // accumulation / emit path below handle the frame — if the
        // receiver happened to catch the first packet of a frame
        // (clean start), the reassembled data is complete and
        // decodes correctly; if it joined mid-frame, the data is
        // partial and emitVideoFrame's existing size / validity
        // checks drop it silently.  Either way, the NEXT frame
        // after the marker is guaranteed clean.
        if (!v.reasmSynced) {
                if (pkt.marker()) {
                        v.reasmSynced = true;
                        // Fall through — let the normal path below
                        // accumulate this marker packet and call
                        // emitVideoFrame.  Clean starts succeed;
                        // mid-joins produce a short/corrupt buffer
                        // that emitVideoFrame discards.
                }
                // else: still pre-sync, no marker yet.  Accumulate
                // anyway (the frame MIGHT be complete if this is a
                // clean start) — the marker will tell us.
        }

        // Diagnostic timing capture (RX thread is the only writer
        // here, so the histograms need no internal locking).
        // rxPacketInterval gives the inter-packet arrival cadence
        // observed on the wire — a tight cluster around the
        // sender's per-packet spacing means the network and the
        // local socket are not introducing jitter; a long tail
        // points at receive-side stalls.  rxFrameStartTime marks
        // the first packet of the current reassembly window so
        // emitVideoFrame can compute the per-frame assemble time.
        const TimeStamp now = TimeStamp::now();
        if (v.rxHasLastPacket) {
                const Duration delta = now - v.rxLastPacketTime;
                v.rxPacketInterval.addSample(delta.microseconds());
        }
        v.rxLastPacketTime = now;
        v.rxHasLastPacket = true;

        if (v.reasmHasTimestamp && v.reasmTimestamp != pkt.timestamp() && !v.reasmPackets.isEmpty()) {
                // Timestamp changed without a prior marker bit —
                // emit whatever we have and start fresh.
                emitVideoFrame();
        }

        // Mark the start of a new reassembly window if this packet
        // is the first one for its timestamp.
        if (v.reasmPackets.isEmpty()) {
                v.rxFrameStartTime = now;
                v.rxHasFrameStart = true;
        }

        // Copy the packet into our reassembly list.  The incoming
        // RtpPacket is a view onto the receive thread's scratch
        // buffer, which is freshly allocated per packet, so we can
        // just take ownership of its backing Buffer by
        // referencing the same RtpPacket.
        v.reasmPackets.pushToBack(pkt);
        v.reasmTimestamp = pkt.timestamp();
        v.reasmHasTimestamp = true;
        v.reasmLastSeq = pkt.sequenceNumber();
        v.reasmHaveLastSeq = true;

        if (pkt.marker()) {
                emitVideoFrame();
        }
}

void RtpMediaIO::emitVideoFrame() {
        if (_videoReaders.isEmpty()) return;
        VideoReaderStream &v = _videoReaders[0];
        if (v.reasmPackets.isEmpty()) return;
        if (v.payload == nullptr) {
                v.reasmPackets.clear();
                v.reasmHasTimestamp = false;
                return;
        }

        // Deferred JPEG geometry: peek at the first packet's
        // RFC 2435 header for the Type field (subsampling) before
        // unpack() consumes the packet list.  Type is at byte 4
        // of the 8-byte payload header:
        //   Type 0 → YCbCr 4:2:2  (FFmpeg convention)
        //   Type 1 → YCbCr 4:2:0
        uint8_t rfc2435Type = 0;
        if (!v.readerImageDesc.isValid() && !v.reasmPackets.isEmpty()) {
                const RtpPacket &first = v.reasmPackets[0];
                if (!first.isNull() && first.payloadSize() >= 8) {
                        rfc2435Type = first.payload()[4];
                }
        }

        // Capture per-frame RTP metadata before unpack clears the list.
        const uint32_t frameRtpTimestamp = v.reasmTimestamp;
        const int32_t  framePacketCount = static_cast<int32_t>(v.reasmPackets.size());

        // Ask the payload class to reassemble the bitstream.
        Buffer reassembled = v.payload->unpack(v.reasmPackets);
        v.reasmPackets.clear();
        v.reasmHasTimestamp = false;

        if (reassembled.size() == 0) return;

        // Deferred geometry for JPEG reader mode: the SDP carries
        // no image dimensions for RFC 2435, so we discover them
        // from the first reassembled JFIF.  The SOF0 marker
        // (FF C0) gives exact width/height (no 2040-pixel cap)
        // and the component sampling factors confirm subsampling.
        // The RFC 2435 Type byte (extracted above) provides the
        // authoritative subsampling when the SOF0 is ambiguous.
        //
        // Color model defaults to Rec.601 full range per the JFIF
        // spec, but can be overridden by ST 2110-20 colorimetry
        // and RANGE parameters in the SDP fmtp line (stashed on
        // v.fmtp by configureVideoStream).
        if (!v.readerImageDesc.isValid()) {
                const uint8_t *p = static_cast<const uint8_t *>(reassembled.data());
                const size_t   n = reassembled.size();
                uint32_t       w = 0, h = 0;
                int            nf = 0;  // component count from SOF0
                uint8_t        ySf = 0; // Y sampling factor byte (Hi<<4|Vi)
                for (size_t i = 0; i + 1 < n; i++) {
                        if (p[i] != 0xFF) continue;
                        // SOF0 (0xC0) or SOF2 (0xC2, progressive)
                        if (p[i + 1] != 0xC0 && p[i + 1] != 0xC2) continue;
                        if (i + 9 >= n) break;
                        h = (static_cast<uint32_t>(p[i + 5]) << 8) | p[i + 6];
                        w = (static_cast<uint32_t>(p[i + 7]) << 8) | p[i + 8];
                        nf = p[i + 9];
                        if (nf >= 1 && i + 11 < n) {
                                ySf = p[i + 11]; // Hi/Vi of first component
                        }
                        break;
                }
                if (w == 0 || h == 0) return;

                // Determine subsampling and RGB from SOF0 / RFC 2435.
                bool is420 = false;
                bool isRgb = false;
                if (nf == 1) {
                        is420 = true; // grayscale
                } else if (nf == 3 && ySf == 0x11 && rfc2435Type >= 2) {
                        isRgb = true;
                } else if (ySf == 0x22) {
                        is420 = true;
                } else if (ySf == 0x21) {
                        is420 = false;
                } else {
                        is420 = (rfc2435Type == 1);
                }

                // Parse colorimetry and RANGE from the SDP fmtp
                // (stashed on v.fmtp).  When absent, the
                // helper defaults to Rec.601 full range.
                String colorimetry;
                String range;
                if (!v.fmtp.isEmpty()) {
                        // Quick parse of "key=val;key=val;..." form.
                        StringList parts = v.fmtp.split(";");
                        for (size_t i = 0; i < parts.size(); i++) {
                                StringList kv = parts[i].split("=");
                                if (kv.size() < 2) continue;
                                String key = kv[0].trim();
                                String val = kv[1].trim();
                                if (key == "colorimetry")
                                        colorimetry = val;
                                else if (key == "RANGE")
                                        range = val;
                        }
                }

                PixelFormat::ID pdId = ImageDesc::jpegPixelFormatFromSdp(colorimetry, range, is420, isRgb);
                v.readerImageDesc = ImageDesc(Size2Du32(w, h), PixelFormat(pdId));
                promekiInfo("RtpMediaIO: JPEG reader discovered "
                            "%ux%u %s from first frame",
                            w, h, PixelFormat(pdId).name().cstr());
        }

        // Build a payload from the reassembled buffer.  Both the
        // compressed and uncompressed paths copy the reassembled
        // bytes into a fresh Buffer that the payload adopts as
        // plane 0.
        Buffer plane = Buffer(reassembled.size());
        std::memcpy(plane.data(), reassembled.data(), reassembled.size());
        plane.setSize(reassembled.size());
        const PixelFormat &pd = v.readerImageDesc.pixelFormat();

        v.framesReceived++;

        // Record assemble time (first packet -> here) and the
        // wall-clock interval between successive complete frames.
        const TimeStamp emitTime = TimeStamp::now();
        if (v.rxHasFrameStart) {
                const Duration assemble = emitTime - v.rxFrameStartTime;
                v.rxFrameAssembleTime.addSample(assemble.microseconds());
                v.rxHasFrameStart = false;
        }
        if (v.rxHasLastFrame) {
                const Duration delta = emitTime - v.rxLastFrameTime;
                v.rxFrameInterval.addSample(delta.microseconds());
        }
        v.rxLastFrameTime = emitTime;
        v.rxHasLastFrame = true;

        // Stamp the payload with RTP and capture metadata before
        // handing it to the Frame.  CaptureTime is when the library
        // saw the first packet of this frame (rxFrameStartTime); the
        // payload's native pts/dts are set from the same value below.
        MediaTimeStamp capMts(v.rxFrameStartTime, v.clockDomain);
        ImageDesc      idesc = v.readerImageDesc;
        {
                Metadata &m = idesc.metadata();
                m.set(Metadata::CaptureTime, capMts);
                m.set(Metadata::RtpTimestamp, frameRtpTimestamp);
                m.set(Metadata::RtpPacketCount, framePacketCount);
                if (!v.ptpGrandmaster.isNull()) {
                        m.set(Metadata::PtpGrandmasterId, v.ptpGrandmaster);
                }
        }

        VideoPayload::Ptr videoPayload;
        if (pd.isCompressed()) {
                // Compressed streams: stamp Keyframe by codec rule.
                //   * Intra-only (JPEG, JPEG XS, ProRes, DNxHD): every
                //     access unit is independently decodable, so always
                //     a keyframe.
                //   * Temporal (H.264, HEVC, AV1): only IDR (H.264) /
                //     IRAP (HEVC) access units are safe random-access
                //     points.  We inspect the reassembled bitstream and
                //     stamp Keyframe only for those.  A future
                //     out-of-band parameter-set delivery path can keep
                //     this stamping accurate without parsing on every
                //     frame, but for in-band-only streams this is the
                //     right semantics.
                bool isKeyframe = true;
                const VideoCodec &codec = pd.videoCodec();
                if (codec.isValid() && codec.codingType() == VideoCodec::CodingTemporal) {
                        BufferView auView(plane, 0, plane.size());
                        if (codec.id() == VideoCodec::H264) {
                                isKeyframe = AvcDecoderConfig::isIdrAnnexB(auView);
                        } else if (codec.id() == VideoCodec::HEVC) {
                                isKeyframe = HevcDecoderConfig::isIrapAnnexB(auView);
                        } else {
                                // Unknown temporal codec: be conservative
                                // and don't stamp Keyframe — downstream
                                // can still decode, just won't treat the
                                // packet as a safe cut point.
                                isKeyframe = false;
                        }
                }
                auto cvp = CompressedVideoPayload::Ptr::create(idesc, plane);
                cvp.modify()->setPts(capMts);
                cvp.modify()->setDts(capMts);
                if (isKeyframe) cvp.modify()->addFlag(MediaPayload::Keyframe);
                videoPayload = cvp;
        } else {
                BufferView planes;
                planes.pushToBack(plane, 0, plane.size());
                auto uvp = UncompressedVideoPayload::Ptr::create(idesc, planes);
                uvp.modify()->setPts(capMts);
                videoPayload = uvp;
        }

        if (!videoPayload.isValid()) {
                if (v.framesReceived <= 1) {
                        promekiDebug("RtpMediaIO: discarding "
                                     "first partial video frame "
                                     "(joined stream mid-flight)");
                } else {
                        promekiWarn("RtpMediaIO: reassembled "
                                    "video frame is invalid");
                }
                return;
        }

        Frame frame = Frame();
        frame.addPayload(std::move(videoPayload));

        // Stamp Frame::captureTime from the video stream's
        // wallclock NTP (sender's capture instant for this frame),
        // converted to a local @ref TimeStamp via the @c (steady,
        // NTP) anchor pinned at open time.  Downstream backends and
        // the SDL player honour @ref Frame::captureTime for sync,
        // so this is what carries cross-stream alignment from the
        // sender to the local pipeline.  When no SR has arrived yet
        // we fall back to the per-essence @c rxFrameStartTime
        // (first-packet-arrival) the existing code stamps —
        // receivers still produce output during the brief startup
        // window before the first SR.
        bool wallclockCapture = false;
        if (v.hasSr && v.streamClock.isValid()) {
                const NtpTime  frameNtp = v.streamClock.toNtp(frameRtpTimestamp);
                const TimeStamp steady = ntpToSteady(frameNtp);
                if (steady.nanoseconds() != 0) {
                        frame.setCaptureTime(MediaTimeStamp(steady, ClockDomain::SystemMonotonic));
                        wallclockCapture = true;
                }
        }
        if (!frame.captureTime().isValid()) {
                frame.setCaptureTime(MediaTimeStamp(v.rxFrameStartTime, v.clockDomain));
        }

        // Aggregate audio: drain one frame's worth of samples from
        // the FIFO that the audio RX thread is filling.  This runs
        // on the video receive thread, so any blocking here also
        // blocks @c recvfrom() on the video socket — long enough
        // for the kernel's UDP ring to overflow and eat upstream
        // packets.  At the cadence we run (audio packets every 1 ms,
        // video frames every ~17 ms) the audio FIFO is normally
        // already populated by the time the video marker arrives, so
        // a non-blocking pop suffices.  If samples are short — the
        // first frame of a stream is the obvious case — we emit the
        // frame video-only rather than wait, leaving downstream
        // consumers to notice the missing audio chunk via the
        // standard "no audio payload" path.
        //
        // **Wallclock-aligned drain.**  When both the video and audio
        // streams have a valid @ref RtpStreamClock (an SR has been
        // observed for each), the FIFO is drained for the audio
        // window @c [T, T+frameDuration) where @c T is the video
        // packet's wallclock instant.  Audio samples whose RTP-TS
        // precedes the target window are dropped — this realigns
        // a receiver that joined the streams at slightly different
        // wallclock instants without leaking the misalignment into
        // every emitted Frame.  When either stream lacks an SR yet,
        // we fall back to the per-frame-index @c samplesPerFrame
        // drain so the reader still produces output at startup.
        // Aggregator pulls samples from the first audio stream's FIFO
        // because the strand emits a single combined Frame per video
        // frame.  Multi-audio reader support is a future change that
        // adds parallel FIFOs (one per audio stream) and emits one
        // PcmAudioPayload per stream into the same Frame.
        if (!_audioReaders.isEmpty() && _audioReaders[0].active &&
            _audioReaders[0].readerAudioDesc.isValid()) {
                AudioReaderStream &as0 = _audioReaders[0];
                refreshStreamClock(as0);
                const size_t needed = _frameRate.samplesPerFrame(
                        static_cast<int64_t>(as0.readerAudioDesc.sampleRate()),
                        _readerAgg.videoFrameIndex.value());

                bool wallclockReady = v.hasSr && v.streamClock.isValid() && as0.hasSr &&
                                      as0.streamClock.isValid() && _readerAgg.audioFifoHasFront;

                if (wallclockReady && needed > 0) {
                        const NtpTime  frameNtp = v.streamClock.toNtp(frameRtpTimestamp);
                        const uint32_t targetAudioRtpTs = as0.streamClock.toRtpTs(frameNtp);
                        // Signed delta = target - frontRtpTs in
                        // modular @c uint32_t units.  Positive means
                        // the FIFO front is "behind" the video's
                        // wallclock (drop those samples — they're
                        // older than T).  Negative (i.e. delta cast
                        // to int32_t) means the FIFO front is
                        // already past the target, which happens
                        // when audio has started arriving before
                        // video — we keep the front and emit
                        // immediately.
                        const uint32_t rawDelta = targetAudioRtpTs - _readerAgg.audioFifoFrontRtpTs;
                        const int32_t  signedDelta = static_cast<int32_t>(rawDelta);
                        if (signedDelta > 0) {
                                size_t toDrop = static_cast<size_t>(signedDelta);
                                if (toDrop > _readerAgg.audioFifo.available()) {
                                        toDrop = _readerAgg.audioFifo.available();
                                }
                                if (toDrop > 0) {
                                        auto [dropped, derr] = _readerAgg.audioFifo.drop(toDrop);
                                        (void)derr;
                                        _readerAgg.audioFifoFrontRtpTs +=
                                                static_cast<uint32_t>(dropped);
                                }
                        }
                }

                if (needed > 0 && _readerAgg.audioFifo.available() >= needed) {
                        size_t bufBytes = as0.readerAudioDesc.bufferSize(needed);
                        Buffer pcm = Buffer(bufBytes);
                        auto [got, err] = _readerAgg.audioFifo.pop(pcm.data(), needed);
                        if (got > 0) {
                                _readerAgg.audioFifoFrontRtpTs += static_cast<uint32_t>(got);
                                size_t usedBytes = as0.readerAudioDesc.bufferSize(got);
                                pcm.setSize(usedBytes);
                                BufferView view(pcm, 0, usedBytes);
                                auto audioPayload = PcmAudioPayload::Ptr::create(as0.readerAudioDesc, got, view);
                                ClockDomain audioCd =
                                        as0.clockDomain.isValid() ? as0.clockDomain : v.clockDomain;
                                // Audio's per-payload pts gets the
                                // same wallclock-aware capture-time
                                // as the Frame when both clocks are
                                // valid; otherwise we keep the
                                // pre-existing first-packet-arrival
                                // stamp so behavior pre-SR is
                                // unchanged.
                                MediaTimeStamp audMts;
                                if (wallclockCapture) {
                                        audMts = frame.captureTime();
                                } else {
                                        audMts = MediaTimeStamp(v.rxFrameStartTime, audioCd);
                                }
                                audioPayload.modify()->desc().metadata().set(Metadata::CaptureTime, audMts);
                                audioPayload.modify()->setPts(audMts);
                                frame.addPayload(audioPayload);
                        }
                }
        }
        _readerAgg.videoFrameIndex++;

        // Aggregate metadata: grab the latest snapshot from the
        // data RX thread and merge it into this frame.
        if (!_dataReaders.isEmpty() && _dataReaders[0].active) {
                Mutex::Locker lock(_readerAgg.dataMutex);
                if (_readerAgg.hasMetadata) {
                        frame.metadata() = _readerAgg.pendingMetadata;
                        _readerAgg.hasMetadata = false;
                }
        }

        pushReaderFrame(std::move(frame));
}

void RtpMediaIO::onAudioPacket(const RtpPacket &pkt) {
        // First audio stream only today — multi-stream reader
        // plumbing (per-port RX threads dispatching to the matching
        // AudioReaderStream) is a future change.  The single-stream
        // path is also what drives the existing aggregator + audio-
        // only fallback below.
        if (_audioReaders.isEmpty()) return;
        AudioReaderStream &as = _audioReaders[0];
        if (!as.active) return;
        as.packetsReceived.fetchAndAdd(1);
        as.bytesReceived.fetchAndAdd(static_cast<int64_t>(pkt.size()));

        // Pick up any newly-arrived SR for this stream so the
        // wallclock-aligned drain in emitVideoFrame has a fresh
        // RtpStreamClock to map FIFO position → wallclock NTP.
        refreshStreamClock(as);

        // L16 arrives as big-endian samples directly.  Push them
        // into the aggregator's audio FIFO so emitVideoFrame can
        // drain one frame's worth of samples when the next video
        // frame completes.  The FIFO is protected by a Mutex and
        // a WaitCondition because this runs on the audio RX thread
        // while emitVideoFrame runs on the video RX thread.
        if (as.payload == nullptr || !as.readerAudioDesc.isValid()) return;
        if (pkt.payloadSize() == 0) return;

        const unsigned int ch = as.readerAudioDesc.channels();
        constexpr size_t   bytesPerSample = 2;
        const size_t       frameBytes = ch * bytesPerSample;
        if (frameBytes == 0) return;
        const size_t samples = pkt.payloadSize() / frameBytes;
        if (samples == 0) return;

        AudioDesc wireDesc(AudioFormat::PCMI_S16BE, as.readerAudioDesc.sampleRate(), ch);

        // When video is active, push samples into the aggregation
        // FIFO so emitVideoFrame can merge them into combined
        // frames.  When video is NOT active (audio-only stream),
        // fall back to the original per-chunk emission so the
        // reader queue still produces output.
        const bool videoActive = !_videoReaders.isEmpty() && _videoReaders[0].active;
        if (videoActive) {
                // Track the RTP-TS of the front sample currently in
                // the FIFO.  emitVideoFrame uses this together with
                // @ref as.streamClock to map any FIFO position to a
                // wallclock instant.  The packet's RTP-TS is the
                // RTP-TS of its first sample; if the FIFO already
                // holds samples and the packet does NOT continue
                // contiguously after them, we treat it as a stream
                // realignment — drop the FIFO and reseed the front.
                const uint32_t pktRtpTs = pkt.timestamp();
                const size_t   fifoSamplesBefore = _readerAgg.audioFifo.available();
                if (!_readerAgg.audioFifoHasFront) {
                        _readerAgg.audioFifoFrontRtpTs = pktRtpTs;
                        _readerAgg.audioFifoHasFront = true;
                } else {
                        const uint32_t expectedNext = _readerAgg.audioFifoFrontRtpTs +
                                                      static_cast<uint32_t>(fifoSamplesBefore);
                        if (pktRtpTs != expectedNext) {
                                // Out-of-order or gapped packet —
                                // realign the FIFO.  Wallclock-
                                // aligned aggregation is more useful
                                // with a clean front than a stale
                                // gap, so we drop and reseed rather
                                // than try to splice silence into the
                                // missing window.
                                _readerAgg.audioFifo.clear();
                                _readerAgg.audioFifoFrontRtpTs = pktRtpTs;
                        }
                }
                Error perr = _readerAgg.audioFifo.push(pkt.payload(), samples, wireDesc);
                if (perr.isError()) {
                        promekiWarn("RtpMediaIO: audio FIFO push failed: %s", perr.desc().cstr());
                        return;
                }
                as.framesReceived++;
        } else {
                // Audio-only stream — push directly.  Chunk into
                // samplesPerFrame-sized Audio objects to match the
                // frame-rate cadence downstream consumers expect.
                Error perr = as.fifo.push(pkt.payload(), samples, wireDesc);
                if (perr.isError()) {
                        promekiWarn("RtpMediaIO: audio FIFO push failed: %s", perr.desc().cstr());
                        return;
                }
                const double fps = _frameRate.isValid() ? _frameRate.toDouble() : 30.0;
                if (fps <= 0.0) return;
                const size_t spf = static_cast<size_t>(as.readerAudioDesc.sampleRate() / fps);
                if (spf == 0) return;
                while (as.fifo.available() >= spf) {
                        size_t bufBytes = as.readerAudioDesc.bufferSize(spf);
                        Buffer pcm = Buffer(bufBytes);
                        auto [got, popErr] = as.fifo.pop(pcm.data(), spf);
                        if (popErr.isError() || got == 0) break;
                        size_t usedBytes = as.readerAudioDesc.bufferSize(got);
                        pcm.setSize(usedBytes);
                        BufferView     view(pcm, 0, usedBytes);
                        auto           audioPayload = PcmAudioPayload::Ptr::create(as.readerAudioDesc, got, view);
                        MediaTimeStamp capMts(TimeStamp::now(), as.clockDomain);
                        audioPayload.modify()->desc().metadata().set(Metadata::CaptureTime, capMts);
                        audioPayload.modify()->setPts(capMts);
                        as.framesReceived++;
                        Frame frame = Frame();
                        frame.addPayload(audioPayload);
                        pushReaderFrame(std::move(frame));
                }
        }
}

void RtpMediaIO::onDataPacket(const RtpPacket &pkt) {
        if (_dataReaders.isEmpty()) return;
        DataReaderStream &d = _dataReaders[0];
        if (!d.active) return;
        d.packetsReceived.fetchAndAdd(1);
        d.bytesReceived.fetchAndAdd(static_cast<int64_t>(pkt.size()));

        refreshStreamClock(d);

        if (d.reasmHasTimestamp && d.reasmTimestamp != pkt.timestamp() && !d.reasmPackets.isEmpty()) {
                emitDataMessage();
        }

        d.reasmPackets.pushToBack(pkt);
        d.reasmTimestamp = pkt.timestamp();
        d.reasmHasTimestamp = true;

        if (pkt.marker()) {
                emitDataMessage();
        }
}

void RtpMediaIO::emitDataMessage() {
        if (_dataReaders.isEmpty()) return;
        DataReaderStream &d = _dataReaders[0];
        if (d.reasmPackets.isEmpty()) return;
        if (d.payload == nullptr) {
                d.reasmPackets.clear();
                d.reasmHasTimestamp = false;
                return;
        }
        // Capture per-message RTP metadata before unpack clears the list.
        const uint32_t dataRtpTimestamp = d.reasmTimestamp;
        const int32_t  dataPacketCount = static_cast<int32_t>(d.reasmPackets.size());

        Buffer bytes = d.payload->unpack(d.reasmPackets);
        d.reasmPackets.clear();
        d.reasmHasTimestamp = false;
        if (bytes.size() == 0) return;

        String     jsonText(static_cast<const char *>(bytes.data()), bytes.size());
        Error      jerr;
        JsonObject obj = JsonObject::parse(jsonText, &jerr);
        if (jerr.isError()) {
                promekiWarn("RtpMediaIO: dropping malformed metadata JSON: %s", jerr.desc().cstr());
                return;
        }
        Metadata m = Metadata::fromJson(obj);
        d.framesReceived++;

        // Stamp the metadata with capture and RTP timing.
        MediaTimeStamp capMts(TimeStamp::now(), d.clockDomain);
        m.set(Metadata::CaptureTime, capMts);
        m.set(Metadata::RtpTimestamp, dataRtpTimestamp);
        m.set(Metadata::RtpPacketCount, dataPacketCount);

        // When video is active, stash metadata so emitVideoFrame
        // can merge it into combined frames.  When video is NOT
        // active (data-only or audio+data stream), push directly.
        const bool videoActive = !_videoReaders.isEmpty() && _videoReaders[0].active;
        if (videoActive) {
                Mutex::Locker lock(_readerAgg.dataMutex);
                _readerAgg.pendingMetadata = m;
                _readerAgg.hasMetadata = true;
        } else {
                Frame frame = Frame();
                frame.metadata() = m;
                pushReaderFrame(std::move(frame));
        }
}

void RtpMediaIO::pushReaderFrame(Frame frame) {
        if (!frame.isValid()) return;
        // Enforce the configured reader queue depth by dropping the
        // oldest frame when the queue is full.  The producer side
        // is our own RX thread, and stalling it would mean dropped
        // wire packets — dropping at the Frame boundary is the
        // safer failure mode for live streams.
        if (_readerMaxDepth > 0 && static_cast<int>(_readerQueue.size()) >= _readerMaxDepth) {
                (void)_readerQueue.tryPop();
                noteFrameDropped(portGroup(0));
        }
        _readerQueue.push(std::move(frame));
        _readerFramesReceived++;
}

// ----- Command dispatch -----

Error RtpMediaIO::executeCmd(MediaIOCommandOpen &cmd) {
        MediaIO::Config cfg = cmd.config;

        // Direction is config-driven via MediaConfig::OpenMode.
        // Default (Read) means reader (source); Write means writer
        // (sink).  RTP supports both in the same backend.
        Enum modeEnum = cfg.get(MediaConfig::OpenMode).asEnum(MediaIOOpenMode::Type);
        const bool isWrite = modeEnum.value() == MediaIOOpenMode::Write.value();
        _readerMode = !isWrite;
        _readCancelled.store(false, std::memory_order_release);

        // Transport-global parameters.
        _localAddress = cfg.getAs<SocketAddress>(MediaConfig::RtpLocalAddress, SocketAddress::any(0));
        _sessionName = cfg.getAs<String>(MediaConfig::RtpSessionName, String("promeki RTP stream"));
        _sessionOrigin = cfg.getAs<String>(MediaConfig::RtpSessionOrigin, String("-"));
        _multicastTTL = cfg.getAs<int>(MediaConfig::RtpMulticastTTL, 64);
        _multicastInterface = cfg.getAs<String>(MediaConfig::RtpMulticastInterface, String());
        _sdpPath = cfg.getAs<String>(MediaConfig::RtpSaveSdpPath, String());

        // Kernel socket-buffer sizing.  Default to 8 MiB on each side
        // — large enough to absorb a frame-sized burst of RFC 4175
        // raw video at 1080p60, but small enough that the kernel will
        // happily honour it when @c net.core.rmem_max / @c wmem_max is
        // raised to a typical broadcast-grade value (16 MiB).  Linux
        // silently doubles the requested SO_RCVBUF / SO_SNDBUF, then
        // clamps to the sysctl ceiling — under-provisioned hosts will
        // see a smaller actual buffer but no error.  Callers that need
        // more (or less) override via the dedicated config keys.
        _recvBufferBytes = cfg.getAs<int>(MediaConfig::RtpRecvBufferBytes, 8 * 1024 * 1024);
        _sendBufferBytes = cfg.getAs<int>(MediaConfig::RtpSendBufferBytes, 8 * 1024 * 1024);

        Error pmErr;
        _pacingMode = cfg.get(MediaConfig::RtpPacingMode).asEnum(RtpPacingMode::Type, &pmErr);
        if (pmErr.isError() || !_pacingMode.hasListedValue()) {
                promekiErr("RtpMediaIO: unknown RTP pacing mode");
                return Error::InvalidArgument;
        }
        // Auto resolves to the best mechanism available on this
        // platform.  Linux gets kernel pacing via SO_MAX_PACING_RATE
        // (zero per-packet CPU cost, honoured by the fq qdisc when
        // active); other platforms fall back to userspace sleep
        // pacing because they cannot offer anything better.  Users
        // who want a specific behaviour (None for loopback tests,
        // TxTime for ST 2110-21 deployments) still set the key
        // explicitly.
        if (_pacingMode.value() == RtpPacingMode::Auto.value()) {
#if defined(PROMEKI_PLATFORM_LINUX)
                _pacingMode = RtpPacingMode::KernelFq;
#else
                _pacingMode = RtpPacingMode::Userspace;
#endif
        }

        _frameRate = cmd.pendingMediaDesc.frameRate();
        if (!_frameRate.isValid()) {
                // Fall back to config; if still absent, the timestamp
                // math still works but downstream receivers may not
                // know the frame rate via SDP.
                _frameRate = cfg.getAs<FrameRate>(MediaConfig::FrameRate, FrameRate(FrameRate::FPS_29_97));
        }

        // Reader-side SDP ingest.  The RtpSdp config key is
        // polymorphic: it accepts either a String (treated as a
        // filesystem path and loaded via SdpSession::fromFile) or
        // an SdpSession (consumed directly, no filesystem access).
        // This lets callers programmatically build an SdpSession
        // and hand it to the reader, or just point at an SDP file
        // on disk — the task handles both the same way.
        //
        // We also accept MediaConfig::Filename as a String fallback
        // for the RtpSdp key so `MediaIO::createForFileRead("foo.sdp")`
        // (which sets Filename to the .sdp path) keeps working via
        // the same code path.
        if (_readerMode) {
                Variant    sdpVar = cfg.get(MediaConfig::RtpSdp);
                SdpSession sdp;
                bool       haveSdp = false;
                if (sdpVar.type() == Variant::TypeSdpSession) {
                        sdp = sdpVar.get<SdpSession>();
                        haveSdp = true;
                } else if (sdpVar.type() == Variant::TypeString) {
                        String path = sdpVar.get<String>();
                        if (!path.isEmpty()) {
                                Result<SdpSession> r = SdpSession::fromFile(path);
                                if (r.second().isError()) {
                                        resetAll();
                                        return r.second();
                                }
                                sdp = r.first();
                                haveSdp = true;
                        }
                }
                // Filename fallback (createForFileRead path).
                if (!haveSdp) {
                        String filename = cfg.getAs<String>(MediaConfig::Filename, String());
                        if (!filename.isEmpty()) {
                                Result<SdpSession> r = SdpSession::fromFile(filename);
                                if (r.second().isError()) {
                                        resetAll();
                                        return r.second();
                                }
                                sdp = r.first();
                                haveSdp = true;
                                // Clear Filename on the working
                                // copy so the rest of the open
                                // path does not try to interpret
                                // it as a media file path.
                                cfg.set(MediaConfig::Filename, String());
                        }
                }
                if (haveSdp) {
                        MediaDesc sdpMd = cmd.pendingMediaDesc;
                        Error     err = applySdp(sdp, cfg, sdpMd);
                        if (err.isError()) {
                                resetAll();
                                return err;
                        }
                        cmd.pendingMediaDesc = sdpMd;
                        if (sdpMd.frameRate().isValid()) {
                                _frameRate = sdpMd.frameRate();
                        }
                }
        }

        _readerJitterMs = cfg.getAs<int>(MediaConfig::RtpJitterMs, 50);
        if (_readerJitterMs <= 0) _readerJitterMs = 50;
        _readerMaxDepth = cfg.getAs<int>(MediaConfig::RtpMaxReadQueueDepth, 4);
        if (_readerMaxDepth <= 0) _readerMaxDepth = 4;
        _readerAgg.audioTimeoutMs = _readerJitterMs;

        // Configure each stream from the media descriptor + per-stream config.
        Error err = configureVideoStream(cfg, cmd.pendingMediaDesc);
        if (err.isError()) {
                resetAll();
                return err;
        }
        err = configureAudioStream(cfg, cmd.pendingMediaDesc);
        if (err.isError()) {
                resetAll();
                return err;
        }
        err = configureDataStream(cfg);
        if (err.isError()) {
                resetAll();
                return err;
        }

        // Set up the reader-side frame aggregator if this is a reader
        // with an audio stream.  The FIFO stores samples in the
        // network wire format (PCMI_S16BE) and is sized for 2 seconds
        // of headroom — enough to absorb a transient burst of audio
        // arriving ahead of the video stream without losing data.
        // Reader-side aggregator FIFO uses the FIRST audio stream as
        // its sample source, since the strand emits a single combined
        // Frame per video frame.  Multi-audio reader support is a
        // future change that adds parallel FIFOs and a richer Frame
        // shape (one PcmAudioPayload per audio stream).
        if (_readerMode && !_audioReaders.isEmpty() && _audioReaders[0].readerAudioDesc.isValid()) {
                const AudioReaderStream &as0 = _audioReaders[0];
                AudioDesc          wireFormat(AudioFormat::PCMI_S16BE, as0.readerAudioDesc.sampleRate(),
                                              as0.readerAudioDesc.channels());
                _readerAgg.audioFifo.setFormat(wireFormat);
                _readerAgg.audioFifo.setInputFormat(wireFormat);
                const size_t headroom = static_cast<size_t>(as0.readerAudioDesc.sampleRate() * 2);
                _readerAgg.audioFifo.reserve(headroom);
                _readerAgg.videoFrameIndex = 0;
                _readerAgg.hasMetadata = false;
                _readerAgg.audioFifoHasFront = false;
                _readerAgg.audioFifoFrontRtpTs = 0;
        }

        // Pin the local @c (steady, wallclock) reference instant for
        // RX-side @ref Frame::captureTime stamping.  Each emitted
        // frame's wallclock NTP — derived from the per-stream
        // @ref RtpStreamClock — is converted back to a local steady
        // @ref TimeStamp via @c steadyAnchor + (frameNtp - ntpAnchor).
        // This pins the steady↔wallclock mapping at one observed
        // instant, which is accurate to the µs class on a normal
        // Linux box and remains stable across the session.  Done
        // unconditionally for the reader-mode branch so the captureTime
        // path is always live, even for video-only or audio-only
        // configs that don't touch the audio FIFO above.
        if (_readerMode) {
                _readerAgg.steadyAnchor = TimeStamp::now();
                _readerAgg.ntpAnchor = NtpTime::now();
                _readerAgg.hasAnchor = true;
        }

        // Enable multicast loopback when the destination is on this
        // host — lets a co-located receiver see our own packets.  Not
        // critical for production but useful for self-tests.
        auto isLocalMulticast = [](const SocketAddress &a) {
                return a.isMulticast() || a.isLoopback();
        };
        bool loopback = false;
        if (_readerMode) {
                for (const VideoReaderStream &vs : _videoReaders) {
                        if (isLocalMulticast(vs.destination)) { loopback = true; break; }
                }
                if (!loopback) {
                        for (const DataReaderStream &ds : _dataReaders) {
                                if (isLocalMulticast(ds.destination)) { loopback = true; break; }
                        }
                }
                if (!loopback) {
                        for (const AudioReaderStream &as : _audioReaders) {
                                if (isLocalMulticast(as.destination)) { loopback = true; break; }
                        }
                }
        } else {
                for (const VideoStream &vs : _videos) {
                        if (isLocalMulticast(vs.destination)) { loopback = true; break; }
                }
                if (!loopback) {
                        for (const DataStream &ds : _datas) {
                                if (isLocalMulticast(ds.destination)) { loopback = true; break; }
                        }
                }
                if (!loopback) {
                        for (const AudioStream &as : _audios) {
                                if (isLocalMulticast(as.destination)) { loopback = true; break; }
                        }
                }
        }

        if (_readerMode) {
                for (VideoReaderStream &vs : _videoReaders) {
                        err = openReaderStream(vs, loopback);
                        if (err.isError()) {
                                resetAll();
                                return err;
                        }
                }
                for (AudioReaderStream &as : _audioReaders) {
                        err = openReaderStream(as, loopback);
                        if (err.isError()) {
                                resetAll();
                                return err;
                        }
                }
                for (DataReaderStream &ds : _dataReaders) {
                        err = openReaderStream(ds, loopback);
                        if (err.isError()) {
                                resetAll();
                                return err;
                        }
                }
        } else {
                for (VideoStream &vs : _videos) {
                        err = openStream(vs, loopback);
                        if (err.isError()) {
                                resetAll();
                                return err;
                        }
                }
                for (size_t i = 0; i < _audios.size(); i++) {
                        AudioStream &as = _audios[i];
                        err = openStream(as, loopback);
                        if (err.isError()) {
                                resetAll();
                                return err;
                        }
                        // Spawn the audio packetizer + TX pair now
                        // that the session / payload / packet shape
                        // are all wired.  AudioPacketizerThread owns
                        // the FIFO and the preroll watermark;
                        // AudioTxThread owns the cadence + silence-
                        // fill rule, so the wire timeline stays
                        // contiguous regardless of source stalls.
                        if (as.active) {
                                auto *tx = new AudioTxThread(this, i);
                                auto *pkt = new AudioPacketizerThread(this, i);
                                pkt->setTx(tx);
                                as.tx = tx;
                                as.packetizer = pkt;
                                tx->start();
                                pkt->start();
                        }
                }
                for (DataStream &ds : _datas) {
                        err = openStream(ds, loopback);
                        if (err.isError()) {
                                resetAll();
                                return err;
                        }
                }
        }

        // At least one stream must be active.
        bool anyVideoActive = false;
        bool anyAudioActive = false;
        bool anyDataActive = false;
        if (_readerMode) {
                for (const VideoReaderStream &vs : _videoReaders) {
                        if (vs.active) { anyVideoActive = true; break; }
                }
                for (const AudioReaderStream &as : _audioReaders) {
                        if (as.active) { anyAudioActive = true; break; }
                }
                for (const DataReaderStream &ds : _dataReaders) {
                        if (ds.active) { anyDataActive = true; break; }
                }
        } else {
                for (const VideoStream &vs : _videos) {
                        if (vs.active) { anyVideoActive = true; break; }
                }
                for (const AudioStream &as : _audios) {
                        if (as.active) { anyAudioActive = true; break; }
                }
                for (const DataStream &ds : _datas) {
                        if (ds.active) { anyDataActive = true; break; }
                }
        }
        if (!anyVideoActive && !anyAudioActive && !anyDataActive) {
                promekiErr("RtpMediaIO: no RTP streams configured "
                           "(set VideoRtpDestination / AudioRtpDestination / DataRtpDestination)");
                resetAll();
                return Error::InvalidArgument;
        }

        // RTCP setup (writer-mode only — readers consume but do not
        // generate SR).  Capture a single wallclock NTP anchor and
        // assign it to every stream's session; this is what makes
        // cross-stream lip-sync work — a receiver gets the same
        // wallclock instant carried in each stream's first SR, so
        // their RTP-clock offsets are immediately recoverable.
        //
        // CNAME is the SDES item that lets receivers identify which
        // streams come from the same sender — when several streams
        // share a CNAME (the typical audio + video pair from one
        // mediaplay process), receivers know to sync them even if
        // their SSRCs are unrelated.
        if (!_readerMode) {
                _rtcpEnabled = cfg.getAs<bool>(MediaConfig::RtpRtcpEnabled, true);
                _rtcpIntervalMs = cfg.getAs<int>(MediaConfig::RtpRtcpIntervalMs, 5000);
                _rtcpCname = cfg.getAs<String>(MediaConfig::RtpRtcpCname, String());
                if (_rtcpCname.isEmpty()) {
                        // Auto-CNAME: stable per-process identifier
                        // shared by every stream this RtpMediaIO emits.
                        // Streams that share a CNAME (the typical
                        // audio + video pair from one mediaplay
                        // process) are correlated by receivers as
                        // belonging to the same sender.
                        _rtcpCname = String("promeki-") + System::hostname() + String("-") +
                                     String::number(static_cast<int64_t>(::getpid()));
                }
                // Seed every active session with a default
                // capture-NTP anchor so even an SR emitted before
                // the first frame has a structurally valid (NTP,
                // RTP_TS) pair.  The very first @c executeCmd(Write)
                // refines this from the inbound Frame's
                // @ref Frame::captureTime, which is the source's
                // authoritative capture instant — receivers that
                // observe a stream's first SR after refinement see
                // the source-capture wallclock rather than open-time
                // wallclock, which is what cross-stream lip-sync
                // depends on.
                const NtpTime defaultAnchorNtp = NtpTime::now();
                _anchorSeeded.setValue(false);
                auto setupSession = [this, &defaultAnchorNtp](Stream &s) {
                        if (!s.active || s.session == nullptr) return;
                        s.session->setCname(_rtcpCname);
                        s.session->setRtpAnchor(defaultAnchorNtp, 0);
                };
                for (VideoStream &vs : _videos) setupSession(vs);
                for (AudioStream &as : _audios) setupSession(as);
                for (DataStream &ds : _datas) setupSession(ds);

                if (_rtcpEnabled) {
                        _rtcpScheduler = new RtcpScheduler(this, _rtcpIntervalMs);
                        _rtcpScheduler->start();
                }
        }

        // Apply kernel-FQ pacing rate if requested (writer-side only —
        // reader does not paces outbound traffic).
        //
        // The pacing rate is what makes @c SO_MAX_PACING_RATE behave
        // as a wall-clock frame-rate enforcer instead of just a
        // bandwidth ceiling: when the cap exactly matches the source
        // bitrate, the kernel @c fq qdisc only releases packets at
        // that rate, the socket send buffer eventually fills, and
        // @c sendmsg blocks the writer in lockstep with the wire
        // schedule.  Any "headroom" above the source rate breaks
        // this — the kernel drains faster than the source produces,
        // the buffer never fills, @c sendmsg never blocks, and the
        // pipeline runs as fast as the encoder will go.  We use the
        // exact bits→bytes conversion (no fudge factor) so the cap
        // matches what the wire actually carries.
        //
        // For VBR compressed video the per-frame size is unknown at
        // open time, so we only honour an explicit user-supplied
        // @c VideoRtpTargetBitrate here — without that, the per-frame
        // update in @ref sendVideo takes over once frames start
        // flowing and sets the rate from each frame's actual byte
        // count.  No hardcoded fallback rate, because picking a
        // wrong fallback (the previous code used 200 Mbps) made
        // the cap so loose that frame-rate pacing was effectively
        // disabled for any compressed format below that bitrate —
        // notably JPEG XS, which is fast enough to outrun wall
        // clock if nothing throttles it.
        //
        // ST 2110-21 Type N/W senders use per-packet @c SCM_TXTIME
        // deadlines instead, scheduled against active scanline
        // timing.  That's the deferred @c RtpPacingMode::TxTime
        // path; until it lands, the rate-cap approach below is the
        // best we can do with @c SO_MAX_PACING_RATE alone.
        if (!_readerMode && _pacingMode.value() == RtpPacingMode::KernelFq.value()) {
                auto applyRate = [](Stream &s, uint64_t bitsPerSec) {
                        if (!s.active || bitsPerSec == 0) return;
                        uint64_t bytesPerSec = bitsPerSec / 8;
                        (void)s.session->setPacingRate(bytesPerSec);
                };
                // Video: user-specified bitrate, or computed for
                // uncompressed from the descriptor.  Compressed
                // formats are paced per-frame from sendVideo; the
                // explicit-config path is preserved so callers that
                // already know their compressed bitrate can opt out
                // of per-frame updates if they want.
                uint64_t videoBitrate = static_cast<uint64_t>(cfg.getAs<int>(MediaConfig::VideoRtpTargetBitrate, 0));
                bool anyVideoActive2 = false;
                for (const VideoStream &vs : _videos) {
                        if (vs.active) {
                                anyVideoActive2 = true;
                                break;
                        }
                }
                if (videoBitrate == 0 && anyVideoActive2 && !cmd.pendingMediaDesc.imageList().isEmpty()) {
                        const ImageDesc &img = cmd.pendingMediaDesc.imageList()[0];
                        if (!img.pixelFormat().isCompressed()) {
                                // Uncompressed: width * height * bpp * fps.
                                // bpp is approximated from
                                // bytesPerBlock / pixelsPerBlock.
                                const PixelMemLayout &pf = img.pixelFormat().memLayout();
                                size_t                ppb = pf.pixelsPerBlock();
                                size_t                bpb = pf.bytesPerBlock();
                                double                bpp =
                                        ppb > 0 ? (8.0 * static_cast<double>(bpb) / static_cast<double>(ppb)) : 0.0;
                                double fps = _frameRate.isValid() ? _frameRate.toDouble() : 30.0;
                                videoBitrate = static_cast<uint64_t>(static_cast<double>(img.width()) *
                                                                     static_cast<double>(img.height()) * bpp * fps);
                        }
                        // Compressed with no explicit bitrate: leave
                        // the rate cap unset here.  sendVideo updates
                        // it per frame from the actual packed byte
                        // count, which is correct for VBR streams.
                }
                for (VideoStream &vs : _videos) applyRate(vs, videoBitrate);

                // Audio: per-stream rate cap, sized for the L16 wire
                // payload + headers.  Each stream's cap reflects its
                // own sample rate / channel count, since multi-stream
                // sessions can mix different audio shapes (e.g. a
                // stereo program plus a multi-channel deliverable).
                // Compute against the L16 wire bytes-per-sample (2,
                // not the source's bytesPerSample which may be wider)
                // and add ~25% headroom over pure-payload bitrate to
                // account for RTP / UDP / IP framing overhead — without
                // headroom the cap is short of actual wire usage and
                // the kernel queue grows unboundedly.
                for (size_t i = 0; i < _audios.size(); i++) {
                        AudioStream &as = _audios[i];
                        if (!as.active) continue;
                        // storageDesc carries the writer-side wire
                        // sample rate / channel count (PCMI_S16BE),
                        // resolved by configureAudioStream from the
                        // upstream AudioDesc.  Same per-second byte
                        // rate as the source, just in network order.
                        const float    rateHz = as.storageDesc.sampleRate();
                        const unsigned ch = as.storageDesc.channels();
                        if (rateHz <= 0.0f || ch == 0) continue;
                        const uint64_t payloadBps =
                                static_cast<uint64_t>(rateHz) * static_cast<uint64_t>(ch) * 2ull * 8ull;
                        const uint64_t cappedBps = (payloadBps * 5ull) / 4ull; // +25% headroom
                        applyRate(as, cappedBps);
                }

                // Data: no natural rate — leave unlimited.
        }

        buildSdp();
        if (!_readerMode && !_sdpPath.isEmpty()) {
                Error sdpErr = writeSdpFile(_sdpPath);
                if (sdpErr.isError()) {
                        resetAll();
                        return sdpErr;
                }
        }

        // For the reader, synthesise the outbound MediaDesc from
        // the resolved per-stream descriptors so downstream
        // consumers (mediaplay's --in stage reporter, a follow-up
        // CSC stage, etc.) see the shape the reader is
        // actually emitting — not the empty one the caller passed
        // in before SDP / config-key fallback ran.
        MediaDesc resolved = cmd.pendingMediaDesc;
        if (_readerMode) {
                if (resolved.imageList().isEmpty()) {
                        for (const VideoReaderStream &vs : _videoReaders) {
                                if (vs.active && vs.readerImageDesc.isValid()) {
                                        resolved.imageList().pushToBack(vs.readerImageDesc);
                                }
                        }
                }
                if (resolved.audioList().isEmpty()) {
                        for (const AudioReaderStream &as : _audioReaders) {
                                if (as.active && as.readerAudioDesc.isValid()) {
                                        resolved.audioList().pushToBack(as.readerAudioDesc);
                                }
                        }
                }
                if (_frameRate.isValid()) resolved.setFrameRate(_frameRate);
        }

        // Single-port-group RTP backend: source when reader, sink
        // when writer.
        MediaIOPortGroup *group = addPortGroup("rtp");
        if (group == nullptr) return Error::Invalid;
        group->setFrameRate(_frameRate);
        group->setCanSeek(false);
        group->setFrameCount(MediaIO::FrameCountInfinite);
        if (isWrite) {
                if (addSink(group, resolved) == nullptr) return Error::Invalid;
        } else {
                if (addSource(group, resolved) == nullptr) return Error::Invalid;
        }
        return Error::Ok;
}

Error RtpMediaIO::executeCmd(MediaIOCommandClose &cmd) {
        (void)cmd;

        // Dump the per-stream timing histograms to the log on
        // close, but only if there's anything to show — empty
        // histograms (no frames sent / received) emit nothing so
        // open-then-close noise stays off the log.  This gives
        // every clean shutdown a one-line-per-histogram diagnostic
        // dump that captures the entire run's pacing distribution
        // without needing a stats query.
        auto dumpIfPopulated = [](const Histogram &h) {
                if (h.count() > 0) {
                        promekiInfo("RtpMediaIO: %s", h.toString().cstr());
                }
        };
        if (_readerMode) {
                for (const VideoReaderStream &vs : _videoReaders) {
                        dumpIfPopulated(vs.rxPacketInterval);
                        dumpIfPopulated(vs.rxFrameInterval);
                        dumpIfPopulated(vs.rxFrameAssembleTime);
                }
        } else {
                for (const VideoStream &vs : _videos) {
                        dumpIfPopulated(vs.txFrameInterval);
                        dumpIfPopulated(vs.txSendDuration);
                }
        }

        resetAll();
        return Error::Ok;
}

Error RtpMediaIO::executeCmd(MediaIOCommandRead &cmd) {
        if (!_readerMode) return Error::NotSupported;

        // Block until a frame is available, the queue surfaces an
        // error, or cancelBlockingWork() is invoked from MediaIO::close.
        // RTP is a leaf source — returning Error::TryAgain here would
        // strand the pipeline pump (which expects an upstream Write to
        // re-fire the read on the next frameReady, an event that never
        // happens for a leaf source).  The short pop timeout is just a
        // polling cadence for _readCancelled; steady-state delivery is
        // condvar-driven so the cadence does not bound throughput.
        constexpr unsigned int kReadPollMs = 100;
        for (;;) {
                Result<Frame> result = _readerQueue.pop(kReadPollMs);
                if (result.second().isOk()) {
                        cmd.frame = result.first();
                        ++_frameCount;
                        cmd.currentFrame = toFrameNumber(_frameCount);
                        return Error::Ok;
                }
                if (result.second() != Error::Timeout) {
                        return result.second();
                }
                if (_readCancelled.load(std::memory_order_acquire)) {
                        return Error::Cancelled;
                }
        }
}

void RtpMediaIO::cancelBlockingWork() {
        // Tripped from MediaIO::close on the caller's thread, before
        // the Close cmd is queued.  The strand worker may be parked
        // inside executeCmd(Read)'s pop loop; raising this flag makes
        // the next short-timeout wakeup return Cancelled so the strand
        // can drain the Read and reach the queued Close.
        _readCancelled.store(true, std::memory_order_release);
}

// ----- Per-stream send helpers -----

namespace {

        /// @brief Returns true if @p nal payload is the H.264 SPS NAL (type 7).
        bool isH264Sps(const uint8_t *p, size_t n) { return n >= 1 && (p[0] & 0x1F) == 7; }

        /// @brief Returns true if @p nal payload is the H.264 PPS NAL (type 8).
        bool isH264Pps(const uint8_t *p, size_t n) { return n >= 1 && (p[0] & 0x1F) == 8; }

        /// @brief Returns true if @p nal payload is an H.264 IDR slice NAL (type 5).
        bool isH264Idr(const uint8_t *p, size_t n) { return n >= 1 && (p[0] & 0x1F) == 5; }

        /// @brief Returns the HEVC nal_unit_type (6 bits, byte 0 bits 6..1).
        uint8_t hevcNalType(uint8_t b0) { return static_cast<uint8_t>((b0 >> 1) & 0x3F); }

        /// @brief Returns true if the NAL is HEVC IRAP (types 16-23: BLA / IDR / CRA + reserved).
        bool isHevcIrap(uint8_t t) { return t >= 16 && t <= 23; }

        /// @brief Copies a NAL payload into a fresh Buffer.
        Buffer copyNal(const uint8_t *p, size_t n) {
                Buffer b(n);
                if (n > 0) std::memcpy(b.data(), p, n);
                b.setSize(n);
                return b;
        }

        /// @brief Compares an in-buffer NAL slice to a cached NAL Buffer for byte equality.
        bool nalEquals(const uint8_t *p, size_t n, const Buffer &cached) {
                if (!cached.isValid() || cached.size() != n) return false;
                if (n == 0) return true;
                return std::memcmp(p, cached.data(), n) == 0;
        }

} // namespace

Error RtpMediaIO::injectParameterSets(const uint8_t *data, size_t size, Buffer &healed) {
        if (data == nullptr || size == 0) return Error::Ok;
        if (_videos.isEmpty()) return Error::Ok;
        VideoStream &vs = _videos[0];
        if (vs.payload == nullptr) return Error::Ok;
        const VideoCodec::ID codec = vs.imageDesc.pixelFormat().videoCodec().id();
        // Self-healing applies only to the temporal codecs.  For
        // intra-only compressed (JPEG, JXS) and uncompressed video,
        // every access unit is independently decodable so there's
        // nothing to inject.
        if (codec != VideoCodec::H264 && codec != VideoCodec::HEVC) return Error::Ok;

        // Wrap the input bytes as a non-owning Buffer so we can hand
        // it to forEachAnnexBNal — same trick used inside
        // RtpPayloadH264::pack.
        Buffer wrap = Buffer::wrapHost(const_cast<uint8_t *>(data), size);
        wrap.setSize(size);
        BufferView view(wrap, 0, size);

        // Diagnostic: confirm the bitstream is in Annex-B form.  If
        // the encoder hands us AVCC (length-prefixed) bytes,
        // forEachAnnexBNal returns CorruptData and we'd silently
        // never populate the cache — which is exactly the symptom
        // "PPS never on the wire" without any error path.  Detect
        // and warn once, on the first send.
        bool nalIterated = false;

        bool sawSps = false, sawPps = false, sawVps = false;
        bool sawIdr = false;
        bool cacheUpdated = false;
        Error iterErr = H264Bitstream::forEachAnnexBNal(view, [&](const H264Bitstream::NalUnit &nal) -> Error {
                nalIterated = true;
                auto         slice = nal.view[0];
                const uint8_t *np = slice.data();
                const size_t   nn = slice.size();
                if (codec == VideoCodec::H264) {
                        if (isH264Sps(np, nn)) {
                                sawSps = true;
                                if (!nalEquals(np, nn, vs.cachedSps)) {
                                        vs.cachedSps = copyNal(np, nn);
                                        cacheUpdated = true;
                                }
                        } else if (isH264Pps(np, nn)) {
                                sawPps = true;
                                if (!nalEquals(np, nn, vs.cachedPps)) {
                                        vs.cachedPps = copyNal(np, nn);
                                        cacheUpdated = true;
                                }
                        } else if (isH264Idr(np, nn)) {
                                sawIdr = true;
                        }
                } else { // HEVC
                        const uint8_t t = hevcNalType(nal.header0);
                        if (t == 32) {
                                sawVps = true;
                                if (!nalEquals(np, nn, vs.cachedVps)) {
                                        vs.cachedVps = copyNal(np, nn);
                                        cacheUpdated = true;
                                }
                        } else if (t == 33) {
                                sawSps = true;
                                if (!nalEquals(np, nn, vs.cachedSps)) {
                                        vs.cachedSps = copyNal(np, nn);
                                        cacheUpdated = true;
                                }
                        } else if (t == 34) {
                                sawPps = true;
                                if (!nalEquals(np, nn, vs.cachedPps)) {
                                        vs.cachedPps = copyNal(np, nn);
                                        cacheUpdated = true;
                                }
                        } else if (isHevcIrap(t)) {
                                sawIdr = true;
                        }
                }
                return Error::Ok;
        });

        // SDP sprop refresh: any time the cached parameter sets
        // change, regenerate the @c sprop-parameter-sets / @c sprop-*
        // fmtp values and rewrite the on-disk SDP file.  This is
        // what lets ffplay (and other RTP-H.264 receivers that
        // bootstrap their decoder from SDP fmtp at probe time)
        // initialize correctly even when they start AFTER the
        // sender's first IDR.
        if (cacheUpdated) {
                refreshSdpSprop();
        }

        // One-shot diagnostic on the first frame after open: tell
        // the operator the shape of what we're seeing so RTP / ffplay
        // interop problems are easier to triage.  Cleared on close
        // via resetStream's cache reset (cachedSps unchanged means
        // first call after open).
        if (!vs.cachedSps.isValid() && !sawSps) {
                if (iterErr.isError() || !nalIterated) {
                        promekiWarn("RtpMediaIO: input bitstream has no Annex-B start codes "
                                    "(found 0 NAL units in %zu bytes); the source is likely AVCC "
                                    "/ length-prefixed.  H.264 / HEVC RTP requires Annex-B input — "
                                    "the writer cannot self-heal parameter sets and the receiver "
                                    "will fail to decode.",
                                    size);
                } else {
                        promekiInfo("RtpMediaIO: H.264/HEVC writer first frame: sawSps=%d sawPps=%d "
                                    "sawVps=%d sawIdr=%d (%zu bytes).  Self-healing cache will "
                                    "populate once SPS / PPS pass through.",
                                    sawSps, sawPps, sawVps, sawIdr, size);
                }
        }

        // Decide whether to prepend.  Only prepend when:
        //   * this access unit is an IDR / IRAP, and
        //   * at least one required parameter set is missing from the
        //     incoming bitstream, and
        //   * we have a cached copy of every parameter set the codec
        //     requires (no point prepending an incomplete set).
        if (!sawIdr) return Error::Ok;
        bool needSps = !sawSps && vs.cachedSps.isValid();
        bool needPps = !sawPps && vs.cachedPps.isValid();
        bool needVps = (codec == VideoCodec::HEVC) && !sawVps && vs.cachedVps.isValid();
        if (!needSps && !needPps && !needVps) return Error::Ok;

        // For self-healing to make the receiver decodable, we must
        // emit every parameter set the codec requires together — an
        // IDR with only SPS but no PPS still fails.  If a required
        // set is missing entirely (never observed yet, nothing
        // cached), we cannot help; the writer ships the original
        // bytes and the receiver waits for the next complete IDR.
        const bool h264Complete = (codec == VideoCodec::H264) &&
                                  vs.cachedSps.isValid() && vs.cachedPps.isValid();
        const bool hevcComplete = (codec == VideoCodec::HEVC) &&
                                  vs.cachedVps.isValid() && vs.cachedSps.isValid() &&
                                  vs.cachedPps.isValid();
        if (!h264Complete && !hevcComplete) return Error::Ok;

        // Build the prepended Annex-B blob:
        //   [4-byte SC] [VPS]  (HEVC only)
        //   [4-byte SC] [SPS]
        //   [4-byte SC] [PPS]
        //   [original bytes]
        size_t extraBytes = 0;
        if (codec == VideoCodec::HEVC) extraBytes += 4 + vs.cachedVps.size();
        extraBytes += 4 + vs.cachedSps.size();
        extraBytes += 4 + vs.cachedPps.size();

        Buffer out(extraBytes + size);
        if (!out) return Error::NoMem;
        uint8_t *dst = static_cast<uint8_t *>(out.data());
        size_t   pos = 0;
        auto     writeNal = [&](const Buffer &nal) {
                dst[pos++] = 0x00;
                dst[pos++] = 0x00;
                dst[pos++] = 0x00;
                dst[pos++] = 0x01;
                if (nal.size() > 0) std::memcpy(dst + pos, nal.data(), nal.size());
                pos += nal.size();
        };
        if (codec == VideoCodec::HEVC) writeNal(vs.cachedVps);
        writeNal(vs.cachedSps);
        writeNal(vs.cachedPps);
        std::memcpy(dst + pos, data, size);
        pos += size;
        out.setSize(pos);
        healed = out;
        promekiDebug("RtpMediaIO: self-healed %s IDR/IRAP — prepended %zu bytes of "
                     "cached parameter sets onto a %zu-byte access unit.",
                     codec == VideoCodec::H264 ? "H.264" : "HEVC", extraBytes, size);
        return Error::Ok;
}


Error RtpMediaIO::executeCmd(MediaIOCommandWrite &cmd) {
        if (!cmd.frame.isValid()) return Error::InvalidArgument;
        const Frame &frame = cmd.frame;

        // First-frame anchor refinement.  At openStream time every
        // active session was seeded with @c (NtpTime::now(), 0) so
        // SRs are structurally valid even before any frame flows;
        // when the first frame arrives we replace that with a value
        // derived from the source's authoritative capture instant
        // (Frame::captureTime).  Compare-exchange so the seed
        // happens exactly once per opening even if a future change
        // ever takes the Write path off the single-threaded strand.
        bool expected = false;
        if (frame.captureTime().isValid() && _anchorSeeded.compareAndSwap(expected, true)) {
                // Pin a single observed (steady, wall) reference
                // instant and use it to convert the captureTime's
                // steady_clock-based @c TimeStamp to NTP wallclock.
                const TimeStamp steadyNow = TimeStamp::now();
                const NtpTime   wallNow   = NtpTime::now();
                const Duration  delta     = frame.captureTime().timeStamp() - steadyNow;
                const NtpTime   captureNtp = wallNow + delta + frame.captureTime().offset();
                auto refine = [&captureNtp](Stream &s) {
                        if (!s.active || s.session == nullptr) return;
                        s.session->setRtpAnchor(captureNtp, 0);
                };
                for (VideoStream &vs : _videos) refine(vs);
                for (AudioStream &as : _audios) refine(as);
                for (DataStream &ds : _datas) refine(ds);
        }

        // Pace gate stays on the strand — synthetic / unclocked
        // sources have no upstream clock and pacing modes that
        // hand whole frames to the kernel in a burst (KernelFq /
        // None) deliver no backpressure to the strand on their
        // own.  For Userspace pacing the gate is a near-no-op
        // because the bounded PayloadQueue blocks the strand at
        // wire rate; for externally-clocked sources the gate's
        // clock binding makes it a no-op too.  Skip verdict drops
        // the frame to bound lag rather than emitting stale
        // content.
        const bool hasVideoEssence = !frame.videoPayloads().isEmpty() &&
                                     frame.videoPayloads()[0].isValid();
        bool anyVideoActiveTx = false;
        for (const VideoStream &vs : _videos) {
                if (vs.active) {
                        anyVideoActiveTx = true;
                        break;
                }
        }
        if (anyVideoActiveTx && hasVideoEssence) {
                if (!paceVideoFrame()) {
                        noteFrameDropped(portGroup(0));
                        ++_frameCount;
                        _framesSent++;
                        cmd.currentFrame = toFrameNumber(_frameCount);
                        cmd.frameCount = MediaIO::FrameCountInfinite;
                        return Error::Ok;
                }
        }

        const RtpFrameWork work{frame, toFrameNumber(_frameCount)};

        // Strand-as-router: push the same Frame (CoW handle —
        // refcount bump only) onto every active stream's
        // PayloadQueue and return.  Each per-stream packetizer
        // pulls its own essence in its own thread.  The strand
        // never blocks on actual wire I/O and never joins on TX
        // results — backpressure flows back through the bounded
        // PayloadQueue when any one stream falls behind.
        for (VideoStream &vs : _videos) {
                if (!vs.active || vs.packetizer == nullptr || !hasVideoEssence) continue;
                Error err = vs.packetizer->pushWork(work);
                if (err.isError() && err != Error::Cancelled) {
                        promekiWarn("RtpMediaIO: video PayloadQueue push failed: %s", err.desc().cstr());
                }
        }
        for (AudioStream &as : _audios) {
                if (!as.active || as.packetizer == nullptr) continue;
                Error err = as.packetizer->pushWork(work);
                if (err.isError() && err != Error::Cancelled) {
                        promekiWarn("RtpMediaIO: audio PayloadQueue push failed: %s", err.desc().cstr());
                }
        }
        for (DataStream &ds : _datas) {
                if (!ds.active || ds.packetizer == nullptr) continue;
                Error err = ds.packetizer->pushWork(work);
                if (err.isError() && err != Error::Cancelled) {
                        promekiWarn("RtpMediaIO: data PayloadQueue push failed: %s", err.desc().cstr());
                }
        }

        ++_frameCount;
        _framesSent++;
        cmd.currentFrame = toFrameNumber(_frameCount);
        cmd.frameCount = MediaIO::FrameCountInfinite;
        return Error::Ok;
}

Error RtpMediaIO::executeCmd(MediaIOCommandParams &cmd) {
        if (cmd.name == ParamGetSdp.name()) {
                // The GetSdp command returns the live session
                // description as text.  Callers that want the
                // structured form should set up their own
                // SdpSession — the RTP backend doesn't currently
                // expose a typed variant-returning params command.
                cmd.output.set(ParamSdp, _sdpSession.toString());
                return Error::Ok;
        }
        return Error::NotSupported;
}

Error RtpMediaIO::executeCmd(MediaIOCommandSetClock &cmd) {
        // Reject in reader mode — RX timing is driven by RTP packet
        // arrival from the network and the user cannot meaningfully
        // replace it.
        if (_readerMode) return Error::NotSupported;

        _videoGate.setClock(cmd.clock);
        return Error::Ok;
}

bool RtpMediaIO::paceVideoFrame() {
        if (!_videoGate.hasClock()) return true;
        if (!_frameRate.isValid()) return true;

        // Update period each call so a mid-stream frame-rate change
        // (rare but legal in some MediaIOs) takes effect immediately.
        _videoGate.setPeriod(_frameRate.frameDuration());

        PacingResult pr = _videoGate.wait();
        if (pr.error.isError()) {
                promekiErr("RtpMediaIO: video pacing clock failure: %s",
                           pr.error.name().cstr());
                // Pacing failed but the frame should still ship —
                // dropping silently on clock failure would surprise
                // callers more than emitting unpaced.
                return true;
        }
        switch (pr.verdict) {
                case PacingVerdict::Skip:
                        return false;
                case PacingVerdict::Reanchor:
                        promekiWarn("RtpMediaIO: video pacing re-anchored after %s lag",
                                    pr.slack.toString().cstr());
                        return true;
                case PacingVerdict::OnTime:
                case PacingVerdict::Late:
                        return true;
        }
        return true;
}

Error RtpMediaIO::executeCmd(MediaIOCommandStats &cmd) {
        // Aggregate per-stream counters.  Each
        // @c Atomic<int64_t>::value() is an aligned acquire-load —
        // no torn reads even though the per-stream TX / RX threads
        // are concurrently writing.
        if (_readerMode) {
                int64_t videoPacketsRx = 0, videoBytesRx = 0;
                for (const VideoReaderStream &vs : _videoReaders) {
                        videoPacketsRx += vs.packetsReceived.value();
                        videoBytesRx += vs.bytesReceived.value();
                }
                int64_t audioPacketsRx = 0, audioBytesRx = 0;
                for (const AudioReaderStream &as : _audioReaders) {
                        audioPacketsRx += as.packetsReceived.value();
                        audioBytesRx += as.bytesReceived.value();
                }
                int64_t dataPacketsRx = 0, dataBytesRx = 0;
                for (const DataReaderStream &ds : _dataReaders) {
                        dataPacketsRx += ds.packetsReceived.value();
                        dataBytesRx += ds.bytesReceived.value();
                }
                cmd.stats.set(StatsFramesReceived, _readerFramesReceived);
                // FramesDropped is populated by the MediaIO base
                // class from noteFrameDropped().
                cmd.stats.set(StatsPacketsReceived,
                              videoPacketsRx + audioPacketsRx + dataPacketsRx);
                cmd.stats.set(StatsBytesReceived,
                              videoBytesRx + audioBytesRx + dataBytesRx);
                // Diagnostic histograms (RX side).  Stored as
                // pretty-printed Strings so callers can dump them
                // straight to a log without re-parsing.  First-stream
                // only today; multi-stream stats land alongside
                // multi-stream config plumbing.
                if (!_videoReaders.isEmpty()) {
                        const VideoReaderStream &v0 = _videoReaders[0];
                        cmd.stats.set(StatsRxVideoPacketIntervalUs, v0.rxPacketInterval.toString());
                        cmd.stats.set(StatsRxVideoFrameIntervalUs, v0.rxFrameInterval.toString());
                        cmd.stats.set(StatsRxVideoFrameAssembleUs, v0.rxFrameAssembleTime.toString());
                }
        } else {
                int64_t videoPacketsTx = 0, videoBytesTx = 0;
                for (const VideoStream &vs : _videos) {
                        videoPacketsTx += vs.packetsSent.value();
                        videoBytesTx += vs.bytesSent.value();
                }
                int64_t audioPacketsTx = 0, audioBytesTx = 0;
                int64_t audioSilencePackets = 0, audioSilenceSamples = 0;
                for (const AudioStream &as : _audios) {
                        audioPacketsTx += as.packetsSent.value();
                        audioBytesTx += as.bytesSent.value();
                        audioSilencePackets += as.silencePacketsEmitted.value();
                        audioSilenceSamples += as.silenceSamplesEmitted.value();
                }
                int64_t dataPacketsTx = 0, dataBytesTx = 0;
                for (const DataStream &ds : _datas) {
                        dataPacketsTx += ds.packetsSent.value();
                        dataBytesTx += ds.bytesSent.value();
                }
                cmd.stats.set(StatsFramesSent, _framesSent);
                cmd.stats.set(StatsPacketsSent, videoPacketsTx + audioPacketsTx + dataPacketsTx);
                cmd.stats.set(StatsBytesSent, videoBytesTx + audioBytesTx + dataBytesTx);
                cmd.stats.set(StatsAudioSilencePacketsEmitted, audioSilencePackets);
                cmd.stats.set(StatsAudioSilenceSamplesEmitted, audioSilenceSamples);
                // Diagnostic histograms (TX side) — first stream
                // only today.
                if (!_videos.isEmpty()) {
                        const VideoStream &v0 = _videos[0];
                        cmd.stats.set(StatsTxVideoFrameIntervalUs, v0.txFrameInterval.toString());
                        cmd.stats.set(StatsTxVideoSendDurationUs, v0.txSendDuration.toString());
                }
        }
        return Error::Ok;
}

PROMEKI_NAMESPACE_END
