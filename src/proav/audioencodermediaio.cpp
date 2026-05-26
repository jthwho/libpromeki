/**
 * @file      audioencodermediaio.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 */

#include <climits>
#include <promeki/audioencodermediaio.h>
#include <promeki/mediaioportgroup.h>
#include <promeki/mediaconfig.h>
#include <promeki/enums_audio.h>
#include <promeki/frame.h>
#include <promeki/framerate.h>
#include <promeki/audiodesc.h>
#include <promeki/mediadesc.h>
#include <promeki/mediaiodescription.h>
#include <promeki/compressedaudiopayload.h>
#include <promeki/pcmaudiopayload.h>
#include <promeki/videopayload.h>
#include <promeki/metadata.h>
#include <promeki/audioformat.h>
#include <promeki/buffer.h>
#include <promeki/logger.h>
#include <promeki/mediatimestamp.h>
#include <promeki/mediaiorequest.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_DEBUG(AudioEncoderMediaIO)

PROMEKI_REGISTER_MEDIAIO_FACTORY(AudioEncoderFactory)

namespace {

        // Bridge: AudioEncoder bridges PCM → compressed.  The planner
        // inserts it whenever a downstream sink demands a compressed
        // AudioFormat that an upstream PCM source can supply.  Video
        // tracks must already match — when both audio and video need
        // to change the planner chains an audio bridge in front of a
        // video bridge (or vice-versa) instead.
        bool audioEncoderBridge(const MediaDesc &from, const MediaDesc &to, MediaIO::Config *outConfig, int *outCost) {
                if (from.audioList().isEmpty() || to.audioList().isEmpty()) return false;
                const AudioFormat &fromFmt = from.audioList()[0].format();
                const AudioFormat &toFmt = to.audioList()[0].format();
                if (!fromFmt.isValid() || !toFmt.isValid()) return false;
                if (fromFmt.isCompressed()) return false;
                if (!toFmt.isCompressed()) return false;

                // Pixel side must match: the encoder forwards every
                // video payload as-is, so if the downstream sink wants
                // a different pixel shape too the planner needs a
                // separate video bridge.
                if (!from.imageList().isEmpty() && !to.imageList().isEmpty()) {
                        if (from.imageList()[0].pixelFormat() != to.imageList()[0].pixelFormat()) return false;
                }

                const AudioCodec codec = toFmt.audioCodec();
                if (!codec.isValid()) return false;
                if (!codec.canEncode()) return false;

                if (outConfig != nullptr) {
                        *outConfig = MediaIOFactory::defaultConfig("AudioEncoder");
                        outConfig->set(MediaConfig::AudioCodec, codec);
                }
                if (outCost != nullptr) {
                        // Audio encoding is lossy for the codecs in
                        // scope (Opus, AAC, MP3, AC-3).  Sit in the
                        // "heavily lossy" band so the planner avoids
                        // it unless the sink really needs the
                        // compressed form.
                        *outCost = 5000;
                }
                return true;
        }

} // namespace

MediaIOFactory::Config::SpecMap AudioEncoderFactory::configSpecs() const {
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
        s(MediaConfig::AudioCodec);
        s(MediaConfig::CodecBackend);
        s(MediaConfig::AudioRate);
        s(MediaConfig::AudioChannels);
        sWithDefault(MediaConfig::BitrateKbps, int32_t(128));
        s(MediaConfig::OpusApplication);
        s(MediaConfig::OpusFrameSizeMs);
        sWithDefault(MediaConfig::Capacity, int32_t(8));
        return specs;
}

bool AudioEncoderFactory::bridge(const MediaDesc &from, const MediaDesc &to, Config *outConfig, int *outCost) const {
        return audioEncoderBridge(from, to, outConfig, outCost);
}

MediaIO *AudioEncoderFactory::create(const Config &config, ObjectBase *parent) const {
        auto *io = new AudioEncoderMediaIO(parent);
        io->setConfig(config);
        return io;
}

AudioEncoderMediaIO::AudioEncoderMediaIO(ObjectBase *parent) : SharedThreadMediaIO(parent) {}

AudioEncoderMediaIO::~AudioEncoderMediaIO() {
        if (isOpen()) (void)close().wait();
}

