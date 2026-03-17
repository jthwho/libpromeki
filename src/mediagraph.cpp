/**
 * @file      mediagraph.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/core/set.h>
#include <promeki/proav/mediagraph.h>

PROMEKI_NAMESPACE_BEGIN

MediaGraph::MediaGraph(ObjectBase *parent) : ObjectBase(parent) {
}

MediaGraph::~MediaGraph() {
        clear();
}

Error MediaGraph::addNode(MediaNode *node) {
        if(node == nullptr) return Error(Error::Invalid);
        if(ownsNode(node)) return Error(Error::Exists);
        _nodes.pushToBack(node);
        return Error(Error::Ok);
}

Error MediaGraph::removeNode(MediaNode *node) {
        if(!ownsNode(node)) return Error(Error::NotExist);

        // Disconnect all links involving this node
        MediaLink::PtrList toRemove;
        for(const auto &link : _links) {
                if(link->sourceNode() == node || link->sinkNode() == node) {
                        toRemove.pushToBack(link);
                }
        }
        for(const auto &link : toRemove) {
                disconnect(link);
        }

        _nodes.removeFirst(node);
        return Error(Error::Ok);
}

MediaNode *MediaGraph::node(const String &name) const {
        for(auto *n : _nodes) {
                if(n->name() == name) return n;
        }
        return nullptr;
}

MediaLink::Ptr MediaGraph::connect(MediaPort::Ptr output, MediaPort::Ptr input) {
        if(!output.isValid() || !input.isValid()) return MediaLink::Ptr();
        if(!output->isCompatible(*input)) return MediaLink::Ptr();

        MediaLink::Ptr link = MediaLink::Ptr::create(output, input);
        _links.pushToBack(link);

        // Register outgoing link on the source node
        MediaNode *src = output->node();
        if(src != nullptr) {
                src->_outgoingLinks.pushToBack(link);
        }

        return link;
}

MediaLink::Ptr MediaGraph::connect(MediaNode *source, int outputIndex,
                                   MediaNode *sink, int inputIndex) {
        if(source == nullptr || sink == nullptr) return MediaLink::Ptr();
        auto output = source->outputPort(outputIndex);
        auto input = sink->inputPort(inputIndex);
        return connect(output, input);
}

MediaLink::Ptr MediaGraph::connect(MediaNode *source, const String &outputName,
                                   MediaNode *sink, const String &inputName) {
        if(source == nullptr || sink == nullptr) return MediaLink::Ptr();
        auto output = source->outputPort(outputName);
        auto input = sink->inputPort(inputName);
        return connect(output, input);
}

Error MediaGraph::disconnect(MediaLink::Ptr link) {
        if(!link.isValid()) return Error(Error::Invalid);
        for(size_t i = 0; i < _links.size(); i++) {
                if(_links[i]->source()->name() == link->source()->name() &&
                   _links[i]->sink()->name() == link->sink()->name() &&
                   _links[i]->sourceNode() == link->sourceNode() &&
                   _links[i]->sinkNode() == link->sinkNode()) {
                        // Remove from source node's outgoing links
                        MediaNode *src = _links[i]->sourceNode();
                        if(src != nullptr) {
                                for(size_t j = 0; j < src->_outgoingLinks.size(); j++) {
                                        if(src->_outgoingLinks[j]->source()->name() == link->source()->name() &&
                                           src->_outgoingLinks[j]->sinkNode() == link->sinkNode()) {
                                                src->_outgoingLinks.remove(j);
                                                break;
                                        }
                                }
                        }
                        _links.remove(i);
                        return Error(Error::Ok);
                }
        }
        return Error(Error::NotExist);
}

Error MediaGraph::disconnect(MediaPort::Ptr output, MediaPort::Ptr input) {
        if(!output.isValid() || !input.isValid()) return Error(Error::Invalid);
        for(size_t i = 0; i < _links.size(); i++) {
                if(_links[i]->sourceNode() == output->node() &&
                   _links[i]->source()->name() == output->name() &&
                   _links[i]->sinkNode() == input->node() &&
                   _links[i]->sink()->name() == input->name()) {
                        // Remove from source node's outgoing links
                        MediaNode *src = output->node();
                        if(src != nullptr) {
                                for(size_t j = 0; j < src->_outgoingLinks.size(); j++) {
                                        if(src->_outgoingLinks[j]->source()->name() == output->name() &&
                                           src->_outgoingLinks[j]->sinkNode() == input->node()) {
                                                src->_outgoingLinks.remove(j);
                                                break;
                                        }
                                }
                        }
                        _links.remove(i);
                        return Error(Error::Ok);
                }
        }
        return Error(Error::NotExist);
}

Error MediaGraph::validate() const {
        if(_nodes.isEmpty()) return Error(Error::Invalid);

        // Check format compatibility on all links
        for(const auto &link : _links) {
                if(!link->isValid()) return Error(Error::Invalid);
        }

        // Check for cycles
        if(hasCycle()) return Error(Error::Invalid);

        return Error(Error::Ok);
}

List<MediaNode *> MediaGraph::topologicalSort() const {
        // Kahn's algorithm
        Map<MediaNode *, int> inDegree;
        for(auto *node : _nodes) {
                inDegree.insert(node, 0);
        }

        // Count in-degrees based on links
        for(const auto &link : _links) {
                MediaNode *sink = link->sinkNode();
                if(sink != nullptr && inDegree.contains(sink)) {
                        inDegree[sink]++;
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

                // Find all outgoing links from current node
                for(const auto &link : _links) {
                        if(link->sourceNode() == current) {
                                MediaNode *sink = link->sinkNode();
                                if(sink != nullptr) {
                                        inDegree[sink]--;
                                        if(inDegree[sink] == 0) {
                                                queue.pushToBack(sink);
                                        }
                                }
                        }
                }
        }

        // If result doesn't contain all nodes, there's a cycle
        if(result.size() != _nodes.size()) return List<MediaNode *>();
        return result;
}

List<MediaNode *> MediaGraph::sourceNodes() const {
        List<MediaNode *> result;
        for(auto *node : _nodes) {
                bool hasIncoming = false;
                for(const auto &link : _links) {
                        if(link->sinkNode() == node) {
                                hasIncoming = true;
                                break;
                        }
                }
                if(!hasIncoming) result.pushToBack(node);
        }
        return result;
}

List<MediaNode *> MediaGraph::sinkNodes() const {
        List<MediaNode *> result;
        for(auto *node : _nodes) {
                bool hasOutgoing = false;
                for(const auto &link : _links) {
                        if(link->sourceNode() == node) {
                                hasOutgoing = true;
                                break;
                        }
                }
                if(!hasOutgoing) result.pushToBack(node);
        }
        return result;
}

void MediaGraph::clear() {
        _links.clear();
        for(auto *node : _nodes) {
                delete node;
        }
        _nodes.clear();
        return;
}

bool MediaGraph::ownsNode(MediaNode *node) const {
        return _nodes.contains(node);
}

bool MediaGraph::hasCycle() const {
        // Use topological sort — if it fails to include all nodes, there's a cycle
        auto sorted = topologicalSort();
        return sorted.size() != _nodes.size();
}

PROMEKI_NAMESPACE_END
