/**
 * @file      rtmpmediaio.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstring>

#include <promeki/aacbitstream.h>
#include <promeki/audiocodec.h>
#include <promeki/audiodesc.h>
#include <promeki/audioformat.h>
#include <promeki/audiopayload.h>
#include <promeki/buffer.h>
#include <promeki/bufferview.h>
#include <promeki/clock.h>
#include <promeki/compressedaudiopayload.h>
#include <promeki/compressedvideopayload.h>
#include <promeki/duration.h>
#include <promeki/error.h>
#include <promeki/flvtag.h>
#include <promeki/frame.h>
#include <promeki/h264bitstream.h>
#include <promeki/hevcbitstream.h>
#include <promeki/imagedesc.h>
#include <promeki/logger.h>
#include <promeki/mediaconfig.h>
#include <promeki/mediadesc.h>
#include <promeki/mediaiocommand.h>
#include <promeki/mediaioportgroup.h>
#include <promeki/mediaiorequest.h>
#include <promeki/mediapayload.h>
#include <promeki/objectbase.tpp>
#include <promeki/pixelformat.h>
#include <promeki/rtmpclient.h>
#include <promeki/rtmpmediaio.h>
#include <promeki/thread.h>
#include <promeki/timestamp.h>
#include <promeki/url.h>
#include <promeki/videocodec.h>
#include <promeki/videopayload.h>

#if defined(PROMEKI_ENABLE_TLS)
#include <promeki/sslcontext.h>
#endif

PROMEKI_NAMESPACE_BEGIN

PROMEKI_DEBUG(RtmpMediaIO)
PROMEKI_REGISTER_MEDIAIO_FACTORY(RtmpFactory)

namespace {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

constexpr unsigned int kReadPollMs = 100;
constexpr unsigned int kDepacketizerPopMs = 100;
constexpr unsigned int kClientPushTimeoutMs = 250;
constexpr unsigned int kPublishTimeoutMs = 5000;
constexpr unsigned int kPlayTimeoutMs = 5000;
constexpr unsigned int kOpenTimeoutMs = 10000;

/// @brief Returns true if @p view bytes start with an Annex-B start code.
bool looksLikeAnnexB(const BufferView &view) {
        if (view.count() == 0) return false;
        BufferView::Entry e = view[0];
        const uint8_t   *p = e.data();
        const size_t     n = e.size();
        if (n < 3) return false;
        if (p[0] == 0x00 && p[1] == 0x00 && p[2] == 0x01) return true;
        return n >= 4 && p[0] == 0x00 && p[1] == 0x00 && p[2] == 0x00 && p[3] == 0x01;
}

/// @brief Copies a payload's first-plane bytes into a fresh standalone Buffer.
Buffer copyFirstPlane(const BufferView &view) {
        Buffer out;
        if (view.count() == 0) return out;
        BufferView::Entry e = view[0];
        out = Buffer(e.size());
        out.setSize(e.size());
        if (e.size() > 0) {
                std::memcpy(out.data(), e.data(), e.size());
        }
        return out;
}

/// @brief Maps a video codec to its FLV codec ID.
FlvVideoTag::Codec flvCodecForVideo(const VideoCodec &c) {
        switch (c.id()) {
                case VideoCodec::H264: return FlvVideoTag::Avc;
                case VideoCodec::HEVC: return FlvVideoTag::ExHevc;
                default:               return FlvVideoTag::CodecUnknown;
        }
}

/// @brief Picks a FLV @c SoundRate enum closest to @p hz.
FlvAudioTag::SoundRate flvRateFor(float hz) {
        if (hz >= 32000.0f) return FlvAudioTag::Rate44000;
        if (hz >= 16500.0f) return FlvAudioTag::Rate22000;
        if (hz >= 8250.0f)  return FlvAudioTag::Rate11000;
        return FlvAudioTag::Rate5500;
}

/// @brief Quantize a @ref MediaTimeStamp to a 32-bit millisecond stamp suitable for RTMP.
uint32_t timestampMs(const MediaTimeStamp &mts) {
        if (!mts.isValid()) return 0;
        const int64_t ms = mts.timeStamp().milliseconds();
        if (ms < 0) return 0;
        return static_cast<uint32_t>(ms & 0xFFFFFFFFu);
}

/// @brief Builds an @c avcC blob from an Annex-B access unit's parameter sets.
Error buildAvccSequenceHeader(const BufferView &au, Buffer &outAvcc) {
        AvcDecoderConfig cfg;
        Error            err = AvcDecoderConfig::fromAnnexB(au, cfg);
        if (err.isError()) return err;
        if (cfg.sps.isEmpty() || cfg.pps.isEmpty()) return Error::InvalidArgument;
        return cfg.serialize(outAvcc);
}

/// @brief Builds an @c hvcC blob from an Annex-B access unit's parameter sets.
Error buildHvccSequenceHeader(const BufferView &au, Buffer &outHvcc) {
        HevcDecoderConfig cfg;
        Error             err = HevcDecoderConfig::fromAnnexB(au, cfg);
        if (err.isError()) return err;
        if (cfg.vps.isEmpty() || cfg.sps.isEmpty() || cfg.pps.isEmpty()) return Error::InvalidArgument;
        return cfg.serialize(outHvcc);
}

} // namespace

// ----------------------------------------------------------------------------
// PacketizerThread — sink-side worker
// ----------------------------------------------------------------------------

/**
 * @brief Per-instance sink-side worker.
 *
 * Drains the strand-side payload queue, converts each Frame's video /
 * audio payloads to FLV tags, emits sequence-header tags on the first
 * keyframe of each kind, and dispatches via RtmpClient::sendVideo /
 * sendAudio.  Runs on its own thread so the strand never blocks on
 * the chunk-layer write queue.
 */
class RtmpMediaIO::PacketizerThread : public Thread {
        public:
                /// @brief Strand-side handoff payload — captured CoW Frame plus optional config-update flag.
                struct Item {
                                Frame frame;
                };

                static constexpr size_t kDefaultQueueDepth = 64;

                explicit PacketizerThread(RtmpMediaIO *owner)
                    : _owner(owner) {
                        _queue.setMaxSize(static_cast<size_t>(_owner->_sendQueueDepth));
                        setName(String("RtmpPacketizer"));
                }

                ~PacketizerThread() override { (void)stop(); }

