/**
 * @file      rtpmediaio.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdint>
#include <cstring>
#include <functional>

#include <promeki/audiodesc.h>
#include <promeki/audiopayload.h>
#include <promeki/buffer.h>
#include <promeki/clockdomain.h>
#include <promeki/compressedvideopayload.h>
#include <promeki/enums.h>
#include <promeki/eui64.h>
#include <promeki/file.h>
#include <promeki/filepath.h>
#include <promeki/frame.h>
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
#include <promeki/pixelformat.h>
#include <promeki/rtpmediaio.h>
#include <promeki/rtppayload.h>
#include <promeki/rtpsession.h>
#include <promeki/sdpsession.h>
#include <promeki/thread.h>
#include <promeki/udpsocket.h>
#include <promeki/udpsockettransport.h>
#include <promeki/uncompressedvideopayload.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_DEBUG(RtpMediaIO)
PROMEKI_REGISTER_MEDIAIO_FACTORY(RtpFactory)

// ----------------------------------------------------------------------------
// SendThread — per-stream TX worker.
//
// Follows the same pattern as RtpSession::ReceiveThread (rtpsession.cpp):
// a Thread subclass that overrides run() with a simple blocking pop
// loop.  Each active stream (video, audio, data) gets its own
// SendThread so video pacing sleeps don't block the audio AES67
// cadence and vice versa.  Work items arrive via a Queue, and each
// item carries a pointer to a caller-owned Queue<Error> that the
// worker pushes the result into — the caller (executeCmd on the
// strand) does a blocking pop on that return-channel to wait.
// ----------------------------------------------------------------------------
class RtpMediaIO::SendThread : public Thread {
        public:
                SendThread(const String &name) {
                        _stopRequested.setValue(false);
                        Thread::setName(name);
                }

                ~SendThread() override {
                        requestStop();
                        // Wait for the worker to fully exit
                        // threadEntry() before we hit the vtable
                        // slice from ~SendThread → ~Thread.  The
                        // worker is still inside the user-overridden
                        // run() loop until it pops the sentinel
                        // pushed by requestStop(), and any access to
                        // *this it makes against a stale (sliced)
                        // vtable is UB.  Skip the wait when the
                        // destructor is being driven from the worker
                        // itself (pathological self-delete path) —
                        // joining ourselves would deadlock.
                        if (!isCurrentThread()) wait();
                }

                void requestStop() {
                        _stopRequested.setValue(true);
                        // Push a sentinel (null work) to unblock
                        // the blocking pop() in run().
                        _workQueue.push(TxWorkItem{nullptr, nullptr});
                }

                Queue<TxWorkItem> _workQueue;

        protected:
                void run() override {
                        while (!_stopRequested.value()) {
                                auto r = _workQueue.pop();
                                if (r.second().isError()) continue;
                                TxWorkItem item = std::move(r.first());
                                if (!item.work) continue;
                                Error err = item.work();
                                if (item.resultQueue) {
                                        item.resultQueue->push(err);
                                }
                        }
                }

        private:
                Atomic<bool> _stopRequested;
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
        s(MediaConfig::RtpJitterMs, int32_t(50));
        s(MediaConfig::RtpMaxReadQueueDepth, int32_t(4));
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
        _video.mediaType = "video";
        _audio.mediaType = "audio";
        _data.mediaType = "application";
}

RtpMediaIO::~RtpMediaIO() {
        if (isOpen()) (void)close().wait();
        resetAll();
}

// ----- Helpers -----

void RtpMediaIO::resetStream(Stream &s) {
        // Tear the per-stream transmit thread down BEFORE the
        // session and transport, so any send work that is in
        // flight when we close the stream finishes against the
        // still-valid session/transport.  requestStop() pushes a
        // sentinel to unblock the pop loop; wait() joins.
        if (s.txThread != nullptr) {
                s.txThread->requestStop();
                s.txThread->wait();
                delete s.txThread;
                s.txThread = nullptr;
        }
        if (s.session != nullptr) {
                // stopReceiving() first so the receive thread is
                // guaranteed idle before we tear down the transport
                // it is pumping.
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
        s.packetsSent = 0;
        s.bytesSent = 0;
        s.packetsReceived = 0;
        s.bytesReceived = 0;
        s.framesReceived = 0;
        s.packetsLost = 0;
        s.rtpmap.clear();
        s.fmtp.clear();
        s.active = false;
        s.readerImageDesc = ImageDesc();
        s.readerAudioDesc = AudioDesc();
        s.reasmTimestamp = 0;
        s.reasmHasTimestamp = false;
        s.reasmLastSeq = 0;
        s.reasmHaveLastSeq = false;
        s.reasmSynced = false;
        s.reasmPackets.clear();
        s.txFrameInterval.reset();
        s.txSendDuration.reset();
        s.rxPacketInterval.reset();
        s.rxFrameInterval.reset();
        s.rxFrameAssembleTime.reset();
        s.txHasLastSend = false;
        s.rxHasLastPacket = false;
        s.rxHasLastFrame = false;
        s.rxHasFrameStart = false;
        s.txLastSendStart = TimeStamp();
        s.rxLastPacketTime = TimeStamp();
        s.rxLastFrameTime = TimeStamp();
        s.rxFrameStartTime = TimeStamp();
}

void RtpMediaIO::resetAll() {
        resetStream(_video);
        resetStream(_audio);
        resetStream(_data);
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

Error RtpMediaIO::openStream(Stream &s, bool enableMulticastLoopback) {
        if (s.destination.isNull() || !s.destination.isIPv4()) {
                resetStream(s);
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

        Error err = s.transport->open();
        if (err.isError()) {
                promekiErr("RtpMediaIO: failed to open %s transport: %s", s.mediaType.cstr(), err.desc().cstr());
                resetStream(s);
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
                resetStream(s);
                return err;
        }

        // Spin up a dedicated transmit worker thread for this
        // stream.  All per-frame send work for this stream runs on
        // this thread so that, e.g., video frame-rate pacing does
        // not block the audio AES67 cadence — and so each thread
        // can independently be promoted to a real-time scheduling
        // class for ST 2110 timing in a follow-up.  This mirrors
        // the receive side, where each RtpSession already runs its
        // own @c rtp-rx thread.
        //
        // SendThread overrides run() with a simple blocking-pop
        // loop (no EventLoop), matching the ReceiveThread pattern
        // in rtpsession.cpp.
        s.txThread = new SendThread(String("rtp-tx-") + s.mediaType);
        s.txThread->start();

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

Error RtpMediaIO::openReaderStream(Stream &s, bool /*enableMulticastLoopback*/) {
        if (s.destination.isNull() || !s.destination.isIPv4()) {
                resetStream(s);
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
        // Loopback is a sender-side concept for multicast; receivers
        // always see their own host's outgoing packets as long as the
        // sender enabled loopback, so nothing to configure here.

        Error err = s.transport->open();
        if (err.isError()) {
                promekiErr("RtpMediaIO: failed to open %s reader transport: %s", s.mediaType.cstr(),
                           err.desc().cstr());
                resetStream(s);
                return err;
        }

        if (isMulticast) {
                UdpSocket *sock = s.transport->socket();
                if (sock == nullptr) {
                        promekiErr("RtpMediaIO: %s transport has no socket", s.mediaType.cstr());
                        resetStream(s);
                        return Error::Invalid;
                }
                Error jerr = _multicastInterface.isEmpty()
                                     ? sock->joinMulticastGroup(s.destination)
                                     : sock->joinMulticastGroup(s.destination, _multicastInterface);
                if (jerr.isError()) {
                        promekiErr("RtpMediaIO: join %s on %s failed: %s", s.destination.toString().cstr(),
                                   s.mediaType.cstr(), jerr.desc().cstr());
                        resetStream(s);
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
                resetStream(s);
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
        if (&s == &_video) {
                recvErr = s.session->startReceiving(
                        [this](const RtpPacket &pkt, const SocketAddress &) { onVideoPacket(pkt); }, threadName);
        } else if (&s == &_audio) {
                recvErr = s.session->startReceiving(
                        [this](const RtpPacket &pkt, const SocketAddress &) { onAudioPacket(pkt); }, threadName);
        } else {
                recvErr = s.session->startReceiving(
                        [this](const RtpPacket &pkt, const SocketAddress &) { onDataPacket(pkt); }, threadName);
        }
        if (recvErr.isError()) {
                promekiErr("RtpMediaIO: startReceiving on %s failed: %s", s.mediaType.cstr(),
                           recvErr.desc().cstr());
                resetStream(s);
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
        _video.destination = dest;
        _video.payloadType = static_cast<uint8_t>(cfg.getAs<int>(MediaConfig::VideoRtpPayloadType, 96) & 0x7F);
        _video.clockRate = static_cast<uint32_t>(cfg.getAs<int>(MediaConfig::VideoRtpClockRate, 90000));
        _video.ssrc = static_cast<uint32_t>(cfg.getAs<uint32_t>(MediaConfig::VideoRtpSsrc, 0));
        _video.dscp = cfg.getAs<int>(MediaConfig::VideoRtpDscp, 46);

        // Reader-mode JPEG: the SDP carries no geometry — RFC 2435
        // puts width/height in each packet header.  Detect JPEG from
        // the payload type (static PT 26) and create the payload
        // handler with placeholder dimensions; readerImageDesc will
        // be populated lazily by emitVideoFrame() from the first
        // reassembled frame.  Stash the fmtp string so the deferred
        // geometry code can read colorimetry and RANGE from it.
        if (!img.isValid() && _readerMode && _video.payloadType == 26) {
                _video.payload = new RtpPayloadJpeg(0, 0);
                _video.rtpmap = String("JPEG/") + String::number(_video.clockRate);
                _video.fmtp = cfg.getAs<String>(MediaConfig::VideoRtpFmtp, String());
                return Error::Ok;
        }

        if (!img.isValid()) {
                promekiErr("RtpMediaIO: VideoRtpDestination set but no "
                           "video track in media descriptor (set VideoSize + "
                           "VideoPixelFormat, or supply a MediaDesc)");
                return Error::InvalidArgument;
        }

        _video.readerImageDesc = img;
        const PixelFormat &pd = img.pixelFormat();

        // Use ImageDesc::toSdp() to derive the rtpmap, fmtp
        // (including colorimetry + RANGE), and encoding name.
        // This keeps all PixelFormat → SDP mapping in one place.
        SdpMediaDescription sdpMd = img.toSdp(_video.payloadType);
        if (sdpMd.mediaType().isEmpty()) {
                promekiErr("RtpMediaIO: unsupported video pixel format '%s'", pd.name().cstr());
                return Error::NotSupported;
        }

        // Extract rtpmap and fmtp from the SdpMediaDescription.
        for (size_t i = 0; i < sdpMd.attributes().size(); i++) {
                const String &key = sdpMd.attributes()[i].first();
                const String &val = sdpMd.attributes()[i].second();
                if (key == "rtpmap") {
                        // Strip the "<pt> " prefix — _video.rtpmap
                        // stores just the encoding/clock part.
                        size_t sp = val.find(' ');
                        _video.rtpmap = (sp != String::npos) ? val.mid(sp + 1) : val;
                } else if (key == "fmtp") {
                        size_t sp = val.find(' ');
                        _video.fmtp = (sp != String::npos) ? val.mid(sp + 1) : val;
                }
        }

        // toSdp may remap the payload type (e.g. 96 → 26 for JPEG).
        if (!sdpMd.payloadTypes().isEmpty()) {
                _video.payloadType = sdpMd.payloadTypes()[0];
        }

        // Pick payload class from the pixel descriptor family.
        if (isJpegPixelFormat(pd)) {
                _video.payload = new RtpPayloadJpeg(static_cast<int>(img.width()), static_cast<int>(img.height()));
        } else if (isJpegXsPixelFormat(pd)) {
                auto *jxs = new RtpPayloadJpegXs(static_cast<int>(img.width()), static_cast<int>(img.height()),
                                                 _video.payloadType);
                _video.payload = jxs;
                _video.clockRate = RtpPayloadJpegXs::ClockRate;
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
                _video.payload = new RtpPayloadRawVideo(static_cast<int>(img.width()), static_cast<int>(img.height()),
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

        _audio.readerAudioDesc = ad;
        _audio.destination = dest;
        _audio.payloadType = static_cast<uint8_t>(cfg.getAs<int>(MediaConfig::AudioRtpPayloadType, 96) & 0x7F);
        _audio.ssrc = cfg.getAs<uint32_t>(MediaConfig::AudioRtpSsrc, 0);
        _audio.dscp = cfg.getAs<int>(MediaConfig::AudioRtpDscp, 34);

        int cfgClockRate = cfg.getAs<int>(MediaConfig::AudioRtpClockRate, 0);
        _audio.clockRate =
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

        _audioState.packetSamples = resolvedSamples;
        _audioState.packetBytes = resolvedSamples * static_cast<size_t>(ch) * kStorageBytesPerSample;
        _audioState.packetTimeUs = resolvedUs;
        _audioState.nextTimestamp = 0;

        // -- Payload handler --
        //
        // L16 only for this first pass.  Clamp the payload's
        // max-payload-size to exactly packetBytes so pack() emits one
        // RtpPacket per AES67 packet (instead of trying to pack more
        // into a single datagram).
        auto *pl16 = new RtpPayloadL16(sr, static_cast<int>(ch));
        pl16->setPayloadType(_audio.payloadType);
        pl16->setMaxPayloadSize(_audioState.packetBytes);
        _audio.payload = pl16;
        _audio.rtpmap = String("L16/") + String::number(_audio.clockRate) + String("/") + String::number(ch);

        // -- FIFO --
        //
        // Storage format = PCMI_S16BE.  AudioBuffer will
        // transparently convert any compatible input (S16LE, S16BE,
        // Float32LE, Float32BE, S24LE/BE, S32LE/BE, etc.) into the
        // storage format on push, so the backend accepts whatever
        // the pipeline hands it provided the sample rate and channel
        // count already match.  Rate / channel conversion still
        // belongs to an upstream CSC stage.
        AudioDesc storageDesc(AudioFormat::PCMI_S16BE, ad.sampleRate(), ad.channels());
        if (!storageDesc.isValid()) {
                promekiErr("RtpMediaIO: could not build L16 storage descriptor (%.1f Hz, %u ch)", ad.sampleRate(),
                           ad.channels());
                return Error::InvalidArgument;
        }
        _audioState.fifo = AudioBuffer(storageDesc);
        // Reserve one second of headroom — generous enough to absorb
        // any reasonable writeFrame burstiness without the producer
        // ever hitting NoSpace.
        Error rsvErr = _audioState.fifo.reserve(static_cast<size_t>(ad.sampleRate()));
        if (rsvErr.isError()) {
                promekiErr("RtpMediaIO: failed to reserve audio FIFO: %s", rsvErr.desc().cstr());
                return rsvErr;
        }

        return Error::Ok;
}

Error RtpMediaIO::configureDataStream(const MediaIO::Config &cfg) {
        bool          enabled = cfg.getAs<bool>(MediaConfig::DataEnabled, false);
        SocketAddress dest = cfg.getAs<SocketAddress>(MediaConfig::DataRtpDestination, SocketAddress());
        if (!enabled || dest.isNull()) return Error::Ok; // Disabled.

        _data.destination = dest;
        _data.payloadType = static_cast<uint8_t>(cfg.getAs<int>(MediaConfig::DataRtpPayloadType, 98) & 0x7F);
        _data.clockRate = static_cast<uint32_t>(cfg.getAs<int>(MediaConfig::DataRtpClockRate, 90000));
        _data.ssrc = static_cast<uint32_t>(cfg.getAs<uint32_t>(MediaConfig::DataRtpSsrc, 0));
        _data.dscp = cfg.getAs<int>(MediaConfig::DataRtpDscp, 34);

        Error fmtErr;
        Enum  fmt = cfg.get(MediaConfig::DataRtpFormat).asEnum(MetadataRtpFormat::Type, &fmtErr);
        if (fmtErr.isError() || !fmt.hasListedValue()) {
                promekiErr("RtpMediaIO: unknown metadata RTP format");
                return Error::InvalidArgument;
        }
        _dataFormat = fmt;

        if (fmt.value() == MetadataRtpFormat::JsonMetadata.value()) {
                auto *p = new RtpPayloadJson(_data.payloadType, _data.clockRate);
                _data.payload = p;
                _data.rtpmap = String("x-promeki-metadata-json/") + String::number(_data.clockRate);
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
                if (!s.fmtp.isEmpty()) {
                        md.setAttribute("fmtp", String::number(s.payloadType) + String(" ") + s.fmtp);
                }
                // RFC 5761 rtcp-mux: tells receivers that RTP and
                // RTCP share a single port for this stream, so the
                // receiver does not open a separate socket at RTP
                // port + 1.  Without this, ffplay (and any other SDP
                // consumer that honours the classic RTCP-on-next-port
                // convention) tries to bind port + 1 for RTCP, which
                // collides when the user has two streams on adjacent
                // ports (e.g. video 10000 / audio 10001).  Since this
                // backend does not transmit RTCP at all — we are a
                // pure sender — multiplexing is a cosmetic lie that
                // has zero wire impact but unblocks the receiver's
                // socket setup.
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
        addStream(_video);
        addStream(_audio);
        addStream(_data);
}

Error RtpMediaIO::writeSdpFile(const String &path) {
        if (path.isEmpty()) return Error::Ok;
        return _sdpSession.toFile(path);
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
                        if (md.mediaType() == "audio") {
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
                        if (md.mediaType() == "video")
                                stream = &_video;
                        else if (md.mediaType() == "audio")
                                stream = &_audio;
                        else
                                stream = &_data;
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

void RtpMediaIO::onVideoPacket(const RtpPacket &pkt) {
        if (!_video.active) return;

        _video.packetsReceived++;
        _video.bytesReceived += static_cast<int64_t>(pkt.size());

        // Sync gate: when the receiver joins a stream that's
        // already in progress, the first packets it sees belong to
        // the tail of an incomplete frame.  Passing those to
        // emitVideoFrame produces a truncated bitstream that the
        // codec rejects ("decoder_init failed").  To avoid that,
        // we stay in a pre-sync state until we see a marker bit,
        // which marks the END of a complete frame.  The very next
        // packet after that marker is the first packet of the next
        // frame — from that point on reassembly is clean.
        //
        // For intra-only codecs (JPEG, JPEG XS) every frame is a
        // key frame, so the first post-sync frame is always
        // independently decodable.  For inter-coded formats (H.264,
        // HEVC, when they land) this would need to additionally
        // wait for an IDR/SPS/PPS boundary.
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
        if (!_video.reasmSynced) {
                if (pkt.marker()) {
                        _video.reasmSynced = true;
                        // Fall through — let the normal path below
                        // accumulate this marker packet and call
                        // emitVideoFrame.  Clean starts succeed;
                        // mid-joins produce a short/corrupt buffer
                        // that emitVideoFrame discards.
                } else {
                        // Still pre-sync, no marker yet.  Accumulate
                        // anyway (the frame MIGHT be complete if this
                        // is a clean start) — the marker will tell us.
                        // Fall through to normal accumulation.
                }
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
        if (_video.rxHasLastPacket) {
                const Duration delta = now - _video.rxLastPacketTime;
                _video.rxPacketInterval.addSample(delta.microseconds());
        }
        _video.rxLastPacketTime = now;
        _video.rxHasLastPacket = true;

        if (_video.reasmHasTimestamp && _video.reasmTimestamp != pkt.timestamp() && !_video.reasmPackets.isEmpty()) {
                // Timestamp changed without a prior marker bit —
                // emit whatever we have and start fresh.
                emitVideoFrame();
        }

        // Mark the start of a new reassembly window if this packet
        // is the first one for its timestamp.
        if (_video.reasmPackets.isEmpty()) {
                _video.rxFrameStartTime = now;
                _video.rxHasFrameStart = true;
        }

        // Copy the packet into our reassembly list.  The incoming
        // RtpPacket is a view onto the receive thread's scratch
        // buffer, which is freshly allocated per packet, so we can
        // just take ownership of its backing Buffer::Ptr by
        // referencing the same RtpPacket.
        _video.reasmPackets.pushToBack(pkt);
        _video.reasmTimestamp = pkt.timestamp();
        _video.reasmHasTimestamp = true;
        _video.reasmLastSeq = pkt.sequenceNumber();
        _video.reasmHaveLastSeq = true;

        if (pkt.marker()) {
                emitVideoFrame();
        }
}

void RtpMediaIO::emitVideoFrame() {
        if (_video.reasmPackets.isEmpty()) return;
        if (_video.payload == nullptr) {
                _video.reasmPackets.clear();
                _video.reasmHasTimestamp = false;
                return;
        }

        // Deferred JPEG geometry: peek at the first packet's
        // RFC 2435 header for the Type field (subsampling) before
        // unpack() consumes the packet list.  Type is at byte 4
        // of the 8-byte payload header:
        //   Type 0 → YCbCr 4:2:2  (FFmpeg convention)
        //   Type 1 → YCbCr 4:2:0
        uint8_t rfc2435Type = 0;
        if (!_video.readerImageDesc.isValid() && !_video.reasmPackets.isEmpty()) {
                const RtpPacket &first = _video.reasmPackets[0];
                if (!first.isNull() && first.payloadSize() >= 8) {
                        rfc2435Type = first.payload()[4];
                }
        }

        // Capture per-frame RTP metadata before unpack clears the list.
        const uint32_t frameRtpTimestamp = _video.reasmTimestamp;
        const int32_t  framePacketCount = static_cast<int32_t>(_video.reasmPackets.size());

        // Ask the payload class to reassemble the bitstream.
        Buffer reassembled = _video.payload->unpack(_video.reasmPackets);
        _video.reasmPackets.clear();
        _video.reasmHasTimestamp = false;

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
        // _video.fmtp by configureVideoStream).
        if (!_video.readerImageDesc.isValid()) {
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
                // (stashed on _video.fmtp).  When absent, the
                // helper defaults to Rec.601 full range.
                String colorimetry;
                String range;
                if (!_video.fmtp.isEmpty()) {
                        // Quick parse of "key=val;key=val;..." form.
                        StringList parts = _video.fmtp.split(";");
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
                _video.readerImageDesc = ImageDesc(Size2Du32(w, h), PixelFormat(pdId));
                promekiInfo("RtpMediaIO: JPEG reader discovered "
                            "%ux%u %s from first frame",
                            w, h, PixelFormat(pdId).name().cstr());
        }

        // Build a payload from the reassembled buffer.  Both the
        // compressed and uncompressed paths copy the reassembled
        // bytes into a fresh Buffer::Ptr that the payload adopts as
        // plane 0.
        Buffer::Ptr plane = Buffer::Ptr::create(reassembled.size());
        std::memcpy(plane->data(), reassembled.data(), reassembled.size());
        plane->setSize(reassembled.size());
        const PixelFormat &pd = _video.readerImageDesc.pixelFormat();

        _video.framesReceived++;

        // Record assemble time (first packet -> here) and the
        // wall-clock interval between successive complete frames.
        const TimeStamp emitTime = TimeStamp::now();
        if (_video.rxHasFrameStart) {
                const Duration assemble = emitTime - _video.rxFrameStartTime;
                _video.rxFrameAssembleTime.addSample(assemble.microseconds());
                _video.rxHasFrameStart = false;
        }
        if (_video.rxHasLastFrame) {
                const Duration delta = emitTime - _video.rxLastFrameTime;
                _video.rxFrameInterval.addSample(delta.microseconds());
        }
        _video.rxLastFrameTime = emitTime;
        _video.rxHasLastFrame = true;

        // Stamp the payload with RTP and capture metadata before
        // handing it to the Frame.  CaptureTime is when the library
        // saw the first packet of this frame (rxFrameStartTime); the
        // payload's native pts/dts are set from the same value below.
        MediaTimeStamp capMts(_video.rxFrameStartTime, _video.clockDomain);
        ImageDesc      idesc = _video.readerImageDesc;
        {
                Metadata &m = idesc.metadata();
                m.set(Metadata::CaptureTime, capMts);
                m.set(Metadata::RtpTimestamp, frameRtpTimestamp);
                m.set(Metadata::RtpPacketCount, framePacketCount);
                if (!_video.ptpGrandmaster.isNull()) {
                        m.set(Metadata::PtpGrandmasterId, _video.ptpGrandmaster);
                }
        }

        VideoPayload::Ptr videoPayload;
        if (pd.isCompressed()) {
                // Compressed streams: every intraframe RTP payload is
                // a keyframe (no inter-frame prediction at this layer).
                auto cvp = CompressedVideoPayload::Ptr::create(idesc, plane);
                cvp.modify()->setPts(capMts);
                cvp.modify()->setDts(capMts);
                cvp.modify()->addFlag(MediaPayload::Keyframe);
                videoPayload = cvp;
        } else {
                BufferView planes;
                planes.pushToBack(plane, 0, plane->size());
                auto uvp = UncompressedVideoPayload::Ptr::create(idesc, planes);
                uvp.modify()->setPts(capMts);
                videoPayload = uvp;
        }

        if (!videoPayload.isValid()) {
                if (_video.framesReceived <= 1) {
                        promekiDebug("RtpMediaIO: discarding "
                                     "first partial video frame "
                                     "(joined stream mid-flight)");
                } else {
                        promekiWarn("RtpMediaIO: reassembled "
                                    "video frame is invalid");
                }
                return;
        }

        Frame::Ptr frame = Frame::Ptr::create();
        Frame     *f = frame.modify();
        f->addPayload(std::move(videoPayload));

        // Aggregate audio: drain one frame's worth of samples from
        // the FIFO that the audio RX thread is filling.  If the
        // samples haven't arrived yet, wait up to the configured
        // jitter timeout (typically ~50 ms) for the audio RX thread
        // to push them.  If they still aren't there after the wait,
        // emit the frame with whatever audio is available (possibly
        // none) rather than stalling the video clock.
        if (_audio.active && _audio.readerAudioDesc.isValid()) {
                const size_t needed = _frameRate.samplesPerFrame(
                        static_cast<int64_t>(_audio.readerAudioDesc.sampleRate()), _readerAgg.videoFrameIndex.value());
                if (needed > 0) {
                        size_t      bufBytes = _audio.readerAudioDesc.bufferSize(needed);
                        Buffer::Ptr pcm = Buffer::Ptr::create(bufBytes);
                        auto [got, err] = _readerAgg.audioFifo.popWait(
                                pcm.modify()->data(), needed, static_cast<unsigned int>(_readerAgg.audioTimeoutMs));
                        if (got > 0) {
                                size_t usedBytes = _audio.readerAudioDesc.bufferSize(got);
                                pcm.modify()->setSize(usedBytes);
                                BufferView view(pcm, 0, usedBytes);
                                auto audioPayload = PcmAudioPayload::Ptr::create(_audio.readerAudioDesc, got, view);
                                ClockDomain audioCd =
                                        _audio.clockDomain.isValid() ? _audio.clockDomain : _video.clockDomain;
                                MediaTimeStamp audMts(_video.rxFrameStartTime, audioCd);
                                audioPayload.modify()->desc().metadata().set(Metadata::CaptureTime, audMts);
                                audioPayload.modify()->setPts(audMts);
                                f->addPayload(audioPayload);
                        }
                }
        }
        _readerAgg.videoFrameIndex++;

        // Aggregate metadata: grab the latest snapshot from the
        // data RX thread and merge it into this frame.
        if (_data.active) {
                Mutex::Locker lock(_readerAgg.dataMutex);
                if (_readerAgg.hasMetadata) {
                        f->metadata() = _readerAgg.pendingMetadata;
                        _readerAgg.hasMetadata = false;
                }
        }

        pushReaderFrame(std::move(frame));
}

void RtpMediaIO::onAudioPacket(const RtpPacket &pkt) {
        if (!_audio.active) return;
        _audio.packetsReceived++;
        _audio.bytesReceived += static_cast<int64_t>(pkt.size());

        // L16 arrives as big-endian samples directly.  Push them
        // into the aggregator's audio FIFO so emitVideoFrame can
        // drain one frame's worth of samples when the next video
        // frame completes.  The FIFO is protected by a Mutex and
        // a WaitCondition because this runs on the audio RX thread
        // while emitVideoFrame runs on the video RX thread.
        if (_audio.payload == nullptr || !_audio.readerAudioDesc.isValid()) return;
        if (pkt.payloadSize() == 0) return;

        const unsigned int ch = _audio.readerAudioDesc.channels();
        constexpr size_t   bytesPerSample = 2;
        const size_t       frameBytes = ch * bytesPerSample;
        if (frameBytes == 0) return;
        const size_t samples = pkt.payloadSize() / frameBytes;
        if (samples == 0) return;

        AudioDesc wireDesc(AudioFormat::PCMI_S16BE, _audio.readerAudioDesc.sampleRate(), ch);

        // When video is active, push samples into the aggregation
        // FIFO so emitVideoFrame can merge them into combined
        // frames.  When video is NOT active (audio-only stream),
        // fall back to the original per-chunk emission so the
        // reader queue still produces output.
        if (_video.active) {
                Error perr = _readerAgg.audioFifo.push(pkt.payload(), samples, wireDesc);
                if (perr.isError()) {
                        promekiWarn("RtpMediaIO: audio FIFO push failed: %s", perr.desc().cstr());
                        return;
                }
                _audio.framesReceived++;
        } else {
                // Audio-only stream — push directly.  Chunk into
                // samplesPerFrame-sized Audio objects to match the
                // frame-rate cadence downstream consumers expect.
                Error perr = _audioState.fifo.push(pkt.payload(), samples, wireDesc);
                if (perr.isError()) {
                        promekiWarn("RtpMediaIO: audio FIFO push failed: %s", perr.desc().cstr());
                        return;
                }
                const double fps = _frameRate.isValid() ? _frameRate.toDouble() : 30.0;
                if (fps <= 0.0) return;
                const size_t spf = static_cast<size_t>(_audio.readerAudioDesc.sampleRate() / fps);
                if (spf == 0) return;
                while (_audioState.fifo.available() >= spf) {
                        size_t      bufBytes = _audio.readerAudioDesc.bufferSize(spf);
                        Buffer::Ptr pcm = Buffer::Ptr::create(bufBytes);
                        auto [got, popErr] = _audioState.fifo.pop(pcm.modify()->data(), spf);
                        if (popErr.isError() || got == 0) break;
                        size_t usedBytes = _audio.readerAudioDesc.bufferSize(got);
                        pcm.modify()->setSize(usedBytes);
                        BufferView     view(pcm, 0, usedBytes);
                        auto           audioPayload = PcmAudioPayload::Ptr::create(_audio.readerAudioDesc, got, view);
                        MediaTimeStamp capMts(TimeStamp::now(), _audio.clockDomain);
                        audioPayload.modify()->desc().metadata().set(Metadata::CaptureTime, capMts);
                        audioPayload.modify()->setPts(capMts);
                        _audio.framesReceived++;
                        Frame::Ptr frame = Frame::Ptr::create();
                        frame.modify()->addPayload(audioPayload);
                        pushReaderFrame(std::move(frame));
                }
        }
}

void RtpMediaIO::onDataPacket(const RtpPacket &pkt) {
        if (!_data.active) return;
        _data.packetsReceived++;
        _data.bytesReceived += static_cast<int64_t>(pkt.size());

        if (_data.reasmHasTimestamp && _data.reasmTimestamp != pkt.timestamp() && !_data.reasmPackets.isEmpty()) {
                emitDataMessage();
        }

        _data.reasmPackets.pushToBack(pkt);
        _data.reasmTimestamp = pkt.timestamp();
        _data.reasmHasTimestamp = true;

        if (pkt.marker()) {
                emitDataMessage();
        }
}

void RtpMediaIO::emitDataMessage() {
        if (_data.reasmPackets.isEmpty()) return;
        if (_data.payload == nullptr) {
                _data.reasmPackets.clear();
                _data.reasmHasTimestamp = false;
                return;
        }
        // Capture per-message RTP metadata before unpack clears the list.
        const uint32_t dataRtpTimestamp = _data.reasmTimestamp;
        const int32_t  dataPacketCount = static_cast<int32_t>(_data.reasmPackets.size());

        Buffer bytes = _data.payload->unpack(_data.reasmPackets);
        _data.reasmPackets.clear();
        _data.reasmHasTimestamp = false;
        if (bytes.size() == 0) return;

        String     jsonText(static_cast<const char *>(bytes.data()), bytes.size());
        Error      jerr;
        JsonObject obj = JsonObject::parse(jsonText, &jerr);
        if (jerr.isError()) {
                promekiWarn("RtpMediaIO: dropping malformed metadata JSON: %s", jerr.desc().cstr());
                return;
        }
        Metadata m = Metadata::fromJson(obj);
        _data.framesReceived++;

        // Stamp the metadata with capture and RTP timing.
        MediaTimeStamp capMts(TimeStamp::now(), _data.clockDomain);
        m.set(Metadata::CaptureTime, capMts);
        m.set(Metadata::RtpTimestamp, dataRtpTimestamp);
        m.set(Metadata::RtpPacketCount, dataPacketCount);

        // When video is active, stash metadata so emitVideoFrame
        // can merge it into combined frames.  When video is NOT
        // active (data-only or audio+data stream), push directly.
        if (_video.active) {
                Mutex::Locker lock(_readerAgg.dataMutex);
                _readerAgg.pendingMetadata = m;
                _readerAgg.hasMetadata = true;
        } else {
                Frame::Ptr frame = Frame::Ptr::create();
                frame.modify()->metadata() = m;
                pushReaderFrame(std::move(frame));
        }
}

void RtpMediaIO::pushReaderFrame(Frame::Ptr frame) {
        if (!frame) return;
        // Enforce the configured reader queue depth by dropping the
        // oldest frame when the queue is full.  The producer side
        // is our own RX thread, and stalling it would mean dropped
        // wire packets — dropping at the Frame::Ptr boundary is the
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

        // Transport-global parameters.
        _localAddress = cfg.getAs<SocketAddress>(MediaConfig::RtpLocalAddress, SocketAddress::any(0));
        _sessionName = cfg.getAs<String>(MediaConfig::RtpSessionName, String("promeki RTP stream"));
        _sessionOrigin = cfg.getAs<String>(MediaConfig::RtpSessionOrigin, String("-"));
        _multicastTTL = cfg.getAs<int>(MediaConfig::RtpMulticastTTL, 64);
        _multicastInterface = cfg.getAs<String>(MediaConfig::RtpMulticastInterface, String());
        _sdpPath = cfg.getAs<String>(MediaConfig::RtpSaveSdpPath, String());

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
        if (_readerMode && _audio.readerAudioDesc.isValid()) {
                AudioDesc wireFormat(AudioFormat::PCMI_S16BE, _audio.readerAudioDesc.sampleRate(),
                                     _audio.readerAudioDesc.channels());
                _readerAgg.audioFifo.setFormat(wireFormat);
                _readerAgg.audioFifo.setInputFormat(wireFormat);
                const size_t headroom = static_cast<size_t>(_audio.readerAudioDesc.sampleRate() * 2);
                _readerAgg.audioFifo.reserve(headroom);
                _readerAgg.videoFrameIndex = 0;
                _readerAgg.hasMetadata = false;
        }

        // Enable multicast loopback when the destination is on this
        // host — lets a co-located receiver see our own packets.  Not
        // critical for production but useful for self-tests.
        auto isLocalMulticast = [](const SocketAddress &a) {
                return a.isMulticast() || a.isLoopback();
        };
        bool loopback = isLocalMulticast(_video.destination) || isLocalMulticast(_audio.destination) ||
                        isLocalMulticast(_data.destination);

        if (_readerMode) {
                err = openReaderStream(_video, loopback);
                if (err.isError()) {
                        resetAll();
                        return err;
                }
                err = openReaderStream(_audio, loopback);
                if (err.isError()) {
                        resetAll();
                        return err;
                }
                err = openReaderStream(_data, loopback);
                if (err.isError()) {
                        resetAll();
                        return err;
                }
        } else {
                err = openStream(_video, loopback);
                if (err.isError()) {
                        resetAll();
                        return err;
                }
                err = openStream(_audio, loopback);
                if (err.isError()) {
                        resetAll();
                        return err;
                }
                err = openStream(_data, loopback);
                if (err.isError()) {
                        resetAll();
                        return err;
                }
        }

        // At least one stream must be active.
        if (!_video.active && !_audio.active && !_data.active) {
                promekiErr("RtpMediaIO: no RTP streams configured "
                           "(set VideoRtpDestination / AudioRtpDestination / DataRtpDestination)");
                resetAll();
                return Error::InvalidArgument;
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
                if (videoBitrate == 0 && _video.active && !cmd.pendingMediaDesc.imageList().isEmpty()) {
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
                applyRate(_video, videoBitrate);

                // Audio: sample_rate * channels * bytes_per_sample * 8.
                if (_audio.active && !cmd.pendingMediaDesc.audioList().isEmpty()) {
                        const AudioDesc &ad = cmd.pendingMediaDesc.audioList()[0];
                        uint64_t         audioBitrate =
                                static_cast<uint64_t>(ad.sampleRate() * ad.channels() * ad.bytesPerSample() * 8);
                        applyRate(_audio, audioBitrate);
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
                if (_video.active && _video.readerImageDesc.isValid() && resolved.imageList().isEmpty()) {
                        resolved.imageList().pushToBack(_video.readerImageDesc);
                }
                if (_audio.active && _audio.readerAudioDesc.isValid() && resolved.audioList().isEmpty()) {
                        resolved.audioList().pushToBack(_audio.readerAudioDesc);
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
                dumpIfPopulated(_video.rxPacketInterval);
                dumpIfPopulated(_video.rxFrameInterval);
                dumpIfPopulated(_video.rxFrameAssembleTime);
        } else {
                dumpIfPopulated(_video.txFrameInterval);
                dumpIfPopulated(_video.txSendDuration);
        }

        resetAll();
        return Error::Ok;
}

Error RtpMediaIO::executeCmd(MediaIOCommandRead &cmd) {
        if (!_readerMode) return Error::NotSupported;

        // Pop a frame from the reader output queue with a bounded
        // wait.  Timeout translates to TryAgain so the MediaIO
        // strand can probe the queue periodically without busy-
        // waiting — a live RTP stream has no "end-of-file", so the
        // only terminal state is Close.
        constexpr unsigned int kReadTimeoutMs = 500;
        Result<Frame::Ptr>     result = _readerQueue.pop(kReadTimeoutMs);
        if (result.second() == Error::Timeout) {
                return Error::TryAgain;
        }
        if (result.second().isError()) {
                return result.second();
        }
        cmd.frame = result.first();
        ++_frameCount;
        cmd.currentFrame = toFrameNumber(_frameCount);
        return Error::Ok;
}

// ----- Per-stream send helpers -----

Error RtpMediaIO::sendVideo(const VideoPayload &payload, const FrameNumber &frameIndex) {
        if (!_video.active || !payload.isValid()) return Error::Ok;
        if (_video.session == nullptr || _video.payload == nullptr) return Error::Invalid;

        // Diagnostic timing capture.  Updated only on this stream's
        // dedicated TX worker thread, so the histograms need no
        // internal locking.  txFrameInterval measures wall-clock
        // between successive sendVideo entries (target = frame
        // period); txSendDuration measures the total time the call
        // spends in here, which is dominated by the per-packet
        // pacing sleep.
        const TimeStamp callStart = TimeStamp::now();
        if (_video.txHasLastSend) {
                const Duration delta = callStart - _video.txLastSendStart;
                _video.txFrameInterval.addSample(delta.microseconds());
        }
        _video.txLastSendStart = callStart;
        _video.txHasLastSend = true;

        // Compute the RTP timestamp via FrameRate::cumulativeTicks(),
        // which returns the exact cumulative tick count at the start
        // of this frame using 64-bit rational math.  This is the
        // same primitive that drives samplesPerFrame for audio, so
        // the video RTP timestamps stay drift-free across both
        // integer rates (exact stride every frame) and fractional
        // rates (alternating stride within the 1001-frame NTSC
        // period).  Truncating to uint32_t produces the wire value
        // with natural modulo-2^32 wrap.
        //
        // The frame index is passed in by the caller (the strand
        // thread) so this worker thread can run in parallel with the
        // strand without racing on @c _frameCount.
        uint32_t ts = static_cast<uint32_t>(_frameRate.cumulativeTicks(_video.clockRate, frameIndex.value()));

        // Grab plane 0 bytes — for compressed payloads this is the
        // bitstream; for RFC 4175 raw video it's the interleaved
        // pixel data.  When the input pixel format doesn't match
        // the RFC 4175 wire format (e.g. YUYV instead of UYVY),
        // convert first.  CSC only applies to uncompressed payloads
        // — compressed bitstreams are always transmitted verbatim.
        const VideoPayload           *src = &payload;
        UncompressedVideoPayload::Ptr converted;
        if (_videoWirePixelFormat.isValid() && !payload.isCompressed() &&
            payload.desc().pixelFormat().id() != _videoWirePixelFormat.id()) {
                const auto *uvp = payload.as<UncompressedVideoPayload>();
                if (uvp == nullptr) return Error::Invalid;
                converted = uvp->convert(_videoWirePixelFormat, Metadata());
                if (!converted.isValid()) return Error::Invalid;
                src = converted.ptr();
        }

        if (src->planeCount() == 0) return Error::Invalid;
        auto plane0 = src->plane(0);
        if (!plane0.isValid() || plane0.size() == 0) return Error::Invalid;

        auto packets = _video.payload->pack(plane0.data(), plane0.size());
        if (packets.isEmpty()) return Error::Invalid;

        // VBR compressed video: per-frame kernel pacing rate update.
        //
        // For uncompressed RFC 4175 video the open-time @c setPacingRate
        // call is enough — the per-frame byte count is constant
        // (width × height × bpp) so a single rate cap exactly matches
        // every frame.  Compressed formats (JPEG, JPEG XS) are
        // variable-rate, so a single open-time cap can never honour
        // each individual frame's wall-clock window.  Instead, before
        // each frame is dispatched we set the cap to that frame's
        // own (bytes × fps), so the kernel @c fq qdisc paces this
        // frame's packets to drain over exactly one frame interval.
        // The rate is recomputed every frame, so VBR streams stay
        // matched without any prior knowledge of the bitrate.
        //
        // The cap takes effect on the very next dispatch, including
        // any packets still queued from the previous frame, so when
        // adjacent frames differ in size the trailing portion of the
        // previous frame is repaced at the new rate.  This is fine
        // for typical broadcast content where successive frames are
        // close in size; for pathological alternating-size streams
        // the proper answer is per-packet @c SCM_TXTIME deadlines
        // (the deferred @c TxTime mode), not a single rate cap.
        //
        // Skipped if @c VideoRtpTargetBitrate is non-zero — that
        // means the caller has explicitly chosen the rate, and
        // open-time @c applyRate already programmed it.
        if (_pacingMode.value() == RtpPacingMode::KernelFq.value() && _frameRate.isValid() && payload.isCompressed()) {
                size_t frameBytes = 0;
                for (size_t i = 0; i < packets.size(); i++) {
                        frameBytes += packets[i].size();
                }
                if (frameBytes > 0) {
                        const double   fps = _frameRate.toDouble();
                        const uint64_t bytesPerSec = static_cast<uint64_t>(static_cast<double>(frameBytes) * fps);
                        (void)_video.session->setPacingRate(bytesPerSec);
                }
        }

        // Decide whether to spread this frame's packets across
        // one frame interval via the userspace per-packet sleep
        // path (sendPacketsPaced) or burst them straight to the
        // socket (sendPackets).
        //
        // When pacing is enabled (any mode except None), all
        // video formats — compressed and uncompressed — use the
        // userspace path.  SO_MAX_PACING_RATE only works on
        // egress interfaces with an fq-family qdisc; the default
        // Linux setup for loopback, wireless, and most virtual
        // interfaces uses noqueue, which silently ignores the
        // rate cap.  The userspace path adds a per-packet sleep
        // that spreads the frame's packets across exactly one
        // frame interval regardless of qdisc, so pacing works
        // everywhere.  On a properly-configured ST 2110 egress
        // (fq qdisc + per-frame setPacingRate), the kernel still
        // gets the first crack at packet spacing and the
        // userspace sleeps are a backstop.
        //
        // RtpPacingMode::None bypasses all pacing and bursts
        // directly to the socket — useful for loopback tests
        // and maximum-throughput scenarios.
        Error      err;
        const bool wantSpread = _frameRate.isValid() && _pacingMode.value() != RtpPacingMode::None.value();
        if (wantSpread) {
                Duration interval = _frameRate.frameDuration();
                err = _video.session->sendPacketsPaced(packets, ts, interval, true);
        } else {
                err = _video.session->sendPackets(packets, ts, true);
        }
        if (err.isError()) return err;

        _video.packetsSent += static_cast<int64_t>(packets.size());
        for (size_t i = 0; i < packets.size(); i++) _video.bytesSent += static_cast<int64_t>(packets[i].size());

        // Record total wall-clock time spent inside this call —
        // dominated by the per-packet pacing sleeps when wantSpread
        // is set.  Useful for spotting frames whose pacing falls
        // behind the requested interval (e.g. an encoder spike or
        // a system pause that bleeds into the next frame's budget).
        const Duration sendDur = TimeStamp::now() - callStart;
        _video.txSendDuration.addSample(sendDur.microseconds());
        return Error::Ok;
}

Error RtpMediaIO::sendAudio(const PcmAudioPayload &payload) {
        if (!_audio.active) return Error::Ok;
        if (_audio.session == nullptr || _audio.payload == nullptr) return Error::Invalid;
        if (payload.sampleCount() == 0) return Error::Ok;
        if (payload.planeCount() == 0) return Error::Invalid;
        if (_audioState.packetBytes == 0 || _audioState.packetSamples == 0) {
                return Error::Invalid;
        }

        // Push the incoming samples into the FIFO.  AudioBuffer
        // auto-converts bit depth / endian / float↔int from the
        // payload's own descriptor into the stored L16 big-endian
        // wire format.  Sample rate and channel count must match
        // what we configured at open time.  Interleaved PCM lives
        // in plane(0); planar PCM isn't supported on the TX path
        // yet.
        auto planeView = payload.plane(0);
        if (planeView.size() == 0) return Error::Invalid;
        Error pushErr = _audioState.fifo.push(planeView.data(), payload.sampleCount(), payload.desc());
        if (pushErr.isError()) {
                promekiErr("RtpMediaIO: audio FIFO push failed: %s", pushErr.desc().cstr());
                return pushErr;
        }

        // Drain whole AES67-sized packets out of the FIFO.  Leftover
        // samples (when the incoming audio is not a whole multiple
        // of packetSamples) stay in the FIFO for the next frame.
        const size_t packetSamples = _audioState.packetSamples;
        const size_t packetBytes = _audioState.packetBytes;
        const size_t available = _audioState.fifo.available();
        if (available < packetSamples) return Error::Ok;

        const size_t count = available / packetSamples;
        const size_t totalSamples = count * packetSamples;
        const size_t totalBytes = count * packetBytes;

        // Drain the aligned block in one pop — one contiguous byte
        // buffer ready to hand to the payload handler.
        List<uint8_t> drained;
        drained.resize(totalBytes);
        auto [popped, popErr] = _audioState.fifo.pop(drained.data(), totalSamples);
        if (popErr.isError()) return popErr;
        if (popped != totalSamples) {
                promekiErr("RtpMediaIO: audio FIFO pop short (%zu / %zu)", popped, totalSamples);
                return Error::IOError;
        }

        // RtpPayloadL16::pack() was configured with maxPayloadSize =
        // packetBytes at open time, so feeding it `count * packetBytes`
        // of samples produces exactly `count` RTP packets all sharing
        // one backing buffer.
        RtpPacket::List packets = _audio.payload->pack(drained.data(), totalBytes);
        if (packets.isEmpty()) return Error::Ok;
        if (packets.size() != count) {
                promekiErr("RtpMediaIO: payload produced %zu packets, expected %zu", packets.size(), count);
                return Error::IOError;
        }

        // Batch-send with per-packet monotonic timestamps.  Each
        // packet's timestamp reflects the first sample of that
        // packet's contents; the marker bit is off for audio
        // (RFC 3551 reserves it for talkspurt boundaries which we
        // don't track).
        const uint32_t startTs = _audioState.nextTimestamp;
        Error          err = _audio.session->sendPackets(packets, startTs, static_cast<uint32_t>(packetSamples),
                                                         false /* no marker */);
        if (err.isError()) return err;

        _audioState.nextTimestamp = startTs + static_cast<uint32_t>(totalSamples);
        _audio.packetsSent += static_cast<int64_t>(packets.size());
        for (size_t i = 0; i < packets.size(); i++) {
                _audio.bytesSent += static_cast<int64_t>(packets[i].size());
        }
        return Error::Ok;
}

Error RtpMediaIO::sendData(const Metadata &metadata, const FrameNumber &frameIndex) {
        if (!_data.active) return Error::Ok;
        if (_data.session == nullptr || _data.payload == nullptr) return Error::Invalid;

        // Only the JsonMetadata format is wired up today; the
        // ST 2110-40 branch is rejected at configure time.
        JsonObject obj = metadata.toJson();
        String     json = obj.toString(0); // compact
        if (json.isEmpty()) return Error::Ok;

        double   fps = _frameRate.isValid() ? _frameRate.toDouble() : 30.0;
        uint32_t ts = static_cast<uint32_t>(static_cast<double>(frameIndex.value()) *
                                            static_cast<double>(_data.clockRate) / fps);

        auto packets = _data.payload->pack(json.cstr(), json.size());
        if (packets.isEmpty()) return Error::Ok;

        Error err = _data.session->sendPackets(packets, ts, true);
        if (err.isError()) return err;

        _data.packetsSent += static_cast<int64_t>(packets.size());
        for (size_t i = 0; i < packets.size(); i++) _data.bytesSent += static_cast<int64_t>(packets[i].size());
        return Error::Ok;
}

Error RtpMediaIO::executeCmd(MediaIOCommandWrite &cmd) {
        if (cmd.frame.isNull()) return Error::InvalidArgument;
        const Frame &frame = *cmd.frame;

        // Capture the current frame index up-front so each worker
        // sees the same value.  _frameCount is owned by this strand
        // thread and is only mutated below after all dispatched
        // sends have completed.  Captured as FrameNumber (not the
        // raw int64_t from .value()) so a poisoned Unknown count
        // never silently surfaces as -1 in downstream RTP-timestamp
        // math.
        const FrameNumber frameIndex = toFrameNumber(_frameCount);

        // Dispatch each per-stream send to its own TX thread so the
        // three sends run in parallel.  Each worker independently
        // honours its own pacing (video paces at frame interval,
        // audio drains its AES67 FIFO at sample-aligned cadence,
        // data is best-effort).  The return-channel Queues are
        // stack-local — zero heap allocation, and pop() blocks
        // until the worker pushes the result.
        Queue<Error> videoResult, audioResult, dataResult;
        bool         videoDispatched = false;
        bool         audioDispatched = false;
        bool         dataDispatched = false;

        auto vids = frame.videoPayloads();
        if (_video.active && _video.txThread != nullptr && !vids.isEmpty() && vids[0].isValid()) {
                // Hand the payload directly to the TX worker — the
                // @ref VideoPayload::Ptr keeps the payload (and its
                // plane buffers) alive for the duration of the
                // packetisation inside @ref sendVideo.  Supports
                // both uncompressed raster and compressed access
                // units in the same lambda.
                VideoPayload::Ptr vp = vids[0];
                _video.txThread->_workQueue.push(
                        TxWorkItem{[this, vp, frameIndex]() { return sendVideo(*vp, frameIndex); }, &videoResult});
                videoDispatched = true;
        }

        auto auds = frame.audioPayloads();
        if (_audio.active && _audio.txThread != nullptr && !auds.isEmpty() && auds[0].isValid()) {
                auto uap = sharedPointerCast<PcmAudioPayload>(auds[0]);
                if (uap.isValid()) {
                        _audio.txThread->_workQueue.push(
                                TxWorkItem{[this, uap]() { return sendAudio(*uap); }, &audioResult});
                        audioDispatched = true;
                }
        }

        if (_data.active && _data.txThread != nullptr) {
                Metadata md = frame.metadata();
                _data.txThread->_workQueue.push(
                        TxWorkItem{[this, md, frameIndex]() { return sendData(md, frameIndex); }, &dataResult});
                dataDispatched = true;
        }

        // Join all three workers.  pop() blocks until each worker
        // pushes its result.  We aggregate by reporting the first
        // error and letting the others complete normally — bailing
        // out early would leave in-flight workers running against
        // state we're about to tear down on Close.
        Error firstErr = Error::Ok;
        auto  join = [&firstErr](Queue<Error> &q, bool dispatched) {
                if (!dispatched) return;
                auto r = q.pop();
                if (r.second().isOk() && r.first().isError() && firstErr.isOk()) {
                        firstErr = r.first();
                }
        };
        join(videoResult, videoDispatched);
        join(audioResult, audioDispatched);
        join(dataResult, dataDispatched);

        if (firstErr.isError()) {
                noteFrameDropped(portGroup(0));
                return firstErr;
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

Error RtpMediaIO::executeCmd(MediaIOCommandStats &cmd) {
        if (_readerMode) {
                cmd.stats.set(StatsFramesReceived, _readerFramesReceived);
                // FramesDropped is populated by the MediaIO base
                // class from noteFrameDropped() — see
                // MediaIO::populateStandardStats.
                cmd.stats.set(StatsPacketsReceived,
                              _video.packetsReceived + _audio.packetsReceived + _data.packetsReceived);
                cmd.stats.set(StatsBytesReceived, _video.bytesReceived + _audio.bytesReceived + _data.bytesReceived);
                // Diagnostic histograms (RX side).  Stored as
                // pretty-printed Strings so callers can dump them
                // straight to a log without re-parsing.
                cmd.stats.set(StatsRxVideoPacketIntervalUs, _video.rxPacketInterval.toString());
                cmd.stats.set(StatsRxVideoFrameIntervalUs, _video.rxFrameInterval.toString());
                cmd.stats.set(StatsRxVideoFrameAssembleUs, _video.rxFrameAssembleTime.toString());
        } else {
                cmd.stats.set(StatsFramesSent, _framesSent);
                // FramesDropped is populated by the MediaIO base
                // class from noteFrameDropped().
                cmd.stats.set(StatsPacketsSent, _video.packetsSent + _audio.packetsSent + _data.packetsSent);
                cmd.stats.set(StatsBytesSent, _video.bytesSent + _audio.bytesSent + _data.bytesSent);
                // Diagnostic histograms (TX side).
                cmd.stats.set(StatsTxVideoFrameIntervalUs, _video.txFrameInterval.toString());
                cmd.stats.set(StatsTxVideoSendDurationUs, _video.txSendDuration.toString());
        }
        return Error::Ok;
}

PROMEKI_NAMESPACE_END
