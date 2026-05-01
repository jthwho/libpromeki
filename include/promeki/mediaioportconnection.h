/**
 * @file      mediaioportconnection.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/atomic.h>
#include <promeki/error.h>
#include <promeki/framecount.h>
#include <promeki/list.h>
#include <promeki/mediaiorequest.h>
#include <promeki/namespace.h>
#include <promeki/objectbase.h>

PROMEKI_NAMESPACE_BEGIN

class MediaIOSource;
class MediaIOSink;

/**
 * @brief Live wire from one @ref MediaIOSource to one or more @ref MediaIOSink.
 * @ingroup mediaio_user
 *
 * A @ref MediaIOPortConnection drives a signal-driven drain pump that
 * forwards frames from a source port to one or more sink ports with
 * proper backpressure.  It subscribes to the source's
 * @ref MediaIOSource::frameReady and to each sink's
 * @ref MediaIOSink::frameWanted signal, and pumps the transfer until
 * either the source signals end-of-stream / error, or every sink has
 * stopped accepting frames (limit hit, write error, or removed).
 *
 * @par Drain semantics
 * Reading from a @ref MediaIOSource is destructive — only one consumer
 * can pop a given frame off the source's queue.  To support pipeline
 * fan-out (one source feeding many sinks) the connection itself owns
 * the read side and dispatches each frame to every active sink in turn.
 *
 * On every @c frameReady or @c frameWanted, the connection drains
 * non-blocking reads from the source into non-blocking writes on every
 * non-stopped sink for as long as everyone cooperates:
 *
 *  - @c readFrame returns @c Error::TryAgain → wait for the next
 *    @c frameReady.
 *  - @c readFrame returns @c Error::EndOfFile → emit @ref upstreamDone
 *    and stop reading.
 *  - @c readFrame returns another error → emit @ref errorOccurred and
 *    stop pumping.
 *  - Any active sink's @c writesAccepted drops to zero → wait for the
 *    next @c frameWanted before reading again (so the source's queue
 *    can absorb backlog while we wait, but the slowest sink throttles
 *    the whole connection).
 *  - A sink @c writeFrame returns a hard error → emit @ref sinkError
 *    for that sink, mark it stopped, continue the pump for the
 *    remaining sinks.
 *  - A sink reaches its per-sink @ref FrameCount limit at the next
 *    @ref Frame::isSafeCutPoint → emit @ref sinkLimitReached, mark
 *    the sink stopped (limit-reached), continue the pump for the
 *    remaining sinks.
 *  - All sinks become stopped → emit @ref allSinksDone and stop
 *    pumping.
 *
 * @par Frame-count cap
 * Each sink may carry an optional @ref FrameCount limit (passed to
 * @ref addSink).  An empty / unknown @ref FrameCount means "no
 * limit"; a finite, non-zero count caps the number of frames written
 * to that sink.  The cap is honoured at the next safe-cut-point on
 * or after the cap-th write so the GOP / audio packet boundary
 * containing the cap stays complete — the cut frame itself is
 * dropped and the sink is marked stopped.
 *
 * @par Lifecycle
 * Constructed in the stopped state.  Sinks are added via
 * @ref addSink before @ref start.  Once @ref start has been called,
 * the sink set is frozen for the life of the run.  @ref stop
 * disconnects the signal handlers and emits @ref stopped; the
 * connection does not open or close the underlying ports — the
 * caller is responsible for opening / closing the owning
 * @ref MediaIO instances.
 *
 * @par Threading
 * Slot delivery follows the connect-side @ref EventLoop affinity, so
 * the pump runs on whichever loop the @ref MediaIOPortConnection
 * lives on.  Distinct connections may run on distinct loops.
 *
 * @par 1:1 convenience
 * The two-port @c (source, sink, parent) constructor remains as a
 * thin convenience over the multi-sink form for the common
 * standalone case (file-to-file copy, "save N frames" CLI helpers,
 * test fixtures).  It is equivalent to constructing a multi-sink
 * connection with the given source and immediately calling
 * @ref addSink with no frame limit.
 *
 * @par Use cases
 * Standalone tooling that needs source→sink forwarding without
 * spinning up a full @ref MediaPipeline; the pipeline itself uses one
 * @ref MediaIOPortConnection per producing stage to drive fan-out
 * and per-sink frame-count caps.
 */
