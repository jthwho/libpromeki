/**
 * @file      mediaiotask_burn.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/mediaiotask_burn.h>
#include <promeki/enums.h>
#include <promeki/image.h>
#include <promeki/frame.h>
#include <promeki/mediadesc.h>
#include <promeki/mediaconfig.h>
#include <promeki/metadata.h>
#include <promeki/pixeldesc.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_REGISTER_MEDIAIO(MediaIOTask_Burn)

MediaIO::FormatDesc MediaIOTask_Burn::formatDesc() {
        return {
                "Burn",
                "Text burn-in overlay on video frames",
                {},
                false,  // canOutput
                false,  // canInput
                true,   // canInputAndOutput
                []() -> MediaIOTask * {
                        return new MediaIOTask_Burn();
                },
                []() -> MediaIO::Config::SpecMap {
                        MediaIO::Config::SpecMap specs;
                        auto s = [&specs](MediaConfig::ID id, const Variant &def) {
                                const VariantSpec *gs = MediaConfig::spec(id);
                                specs.insert(id, gs ? VariantSpec(*gs).setDefault(def) : VariantSpec().setDefault(def));
                        };
                        s(MediaConfig::VideoBurnEnabled, true);
                        s(MediaConfig::VideoBurnFontPath, String());
                        s(MediaConfig::VideoBurnFontSize, int32_t(0));
                        s(MediaConfig::VideoBurnText, String("{Timecode:smpte}"));
                        s(MediaConfig::VideoBurnPosition, BurnPosition::BottomCenter);
                        s(MediaConfig::VideoBurnTextColor, Color::White);
                        s(MediaConfig::VideoBurnBgColor, Color::Black);
                        s(MediaConfig::VideoBurnDrawBg, true);
                        s(MediaConfig::Capacity, int32_t(4));
                        return specs;
                }
        };
}

MediaIOTask_Burn::~MediaIOTask_Burn() = default;

Error MediaIOTask_Burn::executeCmd(MediaIOCommandOpen &cmd) {
        if(cmd.mode != MediaIO::InputAndOutput) {
                promekiErr("MediaIOTask_Burn: only InputAndOutput mode is supported");
                return Error::NotSupported;
        }

        const MediaIO::Config &cfg = cmd.config;

        _burnEnabled = cfg.getAs<bool>(MediaConfig::VideoBurnEnabled, true);
        _pattern.setBurnEnabled(_burnEnabled);

        if(_burnEnabled) {
                _pattern.setBurnFontFilename(
                        cfg.getAs<String>(MediaConfig::VideoBurnFontPath, String()));
                _pattern.setBurnFontSize(
                        cfg.getAs<int>(MediaConfig::VideoBurnFontSize, 0));
                _burnTextTemplate =
                        cfg.getAs<String>(MediaConfig::VideoBurnText, String());
                _pattern.setBurnTextColor(
                        cfg.getAs<Color>(MediaConfig::VideoBurnTextColor, Color::White));
                _pattern.setBurnBackgroundColor(
                        cfg.getAs<Color>(MediaConfig::VideoBurnBgColor, Color::Black));
                _pattern.setBurnDrawBackground(
                        cfg.getAs<bool>(MediaConfig::VideoBurnDrawBg, true));

                Error posErr;
                Enum posEnum = cfg.get(MediaConfig::VideoBurnPosition)
                                   .asEnum(BurnPosition::Type, &posErr);
                if(posErr.isError() || !posEnum.hasListedValue()) {
                        promekiErr("MediaIOTask_Burn: unknown burn position '%s'",
                                   cfg.get(MediaConfig::VideoBurnPosition)
                                           .get<String>().cstr());
                        return Error::InvalidArgument;
                }
                _pattern.setBurnPosition(BurnPosition(posEnum.value()));
        }

        _capacity = cfg.getAs<int>(MediaConfig::Capacity, 4);
        if(_capacity < 1) _capacity = 1;

        _frameCount = 0;
        _readCount = 0;
        _framesBurned = 0;
        _outputQueue.clear();

        cmd.mediaDesc = cmd.pendingMediaDesc;
        if(!cmd.pendingMediaDesc.audioList().isEmpty())
                cmd.audioDesc = cmd.pendingMediaDesc.audioList()[0];
        cmd.metadata = cmd.pendingMetadata;
        cmd.frameRate = cmd.pendingMediaDesc.frameRate();
        cmd.canSeek = false;
        cmd.frameCount = MediaIO::FrameCountInfinite;
        cmd.defaultStep = 1;
        cmd.defaultPrefetchDepth = 1;
        cmd.defaultWriteDepth = _capacity;
        return Error::Ok;
}

Error MediaIOTask_Burn::executeCmd(MediaIOCommandClose &cmd) {
        (void)cmd;
        _outputQueue.clear();
        _burnEnabled = false;
        _burnTextTemplate = String();
        _pattern.setBurnEnabled(false);
        _frameCount = 0;
        _readCount = 0;
        _framesBurned = 0;
        _capacityWarned = false;
        _notPaintableWarned = false;
        return Error::Ok;
}

Error MediaIOTask_Burn::burnFrame(const Frame::Ptr &input, Frame::Ptr &output) {
        if(!input.isValid()) {
                return Error::Invalid;
        }

        if(!_burnEnabled || _burnTextTemplate.isEmpty()) {
                output = input;
                return Error::Ok;
        }

        Frame::Ptr outFrame = Frame::Ptr::create();
        Frame *outRaw = outFrame.modify();
        outRaw->metadata() = input->metadata();

        for(const auto &srcAudioPtr : input->audioList()) {
                outRaw->audioList().pushToBack(srcAudioPtr);
        }

        for(const auto &srcImgPtr : input->imageList()) {
                outRaw->imageList().pushToBack(srcImgPtr);
        }

        String burnText = outFrame->makeString(_burnTextTemplate);
        if(!burnText.isEmpty()) {
                for(size_t i = 0; i < outRaw->imageList().size(); ++i) {
                        Image::Ptr &imgPtr = outRaw->imageList()[i];
                        if(!imgPtr.isValid()) continue;

                        if(!imgPtr->pixelDesc().hasPaintEngine()) {
                                if(!_notPaintableWarned) {
                                        promekiWarn("MediaIOTask_Burn: pixel format %s "
                                                    "has no paint engine; skipping burn",
                                                    imgPtr->pixelDesc().name().cstr());
                                        _notPaintableWarned = true;
                                }
                                continue;
                        }

                        Image *imgMut = imgPtr.modify();
                        imgMut->ensureExclusive();
                        Error burnErr = _pattern.applyBurn(*imgMut, burnText);
                        if(burnErr.isError()) {
                                promekiWarn("MediaIOTask_Burn: applyBurn failed: %s",
                                            burnErr.name().cstr());
                        }
                }
        }

        output = std::move(outFrame);
        return Error::Ok;
}

Error MediaIOTask_Burn::executeCmd(MediaIOCommandWrite &cmd) {
        if(!cmd.frame.isValid()) {
                promekiErr("MediaIOTask_Burn: write with null frame");
                return Error::InvalidArgument;
        }
        stampWorkBegin();

        if(static_cast<int>(_outputQueue.size()) >= _capacity && !_capacityWarned) {
                promekiWarn("MediaIOTask_Burn: output queue exceeded capacity (%d >= %d)",
                        static_cast<int>(_outputQueue.size()), _capacity);
                _capacityWarned = true;
        }

        Frame::Ptr outFrame;
        Error err = burnFrame(cmd.frame, outFrame);
        if(err.isError()) { stampWorkEnd(); return err; }

        _outputQueue.pushToBack(std::move(outFrame));
        _frameCount++;
        _framesBurned++;
        cmd.currentFrame = _frameCount;
        cmd.frameCount = _frameCount;
        stampWorkEnd();
        return Error::Ok;
}

Error MediaIOTask_Burn::executeCmd(MediaIOCommandRead &cmd) {
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

Error MediaIOTask_Burn::executeCmd(MediaIOCommandStats &cmd) {
        cmd.stats.set(StatsFramesBurned, _framesBurned);
        cmd.stats.set(MediaIOStats::QueueDepth, static_cast<int64_t>(_outputQueue.size()));
        cmd.stats.set(MediaIOStats::QueueCapacity, static_cast<int64_t>(_capacity));
        return Error::Ok;
}

int MediaIOTask_Burn::pendingOutput() const {
        return static_cast<int>(_outputQueue.size());
}

PROMEKI_NAMESPACE_END
