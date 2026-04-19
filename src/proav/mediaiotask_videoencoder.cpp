/**
 * @file      mediaiotask_videoencoder.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 */

#include <climits>
#include <cstdlib>
#include <cstring>
#include <promeki/mediaiotask_videoencoder.h>
#include <promeki/mediaconfig.h>
#include <promeki/enums.h>
#include <promeki/frame.h>
#include <promeki/framerate.h>
#include <promeki/image.h>
#include <promeki/imagedesc.h>
#include <promeki/mediadesc.h>
#include <promeki/mediaiodescription.h>
#include <promeki/mediapacket.h>
#include <promeki/metadata.h>
#include <promeki/pixeldesc.h>
#include <promeki/buffer.h>
#include <promeki/logger.h>
#include <promeki/mediatimestamp.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_DEBUG(MediaIOTask_VideoEncoder)

PROMEKI_REGISTER_MEDIAIO(MediaIOTask_VideoEncoder)

namespace {

// Bridge: VideoEncoder bridges uncompressed → compressed.  The
// planner inserts it whenever a downstream sink demands a compressed
// PixelDesc that an upstream uncompressed source can supply.
bool videoEncoderBridge(const MediaDesc &from,
                        const MediaDesc &to,
                        MediaIO::Config *outConfig,
                        int *outCost) {
        if(from.imageList().isEmpty() || to.imageList().isEmpty()) return false;
        const PixelDesc &fromPd = from.imageList()[0].pixelDesc();
        const PixelDesc &toPd   = to.imageList()[0].pixelDesc();
        if(!fromPd.isValid() || !toPd.isValid()) return false;
        if(fromPd.isCompressed())                return false;
        if(!toPd.isCompressed())                 return false;

        const VideoCodec codec = VideoCodec::fromPixelDesc(toPd);
        if(!codec.isValid())   return false;
        if(!codec.canEncode()) return false;

        if(outConfig != nullptr) {
                *outConfig = MediaIO::defaultConfig("VideoEncoder");
                outConfig->set(MediaConfig::VideoCodec, codec);
        }
        if(outCost != nullptr) {
                // Encoding is lossy by construction.  Sit in the
                // "heavily lossy" band so the planner avoids it
                // unless the sink really needs the compressed form.
                *outCost = 5000;
        }
        return true;
}

} // namespace

MediaIO::FormatDesc MediaIOTask_VideoEncoder::formatDesc() {
        MediaIO::FormatDesc d;
        d.name              = "VideoEncoder";
        d.description       = "Generic video encoder stage (picks a VideoEncoder via VideoCodec)";
        d.canBeSource       = false;
        d.canBeSink         = false;
        d.canBeTransform    = true;
        d.create            = []() -> MediaIOTask * { return new MediaIOTask_VideoEncoder(); };
        d.configSpecs       = []() -> MediaIO::Config::SpecMap {
                MediaIO::Config::SpecMap specs;
                // Inherit the library-wide spec for each key so
                // descriptions, types, ranges, and enum types
                // stay consistent across every backend that
                // references them.  Local defaults only override
                // when we want a backend-specific preferred
                // value different from the MediaConfig library
                // default.
                auto s = [&specs](MediaConfig::ID id) {
                        const VariantSpec *gs = MediaConfig::spec(id);
                        if(gs) specs.insert(id, *gs);
                };
                auto sWithDefault =
                        [&specs](MediaConfig::ID id, const Variant &def) {
                        const VariantSpec *gs = MediaConfig::spec(id);
                        specs.insert(id, gs
                                ? VariantSpec(*gs).setDefault(def)
                                : VariantSpec().setDefault(def));
                };
                s(MediaConfig::VideoCodec);
                s(MediaConfig::FrameRate);
                s(MediaConfig::BitrateKbps);
                s(MediaConfig::MaxBitrateKbps);
                s(MediaConfig::VideoRcMode);
                s(MediaConfig::GopLength);
                s(MediaConfig::IdrInterval);
                s(MediaConfig::BFrames);
                s(MediaConfig::LookaheadFrames);
                s(MediaConfig::VideoPreset);
                s(MediaConfig::VideoProfile);
                s(MediaConfig::VideoLevel);
                s(MediaConfig::VideoQp);
                s(MediaConfig::VideoSpatialAQ);
                s(MediaConfig::VideoSpatialAQStrength);
                s(MediaConfig::VideoTemporalAQ);
                s(MediaConfig::VideoMultiPass);
                s(MediaConfig::VideoRepeatHeaders);
                s(MediaConfig::VideoTimecodeSEI);
                s(MediaConfig::VideoColorPrimaries);
                s(MediaConfig::VideoTransferCharacteristics);
                s(MediaConfig::VideoMatrixCoefficients);
                s(MediaConfig::VideoRange);
                s(MediaConfig::VideoChromaSubsampling);
                s(MediaConfig::VideoScanMode);
                s(MediaConfig::HdrMasteringDisplay);
                s(MediaConfig::HdrContentLightLevel);
                sWithDefault(MediaConfig::Capacity, int32_t(8));
                return specs;
        };
        d.bridge            = videoEncoderBridge;
        return d;
}

