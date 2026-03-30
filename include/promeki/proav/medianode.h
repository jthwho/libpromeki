/**
 * @file      proav/medianode.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <promeki/core/namespace.h>
#include <promeki/core/objectbase.h>
#include <promeki/core/string.h>
#include <promeki/core/error.h>
#include <promeki/core/list.h>
#include <promeki/core/map.h>
#include <promeki/core/variant.h>
#include <promeki/core/mutex.h>
#include <promeki/core/waitcondition.h>
#include <promeki/core/thread.h>
#include <promeki/core/timestamp.h>
#include <promeki/core/benchmark.h>
#include <promeki/proav/mediasink.h>
#include <promeki/proav/mediasource.h>
#include <promeki/proav/frame.h>

PROMEKI_NAMESPACE_BEGIN

class MediaNode;
class MediaNodeConfig;
class MediaPipeline;
class BenchmarkReporter;

/**
 * @brief Severity level for node messages.
 * @ingroup proav_pipeline
 */
enum class Severity {
        Info,           ///< @brief Informational message.
        Warning,        ///< @brief Warning -- non-fatal issue.
        Error,          ///< @brief Error -- node transitions to ErrorState.
        Fatal           ///< @brief Fatal -- pipeline should stop.
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
        double lastProcessDuration = 0.0;       ///< @brief Wall-clock time of last process() call in seconds.
        double avgProcessDuration = 0.0;        ///< @brief Exponential moving average of process() duration in seconds.
        double peakProcessDuration = 0.0;       ///< @brief Peak process() duration in seconds.
        int currentQueueDepth = 0;              ///< @brief Aggregate input queue depth across all sinks.
        int peakQueueDepth = 0;                 ///< @brief Peak aggregate input queue depth observed.
};

/**
 * @brief Collects diagnostic messages from a node's build() call.
 * @ingroup proav_pipeline
 *
 * BuildResult can contain informational, warning, and error entries.
 * A build succeeds if there are no Error or Fatal entries.
 */
struct BuildResult {
        /** @brief A single diagnostic entry. */
        struct Entry {
                Severity severity;      ///< @brief Entry severity.
                String message;         ///< @brief Human-readable description.
        };

        using EntryList = List<Entry>;

        EntryList entries;              ///< @brief All collected entries.

        /** @brief Returns true if there are no error-level entries. */
        bool isOk() const;

        /** @brief Returns true if there is at least one error-level entry. */
        bool isError() const;

        /** @brief Adds an informational entry. */
        void addInfo(const String &msg);

        /** @brief Adds a warning entry. */
        void addWarning(const String &msg);

        /** @brief Adds an error entry. */
        void addError(const String &msg);
};

/**
 * @brief Specifies which output source should receive a frame.
 * @ingroup proav_pipeline
 *
 * Used by processFrame() to direct frames to specific outputs.
 * A sourceIndex of -1 means deliver to all outputs.
 */
struct Delivery {
        int        sourceIndex = -1;    ///< @brief Output source index, or -1 for all.
        Frame::Ptr frame;               ///< @brief The frame to deliver.
};

/** @brief List of Delivery items. */
using DeliveryList = List<Delivery>;

