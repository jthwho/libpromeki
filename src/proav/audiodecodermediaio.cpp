/**
 * @file      audiodecodermediaio.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 */

#include <promeki/audiodecodermediaio.h>
#include <promeki/mediaioportgroup.h>
#include <promeki/mediaconfig.h>
#include <promeki/enums_audio.h>
#include <promeki/frame.h>
#include <promeki/audiodesc.h>
#include <promeki/mediadesc.h>
#include <promeki/mediaiodescription.h>
#include <promeki/metadata.h>
#include <promeki/audioformat.h>
#include <promeki/compressedaudiopayload.h>
#include <promeki/videopayload.h>
#include <promeki/logger.h>
#include <promeki/mediaiorequest.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_REGISTER_MEDIAIO_FACTORY(AudioDecoderFactory)

namespace {

        // Bridge: AudioDecoder bridges compressed → PCM.  The planner
        // inserts it whenever a downstream stage cannot accept the
        // upstream's compressed AudioFormat.
        bool audioDecoderBridge(const MediaDesc &from, const MediaDesc &to, MediaIO::Config *outConfig, int *outCost) {
                if (from.audioList().isEmpty() || to.audioList().isEmpty()) return false;
                const AudioFormat &fromFmt = from.audioList()[0].format();
                const AudioFormat &toFmt = to.audioList()[0].format();
                if (!fromFmt.isValid() || !toFmt.isValid()) return false;
                if (!fromFmt.isCompressed()) return false;
                if (toFmt.isCompressed()) return false;

                // Pixel side must match: the decoder forwards every
                // video payload as-is.
                if (!from.imageList().isEmpty() && !to.imageList().isEmpty()) {
                        if (from.imageList()[0].pixelFormat() != to.imageList()[0].pixelFormat()) return false;
                }

                const AudioCodec codec = fromFmt.audioCodec();
                if (!codec.isValid()) return false;
                if (!codec.canDecode()) return false;

                if (outConfig != nullptr) {
                        *outConfig = MediaIOFactory::defaultConfig("AudioDecoder");
                        outConfig->set(MediaConfig::AudioCodec, codec);
                        outConfig->set(MediaConfig::OutputAudioDataType, AudioDataType(toFmt.id()));
                }
                if (outCost != nullptr) {
                        // Decoding to the bitstream's reference
                        // representation is lossless — only the
                        // optional sample-format conversion inside
                        // the decoder (when the requested PCM format
                        // differs from the codec's native output)
                        // costs.  Treat it as a fixed precision-
                        // preserving hop.
                        *outCost = 20;
                }
                return true;
        }

} // namespace

MediaIOFactory::Config::SpecMap AudioDecoderFactory::configSpecs() const {
        Config::SpecMap specs;
        auto            s = [&specs](MediaConfig::ID id) {
                const VariantSpec *gs = MediaConfig::spec(id);
                if (gs) specs.insert(id, *gs);
        };
        auto sWithDefault = [&specs](MediaConfig::ID id, const Variant &def) {
                const VariantSpec *gs = MediaConfig::spec(id);
                specs.insert(id, gs ? VariantSpec(*gs).setDefault(def) : VariantSpec().setDefault(def));
        };
        s(MediaConfig::AudioCodec);
        s(MediaConfig::CodecBackend);
        s(MediaConfig::OutputAudioDataType);
        s(MediaConfig::AudioRate);
        s(MediaConfig::AudioChannels);
        sWithDefault(MediaConfig::Capacity, int32_t(8));
        return specs;
}

bool AudioDecoderFactory::bridge(const MediaDesc &from, const MediaDesc &to, Config *outConfig, int *outCost) const {
        return audioDecoderBridge(from, to, outConfig, outCost);
}

MediaIO *AudioDecoderFactory::create(const Config &config, ObjectBase *parent) const {
        auto *io = new AudioDecoderMediaIO(parent);
        io->setConfig(config);
        return io;
}

AudioDecoderMediaIO::AudioDecoderMediaIO(ObjectBase *parent) : SharedThreadMediaIO(parent) {}

AudioDecoderMediaIO::~AudioDecoderMediaIO() {
        if (isOpen()) (void)close().wait();
}

void AudioDecoderMediaIO::configChanged(const MediaConfig &delta) {
        _config.merge(delta);
        if (_decoder.isValid()) _decoder->configure(_config);
}