Error AudioEncoderMediaIO::executeCmd(MediaIOCommandOpen &cmd) {
        const MediaIO::Config &cfg = cmd.config;
        _config = cfg;

        _codec = cfg.getAs<AudioCodec>(MediaConfig::AudioCodec);
        if (!_codec.isValid()) {
                promekiErr("AudioEncoderMediaIO: AudioCodec is required "
                           "(e.g. \"Opus\", \"AAC\")");
                return Error::InvalidArgument;
        }
        if (!_codec.canEncode()) {
                promekiErr("AudioEncoderMediaIO: codec '%s' has no "
                           "registered encoder factory",
                           _codec.name().cstr());
                return Error::NotSupported;
        }

        // Typed factory lookup; the registered AudioEncoder subclass
        // owns any codec-specific state (libopus session, FDK-AAC
        // handle, …).  Forward the open config so the encoder's
        // configure() runs with the same key set the user authored.
        MediaIO::Config encCfg = cfg;
        auto            encResult = _codec.createEncoder(&encCfg);
        if (error(encResult).isError()) {
                promekiErr("AudioEncoderMediaIO: createEncoder('%s') failed: %s", _codec.name().cstr(),
                           error(encResult).name().cstr());
                return Error::NotSupported;
        }
        AudioEncoder::UPtr enc = AudioEncoder::UPtr::takeOwnership(value(encResult));

        _capacity = cfg.getAs<int>(MediaConfig::Capacity, 8);
        if (_capacity < 1) _capacity = 1;

        // Build the downstream-visible MediaDesc: each source audio
        // track is replaced by one at the encoder's compressed
        // AudioFormat so the next stage picks up the right format
        // before the first frame arrives.
        MediaDesc outDesc;
        outDesc.setFrameRate(cmd.pendingMediaDesc.frameRate());

        // Derive the encoder's output AudioFormat from the codec
        // metadata — every codec spec exposes at least one
        // compressed AudioFormat (Opus, AAC, …) whose
        // @c AudioFormat::audioCodec() reverses back to the
        // encoder's codec identity.
        AudioFormat encOutFmt;
        for (AudioFormat::ID fid : AudioFormat::registeredIDs()) {
                AudioFormat candidate(fid);
                if (!candidate.isCompressed()) continue;
                if (candidate.audioCodec() == _codec) {
                        encOutFmt = candidate;
                        break;
                }
        }
        for (const auto &srcImg : cmd.pendingMediaDesc.imageList()) {
                outDesc.imageList().pushToBack(srcImg);
        }
        for (const auto &srcAudio : cmd.pendingMediaDesc.audioList()) {
                AudioDesc ad = srcAudio;
                if (encOutFmt.isValid()) {
                        ad = AudioDesc(encOutFmt, srcAudio.sampleRate(), srcAudio.channels());
                        ad.setChannelMap(srcAudio.channelMap());
                        ad.metadata() = srcAudio.metadata();
                }
                outDesc.audioList().pushToBack(ad);
        }

        _encoder = std::move(enc);
        _frameCount = 0;
        _readCount = 0;
        _framesEncoded = 0;
        _packetsOut = 0;
        _capacityWarned = false;
        _closed = false;
        _outputQueue.clear();

        MediaIOPortGroup *group = addPortGroup("aencoder");
        if (group == nullptr) {
                promekiWarn("AudioEncoderMediaIO: addPortGroup('aencoder') failed");
                return Error::Invalid;
        }
        group->setFrameRate(outDesc.frameRate());
        group->setCanSeek(false);
        group->setFrameCount(MediaIO::FrameCountInfinite);
        if (addSink(group, cmd.pendingMediaDesc) == nullptr) {
                promekiWarn("AudioEncoderMediaIO: addSink failed (fps=%s)",
                            cmd.pendingMediaDesc.frameRate().toString().cstr());
                return Error::Invalid;
        }
        if (addSource(group, outDesc) == nullptr) {
                promekiWarn("AudioEncoderMediaIO: addSource failed (fps=%s)",
                            outDesc.frameRate().toString().cstr());
                return Error::Invalid;
        }
        return Error::Ok;
}

Error AudioEncoderMediaIO::executeCmd(MediaIOCommandClose &cmd) {
        (void)cmd;
        if (_encoder.isValid()) {
                // Best-effort flush so anything the encoder has
                // buffered (sub-frame samples awaiting a full packet
                // boundary) makes it out before we tear the session
                // down.  Any packets emitted here are stored in
                // _outputQueue for a subsequent read.
                _encoder->flush();
                drainEncoderInto();
                _encoder.clear();
        }
        _config = MediaConfig();
        _codec = AudioCodec();
        _capacity = 0;
        _frameCount = 0;
        _readCount = 0;
        _framesEncoded = 0;
        _packetsOut = 0;
        _capacityWarned = false;
        _closed = true;
        return Error::Ok;
}

void AudioEncoderMediaIO::configChanged(const MediaConfig &delta) {
        _config.merge(delta);
        if (_encoder.isValid()) _encoder->configure(_config);
}

