/**
 * @file      proav/medianode.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <functional>
#include <mutex>
#include <promeki/core/namespace.h>
#include <promeki/core/objectbase.h>
#include <promeki/core/string.h>
#include <promeki/core/error.h>
#include <promeki/core/list.h>
#include <promeki/core/map.h>
#include <promeki/core/variant.h>
#include <promeki/core/queue.h>
#include <promeki/core/timestamp.h>
#include <promeki/proav/mediaport.h>
#include <promeki/proav/medialink.h>
#include <promeki/proav/frame.h>

PROMEKI_NAMESPACE_BEGIN

class ThreadPool;
class MediaNode;
class MediaPipeline;

/**
 * @brief Severity level for node messages.
 * @ingroup proav_pipeline
 */
enum class Severity {
        Info,           ///< @brief Informational message.
        Warning,        ///< @brief Warning — non-fatal issue.
        Error,          ///< @brief Error — node transitions to ErrorState.
        Fatal           ///< @brief Fatal — pipeline should stop.
};

/**
 * @brief Structured message emitted by a pipeline node.
 * @ingroup proav_pipeline
 */
struct NodeMessage {
        Severity severity = Severity::Info;     ///< @brief Message severity level.
        String message;                         ///< @brief Human-readable message text.
        uint64_t frameNumber = 0;               ///< @brief Frame number this message relates to (0 if not frame-specific).
        TimeStamp timestamp;                    ///< @brief When the message was created.
        MediaNode *node = nullptr;              ///< @brief The node that emitted this message.
};

/**
 * @brief Snapshot of node performance statistics.
 * @ingroup proav_pipeline
 */
struct NodeStats {
        uint64_t processCount = 0;              ///< @brief Total process() invocations.
        uint64_t starvationCount = 0;           ///< @brief Total starvation() invocations.
        double lastProcessDuration = 0.0;       ///< @brief Wall-clock time of last process() call in seconds.
        double avgProcessDuration = 0.0;        ///< @brief Exponential moving average of process() duration in seconds.
        double peakProcessDuration = 0.0;       ///< @brief Peak process() duration in seconds.
        int currentQueueDepth = 0;              ///< @brief Current input queue depth.
        int peakQueueDepth = 0;                 ///< @brief Peak input queue depth observed.
};

/**
 * @brief Base class for all pipeline processing nodes.
 * @ingroup proav_pipeline
 *
 * MediaNode is the ObjectBase-derived base class for all processing nodes
 * in the media pipeline framework. It manages input and output ports,
 * lifecycle state transitions, threading policy, and provides a uniform
 * property interface for future JSON serialization.
 *
 * Concrete node subclasses override the virtual lifecycle methods
 * (configure, start, stop, process, starvation) to implement their
 * specific processing logic.
 */
class MediaNode : public ObjectBase {
        PROMEKI_OBJECT(MediaNode, ObjectBase)
        public:
                /** @brief Node lifecycle state. */
                enum State {
                        Idle,           ///< @brief Initial state, not configured.
                        Configured,     ///< @brief Configured and ready to start.
                        Running,        ///< @brief Actively processing data.
                        ErrorState      ///< @brief An error has occurred.
                };

                /** @brief Threading policy for this node. */
                enum ThreadingPolicy {
                        UseGraphPool,   ///< @brief Use the graph's default thread pool.
                        DedicatedThread,///< @brief Run on a dedicated thread.
                        CustomPool      ///< @brief Use a custom thread pool.
                };

                /**
                 * @brief Constructs a MediaNode.
                 * @param parent Optional parent object.
                 */
                MediaNode(ObjectBase *parent = nullptr);

                /** @brief Virtual destructor. */
                virtual ~MediaNode();

                /** @brief Returns the current state. */
                State state() const { return _state; }

                /** @brief Returns the node name. */
                const String &name() const { return _name; }

                /**
                 * @brief Sets the node name.
                 * @param name The human-readable node name.
                 */
                void setName(const String &name) { _name = name; return; }

                // ---- Port management ----

                /** @brief Returns the list of input ports. */
                const MediaPort::PtrList &inputPorts() const { return _inputPorts; }

                /** @brief Returns the list of output ports. */
                const MediaPort::PtrList &outputPorts() const { return _outputPorts; }

                /**
                 * @brief Returns the input port at the given index.
                 * @param index Zero-based port index.
                 * @return The port, or a null Ptr if the index is out of range.
                 */
                MediaPort::Ptr inputPort(int index) const;

                /**
                 * @brief Returns the output port at the given index.
                 * @param index Zero-based port index.
                 * @return The port, or a null Ptr if the index is out of range.
                 */
                MediaPort::Ptr outputPort(int index) const;

                /**
                 * @brief Returns the input port with the given name.
                 * @param name The port name.
                 * @return The port, or a null Ptr if not found.
                 */
                MediaPort::Ptr inputPort(const String &name) const;

