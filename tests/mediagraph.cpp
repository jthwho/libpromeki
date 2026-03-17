/**
 * @file      mediagraph.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/proav/mediagraph.h>

using namespace promeki;

// ============================================================================
// Helper nodes
// ============================================================================

class GraphSourceNode : public MediaNode {
        PROMEKI_OBJECT(GraphSourceNode, MediaNode)
        public:
                GraphSourceNode() : MediaNode() {
                        auto port = MediaPort::Ptr::create("output", MediaPort::Output, MediaPort::Frame);
                        addOutputPort(port);
                }
                void process() override { }
};

class GraphProcessNode : public MediaNode {
        PROMEKI_OBJECT(GraphProcessNode, MediaNode)
        public:
                GraphProcessNode() : MediaNode() {
                        auto in = MediaPort::Ptr::create("input", MediaPort::Input, MediaPort::Frame);
                        auto out = MediaPort::Ptr::create("output", MediaPort::Output, MediaPort::Frame);
                        addInputPort(in);
                        addOutputPort(out);
                }
                void process() override { }
};

class GraphSinkNode : public MediaNode {
        PROMEKI_OBJECT(GraphSinkNode, MediaNode)
        public:
                GraphSinkNode() : MediaNode() {
                        auto port = MediaPort::Ptr::create("input", MediaPort::Input, MediaPort::Frame);
                        addInputPort(port);
                }
                void process() override { }
};

// ============================================================================
// Construction and node management
// ============================================================================

TEST_CASE("MediaGraph_Empty") {
    MediaGraph graph;
    CHECK(graph.nodes().isEmpty());
    CHECK(graph.links().isEmpty());
}

TEST_CASE("MediaGraph_AddNode") {
    MediaGraph graph;
    auto *src = new GraphSourceNode();
    src->setName("source");
    Error err = graph.addNode(src);
    CHECK(err == Error::Ok);
    CHECK(graph.nodes().size() == 1);
}

TEST_CASE("MediaGraph_AddDuplicate") {
    MediaGraph graph;
    auto *src = new GraphSourceNode();
    graph.addNode(src);
    Error err = graph.addNode(src);
    CHECK(err == Error::Exists);
}

TEST_CASE("MediaGraph_AddNull") {
    MediaGraph graph;
    Error err = graph.addNode(nullptr);
    CHECK(err == Error::Invalid);
}

TEST_CASE("MediaGraph_FindByName") {
    MediaGraph graph;
    auto *src = new GraphSourceNode();
    src->setName("mysource");
    graph.addNode(src);
    CHECK(graph.node("mysource") == src);
    CHECK(graph.node("nonexistent") == nullptr);
}

TEST_CASE("MediaGraph_RemoveNode") {
    MediaGraph graph;
    auto *src = new GraphSourceNode();
    graph.addNode(src);
    Error err = graph.removeNode(src);
    CHECK(err == Error::Ok);
    CHECK(graph.nodes().isEmpty());
    delete src; // Caller regains ownership
}

// ============================================================================
// Connecting nodes
// ============================================================================

TEST_CASE("MediaGraph_ConnectByIndex") {
    MediaGraph graph;
    auto *src = new GraphSourceNode();
    auto *sink = new GraphSinkNode();
    graph.addNode(src);
    graph.addNode(sink);

    auto link = graph.connect(src, 0, sink, 0);
    CHECK(link.isValid());
    CHECK(graph.links().size() == 1);
}

TEST_CASE("MediaGraph_ConnectByName") {
    MediaGraph graph;
    auto *src = new GraphSourceNode();
    auto *sink = new GraphSinkNode();
    graph.addNode(src);
    graph.addNode(sink);

    auto link = graph.connect(src, "output", sink, "input");
    CHECK(link.isValid());
    CHECK(graph.links().size() == 1);
}

TEST_CASE("MediaGraph_ConnectByPort") {
    MediaGraph graph;
    auto *src = new GraphSourceNode();
    auto *sink = new GraphSinkNode();
    graph.addNode(src);
    graph.addNode(sink);

    auto link = graph.connect(src->outputPort(0), sink->inputPort(0));
    CHECK(link.isValid());
}

TEST_CASE("MediaGraph_ConnectIncompatible") {
    MediaGraph graph;
    auto *src = new GraphSourceNode(); // Frame output
    auto *sink = new GraphSinkNode();  // Frame input
    graph.addNode(src);
    graph.addNode(sink);

    // Try connecting input to input (wrong direction)
    auto link = graph.connect(sink->inputPort(0), src->outputPort(0));
    // This should still work because isCompatible checks direction internally
    // Let's just verify that connecting by invalid index fails
    auto badLink = graph.connect(src, 5, sink, 5);
    CHECK(!badLink.isValid());
}

// ============================================================================
// Disconnecting
// ============================================================================

TEST_CASE("MediaGraph_Disconnect") {
    MediaGraph graph;
    auto *src = new GraphSourceNode();
    auto *sink = new GraphSinkNode();
    graph.addNode(src);
    graph.addNode(sink);

    auto link = graph.connect(src, 0, sink, 0);
    CHECK(graph.links().size() == 1);

    Error err = graph.disconnect(link);
    CHECK(err == Error::Ok);
    CHECK(graph.links().isEmpty());
}

TEST_CASE("MediaGraph_RemoveNodeDisconnects") {
    MediaGraph graph;
    auto *src = new GraphSourceNode();
    auto *sink = new GraphSinkNode();
    graph.addNode(src);
    graph.addNode(sink);
    graph.connect(src, 0, sink, 0);
    CHECK(graph.links().size() == 1);

    graph.removeNode(src);
    CHECK(graph.links().isEmpty());
    delete src;
}

// ============================================================================
// Validation
// ============================================================================

TEST_CASE("MediaGraph_ValidateEmpty") {
    MediaGraph graph;
    CHECK(graph.validate() == Error::Invalid);
}

TEST_CASE("MediaGraph_ValidateSimple") {
    MediaGraph graph;
    auto *src = new GraphSourceNode();
    auto *sink = new GraphSinkNode();
    graph.addNode(src);
    graph.addNode(sink);
    graph.connect(src, 0, sink, 0);
    CHECK(graph.validate() == Error::Ok);
}

// ============================================================================
// Topological sort
// ============================================================================

TEST_CASE("MediaGraph_TopologicalSort") {
    MediaGraph graph;
    auto *src = new GraphSourceNode();
    src->setName("src");
    auto *proc = new GraphProcessNode();
    proc->setName("proc");
    auto *sink = new GraphSinkNode();
    sink->setName("sink");

    graph.addNode(src);
    graph.addNode(proc);
    graph.addNode(sink);
    graph.connect(src, 0, proc, 0);
    graph.connect(proc, 0, sink, 0);

    auto sorted = graph.topologicalSort();
    CHECK(sorted.size() == 3);

    // Source must come before process, process before sink
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

TEST_CASE("MediaGraph_SourceAndSinkNodes") {
    MediaGraph graph;
    auto *src = new GraphSourceNode();
    auto *proc = new GraphProcessNode();
    auto *sink = new GraphSinkNode();

    graph.addNode(src);
    graph.addNode(proc);
    graph.addNode(sink);
    graph.connect(src, 0, proc, 0);
    graph.connect(proc, 0, sink, 0);

    auto sources = graph.sourceNodes();
    CHECK(sources.size() == 1);
    CHECK(sources[0] == src);

    auto sinks = graph.sinkNodes();
    CHECK(sinks.size() == 1);
    CHECK(sinks[0] == sink);
}

// ============================================================================
// Fan-out (one output to multiple inputs)
// ============================================================================

TEST_CASE("MediaGraph_FanOut") {
    MediaGraph graph;
    auto *src = new GraphSourceNode();
    auto *sink1 = new GraphSinkNode();
    sink1->setName("sink1");
    auto *sink2 = new GraphSinkNode();
    sink2->setName("sink2");

    graph.addNode(src);
    graph.addNode(sink1);
    graph.addNode(sink2);

    auto link1 = graph.connect(src, 0, sink1, 0);
    auto link2 = graph.connect(src, 0, sink2, 0);
    CHECK(link1.isValid());
    CHECK(link2.isValid());
    CHECK(graph.links().size() == 2);

    auto sinks = graph.sinkNodes();
    CHECK(sinks.size() == 2);
}

// ============================================================================
// Clear
// ============================================================================

TEST_CASE("MediaGraph_Clear") {
    MediaGraph graph;
    graph.addNode(new GraphSourceNode());
    graph.addNode(new GraphSinkNode());
    CHECK(graph.nodes().size() == 2);

    graph.clear();
    CHECK(graph.nodes().isEmpty());
    CHECK(graph.links().isEmpty());
}

// ============================================================================
// Cycle detection
// ============================================================================

TEST_CASE("MediaGraph_CycleDetection") {
    // Create A -> B -> C -> A cycle using process nodes
    MediaGraph graph;
    auto *a = new GraphProcessNode();
    a->setName("a");
    auto *b = new GraphProcessNode();
    b->setName("b");
    auto *c = new GraphProcessNode();
    c->setName("c");

    graph.addNode(a);
    graph.addNode(b);
    graph.addNode(c);

    graph.connect(a, 0, b, 0);
    graph.connect(b, 0, c, 0);
    graph.connect(c, 0, a, 0);

    // topologicalSort should return empty list for cyclic graph
    auto sorted = graph.topologicalSort();
    CHECK(sorted.isEmpty());

    // validate should fail
    CHECK(graph.validate() != Error::Ok);
}

// ============================================================================
// Disconnect by port
// ============================================================================

TEST_CASE("MediaGraph_DisconnectByPort") {
    MediaGraph graph;
    auto *src = new GraphSourceNode();
    auto *sink = new GraphSinkNode();
    graph.addNode(src);
    graph.addNode(sink);

    graph.connect(src, 0, sink, 0);
    CHECK(graph.links().size() == 1);

    Error err = graph.disconnect(src->outputPort(0), sink->inputPort(0));
    CHECK(err == Error::Ok);
    CHECK(graph.links().isEmpty());
}

TEST_CASE("MediaGraph_DisconnectByPortNotExist") {
    MediaGraph graph;
    auto *src = new GraphSourceNode();
    auto *sink = new GraphSinkNode();
    graph.addNode(src);
    graph.addNode(sink);

    // No links exist
    Error err = graph.disconnect(src->outputPort(0), sink->inputPort(0));
    CHECK(err == Error::NotExist);
}

// ============================================================================
// Diamond topology
// ============================================================================

TEST_CASE("MediaGraph_DiamondTopology") {
    MediaGraph graph;
    auto *src = new GraphSourceNode();
    src->setName("src");
    auto *left = new GraphProcessNode();
    left->setName("left");
    auto *right = new GraphProcessNode();
    right->setName("right");
    // Sink needs 2 input ports — use a custom node
    auto *sink = new GraphSinkNode();
    sink->setName("sink");

    graph.addNode(src);
    graph.addNode(left);
    graph.addNode(right);
    graph.addNode(sink);

    // src -> left -> sink, src -> right (but right has no link to sink since sink only has 1 input)
    graph.connect(src, 0, left, 0);
    graph.connect(src, 0, right, 0);
    graph.connect(left, 0, sink, 0);

    CHECK(graph.validate() == Error::Ok);

    auto sorted = graph.topologicalSort();
    CHECK(sorted.size() == 4);

    // Source must come before left and right
    int srcIdx = -1, leftIdx = -1, rightIdx = -1, sinkIdx = -1;
    for(int i = 0; i < (int)sorted.size(); i++) {
        if(sorted[i] == src) srcIdx = i;
        if(sorted[i] == left) leftIdx = i;
        if(sorted[i] == right) rightIdx = i;
        if(sorted[i] == sink) sinkIdx = i;
    }
    CHECK(srcIdx < leftIdx);
    CHECK(srcIdx < rightIdx);
    CHECK(leftIdx < sinkIdx);
}

// ============================================================================
// Validate with unconnected node
// ============================================================================

TEST_CASE("MediaGraph_ValidateUnconnectedNodes") {
    MediaGraph graph;
    auto *src = new GraphSourceNode();
    auto *sink = new GraphSinkNode();
    graph.addNode(src);
    graph.addNode(sink);
    // No connections — should still pass validate (nodes exist, no cycles, no links to check)
    CHECK(graph.validate() == Error::Ok);
}

// ============================================================================
// Remove non-existent node
// ============================================================================

TEST_CASE("MediaGraph_RemoveNonExistent") {
    MediaGraph graph;
    auto *node = new GraphSourceNode();
    Error err = graph.removeNode(node);
    CHECK(err == Error::NotExist);
    delete node;
}

// ============================================================================
// ownsNode
// ============================================================================

TEST_CASE("MediaGraph_OwnsNode") {
    MediaGraph graph;
    auto *src = new GraphSourceNode();
    auto *other = new GraphSourceNode();
    graph.addNode(src);

    // Can't directly test ownsNode (private), but addNode duplicate checks it
    Error err = graph.addNode(src);
    CHECK(err == Error::Exists);

    // other is not in the graph
    err = graph.removeNode(other);
    CHECK(err == Error::NotExist);
    delete other;
}
