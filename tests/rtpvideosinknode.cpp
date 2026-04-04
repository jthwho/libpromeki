/**
 * @file      tests/rtpvideosinknode.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstring>
#include <thread>
#include <doctest/doctest.h>
#include <promeki/rtpvideosinknode.h>
#include <promeki/mediapipeline.h>
#include <promeki/medianodeconfig.h>
#include <promeki/frame.h>
#include <promeki/image.h>
#include <promeki/imagedesc.h>
#include <promeki/pixeldesc.h>
#include <promeki/buffer.h>

#include <promeki/socketaddress.h>
#include <promeki/udpsocket.h>
#include <promeki/rtppayload.h>
#include <promeki/rtppacket.h>

using namespace promeki;

// ============================================================================
// Helper: source node that pushes Image frames
// ============================================================================

class RtpVideoTestSource : public MediaNode {
        PROMEKI_OBJECT(RtpVideoTestSource, MediaNode)
        public:
                RtpVideoTestSource() : MediaNode() {
                        setName("RtpVideoTestSource");
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
// Helper: create a small test image
// ============================================================================

static Image createSmallTestImage(int width = 64, int height = 64) {
        ImageDesc idesc(width, height, PixelDesc::RGB8_sRGB_Full);
        Image img(idesc);
        uint8_t *data = static_cast<uint8_t *>(img.data());
        size_t stride = img.lineStride();
        for(int y = 0; y < height; y++) {
                uint8_t *row = data + y * stride;
                for(int x = 0; x < width; x++) {
                        row[x * 3 + 0] = (uint8_t)(x * 255 / width);
                        row[x * 3 + 1] = (uint8_t)(y * 255 / height);
                        row[x * 3 + 2] = 128;
                }
        }
        return img;
}

// ============================================================================
// Construction
// ============================================================================

TEST_CASE("RtpVideoSinkNode_Construct") {
        RtpVideoSinkNode node;
        CHECK(node.name() == "RtpVideoSinkNode");
        CHECK(node.sinkCount() == 1);
        CHECK(node.sourceCount() == 0);
        CHECK(node.sink(0)->contentHint() == ContentVideo);
}

// ============================================================================
// Registry
// ============================================================================

TEST_CASE("RtpVideoSinkNode_Registry") {
        auto types = MediaNode::registeredNodeTypes();
        CHECK(types.contains("RtpVideoSinkNode"));
}

// ============================================================================
// Port structure
// ============================================================================

TEST_CASE("RtpVideoSinkNode_PortStructure") {
        RtpVideoSinkNode node;
        CHECK(node.sinkCount() == 1);
        CHECK(node.sourceCount() == 0);
        CHECK(node.sink(0)->name() == "input");
        CHECK(node.sink(0)->contentHint() == ContentVideo);
}

// ============================================================================
// Configure failure: no payload set
// ============================================================================

TEST_CASE("RtpVideoSinkNode_ConfigureFailNoPayload") {
        MediaPipeline pipeline;
        RtpVideoTestSource *src = new RtpVideoTestSource();
        RtpVideoSinkNode *sink = new RtpVideoSinkNode();

        src->build(MediaNodeConfig());

        MediaNodeConfig sinkCfg("RtpVideoSinkNode", "sink");
        sinkCfg.set("Destination", "127.0.0.1:5004");
        sinkCfg.set("FrameRate", "30");
        BuildResult result = sink->build(sinkCfg);
        CHECK(result.isError());

        pipeline.addNode(src);
        pipeline.addNode(sink);
        pipeline.connect(src, 0, sink, 0);
        Error err = pipeline.start();
        CHECK(err.isError());
}

// ============================================================================
// Configure failure: no destination
// ============================================================================

TEST_CASE("RtpVideoSinkNode_ConfigureFailNoDestination") {
        RtpPayloadRawVideo payload(64, 64, 24);
        MediaPipeline pipeline;
        RtpVideoTestSource *src = new RtpVideoTestSource();
        RtpVideoSinkNode *sink = new RtpVideoSinkNode();

        src->build(MediaNodeConfig());

        MediaNodeConfig sinkCfg("RtpVideoSinkNode", "sink");
        sinkCfg.set("RtpPayload", reinterpret_cast<uint64_t>(&payload));
        sinkCfg.set("FrameRate", "30");
        BuildResult result = sink->build(sinkCfg);
        CHECK(result.isError());

        pipeline.addNode(src);
        pipeline.addNode(sink);
        pipeline.connect(src, 0, sink, 0);
        Error err = pipeline.start();
        CHECK(err.isError());
}

// ============================================================================
// Configure failure: no frame rate
// ============================================================================

TEST_CASE("RtpVideoSinkNode_ConfigureFailNoFrameRate") {
        RtpPayloadRawVideo payload(64, 64, 24);
        MediaPipeline pipeline;
        RtpVideoTestSource *src = new RtpVideoTestSource();
        RtpVideoSinkNode *sink = new RtpVideoSinkNode();

        src->build(MediaNodeConfig());

        MediaNodeConfig sinkCfg("RtpVideoSinkNode", "sink");
        sinkCfg.set("RtpPayload", reinterpret_cast<uint64_t>(&payload));
        sinkCfg.set("Destination", "127.0.0.1:5004");
        BuildResult result = sink->build(sinkCfg);
        CHECK(result.isError());

        pipeline.addNode(src);
        pipeline.addNode(sink);
        pipeline.connect(src, 0, sink, 0);
        Error err = pipeline.start();
        CHECK(err.isError());
}

// ============================================================================
// Configure success
// ============================================================================

TEST_CASE("RtpVideoSinkNode_ConfigureSuccess") {
        RtpPayloadRawVideo payload(64, 64, 24);
        MediaPipeline pipeline;
        RtpVideoTestSource *src = new RtpVideoTestSource();
        RtpVideoSinkNode *sink = new RtpVideoSinkNode();

        src->build(MediaNodeConfig());

        MediaNodeConfig sinkCfg("RtpVideoSinkNode", "sink");
        sinkCfg.set("Destination", "127.0.0.1:5004");
        sinkCfg.set("FrameRate", "30");
        sinkCfg.set("RtpPayload", reinterpret_cast<uint64_t>(&payload));
        sink->build(sinkCfg);

        pipeline.addNode(src);
        pipeline.addNode(sink);
        pipeline.connect(src, 0, sink, 0);
        Error err = pipeline.start();
        CHECK(err.isOk());
        CHECK(sink->state() == MediaNode::Running);
}

// ============================================================================
// Start / stop lifecycle
// ============================================================================

TEST_CASE("RtpVideoSinkNode_StartStopLifecycle") {
        RtpPayloadRawVideo payload(64, 64, 24);
        MediaPipeline pipeline;
        RtpVideoTestSource *src = new RtpVideoTestSource();
        RtpVideoSinkNode *sink = new RtpVideoSinkNode();

        src->build(MediaNodeConfig());

        MediaNodeConfig sinkCfg("RtpVideoSinkNode", "sink");
        sinkCfg.set("Destination", "127.0.0.1:5004");
        sinkCfg.set("FrameRate", "30");
        sinkCfg.set("RtpPayload", reinterpret_cast<uint64_t>(&payload));
        sink->build(sinkCfg);

        pipeline.addNode(src);
        pipeline.addNode(sink);
        pipeline.connect(src, 0, sink, 0);

        Error err = pipeline.start();
        CHECK(err.isOk());
        CHECK(sink->state() == MediaNode::Running);

        pipeline.stop();
        CHECK(sink->state() == MediaNode::Idle);
}

// ============================================================================
// Send frame via loopback -- verify RTP headers
// ============================================================================

TEST_CASE("RtpVideoSinkNode_SendFrameLoopback") {
        UdpSocket receiver;
        receiver.open(UdpSocket::ReadWrite);
        receiver.bind(SocketAddress::localhost(0));
        uint16_t recvPort = receiver.localAddress().port();

        RtpPayloadRawVideo payload(64, 64, 24);

        // Graph owns all nodes -- heap-allocate everything
        MediaPipeline pipeline;
        RtpVideoTestSource *src = new RtpVideoTestSource();
        RtpVideoSinkNode *sink = new RtpVideoSinkNode();

        src->build(MediaNodeConfig());

        MediaNodeConfig sinkCfg("RtpVideoSinkNode", "sink");
        sinkCfg.set("Destination", "127.0.0.1:" + String::number(recvPort));
        sinkCfg.set("FrameRate", "30");
        sinkCfg.set("RtpPayload", reinterpret_cast<uint64_t>(&payload));
        sinkCfg.set("PayloadType", uint8_t(96));
        sink->build(sinkCfg);

        pipeline.addNode(src);
        pipeline.addNode(sink);
        pipeline.connect(src, 0, sink, 0);
        REQUIRE(pipeline.start().isOk());

        Image img = createSmallTestImage();
        Frame::Ptr frame = Frame::Ptr::create();
        frame.modify()->imageList().pushToBack(Image::Ptr::create(img));
        src->pushFrame(frame);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        // Receive at least one datagram
        bool received = false;
        uint8_t buf[2048];
        int retries = 100;
        while(retries-- > 0 && !received) {
                if(receiver.hasPendingDatagrams()) {
                        ssize_t len = receiver.readDatagram(buf, sizeof(buf));
                        if(len > 0) {
                                received = true;
                                CHECK(((buf[0] >> 6) & 0x03) == 2);
                                CHECK((buf[1] & 0x7F) == 96);
                        }
                }
        }
        CHECK(received);

        pipeline.stop();
}

// ============================================================================
// Timestamp continuity across multiple frames
// ============================================================================

TEST_CASE("RtpVideoSinkNode_TimestampContinuity") {
        UdpSocket receiver;
        receiver.open(UdpSocket::ReadWrite);
        receiver.bind(SocketAddress::localhost(0));
        uint16_t recvPort = receiver.localAddress().port();

        RtpPayloadRawVideo payload(16, 16, 24);

        MediaPipeline pipeline;
        RtpVideoTestSource *src = new RtpVideoTestSource();
        RtpVideoSinkNode *sink = new RtpVideoSinkNode();

        src->build(MediaNodeConfig());

        MediaNodeConfig sinkCfg("RtpVideoSinkNode", "sink");
        sinkCfg.set("Destination", "127.0.0.1:" + String::number(recvPort));
        sinkCfg.set("FrameRate", "30");
        sinkCfg.set("RtpPayload", reinterpret_cast<uint64_t>(&payload));
        sinkCfg.set("ClockRate", uint32_t(90000));
        sink->build(sinkCfg);

        pipeline.addNode(src);
        pipeline.addNode(sink);
        pipeline.connect(src, 0, sink, 0);
        REQUIRE(pipeline.start().isOk());

        // Expected increment: 90000 * 1 / 30 = 3000
        uint32_t expectedIncrement = 3000;

        List<uint32_t> timestamps;
        for(int f = 0; f < 2; f++) {
                Image img = createSmallTestImage(16, 16);
                Frame::Ptr frame = Frame::Ptr::create();
                frame.modify()->imageList().pushToBack(Image::Ptr::create(img));
                src->pushFrame(frame);
                std::this_thread::sleep_for(std::chrono::milliseconds(50));

                // Drain all packets for this frame, keep last (marker) timestamp
                uint8_t buf[2048];
                uint32_t lastTs = 0;
                int retries = 100;
                while(retries-- > 0) {
                        if(receiver.hasPendingDatagrams()) {
                                ssize_t len = receiver.readDatagram(buf, sizeof(buf));
                                if(len >= 12) {
                                        RtpPacket pkt(Buffer::Ptr::create(len), 0, len);
                                        std::memcpy(pkt.data(), buf, len);
                                        lastTs = pkt.timestamp();
                                        if(pkt.marker()) break;
                                }
                                retries = 100;
                        }
                }
                timestamps.pushToBack(lastTs);
        }

        REQUIRE(timestamps.size() == 2);
        CHECK(timestamps[1] - timestamps[0] == expectedIncrement);

        pipeline.stop();
}

// ============================================================================
// Extended stats keys present
// ============================================================================

TEST_CASE("RtpVideoSinkNode_ExtendedStats") {
        RtpPayloadRawVideo payload(64, 64, 24);
        MediaPipeline pipeline;
        RtpVideoTestSource *src = new RtpVideoTestSource();
        RtpVideoSinkNode *sink = new RtpVideoSinkNode();

        src->build(MediaNodeConfig());

        MediaNodeConfig sinkCfg("RtpVideoSinkNode", "sink");
        sinkCfg.set("Destination", "127.0.0.1:5004");
        sinkCfg.set("FrameRate", "30");
        sinkCfg.set("RtpPayload", reinterpret_cast<uint64_t>(&payload));
        sink->build(sinkCfg);

        pipeline.addNode(src);
        pipeline.addNode(sink);
        pipeline.connect(src, 0, sink, 0);
        REQUIRE(pipeline.start().isOk());

        auto stats = sink->extendedStats();
        CHECK(stats.contains("PacketsSent"));
        CHECK(stats.contains("BytesSent"));
        CHECK(stats.contains("UnderrunCount"));
        CHECK(stats["PacketsSent"].get<uint64_t>() == 0);
        CHECK(stats["BytesSent"].get<uint64_t>() == 0);
}

TEST_CASE("RtpVideoSinkNode_DefaultConfig") {
        RtpVideoSinkNode node;
        MediaNodeConfig cfg = node.defaultConfig();
        CHECK(cfg.type() == "RtpVideoSinkNode");
        CHECK(cfg.get("PayloadType").get<uint8_t>() == 96);
        CHECK(cfg.get("ClockRate").get<uint32_t>() == 90000);
        CHECK(cfg.get("Dscp").get<uint8_t>() == 34);
}
