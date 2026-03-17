/**
 * @file      proav/mediagraph.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/core/namespace.h>
#include <promeki/core/objectbase.h>
#include <promeki/core/error.h>
#include <promeki/core/list.h>
#include <promeki/core/string.h>
#include <promeki/proav/medianode.h>
#include <promeki/proav/medialink.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Directed acyclic graph of MediaNodes connected by MediaLinks.
 * @ingroup proav_pipeline
 *
 * MediaGraph manages the topology of a media processing pipeline. Nodes
 * are added to the graph, then connected via their ports. The graph
 * validates the topology (no cycles, compatible formats, all required
 * ports connected) and provides topological ordering for processing.
 *
 * An output port may be connected to multiple input ports (fan-out).
 * The same frame is delivered to all connected links.
 */
class MediaGraph : public ObjectBase {
        PROMEKI_OBJECT(MediaGraph, ObjectBase)
        public:
                /**
                 * @brief Constructs an empty graph.
                 * @param parent Optional parent object.
                 */
                MediaGraph(ObjectBase *parent = nullptr);

                /** @brief Destructor. Deletes all owned nodes. */
                virtual ~MediaGraph();

                /**
                 * @brief Adds a node to the graph. Takes ownership.
                 * @param node The node to add.
                 * @return Error::Ok on success.
                 */
                Error addNode(MediaNode *node);

                /**
                 * @brief Removes a node from the graph.
                 *
                 * Disconnects all links involving this node first.
                 * The caller regains ownership and must delete the node.
                 *
                 * @param node The node to remove.
                 * @return Error::Ok on success, Error::NotExist if not found.
                 */
                Error removeNode(MediaNode *node);

                /** @brief Returns all nodes in the graph. */
                const List<MediaNode *> &nodes() const { return _nodes; }

                /**
                 * @brief Finds a node by name.
                 * @param name The node name.
                 * @return The node, or nullptr if not found.
                 */
                MediaNode *node(const String &name) const;

                /**
                 * @brief Connects an output port to an input port.
                 * @param output The source output port.
                 * @param input  The sink input port.
                 * @return The created link, or a null Ptr on error.
                 */
                MediaLink::Ptr connect(MediaPort::Ptr output, MediaPort::Ptr input);

                /**
                 * @brief Connects nodes by port index.
                 * @param source      The source node.
                 * @param outputIndex Output port index on the source node.
                 * @param sink        The sink node.
                 * @param inputIndex  Input port index on the sink node.
                 * @return The created link, or a null Ptr on error.
                 */
                MediaLink::Ptr connect(MediaNode *source, int outputIndex, MediaNode *sink, int inputIndex);

                /**
                 * @brief Connects nodes by port name.
                 * @param source     The source node.
                 * @param outputName Output port name on the source node.
                 * @param sink       The sink node.
                 * @param inputName  Input port name on the sink node.
                 * @return The created link, or a null Ptr on error.
                 */
                MediaLink::Ptr connect(MediaNode *source, const String &outputName,
                                       MediaNode *sink, const String &inputName);

                /**
                 * @brief Disconnects a link.
                 * @param link The link to disconnect.
                 * @return Error::Ok on success.
                 */
                Error disconnect(MediaLink::Ptr link);

                /**
                 * @brief Disconnects the link between two specific ports.
                 * @param output The output port.
                 * @param input  The input port.
                 * @return Error::Ok on success, Error::NotExist if not found.
                 */
                Error disconnect(MediaPort::Ptr output, MediaPort::Ptr input);

                /** @brief Returns all links in the graph. */
                const MediaLink::PtrList &links() const { return _links; }

                /**
                 * @brief Validates the graph topology.
                 *
                 * Checks for cycles (DAG requirement), format compatibility
                 * on all links, and that the graph is non-empty.
                 *
                 * @return Error::Ok if valid, or an error describing the problem.
                 */
                Error validate() const;

                /**
                 * @brief Returns nodes in topological order (dependencies first).
                 *
                 * Source nodes appear first, sink nodes last.
                 *
                 * @return An ordered list of node pointers, or empty if the graph has cycles.
                 */
                List<MediaNode *> topologicalSort() const;

                /**
                 * @brief Returns nodes with no input connections (source nodes).
                 * @return A list of source nodes.
                 */
                List<MediaNode *> sourceNodes() const;

                /**
                 * @brief Returns nodes with no output connections (sink nodes).
                 * @return A list of sink nodes.
                 */
                List<MediaNode *> sinkNodes() const;

                /**
                 * @brief Removes all nodes and links.
                 *
                 * Deletes all owned nodes.
                 */
                void clear();

        private:
                List<MediaNode *>       _nodes;
                MediaLink::PtrList      _links;

                bool ownsNode(MediaNode *node) const;
                bool hasCycle() const;
};

PROMEKI_NAMESPACE_END