                /// @brief Push a frame; returns @c Error::TryAgain on full-queue timeout.
                Error pushFrame(Frame f) {
                        Item it{std::move(f)};
                        Error err = _queue.pushBlocking(std::move(it), kClientPushTimeoutMs);
                        if (err == Error::Timeout) return Error::TryAgain;
                        return err;
                }

                /// @brief Wake any blocked push/pop and join the thread.
                Error stop() {
                        if (!isRunning() && !_started.value()) return Error::Ok;
                        _stopping.setValue(true);
                        _queue.cancelWaiters();
                        return wait();
                }

                /// @brief Live depth of the strand→worker queue.
                int64_t queueDepth() const { return static_cast<int64_t>(_queue.size()); }

        protected:
                void run() override {
                        _started.setValue(true);
                        while (!_stopping.value()) {
                                // If the underlying client tore down
                                // (peer went away, socket failure, etc.)
                                // stop pulling frames rather than
                                // spewing per-frame send failures —
                                // the strand-side write path surfaces
                                // the error to the pipeline via
                                // RtmpMediaIO::_clientDisconnected.
                                if (_owner->_clientDisconnected.load(std::memory_order_acquire)) break;
                                Result<Item> r = _queue.pop(kDepacketizerPopMs);
                                if (r.second() == Error::Cancelled) break;
                                if (r.second() == Error::Timeout) continue;
                                if (r.second().isError()) {
                                        promekiWarn("RtmpMediaIO::PacketizerThread: pop failed: %s",
                                                    r.second().desc().cstr());
                                        break;
                                }
                                handleFrame(r.first().frame);
                        }
                }

        private:
                void handleFrame(const Frame &frame) {
                        // Video first.  Skip silently when no compressed
                        // payload is available; the planner is expected
                        // to insert a VideoEncoderMediaIO upstream when
                        // the source is uncompressed.
                        VideoPayload::PtrList vpl = frame.videoPayloads();
                        for (const VideoPayload::Ptr &vp : vpl) {
                                if (!vp.isValid()) continue;
                                const CompressedVideoPayload *cv = vp->as<CompressedVideoPayload>();
                                if (cv == nullptr) continue;
                                handleVideoPayload(*cv);
                        }
                        AudioPayload::PtrList apl = frame.audioPayloads();
                        for (const AudioPayload::Ptr &ap : apl) {
                                if (!ap.isValid()) continue;
                                const CompressedAudioPayload *ca = ap->as<CompressedAudioPayload>();
                                if (ca == nullptr) continue;
                                handleAudioPayload(*ca);
                        }
                }

                void handleVideoPayload(const CompressedVideoPayload &p) {
                        if (!p.isValid()) return;
                        const VideoCodec &codec = p.desc().pixelFormat().videoCodec();
                        const FlvVideoTag::Codec flvCodec = flvCodecForVideo(codec);
                        if (flvCodec == FlvVideoTag::CodecUnknown) {
                                promekiWarn("RtmpMediaIO: unsupported video codec %s",
                                            codec.name().cstr());
                                return;
                        }

                        const BufferView &data = p.data();
                        const bool        isKey = p.isKeyframe();

                        // First-IDR gate.  Most ingest servers reject a
                        // publish that begins on an inter-frame, so drop
                        // anything that arrives before the first IDR
                        // until we have seen one.
                        if (!_videoSequenceSent && !isKey) {
                                if (_owner->_dropUntilKeyframe) {
                                        _owner->_videoFramesDroppedPreIdr.fetchAndAdd(1);
                                }
                                return;
                        }

                        // Sequence header on the first IDR.  Annex-B in,
                        // avcC / hvcC out.  When the source is already
                        // AVCC we cannot recover the parameter sets
                        // from the access unit alone — skip the
                        // sequence header on the first frame and rely
                        // on out-of-band signaling once that path lands.
                        const bool annexB = looksLikeAnnexB(data);
                        if (!_videoSequenceSent && isKey && annexB) {
                                Buffer seq;
                                Error  err = (codec.id() == VideoCodec::HEVC)
                                                     ? buildHvccSequenceHeader(data, seq)
                                                     : buildAvccSequenceHeader(data, seq);
                                if (err.isOk() && seq.size() > 0) {
                                        FlvVideoTag tag;
                                        tag.frameType = FlvVideoTag::Keyframe;
                                        tag.codec = flvCodec;
                                        tag.packetType = FlvVideoTag::SequenceHeader;
                                        tag.compositionTimeOffsetMs = 0;
                                        tag.data = seq;
                                        Error sendErr = _owner->_client->sendVideo(tag, timestampMs(p.dts().isValid() ? p.dts() : p.pts()));
                                        if (sendErr.isError() && sendErr != Error::Cancelled) {
                                                promekiWarn("RtmpMediaIO: sequence-header send failed: %s",
                                                            sendErr.desc().cstr());
                                                return;
                                        }
                                        _videoSequenceSent = true;
                                }
                        }

                        // Convert Annex-B to AVCC on the fly so the wire
                        // payload is what RTMP expects regardless of
                        // what the encoder emitted.
                        Buffer avccPayload;
                        if (annexB) {
                                Error err = H264Bitstream::annexBToAvcc(data, /*lenSize=*/4, avccPayload);
                                if (err.isError()) {
                                        promekiWarn("RtmpMediaIO: Annex-B → AVCC failed: %s",
                                                    err.desc().cstr());
                                        return;
                                }
                        } else {
                                avccPayload = copyFirstPlane(data);
                        }

                        if (avccPayload.size() == 0) return;

                        FlvVideoTag tag;
                        tag.frameType = isKey ? FlvVideoTag::Keyframe : FlvVideoTag::InterFrame;
                        tag.codec = flvCodec;
                        tag.packetType = FlvVideoTag::Nalu;

                        // Composition-time offset (PTS - DTS) carries
                        // B-frame reorder offsets.  Zero on no-reorder
                        // streams and on streams where DTS is missing.
                        int32_t cto = 0;
                        if (p.pts().isValid() && p.dts().isValid()) {
                                const int64_t ptsMs = p.pts().timeStamp().milliseconds();
                                const int64_t dtsMs = p.dts().timeStamp().milliseconds();
                                cto = static_cast<int32_t>(ptsMs - dtsMs);
                        }
                        tag.compositionTimeOffsetMs = cto;
                        tag.data = avccPayload;

                        // The RTMP message timestamp is DTS (decode
                        // order); CTO carries the PTS - DTS offset.
                        const MediaTimeStamp &stamp = p.dts().isValid() ? p.dts() : p.pts();
                        Error sendErr = _owner->_client->sendVideo(tag, timestampMs(stamp));
                        if (sendErr.isError() && sendErr != Error::Cancelled) {
                                if (sendErr == Error::TryAgain) {
                                        _owner->_sendQueueOverflows.fetchAndAdd(1);
                                } else if (sendErr == Error::Invalid) {
                                        // Client torn down; the strand-side
                                        // _clientDisconnected slot will surface
                                        // the failure to the pipeline.
                                } else {
                                        promekiWarn("RtmpMediaIO: video send failed: %s",
                                                    sendErr.desc().cstr());
                                }
                        }
                }

