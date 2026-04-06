/**
 * @file      mediaio_tpg.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cmath>
#include <promeki/mediaio_tpg.h>
#include <promeki/image.h>
#include <promeki/audio.h>
#include <promeki/frame.h>
#include <promeki/pixeldesc.h>
#include <promeki/metadata.h>
#include <promeki/timecode.h>
#include <promeki/audiolevel.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_REGISTER_MEDIAIO(MediaIO_TPG)

// General
const MediaIO::ConfigID MediaIO_TPG::ConfigFrameRate("FrameRate");

// Video
const MediaIO::ConfigID MediaIO_TPG::ConfigVideoEnabled("VideoEnabled");
const MediaIO::ConfigID MediaIO_TPG::ConfigVideoPattern("VideoPattern");
const MediaIO::ConfigID MediaIO_TPG::ConfigVideoWidth("VideoWidth");
const MediaIO::ConfigID MediaIO_TPG::ConfigVideoHeight("VideoHeight");
const MediaIO::ConfigID MediaIO_TPG::ConfigVideoPixelFormat("VideoPixelFormat");
const MediaIO::ConfigID MediaIO_TPG::ConfigVideoSolidColorR("VideoSolidColorR");
const MediaIO::ConfigID MediaIO_TPG::ConfigVideoSolidColorG("VideoSolidColorG");
const MediaIO::ConfigID MediaIO_TPG::ConfigVideoSolidColorB("VideoSolidColorB");
const MediaIO::ConfigID MediaIO_TPG::ConfigVideoMotion("VideoMotion");

// Audio
const MediaIO::ConfigID MediaIO_TPG::ConfigAudioEnabled("AudioEnabled");
const MediaIO::ConfigID MediaIO_TPG::ConfigAudioMode("AudioMode");
const MediaIO::ConfigID MediaIO_TPG::ConfigAudioRate("AudioRate");
const MediaIO::ConfigID MediaIO_TPG::ConfigAudioChannels("AudioChannels");
const MediaIO::ConfigID MediaIO_TPG::ConfigAudioToneFrequency("AudioToneFrequency");
const MediaIO::ConfigID MediaIO_TPG::ConfigAudioToneLevel("AudioToneLevel");
const MediaIO::ConfigID MediaIO_TPG::ConfigAudioLtcLevel("AudioLtcLevel");
const MediaIO::ConfigID MediaIO_TPG::ConfigAudioLtcChannel("AudioLtcChannel");

// Timecode
const MediaIO::ConfigID MediaIO_TPG::ConfigTimecodeEnabled("TimecodeEnabled");
const MediaIO::ConfigID MediaIO_TPG::ConfigTimecodeStart("TimecodeStart");
const MediaIO::ConfigID MediaIO_TPG::ConfigTimecodeValue("TimecodeValue");
const MediaIO::ConfigID MediaIO_TPG::ConfigTimecodeDropFrame("TimecodeDropFrame");

MediaIO::FormatDesc MediaIO_TPG::formatDesc() {
        return {
                "TPG",
                "Video/audio/timecode test pattern generator",
                {},     // No file extensions — this is a generator
                true,   // canRead
                false,  // canWrite
                [](ObjectBase *parent) -> MediaIO * {
                        return new MediaIO_TPG(parent);
                }
        };
}

MediaIO_TPG::~MediaIO_TPG() {
        delete _audioPattern;
}

Error MediaIO_TPG::open(Mode mode) {
        if(isOpen()) return Error::AlreadyOpen;
        if(mode != Reader) return Error::NotSupported;

        const Config &cfg = config();

        // -- Frame rate (required) --
        FrameRate fps = cfg.getAs<FrameRate>(ConfigFrameRate, FrameRate(FrameRate::FPS_2997));
        if(!fps.isValid()) {
                promekiErr("MediaIO_TPG: invalid frame rate");
                return Error::InvalidArgument;
        }

        _videoDesc = VideoDesc();
        _videoDesc.setFrameRate(fps);

        // -- Video --
        _videoEnabled = cfg.getAs<bool>(ConfigVideoEnabled, false);
        if(_videoEnabled) {
                String patStr = cfg.getAs<String>(ConfigVideoPattern, "colorbars");
                auto [pat, patErr] = VideoTestPattern::fromString(patStr);
                if(patErr.isError()) {
                        promekiErr("MediaIO_TPG: unknown pattern '%s'", patStr.cstr());
                        return Error::InvalidArgument;
                }
                _videoPattern.setPattern(pat);

                int width = cfg.getAs<int>(ConfigVideoWidth, 1920);
                int height = cfg.getAs<int>(ConfigVideoHeight, 1080);
                PixelDesc pd = cfg.getAs<PixelDesc>(ConfigVideoPixelFormat, PixelDesc(PixelDesc::RGB8_sRGB));
                if(width <= 0 || height <= 0) {
                        promekiErr("MediaIO_TPG: invalid dimensions %dx%d", width, height);
                        return Error::InvalidDimension;
                }

                _imageDesc = ImageDesc(width, height, pd.id());
                _videoDesc.imageList().pushToBack(_imageDesc);

                uint16_t solidR = cfg.getAs<uint16_t>(ConfigVideoSolidColorR, uint16_t(0));
                uint16_t solidG = cfg.getAs<uint16_t>(ConfigVideoSolidColorG, uint16_t(0));
                uint16_t solidB = cfg.getAs<uint16_t>(ConfigVideoSolidColorB, uint16_t(0));
                _videoPattern.setSolidColor(solidR, solidG, solidB);

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
                        promekiErr("MediaIO_TPG: invalid audio desc");
                        return Error::InvalidArgument;
                }

                _videoDesc.audioList().pushToBack(_audioDesc);

                double fpsVal = fps.toDouble();
                _samplesPerFrame = (fpsVal > 0.0)
                        ? (size_t)std::round(_audioDesc.sampleRate() / fpsVal)
                        : 1600;

                delete _audioPattern;
                _audioPattern = new AudioTestPattern(_audioDesc);

                String audioModeStr = cfg.getAs<String>(ConfigAudioMode, "tone");
                auto [audioMode, modeErr] = AudioTestPattern::fromString(audioModeStr);
                if(modeErr.isError()) {
                        promekiErr("MediaIO_TPG: unknown audio mode '%s'", audioModeStr.cstr());
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

                // Set the timecode value BEFORE frame rate, because
                // setFrameRate() calls applyMode() which stamps the
                // derived mode onto the stored timecode.
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
                promekiErr("MediaIO_TPG: no components enabled");
                return Error::InvalidArgument;
        }

        _frameCount = 0;
        setMode(Reader);
        return Error::Ok;
}

Error MediaIO_TPG::close() {
        if(!isOpen()) return Error::NotOpen;

        delete _audioPattern;
        _audioPattern = nullptr;
        _cachedImage = Image();
        _imageDesc = ImageDesc();
        _videoDesc = VideoDesc();
        _audioDesc = AudioDesc();
        _tcGen.reset();
        _frameCount = 0;
        _motionOffset = 0.0;
        _videoEnabled = false;
        _audioEnabled = false;
        _timecodeEnabled = false;

        setMode(NotOpen);
        return Error::Ok;
}

VideoDesc MediaIO_TPG::videoDesc() const {
        return _videoDesc;
}

Error MediaIO_TPG::readFrame(Frame &frame) {
        if(!isOpen()) return Error::NotOpen;

        // Advance timecode if enabled
        Timecode tc;
        if(_timecodeEnabled) {
                tc = _tcGen.advance();
        }

        // Video
        if(_videoEnabled) {
                Image img;
                if(_cachedImage.isValid()) {
                        img = _cachedImage;
                } else {
                        img = _videoPattern.create(_imageDesc, _motionOffset);
                }
                if(_timecodeEnabled) {
                        img.metadata().set(Metadata::Timecode, tc);
                }
                frame.imageList().pushToBack(Image::Ptr::create(img));
        }

        // Audio
        if(_audioEnabled && _audioPattern != nullptr) {
                Audio audio = (_timecodeEnabled)
                        ? _audioPattern->create(_samplesPerFrame, tc)
                        : _audioPattern->create(_samplesPerFrame);
                if(audio.isValid()) {
                        frame.audioList().pushToBack(Audio::Ptr::create(audio));
                }
        }

        // Frame-level timecode metadata
        if(_timecodeEnabled) {
                frame.metadata().set(Metadata::Timecode, tc);
        }

        // Advance motion
        if(_videoEnabled && _motion != 0.0) {
                double fpsVal = _videoDesc.frameRate().toDouble();
                if(fpsVal > 0.0) {
                        _motionOffset += _motion * (double)_imageDesc.size().width() / fpsVal;
                        double period = (double)_imageDesc.size().width();
                        if(period > 0.0) {
                                while(_motionOffset >= period) _motionOffset -= period;
                                while(_motionOffset < 0.0) _motionOffset += period;
                        }
                }
        }

        _frameCount++;
        return Error::Ok;
}

uint64_t MediaIO_TPG::currentFrame() const {
        return _frameCount;
}

PROMEKI_NAMESPACE_END
