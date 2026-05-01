/**
 * @file      mediapipeline.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <functional>
#include <promeki/duration.h>
#include <promeki/error.h>
#include <promeki/frame.h>
#include <promeki/list.h>
#include <promeki/logger.h>
#include <promeki/map.h>
#include <promeki/mediaio.h>
#include <promeki/mediaioportconnection.h>
#include <promeki/mediapipelineconfig.h>
#include <promeki/mediapipelinestats.h>
#include <promeki/mutex.h>
#include <promeki/namespace.h>
#include <promeki/objectbase.h>
#include <promeki/pipelineevent.h>
#include <promeki/set.h>
#include <promeki/string.h>
#include <promeki/stringlist.h>

PROMEKI_NAMESPACE_BEGIN

class MediaIOStatsCollector;

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
 *
 * @par Thread Safety
 * Inherits @ref ObjectBase: thread-affine.  Pipeline lifecycle
 * (@c build, @c open, @c start, @c stop, @c close) is driven from
 * the owning EventLoop's thread.  Per-stage execution happens on
 * the strand worker that the stage's MediaIO uses; the pipeline
 * coordinates them via signal-driven notifications.
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
                 * When @p autoplan is @c true the config is first
                 * passed through @ref MediaPipelinePlanner::plan so
                 * any missing bridging stages (CSC, decoder, frame
                 * sync, etc.) are spliced in automatically.  Default
                 * @c false preserves the existing strict behaviour
                 * — callers who want explicit pipelines do not pay
                 * for planner overhead.
                 *
                 * @param config   The declarative pipeline description.
                 * @param autoplan When @c true, run the planner before
                 *                 instantiation.
                 * @return @c Error::Ok, or the first error encountered
                 *         (validation, planning, or instantiation).
                 */
                Error build(const MediaPipelineConfig &config, bool autoplan = false);

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
                 * or @ref State::Closed.  When @ref build sees a stage
                 * whose name matches an injected entry it skips
                 * factory construction and uses the registered pointer
                 * instead.
                 *
                 * @par Name resolution
                 * Picks the stage name in the following order, then
                 * stamps it onto the IO via @ref MediaIO::setName so
                 * @c io->name() reports the resolved name on return:
                 *  -# If @c io->name() is non-empty and does not
                 *     collide with an already-injected stage, use it
                 *     verbatim.
                 *  -# If @c io->name() is non-empty and collides,
                 *     append an incrementing suffix (e.g. @c "sdl",
                 *     @c "sdl2", @c "sdl3", ...).
                 *  -# If @c io->name() is empty, generate a
                 *     role-based default: @c "src&lt;N&gt;",
                 *     @c "sink&lt;N&gt;", or @c "xfm&lt;N&gt;" when the
                 *     role is determinable from @ref MediaIO::isSource
                 *     / @ref MediaIO::isSink (post-open) or the
                 *     @ref MediaIO::describe probe (pre-open via the
                 *     registered factory's role flags).  Falls back to
                 *     @c "stage&lt;N&gt;" when the role is unknown.
                 *
                 * @param io The external MediaIO (must outlive the pipeline).
                 * @return @c Error::Ok or @c Error::Busy when the
                 *         pipeline is not in a settable state, or
                 *         @c Error::InvalidArgument when @p io is null.
                 */
                Error injectStage(MediaIO *io);

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
                 * @brief Default close-watchdog timeout, milliseconds.
                 *
                 * A clean close cascade relies on frames draining from
                 * each stage's input to its output.  If a stage gets
                 * stuck on an external signal that never fires — a
                 * @ref FrameBridge output waiting for a consumer that
                 * never arrives, for example — the cascade never
                 * reaches the stuck stage and the pipeline stalls
                 * indefinitely.  The watchdog escalates to a forced
                 * close on every still-open stage once this deadline
                 * expires.
                 */
                static constexpr unsigned int DefaultCloseTimeoutMs = 3000;

                /**
                 * @brief Sets the close-watchdog timeout.
                 *
                 * A value of zero disables the watchdog — callers who
                 * want an unbounded graceful close can opt out.
                 *
                 * @param ms Timeout in milliseconds.
                 */
                void setCloseTimeoutMs(unsigned int ms) { _closeTimeoutMs = ms; }

                /** @brief Returns the current close-watchdog timeout. */
                unsigned int closeTimeoutMs() const { return _closeTimeoutMs; }

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
                 * For each stage the snapshot contains:
                 *  - the cumulative aggregate returned by
                 *    @ref MediaIO::stats (instance-wide standard keys
                 *    plus any backend-specific cumulative keys); and
                 *  - the per-@ref MediaIOCommand::Kind windowed
                 *    breakdown sourced from the stage's
                 *    @ref MediaIOStatsCollector when stats collection
                 *    is enabled (see
                 *    @ref MediaPipelineConfig::statsWindowSize).
                 *
                 * Stages whose underlying @ref MediaIO is not open
                 * appear as records with a default-constructed
                 * @c cumulative and an empty @c windowed map.
                 */
                MediaPipelineStats stats();

                /**
                 * @brief Collects a stats snapshot for a single stage by name.
                 *
                 * Returns the per-stage record directly (no need to
                 * unwrap a single-entry @ref MediaPipelineStats).  An
                 * unknown @p name yields a default-constructed
                 * @ref MediaPipelineStageStats.
                 */
                MediaPipelineStageStats stageStats(const String &name);

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
                PROMEKI_SIGNAL(stageOpened, String);

                /** @brief Emitted after each stage is wired into the drain. @signal */
                PROMEKI_SIGNAL(stageStarted, String);

                /** @brief Emitted after each stage is unwired from the drain. @signal */
                PROMEKI_SIGNAL(stageStopped, String);

                /** @brief Emitted after each stage's @c close() completes. @signal */
                PROMEKI_SIGNAL(stageClosed, String);

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

                /**
                 * @brief Emitted on every periodic stats tick once
                 *        @ref setStatsInterval has been configured with
                 *        a non-zero interval and the pipeline is
                 *        @ref State::Running.
                 *
                 * Carries a snapshot from @ref stats.  Subscribers that
                 * use @ref subscribe receive the same snapshot wrapped
                 * in a @ref PipelineEvent::Kind::StatsUpdated event;
                 * this signal is provided for direct consumers that
                 * prefer the typed payload.
                 * @signal
                 */
                PROMEKI_SIGNAL(statsUpdated, MediaPipelineStats);

                // ------------------------------------------------------------
                // Event subscription
                // ------------------------------------------------------------

                /**
                 * @brief Callback signature for @ref subscribe.
                 *
                 * Invoked on the subscriber's @ref EventLoop with each
                 * @ref PipelineEvent the pipeline produces.
                 */
                using EventCallback = std::function<void(const PipelineEvent &)>;

                /**
                 * @brief Registers @p cb to receive every future @ref PipelineEvent.
                 *
                 * The subscriber's @ref EventLoop is captured at the call
                 * site (@ref EventLoop::current).  Each event is dispatched
                 * by posting a callable onto that loop, so callbacks always
                 * run on the subscriber's thread regardless of which thread
                 * triggered the underlying pipeline transition.
                 *
                 * Subscriptions can be installed in any pipeline state and
                 * survive across @ref build / @ref open / @ref start /
                 * @ref stop / @ref close cycles.  The first subscription
                 * lazily installs a @ref Logger listener so subsequent
                 * @c promekiInfo / @c promekiWarn / @c promekiErr calls
                 * are mirrored as @ref PipelineEvent::Kind::Log events;
                 * the listener is removed when the last subscriber leaves.
                 *
                 * @param cb Callback invoked once per event.  An empty
                 *           callback is rejected and yields @c -1.
                 * @return Non-negative subscription id usable with
                 *         @ref unsubscribe, or @c -1 when @p cb was
                 *         empty or no @ref EventLoop is available on
                 *         the calling thread.
                 */
                int subscribe(EventCallback cb);

                /**
                 * @brief Removes a subscription previously installed via @ref subscribe.
                 *
                 * Unknown ids are silently ignored.  Removing the last
                 * subscriber tears down the associated @ref Logger
                 * listener.
                 *
                 * @param id Subscription id returned by @ref subscribe.
                 */
                void unsubscribe(int id);

                /**
                 * @brief Configures the periodic stats tick.
                 *
                 * When @p interval is @c Duration::zero (default) the tick
                 * is disabled and neither @ref statsUpdated nor
                 * @ref PipelineEvent::Kind::StatsUpdated fires.  A
                 * positive @p interval is rounded up to the nearest
                 * millisecond and starts a millisecond-precision timer
                 * on this object's @ref EventLoop while the pipeline is
                 * @ref State::Running.  The timer is stopped during
                 * @ref stop / @ref close and restarted on the next
                 * @ref start when @p interval is still non-zero.
                 *
                 * Safe to call from any state.
                 *
                 * @param interval Tick interval (zero disables).
                 */
                void setStatsInterval(Duration interval);

                /** @brief Returns the configured stats tick interval. */
                Duration statsInterval() const { return _statsInterval; }

        private:
                // One record per outgoing route.  Back-pressure,
                // per-sink frame-count caps, and the actual frame
                // dispatch all live on the @ref MediaIOPortConnection
                // that owns this edge's source — the pipeline only
                // tracks enough state per edge to drive the cascade
                // close when the source EOFs, mark the cap as
                // observed, and look up the sink's stage name for
                // error reporting.
                struct EdgeState {
                                String       toName;
                                MediaIO     *to = nullptr;
                                MediaIOSink *toSink = nullptr;
                                bool         isSinkEdge = false;
                                bool         capReached = false;
                };

                // Each source-or-transit stage owns one SourceState entry.
                // Stages with no outgoing routes (pure sinks) do not appear
                // here — they participate only as a sink end of some
                // upstream stage's connection.  The @c connection field is
                // owned by the pipeline and lives from @ref start until
                // @ref close.
                struct SourceState {
                                MediaIO                 *from = nullptr;
                                MediaIOPortConnection   *connection = nullptr;
                                promeki::List<EdgeState> edges;
                                bool                     upstreamDone = false;
                };

                struct Subscriber {
                                int           id;
                                EventCallback fn;
                                EventLoop    *loop;
                };

                Error    destroyStages();
                Error    topologicallySort(promeki::List<String> &order) const;
                MediaIO *instantiateStage(const MediaPipelineConfig::Stage &s);

                /**
                 * @brief Wires the @ref MediaIOPortConnection graph edges
                 *        whose endpoints have just become live.
                 *
                 * Called from @ref open immediately after each stage's
                 * @c MediaIO::open returns.  Lazily creates a
                 * connection on the stage's source port (if it has
                 * one), and binds the stage's sink port onto every
                 * upstream connection that has a route landing here.
                 * Topological order guarantees the upstream's
                 * connection already exists by the time we look it up.
                 */
                void wireConnectionsForOpenedStage(const String &name);

                /**
                 * @brief Stamps @p ev with the current timestamp (when missing)
                 *        and dispatches a copy to every active subscriber on
                 *        their respective EventLoop.
                 *
                 * Subscriber list is snapshotted under @ref _subsMutex, then
                 * dispatch happens outside the lock so callbacks may install
                 * or remove subscriptions without deadlocking.
                 */
                void publish(PipelineEvent ev);

                /**
                 * @brief Builds a @ref PipelineEvent for the current pipeline
                 *        @ref State and publishes it.
                 */
                void publishStateChanged();

                /**
                 * @brief Builds and publishes a stage-state event with the
                 *        canonical stage transition name.
                 */
                void publishStageState(const String &stageName, const String &transition);

                /** @brief Lazy install of the @ref Logger listener tap. */
                void installLoggerTap();

                /** @brief Removes the @ref Logger listener tap. */
                void removeLoggerTap();

                /** @brief Starts the stats tick if the interval is non-zero and we are running. */
                void startStatsTimerIfNeeded();

                /** @brief Stops the stats tick if it is currently armed. */
                void stopStatsTimer();

                /** @brief Emits a stats snapshot (signal + PipelineEvent). */
                void emitStatsSnapshot();

                /**
                 * @brief Routes a connection's @c upstreamDone signal
                 *        into the cascade-close path.
                 *
                 * Latches the source's @c upstreamDone flag, calls
                 * @ref initiateClose with @c clean=true, then closes
                 * every direct downstream MediaIO so each transform /
                 * sink stage flushes its in-flight buffer and emits
                 * its own EOS.
                 */
                void onUpstreamDone(const String &srcName);

                /**
                 * @brief Routes a connection's @c errorOccurred signal
                 *        (source-side fatal error) into the pipeline
                 *        error counter and triggers a non-clean close.
                 */
                void onSourceConnectionError(const String &srcName, Error err);

                /**
                 * @brief Routes a connection's @c sinkError signal into
                 *        the pipeline error counter and triggers a
                 *        non-clean close.
                 */
                void onSinkConnectionError(const String &srcName, MediaIOSink *sink, Error err);

                /**
                 * @brief Routes a connection's @c sinkLimitReached
                 *        signal into the pipeline-wide cap accounting.
                 *
                 * Decrements @ref _terminalSinksRemaining; once the
                 * counter reaches zero (and a cap was configured) the
                 * pipeline triggers a clean close.
                 */
                void onSinkLimitReached(const String &srcName, MediaIOSink *sink);

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
                 * @brief Watchdog: force-close any stage still open
                 *        after @ref _closeTimeoutMs has elapsed since
                 *        the cascade was armed.  The cascade lets
                 *        stages drain gracefully, but any stage whose
                 *        @c executeCmd blocks on an external signal
                 *        (e.g. a FrameBridge output waiting for a
                 *        consumer) can stall the whole teardown —
                 *        this unblocks it and submits a close on its
                 *        strand so the cascade can finish.
                 */
                void forceCloseRemaining();

                // ObjectBase override — routes the close watchdog's
                // TimerEvent to @ref forceCloseRemaining.
                void timerEvent(TimerEvent *e) override;

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

                /**
                 * @brief Wires a @ref MediaIOStatsCollector onto @p io
                 *        when stats collection is enabled.
                 *
                 * Invoked from @ref build for every stage (factory-
                 * constructed and injected) once it has been
                 * registered in @ref _stages.  When the config's
                 * @ref MediaPipelineConfig::statsWindowSize is zero
                 * the call is a no-op so stats-disabled pipelines pay
                 * no per-command observer cost.
                 */
                void attachStatsCollector(const String &name, MediaIO *io);

                /**
                 * @brief Tears down every stats collector created by
                 *        @ref attachStatsCollector.
                 *
                 * Called from @ref destroyStages alongside the rest of
                 * the per-build state so the next @ref build cycle
                 * starts with a clean slate.
                 */
                void clearStatsCollectors();

                /**
                 * @brief Builds a single @ref MediaPipelineStageStats
                 *        record by querying the live MediaIO for its
                 *        cumulative aggregate and folding the per-stage
                 *        @ref MediaIOStatsCollector windows into the
                 *        per-command breakdown.
                 */
                MediaPipelineStageStats buildStageStats(const String &name, MediaIO *io);

                MediaPipelineConfig                                  _config;
                State                                                _state = State::Empty;
                promeki::Map<String, MediaIO *>                      _stages;
                promeki::Map<String, MediaIO *>                      _injected;
                promeki::Map<String, MediaIOStatsCollector *>        _statsCollectors;
                promeki::Map<String, SourceState>                    _sources;
                promeki::List<String>                                _topoOrder;

                // Close-cascade bookkeeping.  Latched by
                // @ref initiateClose and unwound in
                // @ref finalizeClose.  @c _cleanFinish is the running
                // "no errors observed" bit that eventually feeds
                // @ref finishedSignal; drops to false on any
                // operational or close-time error.
                bool                 _closing = false;
                bool                 _cleanFinish = false;
                promeki::Set<String> _stagesAwaitingClosed;
                Error                _closeError = Error::Ok;

                // Close-watchdog timer.  Armed by @ref initiateClose,
                // stopped by @ref finalizeClose, and escalates to
                // @ref forceCloseRemaining when it fires.  Zero means
                // "no watchdog armed".
                unsigned int _closeTimeoutMs = DefaultCloseTimeoutMs;
                int          _closeWatchdogTimerId = -1;

                // Pipeline-wide frame-count cap, cached at build time
                // from @ref MediaPipelineConfig::frameCount.  Zero means
                // "no cap" (matches the @ref FrameCount::isFinite() gate).
                // Non-zero values are passed as the per-sink frame
                // limit to @ref MediaIOPortConnection::addSink for every
                // terminal-sink edge; the connection enforces the cap at
                // safe-cut-point boundaries and emits @c sinkLimitReached
                // when each sink hits it.  @ref _terminalSinksRemaining
                // counts the still-pending capped sinks; when it reaches
                // zero the pipeline initiates a clean close.
                int64_t _frameCountLimit = 0;
                int     _terminalSinksRemaining = 0;

                // Event subscription bookkeeping.  _subsMutex guards
                // _subscribers and the logger-listener handle so
                // subscribe / unsubscribe / publish can race with each
                // other across threads safely.  The actual dispatch in
                // publish() snapshots the list under the lock and
                // releases before invoking callbacks so a callback may
                // (un)subscribe re-entrantly without deadlocking.
                mutable Mutex                 _subsMutex;
                promeki::Map<int, Subscriber> _subscribers;
                int                           _nextSubId = 0;
                Logger::ListenerHandle        _loggerTap = 0;

                // Stats tick.  _statsInterval is the user-configured
                // wall-clock cadence; _statsTimerId is the active timer
                // id (negative when no timer is armed).
                Duration _statsInterval;
                int      _statsTimerId = -1;
};

PROMEKI_NAMESPACE_END
