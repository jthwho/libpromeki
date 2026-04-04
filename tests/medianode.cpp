/**
 * @file      medianode.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <thread>
#include <atomic>
#include <doctest/doctest.h>
#include <promeki/medianode.h>
#include <promeki/mediapipeline.h>
#include <promeki/medianodeconfig.h>
#include <promeki/metadata.h>

using namespace promeki;

// ============================================================================
// Concrete test node subclass (no threading, for unit tests)
// ============================================================================

class TestNode : public MediaNode {
        PROMEKI_OBJECT(TestNode, MediaNode)
        public:
                TestNode(ObjectBase *parent = nullptr) : MediaNode(parent) { }

                void processFrame(Frame::Ptr &frame, int inputIndex, DeliveryList &deliveries) override {
                        (void)frame; (void)inputIndex; (void)deliveries;
                        _processCount++;
                        return;
                }

                int processCount() const { return _processCount; }

                BuildResult build(const MediaNodeConfig &) override {
                        setState(Configured);
                        return BuildResult();
                }

                // Expose protected methods for testing
                using MediaNode::process;
                using MediaNode::cleanup;
                using MediaNode::emitMessage;
                using MediaNode::emitWarning;
                using MediaNode::emitError;
                using MediaNode::recordProcessTiming;

                // Non-threaded start/stop for unit tests
                Error start() override {
                        if(state() != Configured) return Error(Error::Invalid);
                        setState(Running);
                        return Error(Error::Ok);
                }
                void stop() override {
                        setState(Idle);
                        wake();
                        cleanup();
                        return;
                }

        private:
                int _processCount = 0;
};

PROMEKI_REGISTER_NODE(TestNode)

// Concrete source node (no inputs, one output, no threading)
class TestSourceNode : public MediaNode {
        PROMEKI_OBJECT(TestSourceNode, MediaNode)
        public:
                TestSourceNode(ObjectBase *parent = nullptr) : MediaNode(parent) {
                        addSource(MediaSource::Ptr::create(
                                "output",
                                ContentHint(ContentVideo | ContentAudio)
                        ));
                }
                void processFrame(Frame::Ptr &frame, int inputIndex, DeliveryList &deliveries) override {
                        (void)frame; (void)inputIndex; (void)deliveries;
                }
                BuildResult build(const MediaNodeConfig &) override {
                        setState(Configured);
                        return BuildResult();
                }
                Error start() override {
                        if(state() != Configured) return Error(Error::Invalid);
                        setState(Running);
                        return Error(Error::Ok);
                }
                void stop() override { setState(Idle); return; }
};

PROMEKI_REGISTER_NODE(TestSourceNode)

// Concrete sink node (one input, no outputs, no threading)
class TestSinkNode : public MediaNode {
        PROMEKI_OBJECT(TestSinkNode, MediaNode)
        public:
                TestSinkNode(ObjectBase *parent = nullptr) : MediaNode(parent) {
                        addSink(MediaSink::Ptr::create(
                                "input", ContentNone
                        ));
                }
                void processFrame(Frame::Ptr &frame, int inputIndex, DeliveryList &deliveries) override {
                        (void)frame; (void)inputIndex; (void)deliveries;
                }
                BuildResult build(const MediaNodeConfig &) override {
                        setState(Configured);
                        return BuildResult();
                }
                Error start() override {
                        if(state() != Configured) return Error(Error::Invalid);
                        setState(Running);
                        return Error(Error::Ok);
                }
                void stop() override { setState(Idle); return; }
};

PROMEKI_REGISTER_NODE(TestSinkNode)

// ============================================================================
// Default construction and state
// ============================================================================

TEST_CASE("MediaNode_DefaultState") {
    TestNode node;
    CHECK(node.state() == MediaNode::Idle);
    CHECK(node.name().isEmpty());
    CHECK(node.sinkCount() == 0);
    CHECK(node.sourceCount() == 0);
}

// ============================================================================
// Name
// ============================================================================

TEST_CASE("MediaNode_SetName") {
    TestNode node;
    node.setName("my_node");
    CHECK(node.name() == "my_node");
}

// ============================================================================
// State transitions
// ============================================================================

TEST_CASE("MediaNode_StateTransitions") {
    TestNode node;
    CHECK(node.state() == MediaNode::Idle);

    BuildResult result = node.build(MediaNodeConfig());
    CHECK(result.isOk());
    CHECK(node.state() == MediaNode::Configured);

    Error err = node.start();
    CHECK(err == Error::Ok);
    CHECK(node.state() == MediaNode::Running);

    node.stop();
    CHECK(node.state() == MediaNode::Idle);
}

TEST_CASE("MediaNode_BuildFromNonIdle") {
    TestNode node;
    node.build(MediaNodeConfig());
    // Building again from Configured state — node's build() always succeeds
    // but real nodes may reject non-Idle builds
    CHECK(node.state() == MediaNode::Configured);
}

TEST_CASE("MediaNode_StartFromNonConfigured") {
    TestNode node;
    Error err = node.start();
    CHECK(err == Error::Invalid);
}

// ============================================================================
// Port management
// ============================================================================

TEST_CASE("MediaNode_SourcePorts") {
    TestSourceNode node;
    CHECK(node.sinkCount() == 0);
    CHECK(node.sourceCount() == 1);
    CHECK(node.source(0)->name() == "output");
    CHECK(node.source(0)->node() == &node);
}

TEST_CASE("MediaNode_SinkPorts") {
    TestSinkNode node;
    CHECK(node.sinkCount() == 1);
    CHECK(node.sourceCount() == 0);
    CHECK(node.sink(0)->name() == "input");
    CHECK(node.sink(0)->node() == &node);
}

TEST_CASE("MediaNode_PortByName") {
    TestSourceNode node;
    auto port = node.source("output");
    CHECK(port.isValid());
    CHECK(port->name() == "output");

    auto missing = node.source("nonexistent");
    CHECK(!missing.isValid());
}

TEST_CASE("MediaNode_PortByIndexOutOfRange") {
    TestNode node;
    auto port = node.sink(0);
    CHECK(!port.isValid());
    auto port2 = node.source(-1);
    CHECK(!port2.isValid());
}

// ============================================================================
// Per-port queue operations
// ============================================================================

class QueueTestNode : public MediaNode {
        PROMEKI_OBJECT(QueueTestNode, MediaNode)
        public:
                QueueTestNode() : MediaNode() {
                        addSink(MediaSink::Ptr::create("in1", ContentVideo));
                        addSink(MediaSink::Ptr::create("in2", ContentAudio));
                }
                void processFrame(Frame::Ptr &frame, int inputIndex, DeliveryList &deliveries) override {
                        (void)frame; (void)inputIndex; (void)deliveries;
                }
                BuildResult build(const MediaNodeConfig &) override {
                        setState(Configured);
                        return BuildResult();
                }
                using MediaNode::dequeueInput;
};

TEST_CASE("MediaNode_PerPortQueue") {
    QueueTestNode node;
    node.sink(0)->push(Frame::Ptr::create());
    CHECK(node.sink(0)->queueSize() == 1);
    CHECK(node.sink(1)->queueSize() == 0);
    CHECK(node.hasInput());

    Frame::Ptr f = node.dequeueInput(0);
    CHECK(f.isValid());
    CHECK(node.sink(0)->queueSize() == 0);

    f = node.dequeueInput(0);
    CHECK(!f.isValid());
}

TEST_CASE("MediaNode_DequeueByName") {
    QueueTestNode node;
    node.sink(1)->push(Frame::Ptr::create());
    Frame::Ptr f = node.dequeueInput("in2");
    CHECK(f.isValid());
}

TEST_CASE("MediaNode_DequeueFirstAvailable") {
    QueueTestNode node;
    node.sink(1)->push(Frame::Ptr::create());
    Frame::Ptr f = node.dequeueInput();
    CHECK(f.isValid());
    CHECK(node.sink(1)->queueSize() == 0);
}

// ============================================================================
// hasWork / hasInput / canOutput
// ============================================================================

TEST_CASE("MediaNode_HasInput") {
    TestSinkNode node;
    CHECK(!node.hasInput());
    node.sink(0)->push(Frame::Ptr::create());
    CHECK(node.hasInput());
}

TEST_CASE("MediaNode_SourceNodeHasWork") {
    TestSourceNode node;
    // Source with no connected sinks has no work (prevents spinning)
    CHECK(!node.hasWork());

    // Once a sink is connected, the source has work
    MediaSink::Ptr sink = MediaSink::Ptr::create("input", ContentNone);
    node.source(0)->connect(sink);
    CHECK(node.hasWork());
}

// ============================================================================
// Process
// ============================================================================

TEST_CASE("MediaNode_Process") {
    TestNode node;
    node.process();
    node.process();
    node.process();
    CHECK(node.processCount() == 3);
}

// ============================================================================
// Node type registry
// ============================================================================

TEST_CASE("MediaNode_RegisteredTypes") {
    auto types = MediaNode::registeredNodeTypes();
    CHECK(types.contains("TestNode"));
    CHECK(types.contains("TestSourceNode"));
    CHECK(types.contains("TestSinkNode"));
}

TEST_CASE("MediaNode_CreateByTypeName") {
    MediaNode *node = MediaNode::createNode("TestNode");
    CHECK(node != nullptr);
    CHECK(node->sinkCount() == 0);
    CHECK(node->sourceCount() == 0);
    delete node;
}

TEST_CASE("MediaNode_CreateUnknownType") {
    MediaNode *node = MediaNode::createNode("NonexistentNode");
    CHECK(node == nullptr);
}

// ============================================================================
// State change signal
// ============================================================================

TEST_CASE("MediaNode_StateChangedSignal") {
    TestNode node;
    int signalCount = 0;
    MediaNode::State lastState = MediaNode::Idle;
    node.stateChangedSignal.connect([&](MediaNode::State s) {
        signalCount++;
        lastState = s;
    });

    node.build(MediaNodeConfig());
    CHECK(signalCount == 1);
    CHECK(lastState == MediaNode::Configured);

    node.start();
    CHECK(signalCount == 2);
    CHECK(lastState == MediaNode::Running);

    node.stop();
    CHECK(signalCount == 3);
    CHECK(lastState == MediaNode::Idle);
}

// ============================================================================
// Error signal
// ============================================================================

TEST_CASE("MediaNode_ErrorSignal") {
    TestNode node;
    bool gotError = false;
    node.errorOccurredSignal.connect([&](Error) { gotError = true; });

    Error err = node.start();
    CHECK(err == Error::Invalid);
    CHECK(!gotError);
}

// ============================================================================
// Stop from non-running state
// ============================================================================

TEST_CASE("MediaNode_StopFromIdle") {
    TestNode node;
    node.stop();
    CHECK(node.state() == MediaNode::Idle);
}

// ============================================================================
// Node statistics
// ============================================================================

TEST_CASE("MediaNode_StatsDefault") {
    TestNode node;
    NodeStats s = node.stats();
    CHECK(s.processCount == 0);
    CHECK(s.lastProcessDuration == 0.0);
    CHECK(s.avgProcessDuration == 0.0);
    CHECK(s.peakProcessDuration == 0.0);
    CHECK(s.currentQueueDepth == 0);
    CHECK(s.peakQueueDepth == 0);
}

TEST_CASE("MediaNode_RecordProcessTiming") {
    TestNode node;
    node.recordProcessTiming(0.010);
    node.recordProcessTiming(0.020);
    node.recordProcessTiming(0.015);

    NodeStats s = node.stats();
    CHECK(s.processCount == 3);
    CHECK(s.lastProcessDuration == doctest::Approx(0.015));
    CHECK(s.peakProcessDuration == doctest::Approx(0.020));
    CHECK(s.avgProcessDuration > 0.0);
}

TEST_CASE("MediaNode_ResetStats") {
    TestNode node;
    node.recordProcessTiming(0.010);
    node.resetStats();
    NodeStats s = node.stats();
    CHECK(s.processCount == 0);
    CHECK(s.peakProcessDuration == 0.0);
}

TEST_CASE("MediaNode_ExtendedStatsDefault") {
    TestNode node;
    auto ext = node.extendedStats();
    CHECK(ext.isEmpty());
}

// ============================================================================
// Node message system
// ============================================================================

TEST_CASE("MediaNode_EmitWarning") {
    TestNode node;
    node.setName("testnode");
    NodeMessage lastMsg;
    bool gotMsg = false;
    node.messageEmittedSignal.connect([&](NodeMessage msg) {
        lastMsg = msg;
        gotMsg = true;
    });

    node.emitWarning("buffer underrun");
    CHECK(gotMsg);
    CHECK(lastMsg.severity == Severity::Warning);
    CHECK(lastMsg.message == "buffer underrun");
    CHECK(lastMsg.node == &node);
    CHECK(node.state() == MediaNode::Idle);
}

TEST_CASE("MediaNode_EmitError") {
    TestNode node;
    NodeMessage lastMsg;
    bool gotMsg = false;
    bool gotError = false;
    node.messageEmittedSignal.connect([&](NodeMessage msg) {
        lastMsg = msg;
        gotMsg = true;
    });
    node.errorOccurredSignal.connect([&](Error) { gotError = true; });

    node.emitError("socket write failed");
    CHECK(gotMsg);
    CHECK(lastMsg.severity == Severity::Error);
    CHECK(lastMsg.message == "socket write failed");
    CHECK(gotError);
    CHECK(node.state() == MediaNode::ErrorState);
}

TEST_CASE("MediaNode_EmitMessageWithFrame") {
    TestNode node;
    NodeMessage lastMsg;
    node.messageEmittedSignal.connect([&](NodeMessage msg) { lastMsg = msg; });

    node.emitMessage(Severity::Info, "frame processed", 42);
    CHECK(lastMsg.severity == Severity::Info);
    CHECK(lastMsg.frameNumber == 42);
}

// ============================================================================
// Dequeue from empty queue
// ============================================================================

class DequeueTestNode : public MediaNode {
        PROMEKI_OBJECT(DequeueTestNode, MediaNode)
        public:
                DequeueTestNode() : MediaNode() {
                        addSink(MediaSink::Ptr::create("input", ContentNone));
                }
                void processFrame(Frame::Ptr &frame, int inputIndex, DeliveryList &deliveries) override {
                        (void)frame; (void)inputIndex; (void)deliveries;
                }
                BuildResult build(const MediaNodeConfig &) override {
                        setState(Configured);
                        return BuildResult();
                }
                using MediaNode::dequeueInput;
};

TEST_CASE("MediaNode_DequeueEmptyQueue") {
    DequeueTestNode node;
    Frame::Ptr frame = node.dequeueInput();
    CHECK(!frame.isValid());
}

// ============================================================================
// DeliverOutput with no links (no-op, should not crash)
// ============================================================================

class DeliverTestNode : public MediaNode {
        PROMEKI_OBJECT(DeliverTestNode, MediaNode)
        public:
                DeliverTestNode() : MediaNode() {
                        addSource(MediaSource::Ptr::create("output",
                                      ContentHint(ContentVideo | ContentAudio)));
                }
                void processFrame(Frame::Ptr &frame, int inputIndex, DeliveryList &deliveries) override {
                        (void)frame; (void)inputIndex; (void)deliveries;
                }
                BuildResult build(const MediaNodeConfig &) override {
                        setState(Configured);
                        return BuildResult();
                }
                using MediaNode::deliverOutput;
};

TEST_CASE("MediaNode_DeliverOutputNoLinks") {
    DeliverTestNode node;
    Frame::Ptr frame = Frame::Ptr::create();
    node.deliverOutput(0, frame);
    node.deliverOutput(frame);
}

TEST_CASE("MediaNode_DeliverOutputInvalidPort") {
    DeliverTestNode node;
    Frame::Ptr frame = Frame::Ptr::create();
    node.deliverOutput(5, frame);
}

// ============================================================================
// Multiple ports with ContentHint
// ============================================================================

class MultiPortNode : public MediaNode {
        PROMEKI_OBJECT(MultiPortNode, MediaNode)
        public:
                MultiPortNode() : MediaNode() {
                        addSink(MediaSink::Ptr::create("in1", ContentNone));
                        addSink(MediaSink::Ptr::create("in2", ContentAudio));
                        addSource(MediaSource::Ptr::create("out1",
                                      ContentHint(ContentVideo | ContentAudio)));
                }
                void processFrame(Frame::Ptr &frame, int inputIndex, DeliveryList &deliveries) override {
                        (void)frame; (void)inputIndex; (void)deliveries;
                }
                BuildResult build(const MediaNodeConfig &) override {
                        setState(Configured);
                        return BuildResult();
                }
};

TEST_CASE("MediaNode_MultiplePorts") {
    MultiPortNode node;
    CHECK(node.sinkCount() == 2);
    CHECK(node.sourceCount() == 1);
    CHECK(node.sink("in1")->contentHint() == ContentNone);
    CHECK(node.sink("in2")->contentHint() == ContentAudio);
    CHECK((node.source("out1")->contentHint() & ContentVideo) != ContentNone);
}

// ============================================================================
// wake
// ============================================================================

TEST_CASE("MediaNode_Wake") {
    TestNode node;
    node.wake();
}

// ============================================================================
// cleanup
// ============================================================================

class CleanupTestNode : public MediaNode {
        PROMEKI_OBJECT(CleanupTestNode, MediaNode)
        public:
                CleanupTestNode(bool &flag) : MediaNode(), _cleaned(flag) {
                        _cleaned = false;
                }
                void processFrame(Frame::Ptr &frame, int inputIndex, DeliveryList &deliveries) override {
                        (void)frame; (void)inputIndex; (void)deliveries;
                }
                void cleanup() override { _cleaned = true; return; }
                BuildResult build(const MediaNodeConfig &) override {
                        setState(Configured);
                        return BuildResult();
                }
                // Non-threaded start for testing
                Error start() override {
                        if(state() != Configured) return Error(Error::Invalid);
                        setState(Running);
                        return Error(Error::Ok);
                }
                void stop() override {
                        setState(Idle);
                        wake();
                        cleanup();
                        return;
                }
        private:
                bool &_cleaned;
};

TEST_CASE("MediaNode_CleanupCalledOnStop") {
    bool cleaned = false;
    CleanupTestNode node(cleaned);
    node.build(MediaNodeConfig());
    node.start();
    CHECK(!cleaned);
    node.stop();
    CHECK(cleaned);
}

// ============================================================================
// waitForWork returns Stopped when node is not Running
// ============================================================================

class WaitTestNode : public MediaNode {
        PROMEKI_OBJECT(WaitTestNode, MediaNode)
        public:
                WaitTestNode() : MediaNode() {
                        addSink(MediaSink::Ptr::create("input", ContentNone));
                }
                void processFrame(Frame::Ptr &frame, int inputIndex, DeliveryList &deliveries) override {
                        (void)frame; (void)inputIndex; (void)deliveries;
                }
                BuildResult build(const MediaNodeConfig &) override {
                        setState(Configured);
                        return BuildResult();
                }
                using MediaNode::waitForWork;
                // Non-threaded start for testing
                Error start() override {
                        if(state() != Configured) return Error(Error::Invalid);
                        setState(Running);
                        return Error(Error::Ok);
                }
                void stop() override {
                        setState(Idle);
                        wake();
                        return;
                }
};

TEST_CASE("MediaNode_WaitForWorkStoppedWhenIdle") {
    WaitTestNode node;
    Error err = node.waitForWork(100);
    CHECK(err == Error::Stopped);
}

TEST_CASE("MediaNode_WaitForWorkReturnsOkWithData") {
    WaitTestNode node;
    node.build(MediaNodeConfig());
    node.start();
    CHECK(node.state() == MediaNode::Running);

    node.sink(0)->push(Frame::Ptr::create());

    Error err = node.waitForWork(100);
    CHECK(err == Error::Ok);
}

TEST_CASE("MediaNode_WaitForWorkTimesOut") {
    WaitTestNode node;
    node.build(MediaNodeConfig());
    node.start();
    CHECK(node.state() == MediaNode::Running);

    Error err = node.waitForWork(10);
    CHECK(err == Error::Timeout);
}

TEST_CASE("MediaNode_StopWakesWaiters") {
    WaitTestNode node;
    node.build(MediaNodeConfig());
    node.start();
    CHECK(node.state() == MediaNode::Running);

    std::atomic<Error::Code> result{Error::Ok};
    std::thread waiter([&]() {
        Error err = node.waitForWork();
        result = err.code();
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    node.stop();
    waiter.join();

    CHECK(result == Error::Stopped);
}

// ============================================================================
// pipeline() accessor
// ============================================================================

TEST_CASE("MediaNode_PipelinePointer") {
    MediaPipeline pipeline;
    auto *src = new TestSourceNode();
    CHECK(src->pipeline() == nullptr);

    pipeline.addNode(src);
    CHECK(src->pipeline() == &pipeline);

    pipeline.removeNode(src);
    CHECK(src->pipeline() == nullptr);
    delete src;
}

// ============================================================================
// Integration: threaded pipeline (Source -> Process -> Sink)
// ============================================================================

// Threaded source: generates numbered frames until stopped
class IntSourceNode : public MediaNode {
        PROMEKI_OBJECT(IntSourceNode, MediaNode)
        public:
                IntSourceNode() : MediaNode() {
                        setName("IntSource");
                        addSource(MediaSource::Ptr::create("output", ContentNone));
                }
                BuildResult build(const MediaNodeConfig &) override {
                        setState(Configured);
                        return BuildResult();
                }
                void processFrame(Frame::Ptr &frame, int inputIndex, DeliveryList &deliveries) override {
                        (void)inputIndex; (void)deliveries;
                        frame = Frame::Ptr::create();
                        frame.modify()->metadata().set(Metadata::FrameNumber, Variant((uint64_t)_seq++));
                        return;
                }
                uint64_t seq() const { return _seq; }
        private:
                std::atomic<uint64_t> _seq{0};
};

// Threaded processor: reads input, tags with name, passes through
class IntProcessNode : public MediaNode {
        PROMEKI_OBJECT(IntProcessNode, MediaNode)
        public:
                IntProcessNode(const String &tag) : MediaNode(), _tag(tag) {
                        setName(tag);
                        addSink(MediaSink::Ptr::create("input", ContentNone));
                        addSource(MediaSource::Ptr::create("output", ContentNone));
                }
                BuildResult build(const MediaNodeConfig &) override {
                        setState(Configured);
                        return BuildResult();
                }
                void processFrame(Frame::Ptr &frame, int inputIndex, DeliveryList &deliveries) override {
                        (void)inputIndex; (void)deliveries;
                        if(!frame.isValid()) return;
                        frame.modify()->metadata().set(Metadata::Description, Variant(String(_tag)));
                        _count++;
                        return;
                }
                std::atomic<int> &count() { return _count; }
        private:
                String _tag;
                std::atomic<int> _count{0};
};

// Threaded sink: captures received frames
class IntSinkNode : public MediaNode {
        PROMEKI_OBJECT(IntSinkNode, MediaNode)
        public:
                IntSinkNode(const String &name = "IntSink") : MediaNode() {
                        setName(name);
                        addSink(MediaSink::Ptr::create("input", ContentNone));
                }
                BuildResult build(const MediaNodeConfig &) override {
                        setState(Configured);
                        return BuildResult();
                }
                void processFrame(Frame::Ptr &frame, int inputIndex, DeliveryList &deliveries) override {
                        (void)inputIndex; (void)deliveries;
                        if(frame.isValid()) _count++;
                        return;
                }
                std::atomic<int> &count() { return _count; }
        private:
                std::atomic<int> _count{0};
};

TEST_CASE("MediaNode_Integration_ThreadedChain") {
    // Source -> ProcessA -> ProcessB -> Sink
    // All nodes run on their own threads via the default start()
    MediaPipeline pipeline;

    auto *src = new IntSourceNode();
    auto *procA = new IntProcessNode("A");
    auto *procB = new IntProcessNode("B");
    auto *sink = new IntSinkNode();

    pipeline.addNode(src);
    pipeline.addNode(procA);
    pipeline.addNode(procB);
    pipeline.addNode(sink);
    pipeline.connect(src, 0, procA, 0);
    pipeline.connect(procA, 0, procB, 0);
    pipeline.connect(procB, 0, sink, 0);

    // Build all nodes before starting
    src->build(MediaNodeConfig());
    procA->build(MediaNodeConfig());
    procB->build(MediaNodeConfig());
    sink->build(MediaNodeConfig());

    Error err = pipeline.start();
    REQUIRE(err == Error::Ok);

    // Let the pipeline run until the sink has received some frames
    for(int i = 0; i < 100 && sink->count() < 5; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    pipeline.stop();

    // Frames should have flowed through the entire chain
    CHECK(src->seq() > 0);
    CHECK(procA->count() > 0);
    CHECK(procB->count() > 0);
    CHECK(sink->count() > 0);
}

// ============================================================================
// Integration: threaded fan-out
// ============================================================================

TEST_CASE("MediaNode_Integration_ThreadedFanOut") {
    MediaPipeline pipeline;

    auto *src = new IntSourceNode();
    auto *sink1 = new IntSinkNode("Sink1");
    auto *sink2 = new IntSinkNode("Sink2");

    pipeline.addNode(src);
    pipeline.addNode(sink1);
    pipeline.addNode(sink2);
    pipeline.connect(src, 0, sink1, 0);
    pipeline.connect(src, 0, sink2, 0);

    src->build(MediaNodeConfig());
    sink1->build(MediaNodeConfig());
    sink2->build(MediaNodeConfig());

    Error err = pipeline.start();
    REQUIRE(err == Error::Ok);

    for(int i = 0; i < 100 && (sink1->count() < 3 || sink2->count() < 3); i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    pipeline.stop();

    // Both sinks should have received frames
    CHECK(sink1->count() > 0);
    CHECK(sink2->count() > 0);
}

// ============================================================================
// Integration: threaded fan-in
// ============================================================================

class DualInputSink : public MediaNode {
        PROMEKI_OBJECT(DualInputSink, MediaNode)
        public:
                DualInputSink() : MediaNode() {
                        setName("DualSink");
                        addSink(MediaSink::Ptr::create("video", ContentVideo));
                        addSink(MediaSink::Ptr::create("audio", ContentAudio));
                }
                BuildResult build(const MediaNodeConfig &) override {
                        setState(Configured);
                        return BuildResult();
                }
                void processFrame(Frame::Ptr &frame, int inputIndex, DeliveryList &deliveries) override {
                        (void)deliveries;
                        if(!frame.isValid()) return;
                        if(inputIndex == 0) _videoCount++;
                        else if(inputIndex == 1) _audioCount++;
                        return;
                }
                std::atomic<int> &videoCount() { return _videoCount; }
                std::atomic<int> &audioCount() { return _audioCount; }
        private:
                std::atomic<int> _videoCount{0};
                std::atomic<int> _audioCount{0};
};

TEST_CASE("MediaNode_Integration_ThreadedFanIn") {
    MediaPipeline pipeline;

    auto *videoSrc = new IntSourceNode();
    videoSrc->setName("VideoSrc");
    auto *audioSrc = new IntSourceNode();
    audioSrc->setName("AudioSrc");
    auto *sink = new DualInputSink();

    pipeline.addNode(videoSrc);
    pipeline.addNode(audioSrc);
    pipeline.addNode(sink);
    pipeline.connect(videoSrc, "output", sink, "video");
    pipeline.connect(audioSrc, "output", sink, "audio");

    videoSrc->build(MediaNodeConfig());
    audioSrc->build(MediaNodeConfig());
    sink->build(MediaNodeConfig());

    Error err = pipeline.start();
    REQUIRE(err == Error::Ok);

    for(int i = 0; i < 100 && (sink->videoCount() < 3 || sink->audioCount() < 3); i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    pipeline.stop();

    CHECK(sink->videoCount() > 0);
    CHECK(sink->audioCount() > 0);
}

// ============================================================================
// Integration: backpressure (queue fills, source blocks)
// ============================================================================

class ShallowSinkNode : public MediaNode {
        PROMEKI_OBJECT(ShallowSinkNode, MediaNode)
        public:
                ShallowSinkNode(int maxDepth) : MediaNode() {
                        setName("ShallowSink");
                        auto s = MediaSink::Ptr::create("input", ContentNone);
                        s->setMaxQueueDepth(maxDepth);
                        addSink(s);
                }
                BuildResult build(const MediaNodeConfig &) override {
                        setState(Configured);
                        return BuildResult();
                }
                void processFrame(Frame::Ptr &frame, int inputIndex, DeliveryList &deliveries) override {
                        (void)inputIndex; (void)deliveries;
                        if(frame.isValid()) _count++;
                        return;
                }
                std::atomic<int> &count() { return _count; }
        private:
                std::atomic<int> _count{0};
};

TEST_CASE("MediaNode_Integration_Backpressure") {
    MediaPipeline pipeline;

    auto *src = new IntSourceNode();
    auto *sink = new ShallowSinkNode(2);

    pipeline.addNode(src);
    pipeline.addNode(sink);
    pipeline.connect(src, 0, sink, 0);

    src->build(MediaNodeConfig());
    sink->build(MediaNodeConfig());

    Error err = pipeline.start();
    REQUIRE(err == Error::Ok);

    // Let it run -- source should produce, sink should consume
    for(int i = 0; i < 100 && sink->count() < 5; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    pipeline.stop();

    // Source produced frames, sink consumed them despite shallow queue
    CHECK(src->seq() > 0);
    CHECK(sink->count() > 0);
}