                void handleAudioPayload(const CompressedAudioPayload &p) {
                        if (!p.isValid()) return;
                        const AudioCodec &codec = p.desc().format().audioCodec();
                        if (codec.id() != AudioCodec::AAC) {
                                promekiWarn("RtmpMediaIO: unsupported audio codec %s",
                                            codec.name().cstr());
                                return;
                        }

                        // Sequence header on the first audio access
                        // unit.  Derived from the descriptor — works
                        // for any encoder backend that populated a
                        // valid AudioDesc on the payload.
                        if (!_audioSequenceSent) {
                                AacDecoderConfig cfg = AacDecoderConfig::fromAudioDesc(p.desc());
                                Buffer           hdr;
                                Error            err = cfg.serialize(hdr);
                                if (err.isOk() && hdr.size() > 0) {
                                        FlvAudioTag tag;
                                        tag.format = FlvAudioTag::Aac;
                                        tag.rate = flvRateFor(p.desc().sampleRate());
                                        tag.size = FlvAudioTag::Bits16;
                                        tag.channelType = (p.desc().channels() > 1) ? FlvAudioTag::Stereo : FlvAudioTag::Mono;
                                        tag.aacPacketType = FlvAudioTag::AudioSpecificConfig;
                                        tag.data = hdr;
                                        Error sendErr = _owner->_client->sendAudio(tag, timestampMs(p.pts()));
                                        if (sendErr.isError() && sendErr != Error::Cancelled) {
                                                promekiWarn("RtmpMediaIO: audio sequence header send failed: %s",
                                                            sendErr.desc().cstr());
                                                return;
                                        }
                                        _audioSequenceSent = true;
                                }
                        }

                        FlvAudioTag tag;
                        tag.format = FlvAudioTag::Aac;
                        tag.rate = flvRateFor(p.desc().sampleRate());
                        tag.size = FlvAudioTag::Bits16;
                        tag.channelType = (p.desc().channels() > 1) ? FlvAudioTag::Stereo : FlvAudioTag::Mono;
                        tag.aacPacketType = FlvAudioTag::Raw;
                        tag.data = copyFirstPlane(p.data());

                        if (tag.data.size() == 0) return;

                        Error sendErr = _owner->_client->sendAudio(tag, timestampMs(p.pts()));
                        if (sendErr.isError() && sendErr != Error::Cancelled) {
                                if (sendErr == Error::TryAgain) {
                                        _owner->_sendQueueOverflows.fetchAndAdd(1);
                                } else if (sendErr == Error::Invalid) {
                                        // Client torn down; the strand-side
                                        // _clientDisconnected slot will surface
                                        // the failure to the pipeline.
                                } else {
                                        promekiWarn("RtmpMediaIO: audio send failed: %s",
                                                    sendErr.desc().cstr());
                                }
                        }
                }

                RtmpMediaIO       *_owner;
                Queue<Item>        _queue;
                Atomic<bool>       _stopping{false};
                Atomic<bool>       _started{false};
                bool               _videoSequenceSent = false;
                bool               _audioSequenceSent = false;
};

// ----------------------------------------------------------------------------
// DepacketizerThread — source-side worker
// ----------------------------------------------------------------------------

/**
 * @brief Per-instance source-side worker.
 *
 * Drains @ref RtmpClient::takeVideo / @ref takeAudio / @ref takeMetadata,
 * parses the FLV tags, builds @ref Frame payloads, and pushes the
 * resulting frames onto the strand-side reader queue.  Runs on its
 * own thread so the strand isn't blocked on socket-arrival cadence.
 */
class RtmpMediaIO::DepacketizerThread : public Thread {
        public:
                explicit DepacketizerThread(RtmpMediaIO *owner) : _owner(owner) {
                        setName(String("RtmpDepacketizer"));
                }

                ~DepacketizerThread() override { (void)stop(); }

                Error stop() {
                        if (!isRunning() && !_started.value()) return Error::Ok;
                        _stopping.setValue(true);
                        return wait();
                }

        protected:
                void run() override {
                        _started.setValue(true);
                        while (!_stopping.value()) {
                                // Client tore down (peer went away,
                                // socket failure, etc.) — exit the
                                // pump rather than busy-looping on
                                // Error::Invalid take results.
                                if (_owner->_clientDisconnected.load(std::memory_order_acquire)) break;

                                bool any = false;

                                // Video first — most likely to be hot.
                                Result<FlvVideoTag> vr = _owner->_client->takeVideo(kDepacketizerPopMs);
                                if (vr.second().isOk()) {
                                        handleVideo(vr.first());
                                        any = true;
                                } else if (vr.second() == Error::TryAgain || vr.second() == Error::Timeout) {
                                        // nothing pending — fall through
                                } else if (vr.second() == Error::Cancelled
                                           || vr.second() == Error::Invalid) {
                                        break;
                                }

                                // Audio.
                                Result<FlvAudioTag> ar = _owner->_client->takeAudio(1);
                                if (ar.second().isOk()) {
                                        handleAudio(ar.first());
                                        any = true;
                                } else if (ar.second() == Error::Cancelled
                                           || ar.second() == Error::Invalid) {
                                        break;
                                }

                                // Metadata.  Optional.
                                Result<Metadata> mr = _owner->_client->takeMetadata(0);
                                if (mr.second().isOk()) {
                                        handleMetadata(mr.first());
                                        any = true;
                                } else if (mr.second() == Error::Cancelled
                                           || mr.second() == Error::Invalid) {
                                        break;
                                }

                                if (!any) {
                                        // Brief yield so we don't spin
                                        // when the wire is idle.
                                        Thread::sleepMs(5);
                                }
                        }
                }

