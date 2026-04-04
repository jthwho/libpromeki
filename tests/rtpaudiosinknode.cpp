/**
 * @file      tests/rtpaudiosinknode.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstring>
#include <thread>
#include <doctest/doctest.h>
#include <promeki/rtpaudiosinknode.h>
#include <promeki/mediapipeline.h>
#include <promeki/medianodeconfig.h>
#include <promeki/frame.h>
#include <promeki/audio.h>
#include <promeki/audiodesc.h>
#include <promeki/buffer.h>

#include <promeki/socketaddress.h>
#include <promeki/udpsocket.h>
#include <promeki/rtppayload.h>
#include <promeki/rtppacket.h>

using namespace promeki;

// ============================================================================
// Helper: source node that pushes Audio frames
// ============================================================================

class RtpAudioTestSource : public MediaNode {
        PROMEKI_OBJECT(RtpAudioTestSource, MediaNode)
        public:
                RtpAudioTestSource() : MediaNode() {
                        setName("RtpAudioTestSource");
                        auto port = MediaSource::Ptr::create("output", ContentAudio);
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
// Helper: create a test Audio object with silence
// ============================================================================

static Audio createTestAudio(size_t samples, int channels = 2) {
        AudioDesc desc(48000.0f, channels);
        Audio audio(desc, samples);
        audio.zero();
        return audio;
}

// ============================================================================
// Construction
// ============================================================================

TEST_CASE("RtpAudioSinkNode_Construct") {
        RtpAudioSinkNode node;
        CHECK(node.name() == "RtpAudioSinkNode");
        CHECK(node.sinkCount() == 1);
        CHECK(node.sourceCount() == 0);
        CHECK(node.sink(0)->contentHint() == ContentAudio);
}

// ============================================================================
// Registry
// ============================================================================

TEST_CASE("RtpAudioSinkNode_Registry") {
        auto types = MediaNode::registeredNodeTypes();
        CHECK(types.contains("RtpAudioSinkNode"));
}

// ============================================================================
// Port structure
// ============================================================================

TEST_CASE("RtpAudioSinkNode_PortStructure") {
        RtpAudioSinkNode node;
        CHECK(node.sinkCount() == 1);
        CHECK(node.sourceCount() == 0);
        CHECK(node.sink(0)->name() == "input");
        CHECK(node.sink(0)->contentHint() == ContentAudio);
}

// ============================================================================
// Configure failure: no payload set
// ============================================================================

TEST_CASE("RtpAudioSinkNode_ConfigureFailNoPayload") {
        MediaPipeline pipeline;
        RtpAudioTestSource *src = new RtpAudioTestSource();
        RtpAudioSinkNode *sink = new RtpAudioSinkNode();

        src->build(MediaNodeConfig());

        MediaNodeConfig sinkCfg("RtpAudioSinkNode", "sink");
        sinkCfg.set("Destination", "127.0.0.1:5006");
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

TEST_CASE("RtpAudioSinkNode_ConfigureFailNoDestination") {
        RtpPayloadL24 payload(48000, 2);
        MediaPipeline pipeline;
        RtpAudioTestSource *src = new RtpAudioTestSource();
        RtpAudioSinkNode *sink = new RtpAudioSinkNode();

        src->build(MediaNodeConfig());

        MediaNodeConfig sinkCfg("RtpAudioSinkNode", "sink");
        sinkCfg.set("RtpPayload", reinterpret_cast<uint64_t>(&payload));
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

TEST_CASE("RtpAudioSinkNode_ConfigureSuccess") {
        RtpPayloadL24 payload(48000, 2);
        MediaPipeline pipeline;
        RtpAudioTestSource *src = new RtpAudioTestSource();
        RtpAudioSinkNode *sink = new RtpAudioSinkNode();

        src->build(MediaNodeConfig());

        MediaNodeConfig sinkCfg("RtpAudioSinkNode", "sink");
        sinkCfg.set("Destination", "127.0.0.1:5006");
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

TEST_CASE("RtpAudioSinkNode_StartStopLifecycle") {
        RtpPayloadL24 payload(48000, 2);
        MediaPipeline pipeline;
        RtpAudioTestSource *src = new RtpAudioTestSource();
        RtpAudioSinkNode *sink = new RtpAudioSinkNode();

        src->build(MediaNodeConfig());

        MediaNodeConfig sinkCfg("RtpAudioSinkNode", "sink");
        sinkCfg.set("Destination", "127.0.0.1:5006");
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
// Accumulation: less than 1 packet's worth -- no packets sent
// ============================================================================

TEST_CASE("RtpAudioSinkNode_AccumulationSubPacket") {
        UdpSocket receiver;
        receiver.open(UdpSocket::ReadWrite);
        receiver.bind(SocketAddress::localhost(0));
        uint16_t recvPort = receiver.localAddress().port();

        RtpPayloadL24 payload(48000, 2);

        MediaPipeline pipeline;
        RtpAudioTestSource *src = new RtpAudioTestSource();
        RtpAudioSinkNode *sink = new RtpAudioSinkNode();

        src->build(MediaNodeConfig());

        MediaNodeConfig sinkCfg("RtpAudioSinkNode", "sink");
        sinkCfg.set("Destination", "127.0.0.1:" + String::number(recvPort));
        sinkCfg.set("RtpPayload", reinterpret_cast<uint64_t>(&payload));
        sinkCfg.set("PacketTime", 1.0); // 48 samples per packet
        sinkCfg.set("ClockRate", uint32_t(48000));
        sink->build(sinkCfg);

        pipeline.addNode(src);
        pipeline.addNode(sink);
        pipeline.connect(src, 0, sink, 0);
        REQUIRE(pipeline.start().isOk());

        // Send fewer samples than one packet (48 samples needed, send 10)
        Audio audio = createTestAudio(10, 2);
        Frame::Ptr frame = Frame::Ptr::create();
        frame.modify()->audioList().pushToBack(Audio::Ptr::create(audio));
        src->pushFrame(frame);

        // Wait for the node to process the frame
        for(int i = 0; i < 200 && sink->extendedStats()["PacketsSent"].get<uint64_t>() == 0; i++) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        // Brief extra wait to ensure no packets leak through
        std::this_thread::sleep_for(std::chrono::milliseconds(20));

        // Should not have sent any packets yet
        CHECK_FALSE(receiver.hasPendingDatagrams());

        auto stats = sink->extendedStats();
        CHECK(stats["PacketsSent"].get<uint64_t>() == 0);

        pipeline.stop();
}

// ============================================================================
// Accumulation: >= 1 packet's worth -- correct packet count
// ============================================================================

TEST_CASE("RtpAudioSinkNode_AccumulationFullPacket") {
        UdpSocket receiver;
        receiver.open(UdpSocket::ReadWrite);
        receiver.bind(SocketAddress::localhost(0));
        uint16_t recvPort = receiver.localAddress().port();

        RtpPayloadL24 payload(48000, 2);

        MediaPipeline pipeline;
        RtpAudioTestSource *src = new RtpAudioTestSource();
        RtpAudioSinkNode *sink = new RtpAudioSinkNode();

        src->build(MediaNodeConfig());

        MediaNodeConfig sinkCfg("RtpAudioSinkNode", "sink");
        sinkCfg.set("Destination", "127.0.0.1:" + String::number(recvPort));
        sinkCfg.set("RtpPayload", reinterpret_cast<uint64_t>(&payload));
        sinkCfg.set("PacketTime", 1.0); // 48 samples per packet
        sinkCfg.set("ClockRate", uint32_t(48000));
        sink->build(sinkCfg);

        pipeline.addNode(src);
        pipeline.addNode(sink);
        pipeline.connect(src, 0, sink, 0);
        REQUIRE(pipeline.start().isOk());

        // Send exactly 48 samples (1 packet's worth at 1ms/48kHz)
        Audio audio = createTestAudio(48, 2);
        Frame::Ptr frame = Frame::Ptr::create();
        frame.modify()->audioList().pushToBack(Audio::Ptr::create(audio));
        src->pushFrame(frame);

        // Wait for at least one packet to be sent
        for(int i = 0; i < 200 && sink->extendedStats()["PacketsSent"].get<uint64_t>() == 0; i++) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        // Should have sent at least one datagram
        bool received = false;
        uint8_t buf[2048];
        int retries = 100;
        while(retries-- > 0) {
                if(receiver.hasPendingDatagrams()) {
                        ssize_t len = receiver.readDatagram(buf, sizeof(buf));
                        if(len > 0) received = true;
                }
        }
        CHECK(received);

        pipeline.stop();
}

// ============================================================================
// Cross-boundary: 1.5x samples -- 1 packet sent, remainder kept
// ============================================================================

TEST_CASE("RtpAudioSinkNode_CrossBoundary") {
        UdpSocket receiver;
        receiver.open(UdpSocket::ReadWrite);
        receiver.bind(SocketAddress::localhost(0));
        uint16_t recvPort = receiver.localAddress().port();

        RtpPayloadL24 payload(48000, 2);

        MediaPipeline pipeline;
        RtpAudioTestSource *src = new RtpAudioTestSource();
        RtpAudioSinkNode *sink = new RtpAudioSinkNode();

        src->build(MediaNodeConfig());

        MediaNodeConfig sinkCfg("RtpAudioSinkNode", "sink");
        sinkCfg.set("Destination", "127.0.0.1:" + String::number(recvPort));
        sinkCfg.set("RtpPayload", reinterpret_cast<uint64_t>(&payload));
        sinkCfg.set("PacketTime", 1.0); // 48 samples per packet
        sinkCfg.set("ClockRate", uint32_t(48000));
        sink->build(sinkCfg);

        pipeline.addNode(src);
        pipeline.addNode(sink);
        pipeline.connect(src, 0, sink, 0);
        REQUIRE(pipeline.start().isOk());

        // Send 72 samples (1.5x packet) -- should get 1 packet, 24 remaining
        Audio audio = createTestAudio(72, 2);
        Frame::Ptr frame = Frame::Ptr::create();
        frame.modify()->audioList().pushToBack(Audio::Ptr::create(audio));
        src->pushFrame(frame);

        // Wait for processing
        for(int i = 0; i < 200 && sink->extendedStats()["SamplesSent"].get<uint64_t>() == 0; i++) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        auto stats = sink->extendedStats();
        CHECK(stats["SamplesSent"].get<uint64_t>() == 48);

        pipeline.stop();
}

// ============================================================================
// Timestamp tracking
// ============================================================================

TEST_CASE("RtpAudioSinkNode_TimestampTracking") {
        UdpSocket receiver;
        receiver.open(UdpSocket::ReadWrite);
        receiver.bind(SocketAddress::localhost(0));
        uint16_t recvPort = receiver.localAddress().port();

        RtpPayloadL24 payload(48000, 2);

        MediaPipeline pipeline;
        RtpAudioTestSource *src = new RtpAudioTestSource();
        RtpAudioSinkNode *sink = new RtpAudioSinkNode();

        src->build(MediaNodeConfig());

        MediaNodeConfig sinkCfg("RtpAudioSinkNode", "sink");
        sinkCfg.set("Destination", "127.0.0.1:" + String::number(recvPort));
        sinkCfg.set("RtpPayload", reinterpret_cast<uint64_t>(&payload));
        sinkCfg.set("PacketTime", 1.0);
        sinkCfg.set("ClockRate", uint32_t(48000));
        sink->build(sinkCfg);

        pipeline.addNode(src);
        pipeline.addNode(sink);
        pipeline.connect(src, 0, sink, 0);
        REQUIRE(pipeline.start().isOk());

        // Send 96 samples (2 packets worth)
        Audio audio = createTestAudio(96, 2);
        Frame::Ptr frame = Frame::Ptr::create();
        frame.modify()->audioList().pushToBack(Audio::Ptr::create(audio));
        src->pushFrame(frame);

        // Wait for 2 packets to be sent
        for(int i = 0; i < 200 && sink->extendedStats()["SamplesSent"].get<uint64_t>() < 96; i++) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        // Collect timestamps from received packets
        List<uint32_t> timestamps;
        uint8_t buf[2048];
        int retries = 100;
        while(retries-- > 0) {
                if(receiver.hasPendingDatagrams()) {
                        ssize_t len = receiver.readDatagram(buf, sizeof(buf));
                        if(len >= 12) {
                                RtpPacket pkt(Buffer::Ptr::create(len), 0, len);
                                std::memcpy(pkt.data(), buf, len);
                                timestamps.pushToBack(pkt.timestamp());
                        }
                        retries = 100;
                }
        }

        // Should have at least 2 packets with different timestamps
        REQUIRE(timestamps.size() >= 2);
        CHECK(timestamps[timestamps.size() - 1] - timestamps[0] == 48);

        pipeline.stop();
}

// ============================================================================
// Extended stats keys present
// ============================================================================

TEST_CASE("RtpAudioSinkNode_ExtendedStats") {
        RtpPayloadL24 payload(48000, 2);
        MediaPipeline pipeline;
        RtpAudioTestSource *src = new RtpAudioTestSource();
        RtpAudioSinkNode *sink = new RtpAudioSinkNode();

        src->build(MediaNodeConfig());

        MediaNodeConfig sinkCfg("RtpAudioSinkNode", "sink");
        sinkCfg.set("Destination", "127.0.0.1:5006");
        sinkCfg.set("RtpPayload", reinterpret_cast<uint64_t>(&payload));
        sink->build(sinkCfg);

        pipeline.addNode(src);
        pipeline.addNode(sink);
        pipeline.connect(src, 0, sink, 0);
        REQUIRE(pipeline.start().isOk());

        auto stats = sink->extendedStats();
        CHECK(stats.contains("PacketsSent"));
        CHECK(stats.contains("SamplesSent"));
        CHECK(stats.contains("UnderrunCount"));
        CHECK(stats["PacketsSent"].get<uint64_t>() == 0);
        CHECK(stats["SamplesSent"].get<uint64_t>() == 0);
}

TEST_CASE("RtpAudioSinkNode_DefaultConfig") {
        RtpAudioSinkNode node;
        MediaNodeConfig cfg = node.defaultConfig();
        CHECK(cfg.type() == "RtpAudioSinkNode");
        CHECK(cfg.get("PayloadType").get<uint8_t>() == 97);
        CHECK(cfg.get("ClockRate").get<uint32_t>() == 48000);
        CHECK(cfg.get("PacketTime").get<double>() == 4.0);
        CHECK(cfg.get("Dscp").get<uint8_t>() == 46);
}
