/**
 * @file      framedemuxnode.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <atomic>
#include <thread>
#include <doctest/doctest.h>
#include <promeki/proav/framedemuxnode.h>
#include <promeki/proav/mediapipeline.h>
#include <promeki/proav/medianodeconfig.h>
#include <promeki/proav/frame.h>
#include <promeki/proav/image.h>
#include <promeki/proav/audio.h>
#include <promeki/proav/imagedesc.h>
#include <promeki/proav/audiodesc.h>
#include <promeki/proav/pixelformat.h>
#include <promeki/core/metadata.h>
#include <promeki/core/timecode.h>

using namespace promeki;

// ============================================================================
// Helper: sink node that captures from Image port
// ============================================================================

class ImageSinkNode : public MediaNode {
        PROMEKI_OBJECT(ImageSinkNode, MediaNode)
        public:
                ImageSinkNode() : MediaNode() {
                        setName("ImageSink");
                        auto port = MediaSink::Ptr::create("input", ContentVideo);
                        addSink(port);
                }
                BuildResult build(const MediaNodeConfig &) override {
                        setState(Configured);
                        return BuildResult();
                }
                void processFrame(Frame::Ptr &frame, int inputIndex, DeliveryList &deliveries) override {
                        (void)inputIndex; (void)deliveries;
                        if(frame.isValid()) { _lastFrame = frame; _count++; }
                        return;
                }
                Frame::Ptr lastFrame() const { return _lastFrame; }
                int count() const { return _count; }
        private:
                Frame::Ptr _lastFrame;
                std::atomic<int> _count = 0;
};

// ============================================================================
// Helper: sink node that captures from Audio port
// ============================================================================

class AudioSinkNode : public MediaNode {
        PROMEKI_OBJECT(AudioSinkNode, MediaNode)
        public:
                AudioSinkNode() : MediaNode() {
                        setName("AudioSink");
                        auto port = MediaSink::Ptr::create("input", ContentAudio);
                        addSink(port);
                }
                BuildResult build(const MediaNodeConfig &) override {
                        setState(Configured);
                        return BuildResult();
                }
                void processFrame(Frame::Ptr &frame, int inputIndex, DeliveryList &deliveries) override {
                        (void)inputIndex; (void)deliveries;
                        if(frame.isValid()) { _lastFrame = frame; _count++; }
                        return;
                }
                Frame::Ptr lastFrame() const { return _lastFrame; }
                int count() const { return _count; }
        private:
                Frame::Ptr _lastFrame;
                std::atomic<int> _count = 0;
};

// ============================================================================
// Helper: source node that pushes a Frame
// ============================================================================

class FrameSourceNode : public MediaNode {
        PROMEKI_OBJECT(FrameSourceNode, MediaNode)
        public:
                FrameSourceNode() : MediaNode() {
                        setName("FrameSource");
                        auto port = MediaSource::Ptr::create("output", ContentNone);
                        addSource(port);
                }
                BuildResult build(const MediaNodeConfig &) override {
                        setState(Configured);
                        return BuildResult();
                }
                void processFrame(Frame::Ptr &frame, int inputIndex, DeliveryList &deliveries) override {
                        (void)frame; (void)inputIndex; (void)deliveries;
                        return;
                }
                void pushFrame(Frame::Ptr frame) {
                        deliverOutput(frame);
                        return;
                }
};

// Helper to build all nodes in a pipeline with default config
static void buildAllNodes(MediaPipeline &pipeline) {
        for(auto *node : pipeline.nodes()) {
                node->build(MediaNodeConfig());
        }
}

// ============================================================================
// Construction
// ============================================================================

TEST_CASE("FrameDemuxNode_Construct") {
        FrameDemuxNode node;
        CHECK(node.sinkCount() == 1);
        CHECK(node.sourceCount() == 2);
        CHECK(node.sink(0)->contentHint() == ContentNone);
        CHECK(node.source(0)->contentHint() == ContentVideo);
        CHECK(node.source(1)->contentHint() == ContentAudio);
}

// ============================================================================
// Split Frame into Image and Audio
// ============================================================================

TEST_CASE("FrameDemuxNode_Split") {
        MediaPipeline pipeline;
        FrameSourceNode *src = new FrameSourceNode();
        FrameDemuxNode *demux = new FrameDemuxNode();
        ImageSinkNode *imgSink = new ImageSinkNode();
        AudioSinkNode *audSink = new AudioSinkNode();

        pipeline.addNode(src);
        pipeline.addNode(demux);
        pipeline.addNode(imgSink);
        pipeline.addNode(audSink);

        pipeline.connect(src, 0, demux, 0);
        pipeline.connect(demux, "image", imgSink, "input");
        pipeline.connect(demux, "audio", audSink, "input");

        buildAllNodes(pipeline);
        pipeline.start();

        // Create a Frame with image and audio
        Frame::Ptr frame = Frame::Ptr::create();
        ImageDesc idesc(320, 240, PixelFormat::RGB8);
        Image::Ptr img = Image::Ptr::create(Image(idesc));
        frame.modify()->imageList().pushToBack(img);

        AudioDesc adesc(48000.0f, 2);
        Audio::Ptr audio = Audio::Ptr::create(Audio(adesc, 1000));
        frame.modify()->audioList().pushToBack(audio);

        Timecode tc(Timecode::NDF24, 1, 0, 0, 5);
        frame.modify()->metadata().set(Metadata::Timecode, tc);

        // Push through demux
        src->pushFrame(frame);

        // Wait for both sinks to receive their frames
        for(int i = 0; i < 200 && (imgSink->count() < 1 || audSink->count() < 1); i++) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        pipeline.stop();

        // Verify image output
        REQUIRE(imgSink->count() == 1);
        Frame::Ptr imgFrame = imgSink->lastFrame();
        REQUIRE(imgFrame.isValid());
        CHECK(imgFrame->imageList().size() == 1);
        CHECK(imgFrame->audioList().isEmpty());
        Timecode imgTc = imgFrame->metadata().get(Metadata::Timecode).get<Timecode>();
        CHECK(imgTc.frame() == 5);

        // Verify audio output
        REQUIRE(audSink->count() == 1);
        Frame::Ptr audFrame = audSink->lastFrame();
        REQUIRE(audFrame.isValid());
        CHECK(audFrame->audioList().size() == 1);
        CHECK(audFrame->imageList().isEmpty());
        Timecode audTc = audFrame->metadata().get(Metadata::Timecode).get<Timecode>();
        CHECK(audTc.frame() == 5);
}

// ============================================================================
// Frame with no audio only produces image output
// ============================================================================

TEST_CASE("FrameDemuxNode_NoAudio") {
        MediaPipeline pipeline;
        FrameSourceNode *src = new FrameSourceNode();
        FrameDemuxNode *demux = new FrameDemuxNode();
        ImageSinkNode *imgSink = new ImageSinkNode();
        AudioSinkNode *audSink = new AudioSinkNode();

        pipeline.addNode(src);
        pipeline.addNode(demux);
        pipeline.addNode(imgSink);
        pipeline.addNode(audSink);

        pipeline.connect(src, 0, demux, 0);
        pipeline.connect(demux, "image", imgSink, "input");
        pipeline.connect(demux, "audio", audSink, "input");

        buildAllNodes(pipeline);
        pipeline.start();

        Frame::Ptr frame = Frame::Ptr::create();
        ImageDesc idesc(320, 240, PixelFormat::RGB8);
        frame.modify()->imageList().pushToBack(Image::Ptr::create(Image(idesc)));

        src->pushFrame(frame);

        // Wait for image sink to receive the frame
        for(int i = 0; i < 200 && imgSink->count() < 1; i++) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        // Give a brief window for audio sink to erroneously receive something
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        pipeline.stop();

        CHECK(imgSink->count() == 1);
        CHECK(audSink->count() == 0);
}

// ============================================================================
// Empty frame produces no output
// ============================================================================

TEST_CASE("FrameDemuxNode_EmptyFrame") {
        MediaPipeline pipeline;
        FrameSourceNode *src = new FrameSourceNode();
        FrameDemuxNode *demux = new FrameDemuxNode();
        ImageSinkNode *imgSink = new ImageSinkNode();
        AudioSinkNode *audSink = new AudioSinkNode();

        pipeline.addNode(src);
        pipeline.addNode(demux);
        pipeline.addNode(imgSink);
        pipeline.addNode(audSink);

        pipeline.connect(src, 0, demux, 0);
        pipeline.connect(demux, "image", imgSink, "input");
        pipeline.connect(demux, "audio", audSink, "input");

        buildAllNodes(pipeline);
        pipeline.start();

        // Push an empty frame (no images, no audio)
        Frame::Ptr frame = Frame::Ptr::create();
        src->pushFrame(frame);

        // Give pipeline time to process the empty frame
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        pipeline.stop();

        CHECK(imgSink->count() == 0);
        CHECK(audSink->count() == 0);
}

// ============================================================================
// Node starts and stops cleanly with no input
// ============================================================================

TEST_CASE("FrameDemuxNode_ProcessEmpty") {
        MediaPipeline pipeline;
        FrameSourceNode *src = new FrameSourceNode();
        FrameDemuxNode *demux = new FrameDemuxNode();
        ImageSinkNode *imgSink = new ImageSinkNode();
        AudioSinkNode *audSink = new AudioSinkNode();

        pipeline.addNode(src);
        pipeline.addNode(demux);
        pipeline.addNode(imgSink);
        pipeline.addNode(audSink);

        pipeline.connect(src, 0, demux, 0);
        pipeline.connect(demux, "image", imgSink, "input");
        pipeline.connect(demux, "audio", audSink, "input");

        buildAllNodes(pipeline);

        // Verify the node starts and stops cleanly with no input
        Error err = pipeline.start();
        CHECK(err == Error::Ok);
        pipeline.stop();
}

// ============================================================================
// Registry
// ============================================================================

TEST_CASE("FrameDemuxNode_Registry") {
        auto types = MediaNode::registeredNodeTypes();
        CHECK(types.contains("FrameDemuxNode"));
}