/**
 * @brief Base class for all pipeline processing nodes.
 * @ingroup proav_pipeline
 *
 * MediaNode is the ObjectBase-derived base class for all processing nodes
 * in the media pipeline framework. It manages input sinks and output
 * sources, lifecycle state transitions, and per-sink queued input with
 * backpressure.
 *
 * Concrete node subclasses override processFrame() to implement their
 * specific processing logic. The base class handles dequeuing input,
 * benchmark stamping, delivering output, and recording process timing.
 *
 * The MediaPipeline is the sole interface for managing node lifecycle.
 * Nodes cannot be configured, started, or stopped directly — only
 * through the pipeline.
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

                // ---- Sink / Source management ----

                /** @brief Returns the list of input sinks. */
                const MediaSink::PtrList &sinks() const { return _sinks; }

                /** @brief Returns the list of output sources. */
                const MediaSource::PtrList &sources() const { return _sources; }

                /**
                 * @brief Returns the input sink at the given index.
                 * @param index Zero-based index.
                 * @return The sink, or a null Ptr if the index is out of range.
                 */
                MediaSink::Ptr sink(int index) const;

                /**
                 * @brief Returns the output source at the given index.
                 * @param index Zero-based index.
                 * @return The source, or a null Ptr if the index is out of range.
                 */
                MediaSource::Ptr source(int index) const;

                /**
                 * @brief Returns the input sink with the given name.
                 * @param name The sink name.
                 * @return The sink, or a null Ptr if not found.
                 */
                MediaSink::Ptr sink(const String &name) const;

                /**
                 * @brief Returns the output source with the given name.
                 * @param name The source name.
                 * @return The source, or a null Ptr if not found.
                 */
                MediaSource::Ptr source(const String &name) const;

                /** @brief Returns the number of input sinks. */
                int sinkCount() const { return _sinks.size(); }

                /** @brief Returns the number of output sources. */
                int sourceCount() const { return _sources.size(); }

                // ---- Threading ----

                /**
                 * @brief Returns the Thread owned by this node, or nullptr.
                 */
                Thread *thread() const { return _thread; }

                /**
                 * @brief Wakes the node so it re-evaluates work availability.
                 *
                 * Called by MediaSink::push() when a frame arrives, and by
                 * sinks when backpressure is relieved.
                 */
                void wake();

                // ---- Work availability and backpressure ----

                /** @brief Returns true if any input sink queue has data. */
                bool hasInput() const;

                /**
                 * @brief Returns true if all output sources can deliver.
                 *
                 * Checks sinksReadyForFrame() on every source.
                 */
                bool canOutput() const;

                /**
                 * @brief Returns true if the output source at the given index can deliver.
                 * @param sourceIndex Output source index.
                 */
                bool canOutput(int sourceIndex) const;

                /**
                 * @brief Returns true if the node has work to do.
                 *
                 * For nodes with sinks: hasInput() && canOutput().
                 * For source nodes (no sinks): canOutput().
                 */
                bool hasWork() const;

                /** @brief Returns the pipeline this node belongs to, or nullptr. */
                MediaPipeline *pipeline() const { return _pipeline; }

                // ---- Build from config ----

                /**
                 * @brief Configures this node from a MediaNodeConfig.
                 *
                 * Called by MediaPipeline::build() after creating the node
                 * via the factory and wiring connections. The node reads
                 * its configuration from the config, validates it, allocates
                 * resources, and transitions to the Configured state on
                 * success.
                 *
                 * @param config The node configuration.
                 * @return A BuildResult collecting any diagnostics.
                 */
                virtual BuildResult build(const MediaNodeConfig &config) = 0;

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

                // ---- Benchmark ----

                /** @brief Returns true if benchmarking is enabled for this node. */
                bool benchmarkEnabled() const { return _benchmarkEnabled; }

                /**
                 * @brief Returns the benchmark reporter for this node, if any.
                 * @return The reporter pointer (may be null if benchmarking is disabled).
                 */
                BenchmarkReporter *benchmarkReporter() const { return _benchmarkReporter; }

                // ---- Signals ----

                /** @brief Emitted when the node's state changes. */
                PROMEKI_SIGNAL(stateChanged, State);

                /** @brief Emitted when an error occurs. */
                PROMEKI_SIGNAL(errorOccurred, Error);

                /** @brief Emitted when the node produces a message. */
                PROMEKI_SIGNAL(messageEmitted, NodeMessage);

        protected:
                // ---- Virtual lifecycle ----

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
                 * Transitions the node to Idle, wakes any threads
                 * blocked in waitForWork() so they see Error::Stopped,
                 * then calls cleanup().
                 */
                virtual void stop();

                /**
                 * @brief Called during stop() to allow cleanup while the object
                 *        is still fully constructed.
                 *
                 * Override to release resources, disconnect signals, or
                 * notify external systems. Called after the node transitions
                 * to Idle and waiters are woken. Default implementation is
                 * a no-op.
                 */
                virtual void cleanup();

                /**
                 * @brief Processes a single frame of data.
                 *
                 * Called by the base class process() after dequeuing input
                 * and stamping benchmarks.  Concrete nodes implement their
                 * processing logic here.
                 *
                 * For nodes with inputs: @p frame contains the dequeued data
                 * and @p inputIndex identifies which sink it came from.
                 *
                 * For source nodes (no sinks): @p frame is null and
                 * @p inputIndex is -1.  The node creates and populates the frame.
                 *
                 * Output delivery is controlled via @p deliveries:
                 * - If @p deliveries is left empty and @p frame is valid after
                 *   return, the base class delivers @p frame to all outputs.
                 * - If @p deliveries is populated, each entry is delivered to
                 *   its specified sourceIndex (or all outputs if sourceIndex is -1).
                 * - If both are empty/invalid, no delivery occurs (terminal sinks).
                 *
                 * @param frame      The input frame (or null for source nodes).
                 * @param inputIndex The sink index the frame came from, or -1 for source nodes.
                 * @param deliveries Output delivery list; populate for multi-output routing.
                 */
                virtual void processFrame(Frame::Ptr &frame, int inputIndex, DeliveryList &deliveries) = 0;

                /**
                 * @brief Runs one processing cycle.
                 *
                 * Dequeues input (if any sinks), calls processFrame(),
                 * stamps benchmarks, delivers output, and records timing.
                 * Called by the node's thread in the default run loop.
                 * Subclasses may expose this for synchronous testing.
                 */
                void process();

                /**
                 * @brief Returns the pre-created benchmark for the current frame.
                 *
                 * Valid only during a processFrame() call when benchmarking is
                 * enabled.  Source nodes should attach this to their created frame.
                 *
                 * @return The current frame's benchmark, or null if benchmarking is disabled.
                 */
                Benchmark::Ptr currentBenchmark() const { return _currentBenchmark; }

                /**
                 * @brief Sets the thread for this node. Takes ownership.
                 *
                 * The node deletes the thread on destruction or when replaced.
                 * Pass nullptr to clear.
                 *
                 * @param thread The thread to own.
                 */
                void setThread(Thread *thread);

                // ---- Data flow ----

                /**
                 * @brief Dequeues a frame from the input sink at the given index.
                 * @param sinkIndex The input sink index.
                 * @return The next frame, or a null Ptr if the queue is empty.
                 */
                Frame::Ptr dequeueInput(int sinkIndex);

                /**
                 * @brief Dequeues a frame from the named input sink.
                 * @param sinkName The input sink name.
                 * @return The next frame, or a null Ptr if the queue is empty.
                 */
                Frame::Ptr dequeueInput(const String &sinkName);

                /**
                 * @brief Dequeues a frame from the first input sink that has data.
                 * @return The next frame, or a null Ptr if all queues are empty.
                 */
                Frame::Ptr dequeueInput();

                /**
                 * @brief Records timing for a process() call.
                 * @param duration The wall-clock duration of the process() call in seconds.
                 */
                void recordProcessTiming(double duration);

                /**
                 * @brief Adds an input sink to this node.
                 * @param sink The sink to add. Ownership is shared via Ptr.
                 */
                void addSink(MediaSink::Ptr sink);

                /**
                 * @brief Adds an output source to this node.
                 * @param source The source to add. Ownership is shared via Ptr.
                 */
                void addSource(MediaSource::Ptr source);

                /**
                 * @brief Sets the node state and emits stateChanged.
                 * @param state The new state.
                 */
                void setState(State state);

                /**
                 * @brief Delivers a frame via the output source at the given index.
                 * @param sourceIndex The output source index.
                 * @param frame The frame to deliver.
                 */
                void deliverOutput(int sourceIndex, Frame::Ptr frame);

                /**
                 * @brief Delivers a frame via all output sources.
                 *
                 * Convenience for single-output nodes.
                 *
                 * @param frame The frame to deliver.
                 */
                void deliverOutput(Frame::Ptr frame);

                /**
                 * @brief Emits a message with the given severity and text.
                 * @param severity The message severity.
                 * @param message The message text.
                 * @param frameNumber The frame number this relates to (0 if not frame-specific).
                 */
                void emitMessage(Severity severity, const String &message, uint64_t frameNumber = 0);

                /** @brief Emits a Warning-severity message. */
                void emitWarning(const String &message);

                /**
                 * @brief Emits an Error-severity message.
                 *
                 * Also transitions the node to ErrorState and emits errorOccurred.
                 */
                void emitError(const String &message);

                /**
                 * @brief Blocks until the node has work to do, the node
                 *        leaves Running state, or the timeout expires.
                 *
                 * For nodes with sinks: waits until input is available and
                 * outputs can accept. For source nodes: waits until outputs
                 * can accept (backpressure relief).
                 *
                 * @param timeoutMs Maximum time to wait in milliseconds.
                 *        Zero (the default) waits indefinitely.
                 * @return Error::Ok if work is available,
                 *         Error::Stopped if the node is no longer Running,
                 *         Error::Timeout on timeout.
                 */
                Error waitForWork(unsigned int timeoutMs = 0);

        private:
                String                  _name;
                State                   _state = Idle;
                MediaSink::PtrList      _sinks;
                MediaSource::PtrList    _sources;

                // Wake mechanism
                Mutex                   _workMutex;
                WaitCondition           _workCv;
                Thread                  *_thread = nullptr;

                MediaPipeline           *_pipeline = nullptr;
                friend class MediaPipeline;
                friend class MediaNodeThread;

                // Statistics (guarded by _statsMutex)
                mutable std::mutex      _statsMutex;
                uint64_t                _processCount = 0;
                double                  _lastProcessDuration = 0.0;
                double                  _avgProcessDuration = 0.0;
                double                  _peakProcessDuration = 0.0;
                int                     _peakQueueDepth = 0;

                // Benchmark
                bool                    _benchmarkEnabled = false;
                BenchmarkReporter       *_benchmarkReporter = nullptr;
                Benchmark::Ptr          _currentBenchmark;
                Benchmark::Id           _bmQueued;
                Benchmark::Id           _bmBeginProcess;
                Benchmark::Id           _bmEndProcess;

                void initBenchmarkIds();
                int aggregateQueueDepth() const;
                void threadEntry();

                static Map<String, std::function<MediaNode *()>> &nodeRegistry();
};

/**
 * @brief Macro to register a MediaNode subclass for runtime creation.
 *
 * Place this in the .cpp file of each concrete MediaNode subclass.
 */
#define PROMEKI_REGISTER_NODE(ClassName) \
        static struct ClassName##Registrar { \
                ClassName##Registrar() { \
                        MediaNode::registerNodeType(#ClassName, []() -> MediaNode * { return new ClassName(); }); \
                } \
        } __##ClassName##Registrar;

PROMEKI_NAMESPACE_END
