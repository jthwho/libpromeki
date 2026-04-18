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
#include <promeki/set.h>
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
                 * Uses a graceful cascade: the true source stages (no
                 * upstream in the pipeline) are closed first via
                 * @ref MediaIO::close with @c block=false.  As each
                 * source's synthetic EOS reaches @ref drainSource and
                 * latches @c upstreamDone, every direct downstream
                 * consumer of that source is closed in turn — so
                 * intermediate stages only see close() after all their
                 * input frames have been written to them.  The pipeline
                 * emits @ref closedSignal (and the repurposed
                 * @ref finishedSignal) once every stage has emitted its
                 * own @ref MediaIO::closedSignal.
                 *
                 * Safe to call in any state:
                 * - @c Empty / @c Closed: no-op.
                 * - @c Built: stages are destroyed without opening.
                 * - @c Open / @c Stopped: stages are closed in parallel
                 *   (no drain to propagate through).
                 * - @c Running: full cascade through the DAG.
                 *
                 * @param block When @c true (default), blocks until the
                 *              cascade has completed.  When @c false,
                 *              returns immediately and the caller learns
                 *              completion through @ref closedSignal.
                 * @return @c Error::Ok on success or @c Error::Ok after
                 *         successful async submit.  Errors reported by
                 *         individual stages are aggregated into the
                 *         @ref closedSignal payload rather than the
                 *         return value; the return value only reflects
                 *         up-front failures such as the pipeline already
                 *         closing.
                 */
                Error close(bool block = true);

                /**
                 * @brief Returns true while an async close is in flight.
                 *
                 * Set by @ref close(bool) on entry and cleared when
                 * every stage has emitted @ref MediaIO::closedSignal.
                 */
                bool isClosing() const { return _closing; }

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
                 * @brief Emitted once the pipeline has finished draining
                 *        AND every stage has fully closed.
                 *
                 * Fires alongside @ref closedSignal at the end of the
                 * cascade (natural EOF or explicit @ref close).  @p clean
                 * is @c true for natural EOF with no errors, @c false if
                 * an error interrupted the flow.  Sinks have flushed
                 * their in-flight writes by this point — consumers that
                 * previously relied on @c finishedSignal firing as soon
                 * as sources hit EOF now see it after sink drain too.
                 * @signal
                 */
                PROMEKI_SIGNAL(finished, bool);

                /**
                 * @brief Emitted once every stage's @ref MediaIO::closedSignal
                 *        has fired and the pipeline has transitioned to
                 *        @ref State::Closed.
                 *
                 * Fires for every completed close path — explicit
                 * @ref close, natural EOF auto-cascade, and
                 * error-triggered teardown — so consumers have a
                 * single completion signal to wait on regardless of
                 * what drove the cascade.  The payload is the first
                 * non-Ok per-stage close error, or @ref Error::Ok
                 * when every stage closed cleanly.
                 * @signal
                 */
                PROMEKI_SIGNAL(closed, Error);

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

                /**
                 * @brief Common entry point for every close trigger
                 *        (explicit @ref close, natural EOF, error).
                 *
                 * Idempotent: the first call latches @c _closing and
                 * arms the cascade; subsequent calls only downgrade
                 * the @c clean flag if @p clean is @c false.  Callers
                 * waiting for completion watch @ref state for
                 * @ref State::Closed.
                 *
                 * @param clean Seeds the @c clean flag that eventually
                 *              feeds @ref finishedSignal — caller sets
                 *              @c false when triggering from an error.
                 */
                void initiateClose(bool clean);

                /**
                 * @brief Called on every stage's @ref MediaIO::closedSignal.
                 *
                 * Removes the stage from the outstanding set, captures
                 * the first non-Ok close error, and fires the pipeline's
                 * finish + closed signals when the set empties.
                 */
                void onStageClosed(const String &stageName, Error err);

                /**
                 * @brief Finalizes the pipeline close after every stage
                 *        has reported closed.
                 *
                 * Emits @ref finishedSignal and @ref closedSignal,
                 * releases the @ref MediaIO instances, transitions to
                 * @ref State::Closed, and fulfills any outstanding
                 * close-future.
                 */
                void finalizeClose();

                MediaPipelineConfig                   _config;
                State                                 _state    = State::Empty;
                promeki::Map<String, MediaIO *>       _stages;
                promeki::Map<String, MediaIO *>       _injected;
                promeki::Map<String, SourceState>     _sources;
                promeki::List<String>                 _topoOrder;

                // Close-cascade bookkeeping.  Latched by
                // @ref initiateClose and unwound in
                // @ref finalizeClose.  @c _cleanFinish is the running
                // "no errors observed" bit that eventually feeds
                // @ref finishedSignal; drops to false on any
                // operational or close-time error.
                bool                                  _closing = false;
                bool                                  _cleanFinish = false;
                promeki::Set<String>                  _stagesAwaitingClosed;
                Error                                 _closeError = Error::Ok;

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
