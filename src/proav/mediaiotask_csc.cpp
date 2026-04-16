/**
 * @file      mediaiotask_csc.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/mediaiotask_csc.h>
#include <promeki/image.h>
#include <promeki/audio.h>
#include <promeki/frame.h>
#include <promeki/imagedesc.h>
#include <promeki/mediadesc.h>
#include <promeki/mediaconfig.h>
#include <promeki/metadata.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_REGISTER_MEDIAIO(MediaIOTask_CSC)

MediaIO::FormatDesc MediaIOTask_CSC::formatDesc() {
        return {
                "CSC",
                "Color space converter (uncompressed pixel format conversion)",
                {},
                false,  // canOutput
                false,  // canInput
                true,   // canInputAndOutput
                []() -> MediaIOTask * {
                        return new MediaIOTask_CSC();
                },
                []() -> MediaIO::Config::SpecMap {
                        MediaIO::Config::SpecMap specs;
                        auto s = [&specs](MediaConfig::ID id, const Variant &def) {
                                const VariantSpec *gs = MediaConfig::spec(id);
                                specs.insert(id, gs ? VariantSpec(*gs).setDefault(def) : VariantSpec().setDefault(def));
                        };
                        s(MediaConfig::OutputPixelDesc, PixelDesc());
                        s(MediaConfig::Capacity, int32_t(4));
                        return specs;
                }
        };
}

MediaIOTask_CSC::~MediaIOTask_CSC() = default;

Error MediaIOTask_CSC::executeCmd(MediaIOCommandOpen &cmd) {
        if(cmd.mode != MediaIO::InputAndOutput) {
                promekiErr("MediaIOTask_CSC: only InputAndOutput mode is supported");
                return Error::NotSupported;
        }

        const MediaIO::Config &cfg = cmd.config;

        _outputPixelDesc = cfg.getAs<PixelDesc>(MediaConfig::OutputPixelDesc, PixelDesc());
        _outputPixelDescSet = _outputPixelDesc.isValid();

        _capacity = cfg.getAs<int>(MediaConfig::Capacity, 4);
        if(_capacity < 1) _capacity = 1;

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

Error MediaIOTask_CSC::executeCmd(MediaIOCommandClose &cmd) {
        (void)cmd;
        _outputQueue.clear();
        _outputPixelDesc = PixelDesc();
        _outputPixelDescSet = false;
        _frameCount = 0;
        _readCount = 0;
        _framesConverted = 0;
        _capacityWarned = false;
        return Error::Ok;
}

Error MediaIOTask_CSC::convertImage(const Image &input, Image &output) {
        if(!input.isValid()) {
                output = Image();
                return Error::Invalid;
        }

        if(!_outputPixelDescSet) {
                output = input;
                return Error::Ok;
        }

        if(input.pixelDesc() == _outputPixelDesc) {
                output = input;
                return Error::Ok;
        }

        if(input.pixelDesc().isCompressed() || _outputPixelDesc.isCompressed()) {
                promekiErr("MediaIOTask_CSC: %s -> %s is a compression hop; "
                           "use MediaIOTask_VideoEncoder / MediaIOTask_VideoDecoder",
                           input.pixelDesc().name().cstr(),
                           _outputPixelDesc.name().cstr());
                return Error::NotSupported;
        }

        MediaConfig convertConfig;
        Image converted = input.convert(_outputPixelDesc, input.metadata(),
                                        convertConfig);
        if(!converted.isValid()) {
                promekiErr("MediaIOTask_CSC: convert %s -> %s failed",
                           input.pixelDesc().name().cstr(),
                           _outputPixelDesc.name().cstr());
                return Error::ConversionFailed;
        }
        output = std::move(converted);
        return Error::Ok;
}

Error MediaIOTask_CSC::convertFrame(const Frame::Ptr &input, Frame::Ptr &output) {
        if(!input.isValid()) {
                return Error::Invalid;
        }

        Frame::Ptr outFrame = Frame::Ptr::create();
        Frame *outRaw = outFrame.modify();
        outRaw->metadata() = input->metadata();

        for(const auto &srcImgPtr : input->imageList()) {
                if(!srcImgPtr.isValid()) continue;
                const Image &srcImg = *srcImgPtr;
                Image dstImg;
                Error err = convertImage(srcImg, dstImg);
                if(err.isError()) return err;
                outRaw->imageList().pushToBack(Image::Ptr::create(std::move(dstImg)));
        }

        for(const auto &srcAudioPtr : input->audioList()) {
                outRaw->audioList().pushToBack(srcAudioPtr);
        }

        output = std::move(outFrame);
        return Error::Ok;
}

Error MediaIOTask_CSC::executeCmd(MediaIOCommandWrite &cmd) {
        if(!cmd.frame.isValid()) {
                promekiErr("MediaIOTask_CSC: write with null frame");
                return Error::InvalidArgument;
        }
        stampWorkBegin();

        if(static_cast<int>(_outputQueue.size()) >= _capacity && !_capacityWarned) {
                promekiWarn("MediaIOTask_CSC: output queue exceeded capacity (%d >= %d)",
                        static_cast<int>(_outputQueue.size()), _capacity);
                _capacityWarned = true;
        }

        Frame::Ptr outFrame;
        Error err = convertFrame(cmd.frame, outFrame);
        if(err.isError()) { stampWorkEnd(); return err; }

        _outputQueue.pushToBack(std::move(outFrame));
        _frameCount++;
        _framesConverted++;
        cmd.currentFrame = _frameCount;
        cmd.frameCount = _frameCount;
        stampWorkEnd();
        return Error::Ok;
}

Error MediaIOTask_CSC::executeCmd(MediaIOCommandRead &cmd) {
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

Error MediaIOTask_CSC::executeCmd(MediaIOCommandStats &cmd) {
        cmd.stats.set(StatsFramesConverted, _framesConverted);
        cmd.stats.set(MediaIOStats::QueueDepth, static_cast<int64_t>(_outputQueue.size()));
        cmd.stats.set(MediaIOStats::QueueCapacity, static_cast<int64_t>(_capacity));
        return Error::Ok;
}

int MediaIOTask_CSC::pendingOutput() const {
        return static_cast<int>(_outputQueue.size());
}

PROMEKI_NAMESPACE_END
