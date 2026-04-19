/**
 * @file      mediaiotask_videodecoder.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 */

#include <promeki/mediaiotask_videodecoder.h>
#include <promeki/mediaconfig.h>
#include <promeki/enums.h>
#include <promeki/frame.h>
#include <promeki/image.h>
#include <promeki/imagedesc.h>
#include <promeki/mediadesc.h>
#include <promeki/mediaiodescription.h>
#include <promeki/metadata.h>
#include <promeki/pixeldesc.h>
#include <promeki/mediapacket.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_REGISTER_MEDIAIO(MediaIOTask_VideoDecoder)

namespace {

// Bridge: VideoDecoder bridges compressed → uncompressed.  The
// planner inserts it whenever a downstream stage cannot accept the
// upstream's compressed PixelDesc.
bool videoDecoderBridge(const MediaDesc &from,
                        const MediaDesc &to,
                        MediaIO::Config *outConfig,
                        int *outCost) {
        if(from.imageList().isEmpty() || to.imageList().isEmpty()) return false;
        const PixelDesc &fromPd = from.imageList()[0].pixelDesc();
        const PixelDesc &toPd   = to.imageList()[0].pixelDesc();
        if(!fromPd.isValid() || !toPd.isValid()) return false;
        if(!fromPd.isCompressed())               return false;
        if(toPd.isCompressed())                  return false;

        const VideoCodec codec = VideoCodec::fromPixelDesc(fromPd);
        if(!codec.isValid())   return false;
        if(!codec.canDecode()) return false;

        if(outConfig != nullptr) {
                *outConfig = MediaIO::defaultConfig("VideoDecoder");
                outConfig->set(MediaConfig::VideoCodec, codec);
                outConfig->set(MediaConfig::OutputPixelDesc, toPd);
        }
        if(outCost != nullptr) {
                // Decoding to the bitstream's reference representation
                // is lossless — only the optional CSC inside the
                // decoder (when toPd differs from the codec's native
                // output) costs.  Treat it as a fixed precision-
                // preserving hop.
                *outCost = 20;
        }
        return true;
}

} // namespace

MediaIO::FormatDesc MediaIOTask_VideoDecoder::formatDesc() {
        MediaIO::FormatDesc d;
        d.name              = "VideoDecoder";
        d.description       = "Generic video decoder stage (picks a VideoDecoder via VideoCodec)";
        d.canBeSource       = false;
        d.canBeSink         = false;
        d.canBeTransform    = true;
        d.create            = []() -> MediaIOTask * { return new MediaIOTask_VideoDecoder(); };
        d.configSpecs       = []() -> MediaIO::Config::SpecMap {
                MediaIO::Config::SpecMap specs;
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
                s(MediaConfig::OutputPixelDesc);
                // VUI color-description overrides.  Default Auto /
                // Unknown means the decoder uses whatever the
                // bitstream signalled.  Explicit values let the
                // caller force a tag on a mistagged stream — the
                // decoder stamps them verbatim on the output
                // Image's Metadata instead of the bitstream
                // value.
                s(MediaConfig::VideoColorPrimaries);
                s(MediaConfig::VideoTransferCharacteristics);
                s(MediaConfig::VideoMatrixCoefficients);
                s(MediaConfig::VideoRange);
                sWithDefault(MediaConfig::Capacity, int32_t(8));
                return specs;
        };
        d.bridge            = videoDecoderBridge;
        return d;
}

MediaIOTask_VideoDecoder::~MediaIOTask_VideoDecoder() {
        delete _decoder;
}

void MediaIOTask_VideoDecoder::configChanged(const MediaConfig &delta) {
        _config.merge(delta);
        if(_decoder != nullptr) _decoder->configure(_config);
}

Error MediaIOTask_VideoDecoder::createDecoder(const VideoCodec &codec) {
        if(!codec.canDecode()) {
                promekiErr("MediaIOTask_VideoDecoder: codec '%s' has no "
                           "registered decoder factory",
                           codec.name().cstr());
                return Error::NotSupported;
        }
        VideoDecoder *dec = codec.createDecoder();
        if(dec == nullptr) {
                promekiErr("MediaIOTask_VideoDecoder: createDecoder('%s') returned null",
                           codec.name().cstr());
                return Error::NotSupported;
        }
        dec->configure(_config);

        if(!_outputPixelDescSet) {
                List<int> supported = dec->supportedOutputs();
                if(!supported.isEmpty()) {
                        _outputPixelDesc = PixelDesc(
                                static_cast<PixelDesc::ID>(supported[0]));
                        _outputPixelDescSet = _outputPixelDesc.isValid();
                }
        }

        _codec   = codec;
        _decoder = dec;
        return Error::Ok;
}