Error AudioEncoderMediaIO::executeCmd(MediaIOCommandWrite &cmd) {
        if (!cmd.frame.isValid()) {
                promekiErr("AudioEncoderMediaIO: write with null frame");
                return Error::InvalidArgument;
        }
        if (_encoder.isNull()) {
                return Error::NotSupported;
        }

        if (static_cast<int>(_outputQueue.size()) >= _capacity && !_capacityWarned) {
                promekiWarn("AudioEncoderMediaIO: output queue exceeded capacity "
                            "(%d >= %d) — downstream is not draining packets fast enough",
                            static_cast<int>(_outputQueue.size()), _capacity);
                _capacityWarned = true;
        }

        const Frame         &frame = cmd.frame;
        PcmAudioPayload::Ptr srcPayload = AudioEncoder::selectInputPayload(frame);

        if (!srcPayload.isValid()) {
                // No audio to encode — let the frame pass through so
                // video / metadata-only inputs aren't lost.  Build the
                // pass-through using the base helper without a
                // compressed payload.
                _outputQueue.pushToBack(AudioEncoder::buildOutputFrame(frame, CompressedAudioPayload::Ptr()));
                _frameCount++;
                cmd.currentFrame = toFrameNumber(_frameCount);
                cmd.frameCount = _frameCount;
                return Error::Ok;
        }

        Error err = _encoder->submitFrame(frame);
        if (err.isError()) {
                promekiErr("AudioEncoderMediaIO: submitFrame failed: %s", _encoder->lastErrorMessage().cstr());
                return err;
        }
        _frameCount++;
        _framesEncoded++;
        drainEncoderInto();

        cmd.currentFrame = toFrameNumber(_frameCount);
        cmd.frameCount = _frameCount;
        return Error::Ok;
}

void AudioEncoderMediaIO::drainEncoderInto() {
        if (_encoder.isNull()) return;
        while (true) {
                Frame outFrame = _encoder->receiveFrame();
                if (!outFrame.isValid()) break;

                // EOS is an encoder-internal signal that the session
                // is drained; no need to propagate as its own Frame
                // (the pipeline uses the MediaIO close/EOF path for
                // that).  Inspect the frame's compressed audio
                // payloads for the marker and drop the frame if
                // present.
                bool eos = false;
                for (const AudioPayload::Ptr &ap : outFrame.audioPayloads()) {
                        if (!ap.isValid()) continue;
                        CompressedAudioPayload::Ptr cap = sharedPointerCast<CompressedAudioPayload>(ap);
                        if (cap.isValid() && cap->isEndOfStream()) {
                                eos = true;
                                break;
                        }
                }
                if (eos) continue;

                _outputQueue.pushToBack(std::move(outFrame));
                _packetsOut++;
        }
}

