/**
 * @file      mediapipeline.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/mediapipeline.h>
#include <promeki/medianodeconfig.h>
#include <promeki/benchmarkreporter.h>

PROMEKI_NAMESPACE_BEGIN

// ---- BuildError ----

void MediaPipeline::BuildError::add(const String &nodeName, Error error, const String &message) {
        entries.pushToBack({nodeName, error, message});
        return;
}

StringList MediaPipeline::BuildError::messages() const {
        StringList ret;
        for(const auto &e : entries) {
                ret.pushToBack(e.message);
        }
        return ret;
}

// ---- Construction ----

MediaPipeline::MediaPipeline(ObjectBase *parent) : ObjectBase(parent) {
}

MediaPipeline::~MediaPipeline() {
        if(_state == Running || _state == Paused) {
                stop();
        }
        clear();
}

Error MediaPipeline::addNode(MediaNode *node) {
        if(node == nullptr) return Error(Error::Invalid);
        if(ownsNode(node)) return Error(Error::Exists);
        node->_pipeline = this;
        _nodes.pushToBack(node);
        return Error(Error::Ok);
}

Error MediaPipeline::removeNode(MediaNode *node) {
        if(!ownsNode(node)) return Error(Error::NotExist);

        // Disconnect all connections involving this node
        for(const auto &src : node->sources()) {
                src->disconnectAll();
        }
        for(const auto &snk : node->sinks()) {
                // Remove back-pointers from sources connected to this sink
                // by iterating all other nodes' sources
                for(auto *other : _nodes) {
                        if(other == node) continue;
                        for(const auto &otherSrc : other->sources()) {
                                otherSrc->disconnect(snk);
                        }
                }
        }

        _nodes.removeFirst(node);
        node->_pipeline = nullptr;
        return Error(Error::Ok);
}

MediaNode *MediaPipeline::node(const String &name) const {
        for(auto *n : _nodes) {
                if(n->name() == name) return n;
        }
        return nullptr;
}

Error MediaPipeline::connect(MediaSource::Ptr source, MediaSink::Ptr sink) {
        if(!source.isValid() || !sink.isValid()) return Error(Error::Invalid);
        source->connect(sink);
        return Error(Error::Ok);
}

Error MediaPipeline::connect(MediaNode *sourceNode, int sourceIndex,
                             MediaNode *sinkNode, int sinkIndex) {
        if(sourceNode == nullptr || sinkNode == nullptr) return Error(Error::Invalid);
        auto src = sourceNode->source(sourceIndex);
        auto snk = sinkNode->sink(sinkIndex);
        if(!src.isValid() || !snk.isValid()) return Error(Error::Invalid);
        return connect(src, snk);
}

Error MediaPipeline::connect(MediaNode *sourceNode, const String &sourceName,
                             MediaNode *sinkNode, const String &sinkName) {
        if(sourceNode == nullptr || sinkNode == nullptr) return Error(Error::Invalid);
        auto src = sourceNode->source(sourceName);
        auto snk = sinkNode->sink(sinkName);
        if(!src.isValid() || !snk.isValid()) return Error(Error::Invalid);
        return connect(src, snk);
}

Error MediaPipeline::disconnect(MediaSource::Ptr source, MediaSink::Ptr sink) {
        if(!source.isValid() || !sink.isValid()) return Error(Error::Invalid);
        source->disconnect(sink);
        return Error(Error::Ok);
}

Error MediaPipeline::validate() const {
        if(_nodes.isEmpty()) return Error(Error::Invalid);
        if(hasCycle()) return Error(Error::Invalid);
        return Error(Error::Ok);
}

List<MediaNode *> MediaPipeline::topologicalSort() const {
        // Kahn's algorithm -- derive edges from source→sink connections
        Map<MediaNode *, int> inDegree;
        for(auto *node : _nodes) {
                inDegree.insert(node, 0);
        }

        // Count in-degrees
        for(auto *node : _nodes) {
                for(const auto &src : node->sources()) {
                        for(const auto &snk : src->connectedSinks()) {
                                MediaNode *downstream = snk->node();
                                if(downstream != nullptr && inDegree.contains(downstream)) {
                                        inDegree[downstream]++;
                                }
                        }
                }
        }

        // Start with nodes that have no incoming edges
        List<MediaNode *> queue;
        for(auto *node : _nodes) {
                if(inDegree[node] == 0) {
                        queue.pushToBack(node);
                }
        }

        List<MediaNode *> result;
        size_t idx = 0;
        while(idx < queue.size()) {
                MediaNode *current = queue[idx++];
                result.pushToBack(current);

                for(const auto &src : current->sources()) {
                        for(const auto &snk : src->connectedSinks()) {
                                MediaNode *downstream = snk->node();
                                if(downstream != nullptr) {
                                        inDegree[downstream]--;
                                        if(inDegree[downstream] == 0) {
                                                queue.pushToBack(downstream);
                                        }
                                }
                        }
                }
        }

        if(result.size() != _nodes.size()) return List<MediaNode *>();
        return result;
}

List<MediaNode *> MediaPipeline::sourceNodes() const {
        List<MediaNode *> result;
        for(auto *node : _nodes) {
                bool hasIncoming = false;
                // Check if any other node's source connects to one of this node's sinks
                for(auto *other : _nodes) {
                        if(other == node) continue;
                        for(const auto &src : other->sources()) {
                                for(const auto &snk : src->connectedSinks()) {
                                        if(snk->node() == node) {
                                                hasIncoming = true;
                                                break;
                                        }
                                }
                                if(hasIncoming) break;
                        }
                        if(hasIncoming) break;
                }
                if(!hasIncoming) result.pushToBack(node);
        }
        return result;
}

List<MediaNode *> MediaPipeline::sinkNodes() const {
        List<MediaNode *> result;
        for(auto *node : _nodes) {
                bool hasOutgoing = false;
                for(const auto &src : node->sources()) {
                        if(src->isConnected()) {
                                hasOutgoing = true;
                                break;
                        }
                }
                if(!hasOutgoing) result.pushToBack(node);
        }
        return result;
}

void MediaPipeline::clear() {
        // Disconnect all connections first
        for(auto *node : _nodes) {
                for(const auto &src : node->sources()) {
                        src->disconnectAll();
                }
        }
        for(auto *node : _nodes) {
                node->_pipeline = nullptr;
                delete node;
        }
        _nodes.clear();
        return;
}

List<MediaNode *> MediaPipeline::downstreamNodes(MediaNode *node) const {
        List<MediaNode *> result;
        for(const auto &src : node->sources()) {
                for(const auto &snk : src->connectedSinks()) {
                        MediaNode *downstream = snk->node();
                        if(downstream != nullptr && !result.contains(downstream)) {
                                result.pushToBack(downstream);
                        }
                }
        }
        return result;
}

bool MediaPipeline::ownsNode(MediaNode *node) const {
        return _nodes.contains(node);
}

bool MediaPipeline::hasCycle() const {
        auto sorted = topologicalSort();
        return sorted.size() != _nodes.size();
}

// ---- Build from config ----

MediaPipeline::BuildError MediaPipeline::build(const MediaPipelineConfig &config) {
        BuildError result;

        // Must be stopped
        if(_state != Stopped) {
                result.add(String(), Error(Error::Invalid), "Pipeline must be stopped to build");
                return result;
        }

        // Clear any existing nodes
        clear();

        // Phase 1: Create all nodes via factory
        Map<String, MediaNode *> nodeMap;
        for(const auto &cfg : config.nodes()) {
                if(!cfg.isValid()) {
                        result.add(cfg.name(), Error(Error::Invalid),
                                "Node config is invalid (missing name or type)");
                        continue;
                }
                if(nodeMap.contains(cfg.name())) {
                        result.add(cfg.name(), Error(Error::Exists),
                                "Duplicate node name '" + cfg.name() + "'");
                        continue;
                }
                MediaNode *n = MediaNode::createNode(cfg.type());
                if(n == nullptr) {
                        result.add(cfg.name(), Error(Error::NotExist),
                                "Unknown node type '" + cfg.type() + "'");
                        continue;
                }
                n->setName(cfg.name());
                nodeMap.insert(cfg.name(), n);
                addNode(n);
        }

        // If any creation errors, clean up and return
        if(result.isError()) {
                clear();
                return result;
        }

        // Phase 2: Wire connections
        for(const auto &cfg : config.nodes()) {
                StringList conns = cfg.connections();
                MediaNode *sinkNode = nodeMap[cfg.name()];
                for(int i = 0; i < (int)conns.size(); i++) {
                        const String &connStr = conns[i];
                        if(connStr.isEmpty()) continue;

                        Error parseErr;
                        MediaNodeConfig::ParsedConnection pc =
                                MediaNodeConfig::parseConnection(connStr, &parseErr);
                        if(parseErr.isError() || !pc.isValid()) {
                                result.add(cfg.name(), Error(Error::Invalid),
                                        "Invalid connection string '" + connStr +
                                        "' on sink index " + String::number(i));
                                continue;
                        }

                        if(!nodeMap.contains(pc.nodeName)) {
                                result.add(cfg.name(), Error(Error::NotExist),
                                        "Connection references unknown node '" + pc.nodeName +
                                        "' on sink index " + String::number(i));
                                continue;
                        }

                        MediaNode *sourceNode = nodeMap[pc.nodeName];
                        MediaSource::Ptr src;
                        if(pc.sourceIndex >= 0) {
                                src = sourceNode->source(pc.sourceIndex);
                                if(!src.isValid()) {
                                        result.add(cfg.name(), Error(Error::OutOfRange),
                                                "Source index " + String::number(pc.sourceIndex) +
                                                " out of range on node '" + pc.nodeName + "'");
                                        continue;
                                }
                        } else {
                                src = sourceNode->source(pc.sourceName);
                                if(!src.isValid()) {
                                        result.add(cfg.name(), Error(Error::NotExist),
                                                "Source '" + pc.sourceName +
                                                "' not found on node '" + pc.nodeName + "'");
                                        continue;
                                }
                        }

                        MediaSink::Ptr snk = sinkNode->sink(i);
                        if(!snk.isValid()) {
                                result.add(cfg.name(), Error(Error::OutOfRange),
                                        "Sink index " + String::number(i) +
                                        " out of range on node '" + cfg.name() + "'");
                                continue;
                        }

                        Error connErr = connect(src, snk);
                        if(connErr.isError()) {
                                result.add(cfg.name(), connErr,
                                        "Failed to connect '" + connStr +
                                        "' to sink index " + String::number(i));
                        }
                }
        }

        // If any connection errors, clean up and return
        if(result.isError()) {
                clear();
                return result;
        }

        // Phase 3: Call build() on each node (validates config and transitions to Configured)
        for(const auto &cfg : config.nodes()) {
                MediaNode *n = nodeMap[cfg.name()];
                BuildResult buildResult = n->build(cfg);
                if(buildResult.isError()) {
                        for(const auto &entry : buildResult.entries) {
                                if(entry.severity == Severity::Error || entry.severity == Severity::Fatal) {
                                        result.add(cfg.name(), Error(Error::Invalid), entry.message);
                                }
                        }
                }
                // Forward warnings as info-level pipeline entries
                for(const auto &entry : buildResult.entries) {
                        if(entry.severity == Severity::Warning) {
                                result.add(cfg.name(), Error(Error::Ok), entry.message);
                        }
                }
        }

        // If any build errors, clean up and return
        if(result.isError()) {
                clear();
                return result;
        }

        // Phase 4: Initialize benchmarking
        for(const auto &cfg : config.nodes()) {
                if(cfg.benchmarkEnabled()) {
                        MediaNode *n = nodeMap[cfg.name()];
                        n->_benchmarkEnabled = true;
                        n->_benchmarkReporter = new BenchmarkReporter();
                        n->initBenchmarkIds();
                }
        }

        return result;
}

// ---- Lifecycle ----

Error MediaPipeline::start() {
        if(_state != Stopped) return Error(Error::Invalid);
        setState(Starting);

        Error err = validate();
        if(err != Error::Ok) {
                setState(ErrorState);
                errorOccurredSignal.emit(err);
                return err;
        }

        List<MediaNode *> order = topologicalSort();
        if(order.isEmpty()) {
                setState(ErrorState);
                Error cycleErr(Error::Invalid);
                errorOccurredSignal.emit(cycleErr);
                return cycleErr;
        }

        // Start all nodes in topological order
        // Nodes must be in Configured state (set by build())
        for(auto *node : order) {
                err = node->start();
                if(err != Error::Ok) {
                        for(auto *n : order) {
                                if(n == node) break;
                                n->stop();
                        }
                        setState(ErrorState);
                        errorOccurredSignal.emit(err);
                        return err;
                }
        }

        _running = true;
        _paused = false;
        setState(Running);
        startedSignal.emit();
        return Error(Error::Ok);
}

Error MediaPipeline::stop() {
        if(_state != Running && _state != Paused) return Error(Error::Invalid);
        setState(Stopping);

        _running = false;
        _paused = false;

        // Stop all nodes in reverse topological order
        List<MediaNode *> order = topologicalSort();
        for(int i = (int)order.size() - 1; i >= 0; i--) {
                order[i]->stop();
        }

        setState(Stopped);
        stoppedSignal.emit();
        return Error(Error::Ok);
}

Error MediaPipeline::pause() {
        if(_state != Running) return Error(Error::Invalid);
        _paused = true;
        setState(Paused);
        return Error(Error::Ok);
}

Error MediaPipeline::resume() {
        if(_state != Paused) return Error(Error::Invalid);
        _paused = false;
        setState(Running);
        return Error(Error::Ok);
}

String MediaPipeline::benchmarkSummary() const {
        String result;
        for(auto *n : _nodes) {
                BenchmarkReporter *reporter = n->benchmarkReporter();
                if(reporter == nullptr) continue;
                if(!result.isEmpty()) result += "\n";
                result += reporter->summaryReport();
        }
        if(result.isEmpty()) return String("No benchmark data collected");
        return result;
}

void MediaPipeline::setState(State state) {
        if(_state == state) return;
        _state = state;
        stateChangedSignal.emit(state);
        return;
}

PROMEKI_NAMESPACE_END
