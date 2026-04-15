/**
 * @file      mediapipeline.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/atomic.h>
#include <promeki/elapsedtimer.h>
#include <promeki/error.h>
#include <promeki/frame.h>
#include <promeki/list.h>
#include <promeki/map.h>
#include <promeki/mediaio.h>
#include <promeki/mediapipelineconfig.h>
#include <promeki/mediapipelinestats.h>
#include <promeki/namespace.h>
#include <promeki/objectbase.h>
#include <promeki/string.h>
#include <promeki/stringlist.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Data-driven builder and runtime for a DAG of @ref MediaIO stages.
 * @ingroup pipeline
 *
 * @ref MediaPipeline consumes a @ref MediaPipelineConfig, instantiates
 * every stage as a @ref MediaIO (either by registered backend name or
 * by filesystem path), connects @c frameReady / @c frameWanted /
 * @c writeError signals according to the declared routes, and
 * drives frames from source stages to sink stages signal-driven on
 * the owner's @ref EventLoop — no dedicated pumper thread.
 *
 * Lifecycle:
 *   - @ref build — validates the config, creates every @ref MediaIO,
 *     resolves file paths via @ref MediaIO::createForFileRead /
 *     @ref MediaIO::createForFileWrite.
 *   - @ref open — opens each stage (topologically ordered so sinks are
 *     ready when sources emit their first frame).
 *   - @ref start — wires signals and primes the drain so the first
 *     source read kicks off the flow.
 *   - @ref stop — cancels pending commands, disconnects signals.
 *   - @ref close — closes each stage in reverse topological order and
 *     releases the underlying MediaIO instances.
 *
 * Fan-out is supported: a single source may appear as the @c from of
 * multiple routes, and each destination back-pressures independently
 * via @ref MediaIO::writesAccepted and @c frameWanted.  Fan-in on a
 * single stage is not supported in this implementation (the first
 * stage that would receive frames from more than one producer is
 * rejected at @ref build time).
 */
class MediaPipeline : public ObjectBase {
        PROMEKI_OBJECT(MediaPipeline, ObjectBase)
        public:
                /**
                 * @brief Lifecycle state of the pipeline.
                 *
                 * Each lifecycle call (@ref build, @ref open, @ref start,
                 * @ref stop, @ref close) advances this state deterministically
                 * so that integration tests and UI bindings can tell where
                 * they are without racing against the strand workers.
                 */
                enum class State {
                        Empty,   ///< @brief Constructed, no config applied.
                        Built,   ///< @brief MediaIO stages created but not open.
                        Open,    ///< @brief Every stage opened.
                        Running, ///< @brief Signals connected and draining.
                        Stopped, ///< @brief Drain halted; stages still open.
                        Closed   ///< @brief Stages closed and released.
                };

                /**
                 * @brief Constructs an empty pipeline.
                 * @param parent Optional ObjectBase parent.
                 */
                explicit MediaPipeline(ObjectBase *parent = nullptr);

                /** @brief Destroys the pipeline, closing it if still open. */
                ~MediaPipeline() override;

                // ------------------------------------------------------------
                // Lifecycle
                // ------------------------------------------------------------

                /**
                 * @brief Validates @p config and instantiates every MediaIO.
                 *
                 * Pre-condition: the pipeline is @ref State::Empty or
                 * @ref State::Closed.
                 *
                 * @param config The declarative pipeline description.
                 * @return @c Error::Ok, or the validation error.
                 */
                Error build(const MediaPipelineConfig &config);

                /**
                 * @brief Registers an externally-constructed MediaIO for a
                 *        stage that @ref build would otherwise create.
                 *
                 * Some backends cannot be instantiated through
                 * @ref MediaIO::create because they need resources the
                 * library does not manage — the SDL player, for example,
                 * needs live @c SDLVideoWidget / @c SDLAudioOutput
                 * pointers.  Callers pre-build those instances on the
                 * main thread and register them via @c injectStage before
                 * calling @ref build.  Injected stages are @em not
                 * deleted by the pipeline in @ref close — ownership
                 * stays with the caller.
                 *
                 * Must be called while the pipeline is @ref State::Empty
                 * or @ref State::Closed.  When @ref build sees the
                 * stage's name in the injection map it skips
                 * construction and uses the registered pointer instead.
                 *
                 * @param name The stage name declared in the config.
                 * @param io   The external MediaIO (must outlive the pipeline).
                 * @return @c Error::Ok or @c Error::Busy when the
                 *         pipeline is not in a settable state.
                 */
                Error injectStage(const String &name, MediaIO *io);

                /**
                 * @brief Opens every stage.
                 *
                 * Pre-condition: the pipeline is @ref State::Built.  On any
                 * failure previously-opened stages are closed before the
                 * error is returned.
                 */
                Error open();

                /**
                 * @brief Wires signals and primes the drain.
                 *
                 * Pre-condition: the pipeline is @ref State::Open.
                 */
                Error start();