MediaIOTask_VideoEncoder::~MediaIOTask_VideoEncoder() {
        delete _encoder;
}

Error MediaIOTask_VideoEncoder::executeCmd(MediaIOCommandOpen &cmd) {
        if(cmd.mode != MediaIO::Transform) {
                promekiErr("MediaIOTask_VideoEncoder: only InputAndOutput mode is supported");
                return Error::NotSupported;
        }

        const MediaIO::Config &cfg = cmd.config;
        _config = cfg;

        _codec = cfg.getAs<VideoCodec>(MediaConfig::VideoCodec);
        if(!_codec.isValid()) {
                promekiErr("MediaIOTask_VideoEncoder: VideoCodec is required "
                           "(e.g. \"H264\", \"HEVC\", \"JPEG\")");
                return Error::InvalidArgument;
        }
        if(!_codec.canEncode()) {
                promekiErr("MediaIOTask_VideoEncoder: codec '%s' has no "
                           "registered encoder factory",
                           _codec.name().cstr());
                return Error::NotSupported;
        }

        // Typed factory lookup; the registered VideoEncoder subclass
        // owns any codec-specific state (NVENC session, CUDA context, …).
        VideoEncoder *enc = _codec.createEncoder();
        if(enc == nullptr) {
                promekiErr("MediaIOTask_VideoEncoder: createEncoder('%s') returned null",
                           _codec.name().cstr());
                return Error::NotSupported;
        }

        // The entire caller-visible MediaConfig flows unfiltered into
        // the encoder so keys it understands are honoured and ones it
        // doesn't are quietly ignored (the VideoEncoder contract).
        // Stamp the effective FrameRate from the upstream MediaDesc so
        // the encoder's rate-control math (bits-per-frame, HRD buffer,
        // H.264 VUI timing) uses the real stream rate rather than
        // whatever default the backend picked — callers rarely set
        // MediaConfig::FrameRate explicitly on a transform stage.
        MediaIO::Config encCfg = cfg;
        if(cmd.pendingMediaDesc.frameRate().isValid()) {
                encCfg.set(MediaConfig::FrameRate, cmd.pendingMediaDesc.frameRate());
        }
        enc->configure(encCfg);

        _capacity = cfg.getAs<int>(MediaConfig::Capacity, 8);
        if(_capacity < 1) _capacity = 1;

        // Build the downstream-visible MediaDesc: each source image is
        // replaced by one at the encoder's compressed PixelDesc so the
        // next stage picks up the right format before the first frame
        // arrives.
        MediaDesc outDesc;
        outDesc.setFrameRate(cmd.pendingMediaDesc.frameRate());

        const PixelDesc encOutPd = enc->outputPixelDesc();
        for(const auto &srcImg : cmd.pendingMediaDesc.imageList()) {
                outDesc.imageList().pushToBack(ImageDesc(srcImg.size(), encOutPd));
        }
        for(const auto &srcAudio : cmd.pendingMediaDesc.audioList()) {
                outDesc.audioList().pushToBack(srcAudio);
        }

        _encoder          = enc;
        _frameCount       = 0;
        _readCount        = 0;
        _framesEncoded    = 0;
        _packetsOut       = 0;
        _capacityWarned   = false;
        _multiImageWarned = false;
        _closed           = false;
        _outputQueue.clear();
        _pendingSrcFrames.clear();

        cmd.mediaDesc     = outDesc;
        if(!outDesc.audioList().isEmpty()) cmd.audioDesc = outDesc.audioList()[0];
        cmd.metadata      = cmd.pendingMetadata;
        cmd.frameRate     = outDesc.frameRate();
        cmd.canSeek       = false;
        cmd.frameCount    = MediaIO::FrameCountInfinite;
        cmd.defaultStep   = 1;
        cmd.defaultPrefetchDepth = 1;
        cmd.defaultWriteDepth    = _capacity;
        return Error::Ok;
}

