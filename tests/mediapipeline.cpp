/**
 * @file      mediapipeline.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/mediapipeline.h>
#include <promeki/medianodeconfig.h>

using namespace promeki;

// ============================================================================
// Helper nodes
// ============================================================================

class PipeSourceNode : public MediaNode {
        PROMEKI_OBJECT(PipeSourceNode, MediaNode)
        public:
                PipeSourceNode() : MediaNode() {
                        addSource(MediaSource::Ptr::create("output",
                                  ContentHint(ContentVideo | ContentAudio)));
                }
                BuildResult build(const MediaNodeConfig &) override {
                        setState(Configured);
                        return BuildResult();
                }
                void processFrame(Frame::Ptr &frame, int inputIndex, DeliveryList &deliveries) override {
                        (void)frame; (void)inputIndex; (void)deliveries;
                        _processCount++;
                }
                int processCount() const { return _processCount; }
        private:
                int _processCount = 0;
};

class PipeSinkNode : public MediaNode {
        PROMEKI_OBJECT(PipeSinkNode, MediaNode)
        public:
                PipeSinkNode() : MediaNode() {
                        addSink(MediaSink::Ptr::create("input", ContentNone));
                }
                BuildResult build(const MediaNodeConfig &) override {
                        setState(Configured);
                        return BuildResult();
                }
                void processFrame(Frame::Ptr &frame, int inputIndex, DeliveryList &deliveries) override {
                        (void)frame; (void)inputIndex; (void)deliveries;
                        _processCount++;
                }
                int processCount() const { return _processCount; }
        private:
                int _processCount = 0;
};

class PipeProcessNode : public MediaNode {
        PROMEKI_OBJECT(PipeProcessNode, MediaNode)
        public:
                PipeProcessNode() : MediaNode() {
                        addSink(MediaSink::Ptr::create("input", ContentNone));
                        addSource(MediaSource::Ptr::create("output", ContentNone));
                }
                BuildResult build(const MediaNodeConfig &) override {
                        setState(Configured);
                        return BuildResult();
                }
                void processFrame(Frame::Ptr &frame, int inputIndex, DeliveryList &deliveries) override {
                        (void)frame; (void)inputIndex; (void)deliveries;
                }
};

// Helper to build all nodes in a pipeline with default config
static void buildAllNodes(MediaPipeline &pipeline) {
        for(auto *node : pipeline.nodes()) {
                node->build(MediaNodeConfig());
        }
}

// ============================================================================
// Default state
// ============================================================================

TEST_CASE("MediaPipeline_DefaultState") {
    MediaPipeline pipeline;
    CHECK(pipeline.state() == MediaPipeline::Stopped);
    CHECK(pipeline.nodes().isEmpty());
}

// ============================================================================
// Node management
// ============================================================================

TEST_CASE("MediaPipeline_AddNode") {
    MediaPipeline pipeline;
    auto *src = new PipeSourceNode();
    src->setName("source");
    Error err = pipeline.addNode(src);
    CHECK(err == Error::Ok);
    CHECK(pipeline.nodes().size() == 1);
}

TEST_CASE("MediaPipeline_AddDuplicate") {
    MediaPipeline pipeline;
    auto *src = new PipeSourceNode();
    pipeline.addNode(src);
    Error err = pipeline.addNode(src);
    CHECK(err == Error::Exists);
}

TEST_CASE("MediaPipeline_AddNull") {
    MediaPipeline pipeline;
    Error err = pipeline.addNode(nullptr);
    CHECK(err == Error::Invalid);
}

TEST_CASE("MediaPipeline_FindByName") {
    MediaPipeline pipeline;
    auto *src = new PipeSourceNode();
    src->setName("mysource");
    pipeline.addNode(src);
    CHECK(pipeline.node("mysource") == src);
    CHECK(pipeline.node("nonexistent") == nullptr);
}

TEST_CASE("MediaPipeline_RemoveNode") {
    MediaPipeline pipeline;
    auto *src = new PipeSourceNode();
    pipeline.addNode(src);
    Error err = pipeline.removeNode(src);
    CHECK(err == Error::Ok);
    CHECK(pipeline.nodes().isEmpty());
    delete src;
}

// ============================================================================
// Connecting nodes
// ============================================================================

TEST_CASE("MediaPipeline_ConnectByIndex") {
    MediaPipeline pipeline;
    auto *src = new PipeSourceNode();
    auto *sink = new PipeSinkNode();
    pipeline.addNode(src);
    pipeline.addNode(sink);

    Error err = pipeline.connect(src, 0, sink, 0);
    CHECK(err == Error::Ok);
    CHECK(src->source(0)->isConnected());
}

TEST_CASE("MediaPipeline_ConnectByName") {
    MediaPipeline pipeline;
    auto *src = new PipeSourceNode();
    auto *sink = new PipeSinkNode();
    pipeline.addNode(src);
    pipeline.addNode(sink);

    Error err = pipeline.connect(src, "output", sink, "input");
    CHECK(err == Error::Ok);
    CHECK(src->source(0)->connectedSinks().size() == 1);
}

TEST_CASE("MediaPipeline_ConnectBySourceSink") {
    MediaPipeline pipeline;
    auto *src = new PipeSourceNode();
    auto *sink = new PipeSinkNode();
    pipeline.addNode(src);
    pipeline.addNode(sink);

    Error err = pipeline.connect(src->source(0), sink->sink(0));
    CHECK(err == Error::Ok);
    CHECK(src->source(0)->isConnected());
}

TEST_CASE("MediaPipeline_ConnectInvalidIndex") {
    MediaPipeline pipeline;
    auto *src = new PipeSourceNode();
    auto *sink = new PipeSinkNode();
    pipeline.addNode(src);
    pipeline.addNode(sink);

    Error err = pipeline.connect(src, 5, sink, 5);
    CHECK(err == Error::Invalid);
}

// ============================================================================
// Disconnecting
// ============================================================================

TEST_CASE("MediaPipeline_Disconnect") {
    MediaPipeline pipeline;
    auto *src = new PipeSourceNode();
    auto *sink = new PipeSinkNode();
    pipeline.addNode(src);
    pipeline.addNode(sink);

    pipeline.connect(src, 0, sink, 0);
    CHECK(src->source(0)->isConnected());

    Error err = pipeline.disconnect(src->source(0), sink->sink(0));
    CHECK(err == Error::Ok);
    CHECK(!src->source(0)->isConnected());
}

TEST_CASE("MediaPipeline_RemoveNodeDisconnects") {
    MediaPipeline pipeline;
    auto *src = new PipeSourceNode();
    auto *sink = new PipeSinkNode();
    pipeline.addNode(src);
    pipeline.addNode(sink);
    pipeline.connect(src, 0, sink, 0);
    CHECK(src->source(0)->isConnected());

    pipeline.removeNode(src);
    CHECK(!src->source(0)->isConnected());
    delete src;
}

// ============================================================================
// Validation
// ============================================================================

TEST_CASE("MediaPipeline_ValidateEmpty") {
    MediaPipeline pipeline;
    CHECK(pipeline.validate() == Error::Invalid);
}

TEST_CASE("MediaPipeline_ValidateSimple") {
    MediaPipeline pipeline;
    auto *src = new PipeSourceNode();
    auto *sink = new PipeSinkNode();
    pipeline.addNode(src);
    pipeline.addNode(sink);
    pipeline.connect(src, 0, sink, 0);
    CHECK(pipeline.validate() == Error::Ok);
}

// ============================================================================
// Topological sort
// ============================================================================

TEST_CASE("MediaPipeline_TopologicalSort") {
    MediaPipeline pipeline;
    auto *src = new PipeSourceNode();
    src->setName("src");
    auto *proc = new PipeProcessNode();
    proc->setName("proc");
    auto *sink = new PipeSinkNode();
    sink->setName("sink");

    pipeline.addNode(src);
    pipeline.addNode(proc);
    pipeline.addNode(sink);
    pipeline.connect(src, 0, proc, 0);
    pipeline.connect(proc, 0, sink, 0);

    auto sorted = pipeline.topologicalSort();
    CHECK(sorted.size() == 3);

    int srcIdx = -1, procIdx = -1, sinkIdx = -1;
    for(int i = 0; i < (int)sorted.size(); i++) {
        if(sorted[i] == src) srcIdx = i;
        if(sorted[i] == proc) procIdx = i;
        if(sorted[i] == sink) sinkIdx = i;
    }
    CHECK(srcIdx < procIdx);
    CHECK(procIdx < sinkIdx);
}

// ============================================================================
// Source and sink node queries
// ============================================================================

TEST_CASE("MediaPipeline_SourceAndSinkNodes") {
    MediaPipeline pipeline;
    auto *src = new PipeSourceNode();
    auto *proc = new PipeProcessNode();
    auto *sink = new PipeSinkNode();

    pipeline.addNode(src);
    pipeline.addNode(proc);
    pipeline.addNode(sink);
    pipeline.connect(src, 0, proc, 0);
    pipeline.connect(proc, 0, sink, 0);

    auto sources = pipeline.sourceNodes();
    CHECK(sources.size() == 1);
    CHECK(sources[0] == src);

    auto sinks = pipeline.sinkNodes();
    CHECK(sinks.size() == 1);
    CHECK(sinks[0] == sink);
}

// ============================================================================
// Fan-out
// ============================================================================

TEST_CASE("MediaPipeline_FanOut") {
    MediaPipeline pipeline;
    auto *src = new PipeSourceNode();
    auto *sink1 = new PipeSinkNode();
    sink1->setName("sink1");
    auto *sink2 = new PipeSinkNode();
    sink2->setName("sink2");

    pipeline.addNode(src);
    pipeline.addNode(sink1);
    pipeline.addNode(sink2);

    Error err1 = pipeline.connect(src, 0, sink1, 0);
    Error err2 = pipeline.connect(src, 0, sink2, 0);
    CHECK(err1 == Error::Ok);
    CHECK(err2 == Error::Ok);
    CHECK(src->source(0)->connectedSinks().size() == 2);
}

// ============================================================================
// Cycle detection
// ============================================================================

TEST_CASE("MediaPipeline_CycleDetection") {
    MediaPipeline pipeline;
    auto *a = new PipeProcessNode();
    a->setName("a");
    auto *b = new PipeProcessNode();
    b->setName("b");
    auto *c = new PipeProcessNode();
    c->setName("c");

    pipeline.addNode(a);
    pipeline.addNode(b);
    pipeline.addNode(c);

    pipeline.connect(a, 0, b, 0);
    pipeline.connect(b, 0, c, 0);
    pipeline.connect(c, 0, a, 0);

    auto sorted = pipeline.topologicalSort();
    CHECK(sorted.isEmpty());

    CHECK(pipeline.validate() != Error::Ok);
}

// ============================================================================
// Clear
// ============================================================================

TEST_CASE("MediaPipeline_Clear") {
    MediaPipeline pipeline;
    pipeline.addNode(new PipeSourceNode());
    pipeline.addNode(new PipeSinkNode());
    CHECK(pipeline.nodes().size() == 2);

    pipeline.clear();
    CHECK(pipeline.nodes().isEmpty());
}

// ============================================================================
// Start / Stop lifecycle
// ============================================================================

TEST_CASE("MediaPipeline_StartStop") {
    MediaPipeline pipeline;

    auto *src = new PipeSourceNode();
    auto *sink = new PipeSinkNode();
    pipeline.addNode(src);
    pipeline.addNode(sink);
    pipeline.connect(src, 0, sink, 0);

    buildAllNodes(pipeline);

    Error err = pipeline.start();
    CHECK(err == Error::Ok);
    CHECK(pipeline.state() == MediaPipeline::Running);

    // Verify nodes were started
    CHECK(src->state() == MediaNode::Running);
    CHECK(sink->state() == MediaNode::Running);

    err = pipeline.stop();
    CHECK(err == Error::Ok);
    CHECK(pipeline.state() == MediaPipeline::Stopped);

    // Verify nodes were stopped
    CHECK(src->state() == MediaNode::Idle);
    CHECK(sink->state() == MediaNode::Idle);
}

// ============================================================================
// Start with invalid pipeline
// ============================================================================

TEST_CASE("MediaPipeline_StartEmptyPipeline") {
    MediaPipeline pipeline;
    Error err = pipeline.start();
    CHECK(err != Error::Ok);
    CHECK(pipeline.state() == MediaPipeline::ErrorState);
}

// ============================================================================
// Pause / Resume
// ============================================================================

TEST_CASE("MediaPipeline_PauseResume") {
    MediaPipeline pipeline;

    auto *src = new PipeSourceNode();
    auto *sink = new PipeSinkNode();
    pipeline.addNode(src);
    pipeline.addNode(sink);
    pipeline.connect(src, 0, sink, 0);

    buildAllNodes(pipeline);

    pipeline.start();
    CHECK(pipeline.state() == MediaPipeline::Running);

    Error err = pipeline.pause();
    CHECK(err == Error::Ok);
    CHECK(pipeline.state() == MediaPipeline::Paused);

    err = pipeline.resume();
    CHECK(err == Error::Ok);
    CHECK(pipeline.state() == MediaPipeline::Running);

    pipeline.stop();
}

TEST_CASE("MediaPipeline_PauseWhenNotRunning") {
    MediaPipeline pipeline;
    Error err = pipeline.pause();
    CHECK(err == Error::Invalid);
}

TEST_CASE("MediaPipeline_ResumeWhenNotPaused") {
    MediaPipeline pipeline;
    Error err = pipeline.resume();
    CHECK(err == Error::Invalid);
}

// ============================================================================
// Double start / Double stop
// ============================================================================

TEST_CASE("MediaPipeline_DoubleStart") {
    MediaPipeline pipeline;

    auto *src = new PipeSourceNode();
    auto *sink = new PipeSinkNode();
    pipeline.addNode(src);
    pipeline.addNode(sink);
    pipeline.connect(src, 0, sink, 0);

    buildAllNodes(pipeline);

    pipeline.start();
    Error err = pipeline.start();
    CHECK(err == Error::Invalid);

    pipeline.stop();
}

TEST_CASE("MediaPipeline_DoubleStop") {
    MediaPipeline pipeline;
    Error err = pipeline.stop();
    CHECK(err == Error::Invalid);
}

// ============================================================================
// State change signals
// ============================================================================

TEST_CASE("MediaPipeline_Signals") {
    MediaPipeline pipeline;
    int stateCount = 0;
    bool gotStarted = false;
    bool gotStopped = false;

    pipeline.stateChangedSignal.connect([&](MediaPipeline::State) { stateCount++; });
    pipeline.startedSignal.connect([&]() { gotStarted = true; });
    pipeline.stoppedSignal.connect([&]() { gotStopped = true; });

    auto *src = new PipeSourceNode();
    auto *sink = new PipeSinkNode();
    pipeline.addNode(src);
    pipeline.addNode(sink);
    pipeline.connect(src, 0, sink, 0);

    buildAllNodes(pipeline);

    pipeline.start();
    CHECK(gotStarted);
    CHECK(stateCount >= 1);

    pipeline.stop();
    CHECK(gotStopped);
}

// ============================================================================
// Error signal on bad pipeline
// ============================================================================

TEST_CASE("MediaPipeline_ErrorSignal") {
    MediaPipeline pipeline;
    bool gotError = false;

    pipeline.errorOccurredSignal.connect([&](Error) { gotError = true; });

    Error err = pipeline.start();
    CHECK(err != Error::Ok);
    CHECK(gotError);
}

// ============================================================================
// Node build failure
// ============================================================================

class FailConfigNode : public MediaNode {
        PROMEKI_OBJECT(FailConfigNode, MediaNode)
        public:
                FailConfigNode() : MediaNode() {
                        addSource(MediaSource::Ptr::create("output", ContentNone));
                }
                BuildResult build(const MediaNodeConfig &) override {
                        BuildResult result;
                        result.addError("configuration failed");
                        return result;
                }
                void processFrame(Frame::Ptr &frame, int inputIndex, DeliveryList &deliveries) override {
                        (void)frame; (void)inputIndex; (void)deliveries;
                }
};

TEST_CASE("MediaPipeline_NodeConfigFailure") {
    MediaPipeline pipeline;

    auto *bad = new FailConfigNode();
    auto *sink = new PipeSinkNode();
    pipeline.addNode(bad);
    pipeline.addNode(sink);
    pipeline.connect(bad, 0, sink, 0);

    // Build the good node, then try building the bad one
    sink->build(MediaNodeConfig());
    BuildResult result = bad->build(MediaNodeConfig());
    CHECK(result.isError());

    // Pipeline start will fail because bad node is not Configured
    Error err = pipeline.start();
    CHECK(err != Error::Ok);
    CHECK(pipeline.state() == MediaPipeline::ErrorState);
}

// ============================================================================
// Node start failure with rollback
// ============================================================================

class FailStartNode : public MediaNode {
        PROMEKI_OBJECT(FailStartNode, MediaNode)
        public:
                FailStartNode() : MediaNode() {
                        addSink(MediaSink::Ptr::create("input", ContentNone));
                }
                BuildResult build(const MediaNodeConfig &) override {
                        setState(Configured);
                        return BuildResult();
                }
                Error start() override {
                        return Error(Error::LibraryFailure);
                }
                void processFrame(Frame::Ptr &frame, int inputIndex, DeliveryList &deliveries) override {
                        (void)frame; (void)inputIndex; (void)deliveries;
                }
};

TEST_CASE("MediaPipeline_NodeStartFailureRollback") {
    MediaPipeline pipeline;

    auto *src = new PipeSourceNode();
    auto *bad = new FailStartNode();
    pipeline.addNode(src);
    pipeline.addNode(bad);
    pipeline.connect(src, 0, bad, 0);

    buildAllNodes(pipeline);

    Error err = pipeline.start();
    CHECK(err != Error::Ok);
    CHECK(pipeline.state() == MediaPipeline::ErrorState);

    // Source node should have been rolled back to Idle
    CHECK(src->state() == MediaNode::Idle);
}

// ============================================================================
// State transition signal order
// ============================================================================

TEST_CASE("MediaPipeline_StateTransitionOrder") {
    MediaPipeline pipeline;
    List<MediaPipeline::State> states;

    pipeline.stateChangedSignal.connect([&](MediaPipeline::State s) {
        states.pushToBack(s);
    });

    auto *src = new PipeSourceNode();
    auto *sink = new PipeSinkNode();
    pipeline.addNode(src);
    pipeline.addNode(sink);
    pipeline.connect(src, 0, sink, 0);

    buildAllNodes(pipeline);

    pipeline.start();
    pipeline.stop();

    // Should see: Starting, Running, Stopping, Stopped
    CHECK(states.size() == 4);
    CHECK(states[0] == MediaPipeline::Starting);
    CHECK(states[1] == MediaPipeline::Running);
    CHECK(states[2] == MediaPipeline::Stopping);
    CHECK(states[3] == MediaPipeline::Stopped);
}

// ============================================================================
// Disconnect by source/sink
// ============================================================================

TEST_CASE("MediaPipeline_DisconnectBySourceSink") {
    MediaPipeline pipeline;
    auto *src = new PipeSourceNode();
    auto *sink = new PipeSinkNode();
    pipeline.addNode(src);
    pipeline.addNode(sink);

    pipeline.connect(src, 0, sink, 0);
    CHECK(src->source(0)->isConnected());

    Error err = pipeline.disconnect(src->source(0), sink->sink(0));
    CHECK(err == Error::Ok);
    CHECK(!src->source(0)->isConnected());
}

// ============================================================================
// Validate with unconnected nodes
// ============================================================================

TEST_CASE("MediaPipeline_ValidateUnconnectedNodes") {
    MediaPipeline pipeline;
    auto *src = new PipeSourceNode();
    auto *sink = new PipeSinkNode();
    pipeline.addNode(src);
    pipeline.addNode(sink);
    CHECK(pipeline.validate() == Error::Ok);
}

// ============================================================================
// Stop from paused state
// ============================================================================

TEST_CASE("MediaPipeline_StopFromPaused") {
    MediaPipeline pipeline;

    auto *src = new PipeSourceNode();
    auto *sink = new PipeSinkNode();
    pipeline.addNode(src);
    pipeline.addNode(sink);
    pipeline.connect(src, 0, sink, 0);

    buildAllNodes(pipeline);

    pipeline.start();
    pipeline.pause();
    CHECK(pipeline.state() == MediaPipeline::Paused);

    Error err = pipeline.stop();
    CHECK(err == Error::Ok);
    CHECK(pipeline.state() == MediaPipeline::Stopped);
}

// ============================================================================
// Destructor while running calls stop
// ============================================================================

TEST_CASE("MediaPipeline_DestructorWhileRunning") {
    {
        MediaPipeline pipeline;
        auto *src = new PipeSourceNode();
        auto *sink = new PipeSinkNode();
        pipeline.addNode(src);
        pipeline.addNode(sink);
        pipeline.connect(src, 0, sink, 0);

        buildAllNodes(pipeline);

        pipeline.start();
        CHECK(pipeline.state() == MediaPipeline::Running);
    }
}

// ============================================================================
// Remove non-existent node
// ============================================================================

TEST_CASE("MediaPipeline_RemoveNonExistent") {
    MediaPipeline pipeline;
    auto *node = new PipeSourceNode();
    Error err = pipeline.removeNode(node);
    CHECK(err == Error::NotExist);
    delete node;
}
