/**
 * @file      mediapipelineconfig.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/proav/mediapipeline.h>
#include <promeki/proav/medianodeconfig.h>

using namespace promeki;

// ============================================================================
// Helper nodes for build() tests
// ============================================================================

class BuildSourceNode : public MediaNode {
        PROMEKI_OBJECT(BuildSourceNode, MediaNode)
        public:
                BuildSourceNode() : MediaNode() {
                        addSource(MediaSource::Ptr::create("output",
                                  ContentHint(ContentVideo | ContentAudio)));
                }
                void process() override { }
                BuildResult build(const MediaNodeConfig &config) override {
                        _builtWidth = config.get("width", Variant(uint32_t(0))).get<uint32_t>();
                        setState(Configured);
                        return BuildResult();
                }
                uint32_t builtWidth() const { return _builtWidth; }
        private:
                uint32_t _builtWidth = 0;
};
PROMEKI_REGISTER_NODE(BuildSourceNode)

class BuildSinkNode : public MediaNode {
        PROMEKI_OBJECT(BuildSinkNode, MediaNode)
        public:
                BuildSinkNode() : MediaNode() {
                        addSink(MediaSink::Ptr::create("input", ContentNone));
                }
                void process() override { }
                BuildResult build(const MediaNodeConfig &) override {
                        setState(Configured);
                        return BuildResult();
                }
};
PROMEKI_REGISTER_NODE(BuildSinkNode)

class BuildProcessNode : public MediaNode {
        PROMEKI_OBJECT(BuildProcessNode, MediaNode)
        public:
                BuildProcessNode() : MediaNode() {
                        addSink(MediaSink::Ptr::create("input", ContentNone));
                        addSource(MediaSource::Ptr::create("output", ContentNone));
                }
                void process() override { }
                BuildResult build(const MediaNodeConfig &) override {
                        setState(Configured);
                        return BuildResult();
                }
};
PROMEKI_REGISTER_NODE(BuildProcessNode)

class BuildMultiSourceNode : public MediaNode {
        PROMEKI_OBJECT(BuildMultiSourceNode, MediaNode)
        public:
                BuildMultiSourceNode() : MediaNode() {
                        addSource(MediaSource::Ptr::create("video",
                                  ContentHint(ContentVideo)));
                        addSource(MediaSource::Ptr::create("audio",
                                  ContentHint(ContentAudio)));
                }
                void process() override { }
                BuildResult build(const MediaNodeConfig &) override {
                        setState(Configured);
                        return BuildResult();
                }
};
PROMEKI_REGISTER_NODE(BuildMultiSourceNode)

class BuildMultiSinkNode : public MediaNode {
        PROMEKI_OBJECT(BuildMultiSinkNode, MediaNode)
        public:
                BuildMultiSinkNode() : MediaNode() {
                        addSink(MediaSink::Ptr::create("video", ContentNone));
                        addSink(MediaSink::Ptr::create("audio", ContentNone));
                }
                void process() override { }
                BuildResult build(const MediaNodeConfig &) override {
                        setState(Configured);
                        return BuildResult();
                }
};
PROMEKI_REGISTER_NODE(BuildMultiSinkNode)

class BuildFailNode : public MediaNode {
        PROMEKI_OBJECT(BuildFailNode, MediaNode)
        public:
                BuildFailNode() : MediaNode() {
                        addSink(MediaSink::Ptr::create("input", ContentNone));
                }
                void process() override { }
                BuildResult build(const MediaNodeConfig &) override {
                        BuildResult result;
                        result.addError("build failed");
                        return result;
                }
};
PROMEKI_REGISTER_NODE(BuildFailNode)

// ============================================================================
// MediaPipelineConfig basic tests
// ============================================================================

TEST_CASE("MediaPipelineConfig_Default") {
    MediaPipelineConfig cfg;
    CHECK(cfg.isEmpty());
    CHECK(cfg.size() == 0);
}

TEST_CASE("MediaPipelineConfig_AddNode") {
    MediaPipelineConfig cfg;
    cfg.addNode(MediaNodeConfig("BuildSourceNode", "src"));
    CHECK(cfg.size() == 1);
    CHECK(cfg.nodes()[0].name() == "src");
}

TEST_CASE("MediaPipelineConfig_FindByName") {
    MediaPipelineConfig cfg;
    cfg.addNode(MediaNodeConfig("BuildSourceNode", "alpha"));
    cfg.addNode(MediaNodeConfig("BuildSinkNode", "beta"));

    CHECK(cfg.node("alpha") != nullptr);
    CHECK(cfg.node("alpha")->type() == "BuildSourceNode");
    CHECK(cfg.node("beta") != nullptr);
    CHECK(cfg.node("missing") == nullptr);
}

TEST_CASE("MediaPipelineConfig_Clear") {
    MediaPipelineConfig cfg;
    cfg.addNode(MediaNodeConfig("BuildSourceNode", "a"));
    cfg.clear();
    CHECK(cfg.isEmpty());
}

// ============================================================================
// Validation
// ============================================================================

TEST_CASE("MediaPipelineConfig_ValidateEmpty") {
    MediaPipelineConfig cfg;
    Error err;
    CHECK(!cfg.isValid(&err));
    CHECK(err == Error::Invalid);
}

TEST_CASE("MediaPipelineConfig_ValidateInvalidNode") {
    MediaPipelineConfig cfg;
    cfg.addNode(MediaNodeConfig());  // missing name and type
    CHECK(!cfg.isValid());
}

TEST_CASE("MediaPipelineConfig_ValidateDuplicateNames") {
    MediaPipelineConfig cfg;
    cfg.addNode(MediaNodeConfig("BuildSourceNode", "dup"));
    cfg.addNode(MediaNodeConfig("BuildSinkNode", "dup"));
    Error err;
    CHECK(!cfg.isValid(&err));
    CHECK(err == Error::Exists);
}

TEST_CASE("MediaPipelineConfig_ValidateBadConnection") {
    MediaPipelineConfig cfg;
    MediaNodeConfig sink("BuildSinkNode", "sink");
    sink.addConnection("nonexistent[0]");
    cfg.addNode(MediaNodeConfig("BuildSourceNode", "src"));
    cfg.addNode(sink);
    Error err;
    CHECK(!cfg.isValid(&err));
    CHECK(err == Error::NotExist);
}

TEST_CASE("MediaPipelineConfig_ValidateGood") {
    MediaPipelineConfig cfg;
    cfg.addNode(MediaNodeConfig("BuildSourceNode", "src"));
    MediaNodeConfig sink("BuildSinkNode", "sink");
    sink.addConnection("src[0]");
    cfg.addNode(sink);
    CHECK(cfg.isValid());
}

// ============================================================================
// MediaPipeline::build() basic
// ============================================================================

TEST_CASE("MediaPipeline_BuildSimple") {
    MediaPipeline pipeline;
    MediaPipelineConfig cfg;

    cfg.addNode(MediaNodeConfig("BuildSourceNode", "src"));
    MediaNodeConfig sink("BuildSinkNode", "sink");
    sink.addConnection("src[0]");
    cfg.addNode(sink);

    auto result = pipeline.build(cfg);
    CHECK(result.isOk());
    CHECK(pipeline.nodes().size() == 2);
    CHECK(pipeline.node("src") != nullptr);
    CHECK(pipeline.node("sink") != nullptr);

    // Connection should be wired
    CHECK(pipeline.node("src")->source(0)->isConnected());
}

// ============================================================================
// Build with named source connection
// ============================================================================

TEST_CASE("MediaPipeline_BuildNamedSource") {
    MediaPipeline pipeline;
    MediaPipelineConfig cfg;

    cfg.addNode(MediaNodeConfig("BuildMultiSourceNode", "src"));
    MediaNodeConfig sink("BuildMultiSinkNode", "sink");
    sink.addConnection("src.video");
    sink.addConnection("src.audio");
    cfg.addNode(sink);

    auto result = pipeline.build(cfg);
    CHECK(result.isOk());
    CHECK(pipeline.node("src")->source("video")->isConnected());
    CHECK(pipeline.node("src")->source("audio")->isConnected());
}

// ============================================================================
// Build with bare node name (index 0 shorthand)
// ============================================================================

TEST_CASE("MediaPipeline_BuildBareName") {
    MediaPipeline pipeline;
    MediaPipelineConfig cfg;

    cfg.addNode(MediaNodeConfig("BuildSourceNode", "src"));
    MediaNodeConfig sink("BuildSinkNode", "sink");
    sink.addConnection("src");
    cfg.addNode(sink);

    auto result = pipeline.build(cfg);
    CHECK(result.isOk());
    CHECK(pipeline.node("src")->source(0)->isConnected());
}

// ============================================================================
// Build passes config options to node
// ============================================================================

TEST_CASE("MediaPipeline_BuildPassesOptions") {
    MediaPipeline pipeline;
    MediaPipelineConfig cfg;

    MediaNodeConfig srcCfg("BuildSourceNode", "src");
    srcCfg.set("width", Variant(uint32_t(1920)));
    cfg.addNode(srcCfg);

    MediaNodeConfig sink("BuildSinkNode", "sink");
    sink.addConnection("src");
    cfg.addNode(sink);

    auto result = pipeline.build(cfg);
    CHECK(result.isOk());

    auto *srcNode = static_cast<BuildSourceNode *>(pipeline.node("src"));
    CHECK(srcNode->builtWidth() == 1920);
}

// ============================================================================
// Build with chain (three nodes)
// ============================================================================

TEST_CASE("MediaPipeline_BuildChain") {
    MediaPipeline pipeline;
    MediaPipelineConfig cfg;

    cfg.addNode(MediaNodeConfig("BuildSourceNode", "src"));

    MediaNodeConfig proc("BuildProcessNode", "proc");
    proc.addConnection("src[0]");
    cfg.addNode(proc);

    MediaNodeConfig sink("BuildSinkNode", "sink");
    sink.addConnection("proc[0]");
    cfg.addNode(sink);

    auto result = pipeline.build(cfg);
    CHECK(result.isOk());
    CHECK(pipeline.nodes().size() == 3);

    // Verify topology
    auto sorted = pipeline.topologicalSort();
    CHECK(sorted.size() == 3);
}

// ============================================================================
// Build order independence
// ============================================================================

TEST_CASE("MediaPipeline_BuildOrderIndependent") {
    MediaPipeline pipeline;
    MediaPipelineConfig cfg;

    // Add in reverse order
    MediaNodeConfig sink("BuildSinkNode", "sink");
    sink.addConnection("proc[0]");
    cfg.addNode(sink);

    MediaNodeConfig proc("BuildProcessNode", "proc");
    proc.addConnection("src[0]");
    cfg.addNode(proc);

    cfg.addNode(MediaNodeConfig("BuildSourceNode", "src"));

    auto result = pipeline.build(cfg);
    CHECK(result.isOk());
    CHECK(pipeline.nodes().size() == 3);
}

// ============================================================================
// Build error: unknown node type
// ============================================================================

TEST_CASE("MediaPipeline_BuildUnknownType") {
    MediaPipeline pipeline;
    MediaPipelineConfig cfg;
    cfg.addNode(MediaNodeConfig("NoSuchNodeType", "bad"));

    auto result = pipeline.build(cfg);
    CHECK(result.isError());
    CHECK(result.entries.size() == 1);
    CHECK(result.entries[0].nodeName == "bad");
    CHECK(result.entries[0].error == Error::NotExist);
    CHECK(pipeline.nodes().isEmpty());
}

// ============================================================================
// Build error: duplicate names
// ============================================================================

TEST_CASE("MediaPipeline_BuildDuplicateNames") {
    MediaPipeline pipeline;
    MediaPipelineConfig cfg;
    cfg.addNode(MediaNodeConfig("BuildSourceNode", "dup"));
    cfg.addNode(MediaNodeConfig("BuildSinkNode", "dup"));

    auto result = pipeline.build(cfg);
    CHECK(result.isError());
    CHECK(pipeline.nodes().isEmpty());
}

// ============================================================================
// Build error: bad connection target
// ============================================================================

TEST_CASE("MediaPipeline_BuildBadConnectionTarget") {
    MediaPipeline pipeline;
    MediaPipelineConfig cfg;

    cfg.addNode(MediaNodeConfig("BuildSourceNode", "src"));
    MediaNodeConfig sink("BuildSinkNode", "sink");
    sink.addConnection("nonexistent[0]");
    cfg.addNode(sink);

    auto result = pipeline.build(cfg);
    CHECK(result.isError());
    CHECK(result.entries[0].error == Error::NotExist);
    CHECK(pipeline.nodes().isEmpty());
}

// ============================================================================
// Build error: source index out of range
// ============================================================================

TEST_CASE("MediaPipeline_BuildSourceIndexOutOfRange") {
    MediaPipeline pipeline;
    MediaPipelineConfig cfg;

    cfg.addNode(MediaNodeConfig("BuildSourceNode", "src"));
    MediaNodeConfig sink("BuildSinkNode", "sink");
    sink.addConnection("src[99]");
    cfg.addNode(sink);

    auto result = pipeline.build(cfg);
    CHECK(result.isError());
    CHECK(result.entries[0].error == Error::OutOfRange);
}

// ============================================================================
// Build error: source name not found
// ============================================================================

TEST_CASE("MediaPipeline_BuildSourceNameNotFound") {
    MediaPipeline pipeline;
    MediaPipelineConfig cfg;

    cfg.addNode(MediaNodeConfig("BuildSourceNode", "src"));
    MediaNodeConfig sink("BuildSinkNode", "sink");
    sink.addConnection("src.nonexistent");
    cfg.addNode(sink);

    auto result = pipeline.build(cfg);
    CHECK(result.isError());
    CHECK(result.entries[0].error == Error::NotExist);
}

// ============================================================================
// Build error: node build() failure
// ============================================================================

TEST_CASE("MediaPipeline_BuildNodeBuildFailure") {
    MediaPipeline pipeline;
    MediaPipelineConfig cfg;

    cfg.addNode(MediaNodeConfig("BuildSourceNode", "src"));
    MediaNodeConfig failCfg("BuildFailNode", "fail");
    failCfg.addConnection("src[0]");
    cfg.addNode(failCfg);

    auto result = pipeline.build(cfg);
    CHECK(result.isError());
    CHECK(result.entries[0].nodeName == "fail");
    CHECK(result.entries[0].error == Error::Invalid);
    CHECK(pipeline.nodes().isEmpty());
}

// ============================================================================
// Build error: pipeline not stopped
// ============================================================================

TEST_CASE("MediaPipeline_BuildNotStopped") {
    MediaPipeline pipeline;
    MediaPipelineConfig cfg;

    cfg.addNode(MediaNodeConfig("BuildSourceNode", "src"));
    MediaNodeConfig sink("BuildSinkNode", "sink");
    sink.addConnection("src[0]");
    cfg.addNode(sink);

    // First build and start
    pipeline.build(cfg);
    pipeline.start();

    // Try to build again while running
    auto result = pipeline.build(cfg);
    CHECK(result.isError());
    CHECK(result.entries[0].error == Error::Invalid);

    pipeline.stop();
}

// ============================================================================
// Build clears existing pipeline
// ============================================================================

TEST_CASE("MediaPipeline_BuildClearsPrevious") {
    MediaPipeline pipeline;

    // First build
    MediaPipelineConfig cfg1;
    cfg1.addNode(MediaNodeConfig("BuildSourceNode", "old_src"));
    MediaNodeConfig oldSink("BuildSinkNode", "old_sink");
    oldSink.addConnection("old_src[0]");
    cfg1.addNode(oldSink);
    pipeline.build(cfg1);
    CHECK(pipeline.nodes().size() == 2);

    // Second build replaces everything
    MediaPipelineConfig cfg2;
    cfg2.addNode(MediaNodeConfig("BuildSourceNode", "new_src"));
    MediaNodeConfig newSink("BuildSinkNode", "new_sink");
    newSink.addConnection("new_src[0]");
    cfg2.addNode(newSink);

    auto result = pipeline.build(cfg2);
    CHECK(result.isOk());
    CHECK(pipeline.nodes().size() == 2);
    CHECK(pipeline.node("old_src") == nullptr);
    CHECK(pipeline.node("new_src") != nullptr);
}

// ============================================================================
// Build then start/stop lifecycle
// ============================================================================

TEST_CASE("MediaPipeline_BuildThenStartStop") {
    MediaPipeline pipeline;
    MediaPipelineConfig cfg;

    cfg.addNode(MediaNodeConfig("BuildSourceNode", "src"));
    MediaNodeConfig sink("BuildSinkNode", "sink");
    sink.addConnection("src[0]");
    cfg.addNode(sink);

    auto result = pipeline.build(cfg);
    CHECK(result.isOk());

    Error err = pipeline.start();
    CHECK(err == Error::Ok);
    CHECK(pipeline.state() == MediaPipeline::Running);

    err = pipeline.stop();
    CHECK(err == Error::Ok);
    CHECK(pipeline.state() == MediaPipeline::Stopped);
}

// ============================================================================
// BuildError messages
// ============================================================================

TEST_CASE("MediaPipeline_BuildErrorMessages") {
    MediaPipeline::BuildError be;
    CHECK(be.isOk());
    CHECK(be.messages().isEmpty());

    be.add("node1", Error(Error::Invalid), "first error");
    be.add("node2", Error(Error::NotExist), "second error");
    CHECK(be.isError());

    StringList msgs = be.messages();
    CHECK(msgs.size() == 2);
    CHECK(msgs[0] == "first error");
    CHECK(msgs[1] == "second error");
}

// ============================================================================
// Empty connections are skipped
// ============================================================================

TEST_CASE("MediaPipeline_BuildEmptyConnectionSkipped") {
    MediaPipeline pipeline;
    MediaPipelineConfig cfg;

    cfg.addNode(MediaNodeConfig("BuildSourceNode", "src"));
    MediaNodeConfig sink("BuildSinkNode", "sink");
    sink.addConnection("");  // empty connection, should be skipped
    cfg.addNode(sink);

    auto result = pipeline.build(cfg);
    CHECK(result.isOk());
    // sink's input is not connected, but that's fine
    CHECK(!pipeline.node("src")->source(0)->isConnected());
}