Error MediaIOTask_VideoEncoder::executeCmd(MediaIOCommandClose &cmd) {
        (void)cmd;
        if(_encoder != nullptr) {
                // Best-effort flush so anything the encoder has buffered
                // makes it out before we tear the session down.  Because
                // pipelines close us only after all reads have been
                // consumed, any packets emitted here are stored in
                // _outputQueue for a subsequent read if the caller cares
                // to drain again — most consumers won't, but the
                // contract stays honest either way.
                _encoder->flush();
                drainEncoderInto();
                delete _encoder;
                _encoder = nullptr;
        }
        _pendingSrcFrames.clear();
        _config = MediaConfig();
        _codec = VideoCodec();
        _capacity = 0;
        _frameCount = 0;
        _readCount = 0;
        _framesEncoded = 0;
        _packetsOut = 0;
        _capacityWarned = false;
        _multiImageWarned = false;
        _closed = true;
        return Error::Ok;
}

void MediaIOTask_VideoEncoder::configChanged(const MediaConfig &delta) {
        _config.merge(delta);
        if(_encoder != nullptr) _encoder->configure(_config);
}

Error MediaIOTask_VideoEncoder::executeCmd(MediaIOCommandWrite &cmd) {
        if(!cmd.frame.isValid()) {
                promekiErr("MediaIOTask_VideoEncoder: write with null frame");
                return Error::InvalidArgument;
        }
        if(_encoder == nullptr) {
                return Error::NotSupported;
        }
        stampWorkBegin();

        if(static_cast<int>(_outputQueue.size()) >= _capacity && !_capacityWarned) {
                promekiWarn("MediaIOTask_VideoEncoder: output queue exceeded capacity "
                            "(%d >= %d) — downstream is not draining packets fast enough",
                            static_cast<int>(_outputQueue.size()), _capacity);
                _capacityWarned = true;
        }

        const Frame &frame = *cmd.frame;

        // Read the first image's MediaTimeStamp from metadata so the
        // encoder can thread PTS back out on the matching packet.  If
        // the upstream didn't set one we pass an invalid MediaTimeStamp
        // and let the encoder invent something (most codecs will use
        // internal frame indexing).
        MediaTimeStamp pts;
        // Per-essence MediaTimeStamp lives on the Image (an essence
        // object), not on the Frame; the Image::metadata() lookup
        // succeeds transparently when the producer stamped the frame.
        if(!frame.imageList().isEmpty()) {
                const Image::Ptr &imgPtr = frame.imageList()[0];
                if(imgPtr) {
                        const Metadata &md = imgPtr->metadata();
                        pts = md.getAs<MediaTimeStamp>(Metadata::MediaTimeStamp);
                }
        }

        if(frame.imageList().isEmpty()) {
                // No image to encode — let the frame pass through so
                // audio / metadata-only inputs aren't lost.
                Frame::Ptr outFrame = Frame::Ptr::create();
                outFrame.modify()->metadata() = frame.metadata();
                for(const auto &a : frame.audioList()) {
                        outFrame.modify()->audioList().pushToBack(a);
                }
                _outputQueue.pushToBack(std::move(outFrame));
                stampWorkEnd();
                return Error::Ok;
        }

        if(frame.imageList().size() > 1 && !_multiImageWarned) {
                promekiWarn("MediaIOTask_VideoEncoder: Frame carries %zu images; "
                            "only image[0] will be encoded in this cut",
                            (size_t)frame.imageList().size());
                _multiImageWarned = true;
        }

        const Image::Ptr &srcImgPtr = frame.imageList()[0];
        if(!srcImgPtr || !srcImgPtr->isValid()) {
                stampWorkEnd();
                return Error::InvalidArgument;
        }
        if(srcImgPtr->metadata().getAs<bool>(Metadata::ForceKeyframe)) {
                _encoder->requestKeyframe();
        }
        Error err = _encoder->submitFrame(*srcImgPtr, pts);
        if(err.isError()) {
                promekiErr("MediaIOTask_VideoEncoder: submitFrame failed: %s",
                           _encoder->lastErrorMessage().cstr());
                stampWorkEnd();
                return err;
        }
        // Record the source frame so the matching packet — which may
        // not emerge until a later submit if the encoder returned
        // NEED_MORE_INPUT on this one — can be paired back up with its
        // original metadata and audio in drainEncoderInto.
        _pendingSrcFrames.pushToBack(cmd.frame);
        _frameCount++;
        _framesEncoded++;
        drainEncoderInto();

        cmd.currentFrame = _frameCount;
        cmd.frameCount   = _frameCount;
        stampWorkEnd();
        return Error::Ok;
}