Error AudioDecoderMediaIO::createDecoder(const AudioCodec &codec) {
        if (!codec.canDecode()) {
                promekiErr("AudioDecoderMediaIO: codec '%s' has no "
                           "registered decoder factory",
                           codec.name().cstr());
                return Error::NotSupported;
        }
        auto decResult = codec.createDecoder(&_config);
        if (error(decResult).isError()) {
                promekiErr("AudioDecoderMediaIO: createDecoder('%s') failed: %s", codec.name().cstr(),
                           error(decResult).name().cstr());
                return Error::NotSupported;
        }
        AudioDecoder::UPtr dec = AudioDecoder::UPtr::takeOwnership(value(decResult));

        if (!_outputAudioDataTypeSet) {
                List<AudioFormat> supported = dec->codec().decoderSupportedOutputs();
                if (!supported.isEmpty() && supported[0].isValid()) {
                        _outputAudioDataType = supported[0].id();
                        _outputAudioDataTypeSet = (_outputAudioDataType != AudioFormat::Invalid);
                }
        }

        _codec = codec;
        _decoder = std::move(dec);
        return Error::Ok;
}

Error AudioDecoderMediaIO::executeCmd(MediaIOCommandOpen &cmd) {
        const MediaIO::Config &cfg = cmd.config;
        _config = cfg;

        _outputAudioDataType = AudioFormat::Invalid;
        _outputAudioDataTypeSet = false;
        if (cfg.contains(MediaConfig::OutputAudioDataType)) {
                Error      enumErr;
                const Enum adtEnum =
                        cfg.get(MediaConfig::OutputAudioDataType).asEnum(AudioDataType::Type, &enumErr);
                if (enumErr.isOk() && adtEnum.value() != AudioFormat::Invalid) {
                        _outputAudioDataType = static_cast<AudioFormat::ID>(adtEnum.value());
                        _outputAudioDataTypeSet = true;
                }
        }

        _capacity = cfg.getAs<int>(MediaConfig::Capacity, 8);
        if (_capacity < 1) _capacity = 1;

        _codec = AudioCodec();
        _decoder.clear();
        _frameCount = 0;
        _readCount = 0;
        _packetsDecoded = 0;
        _framesOut = 0;
        _capacityWarned = false;
        _closed = false;
        _outputQueue.clear();

        AudioCodec codec = cfg.getAs<AudioCodec>(MediaConfig::AudioCodec);
        if (codec.isValid()) {
                Error err = createDecoder(codec);
                if (err.isError()) return err;
        } else {
                promekiInfo("AudioDecoderMediaIO: no AudioCodec configured, "
                            "will auto-detect from first packet");
        }

        MediaDesc outDesc;
        outDesc.setFrameRate(cmd.pendingMediaDesc.frameRate());
        for (const auto &srcImg : cmd.pendingMediaDesc.imageList()) {
                outDesc.imageList().pushToBack(srcImg);
        }
        for (const auto &srcAudio : cmd.pendingMediaDesc.audioList()) {
                AudioDesc ad = srcAudio;
                if (_outputAudioDataTypeSet) {
                        ad = AudioDesc(AudioFormat(_outputAudioDataType), srcAudio.sampleRate(), srcAudio.channels());
                        ad.setChannelMap(srcAudio.channelMap());
                        ad.metadata() = srcAudio.metadata();
                }
                outDesc.audioList().pushToBack(ad);
        }

        MediaIOPortGroup *group = addPortGroup("adecoder");
        if (group == nullptr) {
                promekiWarn("AudioDecoderMediaIO: addPortGroup('adecoder') failed");
                return Error::Invalid;
        }
        group->setFrameRate(outDesc.frameRate());
        group->setCanSeek(false);
        group->setFrameCount(MediaIO::FrameCountInfinite);
        if (addSink(group, cmd.pendingMediaDesc) == nullptr) {
                promekiWarn("AudioDecoderMediaIO: addSink failed (fps=%s)",
                            cmd.pendingMediaDesc.frameRate().toString().cstr());
                return Error::Invalid;
        }
        if (addSource(group, outDesc) == nullptr) {
                promekiWarn("AudioDecoderMediaIO: addSource failed (fps=%s)",
                            outDesc.frameRate().toString().cstr());
                return Error::Invalid;
        }
        return Error::Ok;
}

Error AudioDecoderMediaIO::executeCmd(MediaIOCommandClose &cmd) {
        (void)cmd;
        if (_decoder.isValid()) {
                _decoder->flush();
                drainDecoderInto();
                _decoder.clear();
        }
        _config = MediaConfig();
        _codec = AudioCodec();
        _outputAudioDataType = AudioFormat::Invalid;
        _outputAudioDataTypeSet = false;
        _capacity = 0;
        _frameCount = 0;
        _readCount = 0;
        _packetsDecoded = 0;
        _framesOut = 0;
        _capacityWarned = false;
        _closed = true;
        return Error::Ok;
}

