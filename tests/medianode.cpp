/**
 * @file      medianode.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/proav/medianode.h>
#include <promeki/core/threadpool.h>

using namespace promeki;

// ============================================================================
// Concrete test node subclass
// ============================================================================

class TestNode : public MediaNode {
        PROMEKI_OBJECT(TestNode, MediaNode)
        public:
                TestNode(ObjectBase *parent = nullptr) : MediaNode(parent) { }

                void process() override {
                        _processCount++;
                        return;
                }

                int processCount() const { return _processCount; }

                Map<String, Variant> properties() const override {
                        Map<String, Variant> ret = MediaNode::properties();
                        ret.insert("testValue", Variant(_testValue));
                        return ret;
                }

                Error setProperty(const String &name, const Variant &value) override {
                        if(name == "testValue") {
                                _testValue = value.get<int>();
                                return Error(Error::Ok);
                        }
                        return MediaNode::setProperty(name, value);
                }

                int testValue() const { return _testValue; }
                void setTestValue(int val) { _testValue = val; return; }

                // Expose protected methods for testing
                using MediaNode::emitMessage;
                using MediaNode::emitWarning;
                using MediaNode::emitError;
                using MediaNode::enqueueInput;
                using MediaNode::recordProcessTiming;
                using MediaNode::recordStarvation;

        private:
                int _processCount = 0;
                int _testValue = 0;
};

PROMEKI_REGISTER_NODE(TestNode)

// Concrete source node (no inputs, one output)
class TestSourceNode : public MediaNode {
        PROMEKI_OBJECT(TestSourceNode, MediaNode)
        public:
                TestSourceNode(ObjectBase *parent = nullptr) : MediaNode(parent) {
                        auto port = MediaPort::Ptr::create(
                                "output", MediaPort::Output, MediaPort::Frame
                        );
                        addOutputPort(port);
                }
                void process() override { }
};

PROMEKI_REGISTER_NODE(TestSourceNode)

// Concrete sink node (one input, no outputs)
class TestSinkNode : public MediaNode {
        PROMEKI_OBJECT(TestSinkNode, MediaNode)
        public:
                TestSinkNode(ObjectBase *parent = nullptr) : MediaNode(parent) {
                        auto port = MediaPort::Ptr::create(
                                "input", MediaPort::Input, MediaPort::Frame
                        );
                        addInputPort(port);
                }
                void process() override { }

                // Expose protected methods for testing
                using MediaNode::enqueueInput;
};

PROMEKI_REGISTER_NODE(TestSinkNode)

// ============================================================================
// Default construction and state
// ============================================================================

TEST_CASE("MediaNode_DefaultState") {
    TestNode node;
    CHECK(node.state() == MediaNode::Idle);
    CHECK(node.name().isEmpty());
    CHECK(node.inputPortCount() == 0);
    CHECK(node.outputPortCount() == 0);
    CHECK(node.threadingPolicy() == MediaNode::UseGraphPool);
    CHECK(node.idealQueueSize() == 2);
    CHECK(node.queuedFrameCount() == 0);
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

    Error err = node.configure();
    CHECK(err == Error::Ok);
    CHECK(node.state() == MediaNode::Configured);

    err = node.start();
    CHECK(err == Error::Ok);
    CHECK(node.state() == MediaNode::Running);

    node.stop();
    CHECK(node.state() == MediaNode::Idle);
}

TEST_CASE("MediaNode_ConfigureFromNonIdle") {
    TestNode node;
    node.configure();
    Error err = node.configure();
    CHECK(err == Error::Invalid);
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
    CHECK(node.inputPortCount() == 0);
    CHECK(node.outputPortCount() == 1);
    CHECK(node.outputPort(0)->name() == "output");
    CHECK(node.outputPort(0)->direction() == MediaPort::Output);
    CHECK(node.outputPort(0)->node() == &node);
}

TEST_CASE("MediaNode_SinkPorts") {
    TestSinkNode node;
    CHECK(node.inputPortCount() == 1);
    CHECK(node.outputPortCount() == 0);
    CHECK(node.inputPort(0)->name() == "input");
    CHECK(node.inputPort(0)->direction() == MediaPort::Input);
    CHECK(node.inputPort(0)->node() == &node);
}

TEST_CASE("MediaNode_PortByName") {
    TestSourceNode node;
    auto port = node.outputPort("output");
    CHECK(port.isValid());
    CHECK(port->name() == "output");

    auto missing = node.outputPort("nonexistent");
    CHECK(!missing.isValid());
}

TEST_CASE("MediaNode_PortByIndexOutOfRange") {
    TestNode node;
    auto port = node.inputPort(0);
    CHECK(!port.isValid());
    auto port2 = node.outputPort(-1);
    CHECK(!port2.isValid());
}

// ============================================================================
// Threading policy
// ============================================================================

TEST_CASE("MediaNode_ThreadingPolicy") {
    TestNode node;
    node.setThreadingPolicy(MediaNode::DedicatedThread);
    CHECK(node.threadingPolicy() == MediaNode::DedicatedThread);
}

// ============================================================================
// Input queue
// ============================================================================

TEST_CASE("MediaNode_IdealQueueSize") {
    TestNode node;
    node.setIdealQueueSize(4);
    CHECK(node.idealQueueSize() == 4);
}

TEST_CASE("MediaNode_EnqueueInput") {
    TestSinkNode node;
    CHECK(node.queuedFrameCount() == 0);
    node.enqueueInput(Frame::Ptr::create());
    CHECK(node.queuedFrameCount() == 1);
    node.enqueueInput(Frame::Ptr::create());
    CHECK(node.queuedFrameCount() == 2);
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
// Property interface
// ============================================================================

TEST_CASE("MediaNode_Properties") {
    TestNode node;
    node.setName("test");
    node.setTestValue(42);
    auto props = node.properties();
    CHECK(props.contains("name"));
    CHECK(props.contains("testValue"));
    CHECK(props["name"].get<String>() == "test");
    CHECK(props["testValue"].get<int>() == 42);
}

TEST_CASE("MediaNode_SetProperty") {
    TestNode node;
    Error err = node.setProperty("name", Variant(String("new_name")));
    CHECK(err == Error::Ok);
    CHECK(node.name() == "new_name");

    err = node.setProperty("testValue", Variant(99));
    CHECK(err == Error::Ok);
    CHECK(node.testValue() == 99);
}

TEST_CASE("MediaNode_SetPropertyUnknown") {
    TestNode node;
    Error err = node.setProperty("bogus", Variant(42));
    CHECK(err == Error::Invalid);
}

TEST_CASE("MediaNode_PropertyByName") {
    TestNode node;
    node.setTestValue(7);
    Variant val = node.property("testValue");
    CHECK(val.get<int>() == 7);
}

TEST_CASE("MediaNode_PropertyByNameMissing") {
    TestNode node;
    Variant val = node.property("nonexistent");
    CHECK(!val.isValid());
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
    CHECK(node->inputPortCount() == 0);
    CHECK(node->outputPortCount() == 0);
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

    node.configure();
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

    // Starting from Idle (not Configured) should fail
    Error err = node.start();
    CHECK(err == Error::Invalid);
    // Error signal is not emitted for state violations — those are returned
    CHECK(!gotError);
}

// ============================================================================
// Starvation callback (default is no-op)
// ============================================================================

class StarvationTestNode : public MediaNode {
        PROMEKI_OBJECT(StarvationTestNode, MediaNode)
        public:
                StarvationTestNode() : MediaNode() { }
                void process() override { }
                void starvation() override { _starvationCount++; return; }
                int starvationCount() const { return _starvationCount; }
        private:
                int _starvationCount = 0;
};

TEST_CASE("MediaNode_Starvation") {
    StarvationTestNode node;
    CHECK(node.starvationCount() == 0);
    node.starvation();
    node.starvation();
    CHECK(node.starvationCount() == 2);
}

TEST_CASE("MediaNode_DefaultStarvationNoOp") {
    TestNode node;
    // Default starvation() is a no-op — just ensure it doesn't crash
    node.starvation();
}

// ============================================================================
// Custom thread pool
// ============================================================================

TEST_CASE("MediaNode_CustomPool") {
    TestNode node;
    ThreadPool pool(2);
    node.setThreadingPolicy(&pool);
    CHECK(node.threadingPolicy() == MediaNode::CustomPool);
    CHECK(node.customThreadPool() == &pool);
}

// ============================================================================
// Multiple ports
// ============================================================================

class MultiPortNode : public MediaNode {
        PROMEKI_OBJECT(MultiPortNode, MediaNode)
        public:
                MultiPortNode() : MediaNode() {
                        addInputPort(MediaPort::Ptr::create("in1", MediaPort::Input, MediaPort::Frame));
                        addInputPort(MediaPort::Ptr::create("in2", MediaPort::Input, MediaPort::Audio));
                        addOutputPort(MediaPort::Ptr::create("out1", MediaPort::Output, MediaPort::Frame));
                }
                void process() override { }
};

TEST_CASE("MediaNode_MultiplePorts") {
    MultiPortNode node;
    CHECK(node.inputPortCount() == 2);
    CHECK(node.outputPortCount() == 1);
    CHECK(node.inputPort("in1")->mediaType() == MediaPort::Frame);
    CHECK(node.inputPort("in2")->mediaType() == MediaPort::Audio);
    CHECK(node.outputPort("out1")->mediaType() == MediaPort::Frame);
}

// ============================================================================
// Stop from non-running state
// ============================================================================

TEST_CASE("MediaNode_StopFromIdle") {
    TestNode node;
    // stop() transitions to Idle regardless — just shouldn't crash
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
    CHECK(s.starvationCount == 0);
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

TEST_CASE("MediaNode_RecordStarvation") {
    TestNode node;
    node.recordStarvation();
    node.recordStarvation();
    CHECK(node.stats().starvationCount == 2);
}

TEST_CASE("MediaNode_ResetStats") {
    TestNode node;
    node.recordProcessTiming(0.010);
    node.recordStarvation();
    node.resetStats();
    NodeStats s = node.stats();
    CHECK(s.processCount == 0);
    CHECK(s.starvationCount == 0);
    CHECK(s.peakProcessDuration == 0.0);
}

TEST_CASE("MediaNode_PeakQueueDepth") {
    TestSinkNode node;
    node.enqueueInput(Frame::Ptr::create());
    node.enqueueInput(Frame::Ptr::create());
    node.enqueueInput(Frame::Ptr::create());
    CHECK(node.stats().peakQueueDepth == 3);
    CHECK(node.stats().currentQueueDepth == 3);
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
    // Node should still be in same state (warning doesn't change state)
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
                        addInputPort(MediaPort::Ptr::create("input", MediaPort::Input, MediaPort::Frame));
                }
                void process() override { }
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
                        addOutputPort(MediaPort::Ptr::create("output", MediaPort::Output, MediaPort::Frame));
                }
                void process() override { }
                using MediaNode::deliverOutput;
};

TEST_CASE("MediaNode_DeliverOutputNoLinks") {
    DeliverTestNode node;
    Frame::Ptr frame = Frame::Ptr::create();
    // Should be a no-op — no links, no crash
    node.deliverOutput(0, frame);
    node.deliverOutput(frame);
}

TEST_CASE("MediaNode_DeliverOutputInvalidPort") {
    DeliverTestNode node;
    Frame::Ptr frame = Frame::Ptr::create();
    // Port index 5 doesn't exist — should be a no-op
    node.deliverOutput(5, frame);
}
