/**
 * @file      videodecodermediaio.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 */

#include <promeki/videodecodermediaio.h>
#include <promeki/mediaioportgroup.h>
#include <promeki/mediaconfig.h>
#include <promeki/enums.h>
#include <promeki/frame.h>
#include <promeki/imagedesc.h>
#include <promeki/mediadesc.h>
#include <promeki/mediaiodescription.h>
#include <promeki/metadata.h>
#include <promeki/pixelformat.h>
#include <promeki/compressedvideopayload.h>
#include <promeki/audiopayload.h>
#include <promeki/logger.h>
#include <promeki/mediaiorequest.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_REGISTER_MEDIAIO_FACTORY(VideoDecoderFactory)

namespace {

        // Bridge: VideoDecoder bridges compressed → uncompressed.  The
        // planner inserts it whenever a downstream stage cannot accept the
        // upstream's compressed PixelFormat.
        bool videoDecoderBridge(const MediaDesc &from, const MediaDesc &to, MediaIO::Config *outConfig, int *outCost) {
                if (from.imageList().isEmpty() || to.imageList().isEmpty()) return false;
                const PixelFormat &fromPd = from.imageList()[0].pixelFormat();
                const PixelFormat &toPd = to.imageList()[0].pixelFormat();
                if (!fromPd.isValid() || !toPd.isValid()) return false;
                if (!fromPd.isCompressed()) return false;
                if (toPd.isCompressed()) return false;

                const VideoCodec codec = VideoCodec::fromPixelFormat(fromPd);
                if (!codec.isValid()) return false;
                if (!codec.canDecode()) return false;

                if (outConfig != nullptr) {
                        *outConfig = MediaIOFactory::defaultConfig("VideoDecoder");
                        outConfig->set(MediaConfig::VideoCodec, codec);
                        outConfig->set(MediaConfig::OutputPixelFormat, toPd);
                }
                if (outCost != nullptr) {
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

MediaIOFactory::Config::SpecMap VideoDecoderFactory::configSpecs() const {
        Config::SpecMap specs;
        auto            s = [&specs](MediaConfig::ID id) {
                const VariantSpec *gs = MediaConfig::spec(id);
                if (gs) specs.insert(id, *gs);
        };
        auto sWithDefault = [&specs](MediaConfig::ID id, const Variant &def) {
                const VariantSpec *gs = MediaConfig::spec(id);
                specs.insert(id, gs ? VariantSpec(*gs).setDefault(def) : VariantSpec().setDefault(def));
        };
        s(MediaConfig::VideoCodec);
        s(MediaConfig::OutputPixelFormat);
        // VUI color-description overrides.  Default Auto / Unknown
        // means the decoder uses whatever the bitstream signalled.
        // Explicit values let the caller force a tag on a mistagged
        // stream — the decoder stamps them verbatim on the output
        // Image's Metadata instead of the bitstream value.
        s(MediaConfig::VideoColorPrimaries);
        s(MediaConfig::VideoTransferCharacteristics);
        s(MediaConfig::VideoMatrixCoefficients);
        s(MediaConfig::VideoRange);
        sWithDefault(MediaConfig::Capacity, int32_t(8));
        return specs;
}

bool VideoDecoderFactory::bridge(const MediaDesc &from, const MediaDesc &to, Config *outConfig, int *outCost) const {
        return videoDecoderBridge(from, to, outConfig, outCost);
}

MediaIO *VideoDecoderFactory::create(const Config &config, ObjectBase *parent) const {
        auto *io = new VideoDecoderMediaIO(parent);
        io->setConfig(config);
        return io;
}

VideoDecoderMediaIO::VideoDecoderMediaIO(ObjectBase *parent) : SharedThreadMediaIO(parent) {}

VideoDecoderMediaIO::~VideoDecoderMediaIO() {
        if (isOpen()) (void)close().wait();
}

void VideoDecoderMediaIO::configChanged(const MediaConfig &delta) {
        _config.merge(delta);
        if (_decoder.isValid()) _decoder->configure(_config);
}

Error VideoDecoderMediaIO::createDecoder(const VideoCodec &codec) {
        if (!codec.canDecode()) {
                promekiErr("VideoDecoderMediaIO: codec '%s' has no "
                           "registered decoder factory",
                           codec.name().cstr());
                return Error::NotSupported;
        }
        auto decResult = codec.createDecoder(&_config);
        if (error(decResult).isError()) {
                promekiErr("VideoDecoderMediaIO: createDecoder('%s') failed: %s", codec.name().cstr(),
                           error(decResult).name().cstr());
                return Error::NotSupported;
        }
        VideoDecoder::UPtr dec = VideoDecoder::UPtr::takeOwnership(value(decResult));

        if (!_outputPixelFormatSet) {
                List<PixelFormat> supported = dec->codec().decoderSupportedOutputs();
                if (!supported.isEmpty()) {
                        _outputPixelFormat = supported[0];
                        _outputPixelFormatSet = _outputPixelFormat.isValid();
                }
        }

        _codec = codec;
        _decoder = std::move(dec);
        return Error::Ok;
}

Error VideoDecoderMediaIO::executeCmd(MediaIOCommandOpen &cmd) {
        const MediaIO::Config &cfg = cmd.config;
        _config = cfg;

        _outputPixelFormat = cfg.getAs<PixelFormat>(MediaConfig::OutputPixelFormat, PixelFormat());
        _outputPixelFormatSet = _outputPixelFormat.isValid();

        _capacity = cfg.getAs<int>(MediaConfig::Capacity, 8);
        if (_capacity < 1) _capacity = 1;

        _codec = VideoCodec();
        _decoder.clear();
        _frameCount = 0;
        _readCount = 0;
        _packetsDecoded = 0;
        _imagesOut = 0;
        _capacityWarned = false;
        _closed = false;
        _outputQueue.clear();
        _pendingSrcFrames.clear();

        VideoCodec codec = cfg.getAs<VideoCodec>(MediaConfig::VideoCodec);
        if (codec.isValid()) {
                Error err = createDecoder(codec);
                if (err.isError()) return err;
        } else {
                promekiInfo("VideoDecoderMediaIO: no VideoCodec configured, "
                            "will auto-detect from first packet");
        }

        MediaDesc outDesc;
        outDesc.setFrameRate(cmd.pendingMediaDesc.frameRate());
        for (const auto &srcImg : cmd.pendingMediaDesc.imageList()) {
                ImageDesc id = srcImg;
                if (_outputPixelFormatSet) id = ImageDesc(srcImg.size(), _outputPixelFormat);
                outDesc.imageList().pushToBack(id);
        }
        for (const auto &srcAudio : cmd.pendingMediaDesc.audioList()) {
                outDesc.audioList().pushToBack(srcAudio);
        }

        MediaIOPortGroup *group = addPortGroup("vdecoder");
        if (group == nullptr) return Error::Invalid;
        group->setFrameRate(outDesc.frameRate());
        group->setCanSeek(false);
        group->setFrameCount(MediaIO::FrameCountInfinite);
        if (addSink(group, cmd.pendingMediaDesc) == nullptr) return Error::Invalid;
        if (addSource(group, outDesc) == nullptr) return Error::Invalid;
        return Error::Ok;
}

Error VideoDecoderMediaIO::executeCmd(MediaIOCommandClose &cmd) {
        (void)cmd;
        if (_decoder.isValid()) {
                _decoder->flush();
                drainDecoderInto();
                _decoder.clear();
        }
        _pendingSrcFrames.clear();
        _config = MediaConfig();
        _codec = VideoCodec();
        _outputPixelFormat = PixelFormat();
        _outputPixelFormatSet = false;
        _capacity = 0;
        _frameCount = 0;
        _readCount = 0;
        _packetsDecoded = 0;
        _imagesOut = 0;
        _capacityWarned = false;
        _closed = true;
        return Error::Ok;
}

Error VideoDecoderMediaIO::executeCmd(MediaIOCommandWrite &cmd) {
        if (!cmd.frame.isValid()) {
                promekiErr("VideoDecoderMediaIO: write with null frame");
                return Error::InvalidArgument;
        }

        const Frame &frame = *cmd.frame;

        // Collect every compressed video payload on the Frame.  The
        // decoder's payload-native @ref submitPayload entry delivers
        // each CompressedVideoPayload straight to the backend, so we
        // forward payloads directly without any packet-style bridge
        // at this layer.
        List<CompressedVideoPayload::Ptr> payloads;
        for (const VideoPayload::Ptr &vp : frame.videoPayloads()) {
                if (!vp.isValid()) continue;
                CompressedVideoPayload::Ptr cvp = sharedPointerCast<CompressedVideoPayload>(vp);
                if (cvp.isValid() && cvp->isValid()) {
                        payloads.pushToBack(std::move(cvp));
                }
        }

        if (payloads.isEmpty()) {
                promekiErr("VideoDecoderMediaIO: write frame carries no "
                           "CompressedVideoPayload; upstream must emit a "
                           "compressed video payload for every frame that "
                           "needs decoding");
                return Error::InvalidArgument;
        }

        if (_decoder.isNull()) {
                const PixelFormat &pf = payloads[0]->desc().pixelFormat();
                VideoCodec         codec = VideoCodec::fromPixelFormat(pf);
                if (!codec.isValid()) {
                        promekiErr("VideoDecoderMediaIO: cannot resolve "
                                   "VideoCodec from PixelFormat '%s'",
                                   pf.name().cstr());
                        return Error::NotSupported;
                }
                Error err = createDecoder(codec);
                if (err.isError()) {
                        return err;
                }
                promekiInfo("VideoDecoderMediaIO: auto-detected codec '%s' "
                            "from payload PixelFormat '%s'",
                            codec.name().cstr(), pf.name().cstr());
        }

        if (static_cast<int>(_outputQueue.size()) >= _capacity && !_capacityWarned) {
                promekiWarn("VideoDecoderMediaIO: output queue exceeded capacity "
                            "(%d >= %d)",
                            static_cast<int>(_outputQueue.size()), _capacity);
                _capacityWarned = true;
        }

        for (const CompressedVideoPayload::Ptr &payload : payloads) {
                _pendingSrcFrames.pushToBack(cmd.frame);
                Error err = _decoder->submitPayload(payload);
                if (err.isError()) {
                        promekiErr("VideoDecoderMediaIO: submitPayload failed: %s",
                                   _decoder->lastErrorMessage().cstr());
                        return err;
                }
                _packetsDecoded++;
                drainDecoderInto();
        }

        _frameCount++;
        cmd.currentFrame = toFrameNumber(_frameCount);
        cmd.frameCount = _frameCount;
        return Error::Ok;
}

void VideoDecoderMediaIO::drainDecoderInto() {
        if (_decoder.isNull()) return;
        while (true) {
                UncompressedVideoPayload::Ptr outPayload = _decoder->receiveVideoPayload();
                if (!outPayload.isValid()) break;

                // Pair this payload with the oldest queued source
                // Frame — that's the packet Frame that produced it
                // even when DPB warmup delays a few frames.  Empty
                // queue is a best-effort fallback (shouldn't happen
                // for I/P-only streams but leaves the payload intact
                // for any other).
                Frame::Ptr origin;
                if (!_pendingSrcFrames.isEmpty()) {
                        origin = _pendingSrcFrames.front();
                        _pendingSrcFrames.remove(0);
                }

                Frame::Ptr outFrame = Frame::Ptr::create();
                Frame     *out = outFrame.modify();
                if (origin.isValid()) {
                        out->metadata() = origin->metadata();
                        for (const AudioPayload::Ptr &ap : origin->audioPayloads()) {
                                if (ap.isValid()) out->addPayload(ap);
                        }
                }
                out->addPayload(outPayload);
                _outputQueue.pushToBack(std::move(outFrame));
                _imagesOut++;
        }
}

Error VideoDecoderMediaIO::executeCmd(MediaIOCommandRead &cmd) {
        if (_outputQueue.isEmpty()) {
                return _closed ? Error::EndOfFile : Error::TryAgain;
        }
        Frame::Ptr frame = std::move(_outputQueue.front());
        _outputQueue.remove(0);
        _readCount++;
        cmd.frame = std::move(frame);
        cmd.currentFrame = _readCount;
        return Error::Ok;
}

Error VideoDecoderMediaIO::executeCmd(MediaIOCommandStats &cmd) {
        cmd.stats.set(StatsPacketsDecoded, _packetsDecoded);
        cmd.stats.set(StatsImagesOut, _imagesOut);
        cmd.stats.set(MediaIOStats::QueueDepth, static_cast<int64_t>(_outputQueue.size()));
        cmd.stats.set(MediaIOStats::QueueCapacity, static_cast<int64_t>(_capacity));
        return Error::Ok;
}

int VideoDecoderMediaIO::pendingInternalWrites() const {
        return static_cast<int>(_outputQueue.size());
}

// ---- Phase 1 introspection / negotiation overrides ----

Error VideoDecoderMediaIO::describe(MediaIODescription *out) const {
        if (out == nullptr) return Error::Invalid;
        // Let the base populate identity / role flags / port snapshots.
        Error baseErr = MediaIO::describe(out);
        if (baseErr.isError()) return baseErr;

        // If the planner / caller has already pinned a codec via
        // config, advertise every compressed PixelFormat that codec
        // covers as an acceptable input shape.
        if (_codec.isValid()) {
                for (const PixelFormat &pf : _codec.compressedPixelFormats()) {
                        MediaDesc accepted;
                        accepted.imageList().pushToBack(ImageDesc(Size2Du32(0, 0), pf));
                        out->acceptableFormats().pushToBack(accepted);
                }
        }
        if (_outputPixelFormatSet) {
                MediaDesc produced;
                produced.imageList().pushToBack(ImageDesc(Size2Du32(0, 0), _outputPixelFormat));
                out->producibleFormats().pushToBack(produced);
                out->setPreferredFormat(produced);
        }
        return Error::Ok;
}

Error VideoDecoderMediaIO::proposeInput(const MediaDesc &offered, MediaDesc *preferred) const {
        if (preferred == nullptr) return Error::Invalid;
        if (offered.imageList().isEmpty()) return Error::NotSupported;

        // VideoDecoder consumes compressed video only.
        const PixelFormat &pd = offered.imageList()[0].pixelFormat();
        if (!pd.isCompressed()) return Error::NotSupported;

        // If a codec has been pinned, the offered codec must match.
        if (_codec.isValid()) {
                const VideoCodec offeredCodec = VideoCodec::fromPixelFormat(pd);
                if (offeredCodec != _codec) return Error::NotSupported;
        } else {
                // Codec auto-detect mode: any compressed PixelFormat
                // whose codec the registry can decode is acceptable.
                const VideoCodec offeredCodec = VideoCodec::fromPixelFormat(pd);
                if (!offeredCodec.isValid() || !offeredCodec.canDecode()) {
                        return Error::NotSupported;
                }
        }
        *preferred = offered;
        return Error::Ok;
}

Error VideoDecoderMediaIO::proposeOutput(const MediaDesc &requested, MediaDesc *achievable) const {
        if (achievable == nullptr) return Error::Invalid;
        const MediaConfig &cfg = config();

        // Start from the input shape and apply any explicit
        // Output* overrides (OutputPixelFormat, OutputFrameRate, ...).
        MediaDesc proposed = MediaIO::applyOutputOverrides(requested, cfg);

        // When OutputPixelFormat is unset, the planner needs to know
        // that the *output* of this decoder is uncompressed — not
        // the compressed shape it was handed on the input.  The
        // authoritative answer is the registered decoder backend's
        // supported output list (the same fallback @ref createDecoder
        // uses at open time); only when no codec / backend is
        // registered do we fall back to the input PixelFormat's static
        // decodeTargets list, which is sparse and missing for codecs
        // like H264 / HEVC that haven't had it backfilled.
        const bool hasExplicitOutput = cfg.contains(MediaConfig::OutputPixelFormat) &&
                                       cfg.getAs<PixelFormat>(MediaConfig::OutputPixelFormat).isValid();
        if (!hasExplicitOutput && !requested.imageList().isEmpty()) {
                const PixelFormat &inPd = requested.imageList()[0].pixelFormat();
                if (inPd.isCompressed()) {
                        PixelFormat      rawPd;
                        const VideoCodec codec = VideoCodec::fromPixelFormat(inPd);
                        if (codec.isValid() && codec.canDecode()) {
                                const auto supported = codec.decoderSupportedOutputs();
                                if (!supported.isEmpty()) rawPd = supported.front();
                        }
                        if (!rawPd.isValid() && !inPd.decodeTargets().isEmpty()) {
                                rawPd = PixelFormat(inPd.decodeTargets().front());
                        }
                        if (rawPd.isValid()) {
                                ImageDesc::List &imgs = proposed.imageList();
                                for (size_t i = 0; i < imgs.size(); ++i) {
                                        imgs[i].setPixelFormat(rawPd);
                                }
                        }
                }
        }

        *achievable = proposed;
        return Error::Ok;
}

PROMEKI_NAMESPACE_END