        private:
                void handleVideo(const FlvVideoTag &tag) {
                        if (tag.data.size() == 0) return;

                        // The first SequenceHeader tag of each kind
                        // carries the parameter sets we need for
                        // downstream decode.  Skip the in-band payload
                        // for v1 — emit only the bitstream NALs.
                        if (tag.packetType == FlvVideoTag::SequenceHeader) {
                                _videoSequenceSeen = true;
                                // Defer emission until we know how
                                // downstream wants to consume parameter
                                // sets.  Many decoders are happy with
                                // the in-band SPS/PPS that the encoder
                                // is expected to re-include on each
                                // keyframe.
                                return;
                        }
                        if (tag.packetType != FlvVideoTag::Nalu) return;

                        // Build an ImageDesc using the tag's codec.
                        // For Enhanced-RTMP we use the FourCC to map
                        // back to HEVC; legacy AVC is straightforward.
                        PixelFormat pf;
                        if (tag.codec == FlvVideoTag::ExHevc) {
                                pf = PixelFormat(PixelFormat::HEVC);
                        } else if (tag.codec == FlvVideoTag::Avc) {
                                pf = PixelFormat(PixelFormat::H264);
                        } else {
                                return;
                        }
                        ImageDesc desc{Size2Du32(), pf};

                        Buffer payload(tag.data.size());
                        payload.setSize(tag.data.size());
                        std::memcpy(payload.data(), tag.data.data(), tag.data.size());

                        CompressedVideoPayload::Ptr cv =
                                CompressedVideoPayload::Ptr::create(desc, payload);
                        if (tag.frameType == FlvVideoTag::Keyframe ||
                            tag.frameType == FlvVideoTag::GeneratedKeyframe) {
                                cv.modify()->addFlag(MediaPayload::Keyframe);
                        }

                        Frame f;
                        f.addPayload(cv);
                        (void)_owner->_readerQueue.pushDropOldest(std::move(f));
                        _owner->_readerFramesReceived.fetchAndAdd(1);
                }

                void handleAudio(const FlvAudioTag &tag) {
                        if (tag.format != FlvAudioTag::Aac) return;
                        if (tag.aacPacketType == FlvAudioTag::AudioSpecificConfig) {
                                _audioSequenceSeen = true;
                                return;
                        }
                        if (tag.data.size() == 0) return;

                        AudioDesc desc{AudioFormat(AudioFormat::AAC), 48000.0f, 2u};
                        Buffer    payload(tag.data.size());
                        payload.setSize(tag.data.size());
                        std::memcpy(payload.data(), tag.data.data(), tag.data.size());

                        CompressedAudioPayload::Ptr ca =
                                CompressedAudioPayload::Ptr::create(desc, payload);
                        Frame f;
                        f.addPayload(ca);
                        (void)_owner->_readerQueue.pushDropOldest(std::move(f));
                        _owner->_readerFramesReceived.fetchAndAdd(1);
                }

                void handleMetadata(const Metadata &) {
                        // v1: surface as no-op.  The metadata-emitting
                        // path lands when the planner needs to consume
                        // it (e.g. width / height from onMetaData).
                }

                RtmpMediaIO *_owner;
                Atomic<bool> _stopping{false};
                Atomic<bool> _started{false};
                bool         _videoSequenceSeen = false;
                bool         _audioSequenceSeen = false;
};

// ----------------------------------------------------------------------------
// Factory
// ----------------------------------------------------------------------------

RtmpFactory::Config::SpecMap RtmpFactory::configSpecs() const {
        Config::SpecMap specs;
        auto            s = [&specs](MediaConfig::ID id, const Variant &def) {
                const VariantSpec *gs = MediaConfig::spec(id);
                specs.insert(id, gs ? VariantSpec(*gs).setDefault(def) : VariantSpec().setDefault(def));
        };
        // RtmpUrl is required; no sensible default exists.  Caller
        // must populate it from a parsed @ref Url before open.  The
        // global spec is still recognised because the @c MediaConfig
        // registry sees the @c PROMEKI_DECLARE_ID; only the
        // factory's defaultConfig view omits it.
        s(MediaConfig::RtmpStreamKey, String());
        s(MediaConfig::RtmpAppName, String());
        s(MediaConfig::RtmpDropUntilKeyframe, true);
        s(MediaConfig::RtmpRepeatParameterSets, true);
        s(MediaConfig::RtmpEnhancedRtmp, true);
        s(MediaConfig::RtmpEmitAnnexB, false);
        s(MediaConfig::RtmpDataEnabled, true);
        s(MediaConfig::RtmpConnectTimeoutMs, int32_t(10000));
        s(MediaConfig::RtmpHandshakeTimeoutMs, int32_t(10000));
        s(MediaConfig::RtmpCommandTimeoutMs, int32_t(5000));
        s(MediaConfig::RtmpSendQueueDepth, int32_t(64));
        s(MediaConfig::RtmpReadQueueDepth, int32_t(64));
        s(MediaConfig::RtmpChunkSize, int32_t(60000));
        s(MediaConfig::RtmpWindowAckSize, int32_t(5'000'000));
        s(MediaConfig::RtmpPeerBandwidth, int32_t(5'000'000));
        s(MediaConfig::RtmpStartTcpNoDelay, true);
        s(MediaConfig::RtmpFcSubscribe, false);
#if defined(PROMEKI_ENABLE_TLS)
        s(MediaConfig::RtmpTlsVerify, true);
#endif
        return specs;
}

Error RtmpFactory::urlToConfig(const Url &url, Config *outConfig) const {
        if (outConfig == nullptr) return Error::InvalidArgument;
        const String scheme = url.scheme().toLower();
        if (scheme != "rtmp" && scheme != "rtmps") {
                return Error::InvalidArgument;
        }
        outConfig->set(MediaConfig::Type, String("Rtmp"));
        outConfig->set(MediaConfig::RtmpUrl, url);
        return Error::Ok;
}

MediaIO *RtmpFactory::create(const Config &config, ObjectBase *parent) const {
        auto *io = new RtmpMediaIO(parent);
        io->setConfig(config);
        return io;
}

// ----------------------------------------------------------------------------
// Ctor / dtor
// ----------------------------------------------------------------------------