Error MediaIOTask_VideoDecoder::executeCmd(MediaIOCommandOpen &cmd) {
        if(cmd.mode != MediaIO::Transform) {
                promekiErr("MediaIOTask_VideoDecoder: only InputAndOutput mode is supported");
                return Error::NotSupported;
        }

        const MediaIO::Config &cfg = cmd.config;
        _config = cfg;

        _outputPixelDesc = cfg.getAs<PixelDesc>(MediaConfig::OutputPixelDesc, PixelDesc());
        _outputPixelDescSet = _outputPixelDesc.isValid();

        _capacity = cfg.getAs<int>(MediaConfig::Capacity, 8);
        if(_capacity < 1) _capacity = 1;

        _codec            = VideoCodec();
        _decoder          = nullptr;
        _frameCount       = 0;
        _readCount        = 0;
        _packetsDecoded   = 0;
        _imagesOut        = 0;
        _capacityWarned   = false;
        _closed           = false;
        _outputQueue.clear();
        _pendingSrcFrames.clear();

        VideoCodec codec = cfg.getAs<VideoCodec>(MediaConfig::VideoCodec);
        if(codec.isValid()) {
                Error err = createDecoder(codec);
                if(err.isError()) return err;
        } else {
                promekiInfo("MediaIOTask_VideoDecoder: no VideoCodec configured, "
                            "will auto-detect from first packet");
        }

        MediaDesc outDesc;
        outDesc.setFrameRate(cmd.pendingMediaDesc.frameRate());
        for(const auto &srcImg : cmd.pendingMediaDesc.imageList()) {
                ImageDesc id = srcImg;
                if(_outputPixelDescSet) id = ImageDesc(srcImg.size(), _outputPixelDesc);
                outDesc.imageList().pushToBack(id);
        }
        for(const auto &srcAudio : cmd.pendingMediaDesc.audioList()) {
                outDesc.audioList().pushToBack(srcAudio);
        }

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

Error MediaIOTask_VideoDecoder::executeCmd(MediaIOCommandClose &cmd) {
        (void)cmd;
        if(_decoder != nullptr) {
                _decoder->flush();
                drainDecoderInto();
                delete _decoder;
                _decoder = nullptr;
        }
        _pendingSrcFrames.clear();
        _config = MediaConfig();
        _codec = VideoCodec();
        _outputPixelDesc = PixelDesc();
        _outputPixelDescSet = false;
        _capacity = 0;
        _frameCount = 0;
        _readCount = 0;
        _packetsDecoded = 0;
        _imagesOut = 0;
        _capacityWarned = false;
        _closed = true;
        return Error::Ok;
}

Error MediaIOTask_VideoDecoder::executeCmd(MediaIOCommandWrite &cmd) {
        if(!cmd.frame.isValid()) {
                promekiErr("MediaIOTask_VideoDecoder: write with null frame");
                return Error::InvalidArgument;
        }
        stampWorkBegin();

        const Frame &frame = *cmd.frame;

        // Collect every compressed Image on the Frame whose attached
        // MediaPacket carries the bitstream we need to decode.  Under
        // the Image::packet model that's the single source of truth —
        // producers (encoder output, container demux, RTP reader,
        // ImageFile loader) all attach the encoded bytes to the
        // compressed Image they emit.
        List<MediaPacket::Ptr> packets;
        for(const Image::Ptr &imgPtr : frame.imageList()) {
                if(!imgPtr.isValid() || !imgPtr->isCompressed()) continue;
                const MediaPacket::Ptr &pkt = imgPtr->packet();
                if(pkt.isValid() && pkt->isValid()) packets.pushToBack(pkt);
        }

        if(_decoder == nullptr) {
                if(packets.isEmpty()) {
                        promekiErr("MediaIOTask_VideoDecoder: no compressed Image "
                                   "with an attached MediaPacket on the write frame");
                        stampWorkEnd();
                        return Error::InvalidArgument;
                }
                const MediaPacket &pkt = *packets[0];
                VideoCodec codec = VideoCodec::fromPixelDesc(pkt.pixelDesc());
                if(!codec.isValid()) {
                        promekiErr("MediaIOTask_VideoDecoder: cannot resolve "
                                   "VideoCodec from PixelDesc '%s'",
                                   pkt.pixelDesc().name().cstr());
                        stampWorkEnd();
                        return Error::NotSupported;
                }
                Error err = createDecoder(codec);
                if(err.isError()) {
                        stampWorkEnd();
                        return err;
                }
                promekiInfo("MediaIOTask_VideoDecoder: auto-detected codec '%s' "
                            "from packet PixelDesc '%s'",
                            codec.name().cstr(),
                            pkt.pixelDesc().name().cstr());
        }

        if(static_cast<int>(_outputQueue.size()) >= _capacity && !_capacityWarned) {
                promekiWarn("MediaIOTask_VideoDecoder: output queue exceeded capacity "
                            "(%d >= %d)",
                            static_cast<int>(_outputQueue.size()), _capacity);
                _capacityWarned = true;
        }

        for(const MediaPacket::Ptr &pktPtr : packets) {
                _pendingSrcFrames.pushToBack(cmd.frame);
                Error err = _decoder->submitPacket(*pktPtr);
                if(err.isError()) {
                        promekiErr("MediaIOTask_VideoDecoder: submitPacket failed: %s",
                                   _decoder->lastErrorMessage().cstr());
                        stampWorkEnd();
                        return err;
                }
                _packetsDecoded++;
                drainDecoderInto();
        }

        _frameCount++;
        cmd.currentFrame = _frameCount;
        cmd.frameCount   = _frameCount;
        stampWorkEnd();
        return Error::Ok;
}

void MediaIOTask_VideoDecoder::drainDecoderInto() {
        if(_decoder == nullptr) return;
        while(true) {
                Image img = _decoder->receiveFrame();
                if(!img.isValid()) break;

                // Pair this image with the oldest queued source Frame
                // — that's the packet Frame that produced it even when
                // DPB warmup delays a few frames.  Empty queue is a
                // best-effort fallback (shouldn't happen for I/P-only
                // streams but leaves the image intact for any other).
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
                out->imageList().pushToBack(Image::Ptr::create(std::move(img)));
                _outputQueue.pushToBack(std::move(outFrame));
                _imagesOut++;
        }
}

Error MediaIOTask_VideoDecoder::executeCmd(MediaIOCommandRead &cmd) {
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

Error MediaIOTask_VideoDecoder::executeCmd(MediaIOCommandStats &cmd) {
        cmd.stats.set(StatsPacketsDecoded, _packetsDecoded);
        cmd.stats.set(StatsImagesOut, _imagesOut);
        cmd.stats.set(MediaIOStats::QueueDepth,
                      static_cast<int64_t>(_outputQueue.size()));
        cmd.stats.set(MediaIOStats::QueueCapacity,
                      static_cast<int64_t>(_capacity));
        return Error::Ok;
}

int MediaIOTask_VideoDecoder::pendingOutput() const {
        return static_cast<int>(_outputQueue.size());
}

// ---- Phase 1 introspection / negotiation overrides ----

Error MediaIOTask_VideoDecoder::describe(MediaIODescription *out) const {
        if(out == nullptr) return Error::Invalid;

        // If the planner / caller has already pinned a codec via
        // config, advertise every compressed PixelDesc that codec
        // covers as an acceptable input shape.
        if(_codec.isValid()) {
                for(int pdId : _codec.compressedPixelDescs()) {
                        MediaDesc accepted;
                        accepted.imageList().pushToBack(
                                ImageDesc(Size2Du32(0, 0),
                                          PixelDesc(static_cast<PixelDesc::ID>(pdId))));
                        out->acceptableFormats().pushToBack(accepted);
                }
        }
        if(_outputPixelDescSet) {
                MediaDesc produced;
                produced.imageList().pushToBack(
                        ImageDesc(Size2Du32(0, 0), _outputPixelDesc));
                out->producibleFormats().pushToBack(produced);
                out->setPreferredFormat(produced);
        }
        return Error::Ok;
}

Error MediaIOTask_VideoDecoder::proposeInput(const MediaDesc &offered,
                                             MediaDesc *preferred) const {
        if(preferred == nullptr) return Error::Invalid;
        if(offered.imageList().isEmpty()) return Error::NotSupported;

        // VideoDecoder consumes compressed video only.
        const PixelDesc &pd = offered.imageList()[0].pixelDesc();
        if(!pd.isCompressed()) return Error::NotSupported;

        // If a codec has been pinned, the offered codec must match.
        if(_codec.isValid()) {
                const VideoCodec offeredCodec = VideoCodec::fromPixelDesc(pd);
                if(offeredCodec != _codec) return Error::NotSupported;
        } else {
                // Codec auto-detect mode: any compressed PixelDesc
                // whose codec the registry can decode is acceptable.
                const VideoCodec offeredCodec = VideoCodec::fromPixelDesc(pd);
                if(!offeredCodec.isValid() || !offeredCodec.canDecode()) {
                        return Error::NotSupported;
                }
        }
        *preferred = offered;
        return Error::Ok;
}

Error MediaIOTask_VideoDecoder::proposeOutput(const MediaDesc &requested,
                                              MediaDesc *achievable) const {
        if(achievable == nullptr) return Error::Invalid;
        const MediaIO *io = mediaIo();
        const MediaConfig &cfg = (io != nullptr) ? io->config() : MediaConfig();
        // Compute the uncompressed output by applying the
        // configured Output* overrides.  When OutputPixelDesc is
        // unset the decoder produces its native uncompressed shape
        // (which the requested input would already imply).
        *achievable = applyOutputOverrides(requested, cfg);
        return Error::Ok;
}

PROMEKI_NAMESPACE_END