Error AudioEncoderMediaIO::executeCmd(MediaIOCommandRead &cmd) {
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

Error AudioEncoderMediaIO::executeCmd(MediaIOCommandStats &cmd) {
        cmd.stats.set(StatsFramesEncoded, _framesEncoded);
        cmd.stats.set(StatsPacketsOut, _packetsOut);
        cmd.stats.set(MediaIOStats::QueueDepth, static_cast<int64_t>(_outputQueue.size()));
        cmd.stats.set(MediaIOStats::QueueCapacity, static_cast<int64_t>(_capacity));
        return Error::Ok;
}

int AudioEncoderMediaIO::pendingInternalWrites() const {
        return static_cast<int>(_outputQueue.size());
}

// ---- Phase 1 introspection / negotiation overrides ----

Error AudioEncoderMediaIO::describe(MediaIODescription *out) const {
        if (out == nullptr) return Error::Invalid;
        // Let the base populate identity / role flags / port snapshots.
        Error baseErr = MediaIO::describe(out);
        if (baseErr.isError()) return baseErr;

        // When a codec has been pinned via config, advertise its
        // compressed AudioFormat as the producible shape so the
        // planner can match it without instantiating a session.
        if (_codec.isValid()) {
                for (AudioFormat::ID fid : AudioFormat::registeredIDs()) {
                        AudioFormat candidate(fid);
                        if (!candidate.isCompressed()) continue;
                        if (candidate.audioCodec() != _codec) continue;
                        MediaDesc produced;
                        AudioDesc ad;
                        ad.setFormat(candidate);
                        produced.audioList().pushToBack(ad);
                        out->producibleFormats().pushToBack(produced);
                }
                if (!out->producibleFormats().isEmpty()) {
                        out->setPreferredFormat(out->producibleFormats()[0]);
                }
        }
        return Error::Ok;
}

Error AudioEncoderMediaIO::proposeInput(const MediaDesc &offered, MediaDesc *preferred) const {
        if (preferred == nullptr) return Error::Invalid;
        if (offered.audioList().isEmpty()) return Error::NotSupported;

        // AudioEncoder consumes uncompressed PCM only.
        const AudioFormat &fmt = offered.audioList()[0].format();
        if (!fmt.isValid()) return Error::NotSupported;
        if (fmt.isCompressed()) return Error::NotSupported;

        // Resolve the codec — either pinned by an earlier open(), or
        // read straight off the stage config during planning (the
        // planner constructs the stage via MediaIO::create but does
        // not open it, so _codec is still invalid at this point).
        AudioCodec codec = _codec;
        if (!codec.isValid()) {
                codec = config().getAs<AudioCodec>(MediaConfig::AudioCodec);
        }

        // Without a codec we can't know which inputs the concrete
        // encoder will accept.  Fall back to passthrough; the
        // planner will fail later at open time rather than insert a
        // bogus bridge now.
        if (!codec.isValid() || !codec.canEncode()) {
                *preferred = offered;
                return Error::Ok;
        }

        // Query the backend's supported input list.  If a session is
        // already live we read it off the pinned codec on the encoder;
        // otherwise we pull the union list from the codec registry so
        // the planner never has to instantiate an encoder just to
        // introspect supported inputs.
        List<AudioFormat> supported;
        if (_encoder.isValid()) {
                supported = _encoder->codec().encoderSupportedInputs();
        } else {
                supported = codec.encoderSupportedInputs();
        }

        // Empty list means "accepts any PCM input the encoder can
        // convert internally" per the AudioEncoder::supportedInputs
        // contract.
        if (supported.isEmpty()) {
                *preferred = offered;
                return Error::Ok;
        }

        // Offered format already on the supported list — no
        // negotiation needed.
        for (const AudioFormat &cand : supported) {
                if (cand == fmt) {
                        *preferred = offered;
                        return Error::Ok;
                }
        }

        // Pick the best-matching supported PCM format to advertise so
        // the planner splices in an SRC.  Preference order:
        //   1. Same bit-depth and signed-ness as source.
        //   2. Same bit-depth (any signed-ness).
        //   3. First supported entry.
        const int offeredBits = static_cast<int>(fmt.bitsPerSample());
        const bool offeredSigned = fmt.isSigned();
        AudioFormat bestPick = supported[0];
        int         bestTier = 3;
        int         bestBitsDelta = INT_MAX;
        for (const AudioFormat &cand : supported) {
                if (!cand.isValid()) continue;
                const int  candBits = static_cast<int>(cand.bitsPerSample());
                const bool sameBits = (offeredBits > 0 && candBits == offeredBits);
                const bool sameSigned = (cand.isSigned() == offeredSigned);

                int tier = 3;
                if (sameBits && sameSigned)
                        tier = 1;
                else if (sameBits)
                        tier = 2;

                const int bitsDelta = (offeredBits > 0 && candBits > 0) ? std::abs(candBits - offeredBits) : INT_MAX;

                if (tier < bestTier || (tier == bestTier && bitsDelta < bestBitsDelta)) {
                        bestTier = tier;
                        bestPick = cand;
                        bestBitsDelta = bitsDelta;
                }
        }

        MediaDesc        out = offered;
        AudioDesc::List &auds = out.audioList();
        for (size_t i = 0; i < auds.size(); ++i) {
                auds[i].setFormat(bestPick);
        }
        *preferred = out;
        return Error::Ok;
}

Error AudioEncoderMediaIO::proposeOutput(const MediaDesc &requested, MediaDesc *achievable,
                                         MediaConfig *configDelta) const {
        if (achievable == nullptr) return Error::Invalid;
        (void)configDelta;

        // Resolve the codec from a live session when open, or from
        // the stage config during planning.
        AudioCodec codec = _codec;
        if (!codec.isValid()) {
                codec = config().getAs<AudioCodec>(MediaConfig::AudioCodec);
        }
        if (!codec.isValid()) {
                // Codec not yet known — defer; the planner has to
                // pin it before the encoder can answer.
                return Error::NotSupported;
        }

        // Locate the compressed AudioFormat whose codec identity
        // reverses to the configured codec — every well-known audio
        // codec has exactly one such format (Opus → AudioFormat::Opus,
        // AAC → AudioFormat::AAC, …).
        AudioFormat compressed;
        for (AudioFormat::ID fid : AudioFormat::registeredIDs()) {
                AudioFormat candidate(fid);
                if (!candidate.isCompressed()) continue;
                if (candidate.audioCodec() == codec) {
                        compressed = candidate;
                        break;
                }
        }
        if (!compressed.isValid()) return Error::NotSupported;

        // The encoder produces a compressed AudioFormat whose codec
        // matches the configured AudioCodec.  Start from the
        // requested input shape (rate / channels flow through),
        // then replace the format on every audio track with the
        // codec's compressed form.
        MediaDesc        out = requested;
        AudioDesc::List &auds = out.audioList();
        for (size_t i = 0; i < auds.size(); ++i) {
                auds[i].setFormat(compressed);
        }
        *achievable = out;
        return Error::Ok;
}

PROMEKI_NAMESPACE_END