                /**
                 * @brief Returns the output port with the given name.
                 * @param name The port name.
                 * @return The port, or a null Ptr if not found.
                 */
                MediaPort::Ptr outputPort(const String &name) const;

                /** @brief Returns the number of input ports. */
                int inputPortCount() const { return _inputPorts.size(); }

                /** @brief Returns the number of output ports. */
                int outputPortCount() const { return _outputPorts.size(); }

                // ---- Threading ----

                /**
                 * @brief Sets the threading policy.
                 * @param policy The threading policy to use.
                 */
                void setThreadingPolicy(ThreadingPolicy policy) {
                        _threadingPolicy = policy;
                        return;
                }

                /**
                 * @brief Sets a custom thread pool and switches policy to CustomPool.
                 * @param pool The custom thread pool.
                 */
                void setThreadingPolicy(ThreadPool *pool) {
                        _threadingPolicy = CustomPool;
                        _customPool = pool;
                        return;
                }

                /** @brief Returns the current threading policy. */
                ThreadingPolicy threadingPolicy() const { return _threadingPolicy; }

                /** @brief Returns the custom thread pool, or nullptr if not using CustomPool. */
                ThreadPool *customThreadPool() const { return _customPool; }

                // ---- Input queue ----

                /**
                 * @brief Sets the ideal input queue depth.
                 *
                 * The pipeline uses this as a hint for back-pressure.
                 *
                 * @param size Target queue depth (default: 2).
                 */
                void setIdealQueueSize(int size) { _idealQueueSize = size; return; }

                /** @brief Returns the ideal input queue size. */
                int idealQueueSize() const { return _idealQueueSize; }

                /** @brief Returns the current input queue depth. */
                int queuedFrameCount() const { return _inputQueue.size(); }

                // ---- Virtual lifecycle ----

                /**
                 * @brief Validates ports and allocates resources.
                 *
                 * Transitions the node from Idle to Configured on success.
                 *
                 * @return Error::Ok on success, or an error code.
                 */
                virtual Error configure();

                /**
                 * @brief Begins processing.
                 *
                 * Transitions the node from Configured to Running on success.
                 *
                 * @return Error::Ok on success, or an error code.
                 */
                virtual Error start();

                /**
                 * @brief Stops processing.
                 *
                 * Transitions the node from Running to Idle.
                 */
                virtual void stop();

                /**
                 * @brief Processes one cycle of data.
                 *
                 * Pure virtual — must be implemented by concrete node subclasses.
                 */
                virtual void process() = 0;

                /**
                 * @brief Called when the node's input queue is empty and data is needed.
                 *
                 * Override for nodes that need to handle starvation (e.g. log,
                 * insert silence/black, repeat last frame). Default: no-op.
                 */
                virtual void starvation();

                // ---- Property interface ----

                /**
                 * @brief Returns all configurable properties as key-value pairs.
                 *
                 * Concrete nodes override this to expose their configuration.
                 *
                 * @return A map of property names to Variant values.
                 */
                virtual Map<String, Variant> properties() const;

                /**
                 * @brief Sets a property by name.
                 * @param name  The property name.
                 * @param value The value to set.
                 * @return Error::Ok on success, or an error (e.g. unknown property, type mismatch).
                 */
                virtual Error setProperty(const String &name, const Variant &value);

                /**
                 * @brief Gets a single property value by name.
                 * @param name The property name.
                 * @return The property value, or an invalid Variant if not found.
                 */
                Variant property(const String &name) const;

                // ---- Node type registry ----

                /**
                 * @brief Registers a node type for runtime creation.
                 * @param typeName The unique type name string.
                 * @param factory  A factory function that creates a new instance.
                 */
                static void registerNodeType(const String &typeName, std::function<MediaNode *()> factory);

                /**
                 * @brief Creates a node by registered type name.
                 * @param typeName The type name to instantiate.
                 * @return A new node instance, or nullptr if the type is not registered.
                 */
                static MediaNode *createNode(const String &typeName);

                /**
                 * @brief Returns the list of all registered node type names.
                 * @return A list of type name strings.
                 */
                static List<String> registeredNodeTypes();

                // ---- Statistics ----

                /**
                 * @brief Returns a snapshot of the node's performance statistics.
                 * @return The current NodeStats.
                 */
                NodeStats stats() const;

                /** @brief Resets all statistics counters to zero. */
                void resetStats();

                /**
                 * @brief Returns additional node-specific statistics.
                 *
                 * Override in concrete nodes to expose custom statistics
                 * (e.g., "packetsSent", "bytesSent" for RTP nodes).
                 *
                 * @return A map of stat names to Variant values.
                 */
                virtual Map<String, Variant> extendedStats() const;

                // ---- Signals ----

