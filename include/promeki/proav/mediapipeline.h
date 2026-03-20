/**
 * @file      proav/mediapipeline.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <atomic>
#include <promeki/core/namespace.h>
#include <promeki/core/objectbase.h>
#include <promeki/core/error.h>
#include <promeki/core/list.h>
#include <promeki/core/string.h>
#include <promeki/core/stringlist.h>
#include <promeki/proav/medianode.h>
#include <promeki/proav/mediapipelineconfig.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Owns nodes and topology, orchestrates pipeline processing.
 * @ingroup proav_pipeline
 *
 * MediaPipeline manages the topology and execution of a media processing
 * pipeline. Nodes are added, connected via their sources and sinks,
 * validated for cycles and correctness, then started.
 *
 * Connections are made by wiring a node's MediaSource to another node's
 * MediaSink. The pipeline provides convenience connect() methods that
 * look up the source/sink by index or name.
 */
class MediaPipeline : public ObjectBase {
        PROMEKI_OBJECT(MediaPipeline, ObjectBase)
        public:
                /** @brief Pipeline lifecycle state. */
                enum State {
                        Stopped,        ///< @brief Pipeline is not running.
                        Starting,       ///< @brief Pipeline is starting up (configuring nodes).
                        Running,        ///< @brief Pipeline is actively processing.
                        Paused,         ///< @brief Pipeline is paused.
                        Stopping,       ///< @brief Pipeline is shutting down.
                        ErrorState      ///< @brief An error has occurred.
                };

                /**
                 * @brief Collects all errors from a build() call.
                 * @ingroup proav_pipeline
                 *
                 * Each entry records the node that failed, the error code,
                 * and a human-readable message.
                 */
                struct BuildError {
                        /** @brief A single error entry. */
                        struct Entry {
                                String nodeName;        ///< @brief Node name this error relates to (empty for pipeline-level errors).
                                Error error;            ///< @brief The error code.
                                String message;         ///< @brief Human-readable description.
                        };

                        using EntryList = List<Entry>;

                        EntryList entries;              ///< @brief All collected error entries.

                        /** @brief Returns true if there are no errors. */
                        bool isOk() const { return entries.isEmpty(); }

                        /** @brief Returns true if there are errors. */
                        bool isError() const { return !entries.isEmpty(); }

                        /**
                         * @brief Adds an error entry.
                         * @param nodeName The node name (empty for pipeline-level errors).
                         * @param error    The error code.
                         * @param message  Human-readable description.
                         */
                        void add(const String &nodeName, Error error, const String &message);

                        /** @brief Returns all error messages as a StringList. */
                        StringList messages() const;
                };

                /**
                 * @brief Constructs a MediaPipeline.
                 * @param parent Optional parent object.
                 */
                MediaPipeline(ObjectBase *parent = nullptr);

                /** @brief Destructor. Stops the pipeline if running, deletes owned nodes. */
                virtual ~MediaPipeline();

                // ---- Build from config ----

                /**
                 * @brief Builds the pipeline from a configuration.
                 *
                 * Creates all nodes via the factory, wires connections, and
                 * calls MediaNode::build() on each node.  The pipeline must
                 * be in the Stopped state.  The configuration list need not
                 * be in any particular order; dependency order is determined
                 * automatically from the connection graph.
                 *
                 * On error, any partially-created nodes are cleaned up and
                 * the pipeline remains in the Stopped state.  All errors are
                 * collected into the returned BuildError (not just the first).
                 *
                 * @param config The pipeline configuration.
                 * @return A BuildError collecting all problems found.
                 */
                BuildError build(const MediaPipelineConfig &config);

                // ---- Node management ----

                /**
                 * @brief Adds a node to the pipeline. Takes ownership.
                 * @param node The node to add.
                 * @return Error::Ok on success.
                 */
                Error addNode(MediaNode *node);

                /**
                 * @brief Removes a node from the pipeline.
                 *
                 * Disconnects all connections involving this node first.
                 * The caller regains ownership and must delete the node.
                 *
                 * @param node The node to remove.
                 * @return Error::Ok on success, Error::NotExist if not found.
                 */
                Error removeNode(MediaNode *node);

