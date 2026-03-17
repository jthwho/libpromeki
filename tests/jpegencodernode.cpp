/**
 * @file      tests/jpegencodernode.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/proav/jpegencodernode.h>
#include <promeki/proav/mediagraph.h>
#include <promeki/proav/frame.h>
#include <promeki/proav/image.h>
#include <promeki/proav/imagedesc.h>
#include <promeki/proav/pixelformat.h>
#include <promeki/core/buffer.h>
#include <promeki/core/metadata.h>
#include <promeki/core/timecode.h>

using namespace promeki;

// ============================================================================
// Helper: source node that pushes Image frames
// ============================================================================

class JpegTestSourceNode : public MediaNode {
        PROMEKI_OBJECT(JpegTestSourceNode, MediaNode)
        public:
                JpegTestSourceNode() : MediaNode() {
                        setName("JpegTestSource");
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

class JpegCaptureSink : public MediaNode {
        PROMEKI_OBJECT(JpegCaptureSink, MediaNode)
        public:
                JpegCaptureSink() : MediaNode() {
                        setName("JpegCaptureSink");
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
// Helper: create a test image with a color gradient
// ============================================================================

static Image createTestImage(int width, int height, int pixfmt = PixelFormat::RGB8) {
        ImageDesc idesc(width, height, pixfmt);
        Image img(idesc);
        int comps = (pixfmt == PixelFormat::RGBA8) ? 4 : 3;
        uint8_t *data = static_cast<uint8_t *>(img.data());
        size_t stride = img.lineStride();
        for(int y = 0; y < height; y++) {
                uint8_t *row = data + y * stride;
                for(int x = 0; x < width; x++) {
                        row[x * comps + 0] = (uint8_t)(x * 255 / width);
                        row[x * comps + 1] = (uint8_t)(y * 255 / height);
                        row[x * comps + 2] = 128;
                        if(comps == 4) row[x * comps + 3] = 255;
                }
        }
        return img;
}

// Helper: push an image through an encoder pipeline, return the output frame.
static Frame::Ptr encodeImage(const Image &img, int quality = 85) {
        MediaGraph graph;
        JpegTestSourceNode *src = new JpegTestSourceNode();
        JpegEncoderNode *enc = new JpegEncoderNode();
        JpegCaptureSink *sink = new JpegCaptureSink();

        enc->setQuality(quality);
        graph.addNode(src);
        graph.addNode(enc);
        graph.addNode(sink);
        graph.connect(src, 0, enc, 0);
        graph.connect(enc, 0, sink, 0);
        enc->configure();

        Frame::Ptr frame = Frame::Ptr::create();
        frame.modify()->imageList().pushToBack(Image::Ptr::create(img));
        frame.modify()->metadata() = img.metadata();
        src->pushFrame(frame);
        while(enc->queuedFrameCount() > 0) enc->process();
        sink->drain();
        return sink->lastFrame();
}

// ============================================================================
// Construction
// ============================================================================

TEST_CASE("JpegEncoderNode_Construct") {
        JpegEncoderNode node;
        CHECK(node.name() == "JpegEncoderNode");
        CHECK(node.inputPortCount() == 1);
        CHECK(node.outputPortCount() == 1);
        CHECK(node.inputPort(0)->mediaType() == MediaPort::Image);
        CHECK(node.outputPort(0)->mediaType() == MediaPort::Image);
        CHECK(node.quality() == 85);
}

// ============================================================================
// Configure
// ============================================================================

TEST_CASE("JpegEncoderNode_ConfigureOk") {
        JpegEncoderNode node;
        Error err = node.configure();
        CHECK(err.isOk());
        CHECK(node.state() == MediaNode::Configured);
}

TEST_CASE("JpegEncoderNode_ConfigureNotIdleReturnsError") {
        JpegEncoderNode node;
        REQUIRE(node.configure().isOk());
        CHECK(node.configure().isError());
}

// ============================================================================
// Quality setter/getter
// ============================================================================

TEST_CASE("JpegEncoderNode_SetQuality") {
        JpegEncoderNode node;
        CHECK(node.quality() == 85);
        node.setQuality(50);
        CHECK(node.quality() == 50);
}

// ============================================================================
// Encode RGB8 image — verify JPEG pixel format and header bytes
// ============================================================================

TEST_CASE("JpegEncoderNode_EncodeRGB8") {
        Image img = createTestImage(320, 240);
        Frame::Ptr out = encodeImage(img);

        REQUIRE(out.isValid());
        REQUIRE(out->imageList().size() == 1);

        Image::Ptr jpegImg = out->imageList()[0];
        CHECK(jpegImg->pixelFormatID() == PixelFormat::JPEG_RGB8);
        CHECK(jpegImg->isCompressed());

        size_t compSize = jpegImg->compressedSize();
        CHECK(compSize > 0);

        // Verify JPEG magic bytes: 0xFF 0xD8 (SOI) and 0xFF 0xD9 (EOI)
        const uint8_t *data = static_cast<const uint8_t *>(jpegImg->data());
        CHECK(data[0] == 0xFF);
        CHECK(data[1] == 0xD8);
        CHECK(data[compSize - 2] == 0xFF);
        CHECK(data[compSize - 1] == 0xD9);
}

// ============================================================================
// Encode RGBA8 image
// ============================================================================

TEST_CASE("JpegEncoderNode_EncodeRGBA8") {
        Image img = createTestImage(160, 120, PixelFormat::RGBA8);
        Frame::Ptr out = encodeImage(img);

        REQUIRE(out.isValid());
        Image::Ptr jpegImg = out->imageList()[0];
        CHECK(jpegImg->pixelFormatID() == PixelFormat::JPEG_RGBA8);

        const uint8_t *data = static_cast<const uint8_t *>(jpegImg->data());
        CHECK(data[0] == 0xFF);
        CHECK(data[1] == 0xD8);
}

// ============================================================================
// Metadata preserved on output frame
// ============================================================================

TEST_CASE("JpegEncoderNode_MetadataPreserved") {
        Image img = createTestImage(64, 64);
        Timecode tc(Timecode::NDF24, 1, 30, 15, 10);
        img.metadata().set(Metadata::Timecode, tc);

        Frame::Ptr out = encodeImage(img);
        REQUIRE(out.isValid());

        // Frame-level metadata
        REQUIRE(out->metadata().contains(Metadata::Timecode));
        Timecode outTc = out->metadata().get(Metadata::Timecode).get<Timecode>();
        CHECK(outTc.hour() == 1);
        CHECK(outTc.min() == 30);
        CHECK(outTc.sec() == 15);
        CHECK(outTc.frame() == 10);

        // Image-level metadata (timecode propagated to JPEG image)
        Image::Ptr jpegImg = out->imageList()[0];
        REQUIRE(jpegImg->metadata().contains(Metadata::Timecode));
        Timecode imgTc = jpegImg->metadata().get(Metadata::Timecode).get<Timecode>();
        CHECK(imgTc.hour() == 1);
}

// ============================================================================
// Empty frame passthrough
// ============================================================================

TEST_CASE("JpegEncoderNode_EmptyFramePassthrough") {
        MediaGraph graph;
        JpegTestSourceNode *src = new JpegTestSourceNode();
        JpegEncoderNode *enc = new JpegEncoderNode();
        JpegCaptureSink *sink = new JpegCaptureSink();

        graph.addNode(src);
        graph.addNode(enc);
        graph.addNode(sink);
        graph.connect(src, 0, enc, 0);
        graph.connect(enc, 0, sink, 0);
        enc->configure();

        Frame::Ptr frame = Frame::Ptr::create();
        src->pushFrame(frame);
        while(enc->queuedFrameCount() > 0) enc->process();
        sink->drain();

        REQUIRE(sink->count() == 1);
        CHECK(sink->lastFrame()->imageList().isEmpty());
}

// ============================================================================
// Multiple frames
// ============================================================================

TEST_CASE("JpegEncoderNode_MultipleFrames") {
        MediaGraph graph;
        JpegTestSourceNode *src = new JpegTestSourceNode();
        JpegEncoderNode *enc = new JpegEncoderNode();
        JpegCaptureSink *sink = new JpegCaptureSink();

        graph.addNode(src);
        graph.addNode(enc);
        graph.addNode(sink);
        graph.connect(src, 0, enc, 0);
        graph.connect(enc, 0, sink, 0);
        enc->configure();

        for(int i = 0; i < 5; i++) {
                Image img = createTestImage(64, 64);
                Frame::Ptr frame = Frame::Ptr::create();
                frame.modify()->imageList().pushToBack(Image::Ptr::create(img));
                src->pushFrame(frame);
        }
        while(enc->queuedFrameCount() > 0) enc->process();
        sink->drain();

        CHECK(sink->count() == 5);
}

// ============================================================================
// Quality affects output size
// ============================================================================

TEST_CASE("JpegEncoderNode_QualityAffectsSize") {
        Image img = createTestImage(320, 240);

        Frame::Ptr lowQ = encodeImage(img, 10);
        Frame::Ptr highQ = encodeImage(img, 95);

        size_t lowSize = lowQ->imageList()[0]->compressedSize();
        size_t highSize = highQ->imageList()[0]->compressedSize();
        CHECK(highSize > lowSize);
}

// ============================================================================
// Extended stats
// ============================================================================

TEST_CASE("JpegEncoderNode_ExtendedStats") {
        MediaGraph graph;
        JpegTestSourceNode *src = new JpegTestSourceNode();
        JpegEncoderNode *enc = new JpegEncoderNode();
        JpegCaptureSink *sink = new JpegCaptureSink();

        graph.addNode(src);
        graph.addNode(enc);
        graph.addNode(sink);
        graph.connect(src, 0, enc, 0);
        graph.connect(enc, 0, sink, 0);
        enc->configure();

        auto stats0 = enc->extendedStats();
        CHECK(stats0["framesEncoded"].get<uint64_t>() == 0);
        CHECK_FALSE(stats0.contains("avgCompressedSize"));
        CHECK_FALSE(stats0.contains("compressionRatio"));

        Image img = createTestImage(64, 64);
        Frame::Ptr frame = Frame::Ptr::create();
        frame.modify()->imageList().pushToBack(Image::Ptr::create(img));
        src->pushFrame(frame);
        while(enc->queuedFrameCount() > 0) enc->process();
        sink->drain();

        auto stats1 = enc->extendedStats();
        CHECK(stats1["framesEncoded"].get<uint64_t>() == 1);
        CHECK(stats1["avgCompressedSize"].get<uint64_t>() > 0);
        CHECK(stats1["compressionRatio"].get<double>() > 1.0);
}

// ============================================================================
// Registry
// ============================================================================

TEST_CASE("JpegEncoderNode_Registry") {
        auto types = MediaNode::registeredNodeTypes();
        CHECK(types.contains("JpegEncoderNode"));
}
