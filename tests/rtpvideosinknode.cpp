/**
 * @file      tests/rtpvideosinknode.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstring>
#include <doctest/doctest.h>
#include <promeki/proav/rtpvideosinknode.h>
#include <promeki/proav/mediagraph.h>
#include <promeki/proav/frame.h>
#include <promeki/proav/image.h>
#include <promeki/proav/imagedesc.h>
#include <promeki/proav/pixelformat.h>
#include <promeki/core/buffer.h>

#ifdef PROMEKI_HAVE_NETWORK
#include <promeki/network/socketaddress.h>
#include <promeki/network/udpsocket.h>
#include <promeki/network/rtppayload.h>
#include <promeki/network/rtppacket.h>
#endif

using namespace promeki;

// ============================================================================
// Helper: source node that pushes Image frames
// ============================================================================

class RtpVideoTestSource : public MediaNode {
        PROMEKI_OBJECT(RtpVideoTestSource, MediaNode)
        public:
                RtpVideoTestSource() : MediaNode() {
                        setName("RtpVideoTestSource");
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
// Helper: create a small test image
// ============================================================================

static Image createSmallTestImage(int width = 64, int height = 64) {
        ImageDesc idesc(width, height, PixelFormat::RGB8);
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
        CHECK(node.inputPortCount() == 1);
        CHECK(node.outputPortCount() == 0);
        CHECK(node.inputPort(0)->mediaType() == MediaPort::Image);
        CHECK(node.payloadType() == 96);
        CHECK(node.clockRate() == 90000);
        CHECK(node.dscp() == 34);
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
        CHECK(node.inputPortCount() == 1);
        CHECK(node.outputPortCount() == 0);
        CHECK(node.inputPort(0)->name() == "input");
        CHECK(node.inputPort(0)->direction() == MediaPort::Input);
        CHECK(node.inputPort(0)->mediaType() == MediaPort::Image);
}

#ifdef PROMEKI_HAVE_NETWORK

// ============================================================================
// Configure failure: no payload set
// ============================================================================

TEST_CASE("RtpVideoSinkNode_ConfigureFailNoPayload") {
        RtpVideoSinkNode node;
        node.setDestination(SocketAddress::localhost(5004));
        node.setFrameRate(FrameRate::FPS_30);
        Error err = node.configure();
        CHECK(err.isError());
}

// ============================================================================
// Configure failure: no destination
// ============================================================================

TEST_CASE("RtpVideoSinkNode_ConfigureFailNoDestination") {
        RtpPayloadRawVideo payload(64, 64, 24);
        RtpVideoSinkNode node;
        node.setRtpPayload(&payload);
        node.setFrameRate(FrameRate::FPS_30);
        Error err = node.configure();
        CHECK(err.isError());
}

// ============================================================================
// Configure failure: no frame rate
// ============================================================================

TEST_CASE("RtpVideoSinkNode_ConfigureFailNoFrameRate") {
        RtpPayloadRawVideo payload(64, 64, 24);
        RtpVideoSinkNode node;
        node.setRtpPayload(&payload);
        node.setDestination(SocketAddress::localhost(5004));
        Error err = node.configure();
        CHECK(err.isError());
}

// ============================================================================
// Configure success
// ============================================================================

TEST_CASE("RtpVideoSinkNode_ConfigureSuccess") {
        RtpPayloadRawVideo payload(64, 64, 24);
        RtpVideoSinkNode node;
        node.setDestination(SocketAddress::localhost(5004));
        node.setFrameRate(FrameRate::FPS_30);
        node.setRtpPayload(&payload);
        Error err = node.configure();
        CHECK(err.isOk());
        CHECK(node.state() == MediaNode::Configured);
}

// ============================================================================
// Start / stop lifecycle
// ============================================================================

TEST_CASE("RtpVideoSinkNode_StartStopLifecycle") {
        RtpPayloadRawVideo payload(64, 64, 24);
        RtpVideoSinkNode node;
        node.setDestination(SocketAddress::localhost(5004));
        node.setFrameRate(FrameRate::FPS_30);
        node.setRtpPayload(&payload);
        REQUIRE(node.configure().isOk());

        Error err = node.start();
        CHECK(err.isOk());
        CHECK(node.state() == MediaNode::Running);

        node.stop();
        CHECK(node.state() == MediaNode::Idle);
}

// ============================================================================
// Send frame via loopback — verify RTP headers
// ============================================================================

TEST_CASE("RtpVideoSinkNode_SendFrameLoopback") {
        UdpSocket receiver;
        receiver.open(UdpSocket::ReadWrite);
        receiver.bind(SocketAddress::localhost(0));
        uint16_t recvPort = receiver.localAddress().port();

        RtpPayloadRawVideo payload(64, 64, 24);

        // Graph owns all nodes — heap-allocate everything
        MediaGraph graph;
        RtpVideoTestSource *src = new RtpVideoTestSource();
        RtpVideoSinkNode *sink = new RtpVideoSinkNode();
        sink->setDestination(SocketAddress::localhost(recvPort));
        sink->setFrameRate(FrameRate::FPS_30);
        sink->setRtpPayload(&payload);
        sink->setPayloadType(96);
        REQUIRE(sink->configure().isOk());
        REQUIRE(sink->start().isOk());

        graph.addNode(src);
        graph.addNode(sink);
        graph.connect(src, 0, sink, 0);

        Image img = createSmallTestImage();
        Frame::Ptr frame = Frame::Ptr::create();
        frame.modify()->imageList().pushToBack(Image::Ptr::create(img));
        src->pushFrame(frame);
        sink->process();

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

        sink->stop();
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

        MediaGraph graph;
        RtpVideoTestSource *src = new RtpVideoTestSource();
        RtpVideoSinkNode *sink = new RtpVideoSinkNode();
        sink->setDestination(SocketAddress::localhost(recvPort));
        sink->setFrameRate(FrameRate::FPS_30);
        sink->setRtpPayload(&payload);
        sink->setClockRate(90000);
        REQUIRE(sink->configure().isOk());
        REQUIRE(sink->start().isOk());

        graph.addNode(src);
        graph.addNode(sink);
        graph.connect(src, 0, sink, 0);

        // Expected increment: 90000 * 1 / 30 = 3000
        uint32_t expectedIncrement = 3000;

        List<uint32_t> timestamps;
        for(int f = 0; f < 2; f++) {
                Image img = createSmallTestImage(16, 16);
                Frame::Ptr frame = Frame::Ptr::create();
                frame.modify()->imageList().pushToBack(Image::Ptr::create(img));
                src->pushFrame(frame);
                sink->process();

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

        sink->stop();
}

// ============================================================================
// Starvation counter
// ============================================================================

TEST_CASE("RtpVideoSinkNode_Starvation") {
        RtpPayloadRawVideo payload(64, 64, 24);
        RtpVideoSinkNode node;
        node.setDestination(SocketAddress::localhost(5004));
        node.setFrameRate(FrameRate::FPS_30);
        node.setRtpPayload(&payload);
        REQUIRE(node.configure().isOk());

        auto stats0 = node.extendedStats();
        CHECK(stats0["underrunCount"].get<uint64_t>() == 0);

        node.starvation();
        node.starvation();

        auto stats1 = node.extendedStats();
        CHECK(stats1["underrunCount"].get<uint64_t>() == 2);
}

// ============================================================================
// Extended stats keys present
// ============================================================================

TEST_CASE("RtpVideoSinkNode_ExtendedStats") {
        RtpPayloadRawVideo payload(64, 64, 24);
        RtpVideoSinkNode node;
        node.setDestination(SocketAddress::localhost(5004));
        node.setFrameRate(FrameRate::FPS_30);
        node.setRtpPayload(&payload);
        REQUIRE(node.configure().isOk());

        auto stats = node.extendedStats();
        CHECK(stats.contains("packetsSent"));
        CHECK(stats.contains("bytesSent"));
        CHECK(stats.contains("underrunCount"));
        CHECK(stats["packetsSent"].get<uint64_t>() == 0);
        CHECK(stats["bytesSent"].get<uint64_t>() == 0);
}

#endif // PROMEKI_HAVE_NETWORK
