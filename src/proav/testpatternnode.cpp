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

PROMEKI_NAMESPACE_BEGIN

PROMEKI_REGISTER_NODE(TestPatternNode)

TestPatternNode::TestPatternNode(ObjectBase *parent) : MediaNode(parent) {
        setName("TestPatternNode");
        auto output = MediaSource::Ptr::create("output", ContentHint(ContentVideo | ContentAudio));
        addSource(output);
}

TestPatternNode::~TestPatternNode() {
        delete _tpg;
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

        // Translate MediaNodeConfig into MediaIO::Config for the TPG
        MediaIO::Config tpgCfg;

        // Frame rate (required)
        FrameRate fps = config.get("FrameRate", FrameRate()).get<FrameRate>();
        if(!fps.isValid()) {
                result.addError("Invalid or missing frame rate");
                return result;
        }
        tpgCfg.set(MediaIO_TPG::ConfigFrameRate, fps);

        // Video — always enabled for TestPatternNode
        tpgCfg.set(MediaIO_TPG::ConfigVideoEnabled, true);

        String patStr = config.get("Pattern", "colorbars").get<String>();
        tpgCfg.set(MediaIO_TPG::ConfigVideoPattern, patStr);

        uint32_t width = config.get("Width", uint32_t(0)).get<uint32_t>();
        uint32_t height = config.get("Height", uint32_t(0)).get<uint32_t>();
        tpgCfg.set(MediaIO_TPG::ConfigVideoWidth, (int)width);
        tpgCfg.set(MediaIO_TPG::ConfigVideoHeight, (int)height);

        PixelDesc pd = config.get("PixelFormat", PixelDesc(PixelDesc::RGB8_sRGB)).get<PixelDesc>();
        tpgCfg.set(MediaIO_TPG::ConfigVideoPixelFormat, pd);

        tpgCfg.set(MediaIO_TPG::ConfigVideoSolidColorR, config.get("SolidColorR", uint16_t(0)).get<uint16_t>());
        tpgCfg.set(MediaIO_TPG::ConfigVideoSolidColorG, config.get("SolidColorG", uint16_t(0)).get<uint16_t>());
        tpgCfg.set(MediaIO_TPG::ConfigVideoSolidColorB, config.get("SolidColorB", uint16_t(0)).get<uint16_t>());
        tpgCfg.set(MediaIO_TPG::ConfigVideoMotion, config.get("Motion", 0.0).get<double>());

        // Audio
        bool audioEnabled = config.get("AudioEnabled", true).get<bool>();
        tpgCfg.set(MediaIO_TPG::ConfigAudioEnabled, audioEnabled);
        if(audioEnabled) {
                tpgCfg.set(MediaIO_TPG::ConfigAudioMode, config.get("AudioMode", "tone").get<String>());
                tpgCfg.set(MediaIO_TPG::ConfigAudioRate, config.get("AudioRate", 48000.0f).get<float>());
                tpgCfg.set(MediaIO_TPG::ConfigAudioChannels, config.get("AudioChannels", 2).get<int>());
                tpgCfg.set(MediaIO_TPG::ConfigAudioToneFrequency, config.get("ToneFrequency", 1000.0).get<double>());
                tpgCfg.set(MediaIO_TPG::ConfigAudioToneLevel, config.get("ToneLevel", -20.0).get<double>());
                tpgCfg.set(MediaIO_TPG::ConfigAudioLtcLevel, config.get("LtcLevel", -20.0).get<double>());
                tpgCfg.set(MediaIO_TPG::ConfigAudioLtcChannel, config.get("LtcChannel", 0).get<int>());
        }

        // Timecode — always enabled for TestPatternNode
        tpgCfg.set(MediaIO_TPG::ConfigTimecodeEnabled, true);
        tpgCfg.set(MediaIO_TPG::ConfigTimecodeDropFrame, config.get("DropFrame", false).get<bool>());

        String tcStr = config.get("StartTimecode", String()).get<String>();
        if(!tcStr.isEmpty()) {
                tpgCfg.set(MediaIO_TPG::ConfigTimecodeStart, tcStr);
        }

        // Accept a pre-built Timecode via Variant
        Variant tcVar = config.get("Timecode");
        if(tcVar.isValid()) {
                tpgCfg.set(MediaIO_TPG::ConfigTimecodeValue, tcVar);
        }

        // Create and open the TPG
        delete _tpg;
        _tpg = new MediaIO_TPG(this);
        _tpg->setConfig(tpgCfg);

        Error err = _tpg->open(MediaIO::Reader);
        if(err.isError()) {
                result.addError("Failed to open TPG: " + err.name());
                delete _tpg;
                _tpg = nullptr;
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

        Frame::Ptr outFrame = Frame::Ptr::create();
        Error err = _tpg->readFrame(*outFrame.modify());
        if(err.isError()) return;

        // Deliver to all outputs
        deliveries.pushToBack(Delivery{-1, outFrame});

        // Update thread-safe stats snapshot
        {
                Mutex::Locker lock(_statsMutex);
                _statsFrameCount = _tpg->currentFrame();
                Variant tcVar = outFrame->metadata().get(Metadata::Timecode);
                if(tcVar.isValid()) {
                        _statsTimecode = tcVar.get<Timecode>();
                }
        }
}

void TestPatternNode::stop() {
        MediaNode::stop();
        if(_tpg != nullptr) {
                _tpg->close();
                delete _tpg;
                _tpg = nullptr;
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
        ret.insert("CurrentTimecode", Variant(_statsTimecode.toString().first));
        return ret;
}

PROMEKI_NAMESPACE_END