void MediaIOTask_VideoEncoder::drainEncoderInto() {
        if(_encoder == nullptr) return;
        while(true) {
                MediaPacket::Ptr pkt = _encoder->receivePacket();
                if(!pkt) break;
                if(pkt->isEndOfStream()) {
                        // EOS is an encoder-internal signal that the
                        // session is drained; no need to propagate it
                        // as its own Frame (the pipeline uses the
                        // MediaIO close/EOF path for that).
                        continue;
                }

                // Pair the packet with the oldest queued source Frame
                // — that's the one that produced this packet even if
                // an intervening submit called drainEncoderInto too.
                // The queue can legitimately be empty on a late flush
                // if the caller already drained everything previously;
                // in that case the output still carries the packet
                // but with no audio / frame metadata.
                Frame::Ptr origin;
                if(!_pendingSrcFrames.isEmpty()) {
                        origin = _pendingSrcFrames.front();
                        _pendingSrcFrames.remove(0);
                }

                Frame::Ptr outFrame = Frame::Ptr::create();
                Frame     *out = outFrame.modify();
                if(origin.isValid()) {
                        out->metadata() = origin->metadata();
                        for(const auto &a : origin->audioList()) {
                                out->audioList().pushToBack(a);
                        }
                }

                // Wrap the encoder's MediaPacket in a compressed Image
                // so the packet travels with its owning essence.  The
                // width/height come from the origin's first image (the
                // compressed frame represents that picture).  The
                // packet's BufferView buffers are adopted into plane
                // 0 zero-copy — when the view spans the whole backing
                // buffer that's literal zero-copy; otherwise fall back
                // to a single copy into a plain Buffer::Ptr plane.
                Size2Du32 imgSize;
                if(origin.isValid() && !origin->imageList().isEmpty()) {
                        imgSize = origin->imageList()[0]->size();
                }
                const BufferView &view = pkt->view();
                Buffer::Ptr plane;
                if(view.buffer().isValid()
                   && view.offset() == 0
                   && view.size() == view.buffer()->size()) {
                        plane = view.buffer();
                } else if(view.isValid() && view.size() > 0) {
                        plane = Buffer::Ptr::create(view.size());
                        std::memcpy(plane.modify()->data(), view.data(), view.size());
                        plane.modify()->setSize(view.size());
                }
                Image compressed = plane.isValid()
                        ? Image::fromBuffer(plane, imgSize.width(), imgSize.height(),
                                            pkt->pixelDesc())
                        : Image();
                if(compressed.isValid()) {
                        compressed.setPacket(pkt);
                        out->imageList().pushToBack(Image::Ptr::create(std::move(compressed)));
                }
                _outputQueue.pushToBack(std::move(outFrame));
                _packetsOut++;
        }
}

Error MediaIOTask_VideoEncoder::executeCmd(MediaIOCommandRead &cmd) {
        if(_outputQueue.isEmpty()) {
                return _closed ? Error::EndOfFile : Error::TryAgain;
        }
        Frame::Ptr frame = std::move(_outputQueue.front());
        _outputQueue.remove(0);
        _readCount++;
        cmd.frame = std::move(frame);
        cmd.currentFrame = _readCount;
        return Error::Ok;
}

