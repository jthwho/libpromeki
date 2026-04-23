/**
 * @file      mediaiotask_src.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/mediaiotask_src.h>
#include <promeki/enums.h>
#include <promeki/audio.h>
#include <promeki/frame.h>
#include <promeki/mediadesc.h>
#include <promeki/mediaconfig.h>
#include <promeki/mediaiodescription.h>
#include <promeki/metadata.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_REGISTER_MEDIAIO(MediaIOTask_SRC)

namespace {

// Bridge: SRC bridges audio sample-format gaps (DataType) only.
// Sample-rate changes are FrameSync's territory.  Video tracks must
// already match.
bool srcBridge(const MediaDesc &from,
               const MediaDesc &to,
               MediaIO::Config *outConfig,
               int *outCost) {
        // Pixel side must match (SRC is audio-only).
        if(!from.imageList().isEmpty() && !to.imageList().isEmpty()) {
                if(from.imageList()[0].pixelFormat() !=
                   to.imageList()[0].pixelFormat()) return false;
        }
        if(from.audioList().isEmpty() || to.audioList().isEmpty()) return false;

        const AudioDesc &a = from.audioList()[0];
        const AudioDesc &b = to.audioList()[0];

        // SRC only converts the sample data type today; rate /
        // channel changes are handled by FrameSync.
        if(a.sampleRate() != b.sampleRate()) return false;
        if(a.channels()   != b.channels())   return false;
        if(a.format().id()   == b.format().id())   return false;
        if(b.format().id()   == AudioFormat::Invalid) return false;

        if(outConfig != nullptr) {
                *outConfig = MediaIO::defaultConfig("SRC");
                outConfig->set(MediaConfig::OutputAudioDataType,
                               AudioDataType(b.format().id()));
        }
        if(outCost != nullptr) {
                // Sample-format conversion is precision-preserving
                // when the target has equal-or-greater bit depth;
                // otherwise expect quantisation noise.  Single
                // bounded-error band for all cases.
                *outCost = 60;
        }
        return true;
}

} // namespace

MediaIO::FormatDesc MediaIOTask_SRC::formatDesc() {
        MediaIO::FormatDesc d;
        d.name              = "SRC";
        d.description       = "Audio sample format converter";
        d.canBeSource       = false;
        d.canBeSink         = false;
        d.canBeTransform    = true;
        d.create            = []() -> MediaIOTask * { return new MediaIOTask_SRC(); };
        d.configSpecs       = []() -> MediaIO::Config::SpecMap {
                MediaIO::Config::SpecMap specs;
                auto s = [&specs](MediaConfig::ID id, const Variant &def) {
                        const VariantSpec *gs = MediaConfig::spec(id);
                        specs.insert(id, gs ? VariantSpec(*gs).setDefault(def)
                                            : VariantSpec().setDefault(def));
                };
                s(MediaConfig::OutputAudioDataType, AudioDataType::Invalid);
                s(MediaConfig::Capacity, int32_t(4));
                return specs;
        };
        d.bridge            = srcBridge;
        return d;
}

MediaIOTask_SRC::~MediaIOTask_SRC() = default;

Error MediaIOTask_SRC::executeCmd(MediaIOCommandOpen &cmd) {
        if(cmd.mode != MediaIO::Transform) {
                promekiErr("MediaIOTask_SRC: only InputAndOutput mode is supported");
                return Error::NotSupported;
        }

        const MediaIO::Config &cfg = cmd.config;

        Error adtErr;
        Enum adtEnum = cfg.get(MediaConfig::OutputAudioDataType).asEnum(AudioDataType::Type, &adtErr);
        if(adtErr.isError()) {
                promekiErr("MediaIOTask_SRC: unknown audio data type");
                return Error::InvalidArgument;
        }
        _outputAudioDataType = static_cast<AudioFormat::ID>(adtEnum.value());
        _outputAudioDataTypeSet = (_outputAudioDataType != AudioFormat::Invalid);

        _capacity = cfg.getAs<int>(MediaConfig::Capacity, 4);
        if(_capacity < 1) _capacity = 1;

        MediaDesc outDesc;
        outDesc.setFrameRate(cmd.pendingMediaDesc.frameRate());

        for(const auto &srcImg : cmd.pendingMediaDesc.imageList()) {
                outDesc.imageList().pushToBack(srcImg);
        }

        for(const auto &srcAudio : cmd.pendingMediaDesc.audioList()) {
                AudioDesc ad = srcAudio;
                if(_outputAudioDataTypeSet) {
                        ad = AudioDesc(_outputAudioDataType,
                                       srcAudio.sampleRate(),
                                       srcAudio.channels());
                        ad.metadata() = srcAudio.metadata();
                }
                outDesc.audioList().pushToBack(ad);
        }

        _frameCount = 0;
        _readCount = 0;
        _framesConverted = 0;
        _outputQueue.clear();

        cmd.mediaDesc = outDesc;
        if(!outDesc.audioList().isEmpty()) cmd.audioDesc = outDesc.audioList()[0];
        cmd.metadata = cmd.pendingMetadata;
        cmd.frameRate = outDesc.frameRate();
        cmd.canSeek = false;
        cmd.frameCount = MediaIO::FrameCountInfinite;
        cmd.defaultStep = 1;
        cmd.defaultPrefetchDepth = 1;
        cmd.defaultWriteDepth = _capacity;
        return Error::Ok;
}

Error MediaIOTask_SRC::executeCmd(MediaIOCommandClose &cmd) {
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

Error MediaIOTask_SRC::convertFrame(const Frame::Ptr &input, Frame::Ptr &output) {
        if(!input.isValid()) {
                return Error::Invalid;
        }

        Frame::Ptr outFrame = Frame::Ptr::create();
        Frame *outRaw = outFrame.modify();
        outRaw->metadata() = input->metadata();

        for(const auto &srcImgPtr : input->imageList()) {
                outRaw->imageList().pushToBack(srcImgPtr);
        }

        for(const auto &srcAudioPtr : input->audioList()) {
                if(!srcAudioPtr.isValid()) continue;
                const Audio &srcAudio = *srcAudioPtr;

                Audio dstAudio;
                if(_outputAudioDataTypeSet &&
                   srcAudio.desc().format().id() != _outputAudioDataType) {
                        dstAudio = srcAudio.convert(_outputAudioDataType);
                        if(!dstAudio.isValid()) {
                                promekiErr("MediaIOTask_SRC: audio convertTo failed");
                                return Error::ConversionFailed;
                        }
                } else {
                        dstAudio = srcAudio;
                }
                outRaw->audioList().pushToBack(Audio::Ptr::create(std::move(dstAudio)));
        }

        output = std::move(outFrame);
        return Error::Ok;
}

Error MediaIOTask_SRC::executeCmd(MediaIOCommandWrite &cmd) {
        if(!cmd.frame.isValid()) {
                promekiErr("MediaIOTask_SRC: write with null frame");
                return Error::InvalidArgument;
        }
        stampWorkBegin();

        if(static_cast<int>(_outputQueue.size()) >= _capacity && !_capacityWarned) {
                promekiWarn("MediaIOTask_SRC: output queue exceeded capacity (%d >= %d)",
                        static_cast<int>(_outputQueue.size()), _capacity);
                _capacityWarned = true;
        }

        Frame::Ptr outFrame;
        Error err = convertFrame(cmd.frame, outFrame);
        if(err.isError()) { stampWorkEnd(); return err; }

        _outputQueue.pushToBack(std::move(outFrame));
        _frameCount++;
        _framesConverted++;
        cmd.currentFrame = toFrameNumber(_frameCount);
        cmd.frameCount = _frameCount;
        stampWorkEnd();
        return Error::Ok;
}

Error MediaIOTask_SRC::executeCmd(MediaIOCommandRead &cmd) {
        if(_outputQueue.isEmpty()) {
                return Error::TryAgain;
        }

        Frame::Ptr frame = std::move(_outputQueue.front());
        _outputQueue.remove(0);
        _readCount++;
        cmd.frame = std::move(frame);
        cmd.currentFrame = _readCount;
        return Error::Ok;
}

Error MediaIOTask_SRC::executeCmd(MediaIOCommandStats &cmd) {
        cmd.stats.set(StatsFramesConverted, _framesConverted);
        cmd.stats.set(MediaIOStats::QueueDepth, static_cast<int64_t>(_outputQueue.size()));
        cmd.stats.set(MediaIOStats::QueueCapacity, static_cast<int64_t>(_capacity));
        return Error::Ok;
}

int MediaIOTask_SRC::pendingOutput() const {
        return static_cast<int>(_outputQueue.size());
}

// ---- Phase 1 introspection / negotiation overrides ----

Error MediaIOTask_SRC::describe(MediaIODescription *out) const {
        if(out == nullptr) return Error::Invalid;
        // SRC accepts any uncompressed audio MediaDesc and produces
        // the configured output sample format (or pass-through).
        return Error::Ok;
}

Error MediaIOTask_SRC::proposeInput(const MediaDesc &offered,
                                    MediaDesc *preferred) const {
        if(preferred == nullptr) return Error::Invalid;
        // SRC has no opinion on the input shape other than wanting
        // uncompressed PCM audio.  Compressed audio belongs to a
        // future audio-decoder stage.
        for(const auto &ad : offered.audioList()) {
                if(!ad.format().isValid() || ad.isCompressed()) {
                        return Error::NotSupported;
                }
        }
        *preferred = offered;
        return Error::Ok;
}

Error MediaIOTask_SRC::proposeOutput(const MediaDesc &requested,
                                     MediaDesc *achievable) const {
        if(achievable == nullptr) return Error::Invalid;
        const MediaIO *io = mediaIo();
        const MediaConfig &cfg = (io != nullptr) ? io->config() : MediaConfig();
        *achievable = applyOutputOverrides(requested, cfg);
        return Error::Ok;
}

PROMEKI_NAMESPACE_END
