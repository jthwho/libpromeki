/**
 * @file      mediaiocommand.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cassert>
#include <functional>
#include <promeki/namespace.h>
#include <promeki/error.h>
#include <promeki/list.h>
#include <promeki/sharedptr.h>
#include <promeki/string.h>
#include <promeki/atomic.h>
#include <promeki/mutex.h>
#include <promeki/waitcondition.h>
#include <promeki/frame.h>
#include <promeki/framecount.h>
#include <promeki/framenumber.h>
#include <promeki/framerate.h>
#include <promeki/mediadesc.h>
#include <promeki/audiodesc.h>
#include <promeki/metadata.h>
#include <promeki/mediaconfig.h>
#include <promeki/mediaiostats.h>
#include <promeki/mediaiotypes.h>
#include <promeki/clock.h>

PROMEKI_NAMESPACE_BEGIN

class EventLoop;
class MediaIOPortGroup;
class MediaIOSink;

// ============================================================================
// Command hierarchy — all I/O operations are submitted as commands to the
// MediaIO submit() virtual.  Each command carries inputs (set by the public
// API method that builds it) and outputs (set by the backend's
// executeCmd()), so all data flow is lock-free.
// ============================================================================

/**
 * @brief Boilerplate for derived MediaIOCommand classes.
 * @ingroup mediaio_backend
 *
 * Injects the `kind()` override and an asserting `_promeki_clone()`
 * required by the SharedPtr proxy machinery.  Use it as the first
 * line of every concrete command class.
 *
 * @param NAME      The class name.
 * @param KIND_TAG  The MediaIOCommand::Kind enum value.
 */
