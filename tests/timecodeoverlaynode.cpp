/**
 * @file      tests/timecodeoverlaynode.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/proav/timecodeoverlaynode.h>
#include <promeki/proav/mediagraph.h>
#include <promeki/proav/frame.h>
#include <promeki/proav/image.h>
#include <promeki/proav/imagedesc.h>
#include <promeki/proav/pixelformat.h>
#include <promeki/core/metadata.h>
#include <promeki/core/timecode.h>
#include <promeki/core/filepath.h>

using namespace promeki;

// Bundled monospace font for tests
static const String testFontPath = String(PROMEKI_SOURCE_DIR) + "/etc/fonts/FiraCodeNerdFontMono-Regular.ttf";

static bool fontAvailable() {
        return FilePath(testFontPath).exists();
}

// ============================================================================
// Helper: source node that pushes Image frames
// ============================================================================

class ImageSourceNode : public MediaNode {
        PROMEKI_OBJECT(ImageSourceNode, MediaNode)
        public:
                ImageSourceNode() : MediaNode() {
                        setName("ImageSource");
                        auto port = MediaPort::Ptr::create("output", MediaPort::Output, MediaPort::Image);
                        addOutputPort(port);
                }
                void process() override { return; }
                void pushFrame(Frame::Ptr frame) {
                        deliverOutput(frame);
                        return;
                }
};

// ============================================================================
// Helper: sink node that captures Image frames
// ============================================================================

class ImageCaptureSink : public MediaNode {
        PROMEKI_OBJECT(ImageCaptureSink, MediaNode)
        public:
                ImageCaptureSink() : MediaNode() {
                        setName("ImageCaptureSink");
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
// Construction
// ============================================================================

TEST_CASE("TimecodeOverlayNode_Construct") {
        TimecodeOverlayNode node;
        CHECK(node.inputPortCount() == 1);
        CHECK(node.outputPortCount() == 1);
        CHECK(node.inputPort(0)->mediaType() == MediaPort::Image);
        CHECK(node.outputPort(0)->mediaType() == MediaPort::Image);
        CHECK(node.fontSize() == 36);
        CHECK(node.position() == TimecodeOverlayNode::BottomCenter);
        CHECK(node.drawBackground() == true);
}

// ============================================================================
// Configure fails without font path
// ============================================================================

TEST_CASE("TimecodeOverlayNode_ConfigureNoFont") {
        TimecodeOverlayNode node;
        Error err = node.configure();
        CHECK(err.isError());
}

// ============================================================================
// Configure fails with nonexistent font path
// ============================================================================

TEST_CASE("TimecodeOverlayNode_ConfigureBadFont") {
        TimecodeOverlayNode node;
        node.setFontPath(FilePath("/nonexistent/font.ttf"));
        Error err = node.configure();
        CHECK(err.isError());
}

// ============================================================================
// Configure succeeds with valid font
// ============================================================================

TEST_CASE("TimecodeOverlayNode_ConfigureOk") {
        if(!fontAvailable()) return;
        TimecodeOverlayNode node;
        node.setFontPath(FilePath(testFontPath));
        Error err = node.configure();
        CHECK(err.isOk());
        CHECK(node.state() == MediaNode::Configured);
}

// ============================================================================
// Overlay modifies image pixels
// ============================================================================

TEST_CASE("TimecodeOverlayNode_OverlayModifiesPixels") {
        if(!fontAvailable()) return;

        MediaGraph graph;
        ImageSourceNode *src = new ImageSourceNode();
        TimecodeOverlayNode *overlay = new TimecodeOverlayNode();
        ImageCaptureSink *sink = new ImageCaptureSink();

        overlay->setFontPath(FilePath(testFontPath));
        overlay->setFontSize(36);
        overlay->setPosition(TimecodeOverlayNode::BottomCenter);

        graph.addNode(src);
        graph.addNode(overlay);
        graph.addNode(sink);

        graph.connect(src, 0, overlay, 0);
        graph.connect(overlay, 0, sink, 0);

        overlay->configure();

        // Create a black image with timecode metadata
        ImageDesc idesc(320, 240, PixelFormat::RGB8);
        Image img(idesc);
        img.fill(0);
        img.metadata().set(Metadata::Timecode, Timecode(Timecode::NDF24, 1, 0, 0, 0));

        // Capture a checksum of the original image
        const uint8_t *origData = (const uint8_t *)img.data();
        size_t dataSize = img.lineStride() * img.height();
        uint64_t origSum = 0;
        for(size_t i = 0; i < dataSize; i++) origSum += origData[i];
        CHECK(origSum == 0); // black image

        // Push through overlay
        Frame::Ptr frame = Frame::Ptr::create();
        frame.modify()->imageList().pushToBack(Image::Ptr::create(img));
        src->pushFrame(frame);
        while(overlay->queuedFrameCount() > 0) overlay->process();
        sink->drain();

        // Verify output received
        REQUIRE(sink->count() == 1);
        Frame::Ptr outFrame = sink->lastFrame();
        REQUIRE(outFrame.isValid());
        REQUIRE(outFrame->imageList().size() == 1);

        // Verify pixels were modified (TC overlay drew something)
        Image::Ptr outImg = outFrame->imageList()[0];
        const uint8_t *outData = (const uint8_t *)outImg->data();
        size_t outSize = outImg->lineStride() * outImg->height();
        uint64_t outSum = 0;
        for(size_t i = 0; i < outSize; i++) outSum += outData[i];
        CHECK(outSum > 0); // image is no longer all-black
}

// ============================================================================
// Metadata is preserved on output frame
// ============================================================================

TEST_CASE("TimecodeOverlayNode_MetadataPreserved") {
        if(!fontAvailable()) return;

        MediaGraph graph;
        ImageSourceNode *src = new ImageSourceNode();
        TimecodeOverlayNode *overlay = new TimecodeOverlayNode();
        ImageCaptureSink *sink = new ImageCaptureSink();

        overlay->setFontPath(FilePath(testFontPath));
        graph.addNode(src);
        graph.addNode(overlay);
        graph.addNode(sink);
        graph.connect(src, 0, overlay, 0);
        graph.connect(overlay, 0, sink, 0);
        overlay->configure();

        ImageDesc idesc(320, 240, PixelFormat::RGB8);
        Image img(idesc);
        img.fill(0);
        Timecode tc(Timecode::NDF25, 10, 30, 0, 0);
        img.metadata().set(Metadata::Timecode, tc);

        Frame::Ptr frame = Frame::Ptr::create();
        frame.modify()->imageList().pushToBack(Image::Ptr::create(img));
        frame.modify()->metadata().set(Metadata::Timecode, tc);
        src->pushFrame(frame);
        while(overlay->queuedFrameCount() > 0) overlay->process();
        sink->drain();

        REQUIRE(sink->count() == 1);
        Frame::Ptr outFrame = sink->lastFrame();
        REQUIRE(outFrame->metadata().contains(Metadata::Timecode));
        Timecode outTc = outFrame->metadata().get(Metadata::Timecode).get<Timecode>();
        CHECK(outTc.hour() == 10);
        CHECK(outTc.min() == 30);
}

// ============================================================================
// Registry
// ============================================================================

TEST_CASE("TimecodeOverlayNode_Registry") {
        auto types = MediaNode::registeredNodeTypes();
        CHECK(types.contains("TimecodeOverlayNode"));
}

// ============================================================================
// Setter/getter coverage
// ============================================================================

TEST_CASE("TimecodeOverlayNode_SetFontSize") {
        TimecodeOverlayNode node;
        CHECK(node.fontSize() == 36); // default
        node.setFontSize(72);
        CHECK(node.fontSize() == 72);
}

TEST_CASE("TimecodeOverlayNode_SetPosition_Presets") {
        TimecodeOverlayNode node;
        CHECK(node.position() == TimecodeOverlayNode::BottomCenter); // default

        node.setPosition(TimecodeOverlayNode::TopLeft);
        CHECK(node.position() == TimecodeOverlayNode::TopLeft);

        node.setPosition(TimecodeOverlayNode::TopCenter);
        CHECK(node.position() == TimecodeOverlayNode::TopCenter);

        node.setPosition(TimecodeOverlayNode::TopRight);
        CHECK(node.position() == TimecodeOverlayNode::TopRight);

        node.setPosition(TimecodeOverlayNode::BottomLeft);
        CHECK(node.position() == TimecodeOverlayNode::BottomLeft);

        node.setPosition(TimecodeOverlayNode::BottomRight);
        CHECK(node.position() == TimecodeOverlayNode::BottomRight);
}

TEST_CASE("TimecodeOverlayNode_SetPosition_Custom") {
        TimecodeOverlayNode node;
        node.setPosition(100, 200);
        CHECK(node.position() == TimecodeOverlayNode::Custom);
}

TEST_CASE("TimecodeOverlayNode_SetDrawBackground") {
        TimecodeOverlayNode node;
        CHECK(node.drawBackground() == true); // default
        node.setDrawBackground(false);
        CHECK(node.drawBackground() == false);
}

TEST_CASE("TimecodeOverlayNode_SetCustomText") {
        TimecodeOverlayNode node;
        CHECK(node.customText().isEmpty());
        node.setCustomText("TEST SIGNAL");
        CHECK(node.customText() == "TEST SIGNAL");
}

TEST_CASE("TimecodeOverlayNode_SetFontPath") {
        TimecodeOverlayNode node;
        CHECK(node.fontPath().isEmpty());
        node.setFontPath(FilePath("/some/font.ttf"));
        CHECK(node.fontPath().toString() == "/some/font.ttf");
}

// ============================================================================
// Configure from non-Idle state
// ============================================================================

TEST_CASE("TimecodeOverlayNode_ConfigureNotIdleReturnsError") {
        if(!fontAvailable()) return;
        TimecodeOverlayNode node;
        node.setFontPath(FilePath(testFontPath));
        Error err = node.configure();
        REQUIRE(err.isOk());
        CHECK(node.state() == MediaNode::Configured);

        // Trying to configure again from Configured state should fail
        Error err2 = node.configure();
        CHECK(err2.isError());
}

// ============================================================================
// Frame with no images (passthrough)
// ============================================================================

TEST_CASE("TimecodeOverlayNode_EmptyFramePassthrough") {
        if(!fontAvailable()) return;

        MediaGraph graph;
        ImageSourceNode *src = new ImageSourceNode();
        TimecodeOverlayNode *overlay = new TimecodeOverlayNode();
        ImageCaptureSink *sink = new ImageCaptureSink();

        overlay->setFontPath(FilePath(testFontPath));
        graph.addNode(src);
        graph.addNode(overlay);
        graph.addNode(sink);
        graph.connect(src, 0, overlay, 0);
        graph.connect(overlay, 0, sink, 0);
        overlay->configure();

        // Push a frame with no images
        Frame::Ptr frame = Frame::Ptr::create();
        src->pushFrame(frame);
        while(overlay->queuedFrameCount() > 0) overlay->process();
        sink->drain();

        REQUIRE(sink->count() == 1);
        Frame::Ptr outFrame = sink->lastFrame();
        REQUIRE(outFrame.isValid());
        CHECK(outFrame->imageList().isEmpty());
}

// ============================================================================
// Frame without timecode metadata renders placeholder
// ============================================================================

TEST_CASE("TimecodeOverlayNode_NoTimecodeMetadata") {
        if(!fontAvailable()) return;

        MediaGraph graph;
        ImageSourceNode *src = new ImageSourceNode();
        TimecodeOverlayNode *overlay = new TimecodeOverlayNode();
        ImageCaptureSink *sink = new ImageCaptureSink();

        overlay->setFontPath(FilePath(testFontPath));
        overlay->setFontSize(24);
        graph.addNode(src);
        graph.addNode(overlay);
        graph.addNode(sink);
        graph.connect(src, 0, overlay, 0);
        graph.connect(overlay, 0, sink, 0);
        overlay->configure();

        // Push an image with NO timecode metadata
        ImageDesc idesc(320, 240, PixelFormat::RGB8);
        Image img(idesc);
        img.fill(0);

        Frame::Ptr frame = Frame::Ptr::create();
        frame.modify()->imageList().pushToBack(Image::Ptr::create(img));
        src->pushFrame(frame);
        while(overlay->queuedFrameCount() > 0) overlay->process();
        sink->drain();

        REQUIRE(sink->count() == 1);
        Frame::Ptr outFrame = sink->lastFrame();
        REQUIRE(outFrame.isValid());
        REQUIRE(outFrame->imageList().size() == 1);

        // Should still render "--:--:--:--" so pixels should differ from black
        Image::Ptr outImg = outFrame->imageList()[0];
        const uint8_t *outData = (const uint8_t *)outImg->data();
        size_t outSize = outImg->lineStride() * outImg->height();
        uint64_t sum = 0;
        for(size_t i = 0; i < outSize; i++) sum += outData[i];
        CHECK(sum > 0);
}

// ============================================================================
// Custom text adds to output
// ============================================================================

TEST_CASE("TimecodeOverlayNode_CustomText") {
        if(!fontAvailable()) return;

        // Render with custom text and without, compare pixel sums to verify
        // custom text adds more drawn pixels.
        auto renderAndSum = [](const String &customText) -> uint64_t {
                MediaGraph graph;
                ImageSourceNode *src = new ImageSourceNode();
                TimecodeOverlayNode *overlay = new TimecodeOverlayNode();
                ImageCaptureSink *sink = new ImageCaptureSink();

                overlay->setFontPath(FilePath(testFontPath));
                overlay->setFontSize(24);
                overlay->setPosition(TimecodeOverlayNode::TopLeft);
                if(!customText.isEmpty()) overlay->setCustomText(customText);

                graph.addNode(src);
                graph.addNode(overlay);
                graph.addNode(sink);
                graph.connect(src, 0, overlay, 0);
                graph.connect(overlay, 0, sink, 0);
                overlay->configure();

                ImageDesc idesc(320, 240, PixelFormat::RGB8);
                Image img(idesc);
                img.fill(0);
                img.metadata().set(Metadata::Timecode, Timecode(Timecode::NDF24, 1, 0, 0, 0));

                Frame::Ptr frame = Frame::Ptr::create();
                frame.modify()->imageList().pushToBack(Image::Ptr::create(img));
                src->pushFrame(frame);
                while(overlay->queuedFrameCount() > 0) overlay->process();
                sink->drain();

                Frame::Ptr outFrame = sink->lastFrame();
                Image::Ptr outImg = outFrame->imageList()[0];
                const uint8_t *data = (const uint8_t *)outImg->data();
                size_t dataSize = outImg->lineStride() * outImg->height();
                uint64_t sum = 0;
                for(size_t i = 0; i < dataSize; i++) sum += data[i];
                return sum;
        };

        uint64_t sumWithout = renderAndSum("");
        uint64_t sumWith = renderAndSum("TEST SIGNAL");
        CHECK(sumWith > sumWithout);
}

// ============================================================================
// drawBackground(false) produces fewer drawn pixels than true
// ============================================================================

TEST_CASE("TimecodeOverlayNode_NoBackground") {
        if(!fontAvailable()) return;

        auto renderAndSum = [](bool drawBg) -> uint64_t {
                MediaGraph graph;
                ImageSourceNode *src = new ImageSourceNode();
                TimecodeOverlayNode *overlay = new TimecodeOverlayNode();
                ImageCaptureSink *sink = new ImageCaptureSink();

                overlay->setFontPath(FilePath(testFontPath));
                overlay->setFontSize(24);
                overlay->setDrawBackground(drawBg);

                graph.addNode(src);
                graph.addNode(overlay);
                graph.addNode(sink);
                graph.connect(src, 0, overlay, 0);
                graph.connect(overlay, 0, sink, 0);
                overlay->configure();

                ImageDesc idesc(320, 240, PixelFormat::RGB8);
                Image img(idesc);
                // Fill with mid-gray so we can detect both black bg and white text
                img.fill(128);
                img.metadata().set(Metadata::Timecode, Timecode(Timecode::NDF24, 1, 0, 0, 0));

                Frame::Ptr frame = Frame::Ptr::create();
                frame.modify()->imageList().pushToBack(Image::Ptr::create(img));
                src->pushFrame(frame);
                while(overlay->queuedFrameCount() > 0) overlay->process();
                sink->drain();

                Frame::Ptr outFrame = sink->lastFrame();
                Image::Ptr outImg = outFrame->imageList()[0];
                const uint8_t *data = (const uint8_t *)outImg->data();
                size_t dataSize = outImg->lineStride() * outImg->height();

                // Count pixels that differ from the original 128 fill
                size_t diffCount = 0;
                for(size_t i = 0; i < dataSize; i++) {
                        if(data[i] != 128) diffCount++;
                }
                return diffCount;
        };

        size_t diffWithBg = renderAndSum(true);
        size_t diffWithoutBg = renderAndSum(false);
        // Background rectangle adds more changed pixels
        CHECK(diffWithBg > diffWithoutBg);
}

// ============================================================================
// Position presets render to expected regions
// ============================================================================

TEST_CASE("TimecodeOverlayNode_TopLeftPosition") {
        if(!fontAvailable()) return;

        MediaGraph graph;
        ImageSourceNode *src = new ImageSourceNode();
        TimecodeOverlayNode *overlay = new TimecodeOverlayNode();
        ImageCaptureSink *sink = new ImageCaptureSink();

        overlay->setFontPath(FilePath(testFontPath));
        overlay->setFontSize(24);
        overlay->setPosition(TimecodeOverlayNode::TopLeft);

        graph.addNode(src);
        graph.addNode(overlay);
        graph.addNode(sink);
        graph.connect(src, 0, overlay, 0);
        graph.connect(overlay, 0, sink, 0);
        overlay->configure();

        ImageDesc idesc(640, 480, PixelFormat::RGB8);
        Image img(idesc);
        img.fill(0);
        img.metadata().set(Metadata::Timecode, Timecode(Timecode::NDF24, 1, 0, 0, 0));

        Frame::Ptr frame = Frame::Ptr::create();
        frame.modify()->imageList().pushToBack(Image::Ptr::create(img));
        src->pushFrame(frame);
        while(overlay->queuedFrameCount() > 0) overlay->process();
        sink->drain();

        REQUIRE(sink->count() == 1);
        Image::Ptr outImg = sink->lastFrame()->imageList()[0];
        size_t stride = outImg->lineStride();
        const uint8_t *data = (const uint8_t *)outImg->data();

        // Check top-left quadrant has drawn pixels
        uint64_t topLeftSum = 0;
        for(size_t y = 0; y < 60; y++) {
                for(size_t x = 0; x < 200 * 3; x++) {
                        topLeftSum += data[y * stride + x];
                }
        }
        CHECK(topLeftSum > 0);

        // Check bottom-right quadrant is still black
        uint64_t bottomRightSum = 0;
        for(size_t y = 400; y < 480; y++) {
                for(size_t x = 400 * 3; x < 640 * 3; x++) {
                        bottomRightSum += data[y * stride + x];
                }
        }
        CHECK(bottomRightSum == 0);
}

TEST_CASE("TimecodeOverlayNode_BottomRightPosition") {
        if(!fontAvailable()) return;

        MediaGraph graph;
        ImageSourceNode *src = new ImageSourceNode();
        TimecodeOverlayNode *overlay = new TimecodeOverlayNode();
        ImageCaptureSink *sink = new ImageCaptureSink();

        overlay->setFontPath(FilePath(testFontPath));
        overlay->setFontSize(24);
        overlay->setPosition(TimecodeOverlayNode::BottomRight);

        graph.addNode(src);
        graph.addNode(overlay);
        graph.addNode(sink);
        graph.connect(src, 0, overlay, 0);
        graph.connect(overlay, 0, sink, 0);
        overlay->configure();

        ImageDesc idesc(640, 480, PixelFormat::RGB8);
        Image img(idesc);
        img.fill(0);
        img.metadata().set(Metadata::Timecode, Timecode(Timecode::NDF24, 1, 0, 0, 0));

        Frame::Ptr frame = Frame::Ptr::create();
        frame.modify()->imageList().pushToBack(Image::Ptr::create(img));
        src->pushFrame(frame);
        while(overlay->queuedFrameCount() > 0) overlay->process();
        sink->drain();

        REQUIRE(sink->count() == 1);
        Image::Ptr outImg = sink->lastFrame()->imageList()[0];
        size_t stride = outImg->lineStride();
        const uint8_t *data = (const uint8_t *)outImg->data();

        // Check bottom-right region has drawn pixels
        uint64_t bottomRightSum = 0;
        for(size_t y = 400; y < 480; y++) {
                for(size_t x = 400 * 3; x < 640 * 3; x++) {
                        bottomRightSum += data[y * stride + x];
                }
        }
        CHECK(bottomRightSum > 0);

        // Check top-left quadrant is still black
        uint64_t topLeftSum = 0;
        for(size_t y = 0; y < 60; y++) {
                for(size_t x = 0; x < 200 * 3; x++) {
                        topLeftSum += data[y * stride + x];
                }
        }
        CHECK(topLeftSum == 0);
}

// ============================================================================
// Multiple frames processed
// ============================================================================

TEST_CASE("TimecodeOverlayNode_MultipleFrames") {
        if(!fontAvailable()) return;

        MediaGraph graph;
        ImageSourceNode *src = new ImageSourceNode();
        TimecodeOverlayNode *overlay = new TimecodeOverlayNode();
        ImageCaptureSink *sink = new ImageCaptureSink();

        overlay->setFontPath(FilePath(testFontPath));
        overlay->setFontSize(24);
        graph.addNode(src);
        graph.addNode(overlay);
        graph.addNode(sink);
        graph.connect(src, 0, overlay, 0);
        graph.connect(overlay, 0, sink, 0);
        overlay->configure();

        // Push 3 frames with different timecodes
        for(int i = 0; i < 3; i++) {
                ImageDesc idesc(320, 240, PixelFormat::RGB8);
                Image img(idesc);
                img.fill(0);
                img.metadata().set(Metadata::Timecode, Timecode(Timecode::NDF24, i, 0, 0, 0));

                Frame::Ptr frame = Frame::Ptr::create();
                frame.modify()->imageList().pushToBack(Image::Ptr::create(img));
                src->pushFrame(frame);
        }

        while(overlay->queuedFrameCount() > 0) overlay->process();
        sink->drain();

        CHECK(sink->count() == 3);
}

// ============================================================================
// RGBA8 image support
// ============================================================================

TEST_CASE("TimecodeOverlayNode_RGBA8") {
        if(!fontAvailable()) return;

        MediaGraph graph;
        ImageSourceNode *src = new ImageSourceNode();
        TimecodeOverlayNode *overlay = new TimecodeOverlayNode();
        ImageCaptureSink *sink = new ImageCaptureSink();

        overlay->setFontPath(FilePath(testFontPath));
        overlay->setFontSize(36);
        graph.addNode(src);
        graph.addNode(overlay);
        graph.addNode(sink);
        graph.connect(src, 0, overlay, 0);
        graph.connect(overlay, 0, sink, 0);
        overlay->configure();

        ImageDesc idesc(320, 240, PixelFormat::RGBA8);
        Image img(idesc);
        img.fill(0);
        img.metadata().set(Metadata::Timecode, Timecode(Timecode::NDF24, 1, 0, 0, 0));

        Frame::Ptr frame = Frame::Ptr::create();
        frame.modify()->imageList().pushToBack(Image::Ptr::create(img));
        src->pushFrame(frame);
        while(overlay->queuedFrameCount() > 0) overlay->process();
        sink->drain();

        REQUIRE(sink->count() == 1);
        Image::Ptr outImg = sink->lastFrame()->imageList()[0];
        const uint8_t *outData = (const uint8_t *)outImg->data();
        size_t outSize = outImg->lineStride() * outImg->height();
        uint64_t outSum = 0;
        for(size_t i = 0; i < outSize; i++) outSum += outData[i];
        CHECK(outSum > 0);
}
