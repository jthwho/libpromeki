/**
 * @file      mediapipeline.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_PROAV
#include <functional>
#include <promeki/function.h>
#include <promeki/clock.h>
#include <promeki/duration.h>
#include <promeki/error.h>
#include <promeki/frame.h>
#include <promeki/list.h>
#include <promeki/logger.h>
#include <promeki/map.h>
#include <promeki/mediaio.h>
#include <promeki/mediaioportconnection.h>
#include <promeki/mediaioportgroup.h>
#include <promeki/mediapipelineconfig.h>
#include <promeki/mediapipelinestats.h>
#include <promeki/mediapipelinetrigger.h>
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
 * via @c MediaIOSink::writesAccepted and @c frameWanted.  Fan-in on a
 * single stage is not supported in this implementation (the first
 * stage that would receive frames from more than one producer is
 * rejected at @ref build time).
 *
 * @par Thread Safety
 * Inherits @ref ObjectBase &mdash; thread-affine.  Pipeline lifecycle
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
                 * @brief Playback transport state, orthogonal to @ref State.
                 *
                 * Only meaningful while the pipeline is @ref State::Running
                 * and the configured @ref MediaPipelineConfig::Kind is
                 * @ref MediaPipelineConfig::Kind::Playback.  Transport
                 * controls (@ref play, @ref pause) drive transitions
                 * between Playing and Paused; @ref Seeking is reserved
                 * for the Phase 3 seek API and unused today; @ref Ended
                 * latches when the source naturally hits EOS.
                 */
                enum class PlaybackState {
                        Idle,    ///< @brief Pre-Running or post-Stopped — transport inactive.
                        Playing, ///< @brief Pacing clock running, frames flowing.
                        Paused,  ///< @brief Pacing clock paused; current frame held.
                        Seeking, ///< @brief Seek in flight (reserved for Phase 3).
                        Ended    ///< @brief Source EOS reached at current rate.
                };

                /**
                 * @brief Capture transport state, orthogonal to @ref State.
                 *
                 * Only meaningful while the pipeline is @ref State::Running
                 * and the configured @ref MediaPipelineConfig::Kind is
                 * @ref MediaPipelineConfig::Kind::Capture.  Capture
                 * controls (@ref armCapture, @ref startCapture,
                 * @ref pauseCapture, @ref resumeCapture, @ref stopCapture)
                 * drive the transitions; @ref setCaptureTrigger optionally
                 * defers the @c Armed → @c Recording transition until a
                 * per-frame predicate matches.
                 */
                enum class CaptureState {
                        Idle,      ///< @brief Capture sink gate closed; no trigger armed.
                        Armed,     ///< @brief Trigger armed; sink gate closed until match.
                        Recording, ///< @brief Sink gate open; frames flow to the capture sink.
                        Paused     ///< @brief Mid-recording pause; first post-resume frame stamped ForceKeyframe.
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
                 * source's synthetic EOS reaches @c drainSource and
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
                // Playback transport
                // ------------------------------------------------------------

                /**
                 * @brief Returns the current playback-transport state.
                 *
                 * @ref PlaybackState::Idle outside @ref State::Running.
                 * Inside @ref State::Running for a Playback pipeline,
                 * follows the actual pacing clock's pause flag plus
                 * any seek / EOS latches.
                 */
                PlaybackState playbackState() const { return _playbackState; }

                /**
                 * @brief Transitions the playback transport to Playing.
                 *
                 * Resumes the pacing-stage clock if paused.  Pre-conditions:
                 * pipeline is @ref State::Running, kind is Playback.
                 *
                 * @return @c Error::Ok on success.  @c Error::NotSupported
                 *         when called on a Capture pipeline.  @c Error::NotOpen
                 *         when the pipeline is not Running.  Backend errors
                 *         from @ref Clock::setPause are propagated.
                 */
                Error play();

                /**
                 * @brief Transitions the playback transport to Paused.
                 *
                 * Pauses the pacing-stage clock; downstream pacing-aware
                 * sinks naturally stop pulling, frames in flight finish
                 * their current command and the pump idles cleanly.
                 * Pre-conditions identical to @ref play.
                 *
                 * @return @c Error::Ok / @c Error::NotSupported / @c Error::NotOpen
                 *         per @ref play.
                 */
                Error pause();

                /**
                 * @brief Convenience: pause when Playing, play otherwise.
                 *
                 * Returns the same error codes as @ref play / @ref pause.
                 * No-op (returns @c Error::Ok) when the transport is in
                 * a state other than Playing or Paused (Idle / Seeking /
                 * Ended) so UI bindings can wire it as a single button.
                 */
                Error togglePlayPause();

                /**
                 * @brief Returns the current playback rate.
                 *
                 * Reads from the pacing port group when the transport is
                 * resolved.  Returns @c 1.0 (the default) when no pacing
                 * stage is configured or the pipeline is not yet running.
                 */
                double rate() const;

                /**
                 * @brief Sets the per-pacing-group playback rate.
                 *
                 * Forwards to @ref MediaIOPortGroup::setRate on the pacing
                 * stage.  See @ref MediaIOPortGroup::setRate for the rate
                 * semantics (1.0 normal, 0.5 slow-mo, 2.0 fast-forward,
                 * negative for reverse, 0.0 for hold).  Pre-conditions are
                 * identical to @ref play.  Non-finite @p r is rejected
                 * with @c Error::InvalidArgument.
                 *
                 * Emits @ref rateChangedSignal on success.
                 */
                Error setRate(double r);

                /**
                 * @brief Returns the pacing group's current frame.
                 *
                 * Reads from the pacing port group when the transport is
                 * resolved.  Returns an invalid @ref FrameNumber when no
                 * pacing stage is configured or the pipeline is not yet
                 * running.
                 */
                FrameNumber currentFrame() const;

                /**
                 * @brief Seeks the pacing group to @p pos.
                 *
                 * Pauses the pacing clock first (so the post-seek display
                 * frame is held), dispatches the seek through
                 * @ref MediaIOPortGroup::seekToFrame, and parks the
                 * transport in @ref PlaybackState::Paused on completion.
                 * @ref PlaybackState::Seeking is emitted while the seek
                 * is in flight.
                 *
                 * @param pos  Target frame number.
                 * @param mode Seek mode (@c SeekDefault picks the
                 *             backend's preferred mode, typically
                 *             @c SeekExact for file backends).
                 * @return @c Error::Ok / @c Error::NotSupported /
                 *         @c Error::NotOpen per @ref play, plus
                 *         @c Error::IllegalSeek when the pacing group
                 *         reports it is not seekable.
                 */
                Error seek(FrameNumber pos, MediaIOSeekMode mode = MediaIO_SeekDefault);

                /**
                 * @brief Advances the transport by @p n frames forward.
                 *
                 * Implemented as @ref pause followed by
                 * @ref seek(currentFrame()+n).  The transport stays in
                 * @ref PlaybackState::Paused.
                 *
                 * @param n Number of frames to step (default 1).
                 * @return Same error codes as @ref seek.
                 */
                Error stepForward(int64_t n = 1);

                /**
                 * @brief Retreats the transport by @p n frames.
                 *
                 * Counterpart to @ref stepForward — equivalent to
                 * @c stepForward(-n).
                 *
                 * @param n Number of frames to step backwards (default 1).
                 * @return Same error codes as @ref seek.
                 */
                Error stepBackward(int64_t n = 1);

                // ------------------------------------------------------------
                // Capture transport
                // ------------------------------------------------------------

                /**
                 * @brief Returns the current capture-transport state.
                 *
                 * @ref CaptureState::Idle outside @ref State::Running.
                 * Inside @ref State::Running for a Capture pipeline,
                 * follows the configured trigger and the explicit
                 * arm / start / pause / resume / stop calls.
                 */
                CaptureState captureState() const { return _captureState; }

                /**
                 * @brief Arms the capture transport.
                 *
                 * If a trigger has been installed via @ref setCaptureTrigger
                 * the pipeline transitions to @ref CaptureState::Armed
                 * (sink gate closed, frames evaluated per-frame against
                 * the trigger).  Without a trigger this collapses to
                 * @ref startCapture for the same effect — recording
                 * begins immediately.
                 *
                 * @return @c Error::Ok on success.  @c Error::NotSupported
                 *         when the pipeline is not @c Kind::Capture, or
                 *         @c Error::NotOpen when not Running.
                 */
                Error armCapture();

                /**
                 * @brief Manually transitions the capture transport to Recording.
                 *
                 * Skips the trigger gate — useful as a manual override
                 * (operator UI button) on an armed pipeline that has
                 * not yet matched, or as a one-shot record-now on an
                 * Idle pipeline.  Stamps @c Metadata::ForceKeyframe on
                 * the first admitted frame so the encoder cuts a
                 * clean IDR at the recording start.
                 *
                 * @return Same error codes as @ref armCapture.
                 */
                Error startCapture();

                /**
                 * @brief Pauses an in-progress recording.
                 *
                 * Closes the capture sink's gate; frames still flow
                 * through the rest of the pipeline (preview, monitor)
                 * but are not handed to the capture sink.  The
                 * pipeline transitions to @ref CaptureState::Paused.
                 * @ref resumeCapture stamps @c ForceKeyframe on the
                 * next admitted frame so the encoder restarts cleanly.
                 *
                 * @return Same error codes as @ref armCapture, plus
                 *         @c Error::Ok no-op when not currently
                 *         Recording.
                 */
                Error pauseCapture();

                /**
                 * @brief Resumes a paused recording.
                 *
                 * Re-opens the capture sink's gate; the next frame
                 * delivered to the capture sink carries
                 * @c Metadata::ForceKeyframe so the downstream
                 * encoder emits an IDR.  The pipeline transitions
                 * back to @ref CaptureState::Recording.
                 *
                 * @return Same error codes as @ref armCapture, plus
                 *         @c Error::Ok no-op when not currently
                 *         Paused.
                 */
                Error resumeCapture();

                /**
                 * @brief Disarms / stops the capture transport.
                 *
                 * Closes the capture sink's gate, clears any installed
                 * trigger arming state, and transitions to
                 * @ref CaptureState::Idle.  The pipeline lifecycle
                 * stays @c Running — the caller drives @ref close /
                 * @ref stop separately when ready to tear down.
                 *
                 * @return Same error codes as @ref armCapture.
                 */
                Error stopCapture();

                /**
                 * @brief Plug-in trigger for the @c Armed → @c Recording transition.
                 *
                 * Replaces the lambda overload's predicate with a
                 * pre-built trigger.  Pass an empty @ref UniquePtr to
                 * clear (equivalent to @ref clearCaptureTrigger).
                 *
                 * @param trig The trigger to install (transferring
                 *             ownership), or empty to clear.
                 * @return @c Error::Ok / @c Error::NotSupported per
                 *         @ref armCapture.
                 */
                Error setCaptureTrigger(MediaPipelineTrigger::UPtr trig);

                /**
                 * @brief Plug-in trigger from a callable.
                 *
                 * Convenience: wraps @p fn in a
                 * @ref MediaPipelineFunctionTrigger and installs it
                 * via @ref setCaptureTrigger.  An empty function
                 * clears any installed trigger.
                 */
                Error setCaptureTrigger(Function<bool(const Frame &)> fn);

                /**
                 * @brief Plug-in trigger from a VariantQuery expression.
                 *
                 * Parses @p queryExpr via @ref MediaPipelineQueryTrigger::parse
                 * and installs the resulting trigger on success.
                 *
                 * @param queryExpr A VariantQuery<Frame> source expression
                 *                  (e.g. @c "Meta.FrameKeyframe == true").
                 * @return @c Error::Ok on success; the parser's error
                 *         on parse failure; @c Error::NotSupported
                 *         when the pipeline is not @c Kind::Capture.
                 */
                Error setCaptureTrigger(const String &queryExpr);

                /** @brief Removes any installed capture trigger. */
                Error clearCaptureTrigger();

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

                /**
                 * @brief Emitted on every playback-transport state transition.
                 *
                 * Fires after @ref play / @ref pause / @ref togglePlayPause
                 * (and, in later phases, @c seek / @c stepForward / EOS
                 * latching).  Subscribers using @ref subscribe receive the
                 * same transition wrapped in a
                 * @ref PipelineEvent::Kind::TransportStateChanged event;
                 * direct consumers can listen on this signal for the typed
                 * payload.
                 * @signal
                 */
                PROMEKI_SIGNAL(playbackStateChanged, PlaybackState);

                /**
                 * @brief Emitted on every @ref setRate transition.
                 *
                 * Carries the new rate value.  Subscribers using
                 * @ref subscribe receive the same transition mirrored
                 * through @ref PipelineEvent::Kind::TransportStateChanged
                 * with @c metadata["scope"] == @c "rate".
                 * @signal
                 */
                PROMEKI_SIGNAL(rateChanged, double);

                /**
                 * @brief Emitted after a seek completes.
                 *
                 * Carries the post-seek frame position so UI bindings can
                 * update scrubbers without polling @ref currentFrame.
                 * @signal
                 */
                PROMEKI_SIGNAL(positionChanged, FrameNumber);

                /**
                 * @brief Emitted on every capture-transport state transition.
                 *
                 * Carries the new @ref CaptureState.  Subscribers using
                 * @ref subscribe receive the same transition mirrored
                 * through @ref PipelineEvent::Kind::TransportStateChanged
                 * with @c metadata["scope"] == @c "capture".
                 * @signal
                 */
                PROMEKI_SIGNAL(captureStateChanged, CaptureState);

                // ------------------------------------------------------------
                // Event subscription
                // ------------------------------------------------------------

                /**
                 * @brief Callback signature for @ref subscribe.
                 *
                 * Invoked on the subscriber's @ref EventLoop with each
                 * @ref PipelineEvent the pipeline produces.
                 */
                using EventCallback = Function<void(const PipelineEvent &)>;

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
                 * is disabled and neither @c statsUpdatedSignal nor
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
                                List<EdgeState> edges;
                                bool                     upstreamDone = false;
                };

                struct Subscriber {
                                int           id;
                                EventCallback fn;
                                EventLoop    *loop;
                };

                Error    destroyStages();
                Error    topologicallySort(List<String> &order) const;
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
                 * @brief Builds a @ref PipelineEvent::Kind::TransportStateChanged
                 *        event with the named scope and dispatches it.
                 *
                 * @param scope    @c "playback" or @c "capture".
                 * @param newState String form of the new transport state
                 *                 (e.g. @c "Playing", @c "Paused").
                 */
                void publishTransportStateChanged(const String &scope, const String &newState);

                /**
                 * @brief Updates @ref _playbackState, fires the transport
                 *        signal, and publishes the matching PipelineEvent.
                 *
                 * No-op when @p s equals the current state.  The signal /
                 * event are only fired on real transitions.
                 */
                void setPlaybackState(PlaybackState s);

                /**
                 * @brief Resolves the pacing stage / port-group / clock
                 *        from the configured @c pacesPipeline flag.
                 *
                 * Called from @ref open after every stage is open so each
                 * stage's port group exists.  For @c Kind::Playback configs
                 * with exactly one pacing stage, populates
                 * @ref _pacingStage / @ref _pacingGroup / @ref _pacingClock
                 * and returns @c Error::Ok.  For @c Kind::Capture configs
                 * the resolution is skipped.  For Playback configs missing
                 * a pacer the pacing fields are left null and the call
                 * returns @c Error::Ok with a warning — Phase 1 keeps the
                 * legacy behavior, the @c play / @c pause API surfaces
                 * @c Error::NotSupported when the fields are unset.
                 */
                Error resolvePacingStage();

                /**
                 * @brief Resolves capture-sink stages from configured
                 *        @c captureSink flags and caches them for the
                 *        capture-transport API.
                 *
                 * Capture pipelines may flag multiple stages as capture
                 * sinks (multi-recorder mirroring); the gate, trigger
                 * stamp, and pause/resume calls fan out to every flagged
                 * sink.  Called from @ref open alongside
                 * @ref resolvePacingStage.
                 */
                Error resolveCaptureSinks();

                /**
                 * @brief Updates @ref _captureState, fires the capture
                 *        signal, and publishes the matching PipelineEvent.
                 */
                void setCaptureState(CaptureState s);

                /**
                 * @brief Per-frame inspector wired into the capture-side
                 *        connection's @ref MediaIOPortConnection::setFrameInspector.
                 *
                 * Evaluates the installed trigger while the pipeline is
                 * @c CaptureState::Armed and, on a match, opens the
                 * capture-sink gates so the matched frame becomes the
                 * first recorded frame (with @c ForceKeyframe stamped
                 * by the gate's open-transition path).
                 */
                void onCaptureFrame(const Frame &frame);

                /**
                 * @brief Sets every capture-sink connection's gate.
                 *
                 * Convenience used by the capture-transport methods to
                 * open / close every connection that feeds a flagged
                 * capture sink in one go.
                 */
                void setCaptureGates(bool open);

                /** @brief Installs the per-frame inspector on every capture-side connection. */
                void installCaptureInspectorIfNeeded();

                /** @brief Removes the per-frame inspector from every capture-side connection. */
                void removeCaptureInspector();

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
                 * @brief Detaches a caller-owned injected stage from the
                 *        pipeline's stage maps.
                 *
                 * Registered as an @ref ObjectBase destruction cleanup on
                 * every injected stage so that a stage the caller deletes
                 * while the pipeline still references it is nulled out of
                 * @ref _stages (and dropped from @ref _injected) before any
                 * close path can dereference the freed pointer.
                 *
                 * @param name The injected stage's resolved name.
                 */
                void detachInjectedStage(const String &name);

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
                PlaybackState                                        _playbackState = PlaybackState::Idle;
                CaptureState                                         _captureState = CaptureState::Idle;

                // Pacing-stage resolution (Playback only).  Populated by
                // @ref resolvePacingStage during @ref open, cleared by
                // @ref destroyStages.  Null when the kind is Capture or
                // when no stage in the config flagged @c pacesPipeline=true.
                MediaIO          *_pacingStage = nullptr;
                MediaIOPortGroup *_pacingGroup = nullptr;
                Clock::Ptr        _pacingClock;

                // Capture-sink resolution (Capture only).  Populated by
                // @ref resolveCaptureSinks during @ref open, cleared by
                // @ref destroyStages.  Empty when the kind is Playback
                // or when no stage in the config flagged @c captureSink=true.
                List<MediaIO *>            _captureSinks;
                MediaPipelineTrigger::UPtr _captureTrigger;
                Map<String, MediaIO *>                      _stages;
                Map<String, MediaIO *>                      _injected;
                Map<String, MediaIOStatsCollector *>        _statsCollectors;
                Map<String, SourceState>                    _sources;
                List<String>                                _topoOrder;

                // Close-cascade bookkeeping.  Latched by
                // @ref initiateClose and unwound in
                // @ref finalizeClose.  @c _cleanFinish is the running
                // "no errors observed" bit that eventually feeds
                // @ref finishedSignal; drops to false on any
                // operational or close-time error.
                bool                 _closing = false;
                bool                 _cleanFinish = false;
                Set<String> _stagesAwaitingClosed;
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
                Map<int, Subscriber> _subscribers;
                int                           _nextSubId = 0;
                Logger::ListenerHandle        _loggerTap = 0;

                // Stats tick.  _statsInterval is the user-configured
                // wall-clock cadence; _statsTimerId is the active timer
                // id (negative when no timer is armed).
                Duration _statsInterval;
                int      _statsTimerId = -1;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_PROAV
