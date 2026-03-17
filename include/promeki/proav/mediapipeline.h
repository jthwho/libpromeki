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
#include <promeki/core/threadpool.h>
#include <promeki/proav/mediagraph.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Owns a MediaGraph and orchestrates pipeline processing.
 * @ingroup proav_pipeline
 *
 * MediaPipeline manages the execution of a media processing graph.
 * It validates the graph, configures all nodes, and schedules processing
 * using a thread pool. The processing model is back-pressure driven:
 * source nodes run continuously gated by downstream queue depth, and
 * only timing-aware sink nodes manage real-time pacing.
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
                 * @brief Constructs a MediaPipeline.
                 * @param parent Optional parent object.
                 */
                MediaPipeline(ObjectBase *parent = nullptr);

                /** @brief Destructor. Stops the pipeline if running. */
                virtual ~MediaPipeline();

                /** @brief Returns the managed graph. */
                MediaGraph *graph() { return &_graph; }

                /** @brief Returns the managed graph (const). */
                const MediaGraph *graph() const { return &_graph; }

                /** @brief Returns the thread pool used for processing. */
                ThreadPool *threadPool() { return _threadPool; }

                /**
                 * @brief Sets an external thread pool to use instead of the internal one.
                 * @param pool The thread pool. Ownership is not transferred.
                 */
                void setThreadPool(ThreadPool *pool) { _threadPool = pool; return; }

                /**
                 * @brief Validates the graph, configures all nodes, and starts processing.
                 * @return Error::Ok on success, or an error describing the failure.
                 */
                Error start();

                /**
                 * @brief Stops all nodes and waits for processing to complete.
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
                MediaGraph              _graph;
                ThreadPool              *_threadPool = nullptr;
                ThreadPool              *_ownedPool = nullptr;
                std::atomic<State>      _state{Stopped};
                std::atomic<bool>       _running{false};
                std::atomic<bool>       _paused{false};

                void setState(State state);
};

PROMEKI_NAMESPACE_END
