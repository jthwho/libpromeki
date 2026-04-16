/**
 * @file      mediaiotask_framesync.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/mediaiotask_framesync.h>
#include <promeki/enums.h>
#include <promeki/frame.h>
#include <promeki/framerate.h>
#include <promeki/audiodesc.h>
#include <promeki/mediadesc.h>
#include <promeki/mediaconfig.h>
#include <promeki/metadata.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_REGISTER_MEDIAIO(MediaIOTask_FrameSync)

MediaIO::FormatDesc MediaIOTask_FrameSync::formatDesc() {
        return {
                "FrameSync",
                "Frame synchroniser — resyncs media to a target clock cadence",
                {},
                false,  // canOutput
                false,  // canInput
                true,   // canInputAndOutput
                []() -> MediaIOTask * {
                        return new MediaIOTask_FrameSync();
                },
                []() -> MediaIO::Config::SpecMap {
                        MediaIO::Config::SpecMap specs;
                        auto s = [&specs](MediaConfig::ID id, const Variant &def) {
                                const VariantSpec *gs = MediaConfig::spec(id);
                                specs.insert(id, gs ? VariantSpec(*gs).setDefault(def) : VariantSpec().setDefault(def));
                        };
                        s(MediaConfig::OutputFrameRate, FrameRate());
                        s(MediaConfig::OutputAudioRate, 0.0f);
                        s(MediaConfig::OutputAudioChannels, int32_t(0));
                        s(MediaConfig::OutputAudioDataType, AudioDataType::Invalid);
                        s(MediaConfig::InputQueueCapacity, int32_t(8));
                        return specs;
                }
        };
}

MediaIOTask_FrameSync::~MediaIOTask_FrameSync() = default;

void MediaIOTask_FrameSync::setClock(Clock *clock) {
        _externalClock = clock;
}

Error MediaIOTask_FrameSync::executeCmd(MediaIOCommandOpen &cmd) {
        if(cmd.mode != MediaIO::InputAndOutput) {
                promekiErr("MediaIOTask_FrameSync: only InputAndOutput mode is supported");
                return Error::NotSupported;
        }

        const MediaIO::Config &cfg = cmd.config;
        const MediaDesc &mdesc = cmd.pendingMediaDesc;

        FrameRate srcFps = mdesc.frameRate();
        if(!srcFps.isValid()) {
                promekiErr("MediaIOTask_FrameSync: pendingMediaDesc has no valid frame rate");
                return Error::InvalidArgument;
        }

        FrameRate outFps = cfg.getAs<FrameRate>(MediaConfig::OutputFrameRate, FrameRate());
        if(!outFps.isValid()) outFps = srcFps;

        int queueCap = cfg.getAs<int>(MediaConfig::InputQueueCapacity, 8);
        if(queueCap < 1) queueCap = 1;

        AudioDesc srcAdesc;
        if(!mdesc.audioList().isEmpty()) srcAdesc = mdesc.audioList()[0];

        AudioDesc adesc = srcAdesc;
        if(adesc.isValid()) {
                float outRate = cfg.getAs<float>(MediaConfig::OutputAudioRate, 0.0f);
                int outChannels = cfg.getAs<int>(MediaConfig::OutputAudioChannels, 0);
                AudioDesc::DataType outDt = adesc.dataType();

                Error adtErr;
                Enum adtEnum = cfg.get(MediaConfig::OutputAudioDataType)
                                   .asEnum(AudioDataType::Type, &adtErr);
                if(adtErr.isOk() && adtEnum.hasListedValue() &&
                   static_cast<AudioDesc::DataType>(adtEnum.value()) != AudioDesc::Invalid) {
                        outDt = static_cast<AudioDesc::DataType>(adtEnum.value());
                }

                adesc = AudioDesc(
                        outDt,
                        (outRate > 0.0f) ? outRate : srcAdesc.sampleRate(),
                        (outChannels > 0) ? static_cast<unsigned int>(outChannels)
                                          : srcAdesc.channels());
                adesc.metadata() = srcAdesc.metadata();
        }

        Clock *clock = _externalClock;
        if(!clock) {
                _ownedClock = SyntheticClock();
                clock = &_ownedClock;
        }

        _sync.setName(String("FrameSync"));
        _sync.setTargetFrameRate(outFps);
        _sync.setTargetAudioDesc(adesc);
        _sync.setClock(clock);
        _sync.setInputQueueCapacity(queueCap);
        _sync.reset();

        _framesPushed = 0;
        _framesPulled = 0;

        MediaDesc outDesc;
        outDesc.setFrameRate(outFps);
        for(const auto &img : mdesc.imageList()) {
                outDesc.imageList().pushToBack(img);
        }
        if(adesc.isValid()) {
                outDesc.audioList().pushToBack(adesc);
        }
        cmd.mediaDesc = outDesc;
        if(adesc.isValid()) cmd.audioDesc = adesc;
        cmd.metadata = cmd.pendingMetadata;
        cmd.frameRate = outFps;
        cmd.canSeek = false;
        cmd.frameCount = MediaIO::FrameCountInfinite;
        cmd.defaultStep = 1;
        cmd.defaultPrefetchDepth = 1;
        cmd.defaultWriteDepth = 2;
        return Error::Ok;
}

Error MediaIOTask_FrameSync::executeCmd(MediaIOCommandClose &cmd) {
        (void)cmd;
        _sync.pushEndOfStream();
        _sync.interrupt();
        _sync.reset();
        _sync.setClock(nullptr);
        _framesPushed = 0;
        _framesPulled = 0;
        return Error::Ok;
}

Error MediaIOTask_FrameSync::executeCmd(MediaIOCommandWrite &cmd) {
        if(!cmd.frame.isValid()) {
                promekiErr("MediaIOTask_FrameSync: write with null frame");
                return Error::InvalidArgument;
        }
        stampWorkBegin();
        Error err = _sync.pushFrame(cmd.frame);
        _framesPushed++;
        cmd.currentFrame = _framesPushed;
        cmd.frameCount = _framesPushed;
        stampWorkEnd();
        return err;
}

Error MediaIOTask_FrameSync::executeCmd(MediaIOCommandRead &cmd) {
        auto result = _sync.pullFrame();
        if(result.second().isError()) {
                return result.second();
        }

        const FrameSync::PullResult &pr = result.first();
        if(!pr.frame.isValid()) {
                return Error::EndOfFile;
        }

        if(pr.framesRepeated > 0) noteFrameRepeated();
        if(pr.framesDropped > 0) noteFrameDropped();

        _framesPulled++;
        cmd.frame = pr.frame;
        cmd.currentFrame = _framesPulled;
        return Error::Ok;
}

Error MediaIOTask_FrameSync::executeCmd(MediaIOCommandStats &cmd) {
        cmd.stats.set(StatsFramesPushed, _framesPushed);
        cmd.stats.set(StatsFramesPulled, _framesPulled);
        cmd.stats.set(MediaIOStats::FramesRepeated, _sync.framesRepeated());
        cmd.stats.set(MediaIOStats::FramesDropped, _sync.framesDropped());
        return Error::Ok;
}

PROMEKI_NAMESPACE_END
