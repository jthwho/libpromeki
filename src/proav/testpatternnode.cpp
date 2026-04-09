/**
 * @file      testpatternnode.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/testpatternnode.h>
#include <promeki/medianodeconfig.h>
#include <promeki/frame.h>
#include <promeki/pixeldesc.h>
#include <promeki/metadata.h>
#include <promeki/timecode.h>
#include <promeki/enums.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_REGISTER_NODE(TestPatternNode)

TestPatternNode::TestPatternNode(ObjectBase *parent) : MediaNode(parent) {
        setName("TestPatternNode");
        auto output = MediaSource::Ptr::create("output", ContentHint(ContentVideo | ContentAudio));
        addSource(output);
}

TestPatternNode::~TestPatternNode() {
        delete _io;
}

MediaNodeConfig TestPatternNode::defaultConfig() const {
        MediaNodeConfig cfg("TestPatternNode", "");
        cfg.set("Pattern", VideoPattern::ColorBars);
        cfg.set("Size", Size2Du32(1920, 1080));
        cfg.set("PixelFormat", PixelDesc(PixelDesc::RGB8_sRGB));
        cfg.set("FrameRate", FrameRate(FrameRate::FPS_2997));
        cfg.set("Motion", 0.0);
        cfg.set("StartTimecode", "00:00:00:00");
        cfg.set("DropFrame", false);
        cfg.set("AudioEnabled", true);
        cfg.set("AudioMode", AudioPattern::Tone);
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

        // Translate MediaNodeConfig into MediaIO::Config for the TPG
        MediaIO::Config tpgCfg;

        // Frame rate (required)
        FrameRate fps = config.get("FrameRate", FrameRate()).get<FrameRate>();
        if(!fps.isValid()) {
                result.addError("Invalid or missing frame rate");
                return result;
        }
        tpgCfg.set(MediaConfig::FrameRate, fps);

        // Video — always enabled for TestPatternNode
        tpgCfg.set(MediaConfig::VideoEnabled, true);

        // Forward the Pattern variant as-is; MediaIOTask_TPG resolves it
        // via Variant::asEnum(VideoPattern::Type) so strings and Enums
        // both work.
        tpgCfg.set(MediaConfig::VideoPattern,
                   config.get("Pattern", VideoPattern::ColorBars));

        Size2Du32 size = config.get("Size", Size2Du32()).get<Size2Du32>();
        tpgCfg.set(MediaConfig::VideoSize, size);

        PixelDesc pd = config.get("PixelFormat", PixelDesc(PixelDesc::RGB8_sRGB)).get<PixelDesc>();
        tpgCfg.set(MediaConfig::VideoPixelFormat, pd);

        tpgCfg.set(MediaConfig::VideoSolidColor, config.get("SolidColor", Color::Black).get<Color>());
        tpgCfg.set(MediaConfig::VideoMotion, config.get("Motion", 0.0).get<double>());

        // Audio
        bool audioEnabled = config.get("AudioEnabled", true).get<bool>();
        tpgCfg.set(MediaConfig::AudioEnabled, audioEnabled);
        if(audioEnabled) {
                // Forward as Variant; MediaIOTask_TPG calls asEnum() on it.
                tpgCfg.set(MediaConfig::AudioMode,
                           config.get("AudioMode", AudioPattern::Tone));
                tpgCfg.set(MediaConfig::AudioRate, config.get("AudioRate", 48000.0f).get<float>());
                tpgCfg.set(MediaConfig::AudioChannels, config.get("AudioChannels", 2).get<int>());
                tpgCfg.set(MediaConfig::AudioToneFrequency, config.get("ToneFrequency", 1000.0).get<double>());
                tpgCfg.set(MediaConfig::AudioToneLevel, config.get("ToneLevel", -20.0).get<double>());
                tpgCfg.set(MediaConfig::AudioLtcLevel, config.get("LtcLevel", -20.0).get<double>());
                tpgCfg.set(MediaConfig::AudioLtcChannel, config.get("LtcChannel", 0).get<int>());
        }

        // Timecode — always enabled for TestPatternNode
        tpgCfg.set(MediaConfig::TimecodeEnabled, true);
        tpgCfg.set(MediaConfig::TimecodeDropFrame, config.get("DropFrame", false).get<bool>());

        String tcStr = config.get("StartTimecode", String()).get<String>();
        if(!tcStr.isEmpty()) {
                tpgCfg.set(MediaConfig::TimecodeStart, tcStr);
        }

        // Accept a pre-built Timecode via Variant
        Variant tcVar = config.get("Timecode");
        if(tcVar.isValid()) {
                tpgCfg.set(MediaConfig::TimecodeValue, tcVar);
        }

        // Create and open the TPG via the MediaIO factory
        tpgCfg.set(MediaConfig::Type, "TPG");
        delete _io;
        _io = MediaIO::create(tpgCfg, this);
        if(_io == nullptr) {
                result.addError("Failed to create TPG MediaIO");
                return result;
        }

        Error err = _io->open(MediaIO::Reader);
        if(err.isError()) {
                result.addError("Failed to open TPG: " + err.name());
                delete _io;
                _io = nullptr;
                return result;
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

        Frame::Ptr outFrame;
        Error err = _io->readFrame(outFrame);
        if(err.isError()) return;

        // Deliver to all outputs
        deliveries.pushToBack(Delivery{-1, outFrame});

        // Update thread-safe stats snapshot
        {
                Mutex::Locker lock(_statsMutex);
                _statsFrameCount = _io->currentFrame();
                Variant tcVar = outFrame->metadata().get(Metadata::Timecode);
                if(tcVar.isValid()) {
                        _statsTimecode = tcVar.get<Timecode>();
                }
        }
}

void TestPatternNode::stop() {
        MediaNode::stop();
        if(_io != nullptr) {
                _io->close();
                delete _io;
                _io = nullptr;
        }
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
        ret.insert("CurrentTimecode", Variant(_statsTimecode.toString().first()));
        return ret;
}

PROMEKI_NAMESPACE_END
