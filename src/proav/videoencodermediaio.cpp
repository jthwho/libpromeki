/**
 * @file      videoencodermediaio.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 */

#include <climits>
#include <cstdlib>
#include <cstring>
#include <promeki/videoencodermediaio.h>
#include <promeki/mediaioportgroup.h>
#include <promeki/mediaconfig.h>
#include <promeki/enums_color.h>
#include <promeki/enums_video.h>
#include <promeki/frame.h>
#include <promeki/framerate.h>
#include <promeki/imagedesc.h>
#include <promeki/mediadesc.h>
#include <promeki/mediaiodescription.h>
#include <promeki/compressedvideopayload.h>
#include <promeki/uncompressedvideopayload.h>
#include <promeki/audiopayload.h>
#include <promeki/metadata.h>
#include <promeki/pixelformat.h>
#include <promeki/buffer.h>
#include <promeki/logger.h>
#include <promeki/mediatimestamp.h>
#include <promeki/mediaiorequest.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_DEBUG(VideoEncoderMediaIO)

PROMEKI_REGISTER_MEDIAIO_FACTORY(VideoEncoderFactory)

namespace {

        // Bridge: VideoEncoder bridges uncompressed → compressed.  The
        // planner inserts it whenever a downstream sink demands a compressed
        // PixelFormat that an upstream uncompressed source can supply.
        bool videoEncoderBridge(const MediaDesc &from, const MediaDesc &to, MediaIO::Config *outConfig, int *outCost) {
                if (from.imageList().isEmpty() || to.imageList().isEmpty()) return false;
                const PixelFormat &fromPd = from.imageList()[0].pixelFormat();
                const PixelFormat &toPd = to.imageList()[0].pixelFormat();
                if (!fromPd.isValid() || !toPd.isValid()) return false;
                if (fromPd.isCompressed()) return false;
                if (!toPd.isCompressed()) return false;

                // Audio must already match: VideoEncoder forwards every
                // audio payload as-is, so if the downstream sink wants a
                // different audio shape too the planner needs a separate
                // audio bridge.  Mirrors @c audioEncoderBridge's pixel-side
                // strictness so the planner can chain video + audio
                // bridges deterministically instead of picking VideoEncoder
                // as a "single bridge solves it" winner that silently
                // leaves the audio gap unresolved.
                if (from.audioList().size() != to.audioList().size()) return false;
                const size_t audioCount = from.audioList().size();
                for (size_t i = 0; i < audioCount; ++i) {
                        const AudioDesc &a = from.audioList()[i];
                        const AudioDesc &b = to.audioList()[i];
                        if (a.sampleRate() != b.sampleRate()) return false;
                        if (a.channels() != b.channels()) return false;
                        if (a.format().id() != b.format().id()) return false;
                }

                const VideoCodec codec = VideoCodec::fromPixelFormat(toPd);
                if (!codec.isValid()) return false;
                if (!codec.canEncode()) return false;

                // Reject when the source PixelFormat isn't directly
                // accepted by any registered encoder backend for this
                // codec.  An empty supportedInputs list is the codec's
                // "accepts anything uncompressed" contract (no backend
                // is fussy about input — keep the legacy permissive
                // behaviour for those).  When the list is non-empty and
                // the source isn't on it, the planner needs to splice
                // a CSC in front of the encoder; @ref
                // findVideoCscEncoderChain handles that two-hop case.
                const List<PixelFormat> supported = codec.encoderSupportedInputs();
                if (!supported.isEmpty()) {
                        bool ok = false;
                        for (const PixelFormat &cand : supported) {
                                if (cand == fromPd) {
                                        ok = true;
                                        break;
                                }
                        }
                        if (!ok) return false;
                }

                if (outConfig != nullptr) {
                        *outConfig = MediaIOFactory::defaultConfig("VideoEncoder");
                        outConfig->set(MediaConfig::VideoCodec, codec);
                }
                if (outCost != nullptr) {
                        // Encoding is lossy by construction.  Sit in the
                        // "heavily lossy" band so the planner avoids it
                        // unless the sink really needs the compressed form.
                        *outCost = 5000;
                }
                return true;
        }

} // namespace