Error AudioDecoderMediaIO::executeCmd(MediaIOCommandWrite &cmd) {
        if (!cmd.frame.isValid()) {
                promekiErr("AudioDecoderMediaIO: write with null frame");
                return Error::InvalidArgument;
        }

        const Frame &frame = cmd.frame;

        // Resolve the compressed payload via the base helper for
        // codec auto-detection.  The decoder will re-select
        // internally on submitFrame.
        CompressedAudioPayload::Ptr probe = AudioDecoder::selectInputPayload(frame);
        if (!probe.isValid() || !probe->isValid()) {
                promekiErr("AudioDecoderMediaIO: write frame carries no "
                           "CompressedAudioPayload; upstream must emit a "
                           "compressed audio payload for every frame that "
                           "needs decoding");
                return Error::InvalidArgument;
        }

        if (_decoder.isNull()) {
                const AudioFormat &fmt = probe->desc().format();
                AudioCodec         codec = fmt.audioCodec();
                if (!codec.isValid()) {
                        promekiErr("AudioDecoderMediaIO: cannot resolve "
                                   "AudioCodec from AudioFormat '%s'",
                                   fmt.name().cstr());
                        return Error::NotSupported;
                }
                Error err = createDecoder(codec);
                if (err.isError()) {
                        return err;
                }
                promekiInfo("AudioDecoderMediaIO: auto-detected codec '%s' "
                            "from payload AudioFormat '%s'",
                            codec.name().cstr(), fmt.name().cstr());
        }

        if (static_cast<int>(_outputQueue.size()) >= _capacity && !_capacityWarned) {
                promekiWarn("AudioDecoderMediaIO: output queue exceeded capacity "
                            "(%d >= %d)",
                            static_cast<int>(_outputQueue.size()), _capacity);
                _capacityWarned = true;
        }

        Error err = _decoder->submitFrame(frame);
        if (err.isError()) {
                promekiErr("AudioDecoderMediaIO: submitFrame failed: %s",
                           _decoder->lastErrorMessage().cstr());
                return err;
        }
        _packetsDecoded++;
        drainDecoderInto();

        _frameCount++;
        cmd.currentFrame = toFrameNumber(_frameCount);
        cmd.frameCount = _frameCount;
        return Error::Ok;
}

void AudioDecoderMediaIO::drainDecoderInto() {
        if (_decoder.isNull()) return;
        while (true) {
                Frame outFrame = _decoder->receiveFrame();
                if (!outFrame.isValid()) break;
                _outputQueue.pushToBack(coerceOutputFormat(std::move(outFrame)));
                _framesOut++;
        }
}

Frame AudioDecoderMediaIO::coerceOutputFormat(Frame in) {
        // Honor the output-format contract.  We advertise
        // @c _outputAudioDataType in the source desc (negotiated with the
        // downstream sink), but a codec backend decodes to whatever PCM
        // format is native to it (e.g. fdk-aac always emits PCMI_S16LE).
        // Convert any mismatched PCM audio payload here so the frame we hand
        // downstream matches what we promised — strict sinks (AudioFile)
        // reject a format that differs from their configured one.
        if (!_outputAudioDataTypeSet) return in;

        // Replace mismatched PCM payloads in place so every other payload and
        // all of the frame's state (metadata, captureTime, configUpdate, ...)
        // survive untouched — rebuilding the Frame would silently narrow it to
        // whatever fields we remembered to copy.
        for (MediaPayload::Ptr &p : in.payloadList()) {
                if (!p.isValid()) continue;
                const auto *pcm = p->as<PcmAudioPayload>();
                if (pcm == nullptr || pcm->desc().format().id() == _outputAudioDataType) continue;
                PcmAudioPayload::Ptr conv = pcm->convert(AudioFormat(_outputAudioDataType));
                if (conv.isValid()) {
                        p = conv;
                        continue;
                }
                promekiWarnThrottled(1000,
                                     "AudioDecoderMediaIO: failed to convert decoded audio "
                                     "from '%s' to '%s'; forwarding as-is",
                                     pcm->desc().format().name().cstr(),
                                     AudioFormat(_outputAudioDataType).name().cstr());
        }
        return in;
}