Atomic<uint64_t> RtmpMediaIO::_nextObjectId{0};

RtmpMediaIO::RtmpMediaIO(ObjectBase *parent) : DedicatedThreadMediaIO(parent) {
        _objectId = _nextObjectId.fetchAndAdd(uint64_t(1)) + uint64_t(1);
}

RtmpMediaIO::~RtmpMediaIO() {
        if (isOpen()) (void)close().wait();
        resetAll();
}

// ----------------------------------------------------------------------------
// Internal helpers
// ----------------------------------------------------------------------------

void RtmpMediaIO::resetAll() {
        // Stop strand-side blocking pops first so reader-side close
        // doesn't deadlock.
        _readerQueue.cancelWaiters();

        // Stop the packetizer (sink) and depacketizer (source) workers
        // before the client; their inner loops touch _client via
        // sendVideo / takeVideo, so the client must outlive them.
        if (_packetizer.isValid()) {
                (void)_packetizer->stop();
                _packetizer.reset();
        }
        if (_depacketizer.isValid()) {
                (void)_depacketizer->stop();
                _depacketizer.reset();
        }

        if (_client.isValid()) {
                // Detach our disconnect slot before close() runs so
                // the synthetic Ok-disconnect it emits on shutdown
                // doesn't queue an onClientDisconnected callable that
                // races a subsequent open() and re-arms
                // _clientDisconnected on the new client.
                _client->disconnectedSignal.disconnectFromObject(this);
                (void)_client->close();
                _client.reset();
        }

        _readerQueue.clear();
        _readCancelled.store(false, std::memory_order_release);
        _clientDisconnected.store(false, std::memory_order_release);
        _disconnectErrorCode.store(0, std::memory_order_release);
        _imageDesc = ImageDesc();
        _audioDesc = AudioDesc();
        _url = Url();
        _streamKey = String();
        _readerMode = false;
        _frameCount = FrameCount(0);
        _framesSent = FrameCount(0);
        _readerFramesReceived.setValue(0);
        _videoFramesDroppedPreIdr.setValue(0);
        _sendQueueOverflows.setValue(0);
        _connectDurationMs = 0;
        _handshakeDurationMs = 0;
        _videoPacingMode = RtmpVideoPacing::Internal;
        _videoPaceGate.setClock(Clock::Ptr());
        _videoPaceGate.setPeriod(Duration());
        _frameRate = FrameRate();
        _paceSkipThresholdMs = 0;
        _paceReanchorThresholdMs = 0;
        _paceClockIsExternal = false;
}

void RtmpMediaIO::onClientDisconnected(Error reason) {
        // Coalesce repeat firings — RtmpClient::close on a still-open
        // client emits a second disconnect with Error::Ok after a
        // peer-driven disconnect already latched the reason.
        if (_clientDisconnected.load(std::memory_order_acquire)) return;
        _disconnectErrorCode.store(static_cast<int>(reason.code()),
                                   std::memory_order_release);
        _clientDisconnected.store(true, std::memory_order_release);
        // Surface as a generic MediaIO error so any listener observing
        // errorOccurredSignal sees the failure too.  The pipeline's
        // primary error cascade still comes through the Write/Read
        // command results below.
        if (reason.isError()) {
                promekiWarn("RtmpMediaIO: RTMP client disconnected: %s",
                            reason.desc().cstr());
                errorOccurredSignal.emit(reason);
        }
}

RtmpConnectOptions RtmpMediaIO::buildConnectOptions(const MediaIO::Config &cfg) const {
        RtmpConnectOptions opts;
        const String       app = cfg.getAs<String>(MediaConfig::RtmpAppName, String());
        if (!app.isEmpty()) opts.app = app;
        opts.flashVer = cfg.getAs<String>(MediaConfig::RtmpFlashVer, opts.flashVer);
        opts.tcUrl = cfg.getAs<String>(MediaConfig::RtmpTcUrl, opts.tcUrl);
        opts.pageUrl = cfg.getAs<String>(MediaConfig::RtmpPageUrl, opts.pageUrl);
        opts.swfUrl = cfg.getAs<String>(MediaConfig::RtmpSwfUrl, opts.swfUrl);
        return opts;
}

bool RtmpMediaIO::hasVideoEssence(const Frame &frame) {
        VideoPayload::PtrList vpl = frame.videoPayloads();
        for (const VideoPayload::Ptr &vp : vpl) {
                if (vp.isValid()) return true;
        }
        return false;
}

bool RtmpMediaIO::hasAudioEssence(const Frame &frame) {
        AudioPayload::PtrList apl = frame.audioPayloads();
        for (const AudioPayload::Ptr &ap : apl) {
                if (ap.isValid()) return true;
        }
        return false;
}

// ----------------------------------------------------------------------------
// executeCmd handlers
// ----------------------------------------------------------------------------

// ----------------------------------------------------------------------------
// proposeInput
// ----------------------------------------------------------------------------

