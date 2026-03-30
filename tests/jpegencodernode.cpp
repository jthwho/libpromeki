/**
 * @file      tests/jpegencodernode.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <atomic>
#include <thread>
#include <doctest/doctest.h>
#include <promeki/proav/jpegencodernode.h>
#include <promeki/proav/mediapipeline.h>
#include <promeki/proav/medianodeconfig.h>
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

class JpegCaptureSink : public MediaNode {
        PROMEKI_OBJECT(JpegCaptureSink, MediaNode)
        public:
                JpegCaptureSink() : MediaNode() {
                        setName("JpegCaptureSink");
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
        MediaPipeline pipeline;
        JpegTestSourceNode *src = new JpegTestSourceNode();
        JpegEncoderNode *enc = new JpegEncoderNode();
        JpegCaptureSink *sink = new JpegCaptureSink();

        MediaNodeConfig encCfg("JpegEncoderNode", "enc");
        encCfg.set("quality", Variant(quality));
        enc->build(encCfg);

        pipeline.addNode(src);
        pipeline.addNode(enc);
        pipeline.addNode(sink);
        pipeline.connect(src, 0, enc, 0);
        pipeline.connect(enc, 0, sink, 0);

        src->build(MediaNodeConfig());
        sink->build(MediaNodeConfig());

        pipeline.start();

        Frame::Ptr frame = Frame::Ptr::create();
        frame.modify()->imageList().pushToBack(Image::Ptr::create(img));
        frame.modify()->metadata() = img.metadata();
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

TEST_CASE("JpegEncoderNode_Construct") {
        JpegEncoderNode node;
        CHECK(node.name() == "JpegEncoderNode");
        CHECK(node.sinkCount() == 1);
        CHECK(node.sourceCount() == 1);
        CHECK(node.sink(0)->contentHint() == ContentVideo);
        CHECK(node.source(0)->contentHint() == ContentVideo);
}

// ============================================================================
// Configure
// ============================================================================

TEST_CASE("JpegEncoderNode_ConfigureOk") {
        MediaPipeline pipeline;
        JpegTestSourceNode *src = new JpegTestSourceNode();
        JpegEncoderNode *enc = new JpegEncoderNode();
        JpegCaptureSink *sink = new JpegCaptureSink();

        pipeline.addNode(src);
        pipeline.addNode(enc);
        pipeline.addNode(sink);
        pipeline.connect(src, 0, enc, 0);
        pipeline.connect(enc, 0, sink, 0);

        src->build(MediaNodeConfig());
        enc->build(MediaNodeConfig());
        sink->build(MediaNodeConfig());

        Error err = pipeline.start();
        CHECK(err.isOk());
        CHECK(enc->state() == MediaNode::Running);
}

TEST_CASE("JpegEncoderNode_ConfigureNotIdleReturnsError") {
        MediaPipeline pipeline;
        JpegTestSourceNode *src = new JpegTestSourceNode();
        JpegEncoderNode *enc = new JpegEncoderNode();
        JpegCaptureSink *sink = new JpegCaptureSink();

        pipeline.addNode(src);
        pipeline.addNode(enc);
        pipeline.addNode(sink);
        pipeline.connect(src, 0, enc, 0);
        pipeline.connect(enc, 0, sink, 0);

        src->build(MediaNodeConfig());
        enc->build(MediaNodeConfig());
        sink->build(MediaNodeConfig());

        REQUIRE(pipeline.start().isOk());
        CHECK(pipeline.start().isError()); // Already running
}

// ============================================================================
// Encode RGB8 image -- verify JPEG pixel format and header bytes
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
        MediaPipeline pipeline;
        JpegTestSourceNode *src = new JpegTestSourceNode();
        JpegEncoderNode *enc = new JpegEncoderNode();
        JpegCaptureSink *sink = new JpegCaptureSink();

        pipeline.addNode(src);
        pipeline.addNode(enc);
        pipeline.addNode(sink);
        pipeline.connect(src, 0, enc, 0);
        pipeline.connect(enc, 0, sink, 0);

        src->build(MediaNodeConfig());
        enc->build(MediaNodeConfig());
        sink->build(MediaNodeConfig());

        pipeline.start();

        Frame::Ptr frame = Frame::Ptr::create();
        src->pushFrame(frame);

        for(int i = 0; i < 200 && sink->count() < 1; i++) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        pipeline.stop();

        REQUIRE(sink->count() == 1);
        CHECK(sink->lastFrame()->imageList().isEmpty());
}

// ============================================================================
// Multiple frames
// ============================================================================

TEST_CASE("JpegEncoderNode_MultipleFrames") {
        MediaPipeline pipeline;
        JpegTestSourceNode *src = new JpegTestSourceNode();
        JpegEncoderNode *enc = new JpegEncoderNode();
        JpegCaptureSink *sink = new JpegCaptureSink();

        pipeline.addNode(src);
        pipeline.addNode(enc);
        pipeline.addNode(sink);
        pipeline.connect(src, 0, enc, 0);
        pipeline.connect(enc, 0, sink, 0);

        src->build(MediaNodeConfig());
        enc->build(MediaNodeConfig());
        sink->build(MediaNodeConfig());

        pipeline.start();

        for(int i = 0; i < 5; i++) {
                Image img = createTestImage(64, 64);
                Frame::Ptr frame = Frame::Ptr::create();
                frame.modify()->imageList().pushToBack(Image::Ptr::create(img));
                src->pushFrame(frame);
        }

        // Wait for all frames to flow through the pipeline
        for(int i = 0; i < 200 && sink->count() < 5; i++) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        pipeline.stop();

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
        MediaPipeline pipeline;
        JpegTestSourceNode *src = new JpegTestSourceNode();
        JpegEncoderNode *enc = new JpegEncoderNode();
        JpegCaptureSink *sink = new JpegCaptureSink();

        pipeline.addNode(src);
        pipeline.addNode(enc);
        pipeline.addNode(sink);
        pipeline.connect(src, 0, enc, 0);
        pipeline.connect(enc, 0, sink, 0);

        src->build(MediaNodeConfig());
        enc->build(MediaNodeConfig());
        sink->build(MediaNodeConfig());

        pipeline.start();

        auto stats0 = enc->extendedStats();
        CHECK(stats0["framesEncoded"].get<uint64_t>() == 0);
        CHECK_FALSE(stats0.contains("avgCompressedSize"));
        CHECK_FALSE(stats0.contains("compressionRatio"));

        Image img = createTestImage(64, 64);
        Frame::Ptr frame = Frame::Ptr::create();
        frame.modify()->imageList().pushToBack(Image::Ptr::create(img));
        src->pushFrame(frame);

        for(int i = 0; i < 200 && sink->count() < 1; i++) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        pipeline.stop();

        auto stats1 = enc->extendedStats();
        CHECK(stats1["framesEncoded"].get<uint64_t>() == 1);
        CHECK(stats1["avgCompressedSize"].get<uint64_t>() > 0);
        CHECK(stats1["compressionRatio"].get<double>() > 1.0);
}

// ============================================================================
// Verify JPEG internal structure for RFC 2435 compatibility
// ============================================================================

// Scan for a JPEG marker (FF xx) starting at pos, return its position or size on failure.
static size_t scanMarker(const uint8_t *data, size_t size, uint8_t marker, size_t startPos = 0) {
        for(size_t i = startPos; i + 1 < size; i++) {
                if(data[i] == 0xFF && data[i + 1] == marker) return i;
        }
        return size;
}

// Read the 2-byte big-endian segment length at data[pos+2..pos+3].
static uint16_t segmentLength(const uint8_t *data, size_t pos) {
        return (static_cast<uint16_t>(data[pos + 2]) << 8) | data[pos + 3];
}

TEST_CASE("JpegEncoderNode_JpegStructureForRfc2435") {
        Image img = createTestImage(640, 480);
        Frame::Ptr out = encodeImage(img);

        REQUIRE(out.isValid());
        REQUIRE(out->imageList().size() == 1);

        Image::Ptr jpegImg = out->imageList()[0];
        REQUIRE(jpegImg->isCompressed());

        const uint8_t *data = static_cast<const uint8_t *>(jpegImg->data());
        size_t size = jpegImg->compressedSize();
        REQUIRE(size > 100);

        // 1. SOI at start
        CHECK(data[0] == 0xFF);
        CHECK(data[1] == 0xD8);

        // 2. Find DQT markers -- should have quantization tables
        size_t dqt1 = scanMarker(data, size, 0xDB, 2);
        REQUIRE(dqt1 < size);
        uint16_t dqt1Len = segmentLength(data, dqt1);
        MESSAGE("DQT1 at offset " << dqt1 << ", length " << dqt1Len);

        // 3. Find SOF0 -- verify 4:2:2 subsampling
        size_t sof0 = scanMarker(data, size, 0xC0, 2);
        REQUIRE(sof0 < size);
        uint16_t sofLen = segmentLength(data, sof0);
        MESSAGE("SOF0 at offset " << sof0 << ", length " << sofLen);

        // SOF0 format: FF C0 Lf P Y X Nf [component data...]
        // P = precision (byte 4), Y = height (bytes 5-6), X = width (bytes 7-8)
        // Nf = number of components (byte 9)
        uint8_t precision = data[sof0 + 4];
        uint16_t height = (data[sof0 + 5] << 8) | data[sof0 + 6];
        uint16_t width = (data[sof0 + 7] << 8) | data[sof0 + 8];
        uint8_t numComponents = data[sof0 + 9];
        CHECK(precision == 8);
        CHECK(height == 480);
        CHECK(width == 640);
        CHECK(numComponents == 3);

        // Component 0 (Y): id, sampling factors (H<<4|V), quant table
        uint8_t y_id = data[sof0 + 10];
        uint8_t y_samp = data[sof0 + 11];
        uint8_t y_qt = data[sof0 + 12];
        MESSAGE("Y: id=" << (int)y_id << " samp=0x" << std::hex << (int)y_samp
                << " qt=" << std::dec << (int)y_qt);
        CHECK(y_samp == 0x21); // H=2, V=1 -> 4:2:2

        // Component 1 (Cb): sampling should be 1x1
        uint8_t cb_samp = data[sof0 + 14];
        CHECK(cb_samp == 0x11); // H=1, V=1

        // Component 2 (Cr): sampling should be 1x1
        uint8_t cr_samp = data[sof0 + 17];
        CHECK(cr_samp == 0x11); // H=1, V=1

        // 4. Find DHT markers
        size_t dht1 = scanMarker(data, size, 0xC4, 2);
        REQUIRE(dht1 < size);
        MESSAGE("First DHT at offset " << dht1);

        // 5. Find SOS marker
        size_t sos = scanMarker(data, size, 0xDA, 2);
        REQUIRE(sos < size);
        uint16_t sosLen = segmentLength(data, sos);
        size_t entropyStart = sos + 2 + sosLen;
        MESSAGE("SOS at offset " << sos << ", length " << sosLen
                << ", entropy starts at " << entropyStart);
        CHECK(entropyStart < size);

        // 6. EOI at end
        CHECK(data[size - 2] == 0xFF);
        CHECK(data[size - 1] == 0xD9);

        // 7. Verify the entropy data is a significant portion of the file
        size_t entropySize = size - entropyStart;
        MESSAGE("Entropy data size: " << entropySize << " / " << size << " total");
        CHECK(entropySize > 100);

        // 8. Print all markers found in order (for debugging)
        MESSAGE("=== JPEG marker map ===");
        for(size_t i = 0; i + 1 < size; i++) {
                if(data[i] == 0xFF && data[i + 1] != 0x00 && data[i + 1] != 0xFF) {
                        uint8_t code = data[i + 1];
                        // Skip RST markers and entropy coded 0xFF00
                        if(code >= 0xD0 && code <= 0xD7) continue;
                        if(code == 0xD8 || code == 0xD9) {
                                MESSAGE("  0x" << std::hex << (int)code << " at offset " << std::dec << i);
                        } else {
                                uint16_t len = segmentLength(data, i);
                                MESSAGE("  0x" << std::hex << (int)code << " at offset " << std::dec << i
                                        << ", length " << len);
                        }
                }
        }
}

// ============================================================================
// Registry
// ============================================================================

TEST_CASE("JpegEncoderNode_Registry") {
        auto types = MediaNode::registeredNodeTypes();
        CHECK(types.contains("JpegEncoderNode"));
}