Error AudioDecoderMediaIO::executeCmd(MediaIOCommandRead &cmd) {
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

Error AudioDecoderMediaIO::executeCmd(MediaIOCommandStats &cmd) {
        cmd.stats.set(StatsPacketsDecoded, _packetsDecoded);
        cmd.stats.set(StatsFramesOut, _framesOut);
        cmd.stats.set(MediaIOStats::QueueDepth, static_cast<int64_t>(_outputQueue.size()));
        cmd.stats.set(MediaIOStats::QueueCapacity, static_cast<int64_t>(_capacity));
        return Error::Ok;
}

int AudioDecoderMediaIO::pendingInternalWrites() const {
        return static_cast<int>(_outputQueue.size());
}

// ---- Phase 1 introspection / negotiation overrides ----

Error AudioDecoderMediaIO::describe(MediaIODescription *out) const {
        if (out == nullptr) return Error::Invalid;
        // Let the base populate identity / role flags / port snapshots.
        Error baseErr = MediaIO::describe(out);
        if (baseErr.isError()) return baseErr;

        // If the planner / caller has already pinned a codec via
        // config, advertise every compressed AudioFormat whose
        // identity maps back to that codec as an acceptable input
        // shape.
        if (_codec.isValid()) {
                for (AudioFormat::ID fid : AudioFormat::registeredIDs()) {
                        AudioFormat candidate(fid);
                        if (!candidate.isCompressed()) continue;
                        if (candidate.audioCodec() != _codec) continue;
                        MediaDesc accepted;
                        AudioDesc ad;
                        ad.setFormat(candidate);
                        accepted.audioList().pushToBack(ad);
                        out->acceptableFormats().pushToBack(accepted);
                }
        }
        if (_outputAudioDataTypeSet) {
                MediaDesc produced;
                AudioDesc ad;
                ad.setFormat(AudioFormat(_outputAudioDataType));
                produced.audioList().pushToBack(ad);
                out->producibleFormats().pushToBack(produced);
                out->setPreferredFormat(produced);
        }
        return Error::Ok;
}

Error AudioDecoderMediaIO::proposeInput(const MediaDesc &offered, MediaDesc *preferred) const {
        if (preferred == nullptr) return Error::Invalid;
        if (offered.audioList().isEmpty()) return Error::NotSupported;

        // AudioDecoder consumes compressed audio only.
        const AudioFormat &fmt = offered.audioList()[0].format();
        if (!fmt.isCompressed()) return Error::NotSupported;

        // If a codec has been pinned, the offered codec must match.
        if (_codec.isValid()) {
                const AudioCodec offeredCodec = fmt.audioCodec();
                if (offeredCodec != _codec) return Error::NotSupported;
        } else {
                // Codec auto-detect mode: any compressed AudioFormat
                // whose codec the registry can decode is acceptable.
                const AudioCodec offeredCodec = fmt.audioCodec();
                if (!offeredCodec.isValid() || !offeredCodec.canDecode()) {
                        return Error::NotSupported;
                }
        }
        *preferred = offered;
        return Error::Ok;
}

Error AudioDecoderMediaIO::proposeOutput(const MediaDesc &requested, MediaDesc *achievable,
                                         MediaConfig *configDelta) const {
        if (achievable == nullptr) return Error::Invalid;
        (void)configDelta;
        const MediaConfig &cfg = config();

        // Start from the input shape and apply any explicit
        // Output* overrides (OutputAudioDataType, OutputAudioRate, ...).
        MediaDesc proposed = MediaIO::applyOutputOverrides(requested, cfg);

        // When OutputAudioDataType is unset, the planner needs to
        // know that the *output* of this decoder is PCM — not the
        // compressed shape it was handed on the input.  The
        // authoritative answer is the registered decoder backend's
        // supported output list (the same fallback @ref createDecoder
        // uses at open time); when no codec / backend is registered
        // we leave the audio descriptor at whatever applyOutputOverrides
        // produced.
        // "Explicit" output means the caller set OutputAudioDataType
        // in this config to something other than Invalid; the spec
        // default (a default-constructed Enum bound to AudioDataType::
        // Type) is treated as "unset" because @ref MediaConfig::get
        // would resolve a missing key to the spec default and we
        // must not confuse that with an intentional override.
        bool hasExplicitOutput = false;
        if (cfg.contains(MediaConfig::OutputAudioDataType)) {
                Error      enumErr;
                const Enum adtEnum =
                        cfg.get(MediaConfig::OutputAudioDataType).asEnum(AudioDataType::Type, &enumErr);
                hasExplicitOutput = enumErr.isOk() && (adtEnum.value() != AudioFormat::Invalid);
        }
        if (!hasExplicitOutput && !requested.audioList().isEmpty()) {
                const AudioFormat &inFmt = requested.audioList()[0].format();
                if (inFmt.isCompressed()) {
                        AudioFormat      rawFmt;
                        const AudioCodec codec = inFmt.audioCodec();
                        if (codec.isValid() && codec.canDecode()) {
                                const auto supported = codec.decoderSupportedOutputs();
                                if (!supported.isEmpty()) rawFmt = supported.front();
                        }
                        if (rawFmt.isValid()) {
                                AudioDesc::List &auds = proposed.audioList();
                                for (size_t i = 0; i < auds.size(); ++i) {
                                        auds[i].setFormat(rawFmt);
                                }
                        }
                }
        }

        *achievable = proposed;
        return Error::Ok;
}

PROMEKI_NAMESPACE_END
