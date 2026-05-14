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
#include <promeki/networkinterface.h>
#include <promeki/filepath.h>
#include <promeki/frame.h>
#include <promeki/h264bitstream.h>
#include <promeki/hevcbitstream.h>
#include <promeki/imagedesc.h>
#include <promeki/iodevice.h>
#include <promeki/jpeggeometryprobe.h>
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
#include <promeki/ancdesc.h>
#include <promeki/ancpayload.h>
#include <promeki/rtpancdepacketizerthread.h>
#include <promeki/rtpancpacketizerthread.h>
#include <promeki/rtpmediaio.h>
#include <promeki/rtppacketbatch.h>
#include <promeki/rtppayload.h>
#include <promeki/rtppayloadanc.h>
#include <promeki/rtpsession.h>
#include <promeki/rtpstreamclock.h>
#include <promeki/mutex.h>
#include <promeki/sdpsession.h>
#include <promeki/application.h>
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

                        // The per-batch send body is invoked from both
                        // the steady-state pop loop and the post-cancel
                        // drain phase below — extracted into a lambda so
                        // the two paths share exactly the same wire
                        // emission semantics (per-packet pacing,
                        // rateCapBps handling, stat accounting).  Without
                        // the drain phase, a clean
                        // executeCmd(Close) cascade would silently lose
                        // every video frame the strand had pushed but
                        // the TX thread hadn't yet sent — empirically
                        // ~3-4 frames at 60 fps for a 30-frame run.
                        auto sendBatch = [&](RtpPacketBatch &batch) {
                                if (batch.packets.isEmpty() || vs.session == nullptr) return;

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
                                        // The deadline scheme is anchored
                                        // at this frame's start — drift
                                        // across frames is bounded by
                                        // the strand's own per-frame
                                        // cadence and does not accumulate.
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
                                        return;
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
                        };

                        while (!isStopRequested()) {
                                auto r = _packetQueue.pop();
                                if (r.second().isError()) break;
                                RtpPacketBatch batch = std::move(r.first());
                                sendBatch(batch);
                        }
                        // Drain phase — see RtpPacketizerThread::run for
                        // the rationale.  After the cancel latch the
                        // blocking @c pop returns @c Error::Cancelled
                        // immediately even when items remain in the
                        // queue; @c tryPop continues to deliver them
                        // until empty.  Bounds: queue depth (3) ×
                        // per-batch wall time (~one frame interval at
                        // userspace pacing) ≈ 50 ms additional close
                        // latency.
                        while (true) {
                                auto r = _packetQueue.tryPop();
                                if (r.second().isError()) break;
                                RtpPacketBatch batch = std::move(r.first());
                                sendBatch(batch);
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

                        // Per-batch send body shared by the steady-state
                        // pop loop and the post-cancel drain phase below.
                        // Mirrors @ref VideoTxThread::run — see there
                        // for the drain-on-close rationale.
                        auto sendBatch = [&](RtpPacketBatch &batch) {
                                if (batch.packets.isEmpty() || ds.session == nullptr) return;
                                const double fps = _owner->_frameRate.isValid()
                                                           ? _owner->_frameRate.toDouble()
                                                           : 30.0;
                                const uint32_t ts = static_cast<uint32_t>(
                                        static_cast<double>(batch.frameIndex.value()) *
                                        static_cast<double>(batch.clockRate) / fps);
                                for (size_t i = 0; i < batch.packets.size(); i++) {
                                        const bool isLast = (i + 1 == batch.packets.size());
                                        batch.packets[i].setTimestamp(ts);
                                        batch.packets[i].setMarker(isLast && batch.markerOnLast);
                                }
                                Error err = ds.session->sendPackets(batch);
                                if (err.isError()) {
                                        promekiErr("RtpMediaIO::DataTxThread: sendPackets failed: %s",
                                                   err.desc().cstr());
                                        return;
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
                        };

                        while (!isStopRequested()) {
                                auto r = _packetQueue.pop();
                                if (r.second().isError()) break;
                                RtpPacketBatch batch = std::move(r.first());
                                sendBatch(batch);
                        }
                        while (true) {
                                auto r = _packetQueue.tryPop();
                                if (r.second().isError()) break;
                                RtpPacketBatch batch = std::move(r.first());
                                sendBatch(batch);
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

// ----------------------------------------------------------------------------
// Per-stream packetizer + TX + depacketizer threads all live in
// network/ as standalone classes:
//   - Writer side: @ref RtpAudioPacketizerThread + @ref RtpAudioTxThread
//     (video / data still nested above for now).
//   - Reader side: @ref RtpAudioDepacketizerThread,
//     @ref RtpDataDepacketizerThread, @ref RtpVideoDepacketizerThread.
// Each is wired into its per-stream state via a per-class context
// struct the @c openWriterStream / @c openReaderStream paths
// populate.  See those headers for per-class detail.
// ----------------------------------------------------------------------------

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
        s(MediaConfig::RtpMaxReadQueueDepth, int32_t(8));
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

Atomic<uint64_t> RtpMediaIO::_nextObjectId{0};

RtpMediaIO::RtpMediaIO(ObjectBase *parent) : DedicatedThreadMediaIO(parent) {
        // Streams of all three kinds are appended on demand by the
        // matching @c configureVideoStream / @c configureAudioStream /
        // @c configureDataStream (one per @c *RtpDestination today, N
        // per multi-stream session in future).  Each pushed entry
        // sets its own @c mediaType so the SDP builder, RTCP
        // scheduler, and reader-side dispatcher can identify it
        // without dynamic dispatch.
        _objectId = _nextObjectId.fetchAndAdd(uint64_t(1)) + uint64_t(1);
}

RtpMediaIO::~RtpMediaIO() {
        if (isOpen()) (void)close().wait();
        resetAll();
}

// ----- Helpers -----

String RtpMediaIO::buildDefaultCname(int64_t pid, uint64_t objectId, const String &host) {
        String out = String("promeki-") + String::number(pid) + String("-") +
                     String::number(static_cast<int64_t>(objectId));
        if (!host.isEmpty()) {
                out += String("@");
                out += host;
        }
        return out;
}

String RtpMediaIO::pickEgressHostForCname(const SocketAddress &destination) {
        // Prefer the egress interface for the destination — that's
        // the interface whose source IP will appear in the outbound
        // RTP packets, which is what RFC 3550 §6.5.1 asks for.
        if (!destination.isNull()) {
                if (destination.isIPv4()) {
                        NetworkInterface::List ifaces = NetworkInterface::findRoutesTo(destination.address().toIpv4());
                        for (const NetworkInterface &iface : ifaces) {
                                Ipv4Address::List addrs = iface.ipv4Addresses();
                                if (!addrs.isEmpty()) return addrs.front().toString();
                        }
                } else if (destination.isIPv6()) {
                        NetworkInterface::List ifaces = NetworkInterface::findRoutesTo(destination.address().toIpv6());
                        for (const NetworkInterface &iface : ifaces) {
                                Ipv6Address::List addrs = iface.ipv6Addresses();
                                if (!addrs.isEmpty()) return String("[") + addrs.front().toString() + String("]");
                        }
                }
        }

        // No routable destination — fall back to the first
        // non-loopback interface.  IPv4 wins if present (most common
        // SDES form); IPv6 if that's all we've got.  Brackets on the
        // IPv6 fallback so the @ separator in the CNAME stays
        // unambiguous.
        NetworkInterface fallback = NetworkInterface::firstNonLoopback();
        if (fallback.isValid()) {
                Ipv4Address::List v4 = fallback.ipv4Addresses();
                if (!v4.isEmpty()) return v4.front().toString();
                Ipv6Address::List v6 = fallback.ipv6Addresses();
                if (!v6.isEmpty()) return String("[") + v6.front().toString() + String("]");
        }
        return String();
}

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
        // Reader-mode tear-down ordering matters in two places:
        //   1. The recv socket thread is the *producer* on the
        //      depacketizer's input queue, so we must stop it before
        //      joining the depacketizer (otherwise the recv thread
        //      keeps pushing into a queue whose destructor is about
        //      to run).
        //   2. The depacketizer reads @c s.session and @c s.payload
        //      on every packet, so we must join the depacketizer
        //      *before* @ref resetStreamCommon nulls those out.
        // The full sequence is therefore: stopReceiving (recv thread
        // joined) → depacketizer.requestStop + clear (depacketizer
        // thread joined) → resetStreamCommon (session / payload /
        // transport torn down with no live worker referencing them).
        if (s.session != nullptr) s.session->stopReceiving();
        if (s.depacketizer.isValid()) {
                s.depacketizer->requestStop();
                s.depacketizer.clear();
        }
        resetStreamCommon(s);
        if (s.reorderBuffer.isValid()) s.reorderBuffer->clear();
        s.reorderQueue.clear();
        s.reorderBuffer.clear();
        s.seqTracker.clear();
        s.resetEpoch.setValue(0);
        s.ssrcChanges.setValue(0);
        s.framesReassembled.setValue(0);
        s.framesDroppedValidate.setValue(0);
        s.framesWaitingParamSets.setValue(0);
        s.framesDroppedSsrcReset.setValue(0);
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

RtcpSchedulerContext RtpMediaIO::buildRtcpSchedulerContext() {
        RtcpSchedulerContext ctx;
        ctx.intervalMs = _rtcpIntervalMs;
        ctx.wireSilenceTimeoutMs = _wireSilenceTimeoutMs;
        // Wire-silence EoS callback: latch _readCancelled, wake any
        // strand-side blocking pop on _readerQueue, and prod the
        // depacketizer to wake from its blocking pop too.  Lifetime:
        // this RtpMediaIO outlives the scheduler (resetAll stops the
        // scheduler before the io destructor returns), so capturing
        // @c this is safe.
        ctx.onWireSilenceEos = [this](RtcpSchedulerReaderStream &view, int64_t /*gapNs*/) {
                _readCancelled.store(true, std::memory_order_release);
                _readerQueue.cancelWaiters();
                // Pulse the matching reader stream's depacketizer.
                // Look up by session pointer — every reader stream
                // has a unique session, so the match is unambiguous.
                auto pulse = [&view](ReaderStream &s) {
                        if (s.session != view.session) return;
                        if (s.depacketizer.isValid()) s.depacketizer->requestStop();
                };
                for (VideoReaderStream &vrs : _videoReaders) pulse(vrs);
                for (AudioReaderStream &ars : _audioReaders) pulse(ars);
                for (DataReaderStream &drs : _dataReaders) pulse(drs);
        };
        auto fillWriter = [](WriterStream &s) {
                RtcpSchedulerWriterStream w;
                w.active = s.active;
                w.mediaType = s.mediaType;
                w.session = s.session;
                w.packetsSent = &s.packetsSent;
                w.senderOctets = &s.senderOctets;
                return w;
        };
        for (VideoStream &vs : _videos) ctx.writers.pushToBack(fillWriter(vs));
        for (AudioStream &as : _audios) ctx.writers.pushToBack(fillWriter(as));
        for (DataStream &ds : _datas) ctx.writers.pushToBack(fillWriter(ds));
        auto fillReader = [](ReaderStream &s) {
                RtcpSchedulerReaderStream r;
                r.active = s.active;
                r.mediaType = s.mediaType;
                r.session = s.session;
                r.seqTracker = s.seqTracker.isValid() ? s.seqTracker.get() : nullptr;
                r.lastPacketArrivalNs = &s.lastPacketArrivalNs;
                r.wireSilenceEosSignaled = &s.wireSilenceEosSignaled;
                return r;
        };
        for (VideoReaderStream &vrs : _videoReaders) ctx.readers.pushToBack(fillReader(vrs));
        for (AudioReaderStream &ars : _audioReaders) ctx.readers.pushToBack(fillReader(ars));
        for (DataReaderStream &drs : _dataReaders) ctx.readers.pushToBack(fillReader(drs));
        return ctx;
}

void RtpMediaIO::resetAll() {
        // Emit BYE for every session that has been actively
        // sending RTCP (writers SR-bearing, readers RR-bearing).
        // Done before the scheduler / sessions stop so the BYE
        // actually goes out on the wire — receivers can drop the
        // source immediately rather than waiting for a timeout.
        if (_rtcpScheduler.isValid()) {
                _rtcpScheduler->emitByeForAll();
        }
        // Stop the RTCP scheduler first — it dispatches on every
        // session, so it must be quiesced before we tear sessions
        // down below.
        if (_rtcpScheduler.isValid()) {
                _rtcpScheduler->requestStop();
                _rtcpScheduler->wait();
                _rtcpScheduler.clear();
        }
        // Stop the aggregator before the depacketizers tear down —
        // the aggregator is the consumer of every per-stream
        // payload queue, so killing it first lets depacketizers
        // drain cleanly without producing into freed queues.
        if (_aggregator.isValid()) {
                _aggregator->requestStop();
                _aggregator.clear();
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
        // Lift any cancel latch left by a previous close cycle so the
        // queue is ready for the next open.  Also re-arm an unbounded
        // initial state until @ref executeCmd(Open) reapplies the
        // configured depth.
        _readerQueue.reset();
        _readerQueue.setMaxSize(0);
        _sdpSession = SdpSession();
        _openedAt = TimeStamp();
        _readerSteadyAnchor = TimeStamp();
        _readerNtpAnchor = NtpTime();
        _readerHasAnchor = false;
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

// Extract the value of a single fmtp parameter from a
// semicolon-separated @c "key=value;key2=value2" string.  Used by
// the H.264 / H.265 reader to pull out @c sprop-parameter-sets,
// @c sprop-vps / @c sprop-sps / @c sprop-pps so the per-payload
// validate() gate can be seeded before any RTP packet arrives.
// Returns an empty String when the key is absent.
static String fmtpParamValue(const String &fmtp, const String &key) {
        if (fmtp.isEmpty()) return String();
        const StringList parts = fmtp.split(";");
        const String     prefix = key + String("=");
        for (size_t i = 0; i < parts.size(); i++) {
                String item = parts[i];
                if (item.find(prefix) == 0) return item.mid(prefix.byteCount());
                if (item == key) return String(); // bare key, no '=value'
        }
        return String();
}

// Decode the first NAL of a comma-separated base64 list whose NAL
// type matches @p expectedType (raw H.264 5-bit type, or HEVC
// 6-bit type pre-shift).  Empty Buffer if no match — callers treat
// that as "no SPS yet, keep the placeholder".
static Buffer decodeSpropNal(const String &csv, uint8_t expectedType, bool isHevc) {
        if (csv.isEmpty()) return Buffer();
        StringList parts = csv.split(",");
        for (size_t i = 0; i < parts.size(); i++) {
                Error  derr;
                Buffer nal = Base64::decode(parts[i], &derr);
                if (derr.isError() || nal.size() < (isHevc ? 2u : 1u)) continue;
                const uint8_t hdr = static_cast<const uint8_t *>(nal.data())[0];
                const uint8_t type = isHevc ? static_cast<uint8_t>((hdr >> 1) & 0x3f)
                                            : static_cast<uint8_t>(hdr & 0x1f);
                if (type == expectedType) return nal;
        }
        return Buffer();
}

// Seed @ref RtpPayloadH264::setSpropParameterSets from the fmtp
// string the reader stashed at SDP-ingest time, and stamp the
// reader's @ref ReaderStream::readerImageDesc with dimensions
// parsed from the first SPS — so the planner sees a valid
// imageDesc on @c open(), before any RTP packets arrive.  Errors
// are silently ignored; the placeholder 0×0 ImageDesc remains in
// place if anything fails, and the in-band path on the first IDR
// fills the gap once packets flow.
static void seedH264SpropFromFmtp(RtpPayloadH264 *payload, const String &fmtp,
                                  ImageDesc &readerImageDesc) {
        const String csv = fmtpParamValue(fmtp, String("sprop-parameter-sets"));
        if (csv.isEmpty()) return;
        payload->setSpropParameterSets(csv);

        Buffer spsNal = decodeSpropNal(csv, RtpPayloadH264::NalTypeSps, /*isHevc=*/false);
        if (!spsNal.isValid() || spsNal.size() == 0) return;
        BufferView              view(spsNal, 0, spsNal.size());
        H264Bitstream::SpsInfo  info;
        Error                   perr = H264Bitstream::parseSpsResolution(view, info);
        if (perr.isError() || info.width == 0 || info.height == 0) return;

        readerImageDesc = ImageDesc(Size2Du32(info.width, info.height), PixelFormat(PixelFormat::H264));
}

// Seed the three HEVC sprop-* values out of the fmtp string per
// RFC 7798 §7.1, and stamp the reader's image desc from the SPS
// dimensions.  Same fall-through-to-placeholder-on-error policy as
// the H.264 path.
static void seedH265SpropFromFmtp(RtpPayloadH265 *payload, const String &fmtp,
                                  ImageDesc &readerImageDesc) {
        const String vps = fmtpParamValue(fmtp, String("sprop-vps"));
        const String sps = fmtpParamValue(fmtp, String("sprop-sps"));
        const String pps = fmtpParamValue(fmtp, String("sprop-pps"));
        if (!vps.isEmpty()) payload->setSpropVps(vps);
        if (!sps.isEmpty()) payload->setSpropSps(sps);
        if (!pps.isEmpty()) payload->setSpropPps(pps);

        if (sps.isEmpty()) return;
        Buffer spsNal = decodeSpropNal(sps, RtpPayloadH265::NalTypeSps, /*isHevc=*/true);
        if (!spsNal.isValid() || spsNal.size() == 0) return;
        BufferView                   view(spsNal, 0, spsNal.size());
        HevcDecoderConfig::SpsInfo   info;
        Error                        perr = HevcDecoderConfig::parseSpsResolution(view, info);
        if (perr.isError() || info.width == 0 || info.height == 0) return;

        readerImageDesc = ImageDesc(Size2Du32(info.width, info.height), PixelFormat(PixelFormat::HEVC));
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
                s.tx = tx;
                if (dynamic_cast<RtpPayloadAnc *>(s.payload) != nullptr) {
                        // RFC 8331 ANC packetizer — produces
                        // RtpPacketBatch directly from the Frame's
                        // AncPayload via @c RtpPayloadAnc::packAncFrame.
                        // DataTxThread's RtpPacketBatch sink is reused
                        // (the TX side is payload-agnostic — it just
                        // stamps RTP TS + marker and writes the wire).
                        RtpAncPacketizerContext ctx;
                        ctx.streamIdx = 0;
                        ctx.clockRateHz = s.clockRate;
                        ctx.payload = static_cast<RtpPayloadAnc *>(s.payload);
                        ctx.txPacketQueue = &tx->packetQueue();
                        auto *pkt = new RtpAncPacketizerThread(std::move(ctx));
                        s.packetizer = pkt;
                        tx->start();
                        pkt->start();
                } else {
                        auto *pkt = new DataPacketizerThread(this);
                        pkt->setTx(tx);
                        s.packetizer = pkt;
                        tx->start();
                        pkt->start();
                }
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

        // Phase 2 queue-mode receive path.  Each ReaderStream owns
        // a per-source @ref RtpSeqTracker (RFC 3550 §A bookkeeping),
        // an @ref RtpSeqReorderBuffer (windowed reorder-by-extended-
        // seq), a post-reorder @c RtpPacket::Queue, and a per-stream
        // depacketizer thread that drains the queue and forwards
        // packets to the corresponding on*Packet handler.  The recv
        // socket thread does only seq-tracker bookkeeping + reorder
        // buffer insert before pushing onto the queue, so the kernel
        // UDP ring drains continuously even when reassembly /
        // unpack work falls behind.
        s.seqTracker = UniquePtr<RtpSeqTracker>::create();
        // Plumb @c MediaConfig::RtpJitterMs into the reorder buffer
        // as @c playoutDelay so the configured jitter budget actually
        // dilates the gap-fill deadline.  Without this, the reader's
        // jitter knob is dead: every reorder buffer falls back to the
        // header default of zero, and the open-internet / chaos.late
        // case can't trade latency for reorder tolerance.
        RtpSeqReorderBuffer::Config rcfg;
        if (_readerJitterMs > 0) {
                rcfg.playoutDelay = Duration::fromMilliseconds(_readerJitterMs);
        }
        s.reorderBuffer = UniquePtr<RtpSeqReorderBuffer>::create(rcfg);
        s.reorderQueue = UniquePtr<RtpPacket::Queue>::create();
        s.resetEpoch.setValue(0);

        // Hook up SSRC-change → reset-epoch bump on the session
        // signal.  Every ReaderStream that fronts a session gets its
        // own listener so the depacketizer drains its private
        // reassembly state on the next handlePacket iteration.
        s.session->ssrcChangeSignal.connect(
                [this, sptr = &s](uint32_t /*oldSsrc*/, uint32_t /*newSsrc*/, uint8_t /*pt*/) {
                        sptr->resetEpoch.fetchAndAdd(1);
                        sptr->ssrcChanges.fetchAndAdd(1);
                        if (sptr->payload != nullptr) sptr->payload->clearParamSets();
                });

        // Hook up the per-session byeReceived signal so a sender's
        // graceful shutdown surfaces as EoS on the strand-side
        // executeCmd(Read).  RFC 3550 §6.6 — the source emits a BYE
        // just before disappearing; receivers should mark it gone
        // immediately rather than waiting on a wire-silence timeout.
        //
        // We only latch the EoS flag — we do NOT cancel queue waiters
        // or stop the depacketizer here.  In-flight bundles need to
        // drain naturally so the strand reads the trailing frames
        // before observing EoS.  The strand's executeCmd(Read) loop
        // polls @c _readCancelled every @c kReadPollMs so the
        // transition is observed within a single poll interval after
        // the queue empties.
        s.session->byeReceivedSignal.connect([this](uint32_t /*ssrc*/) {
                _readCancelled.store(true, std::memory_order_release);
        });

        String                           threadName = String("rtp-rx-") + s.mediaType;
        UniquePtr<RtpDepacketizerThread> depkt;
        if (s.mediaType == "video") {
                auto *vrs = static_cast<VideoReaderStream *>(&s);
                vrs->payloadQueue = UniquePtr<Queue<RxVideoFrame>>::create();
                vrs->payloadQueue->setMaxSize(VideoPayloadQueueDepth);
                RtpVideoDepacketizerContext ctx;
                ctx.payloadQueue = vrs->payloadQueue.get();
                ctx.resetEpoch = &vrs->resetEpoch;
                ctx.active = &vrs->active;
                ctx.payload = vrs->payload;
                ctx.readerImageDesc = &vrs->readerImageDesc;
                ctx.fmtp = vrs->fmtp;
                ctx.ptpGrandmaster = vrs->ptpGrandmaster;
                ctx.clockDomain = vrs->clockDomain;
                ctx.hasSr = &vrs->hasSr;
                ctx.streamClock = &vrs->streamClock;
                ctx.packetsReceived = &vrs->packetsReceived;
                ctx.bytesReceived = &vrs->bytesReceived;
                ctx.lastPacketArrivalNs = &vrs->lastPacketArrivalNs;
                ctx.framesReassembled = &vrs->framesReassembled;
                ctx.framesDroppedValidate = &vrs->framesDroppedValidate;
                ctx.framesWaitingParamSets = &vrs->framesWaitingParamSets;
                ctx.framesDroppedSsrcReset = &vrs->framesDroppedSsrcReset;
                ctx.rxPacketInterval = &vrs->rxPacketInterval;
                ctx.rxFrameInterval = &vrs->rxFrameInterval;
                ctx.rxFrameAssembleTime = &vrs->rxFrameAssembleTime;
                ctx.noteFrameReceived = [vrs]() { vrs->framesReceived++; };
                ctx.refreshStreamClock = [this, vrs]() { refreshStreamClock(*vrs); };
                ctx.ntpToSteady = [this](const NtpTime &ntp) { return ntpToSteady(ntp); };
                depkt = UniquePtr<RtpVideoDepacketizerThread>::create(
                        std::move(ctx), String("RtpVidDepkt"), vrs->clockRate);
        } else if (s.mediaType == "audio") {
                auto *ars = static_cast<AudioReaderStream *>(&s);
                ars->payloadQueue = UniquePtr<Queue<RxAudioChunk>>::create();
                ars->payloadQueue->setMaxSize(AudioPayloadQueueDepth);
                RtpAudioDepacketizerContext ctx;
                ctx.payloadQueue = ars->payloadQueue.get();
                ctx.resetEpoch = &ars->resetEpoch;
                ctx.active = &ars->active;
                ctx.readerAudioDesc = &ars->readerAudioDesc;
                ctx.hasSr = &ars->hasSr;
                ctx.streamClock = &ars->streamClock;
                ctx.packetsReceived = &ars->packetsReceived;
                ctx.bytesReceived = &ars->bytesReceived;
                ctx.lastPacketArrivalNs = &ars->lastPacketArrivalNs;
                ctx.framesReassembled = &ars->framesReassembled;
                ctx.noteFrameReceived = [ars]() { ars->framesReceived++; };
                ctx.refreshStreamClock = [this, ars]() { refreshStreamClock(*ars); };
                ctx.ntpToSteady = [this](const NtpTime &ntp) { return ntpToSteady(ntp); };
                depkt = UniquePtr<RtpAudioDepacketizerThread>::create(
                        std::move(ctx), String("RtpAudDepkt"), ars->clockRate);
        } else {
                auto *drs = static_cast<DataReaderStream *>(&s);
                if (auto *ancPayload = dynamic_cast<RtpPayloadAnc *>(drs->payload)) {
                        drs->ancPayloadQueue = UniquePtr<Queue<RxAncFrame>>::create();
                        drs->ancPayloadQueue->setMaxSize(DataPayloadQueueDepth);
                        RtpAncDepacketizerContext ctx;
                        ctx.payloadQueue = drs->ancPayloadQueue.get();
                        ctx.resetEpoch = &drs->resetEpoch;
                        ctx.active = &drs->active;
                        ctx.payload = ancPayload;
                        ctx.clockDomain = drs->clockDomain;
                        // The static per-stream descriptor — Phase 1
                        // leaves @c desc empty (no paired-video
                        // raster plumbing yet); the aggregator
                        // forwards whatever it carries onto each
                        // produced AncPayload.
                        ctx.hasSr = &drs->hasSr;
                        ctx.streamClock = &drs->streamClock;
                        ctx.packetsReceived = &drs->packetsReceived;
                        ctx.bytesReceived = &drs->bytesReceived;
                        ctx.lastPacketArrivalNs = &drs->lastPacketArrivalNs;
                        ctx.framesReassembled = &drs->framesReassembled;
                        ctx.framesDroppedSsrcReset = &drs->framesDroppedSsrcReset;
                        ctx.noteFrameReceived = [drs]() { drs->framesReceived++; };
                        ctx.refreshStreamClock = [this, drs]() { refreshStreamClock(*drs); };
                        ctx.ntpToSteady = [this](const NtpTime &ntp) { return ntpToSteady(ntp); };
                        depkt = UniquePtr<RtpAncDepacketizerThread>::create(
                                std::move(ctx), String("RtpAncDepkt"), drs->clockRate);
                } else {
                        drs->payloadQueue = UniquePtr<Queue<RxDataMessage>>::create();
                        drs->payloadQueue->setMaxSize(DataPayloadQueueDepth);
                        RtpDataDepacketizerContext ctx;
                        ctx.payloadQueue = drs->payloadQueue.get();
                        ctx.resetEpoch = &drs->resetEpoch;
                        ctx.active = &drs->active;
                        ctx.payload = drs->payload;
                        ctx.clockDomain = drs->clockDomain;
                        ctx.hasSr = &drs->hasSr;
                        ctx.streamClock = &drs->streamClock;
                        ctx.packetsReceived = &drs->packetsReceived;
                        ctx.bytesReceived = &drs->bytesReceived;
                        ctx.lastPacketArrivalNs = &drs->lastPacketArrivalNs;
                        ctx.framesReassembled = &drs->framesReassembled;
                        ctx.framesDroppedSsrcReset = &drs->framesDroppedSsrcReset;
                        ctx.noteFrameReceived = [drs]() { drs->framesReceived++; };
                        ctx.refreshStreamClock = [this, drs]() { refreshStreamClock(*drs); };
                        ctx.ntpToSteady = [this](const NtpTime &ntp) { return ntpToSteady(ntp); };
                        depkt = UniquePtr<RtpDataDepacketizerThread>::create(
                                std::move(ctx), String("RtpDatDepkt"), drs->clockRate);
                }
        }
        s.depacketizer = std::move(depkt);

        // The depacketizer thread owns its inputQueue.  We route the
        // post-reorder output through that same queue so the recv
        // thread's reorder buffer pushes directly to where the
        // depacketizer is parked on pop().
        List<RtpSession::StreamReceiver> receivers;
        RtpSession::StreamReceiver       sr;
        sr.outQueue = &s.depacketizer->inputQueue();
        sr.seqTracker = s.seqTracker.ptr();
        sr.reorderBuffer = s.reorderBuffer.ptr();
        sr.clockRateHz = s.clockRate;
        sr.payloadType = s.payloadType;
        receivers.pushToBack(sr);

        s.depacketizer->start();

        Error recvErr = s.session->startReceiving(std::move(receivers), threadName);
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
        // be populated lazily by the @c VideoDepacketizerThread (via
        // @ref JpegGeometryProbe) from the first reassembled frame.
        // Stash the fmtp string so the geometry code can read
        // colorimetry and RANGE from it.
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
        // populated; until then the @c VideoDepacketizerThread ships
        // frames with the placeholder descriptor so downstream
        // consumers (decoder / pipeline planner) still see the
        // codec identity.
        if (!img.isValid() && _readerMode) {
                String enc = cfg.getAs<String>(MediaConfig::VideoRtpEncoding, String());
                if (enc == "H264" || enc == "h264") {
                        auto *h264 = new RtpPayloadH264(v.payloadType);
                        v.payload = h264;
                        v.clockRate = RtpPayloadH264::ClockRate;
                        v.rtpmap = String("H264/90000");
                        v.fmtp = cfg.getAs<String>(MediaConfig::VideoRtpFmtp, String());
                        // Default placeholder before SPS parse — the
                        // depacketizer will refresh from the in-band
                        // SPS on the first IDR if the SDP didn't
                        // carry sprop-parameter-sets.
                        vrs->readerImageDesc = ImageDesc(Size2Du32(0, 0), PixelFormat(PixelFormat::H264));
                        // Seed the validate() paramSet flags AND the
                        // image descriptor from the SDP fmtp's
                        // sprop-parameter-sets value (RFC 6184 §8.1).
                        // When the sender wrote its SDP after emitting
                        // the first IDR, sprop is present and the
                        // planner sees a valid ImageDesc + the
                        // reader's first AU flips Wait → Accept on
                        // the first packet.  When it isn't, the
                        // in-band path on the first IDR catches up.
                        seedH264SpropFromFmtp(h264, v.fmtp, vrs->readerImageDesc);
                        return Error::Ok;
                }
                if (enc == "H265" || enc == "h265" || enc == "HEVC" || enc == "hevc") {
                        auto *h265 = new RtpPayloadH265(v.payloadType);
                        v.payload = h265;
                        v.clockRate = RtpPayloadH265::ClockRate;
                        v.rtpmap = String("H265/90000");
                        v.fmtp = cfg.getAs<String>(MediaConfig::VideoRtpFmtp, String());
                        vrs->readerImageDesc = ImageDesc(Size2Du32(0, 0), PixelFormat(PixelFormat::HEVC));
                        // RFC 7798 §7.1 splits parameter sets across
                        // three separate fmtp values.
                        seedH265SpropFromFmtp(h265, v.fmtp, vrs->readerImageDesc);
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
        } else if (fmt.value() == MetadataRtpFormat::St2110_40.value()) {
                // RFC 8331 / SMPTE ST 2110-40 ANC.  Clock rate is fixed
                // at 90 kHz per the RFC; accept whatever the config
                // says but log if it disagrees so the operator
                // notices a mistake before it hits the wire.
                if (d.clockRate != RtpPayloadAnc::ClockRate) {
                        promekiWarn("RtpMediaIO: ANC stream clock rate %u overridden to %u "
                                    "(RFC 8331 §3.2 fixes ANC at 90 kHz)",
                                    d.clockRate, RtpPayloadAnc::ClockRate);
                        d.clockRate = RtpPayloadAnc::ClockRate;
                }
                auto *p = new RtpPayloadAnc(d.payloadType);
                d.payload = p;
                d.rtpmap = String("smpte291/") + String::number(d.clockRate);
                // RFC 8331 §6.2 DID_SDID fmtp.  An empty AncDesc emits
                // the full St291 registry — the writer side has no
                // per-format restriction by default, so receivers
                // negotiating against this SDP see every well-known
                // ST 291 format as acceptable.  AncDesc::toSdp returns
                // a complete m=application section; we strip the
                // payload-type prefix off the fmtp value so
                // @ref buildSdp 's standard fmtp emit logic can prefix
                // its own PT.
                SdpMediaDescription ancMd = AncDesc().toSdp(d.payloadType);
                const String        fmtpRaw = ancMd.attribute(String("fmtp"));
                const size_t        sp = fmtpRaw.find(' ');
                d.fmtp = sp != String::npos ? fmtpRaw.substr(sp + 1) : String();
        } else {
                promekiErr("RtpMediaIO: metadata format %s is not yet implemented", fmt.toString().cstr());
                return Error::NotSupported;
        }
        return Error::Ok;
}

void RtpMediaIO::applyClockReferenceConfig(const MediaIO::Config &cfg) {
        // Resolve the writer-side ts-refclk / mediaclk:direct
        // emission decisions once and stamp the result onto every
        // active writer Stream entry.  Reader-mode lists are left
        // alone — applySdp() owns clockDomain / ptpGrandmaster /
        // ptpDomain on the reader side.
        if (_readerMode) return;

        Error rcErr;
        Enum  modeEnum = cfg.get(MediaConfig::RtpRefClock).asEnum(RtpRefClockMode::Type, &rcErr);
        if (rcErr.isError() || !modeEnum.hasListedValue()) {
                modeEnum = RtpRefClockMode::Auto;
        }
        int        modeValue = modeEnum.value();
        EUI64      gm = cfg.getAs<EUI64>(MediaConfig::RtpPtpGrandmaster, EUI64());
        String     profile = cfg.getAs<String>(MediaConfig::RtpPtpProfile, String("IEEE1588-2008"));
        int        domainNum = cfg.getAs<int>(MediaConfig::RtpPtpDomain, 0);
        MacAddress explicitMac = cfg.getAs<MacAddress>(MediaConfig::RtpRefClockLocalMac, MacAddress());
        int32_t    offset = static_cast<int32_t>(cfg.getAs<int>(MediaConfig::RtpMediaClkOffset, 0));

        // Auto resolves to Ptp if a grandmaster was supplied,
        // otherwise LocalMac.  This keeps the typical default
        // (TPG-style smoke test, no PTP) on RFC 7273 §4.4 without
        // forcing the user to think about it; production ST 2110
        // setups override RtpRefClock = Ptp and provide the
        // grandmaster + domain explicitly, which the same code path
        // honours.
        if (modeValue == RtpRefClockMode::Auto.value()) {
                modeValue = (!gm.isNull()) ? RtpRefClockMode::Ptp.value() : RtpRefClockMode::LocalMac.value();
        }
        RtpRefClockMode resolvedMode(modeValue);

        // Pre-resolve the local MAC.  When the user did not pin one
        // explicitly, fall back to the first non-loopback interface
        // discovered through @ref NetworkInterface — the bundled POSIX
        // backend covers Linux today; ST 2110 SmartNIC vendors plug
        // their own backend in via @ref NetworkInterfaceBackend.
        MacAddress localMac = explicitMac;
        if (resolvedMode.value() == RtpRefClockMode::LocalMac.value() && localMac.isNull()) {
                localMac = NetworkInterface::firstNonLoopback().macAddress();
        }

        // For PTP, register a per-profile ClockDomain name so any
        // future receiver-side correlation has a stable identifier
        // to compare against; the actual SDP emission uses
        // ptpGrandmaster / ptpDomain directly.
        ClockDomain ptpDomainHandle;
        if (resolvedMode.value() == RtpRefClockMode::Ptp.value()) {
                ClockDomain::ID cdId = ClockDomain::registerDomain(
                        String("ptp.") + profile,
                        String("PTP reference clock (") + profile + String(")"),
                        ClockEpoch::Absolute);
                ptpDomainHandle = ClockDomain(cdId);
        }

        const auto stamp = [&](Stream &s) {
                s.tsRefClkMode = resolvedMode;
                s.refClockLocalMac = localMac;
                s.ptpGrandmaster = gm;
                s.ptpDomain = static_cast<uint8_t>(domainNum & 0xFF);
                s.mediaClkOffset = offset;
                if (resolvedMode.value() == RtpRefClockMode::Ptp.value() && ptpDomainHandle.isValid()) {
                        s.clockDomain = ptpDomainHandle;
                } else if (resolvedMode.value() == RtpRefClockMode::LocalMac.value()) {
                        // The localmac case keeps the writer's clock in
                        // a Correlated (not cross-machine) domain — the
                        // RFC 7273 receiver semantic is "the sender's
                        // own clock identified by MAC".
                        s.clockDomain = ClockDomain::SystemMonotonic;
                }
        };
        for (auto &v : _videos) stamp(v);
        for (auto &a : _audios) stamp(a);
        for (auto &d : _datas) stamp(d);
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
                // Write clock reference attributes per RFC 7273 /
                // SMPTE ST 2110-10 §6.3.  The mode driving emission
                // is resolved in @ref applyClockReferenceConfig and
                // stamped onto every writer @c Stream — buildSdp does
                // no policy here, only formatting.
                if (s.tsRefClkMode.value() == RtpRefClockMode::Ptp.value()) {
                        // RFC 7273 §4.5: ptp=<version>:<gmid>[:<domain>]
                        String profile = s.clockDomain.isValid() && s.clockDomain.name().startsWith("ptp.")
                                                 ? s.clockDomain.name().mid(4)
                                                 : String("IEEE1588-2008");
                        String tsRefClk = String("ptp=") + profile;
                        if (!s.ptpGrandmaster.isNull()) {
                                tsRefClk += String(":") + s.ptpGrandmaster.toString();
                                // Domain is meaningful only alongside a
                                // grandmaster identifier; receivers that
                                // see ":<domain>" without a gmid treat
                                // the whole tail as malformed.
                                tsRefClk += String(":") + String::number(static_cast<int>(s.ptpDomain));
                        }
                        md.setAttribute("ts-refclk", tsRefClk);
                        md.setAttribute("mediaclk", String("direct=") + String::number(s.mediaClkOffset));
                } else if (s.tsRefClkMode.value() == RtpRefClockMode::LocalMac.value() &&
                           !s.refClockLocalMac.isNull()) {
                        // RFC 7273 §4.4: localmac=<EUI-48>.  The MAC
                        // identifies the sender's local clock so a
                        // receiver can correlate streams from the same
                        // sender without trusting a shared
                        // grandmaster.  Format with hyphens to match
                        // the RFC examples (and ST 2110 reference
                        // captures); receivers parse either separator.
                        md.setAttribute("ts-refclk", String("localmac=") + s.refClockLocalMac.toString('-'));
                        md.setAttribute("mediaclk", String("direct=") + String::number(s.mediaClkOffset));
                }
                // RtpRefClockMode::None deliberately emits nothing —
                // receivers fall back to "trust the SR pair" which is
                // the legacy behaviour.
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
                        } else if (md.mediaType() == "application") {
                                // RFC 8331 / ST 2110-40 announces itself
                                // via @c smpte291 on the application
                                // m=section; flip @c DataRtpFormat so the
                                // reader-side configureDataStream creates
                                // an @ref RtpPayloadAnc rather than the
                                // JSON metadata payload.
                                if (rm.encoding == "smpte291") {
                                        cfg.set(MediaConfig::DataRtpFormat,
                                                MetadataRtpFormat::St2110_40);
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
                                // RFC 7273 §4.5: ptp=<version>[:<gmid>[:<domain>]].
                                // SMPTE ST 2110-10 always carries all three
                                // fields; older RFC 7273 senders may emit
                                // only the version, or version+gmid.
                                String     ptpValue = tsRefClk.split("=")[1];
                                String     profile = ptpValue;
                                StringList parts = ptpValue.split(":");
                                if (parts.size() >= 2) {
                                        profile = parts[0];
                                        auto [gm, gmErr] = EUI64::fromString(parts[1]);
                                        if (gmErr.isOk()) {
                                                stream->ptpGrandmaster = gm;
                                        }
                                }
                                if (parts.size() >= 3) {
                                        Error dErr;
                                        int   domainNum = parts[2].toInt(&dErr);
                                        if (dErr.isOk() && domainNum >= 0 && domainNum <= 255) {
                                                stream->ptpDomain = static_cast<uint8_t>(domainNum);
                                        }
                                }
                                ClockDomain::ID cdId = ClockDomain::registerDomain(
                                        String("ptp.") + profile, String("PTP reference clock (") + tsRefClk + ")",
                                        ClockEpoch::Absolute);
                                stream->clockDomain = ClockDomain(cdId);
                                stream->tsRefClkMode = RtpRefClockMode::Ptp;
                        } else if (!tsRefClk.isEmpty() && tsRefClk.startsWith("localmac=")) {
                                // RFC 7273 §4.4: identifies the sender's
                                // local clock by its EUI-48.  Stash the
                                // MAC so any future receiver-side cross-
                                // sender correlation has it.  The clock
                                // domain stays Correlated (per-sender)
                                // because localmac is explicitly not
                                // cross-machine-traceable.
                                String macStr = tsRefClk.mid(9);
                                auto [mac, mErr] = MacAddress::fromString(macStr);
                                if (mErr.isOk()) stream->refClockLocalMac = mac;
                                stream->clockDomain = ClockDomain(ClockDomain::registerDomain(
                                        "local", "SDP ts-refclk:localmac", ClockEpoch::Correlated));
                                stream->tsRefClkMode = RtpRefClockMode::LocalMac;
                        } else if (!tsRefClk.isEmpty() && tsRefClk.startsWith("local")) {
                                stream->clockDomain = ClockDomain(ClockDomain::registerDomain(
                                        "local", "SDP ts-refclk:local", ClockEpoch::Correlated));
                                stream->tsRefClkMode = RtpRefClockMode::LocalMac;
                        } else {
                                stream->clockDomain = ClockDomain::SystemMonotonic;
                                stream->tsRefClkMode = RtpRefClockMode::None;
                        }
                        // Parse the optional mediaclk:direct=<offset> so
                        // a receiver-side consumer can recover the
                        // sender's RTP-TS origin.  Default 0 covers the
                        // ST 2110 baseline.
                        String mediaClk = md.attribute("mediaclk");
                        if (!mediaClk.isEmpty() && mediaClk.startsWith("direct=")) {
                                Error oErr;
                                int   off = mediaClk.mid(7).toInt(&oErr);
                                if (oErr.isOk()) stream->mediaClkOffset = static_cast<int32_t>(off);
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
        if (!_readerHasAnchor) return TimeStamp();
        // Compute the signed difference (ntp - ntpAnchor) in 1/2^32 s
        // units, then convert to nanoseconds.  Using @c uint64_t
        // subtraction with a deliberate cast to @c int64_t lets the
        // sign survive when @p ntp predates the anchor (which can
        // happen for low-latency receivers picking up the very first
        // SR while the open path is still settling).
        const uint64_t a = (static_cast<uint64_t>(_readerNtpAnchor.seconds()) << 32) |
                           static_cast<uint64_t>(_readerNtpAnchor.fraction());
        const uint64_t t = (static_cast<uint64_t>(ntp.seconds()) << 32) |
                           static_cast<uint64_t>(ntp.fraction());
        const int64_t diffFractional = static_cast<int64_t>(t - a);
        // Split into whole-second and sub-second halves so the
        // multiply by 1e9 doesn't overflow on a multi-decade offset.
        const int64_t  wholeSec = diffFractional >> 32; // arithmetic shift
        const uint32_t subFracU = static_cast<uint32_t>(diffFractional & 0xFFFFFFFFu);
        const int64_t  subNs = (static_cast<int64_t>(subFracU) * 1'000'000'000LL) >> 32;
        const int64_t  totalNs = wholeSec * 1'000'000'000LL + subNs;
        return _readerSteadyAnchor + Duration::fromNanoseconds(totalNs);
}

void RtpMediaIO::pushReaderFrame(Frame frame) {
        if (!frame.isValid()) return;
        // Bounded reader queue — drop-oldest at Frame boundary when
        // the strand is too slow to drain.  Phase 3's devplan calls
        // for block-on-full here so back-pressure surfaces upstream
        // as @c reorderOutputDropped on the per-stream reorder buffer
        // rather than silent Frame drops at this stage, but the
        // existing functional matrix tolerates a structural source/
        // sink rate mismatch (e.g. TX bursting a second of frames
        // into ~400 ms) that block-on-full converts into mid-frame
        // RTP packet drops — corrupting every otherwise-deliverable
        // frame instead of cleanly dropping the oldest few.  Until
        // the TX-side pacing fix lands, drop-oldest at this stage
        // preserves frame integrity; block-on-full re-engages in
        // Phase 6 alongside the chaos matrix that exercises it.
        // The queue's @c setMaxSize was applied at open time so this
        // function stays a hot no-lock call site.
        (void)_readerQueue.pushDropOldest(std::move(frame));
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
        _openedAt = TimeStamp::now();

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
        _readerMaxDepth = cfg.getAs<int>(MediaConfig::RtpMaxReadQueueDepth, 8);
        if (_readerMaxDepth <= 0) _readerMaxDepth = 8;
        // Bound the reader-output queue.  @ref pushReaderFrame uses
        // @c Queue::pushDropOldest which only drops when @c setMaxSize
        // has installed a cap, so this call is what makes the depth
        // actually take effect.
        _readerQueue.setMaxSize(static_cast<size_t>(_readerMaxDepth));
        _wireSilenceTimeoutMs = cfg.getAs<int>(MediaConfig::RtpWireSilenceTimeoutMs, 0);
        _videoWatchdogEnabled = cfg.getAs<bool>(MediaConfig::RtpVideoWatchdogEnabled, false);

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

        // Resolve the writer-side ts-refclk / mediaclk:direct config
        // and stamp the result onto every active writer Stream so
        // @c buildSdp can emit RFC 7273 / SMPTE ST 2110-10 clock
        // reference attributes without re-reading @c cfg.
        applyClockReferenceConfig(cfg);

        // Pin the local @c (steady, wallclock) reference instant for
        // RX-side @ref Frame::captureTime stamping.  Each emitted
        // frame's wallclock NTP — derived from the per-stream
        // @ref RtpStreamClock — is converted back to a local steady
        // @ref TimeStamp via @c steadyAnchor + (frameNtp - ntpAnchor).
        // This pins the steady↔wallclock mapping at one observed
        // instant, which is accurate to the µs class on a normal
        // Linux box and remains stable across the session.
        if (_readerMode) {
                _readerSteadyAnchor = TimeStamp::now();
                _readerNtpAnchor = NtpTime::now();
                _readerHasAnchor = true;
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
                                RtpAudioTxContext txCtx;
                                txCtx.storageDesc = as.storageDesc;
                                txCtx.packetSamples = as.packetSamples;
                                txCtx.packetBytes = as.packetBytes;
                                txCtx.packetTimeUs = as.packetTimeUs;
                                txCtx.session = as.session;
                                txCtx.payload = as.payload;
                                txCtx.clockRate = as.clockRate;
                                txCtx.packetsSent = &as.packetsSent;
                                txCtx.bytesSent = &as.bytesSent;
                                txCtx.senderOctets = &as.senderOctets;
                                txCtx.silencePacketsEmitted = &as.silencePacketsEmitted;
                                txCtx.silenceSamplesEmitted = &as.silenceSamplesEmitted;
                                auto *tx = new RtpAudioTxThread(
                                        std::move(txCtx),
                                        String("RtpAudTx/") + String::number(i));
                                RtpAudioPacketizerContext pktCtx;
                                pktCtx.storageDesc = as.storageDesc;
                                pktCtx.packetSamples = as.packetSamples;
                                pktCtx.packetBytes = as.packetBytes;
                                pktCtx.prerollSamples = as.prerollSamples;
                                pktCtx.streamIdx = i;
                                pktCtx.txPacketQueue = &tx->packetQueue();
                                auto *pkt = new RtpAudioPacketizerThread(
                                        std::move(pktCtx),
                                        String("RtpAudPkt/") + String::number(i));
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

        // Spawn the aggregator now that the depacketizers are
        // running.  The aggregator pops from per-stream payload
        // queues, so it has to be the last thread up and the first
        // down — its requestStop is called before resetReaderStream
        // tears down the depacketizers, so the cancel path doesn't
        // lose any in-flight bundles.
        if (_readerMode) {
                // ANC mode only applies on the m=application section
                // and only when the data stream is configured as ANC
                // (DataRtpFormat::St2110_40 wired a Queue<RxAncFrame>
                // instead of Queue<RxDataMessage>).
                const bool anyAncActive =
                        !_dataReaders.isEmpty() &&
                        _dataReaders[0].ancPayloadQueue.isValid();
                RtpAggregatorThread::Mode mode = RtpAggregatorThread::Mode::Video;
                if (anyVideoActive) {
                        mode = RtpAggregatorThread::Mode::Video;
                } else if (anyAudioActive) {
                        mode = RtpAggregatorThread::Mode::AudioOnly;
                } else if (anyAncActive) {
                        mode = RtpAggregatorThread::Mode::AncOnly;
                } else {
                        mode = RtpAggregatorThread::Mode::DataOnly;
                }
                RtpAggregatorContext ctx;
                ctx.frameRate = _frameRate;
                ctx.videoWatchdogEnabled = _videoWatchdogEnabled;
                if (!_videoReaders.isEmpty()) {
                        VideoReaderStream &v = _videoReaders[0];
                        ctx.video.payloadQueue = v.payloadQueue.get();
                        ctx.video.lastPacketArrivalNs = &v.lastPacketArrivalNs;
                }
                if (!_audioReaders.isEmpty()) {
                        AudioReaderStream &a = _audioReaders[0];
                        ctx.audio.payloadQueue = a.payloadQueue.get();
                        ctx.audio.active = a.active;
                        ctx.audio.readerAudioDesc = &a.readerAudioDesc;
                        ctx.audio.streamClock = &a.streamClock;
                        ctx.audio.hasSr = &a.hasSr;
                        ctx.audio.clockDomain = a.clockDomain;
                }
                if (!_dataReaders.isEmpty()) {
                        DataReaderStream &d = _dataReaders[0];
                        if (d.ancPayloadQueue.isValid()) {
                                ctx.anc.payloadQueue = d.ancPayloadQueue.get();
                                ctx.anc.active = d.active;
                                ctx.anc.clockDomain = d.clockDomain;
                        } else {
                                ctx.data.payloadQueue = d.payloadQueue.get();
                                ctx.data.active = d.active;
                                ctx.data.clockDomain = d.clockDomain;
                        }
                }
                ctx.readerQueue = &_readerQueue;
                ctx.pushFrame = [this](Frame frame) {
                        pushReaderFrame(std::move(frame));
                };
                _aggregator = UniquePtr<RtpAggregatorThread>::create(
                        std::move(ctx), mode, String("RtpAggregator"));
                _aggregator->start();
        }

        // RTCP setup.  Writer mode produces SR + SDES; reader mode
        // produces RR + SDES against the per-source RtpSeqTracker
        // snapshot.  Both modes share the same scheduler thread so
        // a session that's both (when that lands) gets a single
        // compound on the wire.  CNAME is the SDES item that lets
        // receivers identify which streams come from the same
        // sender — when several streams share a CNAME (the typical
        // audio + video pair from one mediaplay process), receivers
        // know to sync them even if their SSRCs are unrelated.
        _rtcpEnabled = cfg.getAs<bool>(MediaConfig::RtpRtcpEnabled, true);
        _rtcpIntervalMs = cfg.getAs<int>(MediaConfig::RtpRtcpIntervalMs, 5000);
        _rtcpCname = cfg.getAs<String>(MediaConfig::RtpRtcpCname, String());
        if (_rtcpCname.isEmpty()) {
                // RFC 3550 §6.5.1 recommends user@host with @c host being
                // either the FQDN or — failing that — the IP address of
                // the interface used for the RTP session.  Pick the first
                // configured destination across video → audio → data
                // (writer mode) or their reader-side counterparts and
                // ask NetworkInterface for the egress IP for that
                // destination.  All sessions on this RtpMediaIO share
                // the resulting CNAME so receivers can group an A/V
                // pair from one source even when the SSRCs are
                // unrelated; pid + objectId disambiguate concurrent
                // RtpMediaIO instances on the same host.
                SocketAddress firstDest;
                auto pickFrom = [&firstDest](const auto &streamList) {
                        if (!firstDest.isNull()) return;
                        for (const auto &s : streamList) {
                                if (!s.destination.isNull()) {
                                        firstDest = s.destination;
                                        return;
                                }
                        }
                };
                pickFrom(_videos);
                pickFrom(_audios);
                pickFrom(_datas);
                pickFrom(_videoReaders);
                pickFrom(_audioReaders);
                pickFrom(_dataReaders);

                String host = pickEgressHostForCname(firstDest);
                if (host.isEmpty()) host = System::hostname();
                _rtcpCname = buildDefaultCname(Application::pid(), _objectId, host);
        }
        if (!_readerMode) {
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
        } else {
                // Reader sessions need the CNAME for their RR's
                // SDES too.
                auto setupReaderSession = [this](Stream &s) {
                        if (!s.active || s.session == nullptr) return;
                        s.session->setCname(_rtcpCname);
                };
                for (VideoReaderStream &vrs : _videoReaders) setupReaderSession(vrs);
                for (AudioReaderStream &ars : _audioReaders) setupReaderSession(ars);
                for (DataReaderStream &drs : _dataReaders) setupReaderSession(drs);
        }
        if (_rtcpEnabled) {
                _rtcpScheduler = UniquePtr<RtcpScheduler>::create(buildRtcpSchedulerContext());
                _rtcpScheduler->start();
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

        // Wake every per-stream depacketizer parked on its
        // post-reorder pop so the resetReaderStream path that
        // follows joins promptly.  Idempotent — duplicate calls are
        // no-ops once the cancel latch is set.
        for (VideoReaderStream &vrs : _videoReaders) {
                if (vrs.depacketizer.isValid()) vrs.depacketizer->requestStop();
        }
        for (AudioReaderStream &ars : _audioReaders) {
                if (ars.depacketizer.isValid()) ars.depacketizer->requestStop();
        }
        for (DataReaderStream &drs : _dataReaders) {
                if (drs.depacketizer.isValid()) drs.depacketizer->requestStop();
        }
        // Wake the aggregator if it's parked on a bounded
        // pushBlocking, and the strand-side executeCmd(Read) if it's
        // parked on a pop.  The aggregator's own requestStop also
        // cancels its input queues, but the output queue (this
        // _readerQueue) is owned by RtpMediaIO and must be cancelled
        // here so the aggregator's last pushBlocking can unwind.
        _readerQueue.cancelWaiters();
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

                // Aggregate per-source RFC 3550 §A.1 / §A.3 / §A.8
                // counters from every reader stream's seq tracker /
                // reorder buffer / SSRC-change counter.  Sums fold
                // across all kinds; first-stream-only fields use the
                // first populated kind's seq tracker so a single
                // representative sample is always reported.
                int64_t  duplicatePackets = 0;
                int64_t  reorderedPackets = 0;
                int32_t  cumulativeLost = 0;
                int64_t  ssrcChanges = 0;
                int64_t  reorderEmittedInOrder = 0;
                int64_t  reorderEmittedOnDeadline = 0;
                int64_t  reorderDroppedOverflow = 0;
                int64_t  reorderDroppedDuplicate = 0;
                int64_t  framesReassembled = 0;
                int64_t  framesDroppedValidate = 0;
                int64_t  framesWaitingParamSets = 0;
                int64_t  framesDroppedSsrcReset = 0;
                uint32_t firstExtHighestSeq = 0;
                uint32_t firstPacketsExpected = 0;
                uint8_t  firstFractionLost = 0;
                uint32_t firstInterarrivalJitter = 0;
                bool     haveSeqTrackerSample = false;
                auto accumulateReaderStream = [&](const ReaderStream &s) {
                        ssrcChanges += s.ssrcChanges.value();
                        framesReassembled += s.framesReassembled.value();
                        framesDroppedValidate += s.framesDroppedValidate.value();
                        framesWaitingParamSets += s.framesWaitingParamSets.value();
                        framesDroppedSsrcReset += s.framesDroppedSsrcReset.value();
                        if (s.seqTracker.isValid()) {
                                RtpSeqTracker::Stats ts = s.seqTracker->snapshot();
                                duplicatePackets += static_cast<int64_t>(ts.duplicatePackets);
                                reorderedPackets += static_cast<int64_t>(ts.reorderedPackets);
                                cumulativeLost += ts.cumulativeLost;
                                if (!haveSeqTrackerSample) {
                                        firstExtHighestSeq = ts.extendedHighestSeq;
                                        firstPacketsExpected = ts.expectedPackets;
                                        firstFractionLost = ts.fractionLost;
                                        firstInterarrivalJitter = ts.interarrivalJitter;
                                        haveSeqTrackerSample = true;
                                }
                        }
                        if (s.reorderBuffer.isValid()) {
                                RtpSeqReorderBuffer::Stats rs = s.reorderBuffer->snapshot();
                                reorderEmittedInOrder += static_cast<int64_t>(rs.emittedInOrder);
                                reorderEmittedOnDeadline += static_cast<int64_t>(rs.emittedOnDeadline);
                                reorderDroppedOverflow += static_cast<int64_t>(rs.droppedOnOverflow);
                                reorderDroppedDuplicate += static_cast<int64_t>(rs.droppedAsDuplicate);
                        }
                };
                for (const VideoReaderStream &vs : _videoReaders) accumulateReaderStream(vs);
                for (const AudioReaderStream &as : _audioReaders) accumulateReaderStream(as);
                for (const DataReaderStream &ds : _dataReaders) accumulateReaderStream(ds);
                if (haveSeqTrackerSample) {
                        cmd.stats.set(StatsRxExtendedHighestSeq,
                                      static_cast<int64_t>(firstExtHighestSeq));
                        cmd.stats.set(StatsRxPacketsExpected,
                                      static_cast<int64_t>(firstPacketsExpected));
                        cmd.stats.set(StatsRxFractionLost,
                                      static_cast<int64_t>(firstFractionLost));
                        cmd.stats.set(StatsRxInterarrivalJitter,
                                      static_cast<int64_t>(firstInterarrivalJitter));
                }
                cmd.stats.set(StatsRxCumulativeLost, static_cast<int64_t>(cumulativeLost));
                cmd.stats.set(StatsRxDuplicatePackets, duplicatePackets);
                cmd.stats.set(StatsRxReorderedPackets, reorderedPackets);
                cmd.stats.set(StatsRxSsrcChanges, ssrcChanges);
                cmd.stats.set(StatsRxReorderEmittedInOrder, reorderEmittedInOrder);
                cmd.stats.set(StatsRxReorderEmittedOnDeadline, reorderEmittedOnDeadline);
                cmd.stats.set(StatsRxReorderDroppedOverflow, reorderDroppedOverflow);
                cmd.stats.set(StatsRxReorderDroppedDuplicate, reorderDroppedDuplicate);
                cmd.stats.set(StatsRxFramesReassembled, framesReassembled);
                cmd.stats.set(StatsRxFramesDroppedValidate, framesDroppedValidate);
                cmd.stats.set(StatsRxFramesWaitingParamSets, framesWaitingParamSets);
                cmd.stats.set(StatsRxFramesDroppedSsrcReset, framesDroppedSsrcReset);

                // RTCP-side sender-report observability.  srObserved
                // is summed across every active reader-side
                // RtpSession; lastSrAge picks the freshest SR
                // across reader streams; firstSrLatency picks the
                // earliest first-SR observation across reader
                // streams (relative to @ref _openedAt).
                int64_t   srObserved = 0;
                TimeStamp newestSr;
                TimeStamp earliestFirstSr;
                auto      foldRtcp = [&](const ReaderStream &s) {
                        if (s.session == nullptr) return;
                        srObserved +=
                                static_cast<int64_t>(s.session->srObservedCount());
                        const RtpSession::ReceivedSr sr = s.session->receivedSr();
                        if (sr.valid && sr.arrivedAt.nanoseconds() != 0) {
                                if (newestSr.nanoseconds() == 0 ||
                                    (sr.arrivedAt - newestSr).nanoseconds() > 0) {
                                        newestSr = sr.arrivedAt;
                                }
                        }
                        const TimeStamp first = s.session->firstSrAt();
                        if (first.nanoseconds() != 0) {
                                if (earliestFirstSr.nanoseconds() == 0 ||
                                    (earliestFirstSr - first).nanoseconds() > 0) {
                                        earliestFirstSr = first;
                                }
                        }
                };
                for (const VideoReaderStream &vs : _videoReaders) foldRtcp(vs);
                for (const AudioReaderStream &as : _audioReaders) foldRtcp(as);
                for (const DataReaderStream &ds : _dataReaders) foldRtcp(ds);
                cmd.stats.set(StatsRxSrObserved, srObserved);
                if (newestSr.nanoseconds() != 0) {
                        const Duration age = TimeStamp::now() - newestSr;
                        cmd.stats.set(StatsRxLastSrAgeUs, age.microseconds());
                }
                if (earliestFirstSr.nanoseconds() != 0 &&
                    _openedAt.nanoseconds() != 0) {
                        const Duration latency = earliestFirstSr - _openedAt;
                        cmd.stats.set(StatsRxFirstSrLatencyUs, latency.microseconds());
                }

                // PayloadQueue + reader-output queue depths — first-
                // stream only on the kind-specific queues.  These are
                // live-instantaneous values (not cumulative) so they
                // surface back-pressure on a single stats call.
                if (!_videoReaders.isEmpty() && _videoReaders[0].payloadQueue.isValid()) {
                        cmd.stats.set(StatsRxVideoQueueDepth,
                                      static_cast<int64_t>(_videoReaders[0].payloadQueue->size()));
                }
                if (!_audioReaders.isEmpty() && _audioReaders[0].payloadQueue.isValid()) {
                        cmd.stats.set(StatsRxAudioQueueDepth,
                                      static_cast<int64_t>(_audioReaders[0].payloadQueue->size()));
                }
                if (!_dataReaders.isEmpty() && _dataReaders[0].payloadQueue.isValid()) {
                        cmd.stats.set(StatsRxDataQueueDepth,
                                      static_cast<int64_t>(_dataReaders[0].payloadQueue->size()));
                }
                cmd.stats.set(StatsRxReaderQueueDepth,
                              static_cast<int64_t>(_readerQueue.size()));
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