Error RtmpMediaIO::proposeInput(const MediaDesc &offered, MediaDesc *preferred) const {
        if (preferred == nullptr) return Error::Invalid;

        // Reader-mode sources are queried via proposeOutput, not
        // proposeInput.  If a caller does ask, fall back to the base
        // class behaviour (transparent passthrough) so we don't fight
        // the planner with a fabricated shape.
        const Enum modeEnum = config().get(MediaConfig::OpenMode).asEnum(MediaIOOpenMode::Type);
        const bool isWrite = modeEnum.value() == MediaIOOpenMode::Write.value();
        if (!isWrite) {
                *preferred = offered;
                return Error::Ok;
        }

        MediaDesc out = offered;

        // Video: rewrite the first image's PixelFormat to the compressed
        // representative of the configured RtmpVideoCodec when the
        // offered shape isn't already compressed.  Picks the first
        // entry in the codec's compressedPixelFormats list — every
        // built-in codec has at least one — so VideoEncoderFactory::bridge
        // sees an uncompressed→compressed gap and splices in an encoder.
        if (!out.imageList().isEmpty()) {
                const PixelFormat &pd = out.imageList()[0].pixelFormat();
                if (pd.isValid() && !pd.isCompressed()) {
                        VideoCodec vc =
                                config().getAs<VideoCodec>(MediaConfig::RtmpVideoCodec,
                                                           VideoCodec(VideoCodec::H264));
                        if (vc.isValid()) {
                                List<PixelFormat> compressed = vc.compressedPixelFormats();
                                if (!compressed.isEmpty()) {
                                        ImageDesc::List &imgs = out.imageList();
                                        for (size_t i = 0; i < imgs.size(); ++i) {
                                                imgs[i].setPixelFormat(compressed[0]);
                                        }
                                }
                        }
                }
        }

        // Audio: rewrite the first audio descriptor's AudioFormat to the
        // compressed representative of the configured RtmpAudioCodec
        // when the offered shape isn't already compressed.  Walks the
        // registered AudioFormat IDs to find the format whose
        // @c audioCodec() reverses to the configured codec — every
        // well-known compressed codec has exactly one such format.
        if (!out.audioList().isEmpty()) {
                const AudioFormat &af = out.audioList()[0].format();
                if (af.isValid() && !af.isCompressed()) {
                        AudioCodec ac =
                                config().getAs<AudioCodec>(MediaConfig::RtmpAudioCodec,
                                                           AudioCodec(AudioCodec::AAC));
                        if (ac.isValid()) {
                                AudioFormat compressedFmt;
                                for (AudioFormat::ID fid : AudioFormat::registeredIDs()) {
                                        AudioFormat candidate(fid);
                                        if (!candidate.isCompressed()) continue;
                                        if (candidate.audioCodec() == ac) {
                                                compressedFmt = candidate;
                                                break;
                                        }
                                }
                                if (compressedFmt.isValid()) {
                                        AudioDesc::List &auds = out.audioList();
                                        for (size_t i = 0; i < auds.size(); ++i) {
                                                auds[i].setFormat(compressedFmt);
                                        }
                                }
                        }
                }
        }

        *preferred = out;
        return Error::Ok;
}

Error RtmpMediaIO::executeCmd(MediaIOCommandOpen &cmd) {
        const MediaIO::Config &cfg = cmd.config;

        Enum modeEnum = cfg.get(MediaConfig::OpenMode).asEnum(MediaIOOpenMode::Type);
        const bool isWrite = modeEnum.value() == MediaIOOpenMode::Write.value();
        _readerMode = !isWrite;
        _readCancelled.store(false, std::memory_order_release);

        // URL is required.
        Variant urlVar = cfg.get(MediaConfig::RtmpUrl);
        if (urlVar.type() != Variant::TypeUrl) {
                promekiErr("RtmpMediaIO: RtmpUrl config key is required");
                return Error::InvalidArgument;
        }
        _url = urlVar.get<Url>();
        if (!_url.isValid()) {
                promekiErr("RtmpMediaIO: RtmpUrl is invalid");
                return Error::InvalidArgument;
        }

        _streamKey = cfg.getAs<String>(MediaConfig::RtmpStreamKey, String());
        _dropUntilKeyframe = cfg.getAs<bool>(MediaConfig::RtmpDropUntilKeyframe, true);
        _repeatParameterSets = cfg.getAs<bool>(MediaConfig::RtmpRepeatParameterSets, true);
        _enhancedRtmp = cfg.getAs<bool>(MediaConfig::RtmpEnhancedRtmp, true);
        _emitAnnexB = cfg.getAs<bool>(MediaConfig::RtmpEmitAnnexB, false);
        _dataEnabled = cfg.getAs<bool>(MediaConfig::RtmpDataEnabled, true);
        _sendQueueDepth = cfg.getAs<int>(MediaConfig::RtmpSendQueueDepth, 64);
        _readQueueDepth = cfg.getAs<int>(MediaConfig::RtmpReadQueueDepth, 64);
        if (_sendQueueDepth < 2)   _sendQueueDepth = 2;
        if (_sendQueueDepth > 1024) _sendQueueDepth = 1024;
        if (_readQueueDepth < 2)   _readQueueDepth = 2;
        if (_readQueueDepth > 1024) _readQueueDepth = 1024;
        _readerQueue.setMaxSize(static_cast<size_t>(_readQueueDepth));

        // Cache the pre-open descriptors so the packetizer can build
        // sequence headers later.
        if (!cmd.pendingMediaDesc.imageList().isEmpty()) {
                _imageDesc = cmd.pendingMediaDesc.imageList()[0];
        }
        if (!cmd.pendingMediaDesc.audioList().isEmpty()) {
                _audioDesc = cmd.pendingMediaDesc.audioList()[0];
        }

        // Resolve the frame rate from the pending MediaDesc, falling
        // back to the MediaConfig::FrameRate key, then to 29.97 fps.
        // Used to seed the pacing gate's period and skip/reanchor
        // thresholds.
        _frameRate = cmd.pendingMediaDesc.frameRate();
        if (!_frameRate.isValid()) {
                _frameRate = cfg.getAs<FrameRate>(MediaConfig::FrameRate,
                                                  FrameRate(FrameRate::FPS_29_97));
        }

        // Pacing mode + thresholds (sink-mode only; readers ignore
        // the gate).  armVideoPaceGate() binds an Internal wall clock
        // when configured; External waits for executeCmd(SetClock).
        Enum pacingEnum = cfg.get(MediaConfig::RtmpVideoPacing)
                                  .asEnum(RtmpVideoPacing::Type);
        _videoPacingMode = RtmpVideoPacing(pacingEnum.value());
        _paceSkipThresholdMs = cfg.getAs<int>(MediaConfig::RtmpPaceSkipThresholdMs, 0);
        _paceReanchorThresholdMs =
                cfg.getAs<int>(MediaConfig::RtmpPaceReanchorThresholdMs, 0);
        if (!_readerMode) armVideoPaceGate();

        _client.reset(new RtmpClient(this));

        // Latch peer-disconnect / socket-failure events.  The slot
        // marshals to the strand event loop via the @c , this binding;
        // the latched flag stops the packetizer / depacketizer from
        // spewing per-frame send failures and causes subsequent
        // executeCmd(Write) / executeCmd(Read) calls to fail with the
        // captured reason so the pipeline's error cascade tears the
        // stage down.
        _client->disconnectedSignal.connect(
                [this](Error err) { onClientDisconnected(err); }, this);

#if defined(PROMEKI_ENABLE_TLS)
        Variant tlsVar = cfg.get(MediaConfig::RtmpTlsContext);
        if (tlsVar.type() == Variant::TypeSslContext) {
                SslContext::Ptr ctx = tlsVar.get<SslContext::Ptr>();
                if (ctx.isValid()) {
                        _client->setSslContext(ctx);
                }
        }
#endif

        RtmpConnectOptions opts = buildConnectOptions(cfg);
        const unsigned int connectTimeoutMs =
                static_cast<unsigned int>(cfg.getAs<int>(MediaConfig::RtmpConnectTimeoutMs, kOpenTimeoutMs));

        const TimeStamp openStart = TimeStamp::now();
        Error err = _client->open(_url, opts, connectTimeoutMs);
        if (err.isError()) {
                promekiErr("RtmpMediaIO: open(%s) failed: %s",
                           _url.toString().cstr(), err.desc().cstr());
                resetAll();
                return err;
        }
        _connectDurationMs = openStart.elapsedMilliseconds();
        _handshakeDurationMs = _connectDurationMs;  // Phase 5 v1: not yet split out.

        if (_readerMode) {
                const unsigned int playTimeoutMs =
                        static_cast<unsigned int>(cfg.getAs<int>(MediaConfig::RtmpCommandTimeoutMs, kPlayTimeoutMs));
                const bool useFcSubscribe = cfg.getAs<bool>(MediaConfig::RtmpFcSubscribe, false);
                err = _client->play(_streamKey, playTimeoutMs, useFcSubscribe);
                if (err.isError()) {
                        promekiErr("RtmpMediaIO: play failed: %s", err.desc().cstr());
                        resetAll();
                        return err;
                }

                // Add a single source port so writeFrame upstream sinks
                // know where to bind.  The MediaDesc is unknown until
                // the first SequenceHeader lands; we expose a generic
                // H.264 + 48 kHz AAC placeholder that downstream
                // planners can reconcile when the real descriptor
                // surfaces.
                MediaDesc rxDesc;
                rxDesc.imageList().pushToBack(ImageDesc(Size2Du32(), PixelFormat(PixelFormat::H264)));
                rxDesc.audioList().pushToBack(AudioDesc(AudioFormat(AudioFormat::AAC), 48000.0f, 2));
                MediaIOPortGroup *group = addPortGroup(String("rtmp-source"));
                if (group != nullptr) {
                        addSource(group, rxDesc, String("source"));
                }
                cmd.mediaDesc = rxDesc;

                _depacketizer.reset(new DepacketizerThread(this));
                _depacketizer->start();
        } else {
                const unsigned int publishTimeoutMs =
                        static_cast<unsigned int>(cfg.getAs<int>(MediaConfig::RtmpCommandTimeoutMs, kPublishTimeoutMs));
                err = _client->publish(_streamKey, String("live"), publishTimeoutMs);
                if (err.isError()) {
                        promekiErr("RtmpMediaIO: publish failed: %s", err.desc().cstr());
                        resetAll();
                        return err;
                }

                MediaDesc txDesc;
                if (_imageDesc.isValid()) {
                        txDesc.imageList().pushToBack(_imageDesc);
                } else {
                        txDesc.imageList().pushToBack(ImageDesc(Size2Du32(), PixelFormat(PixelFormat::H264)));
                }
                if (_audioDesc.isValid()) {
                        txDesc.audioList().pushToBack(_audioDesc);
                } else {
                        txDesc.audioList().pushToBack(AudioDesc(AudioFormat(AudioFormat::AAC), 48000.0f, 2));
                }
                MediaIOPortGroup *group = addPortGroup(String("rtmp-sink"));
                if (group != nullptr) {
                        addSink(group, txDesc, String("sink"));
                }
                cmd.mediaDesc = txDesc;

                _packetizer.reset(new PacketizerThread(this));
                _packetizer->start();
        }

        cmd.frameCount = FrameCount::infinity();
        return Error::Ok;
}

