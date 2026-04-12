/**
 * @file      mediaiotask_converter.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/mediaiotask_converter.h>
#include <promeki/enums.h>
#include <promeki/image.h>
#include <promeki/audio.h>
#include <promeki/frame.h>
#include <promeki/imagedesc.h>
#include <promeki/mediadesc.h>
#include <promeki/mediaconfig.h>
#include <promeki/metadata.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_REGISTER_MEDIAIO(MediaIOTask_Converter)

MediaIO::FormatDesc MediaIOTask_Converter::formatDesc() {
        return {
                "Converter",
                "Intra-frame media converter (CSC, JPEG encode/decode, audio sample format)",
                {},     // No file extensions — this is a transform filter
                false,  // canOutput  — pure Reader has no input source
                false,  // canInput — pure Writer has no output sink
                true,   // canInputAndOutput
                []() -> MediaIOTask * {
                        return new MediaIOTask_Converter();
                },
                []() -> MediaIO::Config::SpecMap {
                        MediaIO::Config::SpecMap specs;
                        auto s = [&specs](MediaConfig::ID id, const Variant &def) {
                                const VariantSpec *gs = MediaConfig::spec(id);
                                specs.insert(id, gs ? VariantSpec(*gs).setDefault(def) : VariantSpec().setDefault(def));
                        };
                        s(MediaConfig::OutputPixelDesc, PixelDesc());
                        s(MediaConfig::JpegQuality, int32_t(85));
                        s(MediaConfig::JpegSubsampling, ChromaSubsampling::YUV422);
                        s(MediaConfig::OutputAudioDataType, AudioDataType::Invalid);
                        s(MediaConfig::Capacity, int32_t(4));
                        return specs;
                }
        };
}

MediaIOTask_Converter::~MediaIOTask_Converter() = default;

Error MediaIOTask_Converter::executeCmd(MediaIOCommandOpen &cmd) {
        if(cmd.mode != MediaIO::InputAndOutput) {
                promekiErr("MediaIOTask_Converter: only ReadWrite mode is supported");
                return Error::NotSupported;
        }

        const MediaIO::Config &cfg = cmd.config;

        // -- Video output pixel description --
        _outputPixelDesc = cfg.getAs<PixelDesc>(MediaConfig::OutputPixelDesc, PixelDesc());
        _outputPixelDescSet = _outputPixelDesc.isValid();

        // -- JPEG encode parameters --
        //
        // Validated here but applied via the MediaConfig that we hand to
        // Image::convert() on every frame.  Keeping them as plain fields
        // (rather than a cached JpegImageCodec member) means
        // Image::convert() stays the single dispatch point for every
        // encode/decode decision in the library, and the JPEG codec only
        // exists for the lifetime of one encode call.
        _jpegQuality = cfg.getAs<int>(MediaConfig::JpegQuality, 85);
        if(_jpegQuality < 1) _jpegQuality = 1;
        if(_jpegQuality > 100) _jpegQuality = 100;

        Error subErr;
        _jpegSubsamplingEnum = cfg.get(MediaConfig::JpegSubsampling).asEnum(
                                        ChromaSubsampling::Type, &subErr);
        if(subErr.isError() || !_jpegSubsamplingEnum.hasListedValue()) {
                promekiErr("MediaIOTask_Converter: unknown JPEG subsampling");
                return Error::InvalidArgument;
        }

        // -- Audio output data type --
        // AudioDataType Enum integer values mirror AudioDesc::DataType,
        // so a plain cast converts between the two.  The sentinel
        // "Invalid" (int 0) means "pass audio through unchanged".
        Error adtErr;
        Enum adtEnum = cfg.get(MediaConfig::OutputAudioDataType).asEnum(AudioDataType::Type, &adtErr);
        if(adtErr.isError()) {
                promekiErr("MediaIOTask_Converter: unknown audio data type");
                return Error::InvalidArgument;
        }
        _outputAudioDataType = static_cast<AudioDesc::DataType>(adtEnum.value());
        _outputAudioDataTypeSet = (_outputAudioDataType != AudioDesc::Invalid);

        // -- FIFO capacity --
        _capacity = cfg.getAs<int>(MediaConfig::Capacity, 4);
        if(_capacity < 1) _capacity = 1;

        // -- Output MediaDesc --
        // Start from the producer-supplied pending MediaDesc (the upstream
        // stage's format) and substitute the configured output types for
        // each track, so downstream consumers see the correct descriptor
        // even before the first frame arrives.
        MediaDesc outDesc;
        outDesc.setFrameRate(cmd.pendingMediaDesc.frameRate());

        for(const auto &srcImg : cmd.pendingMediaDesc.imageList()) {
                ImageDesc id = srcImg;
                if(_outputPixelDescSet) id = ImageDesc(srcImg.size(), _outputPixelDesc);
                outDesc.imageList().pushToBack(id);
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

Error MediaIOTask_Converter::executeCmd(MediaIOCommandClose &cmd) {
        (void)cmd;
        _outputQueue.clear();
        _outputPixelDesc = PixelDesc();
        _outputPixelDescSet = false;
        _outputAudioDataType = AudioDesc::Invalid;
        _outputAudioDataTypeSet = false;
        _jpegSubsamplingEnum = Enum();
        _frameCount = 0;
        _readCount = 0;
        _framesConverted = 0;
        _capacityWarned = false;
        return Error::Ok;
}

Error MediaIOTask_Converter::convertImage(const Image &input, Image &output) {
        if(!input.isValid()) {
                output = Image();
                return Error::Invalid;
        }

        // No output pixel desc configured — pass through unchanged.
        if(!_outputPixelDescSet) {
                output = input;
                return Error::Ok;
        }

        // Same format — no-op pass-through.
        if(input.pixelDesc() == _outputPixelDesc) {
                output = input;
                return Error::Ok;
        }

        // Build a MediaConfig so Image::convert() can honour the JPEG
        // quality / subsampling knobs we parsed in executeCmd().
        // Because MediaConfig is the same type the backend was
        // configured with, this is a pure key re-tagging — no
        // translation or temporary objects.  Image::convert()
        // transparently dispatches to JpegImageCodec for compressed ↔
        // uncompressed conversions and to CSCPipeline for uncompressed
        // ↔ uncompressed, so a single call covers every path this
        // backend supported in its earlier incarnation.
        MediaConfig convertConfig;
        convertConfig.set(MediaConfig::JpegQuality, _jpegQuality);
        if(_jpegSubsamplingEnum.hasListedValue()) {
                convertConfig.set(MediaConfig::JpegSubsampling, _jpegSubsamplingEnum);
        }

        Image converted = input.convert(_outputPixelDesc, input.metadata(),
                                        convertConfig);
        if(!converted.isValid()) {
                promekiErr("MediaIOTask_Converter: convert %s -> %s failed",
                           input.pixelDesc().name().cstr(),
                           _outputPixelDesc.name().cstr());
                return Error::ConversionFailed;
        }
        output = std::move(converted);
        return Error::Ok;
}

Error MediaIOTask_Converter::convertFrame(const Frame::Ptr &input, Frame::Ptr &output) {
        if(!input.isValid()) {
                return Error::Invalid;
        }

        Frame::Ptr outFrame = Frame::Ptr::create();
        Frame *outRaw = outFrame.modify();
        outRaw->metadata() = input->metadata();

        // Images.  Per-plane byte tracking is no longer needed —
        // MediaIO::populateStandardStats drives BytesPerSecond from
        // the base-class RateTracker, which records the full payload
        // size of each outbound frame after a successful write.
        for(const auto &srcImgPtr : input->imageList()) {
                if(!srcImgPtr.isValid()) continue;
                const Image &srcImg = *srcImgPtr;
                Image dstImg;
                Error err = convertImage(srcImg, dstImg);
                if(err.isError()) return err;
                outRaw->imageList().pushToBack(Image::Ptr::create(std::move(dstImg)));
        }

        // Audio
        for(const auto &srcAudioPtr : input->audioList()) {
                if(!srcAudioPtr.isValid()) continue;
                const Audio &srcAudio = *srcAudioPtr;

                Audio dstAudio;
                if(_outputAudioDataTypeSet &&
                   srcAudio.desc().dataType() != _outputAudioDataType) {
                        dstAudio = srcAudio.convertTo(_outputAudioDataType);
                        if(!dstAudio.isValid()) {
                                promekiErr("MediaIOTask_Converter: audio convertTo failed");
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

Error MediaIOTask_Converter::executeCmd(MediaIOCommandWrite &cmd) {
        if(!cmd.frame.isValid()) {
                promekiErr("MediaIOTask_Converter: write with null frame");
                return Error::InvalidArgument;
        }

        if(static_cast<int>(_outputQueue.size()) >= _capacity && !_capacityWarned) {
                promekiWarn("MediaIOTask_Converter: output queue exceeded capacity (%d >= %d)",
                        static_cast<int>(_outputQueue.size()), _capacity);
                _capacityWarned = true;
        }

        Frame::Ptr outFrame;
        Error err = convertFrame(cmd.frame, outFrame);
        if(err.isError()) return err;

        _outputQueue.pushToBack(std::move(outFrame));
        _frameCount++;
        _framesConverted++;
        cmd.currentFrame = _frameCount;
        cmd.frameCount = _frameCount;
        return Error::Ok;
}

Error MediaIOTask_Converter::executeCmd(MediaIOCommandRead &cmd) {
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

Error MediaIOTask_Converter::executeCmd(MediaIOCommandStats &cmd) {
        // FramesConverted is a backend-specific counter.  BytesIn /
        // BytesOut were removed — the MediaIO base class now reports
        // the single authoritative BytesPerSecond value from its
        // RateTracker, which records the converted (output) frame
        // payload on every successful write.
        cmd.stats.set(StatsFramesConverted, _framesConverted);
        cmd.stats.set(MediaIOStats::QueueDepth, static_cast<int64_t>(_outputQueue.size()));
        cmd.stats.set(MediaIOStats::QueueCapacity, static_cast<int64_t>(_capacity));
        return Error::Ok;
}

int MediaIOTask_Converter::pendingOutput() const {
        return static_cast<int>(_outputQueue.size());
}

PROMEKI_NAMESPACE_END