MediaIOFactory::Config::SpecMap VideoEncoderFactory::configSpecs() const {
        Config::SpecMap specs;
        // Inherit the library-wide spec for each key so descriptions,
        // types, ranges, and enum types stay consistent across every
        // backend.  Local defaults only override when we want a
        // backend-specific preferred value different from the
        // MediaConfig library default.
        auto s = [&specs](MediaConfig::ID id) {
                const VariantSpec *gs = MediaConfig::spec(id);
                if (gs) specs.insert(id, *gs);
        };
        auto sWithDefault = [&specs](MediaConfig::ID id, const Variant &def) {
                const VariantSpec *gs = MediaConfig::spec(id);
                specs.insert(id, gs ? VariantSpec(*gs).setDefault(def) : VariantSpec().setDefault(def));
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
}

bool VideoEncoderFactory::bridge(const MediaDesc &from, const MediaDesc &to, Config *outConfig, int *outCost) const {
        return videoEncoderBridge(from, to, outConfig, outCost);
}

MediaIO *VideoEncoderFactory::create(const Config &config, ObjectBase *parent) const {
        auto *io = new VideoEncoderMediaIO(parent);
        io->setConfig(config);
        return io;
}

VideoEncoderMediaIO::VideoEncoderMediaIO(ObjectBase *parent) : SharedThreadMediaIO(parent) {}

VideoEncoderMediaIO::~VideoEncoderMediaIO() {
        if (isOpen()) (void)close().wait();
}

Error VideoEncoderMediaIO::executeCmd(MediaIOCommandOpen &cmd) {
        const MediaIO::Config &cfg = cmd.config;
        _config = cfg;

        _codec = cfg.getAs<VideoCodec>(MediaConfig::VideoCodec);
        if (!_codec.isValid()) {
                promekiErr("VideoEncoderMediaIO: VideoCodec is required "
                           "(e.g. \"H264\", \"HEVC\", \"JPEG\")");
                return Error::InvalidArgument;
        }
        if (!_codec.canEncode()) {
                promekiErr("VideoEncoderMediaIO: codec '%s' has no "
                           "registered encoder factory",
                           _codec.name().cstr());
                return Error::NotSupported;
        }

        // Typed factory lookup; the registered VideoEncoder subclass
        // owns any codec-specific state (NVENC session, CUDA context, …).
        // Build the encoder-specific MediaConfig ahead of the factory so
        // VideoCodec::createEncoder can both select a backend (via
        // MediaConfig::CodecBackend) and forward the full config to the
        // returned instance's configure() in one shot.
        MediaIO::Config encCfg = cfg;
        if (cmd.pendingMediaDesc.frameRate().isValid()) {
                encCfg.set(MediaConfig::FrameRate, cmd.pendingMediaDesc.frameRate());
        }
        auto encResult = _codec.createEncoder(&encCfg);
        if (error(encResult).isError()) {
                promekiErr("VideoEncoderMediaIO: createEncoder('%s') failed: %s", _codec.name().cstr(),
                           error(encResult).name().cstr());
                return Error::NotSupported;
        }
        VideoEncoder::UPtr enc = VideoEncoder::UPtr::takeOwnership(value(encResult));

        _capacity = cfg.getAs<int>(MediaConfig::Capacity, 8);
        if (_capacity < 1) _capacity = 1;

        // Build the downstream-visible MediaDesc: each source image is
        // replaced by one at the encoder's compressed PixelFormat so the
        // next stage picks up the right format before the first frame
        // arrives.
        MediaDesc outDesc;
        outDesc.setFrameRate(cmd.pendingMediaDesc.frameRate());

        // Derive the encoder's output PixelFormat.  Prefer the
        // user's explicitly configured @c VideoPixelFormat when it is
        // a compressed PD for this codec — that's how callers pin a
        // specific bitstream variant (e.g. JPEG XS 10-bit 4:2:2 vs.
        // 8-bit 4:2:0).  Without this, the SDP writer downstream
        // reads the wrong PD off the encoder's output MediaDesc and
        // advertises an 8-bit (or otherwise-mismatched) wire format
        // while the encoder actually produces the configured 10-bit
        // bitstream — receiver-side decode then fails the
        // bit_depth / colour_format check.  Per-frame variation
        // (JPEG's per-image subsampling, for example) is reflected
        // on the emitted CompressedVideoPayload itself.
        PixelFormat encOutPd;
        {
                const PixelFormat configuredPd = cfg.getAs<PixelFormat>(MediaConfig::VideoPixelFormat);
                if (configuredPd.isValid() && configuredPd.isCompressed() &&
                    configuredPd.videoCodec() == _codec) {
                        encOutPd = configuredPd;
                } else {
                        const auto compressedList = _codec.compressedPixelFormats();
                        if (!compressedList.isEmpty()) {
                                encOutPd = compressedList[0];
                        }
                }
        }
        for (const auto &srcImg : cmd.pendingMediaDesc.imageList()) {
                outDesc.imageList().pushToBack(ImageDesc(srcImg.size(), encOutPd));
        }
        for (const auto &srcAudio : cmd.pendingMediaDesc.audioList()) {
                outDesc.audioList().pushToBack(srcAudio);
        }

        _encoder = std::move(enc);
        _frameCount = 0;
        _readCount = 0;
        _framesEncoded = 0;
        _packetsOut = 0;
        _capacityWarned = false;
        _closed = false;
        _outputQueue.clear();

        MediaIOPortGroup *group = addPortGroup("vencoder");
        if (group == nullptr) {
                promekiWarn("VideoEncoderMediaIO: addPortGroup('vencoder') failed");
                return Error::Invalid;
        }
        group->setFrameRate(outDesc.frameRate());
        group->setCanSeek(false);
        group->setFrameCount(MediaIO::FrameCountInfinite);
        if (addSink(group, cmd.pendingMediaDesc) == nullptr) {
                promekiWarn("VideoEncoderMediaIO: addSink failed (fps=%s)",
                            cmd.pendingMediaDesc.frameRate().toString().cstr());
                return Error::Invalid;
        }
        if (addSource(group, outDesc) == nullptr) {
                promekiWarn("VideoEncoderMediaIO: addSource failed (fps=%s)",
                            outDesc.frameRate().toString().cstr());
                return Error::Invalid;
        }
        return Error::Ok;
}

Error VideoEncoderMediaIO::executeCmd(MediaIOCommandClose &cmd) {
        (void)cmd;
        if (_encoder.isValid()) {
                // Best-effort flush so anything the encoder has buffered
                // makes it out before we tear the session down.  Because
                // pipelines close us only after all reads have been
                // consumed, any packets emitted here are stored in
                // _outputQueue for a subsequent read if the caller cares
                // to drain again — most consumers won't, but the
                // contract stays honest either way.
                _encoder->flush();
                drainEncoderInto();
                _encoder.clear();
        }
        _config = MediaConfig();
        _codec = VideoCodec();
        _capacity = 0;
        _frameCount = 0;
        _readCount = 0;
        _framesEncoded = 0;
        _packetsOut = 0;
        _capacityWarned = false;
        _closed = true;
        return Error::Ok;
}

void VideoEncoderMediaIO::configChanged(const MediaConfig &delta) {
        _config.merge(delta);
        if (_encoder.isValid()) _encoder->configure(_config);
}

Error VideoEncoderMediaIO::executeCmd(MediaIOCommandWrite &cmd) {
        if (!cmd.frame.isValid()) {
                promekiErr("VideoEncoderMediaIO: write with null frame");
                return Error::InvalidArgument;
        }
        if (_encoder.isNull()) {
                return Error::NotSupported;
        }

        if (static_cast<int>(_outputQueue.size()) >= _capacity && !_capacityWarned) {
                promekiWarn("VideoEncoderMediaIO: output queue exceeded capacity "
                            "(%d >= %d) — downstream is not draining packets fast enough",
                            static_cast<int>(_outputQueue.size()), _capacity);
                _capacityWarned = true;
        }

        const Frame &frame = cmd.frame;

        // If the source Frame has an uncompressed video payload whose
        // metadata requests an IDR/keyframe, prime the encoder before
        // submitting.  No explicit early-return for audio-only frames:
        // submitFrame will recognise the empty video case and
        // emit a passthrough Frame via the base helper.
        UncompressedVideoPayload::Ptr srcPayload = VideoEncoder::selectInputPayload(frame);
        if (srcPayload.isValid() && srcPayload->desc().metadata().getAs<bool>(Metadata::ForceKeyframe)) {
                _encoder->requestKeyframe();
        }

        if (!srcPayload.isValid()) {
                // No image to encode — let the frame pass through so
                // audio / metadata-only inputs aren't lost.  Build the
                // pass-through using the base helper (echoes audio /
                // ANC / metadata) without a compressed payload.
                _outputQueue.pushToBack(VideoEncoder::buildOutputFrame(frame, CompressedVideoPayload::Ptr()));
                return Error::Ok;
        }

        Error err = _encoder->submitFrame(frame);
        if (err.isError()) {
                promekiErr("VideoEncoderMediaIO: submitFrame failed: %s", _encoder->lastErrorMessage().cstr());
                return err;
        }
        _frameCount++;
        _framesEncoded++;
        drainEncoderInto();

        cmd.currentFrame = toFrameNumber(_frameCount);
        cmd.frameCount = _frameCount;
        return Error::Ok;
}

void VideoEncoderMediaIO::drainEncoderInto() {
        if (_encoder.isNull()) return;
        while (true) {
                Frame outFrame = _encoder->receiveFrame();
                if (!outFrame.isValid()) break;

                // EOS is an encoder-internal signal that the session
                // is drained; no need to propagate as its own Frame
                // (the pipeline uses the MediaIO close/EOF path for
                // that).  Inspect the frame's compressed payloads for
                // the marker and drop the frame if present.
                bool eos = false;
                for (const VideoPayload::Ptr &vp : outFrame.videoPayloads()) {
                        if (!vp.isValid()) continue;
                        CompressedVideoPayload::Ptr cvp = sharedPointerCast<CompressedVideoPayload>(vp);
                        if (cvp.isValid() && cvp->isEndOfStream()) {
                                eos = true;
                                break;
                        }
                }
                if (eos) continue;

                _outputQueue.pushToBack(std::move(outFrame));
                _packetsOut++;
        }
}

Error VideoEncoderMediaIO::executeCmd(MediaIOCommandRead &cmd) {
        if (_outputQueue.isEmpty()) {
                return _closed ? Error::EndOfFile : Error::TryAgain;
        }
        Frame frame = std::move(_outputQueue.front());
        _outputQueue.remove(0);
        _readCount++;
        cmd.frame = std::move(frame);
        cmd.currentFrame = _readCount;
        return Error::Ok;
}

Error VideoEncoderMediaIO::executeCmd(MediaIOCommandStats &cmd) {
        cmd.stats.set(StatsFramesEncoded, _framesEncoded);
        cmd.stats.set(StatsPacketsOut, _packetsOut);
        cmd.stats.set(MediaIOStats::QueueDepth, static_cast<int64_t>(_outputQueue.size()));
        cmd.stats.set(MediaIOStats::QueueCapacity, static_cast<int64_t>(_capacity));
        return Error::Ok;
}

int VideoEncoderMediaIO::pendingInternalWrites() const {
        return static_cast<int>(_outputQueue.size());
}

// ---- Phase 1 introspection / negotiation overrides ----

Error VideoEncoderMediaIO::describe(MediaIODescription *out) const {
        if (out == nullptr) return Error::Invalid;
        // Let the base populate identity / role flags / port snapshots.
        Error baseErr = MediaIO::describe(out);
        if (baseErr.isError()) return baseErr;

        // When a codec has been pinned via config, advertise it as
        // the producible compressed shape.  The acceptable side
        // stays empty as a "any uncompressed video" signal.
        //
        // Preferred format: if @c VideoPixelFormat is configured to a
        // compressed PD for this codec (e.g. the user pinned
        // @c JPEG_XS_YUV10_422_Rec709), honour it.  Otherwise fall
        // back to the registry's first compressed PD — sufficient for
        // codecs whose output PD is chroma-/depth-agnostic
        // (@c H264, @c HEVC) but wrong for chroma-/depth-specific
        // families (@c JPEG_XS_*) where the first-in-registry pick
        // silently disagrees with the user's configured output.
        if (_codec.isValid()) {
                for (const PixelFormat &pf : _codec.compressedPixelFormats()) {
                        MediaDesc produced;
                        produced.imageList().pushToBack(ImageDesc(Size2Du32(0, 0), pf));
                        out->producibleFormats().pushToBack(produced);
                }
                if (!out->producibleFormats().isEmpty()) {
                        const PixelFormat configuredPd =
                                config().getAs<PixelFormat>(MediaConfig::VideoPixelFormat);
                        size_t preferIdx = 0;
                        if (configuredPd.isValid() && configuredPd.isCompressed() &&
                            configuredPd.videoCodec() == _codec) {
                                for (size_t i = 0; i < out->producibleFormats().size(); ++i) {
                                        const MediaDesc &md = out->producibleFormats()[i];
                                        if (md.imageList().isEmpty()) continue;
                                        if (md.imageList()[0].pixelFormat() == configuredPd) {
                                                preferIdx = i;
                                                break;
                                        }
                                }
                        }
                        out->setPreferredFormat(out->producibleFormats()[preferIdx]);
                }
        }
        return Error::Ok;
}

Error VideoEncoderMediaIO::proposeInput(const MediaDesc &offered, MediaDesc *preferred) const {
        if (preferred == nullptr) return Error::Invalid;
        if (offered.imageList().isEmpty()) return Error::NotSupported;

        // VideoEncoder consumes uncompressed video only.
        const PixelFormat &pd = offered.imageList()[0].pixelFormat();
        if (!pd.isValid()) return Error::NotSupported;
        if (pd.isCompressed()) return Error::NotSupported;

        // Resolve the codec — either pinned by an earlier open(), or
        // read straight off the stage config during planning (the
        // planner constructs the stage via MediaIO::create but does
        // not open it, so _codec is still invalid at this point).
        VideoCodec codec = _codec;
        if (!codec.isValid() && true) {
                codec = config().getAs<VideoCodec>(MediaConfig::VideoCodec);
        }

        // Without a codec we can't know which inputs the concrete
        // encoder will accept.  Fall back to passthrough; the planner
        // will fail later at open time rather than insert a bogus
        // bridge now.
        if (!codec.isValid() || !codec.canEncode()) {
                *preferred = offered;
                return Error::Ok;
        }

        // Query the backend's supported input list.  If a session is
        // already live (stage opened) we read it off the pinned codec
        // on the encoder; otherwise we pull the union list from the
        // codec registry so the planner never has to instantiate an
        // encoder (and touch its GPU / libjpeg / SVT-JPEG-XS setup)
        // just to introspect supported inputs.
        List<PixelFormat> supported;
        if (_encoder.isValid()) {
                supported = _encoder->codec().encoderSupportedInputs();
        } else {
                supported = codec.encoderSupportedInputs();
        }

        // Empty list means "accepts any uncompressed input" per the
        // VideoEncoder::supportedInputs contract.
        if (supported.isEmpty()) {
                *preferred = offered;
                return Error::Ok;
        }

        // Short-circuit: if the upstream offered a @ref PixelFormat the
        // encoder accepts directly, use it — don't second-guess the
        // caller's chroma choice against a generic spec default.  This
        // matters specifically when the configured output PD encodes
        // chroma (e.g. @c JPEG_XS_YUV10_422_Rec709 with memLayout
        // @c P_422_3x10_LE) and the upstream is the matching planar
        // source.  Without this short-circuit, the @c VideoChromaSubsampling
        // 4:2:0 spec default would force the planner to swap the
        // upstream to whatever variant matches 4:2:0 and the encoder
        // would then reject the wrong chroma at @c encodeFrame time.
        for (const PixelFormat &cand : supported) {
                if (cand == pd) {
                        *preferred = offered;
                        return Error::Ok;
                }
        }

        // Which chroma does the caller want the CSC to land on?  The
        // config default is 4:2:0 so an RGB or 4:4:4 source routed into
        // an encoder picks the universally-decodable format unless the
        // user opts in to higher-quality chroma with VideoChromaSubsampling.
        //
        // Refinement: when the encoder's configured output
        // @ref VideoPixelFormat is a compressed format whose memory
        // layout encodes the chroma (e.g. @c JPEG_XS_YUV10_422_Rec709
        // uses @c P_422_3x10_LE), prefer that as the bridge target.
        // Codecs that don't encode chroma in their wire
        // @ref PixelFormat (@c H264 / @c HEVC) yield the same YUV420
        // default — callers opt into a different chroma via
        // @c VideoChromaSubsampling, which overrides the PD-derived
        // value because the user's explicit choice always wins.
        ChromaSubsampling wantChroma = ChromaSubsampling::YUV420;
        {
                const PixelFormat outPd = config().getAs<PixelFormat>(MediaConfig::VideoPixelFormat);
                if (outPd.isValid() && outPd.isCompressed()) {
                        const PixelMemLayout::Sampling s = outPd.memLayout().sampling();
                        if (s == PixelMemLayout::Sampling444)
                                wantChroma = ChromaSubsampling::YUV444;
                        else if (s == PixelMemLayout::Sampling422)
                                wantChroma = ChromaSubsampling::YUV422;
                        else if (s == PixelMemLayout::Sampling420)
                                wantChroma = ChromaSubsampling::YUV420;
                }
                const Enum raw = config().getAs<Enum>(MediaConfig::VideoChromaSubsampling);
                if (raw.type() == ChromaSubsampling::Type) {
                        wantChroma = ChromaSubsampling(raw.value());
                }
        }

        auto chromaToSampling = [](ChromaSubsampling c) {
                if (c == ChromaSubsampling::YUV444) return PixelMemLayout::Sampling444;
                if (c == ChromaSubsampling::YUV422) return PixelMemLayout::Sampling422;
                return PixelMemLayout::Sampling420;
        };
        const PixelMemLayout::Sampling wantSampling = chromaToSampling(wantChroma);

        const int offeredBits = pd.memLayout().compCount() > 0 ? static_cast<int>(pd.memLayout().compDesc(0).bits) : 0;

        // Pick the best-matching supported format to advertise so the
        // planner splices in a CSC.  Preference order:
        //   1. Requested chroma AND same bit-depth as source.
        //   2. Requested chroma (any bit-depth, prefers closer depth).
        //   3. Same bit-depth as source (any chroma).
        //   4. First entry (final fallback — every encoder lists at
        //      least one format).
        PixelFormat bestPick = supported[0];
        int         bestTier = 4;
        int         bestBitsDelta = INT_MAX;
        for (const PixelFormat &cand : supported) {
                if (!cand.isValid()) continue;
                const int candBits =
                        cand.memLayout().compCount() > 0 ? static_cast<int>(cand.memLayout().compDesc(0).bits) : 0;
                const bool sameChroma = (cand.memLayout().sampling() == wantSampling);
                const bool sameBits = (offeredBits > 0 && candBits == offeredBits);

                int tier = 4;
                if (sameChroma && sameBits)
                        tier = 1;
                else if (sameChroma)
                        tier = 2;
                else if (sameBits)
                        tier = 3;

                const int bitsDelta = (offeredBits > 0 && candBits > 0) ? std::abs(candBits - offeredBits) : INT_MAX;

                if (tier < bestTier || (tier == bestTier && bitsDelta < bestBitsDelta)) {
                        bestTier = tier;
                        bestPick = cand;
                        bestBitsDelta = bitsDelta;
                }
        }

        MediaDesc        out = offered;
        ImageDesc::List &imgs = out.imageList();
        for (size_t i = 0; i < imgs.size(); ++i) {
                imgs[i].setPixelFormat(bestPick);
        }
        *preferred = out;
        return Error::Ok;
}

Error VideoEncoderMediaIO::proposeOutput(const MediaDesc &requested, MediaDesc *achievable,
                                         MediaConfig *configDelta) const {
        if (achievable == nullptr) return Error::Invalid;
        (void)configDelta;

        // Resolve the codec from a live session when open, or from
        // the stage config during planning (the same fallback used
        // by proposeInput above).
        VideoCodec codec = _codec;
        if (!codec.isValid() && true) {
                codec = config().getAs<VideoCodec>(MediaConfig::VideoCodec);
        }
        if (!codec.isValid() || codec.compressedPixelFormats().isEmpty()) {
                // Codec not yet known — defer; the planner has to
                // pin it before the encoder can answer.
                return Error::NotSupported;
        }

        // The encoder produces a compressed PixelFormat whose codec
        // matches the configured VideoCodec.  Prefer the user's
        // explicitly configured @c VideoPixelFormat when it's a
        // compressed PD for this codec — that's how the JXS / JPEG
        // matrix entries (whose output PD encodes depth / chroma)
        // pin which specific variant to produce.  Without this, the
        // planner-time advertisement falls back to
        // @c compressedPixelFormats()[0] and the SDP writer emits
        // whatever sits first in the registry (e.g. 8-bit JPEG XS)
        // instead of the configured 10-bit variant.
        MediaDesc         out = requested;
        PixelFormat       compressed = codec.compressedPixelFormats()[0];
        {
                const PixelFormat configuredPd = config().getAs<PixelFormat>(MediaConfig::VideoPixelFormat);
                if (configuredPd.isValid() && configuredPd.isCompressed() &&
                    configuredPd.videoCodec() == codec) {
                        compressed = configuredPd;
                }
        }
        ImageDesc::List &imgs = out.imageList();
        for (size_t i = 0; i < imgs.size(); ++i) {
                imgs[i].setPixelFormat(compressed);
        }
        *achievable = out;
        return Error::Ok;
}

PROMEKI_NAMESPACE_END