                /** @brief Emitted when the node's state changes. */
                PROMEKI_SIGNAL(stateChanged, State);

                /** @brief Emitted when an error occurs. */
                PROMEKI_SIGNAL(errorOccurred, Error);

                /** @brief Emitted when the node produces a message. */
                PROMEKI_SIGNAL(messageEmitted, NodeMessage);

        protected:
                /**
                 * @brief Enqueues a frame into this node's input queue.
                 *
                 * Called by the pipeline to deliver frames from upstream nodes.
                 *
                 * @param frame The frame to enqueue.
                 */
                void enqueueInput(Frame::Ptr frame);

                /**
                 * @brief Records timing for a process() call.
                 *
                 * Called by the pipeline to instrument process() with timing.
                 * Updates processCount, lastProcessDuration, avgProcessDuration,
                 * and peakProcessDuration.
                 *
                 * @param duration The wall-clock duration of the process() call in seconds.
                 */
                void recordProcessTiming(double duration);

                /**
                 * @brief Records a starvation event.
                 *
                 * Called by the pipeline when starvation() is invoked.
                 * Increments starvationCount.
                 */
                void recordStarvation();

                /**
                 * @brief Adds an input port to this node.
                 * @param port The port to add.
                 */
                void addInputPort(MediaPort::Ptr port);

                /**
                 * @brief Adds an output port to this node.
                 * @param port The port to add.
                 */
                void addOutputPort(MediaPort::Ptr port);

                /**
                 * @brief Sets the node state and emits stateChanged.
                 * @param state The new state.
                 */
                void setState(State state);

                /**
                 * @brief Dequeues a frame from this node's input queue.
                 *
                 * Returns a null Ptr if the queue is empty. Subclasses call
                 * this from process() to pull input frames.
                 *
                 * @return The next frame, or a null Ptr if the queue is empty.
                 */
                Frame::Ptr dequeueInput();

                /**
                 * @brief Delivers a frame to all outgoing links on the given output port.
                 *
                 * Source nodes call this from process() to push frames downstream.
                 *
                 * @param portIndex The output port index.
                 * @param frame The frame to deliver.
                 */
                void deliverOutput(int portIndex, Frame::Ptr frame);

                /**
                 * @brief Delivers a frame to all outgoing links on all output ports.
                 *
                 * Convenience for single-output nodes.
                 *
                 * @param frame The frame to deliver.
                 */
                void deliverOutput(Frame::Ptr frame);

                /**
                 * @brief Emits a message with the given severity and text.
                 *
                 * Auto-populates timestamp and node pointer. Subclasses
                 * call this to report events during processing.
                 *
                 * @param severity The message severity.
                 * @param message The message text.
                 * @param frameNumber The frame number this relates to (0 if not frame-specific).
                 */
                void emitMessage(Severity severity, const String &message, uint64_t frameNumber = 0);

                /**
                 * @brief Emits a Warning-severity message.
                 * @param message The warning text.
                 */
                void emitWarning(const String &message);

                /**
                 * @brief Emits an Error-severity message.
                 *
                 * Also transitions the node to ErrorState and emits errorOccurred.
                 *
                 * @param message The error text.
                 */
                void emitError(const String &message);

        private:
                String                  _name;
                State                   _state = Idle;
                ThreadingPolicy         _threadingPolicy = UseGraphPool;
                ThreadPool              *_customPool = nullptr;
                int                     _idealQueueSize = 2;
                MediaPort::PtrList      _inputPorts;
                MediaPort::PtrList      _outputPorts;
                Queue<Frame::Ptr>       _inputQueue;

                // Outgoing links (managed by MediaGraph)
                MediaLink::PtrList      _outgoingLinks;
                friend class MediaGraph;
                friend class MediaLink;
                friend class MediaPipeline;

                // Statistics (guarded by _statsMutex)
                mutable std::mutex      _statsMutex;
                uint64_t                _processCount = 0;
                uint64_t                _starvationCount = 0;
                double                  _lastProcessDuration = 0.0;
                double                  _avgProcessDuration = 0.0;
                double                  _peakProcessDuration = 0.0;
                int                     _peakQueueDepth = 0;

                static Map<String, std::function<MediaNode *()>> &nodeRegistry();
};

/**
 * @brief Macro to register a MediaNode subclass for runtime creation.
 *
 * Place this in the .cpp file of each concrete MediaNode subclass.
 * It registers the class at static initialization time so that
 * MediaNode::createNode() can instantiate it by name.
 */
#define PROMEKI_REGISTER_NODE(ClassName) \
        static struct ClassName##Registrar { \
                ClassName##Registrar() { \
                        MediaNode::registerNodeType(#ClassName, []() -> MediaNode * { return new ClassName(); }); \
                } \
        } __##ClassName##Registrar;

PROMEKI_NAMESPACE_END