Error MediaIOTask_VideoEncoder::executeCmd(MediaIOCommandStats &cmd) {
        cmd.stats.set(StatsFramesEncoded, _framesEncoded);
        cmd.stats.set(StatsPacketsOut, _packetsOut);
        cmd.stats.set(MediaIOStats::QueueDepth,
                      static_cast<int64_t>(_outputQueue.size()));
        cmd.stats.set(MediaIOStats::QueueCapacity,
                      static_cast<int64_t>(_capacity));
        return Error::Ok;
}

int MediaIOTask_VideoEncoder::pendingOutput() const {
        return static_cast<int>(_outputQueue.size());
}

// ---- Phase 1 introspection / negotiation overrides ----

Error MediaIOTask_VideoEncoder::describe(MediaIODescription *out) const {
        if(out == nullptr) return Error::Invalid;

        // When a codec has been pinned via config, advertise it as
        // the producible compressed shape.  The acceptable side
        // stays empty as a "any uncompressed video" signal.
        if(_codec.isValid()) {
                for(int pdId : _codec.compressedPixelDescs()) {
                        MediaDesc produced;
                        produced.imageList().pushToBack(
                                ImageDesc(Size2Du32(0, 0),
                                          PixelDesc(static_cast<PixelDesc::ID>(pdId))));
                        out->producibleFormats().pushToBack(produced);
                }
                if(!out->producibleFormats().isEmpty()) {
                        out->setPreferredFormat(out->producibleFormats()[0]);
                }
        }
        return Error::Ok;
}

