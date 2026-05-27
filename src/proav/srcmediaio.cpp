/**
 * @file      srcmediaio.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/srcmediaio.h>
#include <promeki/mediaioportgroup.h>
#include <promeki/enums_audio.h>
#include <promeki/enums_mediaio.h>
#include <promeki/frame.h>
#include <promeki/mediapayload.h>
#include <promeki/videopayload.h>
#include <promeki/audiopayload.h>
#include <promeki/pcmaudiopayload.h>
#include <promeki/mediadesc.h>
#include <promeki/mediaconfig.h>
#include <promeki/mediaiodescription.h>
#include <promeki/metadata.h>
#include <promeki/logger.h>
#include <promeki/mediaiorequest.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_REGISTER_MEDIAIO_FACTORY(SrcFactory)

namespace {

        // Bridge: SRC bridges audio sample-format gaps (DataType) only.
        // Sample-rate changes are FrameSync's territory.  Video tracks must
        // already match.
        bool srcBridge(const MediaDesc &from, const MediaDesc &to, MediaIO::Config *outConfig, int *outCost) {
                // Pixel side must match (SRC is audio-only).
                if (!from.imageList().isEmpty() && !to.imageList().isEmpty()) {
                        if (from.imageList()[0].pixelFormat() != to.imageList()[0].pixelFormat()) return false;
                }
                if (from.audioList().isEmpty() || to.audioList().isEmpty()) return false;

                const AudioDesc &a = from.audioList()[0];
                const AudioDesc &b = to.audioList()[0];

                // SRC only converts the sample data type today; rate /
                // channel changes are handled by FrameSync, and the
                // compressed-codec axis is handled by AudioEncoder /
                // AudioDecoder.
                if (a.sampleRate() != b.sampleRate()) return false;
                if (a.channels() != b.channels()) return false;
                if (a.format().id() == b.format().id()) return false;
                if (b.format().id() == AudioFormat::Invalid) return false;
                if (a.format().isCompressed() || b.format().isCompressed()) return false;

                if (outConfig != nullptr) {
                        *outConfig = MediaIOFactory::defaultConfig("SRC");
                        outConfig->set(MediaConfig::OutputAudioDataType, AudioDataType(b.format().id()));
                }
                if (outCost != nullptr) {
                        // Sample-format conversion is precision-preserving
                        // when the target has equal-or-greater bit depth;
                        // otherwise expect quantisation noise.  Single
                        // bounded-error band for all cases.
                        *outCost = 60;
                }
                return true;
        }

} // namespace

MediaIOFactory::Config::SpecMap SrcFactory::configSpecs() const {
        Config::SpecMap specs;
        auto            s = [&specs](MediaConfig::ID id, const Variant &def) {
                const VariantSpec *gs = MediaConfig::spec(id);
                specs.insert(id, gs ? VariantSpec(*gs).setDefault(def) : VariantSpec().setDefault(def));
        };
        s(MediaConfig::OutputAudioDataType, AudioDataType::Invalid);
        s(MediaConfig::Capacity, int32_t(4));
        return specs;
}

bool SrcFactory::bridge(const MediaDesc &from, const MediaDesc &to, Config *outConfig, int *outCost) const {
        return srcBridge(from, to, outConfig, outCost);
}

MediaIO *SrcFactory::create(const Config &config, ObjectBase *parent) const {
        auto *io = new SrcMediaIO(parent);
        io->setConfig(config);
        return io;
}

SrcMediaIO::SrcMediaIO(ObjectBase *parent) : SharedThreadMediaIO(parent) {}

SrcMediaIO::~SrcMediaIO() {
        if (isOpen()) (void)close().wait();
}

Error SrcMediaIO::executeCmd(MediaIOCommandOpen &cmd) {
        const MediaIO::Config &cfg = cmd.config;

        Error adtErr;
        Enum  adtEnum = cfg.get(MediaConfig::OutputAudioDataType).asEnum(AudioDataType::Type, &adtErr);
        if (adtErr.isError()) {
                promekiErr("SrcMediaIO: unknown audio data type");
                return Error::InvalidArgument;
        }
        _outputAudioDataType = static_cast<AudioFormat::ID>(adtEnum.value());
        _outputAudioDataTypeSet = (_outputAudioDataType != AudioFormat::Invalid);

        _capacity = cfg.getAs<int>(MediaConfig::Capacity, 4);
        if (_capacity < 1) _capacity = 1;

        MediaDesc outDesc;
        outDesc.setFrameRate(cmd.pendingMediaDesc.frameRate());

        for (const auto &srcImg : cmd.pendingMediaDesc.imageList()) {
                outDesc.imageList().pushToBack(srcImg);
        }

        for (const auto &srcAudio : cmd.pendingMediaDesc.audioList()) {
                AudioDesc ad = srcAudio;
                if (_outputAudioDataTypeSet) {
                        ad = AudioDesc(_outputAudioDataType, srcAudio.sampleRate(), srcAudio.channels());
                        ad.metadata() = srcAudio.metadata();
                }
                outDesc.audioList().pushToBack(ad);
        }

        _frameCount = 0;
        _readCount = 0;
        _framesConverted = 0;
        _outputQueue.clear();

        MediaIOPortGroup *group = addPortGroup("src");
        if (group == nullptr) {
                promekiWarn("SrcMediaIO: addPortGroup('src') failed");
                return Error::Invalid;
        }
        group->setFrameRate(outDesc.frameRate());
        group->setCanSeek(false);
        group->setFrameCount(MediaIO::FrameCountInfinite);
        if (addSink(group, cmd.pendingMediaDesc) == nullptr) {
                promekiWarn("SrcMediaIO: addSink failed (fps=%s)",
                            cmd.pendingMediaDesc.frameRate().toString().cstr());
                return Error::Invalid;
        }
        if (addSource(group, outDesc) == nullptr) {
                promekiWarn("SrcMediaIO: addSource failed (fps=%s)",
                            outDesc.frameRate().toString().cstr());
                return Error::Invalid;
        }
        return Error::Ok;
}

Error SrcMediaIO::executeCmd(MediaIOCommandClose &cmd) {
        (void)cmd;
        _outputQueue.clear();
        _outputAudioDataType = AudioFormat::Invalid;
        _outputAudioDataTypeSet = false;
        _frameCount = 0;
        _readCount = 0;
        _framesConverted = 0;
        _capacityWarned = false;
        return Error::Ok;
}

Error SrcMediaIO::convertFrame(const Frame &input, Frame &output) {
        if (!input.isValid()) {
                return Error::Invalid;
        }

        // CoW copy: preserves metadata, captureTime, configUpdate, and
        // every non-audio payload (video, ANC) verbatim.  We then walk
        // the payload list and replace each PCM audio slot with its
        // converted form — CoW detaches the payload list before the
        // assignment, so the upstream Frame is not mutated.  Keeping
        // ANC payloads shared with the upstream Frame is what lets
        // the downstream caption-emitting encoders still see the
        // source ANC packets when an SRC bridge is spliced into the
        // pipeline.
        Frame                  outFrame = input;
        MediaPayload::PtrList &plist = outFrame.payloadList();
        for (size_t i = 0; i < plist.size(); ++i) {
                MediaPayload::Ptr &slot = plist[i];
                if (!slot.isValid()) continue;
                if (slot->kind() != MediaPayloadKind::Audio) continue;
                const auto *srcUap = slot->as<PcmAudioPayload>();
                if (srcUap == nullptr) {
                        promekiErr("SrcMediaIO: compressed audio payload "
                                   "reached SRC — expected uncompressed PCM");
                        return Error::NotSupported;
                }

                PcmAudioPayload::Ptr dstPayload;
                if (_outputAudioDataTypeSet && srcUap->desc().format().id() != _outputAudioDataType) {
                        dstPayload = srcUap->convert(AudioFormat(_outputAudioDataType));
                        if (!dstPayload.isValid()) {
                                promekiErr("SrcMediaIO: audio convertTo failed");
                                return Error::ConversionFailed;
                        }
                } else {
                        dstPayload = PcmAudioPayload::Ptr::create(*srcUap);
                }
                slot = MediaPayload::Ptr(dstPayload);
        }

        output = std::move(outFrame);
        return Error::Ok;
}

Error SrcMediaIO::executeCmd(MediaIOCommandWrite &cmd) {
        if (!cmd.frame.isValid()) {
                promekiErr("SrcMediaIO: write with null frame");
                return Error::InvalidArgument;
        }

        if (static_cast<int>(_outputQueue.size()) >= _capacity && !_capacityWarned) {
                promekiWarn("SrcMediaIO: output queue exceeded capacity (%d >= %d)",
                            static_cast<int>(_outputQueue.size()), _capacity);
                _capacityWarned = true;
        }

        Frame outFrame;
        Error      err = convertFrame(cmd.frame, outFrame);
        if (err.isError()) {
                return err;
        }

        _outputQueue.pushToBack(std::move(outFrame));
        _frameCount++;
        _framesConverted++;
        cmd.currentFrame = toFrameNumber(_frameCount);
        cmd.frameCount = _frameCount;
        return Error::Ok;
}

Error SrcMediaIO::executeCmd(MediaIOCommandRead &cmd) {
        if (_outputQueue.isEmpty()) {
                return Error::TryAgain;
        }

        Frame frame = std::move(_outputQueue.front());
        _outputQueue.remove(0);
        _readCount++;
        cmd.frame = std::move(frame);
        cmd.currentFrame = _readCount;
        return Error::Ok;
}

Error SrcMediaIO::executeCmd(MediaIOCommandStats &cmd) {
        cmd.stats.set(StatsFramesConverted, _framesConverted);
        cmd.stats.set(MediaIOStats::QueueDepth, static_cast<int64_t>(_outputQueue.size()));
        cmd.stats.set(MediaIOStats::QueueCapacity, static_cast<int64_t>(_capacity));
        return Error::Ok;
}

int SrcMediaIO::pendingInternalWrites() const {
        return static_cast<int>(_outputQueue.size());
}

// ---- Phase 1 introspection / negotiation overrides ----

Error SrcMediaIO::describe(MediaIODescription *out) const {
        if (out == nullptr) return Error::Invalid;
        // Let the base populate identity / role flags / port snapshots.
        // SRC accepts any uncompressed audio MediaDesc and produces
        // the configured output sample format (or pass-through).
        return MediaIO::describe(out);
}

Error SrcMediaIO::proposeInput(const MediaDesc &offered, MediaDesc *preferred) const {
        if (preferred == nullptr) return Error::Invalid;
        // SRC has no opinion on the input shape other than wanting
        // uncompressed PCM audio.  Compressed audio belongs to a
        // future audio-decoder stage.
        for (const auto &ad : offered.audioList()) {
                if (!ad.format().isValid() || ad.isCompressed()) {
                        return Error::NotSupported;
                }
        }
        *preferred = offered;
        return Error::Ok;
}

Error SrcMediaIO::proposeOutput(const MediaDesc &requested, MediaDesc *achievable, MediaConfig *configDelta) const {
        if (achievable == nullptr) return Error::Invalid;
        (void)configDelta;
        *achievable = MediaIO::applyOutputOverrides(requested, config());
        return Error::Ok;
}

PROMEKI_NAMESPACE_END