class MediaIOPortConnection : public ObjectBase {
                PROMEKI_OBJECT(MediaIOPortConnection, ObjectBase)
        public:
                /**
                 * @brief Constructs a 1:1 connection between @p source and @p sink.
                 *
                 * Convenience for the common single-sink case;
                 * equivalent to constructing the connection with
                 * @p source alone and calling @ref addSink with an
                 * empty @ref FrameCount immediately afterwards.  Both
                 * ports must outlive the connection.
                 *
                 * @param source The source side; must be non-null.
                 * @param sink   The sink side; must be non-null.
                 * @param parent Optional parent for ObjectBase lifetime
                 *               management.
                 */
                MediaIOPortConnection(MediaIOSource *source, MediaIOSink *sink, ObjectBase *parent = nullptr);

                /**
                 * @brief Constructs a multi-sink connection rooted at @p source.
                 *
                 * No sinks are attached at construction time; callers
                 * register them via @ref addSink before calling
                 * @ref start.
                 *
                 * @param source The source side; must be non-null.
                 * @param parent Optional parent for ObjectBase lifetime
                 *               management.
                 */
                explicit MediaIOPortConnection(MediaIOSource *source, ObjectBase *parent = nullptr);

                /** @brief Destructor.  Stops the connection if running. */
                ~MediaIOPortConnection() override;

                /** @brief Returns the source port this connection reads from. */
                MediaIOSource *source() const { return _source; }

                /**
                 * @brief Returns the first attached sink (or nullptr).
                 *
                 * Convenience accessor for 1:1 connections; multi-sink
                 * users should iterate @ref sinks instead.
                 */
                MediaIOSink *sink() const;

                /** @brief Returns the list of attached sinks in attachment order. */
                promeki::List<MediaIOSink *> sinks() const;

                /** @brief Returns the number of attached sinks. */
                int sinkCount() const { return static_cast<int>(_sinks.size()); }

                /** @brief True if the connection is currently driving the pump. */
                bool isRunning() const { return _running; }

                /**
                 * @brief Attaches a sink to this connection.
                 *
                 * Must be called before @ref start.  The sink is
                 * appended to the dispatch list; every successful
                 * source read after @ref start will be forwarded to
                 * each attached sink that has not stopped.
                 *
                 * @param sink       The sink to attach; must be non-null.
                 * @param frameLimit Optional per-sink cap on the
                 *                   number of frames written to this
                 *                   sink.  An empty / unknown
                 *                   @ref FrameCount disables the cap;
                 *                   a finite, non-zero count caps the
                 *                   sink at the next safe cut point
                 *                   on or after the limit-th write.
                 * @return @c Error::Ok on success.
                 *         @c Error::Invalid when @p sink is null.
                 *         @c Error::Busy when called after @ref start.
                 */
                Error addSink(MediaIOSink *sink, FrameCount frameLimit = FrameCount());

                /**
                 * @brief Wires the drain handlers and primes the pump.
                 *
                 * Connects the source's @c frameReady signal and each
                 * sink's @c frameWanted / @c writeError signal to the
                 * internal pump, then performs an initial drain attempt
                 * to pick up any frames already queued or kick a
                 * pre-fetch.  Idempotent — calling @ref start on a
                 * running connection is a no-op.
                 *
                 * @return @c Error::Ok on success, or @c Error::Invalid
                 *         when the source is null or no sinks are
                 *         attached.
                 */
                Error start();

                /**
                 * @brief Disconnects the drain handlers and emits @ref stopped.
                 *
                 * In-flight strand commands on the source / sinks
                 * complete normally — the connection just stops
                 * listening for @c frameReady / @c frameWanted /
                 * @c writeError.  Idempotent.
                 */
                void stop();

                // ---- Stats ----

                /**
                 * @brief Total successful source reads since @ref start.
                 *
                 * Counts one per source read regardless of how many
                 * sinks consumed the frame; per-sink delivery counts
                 * are queried via @ref framesWritten.
                 */
                int64_t framesTransferred() const { return _framesTransferred.value(); }

                /**
                 * @brief Total successful writes to @p sink since @ref start.
                 *
                 * @param sink Must be a sink previously attached via
                 *             @ref addSink.
                 * @return Per-sink write count, or @c 0 when @p sink
                 *         is not attached.
                 */
                int64_t framesWritten(MediaIOSink *sink) const;

                /** @brief True once the source has reported end-of-stream. */
                bool upstreamDone() const { return _upstreamDone; }