Error MediaIOTask_VideoEncoder::proposeInput(const MediaDesc &offered,
                                             MediaDesc *preferred) const {
        if(preferred == nullptr) return Error::Invalid;
        if(offered.imageList().isEmpty()) return Error::NotSupported;

        // VideoEncoder consumes uncompressed video only.
        const PixelDesc &pd = offered.imageList()[0].pixelDesc();
        if(!pd.isValid())     return Error::NotSupported;
        if(pd.isCompressed()) return Error::NotSupported;

        // Resolve the codec — either pinned by an earlier open(), or
        // read straight off the stage config during planning (the
        // planner constructs the stage via MediaIO::create but does
        // not open it, so _codec is still invalid at this point).
        VideoCodec codec = _codec;
        if(!codec.isValid() && mediaIo() != nullptr) {
                codec = mediaIo()->config().getAs<VideoCodec>(MediaConfig::VideoCodec);
        }

        // Without a codec we can't know which inputs the concrete
        // encoder will accept.  Fall back to passthrough; the planner
        // will fail later at open time rather than insert a bogus
        // bridge now.
        if(!codec.isValid() || !codec.canEncode()) {
                *preferred = offered;
                return Error::Ok;
        }

        // Query the backend's supported input list.  If a session is
        // already live (stage opened) we use it; otherwise we pull the
        // list from the codec registry so the planner never has to
        // instantiate an encoder (and touch its GPU / libjpeg /
        // SVT-JPEG-XS setup) just to introspect supported inputs.
        List<int> supported;
        if(_encoder != nullptr) {
                supported = _encoder->supportedInputs();
        } else {
                supported = codec.encoderSupportedInputs();
        }

        // Empty list means "accepts any uncompressed input" per the
        // VideoEncoder::supportedInputs contract.
        if(supported.isEmpty()) {
                *preferred = offered;
                return Error::Ok;
        }

        // Which chroma does the caller want the CSC to land on?  The
        // config default is 4:2:0 so an RGB or 4:4:4 source routed into
        // an encoder picks the universally-decodable format unless the
        // user opts in to higher-quality chroma with VideoChromaSubsampling.
        ChromaSubsampling wantChroma = ChromaSubsampling::YUV420;
        if(mediaIo() != nullptr) {
                const Enum raw = mediaIo()->config().getAs<Enum>(
                        MediaConfig::VideoChromaSubsampling);
                if(raw.type() == ChromaSubsampling::Type) {
                        wantChroma = ChromaSubsampling(raw.value());
                }
        }

        auto chromaToSampling = [](ChromaSubsampling c) {
                if(c == ChromaSubsampling::YUV444) return PixelFormat::Sampling444;
                if(c == ChromaSubsampling::YUV422) return PixelFormat::Sampling422;
                return PixelFormat::Sampling420;
        };
        const PixelFormat::Sampling wantSampling = chromaToSampling(wantChroma);

        const int offeredBits = pd.pixelFormat().compCount() > 0
                ? static_cast<int>(pd.pixelFormat().compDesc(0).bits) : 0;

        // Offered format already satisfies the requested chroma and is
        // on the supported list — no negotiation needed.
        const int offeredId = static_cast<int>(pd.id());
        if(pd.pixelFormat().sampling() == wantSampling) {
                for(size_t i = 0; i < supported.size(); ++i) {
                        if(supported[i] == offeredId) {
                                *preferred = offered;
                                return Error::Ok;
                        }
                }
        }

        // Pick the best-matching supported format to advertise so the
        // planner splices in a CSC.  Preference order:
        //   1. Requested chroma AND same bit-depth as source.
        //   2. Requested chroma (any bit-depth, prefers closer depth).
        //   3. Same bit-depth as source (any chroma).
        //   4. First entry (final fallback — every encoder lists at
        //      least one format).
        int bestId        = supported[0];
        int bestTier      = 4;
        int bestBitsDelta = INT_MAX;
        for(size_t i = 0; i < supported.size(); ++i) {
                const PixelDesc cand(static_cast<PixelDesc::ID>(supported[i]));
                if(!cand.isValid()) continue;
                const int candBits = cand.pixelFormat().compCount() > 0
                        ? static_cast<int>(cand.pixelFormat().compDesc(0).bits) : 0;
                const bool sameChroma = (cand.pixelFormat().sampling() == wantSampling);
                const bool sameBits   = (offeredBits > 0 && candBits == offeredBits);

                int tier = 4;
                if(sameChroma && sameBits) tier = 1;
                else if(sameChroma)        tier = 2;
                else if(sameBits)          tier = 3;

                const int bitsDelta = (offeredBits > 0 && candBits > 0)
                        ? std::abs(candBits - offeredBits)
                        : INT_MAX;

                if(tier < bestTier ||
                   (tier == bestTier && bitsDelta < bestBitsDelta)) {
                        bestTier      = tier;
                        bestId        = supported[i];
                        bestBitsDelta = bitsDelta;
                }
        }

        MediaDesc out = offered;
        ImageDesc::List &imgs = out.imageList();
        const PixelDesc pick(static_cast<PixelDesc::ID>(bestId));
        for(size_t i = 0; i < imgs.size(); ++i) {
                imgs[i].setPixelDesc(pick);
        }
        *preferred = out;
        return Error::Ok;
}

Error MediaIOTask_VideoEncoder::proposeOutput(const MediaDesc &requested,
                                              MediaDesc *achievable) const {
        if(achievable == nullptr) return Error::Invalid;

        // Resolve the codec from a live session when open, or from
        // the stage config during planning (the same fallback used
        // by proposeInput above).
        VideoCodec codec = _codec;
        if(!codec.isValid() && mediaIo() != nullptr) {
                codec = mediaIo()->config().getAs<VideoCodec>(MediaConfig::VideoCodec);
        }
        if(!codec.isValid() || codec.compressedPixelDescs().isEmpty()) {
                // Codec not yet known — defer; the planner has to
                // pin it before the encoder can answer.
                return Error::NotSupported;
        }

        // The encoder produces a compressed PixelDesc whose codec
        // matches the configured VideoCodec.  Start from the
        // requested input shape (raster + frame rate flow through),
        // then replace the pixel desc with the codec's compressed
        // form.
        MediaDesc out = requested;
        const PixelDesc compressed(
                static_cast<PixelDesc::ID>(codec.compressedPixelDescs()[0]));
        ImageDesc::List &imgs = out.imageList();
        for(size_t i = 0; i < imgs.size(); ++i) {
                imgs[i].setPixelDesc(compressed);
        }
        *achievable = out;
        return Error::Ok;
}

PROMEKI_NAMESPACE_END
