/**
 * @file      tests/testpatternnode.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/proav/testpatternnode.h>
#include <promeki/proav/mediagraph.h>
#include <promeki/proav/image.h>
#include <promeki/proav/audio.h>
#include <promeki/proav/frame.h>
#include <promeki/proav/pixelformat.h>
#include <promeki/proav/ltcdecoder.h>

using namespace promeki;

// ============================================================================
// Helper: simple sink node that captures delivered frames
// ============================================================================

class CaptureSinkNode : public MediaNode {
        PROMEKI_OBJECT(CaptureSinkNode, MediaNode)
        public:
                CaptureSinkNode() : MediaNode() {
                        setName("CaptureSink");
                        auto port = MediaPort::Ptr::create("input", MediaPort::Input, MediaPort::Frame);
                        addInputPort(port);
                }

                void process() override {
                        Frame::Ptr frame = dequeueInput();
                        if(frame.isValid()) {
                                _lastFrame = frame;
                                _count++;
                        }
                        return;
                }

                void drain() {
                        while(queuedFrameCount() > 0) {
                                process();
                        }
                        return;
                }

                Frame::Ptr lastFrame() const { return _lastFrame; }
                int capturedCount() const { return _count; }

        private:
                Frame::Ptr _lastFrame;
                int _count = 0;
};

// ============================================================================
// Helper: create a standard VideoDesc for testing
// ============================================================================

static VideoDesc makeTestVideoDesc(int w = 320, int h = 240, FrameRate::WellKnownRate rate = FrameRate::FPS_24) {
        VideoDesc vdesc;
        vdesc.setFrameRate(FrameRate(rate));
        vdesc.imageList().pushToBack(ImageDesc(w, h, PixelFormat::RGB8));
        return vdesc;
}

// ============================================================================
// Construction
// ============================================================================

TEST_CASE("TestPatternNode_Construct") {
        TestPatternNode node;
        CHECK(node.outputPortCount() == 1);
        CHECK(node.inputPortCount() == 0);
        CHECK(node.state() == MediaNode::Idle);
        CHECK(node.pattern() == TestPatternNode::ColorBars);
        CHECK(node.audioEnabled() == true);
        CHECK(node.audioMode() == TestPatternNode::Tone);
        CHECK(node.motion() == 0.0);
}

// ============================================================================
// Configure with valid VideoDesc
// ============================================================================

TEST_CASE("TestPatternNode_Configure") {
        TestPatternNode node;
        node.setVideoDesc(makeTestVideoDesc());
        Error err = node.configure();
        CHECK(err == Error::Ok);
        CHECK(node.state() == MediaNode::Configured);
}

// ============================================================================
// Configure fails without VideoDesc
// ============================================================================

TEST_CASE("TestPatternNode_ConfigureNoVideo") {
        TestPatternNode node;
        Error err = node.configure();
        CHECK(err != Error::Ok);
}

// ============================================================================
// Generate one frame of each pattern
// ============================================================================

TEST_CASE("TestPatternNode_AllPatterns") {
        TestPatternNode::Pattern patterns[] = {
                TestPatternNode::ColorBars,
                TestPatternNode::ColorBars75,
                TestPatternNode::Ramp,
                TestPatternNode::Grid,
                TestPatternNode::Crosshatch,
                TestPatternNode::Checkerboard,
                TestPatternNode::SolidColor,
                TestPatternNode::White,
                TestPatternNode::Black,
                TestPatternNode::Noise,
                TestPatternNode::ZonePlate
        };

        for(auto pat : patterns) {
                MediaGraph graph;
                TestPatternNode *src = new TestPatternNode();
                CaptureSinkNode *sink = new CaptureSinkNode();

                src->setPattern(pat);
                src->setVideoDesc(makeTestVideoDesc());
                src->setAudioEnabled(false);
                src->setStartTimecode(Timecode(Timecode::NDF24, 1, 0, 0, 0));

                graph.addNode(src);
                graph.addNode(sink);
                graph.connect(src, 0, sink, 0);

                src->configure();
                src->start();
                src->process();
                sink->drain();

                REQUIRE(sink->capturedCount() == 1);
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
        MediaGraph graph;
        TestPatternNode *src = new TestPatternNode();
        CaptureSinkNode *sink = new CaptureSinkNode();

        src->setVideoDesc(makeTestVideoDesc());
        src->setAudioEnabled(false);
        src->setStartTimecode(Timecode(Timecode::NDF24, 1, 0, 0, 0));

        graph.addNode(src);
        graph.addNode(sink);
        graph.connect(src, 0, sink, 0);

        src->configure();
        src->start();

        for(int i = 0; i < 5; i++) {
                src->process();
                sink->drain();
        }

        CHECK(src->frameCount() == 5);

        Frame::Ptr frame = sink->lastFrame();
        REQUIRE(frame.isValid());
        Timecode tc = frame->metadata().get(Metadata::Timecode).get<Timecode>();
        CHECK(tc.frame() == 4);
        CHECK(tc.hour() == 1);
}

// ============================================================================
// Audio tone output
// ============================================================================

TEST_CASE("TestPatternNode_AudioTone") {
        MediaGraph graph;
        TestPatternNode *src = new TestPatternNode();
        CaptureSinkNode *sink = new CaptureSinkNode();

        src->setVideoDesc(makeTestVideoDesc());
        src->setAudioMode(TestPatternNode::Tone);
        src->setToneFrequency(1000.0);

        graph.addNode(src);
        graph.addNode(sink);
        graph.connect(src, 0, sink, 0);

        src->configure();
        src->start();
        src->process();
        sink->drain();

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
        MediaGraph graph;
        TestPatternNode *src = new TestPatternNode();
        CaptureSinkNode *sink = new CaptureSinkNode();

        src->setVideoDesc(makeTestVideoDesc());
        src->setAudioMode(TestPatternNode::Silence);

        graph.addNode(src);
        graph.addNode(sink);
        graph.connect(src, 0, sink, 0);

        src->configure();
        src->start();
        src->process();
        sink->drain();

        Frame::Ptr frame = sink->lastFrame();
        REQUIRE(frame.isValid());
        CHECK(frame->audioList().size() == 1);
}

// ============================================================================
// Audio disabled
// ============================================================================

TEST_CASE("TestPatternNode_AudioDisabled") {
        MediaGraph graph;
        TestPatternNode *src = new TestPatternNode();
        CaptureSinkNode *sink = new CaptureSinkNode();

        src->setVideoDesc(makeTestVideoDesc());
        src->setAudioEnabled(false);

        graph.addNode(src);
        graph.addNode(sink);
        graph.connect(src, 0, sink, 0);

        src->configure();
        src->start();
        src->process();
        sink->drain();

        Frame::Ptr frame = sink->lastFrame();
        REQUIRE(frame.isValid());
        CHECK(frame->audioList().isEmpty());
}

// ============================================================================
// LTC audio mode
// ============================================================================

TEST_CASE("TestPatternNode_AudioLTC") {
        MediaGraph graph;
        TestPatternNode *src = new TestPatternNode();
        CaptureSinkNode *sink = new CaptureSinkNode();

        src->setVideoDesc(makeTestVideoDesc());
        src->setAudioMode(TestPatternNode::LTC);
        src->setAudioDesc(AudioDesc(AudioDesc::PCMI_S8, 48000.0f, 1));
        src->setStartTimecode(Timecode(Timecode::NDF24, 1, 0, 0, 0));

        graph.addNode(src);
        graph.addNode(sink);
        graph.connect(src, 0, sink, 0);

        src->configure();
        src->start();
        src->process();
        sink->drain();

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
        MediaGraph graph;
        TestPatternNode *src = new TestPatternNode();
        CaptureSinkNode *sink = new CaptureSinkNode();

        src->setPattern(TestPatternNode::Ramp);
        src->setVideoDesc(makeTestVideoDesc());
        src->setMotion(1.0);
        src->setAudioEnabled(false);

        graph.addNode(src);
        graph.addNode(sink);
        graph.connect(src, 0, sink, 0);

        src->configure();
        src->start();

        src->process();
        sink->drain();
        Frame::Ptr frame1 = sink->lastFrame();

        src->process();
        sink->drain();
        Frame::Ptr frame2 = sink->lastFrame();

        REQUIRE(frame1.isValid());
        REQUIRE(frame2.isValid());

        Image::Ptr img1 = frame1->imageList()[0];
        Image::Ptr img2 = frame2->imageList()[0];
        uint8_t *row1 = static_cast<uint8_t *>(img1->data());
        uint8_t *row2 = static_cast<uint8_t *>(img2->data());
        bool differ = false;
        for(size_t i = 0; i < img1->lineStride(); i++) {
                if(row1[i] != row2[i]) {
                        differ = true;
                        break;
                }
        }
        CHECK(differ);
}

// ============================================================================
// Static patterns are identical regardless of motion
// ============================================================================

TEST_CASE("TestPatternNode_StaticUnchanged") {
        MediaGraph graph;
        TestPatternNode *src = new TestPatternNode();
        CaptureSinkNode *sink = new CaptureSinkNode();

        src->setPattern(TestPatternNode::White);
        src->setVideoDesc(makeTestVideoDesc());
        src->setMotion(1.0);
        src->setAudioEnabled(false);

        graph.addNode(src);
        graph.addNode(sink);
        graph.connect(src, 0, sink, 0);

        src->configure();
        src->start();

        src->process();
        sink->drain();
        Frame::Ptr frame1 = sink->lastFrame();

        src->process();
        sink->drain();
        Frame::Ptr frame2 = sink->lastFrame();

        REQUIRE(frame1.isValid());
        REQUIRE(frame2.isValid());

        Image::Ptr img1 = frame1->imageList()[0];
        Image::Ptr img2 = frame2->imageList()[0];
        uint8_t *data1 = static_cast<uint8_t *>(img1->data());
        uint8_t *data2 = static_cast<uint8_t *>(img2->data());

        bool same = true;
        for(size_t i = 0; i < img1->lineStride() * img1->height(); i++) {
                if(data1[i] != data2[i]) {
                        same = false;
                        break;
                }
        }
        CHECK(same);
}

// ============================================================================
// Extended stats
// ============================================================================

TEST_CASE("TestPatternNode_ExtendedStats") {
        MediaGraph graph;
        TestPatternNode *src = new TestPatternNode();
        CaptureSinkNode *sink = new CaptureSinkNode();

        src->setVideoDesc(makeTestVideoDesc());
        src->setAudioEnabled(false);
        src->setStartTimecode(Timecode(Timecode::NDF24, 1, 0, 0, 0));

        graph.addNode(src);
        graph.addNode(sink);
        graph.connect(src, 0, sink, 0);

        src->configure();
        src->start();
        src->process();
        src->process();

        auto stats = src->extendedStats();
        CHECK(stats.contains("framesGenerated"));
        CHECK(stats.contains("currentTimecode"));
}

// ============================================================================
// Start and stop lifecycle
// ============================================================================

TEST_CASE("TestPatternNode_Lifecycle") {
        TestPatternNode node;
        node.setVideoDesc(makeTestVideoDesc());

        CHECK(node.configure() == Error::Ok);
        CHECK(node.state() == MediaNode::Configured);

        CHECK(node.start() == Error::Ok);
        CHECK(node.state() == MediaNode::Running);

        node.stop();
        CHECK(node.state() == MediaNode::Idle);
        CHECK(node.frameCount() == 0);
}

// ============================================================================
// Node registry
// ============================================================================

TEST_CASE("TestPatternNode_Registry") {
        auto types = MediaNode::registeredNodeTypes();
        CHECK(types.contains("TestPatternNode"));

        MediaNode *node = MediaNode::createNode("TestPatternNode");
        REQUIRE(node != nullptr);
        CHECK(node->outputPortCount() == 1);
        delete node;
}

// ============================================================================
// SolidColor with custom color
// ============================================================================

TEST_CASE("TestPatternNode_SolidColor") {
        MediaGraph graph;
        TestPatternNode *src = new TestPatternNode();
        CaptureSinkNode *sink = new CaptureSinkNode();

        src->setPattern(TestPatternNode::SolidColor);
        src->setSolidColor(65535, 0, 0); // Red
        src->setVideoDesc(makeTestVideoDesc());
        src->setAudioEnabled(false);

        graph.addNode(src);
        graph.addNode(sink);
        graph.connect(src, 0, sink, 0);

        src->configure();
        src->start();
        src->process();
        sink->drain();

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
// setChannelConfig
// ============================================================================

TEST_CASE("TestPatternNode_SetChannelConfig") {
        MediaGraph graph;
        TestPatternNode *src = new TestPatternNode();
        CaptureSinkNode *sink = new CaptureSinkNode();

        src->setVideoDesc(makeTestVideoDesc());
        src->setAudioMode(TestPatternNode::Tone);
        src->setAudioDesc(AudioDesc(48000.0f, 2));

        AudioGen::Config cfg;
        cfg.type = AudioGen::Sine;
        cfg.freq = 440.0f;
        cfg.level = AudioLevel::fromDbfs(-2.0);
        cfg.phase = 0.0f;
        cfg.dutyCycle = 0.5f;
        src->setChannelConfig(0, cfg);
        src->setChannelConfig(1, cfg);

        graph.addNode(src);
        graph.addNode(sink);
        graph.connect(src, 0, sink, 0);

        src->configure();
        src->start();
        src->process();
        sink->drain();

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
        MediaGraph graph;
        TestPatternNode *src = new TestPatternNode();
        CaptureSinkNode *sink = new CaptureSinkNode();

        src->setVideoDesc(makeTestVideoDesc());
        src->setAudioMode(TestPatternNode::LTC);
        src->setAudioDesc(AudioDesc(48000.0f, 4));
        src->setLtcChannel(2);
        src->setLtcLevel(AudioLevel::fromDbfs(-3.0));
        src->setStartTimecode(Timecode(Timecode::NDF24, 1, 0, 0, 0));

        graph.addNode(src);
        graph.addNode(sink);
        graph.connect(src, 0, sink, 0);

        src->configure();
        src->start();
        src->process();
        sink->drain();

        Frame::Ptr frame = sink->lastFrame();
        REQUIRE(frame.isValid());
        CHECK(frame->audioList().size() == 1);
        Audio::Ptr audio = frame->audioList()[0];
        CHECK(audio->samples() > 0);
}

// ============================================================================
// setToneLevel
// ============================================================================

TEST_CASE("TestPatternNode_SetToneLevel") {
        MediaGraph graph;
        TestPatternNode *src = new TestPatternNode();
        CaptureSinkNode *sink = new CaptureSinkNode();

        src->setVideoDesc(makeTestVideoDesc());
        src->setAudioMode(TestPatternNode::Tone);
        src->setAudioDesc(AudioDesc(48000.0f, 2));
        src->setToneFrequency(440.0);
        src->setToneLevel(AudioLevel::fromDbfs(-6.0));

        graph.addNode(src);
        graph.addNode(sink);
        graph.connect(src, 0, sink, 0);

        src->configure();
        src->start();
        src->process();
        sink->drain();

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
        TestPatternNode node;
        node.setVideoDesc(makeTestVideoDesc());
        node.configure();
        Error err = node.configure(); // Already configured
        CHECK(err == Error::Invalid);
}

// ============================================================================
// Start failure from non-configured state
// ============================================================================

TEST_CASE("TestPatternNode_StartFromIdle") {
        TestPatternNode node;
        Error err = node.start(); // Not configured yet
        CHECK(err == Error::Invalid);
}

// ============================================================================
// VideoDesc with no image layers
// ============================================================================

TEST_CASE("TestPatternNode_ConfigureNoImageLayers") {
        TestPatternNode node;
        VideoDesc vdesc;
        vdesc.setFrameRate(FrameRate(FrameRate::FPS_24));
        // No image layers added
        node.setVideoDesc(vdesc);
        Error err = node.configure();
        CHECK(err != Error::Ok);
}