                /**
                 * @brief True if @p sink has stopped (limit reached, error, or never attached).
                 *
                 * @param sink Must be a sink previously attached via
                 *             @ref addSink.
                 */
                bool sinkStopped(MediaIOSink *sink) const;

                /** @brief True once every attached sink has stopped. */
                bool allSinksDone() const { return _allSinksDoneEmitted; }

                // ---- Signals ----

                /**
                 * @brief Emitted when the source reports end-of-stream.
                 * @signal
                 *
                 * Fired exactly once per @ref start.  The connection
                 * keeps the sinks armed so any backlog already in
                 * flight completes naturally; callers typically
                 * respond by closing the source's @ref MediaIO and
                 * letting the cascade reach downstream.
                 */
                PROMEKI_SIGNAL(upstreamDone);

                /**
                 * @brief Emitted on a non-recoverable source-side error.
                 * @signal
                 *
                 * Source-side @c readFrame errors and contract
                 * violations from the pump bubble up here.  Per-sink
                 * write errors fire on @ref sinkError instead.
                 *
                 * @param err The error returned by @c readFrame.
                 *            @c TryAgain is handled internally and
                 *            does not surface here.
                 */
                PROMEKI_SIGNAL(errorOccurred, Error);

                /**
                 * @brief Emitted when a sink hits its per-sink frame-count cap.
                 * @signal
                 *
                 * The capped sink is removed from the active dispatch
                 * set.  Pumping continues for the remaining sinks.
                 *
                 * @param sink The sink that reached its limit.
                 */
                PROMEKI_SIGNAL(sinkLimitReached, MediaIOSink *);

                /**
                 * @brief Emitted on a non-recoverable per-sink error.
                 * @signal
                 *
                 * Both synchronous @c writeFrame failures and
                 * asynchronous strand-side @c writeError emissions
                 * funnel through this signal.  The errored sink is
                 * removed from the active dispatch set; pumping
                 * continues for the remaining sinks.
                 *
                 * @param sink The sink that errored.
                 * @param err  The reported error.
                 */
                PROMEKI_SIGNAL(sinkError, MediaIOSink *, Error);

                /**
                 * @brief Emitted exactly once when every attached sink has stopped.
                 * @signal
                 *
                 * Fires on the transition where the final sink leaves
                 * the active set (limit, error, or post-error
                 * shutdown), giving consumers a single completion
                 * point to wait on.  After this signal the pump is a
                 * no-op until @ref stop is called.
                 */
                PROMEKI_SIGNAL(allSinksDone);

                /** @brief Emitted by @ref stop. @signal */
                PROMEKI_SIGNAL(stopped);

        private:
                struct SinkState {
                                MediaIOSink *sink = nullptr;
                                FrameCount   frameLimit;
                                int64_t      framesWritten = 0;
                                bool         doneByLimit = false;
                                bool         stopped = false;
                };

                void              pump();
                void              onSinkWriteError(MediaIOSink *sink, Error err);
                SinkState        *findSinkState(MediaIOSink *sink);
                const SinkState  *findSinkState(MediaIOSink *sink) const;
                void              maybeEmitAllSinksDone();
                bool              everyActiveSinkAcceptsWrite() const;

                void schedulePump();

                MediaIOSource           *_source = nullptr;
                promeki::List<SinkState> _sinks;
                // The pump holds the most recent in-flight read
                // request across iterations.  When @ref readFrame
                // returns a request whose payload has not yet
                // arrived (cache miss) the pump stashes it here and
                // returns; the next pump invocation (kicked by
                // @c frameReady) re-checks and consumes when ready,
                // so no prefetched frame is ever discarded.
                MediaIORequest           _pendingRead;
                bool                     _running = false;
                bool                     _upstreamDone = false;
                bool                     _allSinksDoneEmitted = false;
                Atomic<int64_t>          _framesTransferred{0};
                // Signal-coalescer: at most one pump is queued on
                // the loop at a time.  Many signals (frameReady +
                // per-sink frameWanted + per-cmd .then()) can fire
                // simultaneously from the strand worker; without
                // this flag they each post a pump callable and the
                // EventLoop's @c processEvents drains them faster
                // than @c pumpUntil can break out to check its
                // predicate.  The flag flips to true on the first
                // post and is cleared inside pump itself.
                Atomic<bool>             _pumpScheduled{false};
};

PROMEKI_NAMESPACE_END
