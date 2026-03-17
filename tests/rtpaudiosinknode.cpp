/**
 * @file      tests/rtpaudiosinknode.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstring>
#include <doctest/doctest.h>
#include <promeki/proav/rtpaudiosinknode.h>
#include <promeki/proav/mediagraph.h>
#include <promeki/proav/frame.h>
#include <promeki/proav/audio.h>
#include <promeki/proav/audiodesc.h>
#include <promeki/core/buffer.h>

#ifdef PROMEKI_HAVE_NETWORK
#include <promeki/network/socketaddress.h>
#include <promeki/network/udpsocket.h>
#include <promeki/network/rtppayload.h>
#include <promeki/network/rtppacket.h>
#endif

using namespace promeki;

// ============================================================================
// Helper: source node that pushes Audio frames
// ============================================================================

class RtpAudioTestSource : public MediaNode {
        PROMEKI_OBJECT(RtpAudioTestSource, MediaNode)
        public:
                RtpAudioTestSource() : MediaNode() {
                        setName("RtpAudioTestSource");
                        auto port = MediaPort::Ptr::create("output", MediaPort::Output, MediaPort::Audio);
                        addOutputPort(port);
                }
                void process() override { return; }
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
        CHECK(node.inputPortCount() == 1);
        CHECK(node.outputPortCount() == 0);
        CHECK(node.inputPort(0)->mediaType() == MediaPort::Audio);
        CHECK(node.payloadType() == 97);
        CHECK(node.clockRate() == 48000);
        CHECK(node.dscp() == 46);
        CHECK(node.packetTime() == 4.0);
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
        CHECK(node.inputPortCount() == 1);
        CHECK(node.outputPortCount() == 0);
        CHECK(node.inputPort(0)->name() == "input");
        CHECK(node.inputPort(0)->direction() == MediaPort::Input);
        CHECK(node.inputPort(0)->mediaType() == MediaPort::Audio);
}

#ifdef PROMEKI_HAVE_NETWORK

// ============================================================================
// Configure failure: no payload set
// ============================================================================

TEST_CASE("RtpAudioSinkNode_ConfigureFailNoPayload") {
        RtpAudioSinkNode node;
        node.setDestination(SocketAddress::localhost(5006));
        Error err = node.configure();
        CHECK(err.isError());
}

// ============================================================================
// Configure failure: no destination
// ============================================================================

TEST_CASE("RtpAudioSinkNode_ConfigureFailNoDestination") {
        RtpPayloadL24 payload(48000, 2);
        RtpAudioSinkNode node;
        node.setRtpPayload(&payload);
        Error err = node.configure();
        CHECK(err.isError());
}

// ============================================================================
// Configure success
// ============================================================================

TEST_CASE("RtpAudioSinkNode_ConfigureSuccess") {
        RtpPayloadL24 payload(48000, 2);
        RtpAudioSinkNode node;
        node.setDestination(SocketAddress::localhost(5006));
        node.setRtpPayload(&payload);
        Error err = node.configure();
        CHECK(err.isOk());
        CHECK(node.state() == MediaNode::Configured);
}

// ============================================================================
// Start / stop lifecycle
// ============================================================================

TEST_CASE("RtpAudioSinkNode_StartStopLifecycle") {
        RtpPayloadL24 payload(48000, 2);
        RtpAudioSinkNode node;
        node.setDestination(SocketAddress::localhost(5006));
        node.setRtpPayload(&payload);
        REQUIRE(node.configure().isOk());

        Error err = node.start();
        CHECK(err.isOk());
        CHECK(node.state() == MediaNode::Running);

        node.stop();
        CHECK(node.state() == MediaNode::Idle);
}

// ============================================================================
// Accumulation: less than 1 packet's worth — no packets sent
// ============================================================================

TEST_CASE("RtpAudioSinkNode_AccumulationSubPacket") {
        UdpSocket receiver;
        receiver.open(UdpSocket::ReadWrite);
        receiver.bind(SocketAddress::localhost(0));
        uint16_t recvPort = receiver.localAddress().port();

        RtpPayloadL24 payload(48000, 2);

        MediaGraph graph;
        RtpAudioTestSource *src = new RtpAudioTestSource();
        RtpAudioSinkNode *sink = new RtpAudioSinkNode();
        sink->setDestination(SocketAddress::localhost(recvPort));
        sink->setRtpPayload(&payload);
        sink->setPacketTime(1.0); // 48 samples per packet
        sink->setClockRate(48000);
        REQUIRE(sink->configure().isOk());
        REQUIRE(sink->start().isOk());

        graph.addNode(src);
        graph.addNode(sink);
        graph.connect(src, 0, sink, 0);

        // Send fewer samples than one packet (48 samples needed, send 10)
        Audio audio = createTestAudio(10, 2);
        Frame::Ptr frame = Frame::Ptr::create();
        frame.modify()->audioList().pushToBack(Audio::Ptr::create(audio));
        src->pushFrame(frame);
        sink->process();

        // Should not have sent any packets yet
        CHECK_FALSE(receiver.hasPendingDatagrams());

        auto stats = sink->extendedStats();
        CHECK(stats["packetsSent"].get<uint64_t>() == 0);

        sink->stop();
}

// ============================================================================
// Accumulation: >= 1 packet's worth — correct packet count
// ============================================================================

TEST_CASE("RtpAudioSinkNode_AccumulationFullPacket") {
        UdpSocket receiver;
        receiver.open(UdpSocket::ReadWrite);
        receiver.bind(SocketAddress::localhost(0));
        uint16_t recvPort = receiver.localAddress().port();

        RtpPayloadL24 payload(48000, 2);

        MediaGraph graph;
        RtpAudioTestSource *src = new RtpAudioTestSource();
        RtpAudioSinkNode *sink = new RtpAudioSinkNode();
        sink->setDestination(SocketAddress::localhost(recvPort));
        sink->setRtpPayload(&payload);
        sink->setPacketTime(1.0); // 48 samples per packet
        sink->setClockRate(48000);
        REQUIRE(sink->configure().isOk());
        REQUIRE(sink->start().isOk());

        graph.addNode(src);
        graph.addNode(sink);
        graph.connect(src, 0, sink, 0);

        // Send exactly 48 samples (1 packet's worth at 1ms/48kHz)
        Audio audio = createTestAudio(48, 2);
        Frame::Ptr frame = Frame::Ptr::create();
        frame.modify()->audioList().pushToBack(Audio::Ptr::create(audio));
        src->pushFrame(frame);
        sink->process();

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

        sink->stop();
}

// ============================================================================
// Cross-boundary: 1.5x samples — 1 packet sent, remainder kept
// ============================================================================

TEST_CASE("RtpAudioSinkNode_CrossBoundary") {
        UdpSocket receiver;
        receiver.open(UdpSocket::ReadWrite);
        receiver.bind(SocketAddress::localhost(0));
        uint16_t recvPort = receiver.localAddress().port();

        RtpPayloadL24 payload(48000, 2);

        MediaGraph graph;
        RtpAudioTestSource *src = new RtpAudioTestSource();
        RtpAudioSinkNode *sink = new RtpAudioSinkNode();
        sink->setDestination(SocketAddress::localhost(recvPort));
        sink->setRtpPayload(&payload);
        sink->setPacketTime(1.0); // 48 samples per packet
        sink->setClockRate(48000);
        REQUIRE(sink->configure().isOk());
        REQUIRE(sink->start().isOk());

        graph.addNode(src);
        graph.addNode(sink);
        graph.connect(src, 0, sink, 0);

        // Send 72 samples (1.5x packet) — should get 1 packet, 24 remaining
        Audio audio = createTestAudio(72, 2);
        Frame::Ptr frame = Frame::Ptr::create();
        frame.modify()->audioList().pushToBack(Audio::Ptr::create(audio));
        src->pushFrame(frame);
        sink->process();

        auto stats = sink->extendedStats();
        CHECK(stats["samplesSent"].get<uint64_t>() == 48);

        sink->stop();
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

        MediaGraph graph;
        RtpAudioTestSource *src = new RtpAudioTestSource();
        RtpAudioSinkNode *sink = new RtpAudioSinkNode();
        sink->setDestination(SocketAddress::localhost(recvPort));
        sink->setRtpPayload(&payload);
        sink->setPacketTime(1.0);
        sink->setClockRate(48000);
        REQUIRE(sink->configure().isOk());
        REQUIRE(sink->start().isOk());

        graph.addNode(src);
        graph.addNode(sink);
        graph.connect(src, 0, sink, 0);

        // Send 96 samples (2 packets worth)
        Audio audio = createTestAudio(96, 2);
        Frame::Ptr frame = Frame::Ptr::create();
        frame.modify()->audioList().pushToBack(Audio::Ptr::create(audio));
        src->pushFrame(frame);
        sink->process();

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

        sink->stop();
}

// ============================================================================
// Starvation counter
// ============================================================================

TEST_CASE("RtpAudioSinkNode_Starvation") {
        RtpPayloadL24 payload(48000, 2);
        RtpAudioSinkNode node;
        node.setDestination(SocketAddress::localhost(5006));
        node.setRtpPayload(&payload);
        REQUIRE(node.configure().isOk());

        auto stats0 = node.extendedStats();
        CHECK(stats0["underrunCount"].get<uint64_t>() == 0);

        node.starvation();
        node.starvation();
        node.starvation();

        auto stats1 = node.extendedStats();
        CHECK(stats1["underrunCount"].get<uint64_t>() == 3);
}

// ============================================================================
// Extended stats keys present
// ============================================================================

TEST_CASE("RtpAudioSinkNode_ExtendedStats") {
        RtpPayloadL24 payload(48000, 2);
        RtpAudioSinkNode node;
        node.setDestination(SocketAddress::localhost(5006));
        node.setRtpPayload(&payload);
        REQUIRE(node.configure().isOk());

        auto stats = node.extendedStats();
        CHECK(stats.contains("packetsSent"));
        CHECK(stats.contains("samplesSent"));
        CHECK(stats.contains("underrunCount"));
        CHECK(stats["packetsSent"].get<uint64_t>() == 0);
        CHECK(stats["samplesSent"].get<uint64_t>() == 0);
}

#endif // PROMEKI_HAVE_NETWORK
