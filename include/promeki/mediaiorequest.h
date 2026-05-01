/**
 * @file      mediaiorequest.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <functional>
#include <utility>
#include <promeki/namespace.h>
#include <promeki/error.h>
#include <promeki/eventloop.h>
#include <promeki/mediaiocommand.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Asynchronous handle for a MediaIO operation.
 * @ingroup mediaio_user
 *
 * Every public operation on @ref MediaIO, @ref MediaIOSource,
 * @ref MediaIOSink, and @ref MediaIOPortGroup that goes through the
 * strand returns a @ref MediaIORequest — a thin facade over the
 * underlying @ref MediaIOCommand.  The request adds no completion
 * state of its own; it just forwards @ref wait /
 * @ref isReady / @ref then / @ref cancel to the command's own
 * @ref MediaIOCommand::waitForCompletion /
 * @ref MediaIOCommand::isCompleted /
 * @ref MediaIOCommand::setCompletionCallback /
 * @ref MediaIOCommand::cancelled hooks.
 *
 * @par Why non-templated
 * Every command carries an @ref Error in @ref MediaIOCommand::result,
 * so the request just proxies that — there is one canonical
 * "outcome" type and no need to template over a payload.  Typed
 * Output fields (e.g. @ref MediaIOCommandRead::frame,
 * @ref MediaIOCommandSeek::currentFrame,
 * @ref MediaIOCommandParams::output) live on the concrete command
 * subclass and are reached via @c commandAs<MediaIOCommandRead>().
 *
 * @par Lifetime
 * @ref MediaIORequest is a value type — it holds a
 * @ref MediaIOCommand::Ptr (a shared pointer) plus a small Error
 * sentinel for the @c resolved(Error) factory path.  Copying the
 * handle copies the shared pointer.  When every handle goes away,
 * the command is freed (the strand keeps its own reference until
 * the command runs, so dropping every caller-side handle is safe).
 *
 * @par Cancellation
 * @ref cancel() is best-effort and has three observable behaviors
 * depending on lifecycle stage:
 *
 *  1. **Not yet dispatched** — the strategy completes the command
 *     immediately with @ref Error::Cancelled; @ref wait returns
 *     the cancellation.  The backend's @c executeCmd is never
 *     called.
 *  2. **Dispatched and executing** on a non-blocking strategy
 *     (@c InlineMediaIO, @c SharedThreadMediaIO) — the strategy
 *     ignores late cancellation requests and lets the command
 *     complete normally.  Cancellation is not preemptive on
 *     shared-pool workers because doing so would require backend
 *     cooperation we cannot rely on.
 *  3. **Dispatched and executing** on @c DedicatedThreadMediaIO —
 *     the strategy invokes the backend's @c cancelBlockingWork()
 *     virtual on a thread distinct from the worker.  Backends
 *     override @c cancelBlockingWork() to interrupt syscalls
 *     (close fds, signal condvars, set a shutdown flag, etc.).
 *
 * @par Callback marshalling
 * @ref then captures the calling thread's @ref EventLoop at the
 * time of registration and dispatches the callback via that loop's
 * @c postCallable when the request resolves.  When @ref then is
 * called on a thread without an active @ref EventLoop, the callback
 * runs synchronously on the thread that resolves the request (the
 * strategy worker thread).  When @ref then is called after the
 * request has already resolved, the callback fires immediately
 * (still marshalled through the captured loop, if any).
 */
class MediaIORequest {
        public:
                /** @brief Continuation callback type. */
                using Callback = std::function<void(Error)>;

                /**
                 * @brief Constructs an empty / invalid request.
                 *
                 * @ref wait on an empty request returns
                 * @c Error::Invalid.  Used as a sentinel for
                 * uninitialized handles.
                 */
                MediaIORequest() = default;

                /**
                 * @brief Wraps an existing in-flight @p cmd.
                 *
                 * The framework constructs requests this way after
                 * building a command and handing it to @ref MediaIO::submit.
                 * Callers do not normally instantiate requests
                 * themselves.
                 */
                explicit MediaIORequest(MediaIOCommand::Ptr cmd) : _cmd(std::move(cmd)) {}

                /**
                 * @brief Builds a request that is already resolved with @p err.
                 *
                 * Carries no command; @ref command returns an empty
                 * pointer.  Useful for short-circuit returns from
                 * the public API where the input itself fails (e.g.
                 * @c open() on an already-open MediaIO).
                 */
                static MediaIORequest resolved(Error err);

                /**
                 * @brief Builds a request that is already resolved
                 *        with the result baked into @p cmd.
                 *
                 * Useful for short-circuit returns where the public
                 * API can fully service the request without going
                 * through the strand and wants to surface a typed
                 * output.  The caller pre-populates @p cmd's
                 * @ref MediaIOCommand::result (and any Output
                 * fields), then this factory marks the command
                 * completed so subsequent @ref wait / @ref then
                 * calls fire immediately.
                 */
                static MediaIORequest resolved(MediaIOCommand::Ptr cmd);

                /**
                 * @brief Blocks until completion or @p timeoutMs elapses.
                 *
                 * Returns @c Error::Invalid when the request is
                 * empty (default-constructed), @c Error::Timeout
                 * when the deadline expired before the command
                 * resolved, or the underlying command's
                 * @ref MediaIOCommand::result on completion.  When
                 * the request was built from
                 * @c resolved(Error err) (no command), @p err is
                 * returned immediately regardless of @p timeoutMs.
                 *
                 * @param timeoutMs Maximum time to wait in
                 *                  milliseconds.  A value of @c 0
                 *                  (the default) waits indefinitely.
                 */
                Error wait(unsigned int timeoutMs = 0);