                /** @brief Returns all nodes in the pipeline. */
                const List<MediaNode *> &nodes() const { return _nodes; }

                /**
                 * @brief Finds a node by name.
                 * @param name The node name.
                 * @return The node, or nullptr if not found.
                 */
                MediaNode *node(const String &name) const;

                // ---- Connection management ----

                /**
                 * @brief Connects a source to a sink.
                 * @param source The output source.
                 * @param sink   The input sink.
                 * @return Error::Ok on success, Error::Invalid if either is null.
                 */
                Error connect(MediaSource::Ptr source, MediaSink::Ptr sink);

                /**
                 * @brief Connects nodes by source/sink index.
                 * @param sourceNode   The source node.
                 * @param sourceIndex  Output source index on the source node.
                 * @param sinkNode     The sink node.
                 * @param sinkIndex    Input sink index on the sink node.
                 * @return Error::Ok on success, Error::Invalid on bad indices.
                 */
                Error connect(MediaNode *sourceNode, int sourceIndex,
                              MediaNode *sinkNode, int sinkIndex);

                /**
                 * @brief Connects nodes by source/sink name.
                 * @param sourceNode   The source node.
                 * @param sourceName   Output source name on the source node.
                 * @param sinkNode     The sink node.
                 * @param sinkName     Input sink name on the sink node.
                 * @return Error::Ok on success, Error::Invalid if names not found.
                 */
                Error connect(MediaNode *sourceNode, const String &sourceName,
                              MediaNode *sinkNode, const String &sinkName);

                /**
                 * @brief Disconnects a source from a sink.
                 * @param source The output source.
                 * @param sink   The input sink.
                 * @return Error::Ok on success.
                 */
                Error disconnect(MediaSource::Ptr source, MediaSink::Ptr sink);

                // ---- Topology queries ----

                /**
                 * @brief Validates the pipeline topology.
                 *
                 * Checks for cycles (DAG requirement) and that the pipeline
                 * is non-empty.
                 *
                 * @return Error::Ok if valid, or an error describing the problem.
                 */
                Error validate() const;

                /**
                 * @brief Returns nodes in topological order (dependencies first).
                 * @return An ordered list of node pointers, or empty if the pipeline has cycles.
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
                 * @brief Removes all nodes and links. Deletes all owned nodes.
                 */
                void clear();

                // ---- Lifecycle ----

                /**
                 * @brief Validates the pipeline, configures all nodes, and starts processing.
                 * @return Error::Ok on success, or an error describing the failure.
                 */
                Error start();

                /**
                 * @brief Stops all nodes.
                 * @return Error::Ok on success.
                 */
                Error stop();

                /**
                 * @brief Pauses processing.
                 * @return Error::Ok on success.
                 */
                Error pause();

                /**
                 * @brief Resumes processing from a paused state.
                 * @return Error::Ok on success.
                 */
                Error resume();

                /** @brief Returns the current pipeline state. */
                State state() const { return _state; }

                /** @brief Emitted when the pipeline state changes. */
                PROMEKI_SIGNAL(stateChanged, State);

                /** @brief Emitted when an error occurs in the pipeline. */
                PROMEKI_SIGNAL(errorOccurred, Error);

                /** @brief Emitted when the pipeline has started. */
                PROMEKI_SIGNAL(started);

                /** @brief Emitted when the pipeline has stopped. */
                PROMEKI_SIGNAL(stopped);

        private:
                List<MediaNode *>       _nodes;
                std::atomic<State>      _state{Stopped};
                std::atomic<bool>       _running{false};
                std::atomic<bool>       _paused{false};

                void setState(State state);
                bool ownsNode(MediaNode *node) const;
                bool hasCycle() const;

                /** @brief Returns downstream nodes reachable from a given node. */
                List<MediaNode *> downstreamNodes(MediaNode *node) const;
};

PROMEKI_NAMESPACE_END
