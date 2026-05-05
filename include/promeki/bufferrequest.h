/**
 * @file      bufferrequest.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <functional>
#include <utility>
#include <promeki/namespace.h>
#include <promeki/error.h>
#include <promeki/buffercommand.h>
#include <promeki/eventloop.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Asynchronous handle for a Buffer mapping operation.
 * @ingroup util
 *
 * BufferRequest is the value-type façade returned by every async
 * @ref Buffer entry point — @c Buffer::mapAcquire,
 * @c Buffer::mapRelease, @c Buffer::copyTo.  It mirrors
 * @ref MediaIORequest exactly: the underlying @ref BufferCommand
 * carries the result, while the request adds a uniform
 * @ref wait / @ref isReady / @ref then / @ref cancel interface.
 *
 * @par Lifetime
 * BufferRequest is a value type holding a @c BufferCommand::Ptr (a
 * shared pointer) plus a small @ref Error sentinel for the
 * @ref resolved(Error) factory path.  Copying the handle copies the
 * shared pointer.  The command is freed when every handle goes away;
 * backends that have not yet processed the command keep their own
 * reference until they do.
 *
 * @par Inline vs async resolution
 * Backends that complete inline (e.g. HostBufferImpl, where
 * @c mapAcquire(Host) is a no-op) construct the request via
 * @ref resolved(BufferCommand::Ptr) — the request reports
 * @ref isReady true immediately and @ref then fires the callback
 * synchronously.  Async backends post the command to a worker /
 * strand and return the in-flight handle; the framework's completion
 * machinery (on @ref BufferCommand) handles the wake-up.
 *
 * @par Callback marshalling
 * @ref then captures the calling thread's @ref EventLoop at the time
 * of registration and dispatches the callback through that loop's
 * @c postCallable when the request resolves.  When @ref then is
 * called on a thread without an active @ref EventLoop, the callback
 * runs synchronously on the thread that resolves the request.
 */
class BufferRequest {
        public:
                /** @brief Continuation callback type. */
                using Callback = std::function<void(Error)>;

                /**
                 * @brief Constructs an empty / invalid request.
                 *
                 * @ref wait on an empty request returns
                 * @ref Error::Invalid.  Used as a sentinel for
                 * uninitialized handles.
                 */
                BufferRequest() = default;

                /**
                 * @brief Wraps an existing in-flight @p cmd.
                 *
                 * Backends construct requests this way after building
                 * a command and posting it to their worker.
                 */
                explicit BufferRequest(BufferCommand::Ptr cmd) : _cmd(std::move(cmd)) {}

                /**
                 * @brief Builds a request that is already resolved with @p err.
                 *
                 * Carries no command; @ref command returns an empty
                 * pointer.  Useful for short-circuit returns where
                 * the input itself fails (e.g. @c mapAcquire on an
                 * unsupported domain).
                 */
                static BufferRequest resolved(Error err) {
                        BufferRequest r;
                        r._overrideError = err;
                        r._hasOverride = true;
                        return r;
                }

                /**
                 * @brief Builds a request that is already resolved
                 *        with the result baked into @p cmd.
                 *
                 * Used for short-circuit returns where the backend
                 * can fully service the request inline and wants to
                 * surface a typed output (e.g. host-mapped buffer's
                 * @c BufferMapCommand::hostPtr).  The caller
                 * pre-populates the command's @ref BufferCommand::result
                 * (and any output fields), then this factory marks the
                 * command completed so subsequent @ref wait / @ref then
                 * calls fire immediately.
                 */
                static BufferRequest resolved(BufferCommand::Ptr cmd) {
                        if (cmd.isValid()) cmd.modify()->markCompleted();
                        return BufferRequest(std::move(cmd));
                }