                /**
                 * @brief Non-blocking poll.
                 * @return True once @ref wait / @ref then would fire.
                 */
                bool isReady() const;

                /**
                 * @brief Attaches a continuation.
                 *
                 * Marshalled through the @ref EventLoop active on
                 * the thread that calls @ref then.  When the
                 * request is already resolved the callback fires
                 * immediately (still marshalled through the
                 * captured loop, if any).  Only one callback may
                 * be registered per request — a second @ref then
                 * replaces the first.
                 *
                 * @param cb The continuation to invoke on
                 *           resolution.
                 */
                void then(Callback cb);

                /**
                 * @brief Requests cancellation.
                 *
                 * Idempotent.  See the class doc for the
                 * three-state behavior depending on lifecycle
                 * stage.
                 */
                void cancel();

                /** @brief True once @ref cancel has been called on this request. */
                bool isCancelled() const;

                /**
                 * @brief Returns the underlying command.
                 *
                 * The primary path for callers that need access to
                 * a typed Output field — use @ref commandAs to cast
                 * to the concrete subclass.  Returns an empty
                 * pointer when the request was constructed via
                 * @c resolved(Error).
                 */
                MediaIOCommand::Ptr command() const { return _cmd; }

                /**
                 * @brief Returns the underlying command typed as @c CmdT.
                 *
                 * Returns a raw pointer because @ref MediaIOCommand
                 * uses the @c SharedPtrProxy path, so a shared
                 * dynamic-cast across the hierarchy isn't
                 * available.  The returned pointer's lifetime is
                 * tied to this @ref MediaIORequest handle (or
                 * another copy of it) — keep a request alive as
                 * long as the pointer is in use.
                 *
                 * Unchecked @c static_cast — callers built the
                 * command knowing its concrete type, so a runtime
                 * mismatch is a programmer error rather than a
                 * recoverable condition.  Returns @c nullptr when
                 * the request has no command (the
                 * @c resolved(Error) sentinel path).
                 *
                 * @code
                 * MediaIORequest req = source->readFrame();
                 * if (req.wait().isOk()) {
                 *     Frame::Ptr frame = req.commandAs<MediaIOCommandRead>()->frame;
                 * }
                 * @endcode
                 */
                template <typename CmdT> const CmdT *commandAs() const {
                        if (_cmd.isNull()) return nullptr;
                        return static_cast<const CmdT *>(_cmd.ptr());
                }

                /**
                 * @brief Telemetry snapshot for this request.
                 *
                 * Returns the @ref MediaIOStats container the
                 * framework + backend populated for this command.
                 * Same accessor for both contexts:
                 *
                 *  - For standard commands (open/close/read/write/
                 *    seek/params): per-command keys (dispatch
                 *    duration, queue wait, bytes processed, plus
                 *    any backend-specific keys).
                 *  - For @ref MediaIOCommandStats: instance-wide
                 *    cumulative aggregate (frames dropped, bytes
                 *    per second, queue depth, etc.).
                 *
                 * Returns an empty container when the request has
                 * no command attached (the @c resolved(Error)
                 * sentinel path).
                 */
                MediaIOStats stats() const { return _cmd.isValid() ? _cmd->stats : MediaIOStats(); }

                /** @brief True if this request has internal state. */
                bool isValid() const { return _cmd.isValid() || _hasOverride; }

        private:
                MediaIOCommand::Ptr _cmd;
                Error               _overrideError = Error::Invalid;
                bool                _hasOverride = false;
                // Sentinel-path cancellation flag — when there is no
                // command, cancel() has nothing to forward to, so the
                // bit lives here.  When _cmd is valid the cmd's own
                // atomic flag is the source of truth and isCancelled
                // ORs both.
                bool _cancelledLocal = false;
};

inline MediaIORequest MediaIORequest::resolved(Error err) {
        MediaIORequest r;
        r._overrideError = err;
        r._hasOverride = true;
        return r;
}

inline MediaIORequest MediaIORequest::resolved(MediaIOCommand::Ptr cmd) {
        if (cmd.isValid()) cmd.modify()->markCompleted();
        return MediaIORequest(std::move(cmd));
}

inline Error MediaIORequest::wait(unsigned int timeoutMs) {
        if (_hasOverride) return _overrideError;
        if (_cmd.isNull()) return Error::Invalid;
        const Error waitErr = _cmd.modify()->waitForCompletion(timeoutMs);
        if (waitErr == Error::Timeout) return Error::Timeout;
        return _cmd->result;
}

inline bool MediaIORequest::isReady() const {
        if (_hasOverride) return true;
        return _cmd.isValid() && _cmd->isCompleted();
}

inline void MediaIORequest::then(Callback cb) {
        if (_hasOverride) {
                // Sentinel path: fire immediately (marshal through
                // the current loop when one is active so the
                // callback observes the same thread semantics as
                // the post-completion case).
                EventLoop *loop = EventLoop::current();
                Error      err = _overrideError;
                if (loop != nullptr) {
                        loop->postCallable([cb = std::move(cb), err]() mutable { cb(err); });
                } else {
                        cb(err);
                }
                return;
        }
        if (_cmd.isNull()) return;
        _cmd.modify()->setCompletionCallback(std::move(cb), EventLoop::current());
}

inline void MediaIORequest::cancel() {
        _cancelledLocal = true;
        if (_cmd.isValid()) _cmd.modify()->cancelled.setValue(true);
}

inline bool MediaIORequest::isCancelled() const {
        if (_cancelledLocal) return true;
        return _cmd.isValid() && _cmd->cancelled.value();
}

PROMEKI_NAMESPACE_END
