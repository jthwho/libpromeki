/**
 * @file      framedemuxnode.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/proav/framedemuxnode.h>
#include <promeki/proav/mediagraph.h>
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
                        auto port = MediaPort::Ptr::create("input", MediaPort::Input, MediaPort::Image);
                        addInputPort(port);
                }
                void process() override {
                        Frame::Ptr frame = dequeueInput();
                        if(frame.isValid()) { _lastFrame = frame; _count++; }
                        return;
                }
                void drain() { while(queuedFrameCount() > 0) process(); return; }
                Frame::Ptr lastFrame() const { return _lastFrame; }
                int count() const { return _count; }
        private:
                Frame::Ptr _lastFrame;
                int _count = 0;
};

// ============================================================================
// Helper: sink node that captures from Audio port
// ============================================================================

class AudioSinkNode : public MediaNode {
        PROMEKI_OBJECT(AudioSinkNode, MediaNode)
        public:
                AudioSinkNode() : MediaNode() {
                        setName("AudioSink");
                        auto port = MediaPort::Ptr::create("input", MediaPort::Input, MediaPort::Audio);
                        addInputPort(port);
                }
                void process() override {
                        Frame::Ptr frame = dequeueInput();
                        if(frame.isValid()) { _lastFrame = frame; _count++; }
                        return;
                }
                void drain() { while(queuedFrameCount() > 0) process(); return; }
                Frame::Ptr lastFrame() const { return _lastFrame; }
                int count() const { return _count; }
        private:
                Frame::Ptr _lastFrame;
                int _count = 0;
};

// ============================================================================
// Helper: source node that pushes a Frame
// ============================================================================

class FrameSourceNode : public MediaNode {
        PROMEKI_OBJECT(FrameSourceNode, MediaNode)
        public:
                FrameSourceNode() : MediaNode() {
                        setName("FrameSource");
                        auto port = MediaPort::Ptr::create("output", MediaPort::Output, MediaPort::Frame);
                        addOutputPort(port);
                }
                void process() override { return; }
                void pushFrame(Frame::Ptr frame) {
                        deliverOutput(frame);
                        return;
                }
};

// ============================================================================
// Construction
// ============================================================================

TEST_CASE("FrameDemuxNode_Construct") {
        FrameDemuxNode node;
        CHECK(node.inputPortCount() == 1);
        CHECK(node.outputPortCount() == 2);
        CHECK(node.inputPort(0)->mediaType() == MediaPort::Frame);
        CHECK(node.outputPort(0)->mediaType() == MediaPort::Image);
        CHECK(node.outputPort(1)->mediaType() == MediaPort::Audio);
}

// ============================================================================
// Split Frame into Image and Audio
// ============================================================================

TEST_CASE("FrameDemuxNode_Split") {
        MediaGraph graph;
        FrameSourceNode *src = new FrameSourceNode();
        FrameDemuxNode *demux = new FrameDemuxNode();
        ImageSinkNode *imgSink = new ImageSinkNode();
        AudioSinkNode *audSink = new AudioSinkNode();

        graph.addNode(src);
        graph.addNode(demux);
        graph.addNode(imgSink);
        graph.addNode(audSink);

        graph.connect(src, 0, demux, 0);
        graph.connect(demux, "image", imgSink, "input");
        graph.connect(demux, "audio", audSink, "input");

        demux->configure();

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
        while(demux->queuedFrameCount() > 0) demux->process();
        imgSink->drain();
        audSink->drain();

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
        MediaGraph graph;
        FrameSourceNode *src = new FrameSourceNode();
        FrameDemuxNode *demux = new FrameDemuxNode();
        ImageSinkNode *imgSink = new ImageSinkNode();
        AudioSinkNode *audSink = new AudioSinkNode();

        graph.addNode(src);
        graph.addNode(demux);
        graph.addNode(imgSink);
        graph.addNode(audSink);

        graph.connect(src, 0, demux, 0);
        graph.connect(demux, "image", imgSink, "input");
        graph.connect(demux, "audio", audSink, "input");

        demux->configure();

        Frame::Ptr frame = Frame::Ptr::create();
        ImageDesc idesc(320, 240, PixelFormat::RGB8);
        frame.modify()->imageList().pushToBack(Image::Ptr::create(Image(idesc)));

        src->pushFrame(frame);
        while(demux->queuedFrameCount() > 0) demux->process();
        imgSink->drain();
        audSink->drain();

        CHECK(imgSink->count() == 1);
        CHECK(audSink->count() == 0);
}

// ============================================================================
// Registry
// ============================================================================

// ============================================================================
// Empty frame produces no output
// ============================================================================

TEST_CASE("FrameDemuxNode_EmptyFrame") {
        MediaGraph graph;
        FrameSourceNode *src = new FrameSourceNode();
        FrameDemuxNode *demux = new FrameDemuxNode();
        ImageSinkNode *imgSink = new ImageSinkNode();
        AudioSinkNode *audSink = new AudioSinkNode();

        graph.addNode(src);
        graph.addNode(demux);
        graph.addNode(imgSink);
        graph.addNode(audSink);

        graph.connect(src, 0, demux, 0);
        graph.connect(demux, "image", imgSink, "input");
        graph.connect(demux, "audio", audSink, "input");

        demux->configure();

        // Push an empty frame (no images, no audio)
        Frame::Ptr frame = Frame::Ptr::create();
        src->pushFrame(frame);
        while(demux->queuedFrameCount() > 0) demux->process();
        imgSink->drain();
        audSink->drain();

        CHECK(imgSink->count() == 0);
        CHECK(audSink->count() == 0);
}

// ============================================================================
// Process with no input does nothing
// ============================================================================

TEST_CASE("FrameDemuxNode_ProcessEmpty") {
        FrameDemuxNode node;
        node.configure();
        // process() with empty queue should be a no-op
        node.process();
}

// ============================================================================
// Registry
// ============================================================================

TEST_CASE("FrameDemuxNode_Registry") {
        auto types = MediaNode::registeredNodeTypes();
        CHECK(types.contains("FrameDemuxNode"));
}