                /**
                 * @brief Cancels pending commands and halts the drain.
                 *
                 * Pre-condition: the pipeline is @ref State::Running.
                 *
                 * @note @c stop is a one-shot transition into
                 *       @ref State::Stopped — the terminal pre-close
                 *       state.  The pipeline cannot be re-started
                 *       in place: signal connections were established
                 *       against the live @ref MediaIO instances at
                 *       @ref start time, and there is no mechanism
                 *       today to disconnect and re-connect them.  To
                 *       run again, tear the pipeline down with
                 *       @ref close and rebuild via
                 *       @ref build / @ref open / @ref start.
                 */
                Error stop();

                /**
                 * @brief Closes every stage and releases the MediaIO instances.
                 *
                 * Safe to call in any state; subsequent @ref build calls
                 * recreate the stages.
                 */
                Error close();

                /** @brief Returns the current lifecycle state. */
                State state() const { return _state; }

                // ------------------------------------------------------------
                // Introspection
                // ------------------------------------------------------------

                /** @brief Returns the config the pipeline was built from. */
                const MediaPipelineConfig &config() const { return _config; }

                /**
                 * @brief Returns the live @ref MediaIO for @p name.
                 * @return Pointer (owned by the pipeline), or nullptr.
                 */
                MediaIO *stage(const String &name) const;

                /** @brief Returns every stage name in topological order. */
                StringList stageNames() const;

                /**
                 * @brief Produces a multi-line human-readable summary.
                 *
                 * Extends @ref MediaPipelineConfig::describe with the live
                 * @ref MediaIO::mediaDesc strings where available (after
                 * @ref open has populated the backend cache).
                 */
                StringList describe() const;

                /**
                 * @brief Collects a stats snapshot from every live stage.
                 *
                 * Queries each stage via @ref MediaIO::stats and rolls the
                 * result up into the snapshot's aggregate.
                 */
                MediaPipelineStats stats();

                // ------------------------------------------------------------
                // Signals
                // ------------------------------------------------------------

                /**
                 * @brief Emitted when any stage reports an error.
                 *
                 * @p stageName identifies the offending stage; @p err
                 * carries the error code.  The pipeline always follows an
                 * error with a @ref finishedSignal emission with
                 * @c clean=false.
                 * @signal
                 */
                PROMEKI_SIGNAL(pipelineError, String, Error);

                /** @brief Emitted after each stage's @c open() completes. @signal */
                PROMEKI_SIGNAL(stageOpened,  String);

                /** @brief Emitted after each stage is wired into the drain. @signal */
                PROMEKI_SIGNAL(stageStarted, String);

                /** @brief Emitted after each stage is unwired from the drain. @signal */
                PROMEKI_SIGNAL(stageStopped, String);

                /** @brief Emitted after each stage's @c close() completes. @signal */
                PROMEKI_SIGNAL(stageClosed,  String);

                /**
                 * @brief Emitted once the pipeline drains to completion.
                 *
                 * @p clean is @c true for natural EOF / stop-on-complete,
                 * @c false if the pipeline tore down because of an error.
                 * @signal
                 */
                PROMEKI_SIGNAL(finished, bool);

        private:
                // One record per outgoing route.  Back-pressure is
                // driven entirely by @ref MediaIO::writesAccepted
                // checked before each pull from the source — under
                // the documented single-threaded driving contract,
                // a non-blocking @ref MediaIO::writeFrame can never
                // refuse a frame we just confirmed there was room
                // for.  Async write failures arrive on
                // @c writeErrorSignal.
                struct EdgeState {
                        String   toName;
                        MediaIO *to = nullptr;
                };

                // Each source-or-transit stage owns one SourceState entry.
                // Stages with no outgoing routes (pure sinks) do not appear
                // here; they participate only as write-error / frameWanted
                // observers.
                struct SourceState {
                        MediaIO                 *from     = nullptr;
                        promeki::List<EdgeState> edges;
                        bool                     upstreamDone = false;
                };

                Error destroyStages();
                Error topologicallySort(promeki::List<String> &order) const;
                MediaIO *instantiateStage(const MediaPipelineConfig::Stage &s);

                void drainSource(const String &srcName);
                void onWriteError(const String &stageName, Error err);
                void checkDrained();
                void finish(bool clean);

                MediaPipelineConfig                   _config;
                State                                 _state    = State::Empty;
                promeki::Map<String, MediaIO *>       _stages;
                promeki::Map<String, MediaIO *>       _injected;
                promeki::Map<String, SourceState>     _sources;
                promeki::List<String>                 _topoOrder;
                bool                                  _finished = false;
                bool                                  _cleanFinish = false;

                // Pipeline-layer telemetry counters surfaced via
                // PipelineStats on every @ref stats() call.  Atomic
                // so stats() called from the stats-thread context
                // (see mediaplay's --stats worker) doesn't race the
                // drain that runs on the owner's EventLoop.
                Atomic<int64_t>                       _framesProduced{0};
                Atomic<int64_t>                       _writeRetries{0};
                Atomic<int64_t>                       _pipelineErrors{0};
                ElapsedTimer                          _uptime;
                bool                                  _uptimeStarted = false;
};

PROMEKI_NAMESPACE_END