Error RtmpMediaIO::executeCmd(MediaIOCommandClose &cmd) {
        (void)cmd;
        resetAll();
        return Error::Ok;
}

Error RtmpMediaIO::executeCmd(MediaIOCommandRead &cmd) {
        if (!_readerMode) return Error::NotSupported;
        for (;;) {
                Result<Frame> r = _readerQueue.pop(kReadPollMs);
                if (r.second().isOk()) {
                        cmd.frame = r.first();
                        _frameCount += 1;
                        cmd.currentFrame = toFrameNumber(_frameCount);
                        return Error::Ok;
                }
                if (r.second() == Error::Cancelled) {
                        return Error::Cancelled;
                }
                if (r.second() != Error::Timeout) {
                        return r.second();
                }
                if (_readCancelled.load(std::memory_order_acquire)) {
                        return Error::Cancelled;
                }
                if (_clientDisconnected.load(std::memory_order_acquire)) {
                        // Drain any frames the depacketizer queued
                        // before the disconnect latched (handled above
                        // via the .isOk() path); once empty, surface
                        // the captured reason — or @c BrokenPipe as a
                        // sentinel when the disconnect was clean.
                        Error::Code code = static_cast<Error::Code>(
                                _disconnectErrorCode.load(std::memory_order_acquire));
                        return code == Error::Ok ? Error(Error::BrokenPipe) : Error(code);
                }
        }
}

Error RtmpMediaIO::executeCmd(MediaIOCommandWrite &cmd) {
        if (_readerMode) return Error::NotSupported;
        if (!cmd.frame.isValid()) return Error::InvalidArgument;
        if (!_packetizer.isValid() || !_client.isValid()) return Error::Invalid;

        // The peer went away mid-stream; surface the captured reason
        // so MediaIOPortConnection's writeError signal cascades the
        // failure to the pipeline (which then initiates a teardown).
        if (_clientDisconnected.load(std::memory_order_acquire)) {
                Error::Code code = static_cast<Error::Code>(
                        _disconnectErrorCode.load(std::memory_order_acquire));
                return code == Error::Ok ? Error(Error::BrokenPipe) : Error(code);
        }

        if (!hasVideoEssence(cmd.frame) && !hasAudioEssence(cmd.frame)) {
                // Nothing to do; treat as a silent no-op so an empty
                // sentinel frame upstream doesn't generate noise.
                _frameCount += 1;
                _framesSent += 1;
                cmd.currentFrame = toFrameNumber(_frameCount);
                cmd.frameCount = FrameCount::infinity();
                return Error::Ok;
        }

        // Strand-side video pacing.  The gate is a no-op when no clock
        // is bound (External-without-setClock or None mode), so it has
        // zero cost when pacing is disabled.  Skip verdicts drop the
        // frame to bound lag — same shape as RtpMediaIO::paceVideoFrame.
        if (hasVideoEssence(cmd.frame)) {
                if (!paceVideoFrame()) {
                        _frameCount += 1;
                        _framesSent += 1;
                        cmd.currentFrame = toFrameNumber(_frameCount);
                        cmd.frameCount = FrameCount::infinity();
                        return Error::Ok;
                }
        }

        Error err = _packetizer->pushFrame(cmd.frame);
        if (err.isError() && err != Error::TryAgain) {
                if (err == Error::Cancelled) {
                        return err;
                }
                promekiWarn("RtmpMediaIO: pushFrame failed: %s", err.desc().cstr());
                return err;
        }
        if (err == Error::TryAgain) {
                _sendQueueOverflows.fetchAndAdd(1);
                return err;
        }

        _frameCount += 1;
        _framesSent += 1;
        cmd.currentFrame = toFrameNumber(_frameCount);
        cmd.frameCount = FrameCount::infinity();
        return Error::Ok;
}

