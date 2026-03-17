/**
 * @file      mediapipeline.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/proav/mediapipeline.h>

using namespace promeki;

// ============================================================================
// Helper nodes
// ============================================================================

class PipeSourceNode : public MediaNode {
        PROMEKI_OBJECT(PipeSourceNode, MediaNode)
        public:
                PipeSourceNode() : MediaNode() {
                        auto port = MediaPort::Ptr::create("output", MediaPort::Output, MediaPort::Frame);
                        addOutputPort(port);
                }
                void process() override {
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
                        auto port = MediaPort::Ptr::create("input", MediaPort::Input, MediaPort::Frame);
                        addInputPort(port);
                }
                void process() override {
                        _processCount++;
                }
                int processCount() const { return _processCount; }
        private:
                int _processCount = 0;
};

// ============================================================================
// Default state
// ============================================================================

TEST_CASE("MediaPipeline_DefaultState") {
    MediaPipeline pipeline;
    CHECK(pipeline.state() == MediaPipeline::Stopped);
    CHECK(pipeline.graph() != nullptr);
    CHECK(pipeline.graph()->nodes().isEmpty());
}

// ============================================================================
// Start / Stop lifecycle
// ============================================================================

TEST_CASE("MediaPipeline_StartStop") {
    MediaPipeline pipeline;

    auto *src = new PipeSourceNode();
    auto *sink = new PipeSinkNode();
    pipeline.graph()->addNode(src);
    pipeline.graph()->addNode(sink);
    pipeline.graph()->connect(src, 0, sink, 0);

    Error err = pipeline.start();
    CHECK(err == Error::Ok);
    CHECK(pipeline.state() == MediaPipeline::Running);

    // Verify nodes were configured and started
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
// Start with invalid graph
// ============================================================================

TEST_CASE("MediaPipeline_StartEmptyGraph") {
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
    pipeline.graph()->addNode(src);
    pipeline.graph()->addNode(sink);
    pipeline.graph()->connect(src, 0, sink, 0);

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
    pipeline.graph()->addNode(src);
    pipeline.graph()->addNode(sink);
    pipeline.graph()->connect(src, 0, sink, 0);

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
    pipeline.graph()->addNode(src);
    pipeline.graph()->addNode(sink);
    pipeline.graph()->connect(src, 0, sink, 0);

    pipeline.start();
    CHECK(gotStarted);
    CHECK(stateCount >= 1);

    pipeline.stop();
    CHECK(gotStopped);
}

// ============================================================================
// Error signal on bad graph
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
// Custom thread pool
// ============================================================================

TEST_CASE("MediaPipeline_CustomThreadPool") {
    MediaPipeline pipeline;
    ThreadPool pool(2);
    pipeline.setThreadPool(&pool);

    auto *src = new PipeSourceNode();
    auto *sink = new PipeSinkNode();
    pipeline.graph()->addNode(src);
    pipeline.graph()->addNode(sink);
    pipeline.graph()->connect(src, 0, sink, 0);

    Error err = pipeline.start();
    CHECK(err == Error::Ok);
    CHECK(pipeline.threadPool() == &pool);

    pipeline.stop();
}

// ============================================================================
// Node configuration failure
// ============================================================================

class FailConfigNode : public MediaNode {
        PROMEKI_OBJECT(FailConfigNode, MediaNode)
        public:
                FailConfigNode() : MediaNode() {
                        auto port = MediaPort::Ptr::create("output", MediaPort::Output, MediaPort::Frame);
                        addOutputPort(port);
                }
                Error configure() override {
                        return Error(Error::LibraryFailure);
                }
                void process() override { }
};

TEST_CASE("MediaPipeline_NodeConfigFailure") {
    MediaPipeline pipeline;

    auto *bad = new FailConfigNode();
    auto *sink = new PipeSinkNode();
    pipeline.graph()->addNode(bad);
    pipeline.graph()->addNode(sink);
    pipeline.graph()->connect(bad, 0, sink, 0);

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
                        auto port = MediaPort::Ptr::create("input", MediaPort::Input, MediaPort::Frame);
                        addInputPort(port);
                }
                Error start() override {
                        return Error(Error::LibraryFailure);
                }
                void process() override { }
};

TEST_CASE("MediaPipeline_NodeStartFailureRollback") {
    MediaPipeline pipeline;

    auto *src = new PipeSourceNode();
    auto *bad = new FailStartNode();
    pipeline.graph()->addNode(src);
    pipeline.graph()->addNode(bad);
    pipeline.graph()->connect(src, 0, bad, 0);

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
    pipeline.graph()->addNode(src);
    pipeline.graph()->addNode(sink);
    pipeline.graph()->connect(src, 0, sink, 0);

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
// Pause from error state
// ============================================================================

TEST_CASE("MediaPipeline_PauseFromErrorState") {
    MediaPipeline pipeline;
    // Start with empty graph to get into ErrorState
    pipeline.start();
    CHECK(pipeline.state() == MediaPipeline::ErrorState);

    Error err = pipeline.pause();
    CHECK(err == Error::Invalid);
}

// ============================================================================
// Resume from stopped
// ============================================================================

TEST_CASE("MediaPipeline_ResumeFromStopped") {
    MediaPipeline pipeline;
    Error err = pipeline.resume();
    CHECK(err == Error::Invalid);
}

// ============================================================================
// Destructor while running calls stop
// ============================================================================

TEST_CASE("MediaPipeline_DestructorWhileRunning") {
    auto *src = new PipeSourceNode();
    auto *sink = new PipeSinkNode();

    {
        MediaPipeline pipeline;
        pipeline.graph()->addNode(src);
        pipeline.graph()->addNode(sink);
        pipeline.graph()->connect(src, 0, sink, 0);

        pipeline.start();
        CHECK(pipeline.state() == MediaPipeline::Running);
        // Let destructor run without calling stop()
    }
    // If we got here without crashing, the destructor properly cleaned up
}

// ============================================================================
// Destructor while paused calls stop
// ============================================================================

TEST_CASE("MediaPipeline_DestructorWhilePaused") {
    auto *src = new PipeSourceNode();
    auto *sink = new PipeSinkNode();

    {
        MediaPipeline pipeline;
        pipeline.graph()->addNode(src);
        pipeline.graph()->addNode(sink);
        pipeline.graph()->connect(src, 0, sink, 0);

        pipeline.start();
        pipeline.pause();
        CHECK(pipeline.state() == MediaPipeline::Paused);
    }
}

// ============================================================================
// External thread pool is not deleted
// ============================================================================

TEST_CASE("MediaPipeline_ExternalPoolNotDeleted") {
    ThreadPool pool(2);
    bool poolUsable = false;

    {
        MediaPipeline pipeline;
        pipeline.setThreadPool(&pool);

        auto *src = new PipeSourceNode();
        auto *sink = new PipeSinkNode();
        pipeline.graph()->addNode(src);
        pipeline.graph()->addNode(sink);
        pipeline.graph()->connect(src, 0, sink, 0);

        pipeline.start();
        pipeline.stop();
    }

    // Pool should still be usable after pipeline destruction
    pool.submit([]() { });
    pool.waitForDone();
    poolUsable = true;
    CHECK(poolUsable);
}

// ============================================================================
// Stop from paused state
// ============================================================================

TEST_CASE("MediaPipeline_StopFromPaused") {
    MediaPipeline pipeline;

    auto *src = new PipeSourceNode();
    auto *sink = new PipeSinkNode();
    pipeline.graph()->addNode(src);
    pipeline.graph()->addNode(sink);
    pipeline.graph()->connect(src, 0, sink, 0);

    pipeline.start();
    pipeline.pause();
    CHECK(pipeline.state() == MediaPipeline::Paused);

    Error err = pipeline.stop();
    CHECK(err == Error::Ok);
    CHECK(pipeline.state() == MediaPipeline::Stopped);
}
