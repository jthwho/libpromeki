/**
 * @file      tests/timecodeoverlaynode.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <thread>
#include <doctest/doctest.h>
#include <promeki/atomic.h>
#include <promeki/mutex.h>
#include <promeki/timecodeoverlaynode.h>
#include <promeki/mediapipeline.h>
#include <promeki/medianodeconfig.h>
#include <promeki/frame.h>
#include <promeki/image.h>
#include <promeki/imagedesc.h>
#include <promeki/pixeldesc.h>
#include <promeki/metadata.h>
#include <promeki/timecode.h>
#include <promeki/color.h>
#include <promeki/filepath.h>
#include <promeki/fastfont.h>

using namespace promeki;

// Probes the library's bundled default font so tests that render
// text can skip themselves if the library was built without the
// compiled-in resource filesystem.
static bool fontAvailable() {
        Image img(16, 16, PixelDesc::RGB8_sRGB);
        FastFont probe(img.createPaintEngine());
        probe.setFontSize(12);
        return probe.measureText("A") > 0;
}

// ============================================================================
// Helper: source node that pushes Image frames
// ============================================================================

class ImageSourceNode : public MediaNode {
        PROMEKI_OBJECT(ImageSourceNode, MediaNode)
        public:
                ImageSourceNode() : MediaNode() {
                        setName("ImageSource");
                        auto port = MediaSource::Ptr::create("output", ContentVideo);
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

// ============================================================================
// Helper: sink node that captures Image frames
// ============================================================================

class ImageCaptureSink : public MediaNode {
        PROMEKI_OBJECT(ImageCaptureSink, MediaNode)
        public:
                ImageCaptureSink() : MediaNode() {
                        setName("ImageCaptureSink");
                        auto port = MediaSink::Ptr::create("input", ContentVideo);
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
// Helper: push an image through an overlay pipeline, return the output frame.
// ============================================================================

static Frame::Ptr overlayImage(const Image &img, const MediaNodeConfig &overlayCfg) {
        MediaPipeline pipeline;
        ImageSourceNode *src = new ImageSourceNode();
        TimecodeOverlayNode *overlay = new TimecodeOverlayNode();
        ImageCaptureSink *sink = new ImageCaptureSink();

        src->build(MediaNodeConfig());
        overlay->build(overlayCfg);
        sink->build(MediaNodeConfig());

        pipeline.addNode(src);
        pipeline.addNode(overlay);
        pipeline.addNode(sink);
        pipeline.connect(src, 0, overlay, 0);
        pipeline.connect(overlay, 0, sink, 0);
        pipeline.start();

        Frame::Ptr frame = Frame::Ptr::create();
        frame.modify()->imageList().pushToBack(Image::Ptr::create(img));
        src->pushFrame(frame);

        for(int i = 0; i < 200 && sink->count() < 1; i++) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        pipeline.stop();
        return sink->lastFrame();
}

// ============================================================================
// Construction
// ============================================================================

TEST_CASE("TimecodeOverlayNode_Construct") {
        TimecodeOverlayNode node;
        CHECK(node.sinkCount() == 1);
        CHECK(node.sourceCount() == 1);
        CHECK(node.sink(0)->contentHint() == ContentVideo);
        CHECK(node.source(0)->contentHint() == ContentVideo);
}

// ============================================================================
// Configure succeeds without an explicit font path (uses bundled default)
// ============================================================================

TEST_CASE("TimecodeOverlayNode_ConfigureDefaultFont") {
        if(!fontAvailable()) return;
        TimecodeOverlayNode *overlay = new TimecodeOverlayNode();
        // Build without FontPath — empty path means "use the
        // library's bundled default font".
        MediaNodeConfig overlayCfg("TimecodeOverlayNode", "overlay");
        BuildResult result = overlay->build(overlayCfg);
        CHECK(result.isOk());
        delete overlay;
}

// ============================================================================
// Configure fails with nonexistent font path
// ============================================================================

TEST_CASE("TimecodeOverlayNode_ConfigureBadFont") {
        MediaPipeline pipeline;
        ImageSourceNode *src = new ImageSourceNode();
        TimecodeOverlayNode *overlay = new TimecodeOverlayNode();
        ImageCaptureSink *sink = new ImageCaptureSink();

        src->build(MediaNodeConfig());

        MediaNodeConfig overlayCfg("TimecodeOverlayNode", "overlay");
        overlayCfg.set("FontPath", "/nonexistent/font.ttf");
        BuildResult result = overlay->build(overlayCfg);
        CHECK(result.isError());

        pipeline.addNode(src);
        pipeline.addNode(overlay);
        pipeline.addNode(sink);
        pipeline.connect(src, 0, overlay, 0);
        pipeline.connect(overlay, 0, sink, 0);

        sink->build(MediaNodeConfig());

        Error err = pipeline.start();
        CHECK(err.isError());
}

// ============================================================================
// Configure succeeds with valid font
// ============================================================================

TEST_CASE("TimecodeOverlayNode_ConfigureOk") {
        if(!fontAvailable()) return;
        MediaPipeline pipeline;
        ImageSourceNode *src = new ImageSourceNode();
        TimecodeOverlayNode *overlay = new TimecodeOverlayNode();
        ImageCaptureSink *sink = new ImageCaptureSink();

        src->build(MediaNodeConfig());

        MediaNodeConfig overlayCfg("TimecodeOverlayNode", "overlay");
        overlay->build(overlayCfg);

        sink->build(MediaNodeConfig());

        pipeline.addNode(src);
        pipeline.addNode(overlay);
        pipeline.addNode(sink);
        pipeline.connect(src, 0, overlay, 0);
        pipeline.connect(overlay, 0, sink, 0);
        Error err = pipeline.start();
        CHECK(err.isOk());
        CHECK(overlay->state() == MediaNode::Running);
}

// ============================================================================
// Overlay modifies image pixels
// ============================================================================

TEST_CASE("TimecodeOverlayNode_OverlayModifiesPixels") {
        if(!fontAvailable()) return;

        MediaPipeline pipeline;
        ImageSourceNode *src = new ImageSourceNode();
        TimecodeOverlayNode *overlay = new TimecodeOverlayNode();
        ImageCaptureSink *sink = new ImageCaptureSink();

        src->build(MediaNodeConfig());

        MediaNodeConfig overlayCfg("TimecodeOverlayNode", "overlay");
        overlayCfg.set("FontSize", 36);
        overlayCfg.set("Position", "bottomcenter");
        overlay->build(overlayCfg);

        sink->build(MediaNodeConfig());

        pipeline.addNode(src);
        pipeline.addNode(overlay);
        pipeline.addNode(sink);

        pipeline.connect(src, 0, overlay, 0);
        pipeline.connect(overlay, 0, sink, 0);

        pipeline.start();

        // Create a black image with timecode metadata
        ImageDesc idesc(320, 240, PixelDesc::RGB8_sRGB);
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

        for(int i = 0; i < 200 && sink->count() < 1; i++) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        pipeline.stop();

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

        MediaPipeline pipeline;
        ImageSourceNode *src = new ImageSourceNode();
        TimecodeOverlayNode *overlay = new TimecodeOverlayNode();
        ImageCaptureSink *sink = new ImageCaptureSink();

        src->build(MediaNodeConfig());

        MediaNodeConfig overlayCfg("TimecodeOverlayNode", "overlay");
        overlay->build(overlayCfg);

        sink->build(MediaNodeConfig());

        pipeline.addNode(src);
        pipeline.addNode(overlay);
        pipeline.addNode(sink);
        pipeline.connect(src, 0, overlay, 0);
        pipeline.connect(overlay, 0, sink, 0);
        pipeline.start();

        ImageDesc idesc(320, 240, PixelDesc::RGB8_sRGB);
        Image img(idesc);
        img.fill(0);
        Timecode tc(Timecode::NDF25, 10, 30, 0, 0);
        img.metadata().set(Metadata::Timecode, tc);

        Frame::Ptr frame = Frame::Ptr::create();
        frame.modify()->imageList().pushToBack(Image::Ptr::create(img));
        frame.modify()->metadata().set(Metadata::Timecode, tc);
        src->pushFrame(frame);

        for(int i = 0; i < 200 && sink->count() < 1; i++) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        pipeline.stop();

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
// Configure from non-Idle state
// ============================================================================

TEST_CASE("TimecodeOverlayNode_ConfigureNotIdleReturnsError") {
        if(!fontAvailable()) return;
        MediaPipeline pipeline;
        ImageSourceNode *src = new ImageSourceNode();
        TimecodeOverlayNode *overlay = new TimecodeOverlayNode();
        ImageCaptureSink *sink = new ImageCaptureSink();

        src->build(MediaNodeConfig());

        MediaNodeConfig overlayCfg("TimecodeOverlayNode", "overlay");
        overlay->build(overlayCfg);

        sink->build(MediaNodeConfig());

        pipeline.addNode(src);
        pipeline.addNode(overlay);
        pipeline.addNode(sink);
        pipeline.connect(src, 0, overlay, 0);
        pipeline.connect(overlay, 0, sink, 0);
        REQUIRE(pipeline.start().isOk());
        CHECK(overlay->state() == MediaNode::Running);

        // Trying to start again should fail
        Error err2 = pipeline.start();
        CHECK(err2.isError());
}

// ============================================================================
// Frame with no images (passthrough)
// ============================================================================

TEST_CASE("TimecodeOverlayNode_EmptyFramePassthrough") {
        if(!fontAvailable()) return;

        MediaPipeline pipeline;
        ImageSourceNode *src = new ImageSourceNode();
        TimecodeOverlayNode *overlay = new TimecodeOverlayNode();
        ImageCaptureSink *sink = new ImageCaptureSink();

        src->build(MediaNodeConfig());

        MediaNodeConfig overlayCfg("TimecodeOverlayNode", "overlay");
        overlay->build(overlayCfg);

        sink->build(MediaNodeConfig());

        pipeline.addNode(src);
        pipeline.addNode(overlay);
        pipeline.addNode(sink);
        pipeline.connect(src, 0, overlay, 0);
        pipeline.connect(overlay, 0, sink, 0);
        pipeline.start();

        // Push a frame with no images
        Frame::Ptr frame = Frame::Ptr::create();
        src->pushFrame(frame);

        for(int i = 0; i < 200 && sink->count() < 1; i++) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        pipeline.stop();

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

        MediaPipeline pipeline;
        ImageSourceNode *src = new ImageSourceNode();
        TimecodeOverlayNode *overlay = new TimecodeOverlayNode();
        ImageCaptureSink *sink = new ImageCaptureSink();

        src->build(MediaNodeConfig());

        MediaNodeConfig overlayCfg("TimecodeOverlayNode", "overlay");
        overlayCfg.set("FontSize", 24);
        overlay->build(overlayCfg);

        sink->build(MediaNodeConfig());

        pipeline.addNode(src);
        pipeline.addNode(overlay);
        pipeline.addNode(sink);
        pipeline.connect(src, 0, overlay, 0);
        pipeline.connect(overlay, 0, sink, 0);
        pipeline.start();

        // Push an image with NO timecode metadata
        ImageDesc idesc(320, 240, PixelDesc::RGB8_sRGB);
        Image img(idesc);
        img.fill(0);

        Frame::Ptr frame = Frame::Ptr::create();
        frame.modify()->imageList().pushToBack(Image::Ptr::create(img));
        src->pushFrame(frame);

        for(int i = 0; i < 200 && sink->count() < 1; i++) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        pipeline.stop();

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
                MediaNodeConfig overlayCfg("TimecodeOverlayNode", "overlay");
                        overlayCfg.set("FontSize", 24);
                overlayCfg.set("Position", "topleft");
                if(!customText.isEmpty()) overlayCfg.set("CustomText", customText);

                ImageDesc idesc(320, 240, PixelDesc::RGB8_sRGB);
                Image img(idesc);
                img.fill(0);
                img.metadata().set(Metadata::Timecode, Timecode(Timecode::NDF24, 1, 0, 0, 0));

                Frame::Ptr outFrame = overlayImage(img, overlayCfg);
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
                MediaNodeConfig overlayCfg("TimecodeOverlayNode", "overlay");
                        overlayCfg.set("FontSize", 24);
                overlayCfg.set("DrawBackground", drawBg);

                ImageDesc idesc(320, 240, PixelDesc::RGB8_sRGB);
                Image img(idesc);
                // Fill with mid-gray so we can detect both black bg and white text
                img.fill(128);
                img.metadata().set(Metadata::Timecode, Timecode(Timecode::NDF24, 1, 0, 0, 0));

                Frame::Ptr outFrame = overlayImage(img, overlayCfg);
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

        MediaPipeline pipeline;
        ImageSourceNode *src = new ImageSourceNode();
        TimecodeOverlayNode *overlay = new TimecodeOverlayNode();
        ImageCaptureSink *sink = new ImageCaptureSink();

        src->build(MediaNodeConfig());

        MediaNodeConfig overlayCfg("TimecodeOverlayNode", "overlay");
        overlayCfg.set("FontSize", 24);
        overlayCfg.set("Position", "topleft");
        overlay->build(overlayCfg);

        sink->build(MediaNodeConfig());

        pipeline.addNode(src);
        pipeline.addNode(overlay);
        pipeline.addNode(sink);
        pipeline.connect(src, 0, overlay, 0);
        pipeline.connect(overlay, 0, sink, 0);
        pipeline.start();

        ImageDesc idesc(640, 480, PixelDesc::RGB8_sRGB);
        Image img(idesc);
        img.fill(0);
        img.metadata().set(Metadata::Timecode, Timecode(Timecode::NDF24, 1, 0, 0, 0));

        Frame::Ptr frame = Frame::Ptr::create();
        frame.modify()->imageList().pushToBack(Image::Ptr::create(img));
        src->pushFrame(frame);

        for(int i = 0; i < 200 && sink->count() < 1; i++) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        pipeline.stop();

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

        MediaPipeline pipeline;
        ImageSourceNode *src = new ImageSourceNode();
        TimecodeOverlayNode *overlay = new TimecodeOverlayNode();
        ImageCaptureSink *sink = new ImageCaptureSink();

        src->build(MediaNodeConfig());

        MediaNodeConfig overlayCfg("TimecodeOverlayNode", "overlay");
        overlayCfg.set("FontSize", 24);
        overlayCfg.set("Position", "bottomright");
        overlay->build(overlayCfg);

        sink->build(MediaNodeConfig());

        pipeline.addNode(src);
        pipeline.addNode(overlay);
        pipeline.addNode(sink);
        pipeline.connect(src, 0, overlay, 0);
        pipeline.connect(overlay, 0, sink, 0);
        pipeline.start();

        ImageDesc idesc(640, 480, PixelDesc::RGB8_sRGB);
        Image img(idesc);
        img.fill(0);
        img.metadata().set(Metadata::Timecode, Timecode(Timecode::NDF24, 1, 0, 0, 0));

        Frame::Ptr frame = Frame::Ptr::create();
        frame.modify()->imageList().pushToBack(Image::Ptr::create(img));
        src->pushFrame(frame);

        for(int i = 0; i < 200 && sink->count() < 1; i++) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        pipeline.stop();

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

        MediaPipeline pipeline;
        ImageSourceNode *src = new ImageSourceNode();
        TimecodeOverlayNode *overlay = new TimecodeOverlayNode();
        ImageCaptureSink *sink = new ImageCaptureSink();

        src->build(MediaNodeConfig());

        MediaNodeConfig overlayCfg("TimecodeOverlayNode", "overlay");
        overlayCfg.set("FontSize", 24);
        overlay->build(overlayCfg);

        sink->build(MediaNodeConfig());

        pipeline.addNode(src);
        pipeline.addNode(overlay);
        pipeline.addNode(sink);
        pipeline.connect(src, 0, overlay, 0);
        pipeline.connect(overlay, 0, sink, 0);
        pipeline.start();

        // Push 3 frames with different timecodes
        for(int i = 0; i < 3; i++) {
                ImageDesc idesc(320, 240, PixelDesc::RGB8_sRGB);
                Image img(idesc);
                img.fill(0);
                img.metadata().set(Metadata::Timecode, Timecode(Timecode::NDF24, i, 0, 0, 0));

                Frame::Ptr frame = Frame::Ptr::create();
                frame.modify()->imageList().pushToBack(Image::Ptr::create(img));
                src->pushFrame(frame);
        }

        // Wait for all frames to flow through the pipeline
        for(int i = 0; i < 200 && sink->count() < 3; i++) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        pipeline.stop();

        CHECK(sink->count() == 3);
}

// ============================================================================
// RGBA8 image support
// ============================================================================

TEST_CASE("TimecodeOverlayNode_RGBA8") {
        if(!fontAvailable()) return;

        MediaPipeline pipeline;
        ImageSourceNode *src = new ImageSourceNode();
        TimecodeOverlayNode *overlay = new TimecodeOverlayNode();
        ImageCaptureSink *sink = new ImageCaptureSink();

        src->build(MediaNodeConfig());

        MediaNodeConfig overlayCfg("TimecodeOverlayNode", "overlay");
        overlayCfg.set("FontSize", 36);
        overlay->build(overlayCfg);

        sink->build(MediaNodeConfig());

        pipeline.addNode(src);
        pipeline.addNode(overlay);
        pipeline.addNode(sink);
        pipeline.connect(src, 0, overlay, 0);
        pipeline.connect(overlay, 0, sink, 0);
        pipeline.start();

        ImageDesc idesc(320, 240, PixelDesc::RGBA8_sRGB);
        Image img(idesc);
        img.fill(0);
        img.metadata().set(Metadata::Timecode, Timecode(Timecode::NDF24, 1, 0, 0, 0));

        Frame::Ptr frame = Frame::Ptr::create();
        frame.modify()->imageList().pushToBack(Image::Ptr::create(img));
        src->pushFrame(frame);

        for(int i = 0; i < 200 && sink->count() < 1; i++) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        pipeline.stop();

        REQUIRE(sink->count() == 1);
        Image::Ptr outImg = sink->lastFrame()->imageList()[0];
        const uint8_t *outData = (const uint8_t *)outImg->data();
        size_t outSize = outImg->lineStride() * outImg->height();
        uint64_t outSum = 0;
        for(size_t i = 0; i < outSize; i++) outSum += outData[i];
        CHECK(outSum > 0);
}

// ============================================================================
// defaultConfig
// ============================================================================

TEST_CASE("TimecodeOverlayNode_DefaultConfig") {
        TimecodeOverlayNode node;
        MediaNodeConfig cfg = node.defaultConfig();
        CHECK(cfg.type() == "TimecodeOverlayNode");
        CHECK(cfg.get("FontSize").get<int>() == 36);
        CHECK(cfg.get("Position").get<String>() == "bottomcenter");
        CHECK(cfg.get("DrawBackground").get<bool>() == true);
        CHECK(cfg.get("TextColor").get<Color>() == Color::White);
        CHECK(cfg.get("BackgroundColor").get<Color>() == Color::Black);
}

// ============================================================================
// Custom text color and background color
// ============================================================================

TEST_CASE("TimecodeOverlayNode_CustomColors") {
        if(!fontAvailable()) return;

        // Render with red text on green background and verify output differs
        // from default white on black.
        auto renderAndSum = [](Color textColor, Color bgColor) -> uint64_t {
                MediaNodeConfig overlayCfg("TimecodeOverlayNode", "overlay");
                        overlayCfg.set("FontSize", 24);
                overlayCfg.set("Position", "topleft");
                overlayCfg.set("TextColor", textColor);
                overlayCfg.set("BackgroundColor", bgColor);

                ImageDesc idesc(320, 240, PixelDesc::RGB8_sRGB);
                Image img(idesc);
                img.fill(0);
                img.metadata().set(Metadata::Timecode, Timecode(Timecode::NDF24, 1, 0, 0, 0));

                Frame::Ptr outFrame = overlayImage(img, overlayCfg);
                if(!outFrame.isValid()) return 0;
                Image::Ptr outImg = outFrame->imageList()[0];
                const uint8_t *data = (const uint8_t *)outImg->data();
                size_t dataSize = outImg->lineStride() * outImg->height();
                uint64_t sum = 0;
                for(size_t i = 0; i < dataSize; i++) sum += data[i];
                return sum;
        };

        uint64_t sumDefault = renderAndSum(Color::White, Color::Black);
        uint64_t sumCustom = renderAndSum(Color::Red, Color::Green);
        // Both should render something (non-zero), but different color combos
        // produce different pixel sums
        CHECK(sumDefault > 0);
        CHECK(sumCustom > 0);
        CHECK(sumDefault != sumCustom);
}
