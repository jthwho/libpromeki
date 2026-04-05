/**
 * @file      testpatternnode.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cmath>
#include <cstring>
#include <promeki/testpatternnode.h>
#include <promeki/medianodeconfig.h>
#include <promeki/image.h>
#include <promeki/audio.h>
#include <promeki/frame.h>
#include <promeki/pixeldesc.h>
#include <promeki/metadata.h>
#include <promeki/timecode.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_REGISTER_NODE(TestPatternNode)

TestPatternNode::TestPatternNode(ObjectBase *parent) : MediaNode(parent) {
        setName("TestPatternNode");
        auto output = MediaSource::Ptr::create("output", ContentHint(ContentVideo | ContentAudio));
        addSource(output);
}

TestPatternNode::~TestPatternNode() {
        delete _audioPattern;
}

MediaNodeConfig TestPatternNode::defaultConfig() const {
        MediaNodeConfig cfg("TestPatternNode", "");
        cfg.set("Pattern", "colorbars");
        cfg.set("Width", uint32_t(1920));
        cfg.set("Height", uint32_t(1080));
        cfg.set("PixelFormat", PixelDesc(PixelDesc::RGB8_sRGB));
        cfg.set("FrameRate", FrameRate(FrameRate::FPS_2997));
        cfg.set("Motion", 0.0);
        cfg.set("StartTimecode", "00:00:00:00");
        cfg.set("DropFrame", false);
        cfg.set("AudioEnabled", true);
        cfg.set("AudioMode", "tone");
        cfg.set("AudioRate", 48000.0f);
        cfg.set("AudioChannels", 2);
        cfg.set("ToneFrequency", 1000.0);
        cfg.set("ToneLevel", -20.0);
        cfg.set("LtcLevel", -20.0);
        cfg.set("LtcChannel", 0);
        return cfg;
}

BuildResult TestPatternNode::build(const MediaNodeConfig &config) {
        BuildResult result;
        if(state() != Idle) {
                result.addError("Node is not in Idle state");
                return result;
        }

        // Parse pattern
        String patStr = config.get("Pattern", "colorbars").get<String>();
        if(!patStr.isEmpty()) {
                auto [pat, err] = VideoTestPattern::fromString(patStr);
                if(err.isError()) {
                        result.addError("Unknown pattern: " + patStr);
                        return result;
                }
                _videoPattern.setPattern(pat);
        }

        // Parse video config
        uint32_t width = config.get("Width", uint32_t(0)).get<uint32_t>();
        uint32_t height = config.get("Height", uint32_t(0)).get<uint32_t>();
        PixelDesc pd = config.get("PixelFormat", PixelDesc(PixelDesc::RGB8_sRGB)).get<PixelDesc>();
        PixelDesc::ID pixFmt = pd.id();

        FrameRate fps = config.get("FrameRate", FrameRate()).get<FrameRate>();
        if(!fps.isValid()) {
                result.addError("Invalid or missing frame rate");
                return result;
        }

        if(width > 0 && height > 0 && fps.isValid()) {
                _videoDesc = VideoDesc();
                _videoDesc.setFrameRate(fps);
                _videoDesc.imageList().pushToBack(ImageDesc(width, height, pixFmt));
        }

        // Solid color
        uint16_t solidR = config.get("SolidColorR", uint16_t(0)).get<uint16_t>();
        uint16_t solidG = config.get("SolidColorG", uint16_t(0)).get<uint16_t>();
        uint16_t solidB = config.get("SolidColorB", uint16_t(0)).get<uint16_t>();
        _videoPattern.setSolidColor(solidR, solidG, solidB);

        // Motion
        _motion = config.get("Motion", 0.0).get<double>();

        // Audio config
        _audioEnabled = config.get("AudioEnabled", true).get<bool>();
        float audioRate = config.get("AudioRate", 48000.0f).get<float>();
        int audioChannels = config.get("AudioChannels", 2).get<int>();

        if(_audioEnabled) {
                _audioDesc = AudioDesc(audioRate, audioChannels);
        }

        // Timecode
        String tcStr = config.get("StartTimecode", String()).get<String>();
        if(!tcStr.isEmpty()) {
                auto [tc, err] = Timecode::fromString(tcStr);
                if(err.isOk()) {
                        _tcGen.setTimecode(tc);
                }
        }
        // Accept a pre-built Timecode via Variant
        Variant tcVar = config.get("Timecode");
        if(tcVar.isValid()) {
                _tcGen.setTimecode(tcVar.get<Timecode>());
        }
        bool dropFrame = config.get("DropFrame", false).get<bool>();
        _tcGen.setDropFrame(dropFrame);

        // ---- Validate and configure ----

        if(!_videoDesc.isValid()) {
                result.addError("VideoDesc is not valid");
                return result;
        }
        if(_videoDesc.imageList().isEmpty()) {
                result.addError("VideoDesc has no image layers");
                return result;
        }

        _imageDesc = _videoDesc.imageList()[0];
        if(!_imageDesc.isValid()) {
                result.addError("ImageDesc is not valid");
                return result;
        }

        // Set up timecode generator from frame rate
        _tcGen.setFrameRate(_videoDesc.frameRate());

        // Set up audio
        if(_audioEnabled) {
                if(!_audioDesc.isValid()) {
                        _audioDesc = AudioDesc(48000.0f, 2);
                }

                // Compute samples per frame from video frame rate and audio sample rate
                double fpsVal = _videoDesc.frameRate().toDouble();
                if(fpsVal > 0.0) {
                        _samplesPerFrame = (size_t)std::round(_audioDesc.sampleRate() / fpsVal);
                } else {
                        _samplesPerFrame = 1600;
                }

                // Configure audio pattern generator
                delete _audioPattern;
                _audioPattern = new AudioTestPattern(_audioDesc);

                String audioModeStr = config.get("AudioMode", "tone").get<String>();
                if(!audioModeStr.isEmpty()) {
                        auto [mode, modeErr] = AudioTestPattern::fromString(audioModeStr);
                        if(modeErr.isError()) {
                                result.addError("Unknown audio mode: " + audioModeStr);
                                return result;
                        }
                        _audioPattern->setMode(mode);
                }

                double toneFreq = config.get("ToneFrequency", 1000.0).get<double>();
                double toneLevelDbfs = config.get("ToneLevel", -20.0).get<double>();
                double ltcLevelDbfs = config.get("LtcLevel", -20.0).get<double>();
                int ltcChannel = config.get("LtcChannel", 0).get<int>();

                _audioPattern->setToneFrequency(toneFreq);
                _audioPattern->setToneLevel(AudioLevel::fromDbfs(toneLevelDbfs));
                _audioPattern->setLtcLevel(AudioLevel::fromDbfs(ltcLevelDbfs));
                _audioPattern->setLtcChannel(ltcChannel);
                _audioPattern->configure();
        }

        _motionOffset = 0.0;
        _frameCount = 0;
        _cachedImage = Image();

        // Pre-render cached image for static patterns (no motion, not Noise)
        if(_motion == 0.0 && _videoPattern.pattern() != VideoTestPattern::Noise) {
                _cachedImage = _videoPattern.create(_imageDesc);
        }

        setState(Configured);
        return result;
}

Error TestPatternNode::start() {
        return MediaNode::start();
}

void TestPatternNode::processFrame(Frame::Ptr &frame, int inputIndex, DeliveryList &deliveries) {
        (void)frame;
        (void)inputIndex;

        // Advance timecode — returns the value for the current frame
        Timecode tc = _tcGen.advance();

        // Use cached image or render a new one
        Image img;
        if(_cachedImage.isValid()) {
                img = _cachedImage;
        } else {
                img = _videoPattern.create(_imageDesc, _motionOffset);
        }

        // Set metadata on the image
        img.metadata().set(Metadata::Timecode, tc);

        // Create frame
        Frame::Ptr outFrame = Frame::Ptr::create();
        outFrame.modify()->imageList().pushToBack(Image::Ptr::create(img));

        // Generate audio using AudioTestPattern
        if(_audioEnabled && _audioPattern != nullptr) {
                Audio audio = _audioPattern->create(_samplesPerFrame, tc);
                if(audio.isValid()) {
                        outFrame.modify()->audioList().pushToBack(Audio::Ptr::create(audio));
                }
        }

        // Set frame metadata
        outFrame.modify()->metadata().set(Metadata::Timecode, tc);

        // Deliver to all outputs
        deliveries.pushToBack(Delivery{-1, outFrame});

        // Advance motion
        if(_motion != 0.0) {
                double fpsVal = _videoDesc.frameRate().toDouble();
                if(fpsVal > 0.0) {
                        _motionOffset += _motion * (double)_imageDesc.size().width() / fpsVal;
                        // Wrap at pattern period (use image width)
                        double period = (double)_imageDesc.size().width();
                        if(period > 0.0) {
                                while(_motionOffset >= period) _motionOffset -= period;
                                while(_motionOffset < 0.0) _motionOffset += period;
                        }
                }
        }

        _frameCount++;

        // Update thread-safe stats snapshot
        {
                Mutex::Locker lock(_statsMutex);
                _statsFrameCount = _frameCount;
                _statsTimecode = tc;
        }
}

void TestPatternNode::stop() {
        MediaNode::stop();
        delete _audioPattern;
        _audioPattern = nullptr;
        _frameCount = 0;
        _motionOffset = 0.0;
        _cachedImage = Image();
        _tcGen.reset();
        {
                Mutex::Locker lock(_statsMutex);
                _statsFrameCount = 0;
                _statsTimecode = Timecode();
        }
}

Map<String, Variant> TestPatternNode::extendedStats() const {
        Mutex::Locker lock(_statsMutex);
        Map<String, Variant> ret;
        ret.insert("FramesGenerated", Variant(_statsFrameCount));
        ret.insert("CurrentTimecode", Variant(_statsTimecode.toString().first));
        return ret;
}

PROMEKI_NAMESPACE_END