                /**
                 * @brief Blocks until completion or @p timeoutMs elapses.
                 *
                 * Returns @ref Error::Invalid when the request is
                 * empty (default-constructed), @ref Error::Timeout
                 * when the deadline expired before the command
                 * resolved, or the underlying command's
                 * @ref BufferCommand::result on completion.  When
                 * the request was built from @ref resolved(Error),
                 * @p err is returned immediately regardless of
                 * @p timeoutMs.
                 *
                 * @param timeoutMs Maximum time to wait in
                 *                  milliseconds.  A value of 0 (the
                 *                  default) waits indefinitely.
                 */
                Error wait(unsigned int timeoutMs = 0) {
                        if (_hasOverride) return _overrideError;
                        if (_cmd.isNull()) return Error::Invalid;
                        const Error waitErr = _cmd.modify()->waitForCompletion(timeoutMs);
                        if (waitErr == Error::Timeout) return Error::Timeout;
                        return _cmd->result;
                }

                /**
                 * @brief Non-blocking poll.
                 * @return True once @ref wait / @ref then would fire.
                 */
                bool isReady() const {
                        if (_hasOverride) return true;
                        return _cmd.isValid() && _cmd->isCompleted();
                }

                /**
                 * @brief Attaches a continuation.
                 *
                 * Marshalled through the @ref EventLoop active on the
                 * thread that calls @ref then.  When the request is
                 * already resolved, the callback fires immediately
                 * (still marshalled through the captured loop, if
                 * any).  Only one callback may be registered per
                 * request — a second @ref then replaces the first.
                 */
                void then(Callback cb) {
                        if (_hasOverride) {
                                EventLoop *loop = EventLoop::current();
                                Error      err = _overrideError;
                                if (loop != nullptr) {
                                        loop->postCallable(
                                                [cb = std::move(cb), err]() mutable { cb(err); });
                                } else {
                                        cb(err);
                                }
                                return;
                        }
                        if (_cmd.isNull()) return;
                        _cmd.modify()->setCompletionCallback(std::move(cb), EventLoop::current());
                }

                /**
                 * @brief Requests cancellation.
                 *
                 * Idempotent.  Backends poll @ref BufferCommand::cancelled
                 * before dispatching and short-circuit with
                 * @ref Error::Cancelled when set.  Late cancellations
                 * (after dispatch has begun) are best-effort.
                 */
                void cancel() {
                        _cancelledLocal = true;
                        if (_cmd.isValid()) _cmd.modify()->cancelled.setValue(true);
                }

                /** @brief True once @ref cancel has been called on this request. */
                bool isCancelled() const {
                        if (_cancelledLocal) return true;
                        return _cmd.isValid() && _cmd->cancelled.value();
                }

                /**
                 * @brief Returns the underlying command.
                 *
                 * Primary path for callers that need access to a
                 * typed Output field — use @ref commandAs to cast to
                 * the concrete subclass.  Returns an empty pointer
                 * when the request was constructed via
                 * @ref resolved(Error).
                 */
                BufferCommand::Ptr command() const { return _cmd; }

                /**
                 * @brief Returns the underlying command typed as @c CmdT.
                 *
                 * Returns a raw pointer because @ref BufferCommand
                 * uses the @c SharedPtrProxy path, so a shared
                 * dynamic-cast across the hierarchy is not available.
                 * The returned pointer's lifetime is tied to this
                 * BufferRequest handle (or another copy of it).
                 *
                 * Unchecked @c static_cast — callers built the
                 * command knowing its concrete type, so a runtime
                 * mismatch is a programmer error.  Returns
                 * @c nullptr when the request has no command (the
                 * @ref resolved(Error) sentinel path).
                 */
                template <typename CmdT> const CmdT *commandAs() const {
                        if (_cmd.isNull()) return nullptr;
                        return static_cast<const CmdT *>(_cmd.ptr());
                }

                /** @brief True if this request has any internal state. */
                bool isValid() const { return _cmd.isValid() || _hasOverride; }

        private:
                BufferCommand::Ptr _cmd;
                Error              _overrideError = Error::Invalid;
                bool               _hasOverride = false;
                // Sentinel-path cancellation flag — when there is no
                // command, cancel() has nothing to forward to, so the
                // bit lives here.  When _cmd is valid, the cmd's own
                // atomic flag is the source of truth and isCancelled
                // ORs both.
                bool _cancelledLocal = false;
};

PROMEKI_NAMESPACE_END