Error RtmpMediaIO::executeCmd(MediaIOCommandStats &cmd) {
        if (_client.isValid()) {
                cmd.stats.set(StatsBytesSent, _client->bytesSent());
                cmd.stats.set(StatsBytesReceived, _client->bytesReceived());
                cmd.stats.set(StatsVideoMessagesSent, _client->videoMessagesSent());
                cmd.stats.set(StatsAudioMessagesSent, _client->audioMessagesSent());
                cmd.stats.set(StatsVideoMessagesReceived, _client->videoMessagesReceived());
                cmd.stats.set(StatsAudioMessagesReceived, _client->audioMessagesReceived());
        }
        cmd.stats.set(StatsFramesSent, static_cast<int64_t>(_framesSent.value()));
        cmd.stats.set(StatsFramesReceived, _readerFramesReceived.value());
        cmd.stats.set(StatsSendQueueDepth,
                      _packetizer.isValid() ? _packetizer->queueDepth() : int64_t(0));
        cmd.stats.set(StatsReadQueueDepth, static_cast<int64_t>(_readerQueue.size()));
        cmd.stats.set(StatsSendQueueOverflows, _sendQueueOverflows.value());
        cmd.stats.set(StatsVideoFramesDroppedPreIdr, _videoFramesDroppedPreIdr.value());
        cmd.stats.set(StatsConnectDurationMs, _connectDurationMs);
        cmd.stats.set(StatsHandshakeDurationMs, _handshakeDurationMs);
        cmd.stats.set(StatsPacingTicksOnTime, _videoPaceGate.ticksOnTime());
        cmd.stats.set(StatsPacingTicksLate, _videoPaceGate.ticksLate());
        cmd.stats.set(StatsPacingTicksSkipped, _videoPaceGate.ticksSkipped());
        cmd.stats.set(StatsPacingReanchors, _videoPaceGate.reanchors());
        cmd.stats.set(StatsPacingClockKind, paceClockKind());
        return Error::Ok;
}

Error RtmpMediaIO::executeCmd(MediaIOCommandSetClock &cmd) {
        // RX timing is driven by network arrival; the user can't
        // meaningfully replace it.
        if (_readerMode) return Error::NotSupported;

        if (cmd.clock.isValid()) {
                // Honor the externally-supplied clock regardless of
                // configured mode — capture-card upstreams typically
                // arrive after Open via the planner forwarding the
                // port-group clock.
                _videoPaceGate.setClock(cmd.clock);
                _paceClockIsExternal = true;
                if (_frameRate.isValid()) {
                        _videoPaceGate.setPeriod(_frameRate.frameDuration());
                }
        } else {
                // Detach the external clock.  For Internal mode, re-arm
                // a fresh wall clock so pacing continues uninterrupted;
                // for External / None modes the gate stays unbound.
                _paceClockIsExternal = false;
                if (_videoPacingMode.value() == RtmpVideoPacing::Internal.value()) {
                        armVideoPaceGate();
                } else {
                        _videoPaceGate.setClock(Clock::Ptr());
                }
        }
        return Error::Ok;
}

void RtmpMediaIO::armVideoPaceGate() {
        _paceClockIsExternal = false;
        if (_videoPacingMode.value() == RtmpVideoPacing::Internal.value()) {
                _videoPaceGate.setClock(Clock::Ptr::takeOwnership(new WallClock()));
        } else {
                _videoPaceGate.setClock(Clock::Ptr());
        }
        if (_frameRate.isValid()) {
                _videoPaceGate.setPeriod(_frameRate.frameDuration());
                if (_paceSkipThresholdMs > 0) {
                        _videoPaceGate.setSkipThreshold(
                                Duration::fromMilliseconds(_paceSkipThresholdMs));
                }
                if (_paceReanchorThresholdMs > 0) {
                        _videoPaceGate.setReanchorThreshold(
                                Duration::fromMilliseconds(_paceReanchorThresholdMs));
                }
        }
}

bool RtmpMediaIO::paceVideoFrame() {
        if (!_videoPaceGate.hasClock()) return true;
        if (!_frameRate.isValid()) return true;

        // Refresh the period each call so a mid-stream frame-rate
        // change takes effect immediately (rare, but legal — matches
        // RtpMediaIO behavior).
        _videoPaceGate.setPeriod(_frameRate.frameDuration());

        PacingResult pr = _videoPaceGate.wait();
        if (pr.error.isError()) {
                promekiErr("RtmpMediaIO: video pacing clock failure: %s",
                           pr.error.name().cstr());
                return true;
        }
        switch (pr.verdict) {
                case PacingVerdict::Skip:
                        return false;
                case PacingVerdict::Reanchor:
                        promekiWarn("RtmpMediaIO: video pacing re-anchored after %s lag",
                                    pr.slack.toString().cstr());
                        return true;
                case PacingVerdict::OnTime:
                case PacingVerdict::Late:
                        return true;
        }
        return true;
}

String RtmpMediaIO::paceClockKind() const {
        if (!_videoPaceGate.hasClock()) return String("none");
        return _paceClockIsExternal ? String("external") : String("internal");
}

void RtmpMediaIO::cancelBlockingWork() {
        _readCancelled.store(true, std::memory_order_release);
        _readerQueue.cancelWaiters();
}

PROMEKI_NAMESPACE_END
