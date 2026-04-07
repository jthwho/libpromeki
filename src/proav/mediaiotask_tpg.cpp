/**
 * @file      mediaiotask_tpg.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cmath>
#include <promeki/mediaiotask_tpg.h>
#include <promeki/image.h>
#include <promeki/audio.h>
#include <promeki/frame.h>
#include <promeki/pixeldesc.h>
#include <promeki/metadata.h>
#include <promeki/timecode.h>
#include <promeki/audiolevel.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_REGISTER_MEDIAIO(MediaIOTask_TPG)

// General
const MediaIO::ConfigID MediaIOTask_TPG::ConfigFrameRate("FrameRate");

// Video
const MediaIO::ConfigID MediaIOTask_TPG::ConfigVideoEnabled("VideoEnabled");
const MediaIO::ConfigID MediaIOTask_TPG::ConfigVideoPattern("VideoPattern");
const MediaIO::ConfigID MediaIOTask_TPG::ConfigVideoWidth("VideoWidth");
const MediaIO::ConfigID MediaIOTask_TPG::ConfigVideoHeight("VideoHeight");
const MediaIO::ConfigID MediaIOTask_TPG::ConfigVideoPixelFormat("VideoPixelFormat");
const MediaIO::ConfigID MediaIOTask_TPG::ConfigVideoSolidColor("VideoSolidColor");
const MediaIO::ConfigID MediaIOTask_TPG::ConfigVideoMotion("VideoMotion");

// Audio
const MediaIO::ConfigID MediaIOTask_TPG::ConfigAudioEnabled("AudioEnabled");
const MediaIO::ConfigID MediaIOTask_TPG::ConfigAudioMode("AudioMode");
const MediaIO::ConfigID MediaIOTask_TPG::ConfigAudioRate("AudioRate");
const MediaIO::ConfigID MediaIOTask_TPG::ConfigAudioChannels("AudioChannels");
const MediaIO::ConfigID MediaIOTask_TPG::ConfigAudioToneFrequency("AudioToneFrequency");
const MediaIO::ConfigID MediaIOTask_TPG::ConfigAudioToneLevel("AudioToneLevel");
const MediaIO::ConfigID MediaIOTask_TPG::ConfigAudioLtcLevel("AudioLtcLevel");
const MediaIO::ConfigID MediaIOTask_TPG::ConfigAudioLtcChannel("AudioLtcChannel");

// Timecode
const MediaIO::ConfigID MediaIOTask_TPG::ConfigTimecodeEnabled("TimecodeEnabled");
const MediaIO::ConfigID MediaIOTask_TPG::ConfigTimecodeStart("TimecodeStart");
const MediaIO::ConfigID MediaIOTask_TPG::ConfigTimecodeValue("TimecodeValue");
const MediaIO::ConfigID MediaIOTask_TPG::ConfigTimecodeDropFrame("TimecodeDropFrame");

MediaIO::FormatDesc MediaIOTask_TPG::formatDesc() {
        return {
                "TPG",
                "Video/audio/timecode test pattern generator",
                {},     // No file extensions — this is a generator
                true,   // canRead
                false,  // canWrite
                false,  // canReadWrite
                []() -> MediaIOTask * {
                        return new MediaIOTask_TPG();
                },
                []() -> MediaIO::Config {
                        MediaIO::Config cfg;
                        // General
                        cfg.set(ConfigFrameRate, FrameRate(FrameRate::FPS_2997));
                        // Video
                        cfg.set(ConfigVideoEnabled, false);
                        cfg.set(ConfigVideoPattern, "colorbars");
                        cfg.set(ConfigVideoWidth, 1920);
                        cfg.set(ConfigVideoHeight, 1080);
                        cfg.set(ConfigVideoPixelFormat, PixelDesc(PixelDesc::RGB8_sRGB));
                        cfg.set(ConfigVideoSolidColor, Color::Black);
                        cfg.set(ConfigVideoMotion, 0.0);
                        // Audio
                        cfg.set(ConfigAudioEnabled, false);
                        cfg.set(ConfigAudioMode, "tone");
                        cfg.set(ConfigAudioRate, 48000.0f);
                        cfg.set(ConfigAudioChannels, 2);
                        cfg.set(ConfigAudioToneFrequency, 1000.0);
                        cfg.set(ConfigAudioToneLevel, -20.0);
                        cfg.set(ConfigAudioLtcLevel, -20.0);
                        cfg.set(ConfigAudioLtcChannel, 0);
                        // Timecode
                        cfg.set(ConfigTimecodeEnabled, false);
                        cfg.set(ConfigTimecodeStart, "00:00:00:00");
                        cfg.set(ConfigTimecodeDropFrame, false);
                        return cfg;
                }
        };
}

MediaIOTask_TPG::~MediaIOTask_TPG() {
        delete _audioPattern;
}

Error MediaIOTask_TPG::executeCmd(MediaIOCommandOpen &cmd) {
        if(cmd.mode != MediaIO::Reader) return Error::NotSupported;

        const MediaIO::Config &cfg = cmd.config;

        // -- Frame rate (required) --
        FrameRate fps = cfg.getAs<FrameRate>(ConfigFrameRate, FrameRate(FrameRate::FPS_2997));
        if(!fps.isValid()) {
                promekiErr("MediaIOTask_TPG: invalid frame rate");
                return Error::InvalidArgument;
        }
        _frameRate = fps;

        MediaDesc mediaDesc;
        mediaDesc.setFrameRate(fps);

        // -- Video --
        _videoEnabled = cfg.getAs<bool>(ConfigVideoEnabled, false);
        if(_videoEnabled) {
                String patStr = cfg.getAs<String>(ConfigVideoPattern, "colorbars");
                auto [pat, patErr] = VideoTestPattern::fromString(patStr);
                if(patErr.isError()) {
                        promekiErr("MediaIOTask_TPG: unknown pattern '%s'", patStr.cstr());
                        return Error::InvalidArgument;
                }
                _videoPattern.setPattern(pat);

                int width = cfg.getAs<int>(ConfigVideoWidth, 1920);
                int height = cfg.getAs<int>(ConfigVideoHeight, 1080);
                PixelDesc pd = cfg.getAs<PixelDesc>(ConfigVideoPixelFormat, PixelDesc(PixelDesc::RGB8_sRGB));
                if(width <= 0 || height <= 0) {
                        promekiErr("MediaIOTask_TPG: invalid dimensions %dx%d", width, height);
                        return Error::InvalidDimension;
                }

                _imageDesc = ImageDesc(width, height, pd.id());
                mediaDesc.imageList().pushToBack(_imageDesc);

                Color solidColor = cfg.getAs<Color>(ConfigVideoSolidColor, Color::Black);
                _videoPattern.setSolidColor(solidColor);

                _motion = cfg.getAs<double>(ConfigVideoMotion, 0.0);
                _motionOffset = 0.0;

                // Pre-render for static patterns
                if(_motion == 0.0 && _videoPattern.pattern() != VideoTestPattern::Noise) {
                        _cachedImage = _videoPattern.create(_imageDesc);
                } else {
                        _cachedImage = Image();
                }
        }

        // -- Audio --
        _audioEnabled = cfg.getAs<bool>(ConfigAudioEnabled, false);
        if(_audioEnabled) {
                float audioRate = cfg.getAs<float>(ConfigAudioRate, 48000.0f);
                int audioChannels = cfg.getAs<int>(ConfigAudioChannels, 2);
                _audioDesc = AudioDesc(audioRate, audioChannels);
                if(!_audioDesc.isValid()) {
                        promekiErr("MediaIOTask_TPG: invalid audio desc");
                        return Error::InvalidArgument;
                }

                mediaDesc.audioList().pushToBack(_audioDesc);

                double fpsVal = fps.toDouble();
                _samplesPerFrame = (fpsVal > 0.0)
                        ? (size_t)std::round(_audioDesc.sampleRate() / fpsVal)
                        : 1600;

                delete _audioPattern;
                _audioPattern = new AudioTestPattern(_audioDesc);

                String audioModeStr = cfg.getAs<String>(ConfigAudioMode, "tone");
                auto [audioMode, modeErr] = AudioTestPattern::fromString(audioModeStr);
                if(modeErr.isError()) {
                        promekiErr("MediaIOTask_TPG: unknown audio mode '%s'", audioModeStr.cstr());
                        delete _audioPattern;
                        _audioPattern = nullptr;
                        return Error::InvalidArgument;
                }
                _audioPattern->setMode(audioMode);

                double toneFreq = cfg.getAs<double>(ConfigAudioToneFrequency, 1000.0);
                double toneLevel = cfg.getAs<double>(ConfigAudioToneLevel, -20.0);
                double ltcLevel = cfg.getAs<double>(ConfigAudioLtcLevel, -20.0);
                int ltcChannel = cfg.getAs<int>(ConfigAudioLtcChannel, 0);

                _audioPattern->setToneFrequency(toneFreq);
                _audioPattern->setToneLevel(AudioLevel::fromDbfs(toneLevel));
                _audioPattern->setLtcLevel(AudioLevel::fromDbfs(ltcLevel));
                _audioPattern->setLtcChannel(ltcChannel);
                _audioPattern->configure();
        }

        // -- Timecode --
        _timecodeEnabled = cfg.getAs<bool>(ConfigTimecodeEnabled, false);
        if(_timecodeEnabled) {
                _tcGen = TimecodeGenerator();

                Variant tcVar = cfg.get(ConfigTimecodeValue);
                if(tcVar.isValid()) {
                        _tcGen.setTimecode(tcVar.get<Timecode>());
                } else {
                        String tcStr = cfg.getAs<String>(ConfigTimecodeStart, "00:00:00:00");
                        if(!tcStr.isEmpty()) {
                                auto [tc, tcErr] = Timecode::fromString(tcStr);
                                if(tcErr.isOk()) {
                                        _tcGen.setTimecode(tc);
                                }
                        }
                }

                bool dropFrame = cfg.getAs<bool>(ConfigTimecodeDropFrame, false);
                _tcGen.setDropFrame(dropFrame);
                _tcGen.setFrameRate(fps);
        }

        // Must have at least one component enabled
        if(!_videoEnabled && !_audioEnabled && !_timecodeEnabled) {
                promekiErr("MediaIOTask_TPG: no components enabled");
                return Error::InvalidArgument;
        }

        _frameCount = 0;

        // Fill output fields
        cmd.mediaDesc = mediaDesc;
        if(_audioEnabled) cmd.audioDesc = _audioDesc;
        cmd.frameRate = fps;
        cmd.canSeek = false;
        cmd.frameCount = MediaIO::FrameCountInfinite;
        return Error::Ok;
}

Error MediaIOTask_TPG::executeCmd(MediaIOCommandClose &cmd) {
        delete _audioPattern;
        _audioPattern = nullptr;
        _cachedImage = Image();
        _imageDesc = ImageDesc();
        _audioDesc = AudioDesc();
        _frameRate = FrameRate();
        _tcGen.reset();
        _frameCount = 0;
        _motionOffset = 0.0;
        _videoEnabled = false;
        _audioEnabled = false;
        _timecodeEnabled = false;
        return Error::Ok;
}

Error MediaIOTask_TPG::executeCmd(MediaIOCommandRead &cmd) {
        int s = cmd.step;

        // The TPG ignores step direction for the timecode generator's run mode;
        // each read processes |step| advances.  (Previously the TPG cached this
        // via setStep on the base class — now it's per-read so the task is
        // stateless from MediaIO's perspective.)
        if(_timecodeEnabled) {
                if(s > 0)      _tcGen.setRunMode(TimecodeGenerator::Forward);
                else if(s < 0) _tcGen.setRunMode(TimecodeGenerator::Reverse);
                else           _tcGen.setRunMode(TimecodeGenerator::Still);
        }

        Frame::Ptr frame = Frame::Ptr::create();

        // Advance timecode by |step| frames (or hold at step=0).
        Timecode tc;
        if(_timecodeEnabled) {
                int advances = (s >= 0) ? s : -s;
                if(advances == 0) {
                        tc = _tcGen.timecode();
                } else {
                        tc = _tcGen.advance();
                        for(int i = 1; i < advances; i++) _tcGen.advance();
                }
        }

        // Video
        if(_videoEnabled) {
                Image img;
                if(_cachedImage.isValid() && s == 0) {
                        img = _cachedImage;
                } else {
                        img = _videoPattern.create(_imageDesc, _motionOffset);
                }
                if(_timecodeEnabled) {
                        img.metadata().set(Metadata::Timecode, tc);
                }
                frame.modify()->imageList().pushToBack(Image::Ptr::create(img));
        }

        // Audio
        if(_audioEnabled && _audioPattern != nullptr) {
                Audio audio = (_timecodeEnabled)
                        ? _audioPattern->create(_samplesPerFrame, tc)
                        : _audioPattern->create(_samplesPerFrame);
                if(audio.isValid()) {
                        frame.modify()->audioList().pushToBack(Audio::Ptr::create(audio));
                }
        }

        // Frame-level timecode metadata
        if(_timecodeEnabled) {
                frame.modify()->metadata().set(Metadata::Timecode, tc);
        }

        // Advance motion by step (negative step reverses direction)
        if(_videoEnabled && _motion != 0.0 && s != 0) {
                double fpsVal = _frameRate.toDouble();
                if(fpsVal > 0.0) {
                        _motionOffset += _motion * (double)s * (double)_imageDesc.size().width() / fpsVal;
                        double period = (double)_imageDesc.size().width();
                        if(period > 0.0) {
                                while(_motionOffset >= period) _motionOffset -= period;
                                while(_motionOffset < 0.0) _motionOffset += period;
                        }
                }
        }

        _frameCount++;
        cmd.frame = std::move(frame);
        cmd.currentFrame = _frameCount;
        return Error::Ok;
}

PROMEKI_NAMESPACE_END