#define PROMEKI_MEDIAIO_COMMAND(NAME, KIND_TAG)                                                                        \
public:                                                                                                                \
        Kind kind() const override {                                                                                   \
                return KIND_TAG;                                                                                       \
        }                                                                                                              \
        NAME *_promeki_clone() const {                                                                                 \
                assert(false && #NAME " should not be cloned");                                                        \
                return nullptr;                                                                                        \
        }

/**
 * @brief Base class for MediaIO commands.
 * @ingroup mediaio_backend
 *
 * Commands are shared via SharedPtr<MediaIOCommand, false> with COW
 * disabled — derived types are owned via the proxy path and never
 * cloned.
 *
 * Commands flow through the framework in three stages:
 *
 *  1. **Built** by a public API method on @ref MediaIO /
 *     @ref MediaIOSource / @ref MediaIOSink / @ref MediaIOPortGroup.
 *     The Input fields are populated from the caller's arguments and
 *     from the @ref MediaIO's pre-open / pending state.  A
 *     @ref MediaIORequest is constructed atop the command, arming the
 *     completion hook.
 *  2. **Dispatched** by @ref MediaIO::submit().  The default strategy
 *     posts to a strand; concrete strategy classes (added in Phase 10)
 *     route the command differently.  The strategy invokes
 *     @c executeCmd(...) on the backend, which populates the Output
 *     fields and returns an @ref Error.
 *  3. **Completed** by @ref MediaIO::completeCommand().  Cache writes
 *     happen first, then cache-derived signals fire, then the
 *     command's @ref onComplete hook fires (resolving the
 *     @ref MediaIORequest).  This ordering is fixed so
 *     @c MediaIORequest::then() callbacks always observe up-to-date
 *     cached state.
 */
class MediaIOCommand {
        public:
                /** @brief Concrete command kind. */
                enum Kind {
                        Open,     ///< @brief MediaIOCommandOpen
                        Close,    ///< @brief MediaIOCommandClose
                        Read,     ///< @brief MediaIOCommandRead
                        Write,    ///< @brief MediaIOCommandWrite
                        Seek,     ///< @brief MediaIOCommandSeek
                        Params,   ///< @brief MediaIOCommandParams
                        Stats,    ///< @brief MediaIOCommandStats
                        SetClock  ///< @brief MediaIOCommandSetClock
                };

                /** @brief Shared pointer type for command sharing. */
                using Ptr = SharedPtr<MediaIOCommand, false>;

                /**
                 * @internal
                 * @brief Reference count (managed by SharedPtrProxy).
                 *
                 * Public so the proxy can manipulate it without
                 * friend declarations; not part of the user-visible
                 * API.
                 */
                RefCount _promeki_refct;

                /**
                 * @brief Clone hook required by SharedPtr machinery.
                 *
                 * Commands are never cloned (COW is disabled).  The
                 * implementation asserts to catch accidental clone calls.
                 */
                MediaIOCommand *_promeki_clone() const {
                        assert(false && "MediaIOCommand should never be cloned");
                        return nullptr;
                }

                /** @brief Constructs an empty command. */
                MediaIOCommand() = default;

                /** @brief Virtual destructor for polymorphic ownership. */
                virtual ~MediaIOCommand() = default;

                /**
                 * @brief Returns the concrete command kind.
                 * @return The Kind enum value.
                 */
                virtual Kind kind() const = 0;

                /**
                 * @brief Returns the canonical short name for a command kind.
                 *
                 * Used as the prefix for windowed-stat keys
                 * (e.g. @c "Read" + @c "ExecuteDuration" =
                 * @c ReadExecuteDuration).  Stable identifiers — do
                 * not localize.  Unknown values fall back to
                 * @c "Unknown".
                 */
                static const char *kindName(Kind k) {
                        switch (k) {
                                case Open:     return "Open";
                                case Close:    return "Close";
                                case Read:     return "Read";
                                case Write:    return "Write";
                                case Seek:     return "Seek";
                                case Params:   return "Params";
                                case Stats:    return "Stats";
                                case SetClock: return "SetClock";
                        }
                        return "Unknown";
                }

                /** @brief Convenience: @ref kindName for this command's kind. */
                const char *kindName() const { return kindName(kind()); }

                // ---- Cancellation ----

                /**
                 * @brief Cancellation flag — atomic for cross-thread polling.
                 *
                 * Set by @ref MediaIORequest::cancel().  Strategies
                 * read this just before dispatching the command — if
                 * set, the command short-circuits with
                 * @ref Error::Cancelled instead of calling the
                 * backend's @c executeCmd.  Late cancellations
                 * (after dispatch has begun) are ignored on
                 * non-blocking strategies; @c DedicatedThreadMediaIO
                 * uses the flag together with @c cancelBlockingWork()
                 * to interrupt blocking I/O.
                 */
                Atomic<bool> cancelled{false};

                // ---- Strategy hints ----

                /**
                 * @brief Hint for the strategy to prioritize this command.
                 *
                 * When @c true the strategy should expedite the
                 * command — for example, the default
                 * @c SharedThreadMediaIO posts urgent commands to the
                 * front of its strand's queue so a stats poll does not
                 * block behind a deep backlog of real I/O.  Strategies
                 * are free to ignore the hint when their executor has
                 * no notion of priority.  Set by the public API method
                 * that builds the command (today only @ref MediaIO::stats);
                 * backends never touch this.
                 */
                bool urgent = false;

                // ---- Output (filled by the framework after dispatch) ----

                /**
                 * @brief Outcome of @c executeCmd() — set by the framework.
                 *
                 * The strategy class records the @ref Error returned by
                 * the backend's @c executeCmd() invocation here before
                 * calling @ref MediaIO::completeCommand().  Backends
                 * never write to this field directly — they return an
                 * @ref Error from @c executeCmd() and the framework
                 * captures it.  When the command never reaches the
                 * backend (cancelled before dispatch) the strategy sets
                 * this to @ref Error::Cancelled.
                 */
                Error result = Error::Ok;

                /**
                 * @brief Telemetry — what happened on this command run.
                 *
                 * Single @ref MediaIOStats container that serves
                 * both per-command and instance-wide reporting:
                 *
                 *  - For the standard commands (Open / Close / Read
                 *    / Write / Seek / Params) this is per-command
                 *    telemetry.  The framework populates the timing
                 *    keys (@ref MediaIOStats::ExecuteDuration,
                 *    @ref MediaIOStats::QueueWaitDuration)
                 *    automatically; backends may add per-command
                 *    keys (@ref MediaIOStats::BytesProcessed,
                 *    backend-specific) from inside
                 *    @c executeCmd(...).
                 *  - For @ref MediaIOCommandStats this is the
                 *    instance-wide aggregate.  The backend's
                 *    @c executeCmd(MediaIOCommandStats &) populates the
                 *    cumulative keys (@ref MediaIOStats::FramesDropped,
                 *    @ref MediaIOStats::BytesPerSecond, etc.) and
                 *    @ref MediaIO::completeCommand overlays the
                 *    framework-managed standard keys before
                 *    resolution.
                 *
                 * Callers retrieve the snapshot from the resolved
                 * @ref MediaIORequest via @c .stats() — the same
                 * accessor regardless of which command type is
                 * underneath.
                 */
                MediaIOStats stats;

                // ---- Completion synchronization ----
                //
                // Every command carries its own completion latch + a
                // wait condition for blocking waiters and a one-shot
                // continuation callback for asynchronous consumers.
                // @ref MediaIORequest is a thin facade over these
                // primitives — it adds no completion state of its own
                // beyond a shared pointer to this command.

                /**
                 * @brief Marks the command completed and wakes
                 *        every waiter.
                 *
                 * Invoked by @ref MediaIO::completeCommand() after
                 * cache writes and signal emission have run.  Sets
                 * the @c _completed flag, broadcasts the wait
                 * condition so all @ref waitForCompletion() callers
                 * unblock, and fires the registered continuation
                 * callback exactly once (marshalled through the
                 * captured @ref EventLoop when one was supplied).
                 *
                 * Idempotent: a second call is a no-op.  Strategies
                 * may safely re-invoke it on a cancellation path
                 * even if the dispatch path already fired.
                 */
                void markCompleted();

                /**
                 * @brief Blocks until @ref markCompleted has run, or until @p timeoutMs elapses.
                 *
                 * Thread-safe — multiple waiters on different
                 * threads all unblock when the latch fires.
                 * Returns immediately if the command is already
                 * completed.
                 *
                 * @param timeoutMs Maximum time to wait in
                 *                  milliseconds.  A value of @c 0
                 *                  (the default) waits indefinitely
                 *                  — matching @ref WaitCondition's
                 *                  zero-timeout convention.
                 * @return @c Error::Ok when the command completed
                 *         within the budget, @c Error::Timeout when
                 *         the deadline elapsed first.
                 */
                Error waitForCompletion(unsigned int timeoutMs = 0) const;

                /**
                 * @brief True once @ref markCompleted has run.
                 *
                 * Cheap atomic read suitable for non-blocking polls.
                 */
                bool isCompleted() const { return _completed.value(); }

                /**
                 * @brief Registers a one-shot continuation callback.
                 *
                 * Called by @ref MediaIORequest::then().  The
                 * callback fires from @ref markCompleted when the
                 * command resolves, marshalled to @p loop's
                 * @c postCallable when @p loop is non-null and run
                 * synchronously on the resolution thread otherwise.
                 * Re-registering replaces any previously-set
                 * callback; calling after completion fires
                 * immediately.
                 */
                void setCompletionCallback(std::function<void(Error)> cb, EventLoop *loop);

        private:
                Atomic<bool>          _completed{false};
                mutable Mutex         _completionMutex;
                mutable WaitCondition _completionCv;
                Mutex                       _callbackMutex;
                std::function<void(Error)>  _callback;
                EventLoop                  *_callbackLoop = nullptr;
};

/**
 * @brief Command to open a media resource.
 * @ingroup mediaio_backend
 */
class MediaIOCommandOpen : public MediaIOCommand {
                PROMEKI_MEDIAIO_COMMAND(MediaIOCommandOpen, Open)
        public:
                // ---- Inputs ----

                /** @brief Backend configuration the framework opens with. */
                MediaConfig config;
                /** @brief Caller's pre-open MediaDesc hint; backends may treat as required. */
                MediaDesc   pendingMediaDesc;
                /** @brief Caller's pre-open container Metadata hint. */
                Metadata    pendingMetadata;
                /** @brief Caller's pre-open AudioDesc hint. */
                AudioDesc   pendingAudioDesc;
                /** @brief Empty = task default (first video track). */
                List<int>   videoTracks;
                /** @brief Empty = task default (first audio track). */
                List<int>   audioTracks;

                // ---- Outputs ----

                /** @brief Resolved MediaDesc that the framework caches and signals. */
                MediaDesc       mediaDesc;
                /** @brief Resolved AudioDesc that the framework caches. */
                AudioDesc       audioDesc;
                /** @brief Resolved container-level Metadata that the framework caches. */
                Metadata        metadata;
                /** @brief Resolved frame rate (cached on @ref MediaIO and the first port group). */
                FrameRate       frameRate;
                /** @brief True when the resource supports @ref MediaIOCommandSeek. */
                bool            canSeek = false;
                /** @brief Total frame count, or @c FrameCount::unknown / @c FrameCount::infinity. */
                FrameCount      frameCount = FrameCount::unknown();
                /** @brief Backend's preferred default step. */
                int             defaultStep = 1;
                /** @brief Backend's preferred prefetch depth. */
                int             defaultPrefetchDepth = 1;
                /** @brief Backend's preferred write pipeline depth. */
                int             defaultWriteDepth = 4;
                /** @brief Backend's resolution of @ref MediaIO_SeekDefault. */
                MediaIOSeekMode defaultSeekMode = MediaIO_SeekExact;
};

/**
 * @brief Command to close a media resource.
 * @ingroup mediaio_backend
 *
 * No inputs or outputs — close is purely a side-effect operation.
 * The completion @ref Error is reported through @ref MediaIOCommand::result.
 */
class MediaIOCommandClose : public MediaIOCommand {
                PROMEKI_MEDIAIO_COMMAND(MediaIOCommandClose, Close)
};

/**
 * @brief Command to read the next frame.
 * @ingroup mediaio_backend
 */
class MediaIOCommandRead : public MediaIOCommand {
                PROMEKI_MEDIAIO_COMMAND(MediaIOCommandRead, Read)
        public:
                // ---- Inputs ----
                /**
                 * @brief The port group this read targets.
                 *
                 * Set by @ref MediaIOSource when it builds the read
                 * command from the source's owning group.  Backends use
                 * it to know which group's source(s) the produced
                 * frame(s) belong to — for sync-grouped reads this is
                 * the join key for distributing one frame per source in
                 * the group.
                 */
                MediaIOPortGroup *group = nullptr;
                /**
                 * @brief Per-group step at submit time.
                 *
                 * Mirror of @ref MediaIOPortGroup::step copied at
                 * submit time so the backend doesn't have to re-read
                 * the live group state during @c executeCmd
                 * (positive = forward, 0 = repeat / hold,
                 * negative = reverse).
                 */
                int               step = 1;

                // ---- Outputs ----

                /** @brief The frame the backend produced.  Required on @c Error::Ok. */
                Frame::Ptr  frame;
                /** @brief Backend-reported position of @ref frame within the group. */
                FrameNumber currentFrame;

                // ---- Optional outputs (mid-stream descriptor change) ----
                /**
                 * @brief Set true when the backend wants to update the
                 * MediaDesc that MediaIO caches.
                 *
                 * Used for variable-frame-rate or segmented streams whose
                 * format changes mid-playback.  When true, the framework
                 * copies @c updatedMediaDesc into the cache, stamps
                 * @c Metadata::MediaDescChanged on the returned frame,
                 * and emits the @c descriptorChanged signal.
                 */
                bool mediaDescChanged = false;
                /** @brief New MediaDesc — only valid when @c mediaDescChanged is true. */
                MediaDesc updatedMediaDesc;
};

/**
 * @brief Command to write a frame.
 * @ingroup mediaio_backend
 */
class MediaIOCommandWrite : public MediaIOCommand {
                PROMEKI_MEDIAIO_COMMAND(MediaIOCommandWrite, Write)
        public:
                // ---- Inputs ----
                /**
                 * @brief The port group this write targets.
                 *
                 * Set by @ref MediaIOSink::writeFrame from the sink's
                 * owning group.  Backends use it to know which group's
                 * sink(s) the frame(s) feed — for sync-grouped writes
                 * this is the join key for collecting one frame per
                 * sink in the group before dispatching as a batch.
                 */
                MediaIOPortGroup *group = nullptr;

                /**
                 * @brief The originating sink port.
                 *
                 * Set by @ref MediaIOSink::writeFrame so
                 * @ref MediaIO::completeCommand can route per-sink
                 * @c frameWanted / @c writeError signals back to the
                 * specific sink that submitted the write.  Backends
                 * never read this — they receive frames per-group via
                 * @ref group above.
                 */
                MediaIOSink      *sink = nullptr;
                /** @brief The frame to write.  Required. */
                Frame::Ptr        frame;

                // ---- Outputs ----

                /** @brief Backend-reported position of @ref frame after the write. */
                FrameNumber currentFrame;
                /** @brief Backend-reported total frame count after the write. */
                FrameCount  frameCount;
};

/**
 * @brief Command to seek to a frame position.
 * @ingroup mediaio_backend
 */
class MediaIOCommandSeek : public MediaIOCommand {
                PROMEKI_MEDIAIO_COMMAND(MediaIOCommandSeek, Seek)
        public:
                // ---- Inputs ----
                /**
                 * @brief The port group this seek targets.
                 *
                 * Set by @ref MediaIOPortGroup::seekToFrame from the
                 * caller's group.  Backends with multiple groups use
                 * this to dispatch the seek to the correct group's
                 * media; single-group backends ignore it.
                 */
                MediaIOPortGroup *group = nullptr;
                /** @brief Zero-based target frame number. */
                FrameNumber       frameNumber;
                /**
                 * @brief How to interpret @ref frameNumber.
                 *
                 * @ref MediaIO_SeekDefault is resolved to the
                 * backend's preferred mode by the framework before
                 * dispatch — backends never observe the @c Default
                 * value here.
                 */
                MediaIOSeekMode   mode = MediaIO_SeekDefault;

                // ---- Output ----

                /** @brief Backend-reported landing position. */
                FrameNumber currentFrame;
};

/**
 * @brief Backend-specific parameterized command.
 * @ingroup mediaio_backend
 *
 * Carries an arbitrary operation name plus parameter and result
 * containers, allowing backends to expose operations beyond the
 * standard open/close/read/write/seek set.  Examples: setting
 * device gain, querying device temperature, triggering a one-shot
 * capture, retrieving codec parameters.
 *
 * Backends override @c executeCmd(MediaIOCommandParams &) and
 * dispatch on @c name.  Unrecognized names should return
 * @c Error::NotSupported.  Default implementation returns
 * @c Error::NotSupported.
 */
class MediaIOCommandParams : public MediaIOCommand {
                PROMEKI_MEDIAIO_COMMAND(MediaIOCommandParams, Params)
        public:
                // ---- Inputs ----
                String        name;   ///< @brief Operation name (backend-defined).
                MediaIOParams params; ///< @brief Operation parameters.

                // ---- Output ----
                MediaIOParams output; ///< @brief Operation result fields.
};

/**
 * @brief Instance-wide stats query command.
 * @ingroup mediaio_backend
 *
 * Marker subclass with no extra fields — backends populate the
 * cumulative aggregate keys directly into the inherited
 * @ref MediaIOCommand::stats container, and
 * @ref MediaIO::completeCommand overlays the framework-managed
 * standard keys (rate trackers, drop / repeat / late counters,
 * @ref MediaIOStats::PendingOperations) before resolving the
 * request.
 *
 * Backends that want to expose backend-specific aggregate keys
 * override @c executeCmd(MediaIOCommandStats &) and write into
 * @c cmd.stats.  The default implementation is a no-op (returns
 * @c Error::Ok with whatever the framework has already populated).
 */
class MediaIOCommandStats : public MediaIOCommand {
                PROMEKI_MEDIAIO_COMMAND(MediaIOCommandStats, Stats)
};

/**
 * @brief Command to replace the @ref Clock bound to a port group.
 * @ingroup mediaio_backend
 *
 * Built by @ref MediaIOPortGroup::setClock and dispatched through
 * @ref MediaIO::submit.  The backend's
 * @c executeCmd(MediaIOCommandSetClock &) decides whether it can
 * honor the new clock as its timing reference — typically a sender
 * uses the supplied clock to pace outbound frames against an
 * upstream device clock (capture card, AES67, PTP grandmaster).
 *
 * On @c Error::Ok the framework swaps the clock pointer on
 * @ref group inside @ref MediaIO::completeCommand; on any error the
 * existing clock is left in place, so @c group->clock() always
 * reflects what is actually in effect.
 *
 * Default base implementation returns @c Error::NotSupported.  A
 * null @ref clock means "restore the backend's default clock
 * behavior" — backends that override this command interpret a null
 * input as an explicit unbind.
 */
class MediaIOCommandSetClock : public MediaIOCommand {
                PROMEKI_MEDIAIO_COMMAND(MediaIOCommandSetClock, SetClock)
        public:
                // ---- Inputs ----
                /**
                 * @brief The port group whose clock is being replaced.
                 *
                 * Set by @ref MediaIOPortGroup::setClock to @c this.
                 * Backends with multiple groups dispatch on this pointer
                 * to decide which internal pacing reference to update.
                 */
                MediaIOPortGroup *group = nullptr;

                /**
                 * @brief The new clock to bind, or null to restore the
                 *        backend's default behavior.
                 *
                 * Reference-counted so the pointer outlives the
                 * dispatch path even when callers drop their handle
                 * immediately after submitting.
                 */
                Clock::Ptr        clock;
};

PROMEKI_NAMESPACE_END
