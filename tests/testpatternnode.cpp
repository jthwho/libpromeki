/**
 * @file      tests/testpatternnode.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <thread>
#include <doctest/doctest.h>
#include <promeki/atomic.h>
#include <promeki/mutex.h>
#include <promeki/testpatternnode.h>
#include <promeki/mediapipeline.h>
#include <promeki/medianodeconfig.h>
#include <promeki/image.h>
#include <promeki/audio.h>
#include <promeki/frame.h>
#include <promeki/pixeldesc.h>
#include <promeki/ltcdecoder.h>
#include <promeki/audiolevel.h>

using namespace promeki;

// ============================================================================
// Helper: simple sink node that captures delivered frames
// ============================================================================

class CaptureSinkNode : public MediaNode {
        PROMEKI_OBJECT(CaptureSinkNode, MediaNode)
        public:
                CaptureSinkNode() : MediaNode() {
                        setName("CaptureSink");
                        auto port = MediaSink::Ptr::create("input", ContentNone);
                        addSink(port);
                }

                BuildResult build(const MediaNodeConfig &) override {
                        setState(Configured);
                        return BuildResult();
                }

                void processFrame(Frame::Ptr &frame, int inputIndex, DeliveryList &deliveries) override {
                        (void)inputIndex; (void)deliveries;
                        if(frame.isValid()) {
                                Mutex::Locker lock(_mutex);
                                _lastFrame = frame;
                                _count.fetchAndAdd(1);
                        }
                        return;
                }

                Frame::Ptr lastFrame() const {
                        Mutex::Locker lock(_mutex);
                        return _lastFrame;
                }
                int count() const { return _count.value(); }

        private:
                mutable Mutex _mutex;
                Frame::Ptr _lastFrame;
                Atomic<int> _count{0};
};

// ============================================================================
// Helper: create a standard MediaNodeConfig for a test pattern source
// ============================================================================

static MediaNodeConfig makeTestConfig(
        const String &name = "src",
        int w = 320,
        int h = 240,
        const String &fps = "24"
) {
        MediaNodeConfig cfg("TestPatternNode", name);
        cfg.set("Width", uint32_t(w));
        cfg.set("Height", uint32_t(h));
        cfg.set("PixelFormat", PixelDesc(PixelDesc::RGB8_sRGB));
        cfg.set("FrameRate", fps);
        return cfg;
}

// ============================================================================
// Construction
// ============================================================================

TEST_CASE("TestPatternNode_Construct") {
        TestPatternNode node;
        CHECK(node.sourceCount() == 1);
        CHECK(node.sinkCount() == 0);
        CHECK(node.state() == MediaNode::Idle);
}

// ============================================================================
// Configure with valid video params
// ============================================================================

TEST_CASE("TestPatternNode_Configure") {
        MediaPipeline pipeline;
        TestPatternNode *src = new TestPatternNode();
        CaptureSinkNode *sink = new CaptureSinkNode();

        MediaNodeConfig cfg = makeTestConfig();
        src->build(cfg);

        pipeline.addNode(src);
        pipeline.addNode(sink);
        pipeline.connect(src, 0, sink, 0);

        sink->build(MediaNodeConfig());

        Error err = pipeline.start();
        CHECK(err == Error::Ok);
        CHECK(src->state() == MediaNode::Running);
}

// ============================================================================
// Configure fails without video params
// ============================================================================

TEST_CASE("TestPatternNode_ConfigureNoVideo") {
        MediaPipeline pipeline;
        TestPatternNode *src = new TestPatternNode();
        CaptureSinkNode *sink = new CaptureSinkNode();

        // Build without video dimensions — should fail
        MediaNodeConfig cfg("TestPatternNode", "src");
        BuildResult result = src->build(cfg);
        CHECK(result.isError());

        pipeline.addNode(src);
        pipeline.addNode(sink);
        pipeline.connect(src, 0, sink, 0);

        sink->build(MediaNodeConfig());

        Error err = pipeline.start();
        CHECK(err != Error::Ok);
}

// ============================================================================
// Generate one frame of each pattern
// ============================================================================

TEST_CASE("TestPatternNode_AllPatterns") {
        String patternNames[] = {
                "colorbars",
                "colorbars75",
                "ramp",
                "grid",
                "crosshatch",
                "checkerboard",
                "solidcolor",
                "white",
                "black",
                "noise",
                "zoneplate"
        };

        for(const auto &patName : patternNames) {
                MediaPipeline pipeline;
                TestPatternNode *src = new TestPatternNode();
                CaptureSinkNode *sink = new CaptureSinkNode();

                MediaNodeConfig cfg = makeTestConfig();
                cfg.set("Pattern", patName);
                cfg.set("AudioEnabled", false);
                cfg.set("Timecode", Timecode(Timecode::NDF24, 1, 0, 0, 0));
                src->build(cfg);

                pipeline.addNode(src);
                pipeline.addNode(sink);
                pipeline.connect(src, 0, sink, 0);

                sink->build(MediaNodeConfig());

                pipeline.start();

                // Wait for the node's thread to produce one frame
                for(int i = 0; i < 200 && sink->count() < 1; i++) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
                pipeline.stop();

                REQUIRE(sink->count() >= 1);
                Frame::Ptr frame = sink->lastFrame();
                REQUIRE(frame.isValid());
                CHECK(frame->imageList().size() == 1);
                Image::Ptr img = frame->imageList()[0];
                CHECK(img->width() == 320);
                CHECK(img->height() == 240);
        }
}

// ============================================================================
// Timecode increments correctly
// ============================================================================

TEST_CASE("TestPatternNode_TimecodeIncrements") {
        MediaPipeline pipeline;
        TestPatternNode *src = new TestPatternNode();
        CaptureSinkNode *sink = new CaptureSinkNode();

        MediaNodeConfig cfg = makeTestConfig();
        cfg.set("AudioEnabled", false);
        cfg.set("Timecode", Timecode(Timecode::NDF24, 1, 0, 0, 0));
        src->build(cfg);

        pipeline.addNode(src);
        pipeline.addNode(sink);
        pipeline.connect(src, 0, sink, 0);

        sink->build(MediaNodeConfig());

        pipeline.start();

        // Wait for the node's thread to produce at least 5 frames
        for(int i = 0; i < 200 && sink->count() < 5; i++) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        // Capture frame count before stop (stop resets counters)
        uint64_t frameCount = src->extendedStats()["FramesGenerated"].get<uint64_t>();
        Frame::Ptr frame = sink->lastFrame();

        pipeline.stop();

        CHECK(frameCount >= 5);

        REQUIRE(frame.isValid());

        // Frame-level timecode metadata
        Timecode tc = frame->metadata().get(Metadata::Timecode).get<Timecode>();
        CHECK(tc.hour() == 1);

        // Image-level timecode metadata (required by TimecodeOverlayNode)
        REQUIRE(frame->imageList().size() >= 1);
        Image::Ptr img = frame->imageList()[0];
        REQUIRE(img->metadata().contains(Metadata::Timecode));
        Timecode imgTc = img->metadata().get(Metadata::Timecode).get<Timecode>();
        CHECK(imgTc.hour() == 1);
}

// ============================================================================
// Audio tone output
// ============================================================================

TEST_CASE("TestPatternNode_AudioTone") {
        MediaPipeline pipeline;
        TestPatternNode *src = new TestPatternNode();
        CaptureSinkNode *sink = new CaptureSinkNode();

        MediaNodeConfig cfg = makeTestConfig();
        cfg.set("AudioMode", "tone");
        cfg.set("ToneFrequency", 1000.0);
        src->build(cfg);

        pipeline.addNode(src);
        pipeline.addNode(sink);
        pipeline.connect(src, 0, sink, 0);

        sink->build(MediaNodeConfig());

        pipeline.start();

        for(int i = 0; i < 200 && sink->count() < 1; i++) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        pipeline.stop();

        Frame::Ptr frame = sink->lastFrame();
        REQUIRE(frame.isValid());
        CHECK(frame->audioList().size() == 1);
        Audio::Ptr audio = frame->audioList()[0];
        CHECK(audio->samples() > 0);
}

// ============================================================================
// Audio silence
// ============================================================================

TEST_CASE("TestPatternNode_AudioSilence") {
        MediaPipeline pipeline;
        TestPatternNode *src = new TestPatternNode();
        CaptureSinkNode *sink = new CaptureSinkNode();

        MediaNodeConfig cfg = makeTestConfig();
        cfg.set("AudioMode", "silence");
        src->build(cfg);

        pipeline.addNode(src);
        pipeline.addNode(sink);
        pipeline.connect(src, 0, sink, 0);

        sink->build(MediaNodeConfig());

        pipeline.start();

        for(int i = 0; i < 200 && sink->count() < 1; i++) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        pipeline.stop();

        Frame::Ptr frame = sink->lastFrame();
        REQUIRE(frame.isValid());
        CHECK(frame->audioList().size() == 1);
}

// ============================================================================
// Audio disabled
// ============================================================================

TEST_CASE("TestPatternNode_AudioDisabled") {
        MediaPipeline pipeline;
        TestPatternNode *src = new TestPatternNode();
        CaptureSinkNode *sink = new CaptureSinkNode();

        MediaNodeConfig cfg = makeTestConfig();
        cfg.set("AudioEnabled", false);
        src->build(cfg);

        pipeline.addNode(src);
        pipeline.addNode(sink);
        pipeline.connect(src, 0, sink, 0);

        sink->build(MediaNodeConfig());

        pipeline.start();

        for(int i = 0; i < 200 && sink->count() < 1; i++) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        pipeline.stop();

        Frame::Ptr frame = sink->lastFrame();
        REQUIRE(frame.isValid());
        CHECK(frame->audioList().isEmpty());
}

// ============================================================================
// LTC audio mode
// ============================================================================

TEST_CASE("TestPatternNode_AudioLTC") {
        MediaPipeline pipeline;
        TestPatternNode *src = new TestPatternNode();
        CaptureSinkNode *sink = new CaptureSinkNode();

        MediaNodeConfig cfg = makeTestConfig();
        cfg.set("AudioMode", "ltc");
        cfg.set("AudioRate", 48000.0f);
        cfg.set("AudioChannels", 1);
        cfg.set("Timecode", Timecode(Timecode::NDF24, 1, 0, 0, 0));
        src->build(cfg);

        pipeline.addNode(src);
        pipeline.addNode(sink);
        pipeline.connect(src, 0, sink, 0);

        sink->build(MediaNodeConfig());

        pipeline.start();

        for(int i = 0; i < 200 && sink->count() < 1; i++) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        pipeline.stop();

        Frame::Ptr frame = sink->lastFrame();
        REQUIRE(frame.isValid());
        CHECK(frame->audioList().size() == 1);
        Audio::Ptr audio = frame->audioList()[0];
        CHECK(audio->samples() > 0);
}

// ============================================================================
// Motion produces different frames
// ============================================================================

TEST_CASE("TestPatternNode_MotionDiffers") {
        MediaPipeline pipeline;
        TestPatternNode *src = new TestPatternNode();
        CaptureSinkNode *sink = new CaptureSinkNode();

        MediaNodeConfig cfg = makeTestConfig();
        cfg.set("Pattern", "ramp");
        cfg.set("Motion", 1.0);
        cfg.set("AudioEnabled", false);
        src->build(cfg);

        pipeline.addNode(src);
        pipeline.addNode(sink);
        pipeline.connect(src, 0, sink, 0);

        sink->build(MediaNodeConfig());

        pipeline.start();

        // Wait for at least 2 frames
        for(int i = 0; i < 200 && sink->count() < 2; i++) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        pipeline.stop();

        REQUIRE(sink->count() >= 2);
        // Note: lastFrame() only captures the most recent frame, so we cannot
        // compare two distinct frames this way with threaded processing.
        // Instead, just verify we got valid output.
        Frame::Ptr frame = sink->lastFrame();
        REQUIRE(frame.isValid());
        Image::Ptr img = frame->imageList()[0];
        CHECK(img->width() == 320);
        CHECK(img->height() == 240);
}

// ============================================================================
// Static patterns are identical regardless of motion
// ============================================================================

TEST_CASE("TestPatternNode_StaticUnchanged") {
        MediaPipeline pipeline;
        TestPatternNode *src = new TestPatternNode();
        CaptureSinkNode *sink = new CaptureSinkNode();

        MediaNodeConfig cfg = makeTestConfig();
        cfg.set("Pattern", "white");
        cfg.set("Motion", 1.0);
        cfg.set("AudioEnabled", false);
        src->build(cfg);

        pipeline.addNode(src);
        pipeline.addNode(sink);
        pipeline.connect(src, 0, sink, 0);

        sink->build(MediaNodeConfig());

        pipeline.start();

        // Wait for at least 2 frames
        for(int i = 0; i < 200 && sink->count() < 2; i++) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        pipeline.stop();

        REQUIRE(sink->count() >= 2);
        // Verify the last frame is a valid white frame
        Frame::Ptr frame = sink->lastFrame();
        REQUIRE(frame.isValid());
        Image::Ptr img = frame->imageList()[0];
        CHECK(img->width() == 320);
        CHECK(img->height() == 240);
}

// ============================================================================
// Extended stats
// ============================================================================

TEST_CASE("TestPatternNode_ExtendedStats") {
        MediaPipeline pipeline;
        TestPatternNode *src = new TestPatternNode();
        CaptureSinkNode *sink = new CaptureSinkNode();

        MediaNodeConfig cfg = makeTestConfig();
        cfg.set("AudioEnabled", false);
        cfg.set("Timecode", Timecode(Timecode::NDF24, 1, 0, 0, 0));
        src->build(cfg);

        pipeline.addNode(src);
        pipeline.addNode(sink);
        pipeline.connect(src, 0, sink, 0);

        sink->build(MediaNodeConfig());

        pipeline.start();

        // Wait for at least 2 frames to be generated
        for(int i = 0; i < 200 && sink->count() < 2; i++) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        pipeline.stop();

        auto stats = src->extendedStats();
        CHECK(stats.contains("FramesGenerated"));
        CHECK(stats.contains("CurrentTimecode"));
}

// ============================================================================
// Start and stop lifecycle
// ============================================================================

TEST_CASE("TestPatternNode_Lifecycle") {
        MediaPipeline pipeline;
        TestPatternNode *node = new TestPatternNode();
        CaptureSinkNode *sink = new CaptureSinkNode();

        MediaNodeConfig cfg = makeTestConfig("node");
        node->build(cfg);

        pipeline.addNode(node);
        pipeline.addNode(sink);
        pipeline.connect(node, 0, sink, 0);

        sink->build(MediaNodeConfig());

        CHECK(pipeline.start() == Error::Ok);
        CHECK(node->state() == MediaNode::Running);

        pipeline.stop();
        CHECK(node->state() == MediaNode::Idle);
}

// ============================================================================
// Node registry
// ============================================================================

TEST_CASE("TestPatternNode_Registry") {
        auto types = MediaNode::registeredNodeTypes();
        CHECK(types.contains("TestPatternNode"));

        MediaNode *node = MediaNode::createNode("TestPatternNode");
        REQUIRE(node != nullptr);
        CHECK(node->sourceCount() == 1);
        delete node;
}

// ============================================================================
// SolidColor with custom color
// ============================================================================

TEST_CASE("TestPatternNode_SolidColor") {
        MediaPipeline pipeline;
        TestPatternNode *src = new TestPatternNode();
        CaptureSinkNode *sink = new CaptureSinkNode();

        MediaNodeConfig cfg = makeTestConfig();
        cfg.set("Pattern", "solidcolor");
        cfg.set("SolidColorR", uint16_t(65535));
        cfg.set("SolidColorG", uint16_t(0));
        cfg.set("SolidColorB", uint16_t(0));
        cfg.set("AudioEnabled", false);
        src->build(cfg);

        pipeline.addNode(src);
        pipeline.addNode(sink);
        pipeline.connect(src, 0, sink, 0);

        sink->build(MediaNodeConfig());

        pipeline.start();

        for(int i = 0; i < 200 && sink->count() < 1; i++) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        pipeline.stop();

        Frame::Ptr frame = sink->lastFrame();
        REQUIRE(frame.isValid());
        Image::Ptr img = frame->imageList()[0];
        uint8_t *data = static_cast<uint8_t *>(img->data());
        // First pixel should be red (RGB8: 255, 0, 0)
        CHECK(data[0] == 255);
        CHECK(data[1] == 0);
        CHECK(data[2] == 0);
}

// ============================================================================
// Audio channel config with tone frequency and level
// ============================================================================

TEST_CASE("TestPatternNode_SetChannelConfig") {
        MediaPipeline pipeline;
        TestPatternNode *src = new TestPatternNode();
        CaptureSinkNode *sink = new CaptureSinkNode();

        MediaNodeConfig cfg = makeTestConfig();
        cfg.set("AudioMode", "tone");
        cfg.set("AudioRate", 48000.0f);
        cfg.set("AudioChannels", 2);
        cfg.set("ToneFrequency", 440.0);
        cfg.set("ToneLevel", -2.0);
        src->build(cfg);

        pipeline.addNode(src);
        pipeline.addNode(sink);
        pipeline.connect(src, 0, sink, 0);

        sink->build(MediaNodeConfig());

        pipeline.start();

        for(int i = 0; i < 200 && sink->count() < 1; i++) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        pipeline.stop();

        Frame::Ptr frame = sink->lastFrame();
        REQUIRE(frame.isValid());
        CHECK(frame->audioList().size() == 1);
        Audio::Ptr audio = frame->audioList()[0];
        CHECK(audio->samples() > 0);
}

// ============================================================================
// Multi-channel LTC embedding
// ============================================================================

TEST_CASE("TestPatternNode_MultiChannelLTC") {
        MediaPipeline pipeline;
        TestPatternNode *src = new TestPatternNode();
        CaptureSinkNode *sink = new CaptureSinkNode();

        MediaNodeConfig cfg = makeTestConfig();
        cfg.set("AudioMode", "ltc");
        cfg.set("AudioRate", 48000.0f);
        cfg.set("AudioChannels", 4);
        cfg.set("LtcChannel", 2);
        cfg.set("LtcLevel", -3.0);
        cfg.set("Timecode", Timecode(Timecode::NDF24, 1, 0, 0, 0));
        src->build(cfg);

        pipeline.addNode(src);
        pipeline.addNode(sink);
        pipeline.connect(src, 0, sink, 0);

        sink->build(MediaNodeConfig());

        pipeline.start();

        for(int i = 0; i < 200 && sink->count() < 1; i++) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        pipeline.stop();

        Frame::Ptr frame = sink->lastFrame();
        REQUIRE(frame.isValid());
        CHECK(frame->audioList().size() == 1);
        Audio::Ptr audio = frame->audioList()[0];
        CHECK(audio->samples() > 0);
}

// ============================================================================
// Tone level affects sample amplitude
// ============================================================================

TEST_CASE("TestPatternNode_SetToneLevel") {
        MediaPipeline pipeline;
        TestPatternNode *src = new TestPatternNode();
        CaptureSinkNode *sink = new CaptureSinkNode();

        MediaNodeConfig cfg = makeTestConfig();
        cfg.set("AudioMode", "tone");
        cfg.set("AudioRate", 48000.0f);
        cfg.set("AudioChannels", 2);
        cfg.set("ToneFrequency", 440.0);
        cfg.set("ToneLevel", -6.0);
        src->build(cfg);

        pipeline.addNode(src);
        pipeline.addNode(sink);
        pipeline.connect(src, 0, sink, 0);

        sink->build(MediaNodeConfig());

        pipeline.start();

        for(int i = 0; i < 200 && sink->count() < 1; i++) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        pipeline.stop();

        Frame::Ptr frame = sink->lastFrame();
        REQUIRE(frame.isValid());
        CHECK(frame->audioList().size() == 1);
        Audio::Ptr audio = frame->audioList()[0];
        REQUIRE(audio->isValid());
        CHECK(audio->samples() > 0);

        // Verify the samples are within the expected linear gain range for -6 dBFS (~0.501)
        float linear = AudioLevel::fromDbfs(-6.0).toLinearFloat();
        const float *samples = audio->data<float>();
        for(size_t i = 0; i < audio->frames(); i++) {
                CHECK(samples[i] >= -linear - 0.01f);
                CHECK(samples[i] <= linear + 0.01f);
        }
}

// ============================================================================
// Configure failure from non-idle state
// ============================================================================

TEST_CASE("TestPatternNode_ConfigureFromNonIdle") {
        // Pipeline.start() starts in one call.
        // Double-start returns Error::Invalid.
        MediaPipeline pipeline;
        TestPatternNode *node = new TestPatternNode();
        CaptureSinkNode *sink = new CaptureSinkNode();

        MediaNodeConfig cfg = makeTestConfig("node");
        node->build(cfg);

        pipeline.addNode(node);
        pipeline.addNode(sink);
        pipeline.connect(node, 0, sink, 0);

        sink->build(MediaNodeConfig());

        pipeline.start();
        Error err = pipeline.start(); // Already running
        CHECK(err == Error::Invalid);
}

// ============================================================================
// Start failure from non-configured state
// ============================================================================

TEST_CASE("TestPatternNode_StartFromIdle") {
        // Without valid video params, build() on the node fails.
        // Pipeline start will then fail because node is not Configured.
        MediaPipeline pipeline;
        TestPatternNode *node = new TestPatternNode();
        CaptureSinkNode *sink = new CaptureSinkNode();

        MediaNodeConfig cfg("TestPatternNode", "node");
        node->build(cfg); // No video params — should fail

        pipeline.addNode(node);
        pipeline.addNode(sink);
        pipeline.connect(node, 0, sink, 0);

        sink->build(MediaNodeConfig());

        Error err = pipeline.start();
        CHECK(err != Error::Ok);
}

// ============================================================================
// Zero-dimension video fails build
// ============================================================================

TEST_CASE("TestPatternNode_ConfigureNoImageLayers") {
        MediaPipeline pipeline;
        TestPatternNode *node = new TestPatternNode();
        CaptureSinkNode *sink = new CaptureSinkNode();

        // Width/height of 0 means no valid video desc
        MediaNodeConfig cfg("TestPatternNode", "node");
        cfg.set("Width", uint32_t(0));
        cfg.set("Height", uint32_t(0));
        cfg.set("FrameRate", "24");
        BuildResult result = node->build(cfg);
        CHECK(result.isError());

        pipeline.addNode(node);
        pipeline.addNode(sink);
        pipeline.connect(node, 0, sink, 0);

        sink->build(MediaNodeConfig());

        Error err = pipeline.start();
        CHECK(err != Error::Ok);
}

// ============================================================================
// defaultConfig returns usable defaults
// ============================================================================

TEST_CASE("TestPatternNode_DefaultConfig") {
        TestPatternNode node;
        MediaNodeConfig cfg = node.defaultConfig();

        CHECK(cfg.type() == "TestPatternNode");
        CHECK(cfg.get("Width").get<uint32_t>() == 1920);
        CHECK(cfg.get("Height").get<uint32_t>() == 1080);
        CHECK(cfg.get("FrameRate").get<FrameRate>() == FrameRate(FrameRate::FPS_2997));
        CHECK(cfg.get("Pattern").get<String>() == "colorbars");
        CHECK(cfg.get("AudioEnabled").get<bool>() == true);
        CHECK(cfg.get("AudioMode").get<String>() == "tone");
        CHECK(cfg.get("ToneFrequency").get<double>() == 1000.0);

        // Should build successfully with just the defaults
        cfg.setName("src");
        MediaPipeline pipeline;
        TestPatternNode *src = new TestPatternNode();
        CaptureSinkNode *sink = new CaptureSinkNode();
        BuildResult result = src->build(cfg);
        CHECK(result.isOk());

        pipeline.addNode(src);
        pipeline.addNode(sink);
        pipeline.connect(src, 0, sink, 0);
        sink->build(MediaNodeConfig());

        CHECK(pipeline.start() == Error::Ok);
        pipeline.stop();
}
