/**
 * @file      buffercommand.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cassert>
#include <functional>
#include <promeki/namespace.h>
#include <promeki/error.h>
#include <promeki/sharedptr.h>
#include <promeki/atomic.h>
#include <promeki/mutex.h>
#include <promeki/waitcondition.h>
#include <promeki/memdomain.h>
#include <promeki/buffermapflags.h>

PROMEKI_NAMESPACE_BEGIN

class EventLoop;

/**
 * @brief Boilerplate macro for concrete BufferCommand subclasses.
 * @ingroup util
 *
 * Injects the @c kind() override and an asserting @c _promeki_clone
 * required by the SharedPtr proxy machinery.  Use it as the first
 * line of every concrete BufferCommand class.
 *
 * @param NAME      The class name.
 * @param KIND_TAG  The BufferCommand::Kind enum value.
 */
#define PROMEKI_BUFFER_COMMAND(NAME, KIND_TAG)                                                                         \
public:                                                                                                                \
        Kind kind() const override {                                                                                   \
                return KIND_TAG;                                                                                       \
        }                                                                                                              \
        NAME *_promeki_clone() const {                                                                                 \
                assert(false && #NAME " should not be cloned");                                                        \
                return nullptr;                                                                                        \
        }

/**
 * @brief Base class for asynchronous Buffer operations.
 * @ingroup util
 *
 * BufferCommand mirrors the @ref MediaIOCommand shape: each
 * outstanding async operation on a @ref Buffer (mapAcquire,
 * mapRelease, copyTo) is represented by a concrete BufferCommand
 * subclass that carries inputs (set by the public Buffer API) and
 * outputs (set by the BufferImpl backend).  A @ref BufferRequest is
 * the value-type façade callers see.
 *
 * @par Lifecycle
 *  1. **Built** by a @ref Buffer public API method.  Inputs (target
 *     domain, flags) are populated; @c result is initialized to
 *     @ref Error::Ok.  A @ref BufferRequest is constructed from the
 *     command's @c Ptr.
 *  2. **Dispatched** by the backend.  HostBufferImpl completes
 *     inline — populates outputs and calls @ref markCompleted before
 *     returning.  Async backends (CUDA-device, FPGA, RDMA) post the
 *     command to a strand or worker thread and return the in-flight
 *     handle to the caller.
 *  3. **Completed** by the backend calling @ref markCompleted.
 *     Wakes every blocked waiter and fires any registered
 *     continuation callback.
 *
 * Commands are shared via @c SharedPtr<BufferCommand, false> with
 * COW disabled — concrete commands are never cloned.
 *
 * @par Thread Safety
 * The completion latch and callback slot are internally synchronized
 * — @ref markCompleted, @ref waitForCompletion, and
 * @ref setCompletionCallback are safe to call from any thread.
 * The Inputs / Outputs on concrete subclasses are written by exactly
 * one producer (the backend) and read by exactly one consumer (the
 * caller, after the request resolves); no synchronization is required
 * for those fields beyond the happens-before that completion provides.
 */
class BufferCommand {
        public:
                /** @brief Concrete command kind. */
                enum Kind {
                        Map,   ///< @ref BufferMapCommand
                        Unmap, ///< @ref BufferUnmapCommand
                        Copy,  ///< @ref BufferCopyCommand
                };

                /** @brief Shared pointer type for BufferCommand. */
                using Ptr = SharedPtr<BufferCommand, false>;

                /**
                 * @internal
                 * @brief Reference count (managed by SharedPtrProxy).
                 */
                RefCount _promeki_refct;

                /**
                 * @brief Clone hook required by SharedPtr machinery.
                 *
                 * Commands are never cloned (COW is disabled).  Asserts
                 * in debug builds to catch accidental clone calls.
                 */
                BufferCommand *_promeki_clone() const {
                        assert(false && "BufferCommand should never be cloned");
                        return nullptr;
                }

                BufferCommand() = default;
                virtual ~BufferCommand() = default;

                /** @brief Returns the concrete command kind. */
                virtual Kind kind() const = 0;

                /**
                 * @brief Returns the canonical short name for a kind.
                 *
                 * Stable identifiers — do not localize.  Unknown
                 * values fall back to @c "Unknown".
                 */
                static const char *kindName(Kind k);

                /** @brief Convenience: @ref kindName for this command's kind. */
                const char *kindName() const { return kindName(kind()); }

                // ---- Cancellation ----

                /**
                 * @brief Cancellation flag — atomic for cross-thread polling.
                 *
                 * Set by @ref BufferRequest::cancel.  Backends read
                 * this just before dispatching the command; if set,
                 * the backend should short-circuit with
                 * @ref Error::Cancelled instead of performing the
                 * operation.  Late cancellations (after dispatch has
                 * begun) are best-effort — backends that cannot
                 * interrupt their work let it finish.
                 */
                Atomic<bool> cancelled{false};

                // ---- Output (filled by the backend) ----

                /**
                 * @brief Outcome of the command.
                 *
                 * The backend writes the final @ref Error here before
                 * calling @ref markCompleted.  Defaults to
                 * @ref Error::Ok so backends that complete inline only
                 * have to set the field on the failure path.
                 */
                Error result = Error::Ok;

                // ---- Completion ----

                /**
                 * @brief Marks the command completed and wakes every waiter.
                 *
                 * Idempotent: a second call is a no-op.  The
                 * registered continuation callback (if any) fires
                 * exactly once, marshalled through the captured
                 * @ref EventLoop when one was supplied.
                 */
                void markCompleted();

                /**
                 * @brief Blocks until @ref markCompleted runs, or until @p timeoutMs elapses.
                 * @param timeoutMs Maximum time to wait in milliseconds.
                 *                  A value of 0 (the default) waits
                 *                  indefinitely.
                 * @return @ref Error::Ok when the command completed
                 *         within the budget, @ref Error::Timeout when
                 *         the deadline elapsed first.
                 */
                Error waitForCompletion(unsigned int timeoutMs = 0) const;

                /** @brief True once @ref markCompleted has run. */
                bool isCompleted() const { return _completed.value(); }

                /**
                 * @brief Registers a one-shot continuation callback.
                 *
                 * The callback fires from @ref markCompleted, marshalled
                 * to @p loop's @c postCallable when @p loop is non-null
                 * and run synchronously on the resolution thread
                 * otherwise.  Re-registering replaces any previously-set
                 * callback; calling after completion fires immediately.
                 */
                void setCompletionCallback(std::function<void(Error)> cb, EventLoop *loop);

        private:
                Atomic<bool>               _completed{false};
                mutable Mutex              _completionMutex;
                mutable WaitCondition      _completionCv;
                Mutex                      _callbackMutex;
                std::function<void(Error)> _callback;
                EventLoop                 *_callbackLoop = nullptr;
};

/**
 * @brief Command to map a Buffer for access from a specific MemDomain.
 * @ingroup util
 *
 * Built by @c Buffer::mapAcquire.  The backend resolves the request
 * by ensuring the buffer is accessible to @ref target, populating any
 * domain-specific output, and calling @ref markCompleted.  When
 * @ref target is @ref MemDomain::Host, @ref hostPtr is the pointer
 * the caller can dereference until a matching @ref BufferUnmapCommand
 * runs.
 *
 * Backends that expose richer per-domain outputs (an FPGA buffer
 * index, an RDMA region key, a CUDA stream handle) subclass
 * BufferMapCommand and add the field there; callers reach the typed
 * field via @c BufferRequest::commandAs.
 */
class BufferMapCommand : public BufferCommand {
                PROMEKI_BUFFER_COMMAND(BufferMapCommand, Map)
        public:
                // ---- Inputs ----

                /** @brief Target domain to make the buffer accessible to. */
                MemDomain target;
                /** @brief Access mode requested for the mapping. */
                MapFlags  flags = MapFlags::Read;

                // ---- Outputs ----

                /**
                 * @brief Host pointer when @ref target is @ref MemDomain::Host.
                 *
                 * Populated on success; remains nullptr on error or
                 * for non-host target domains.
                 */
                void *hostPtr = nullptr;
};

/**
 * @brief Command to copy a byte range from one Buffer to another.
 * @ingroup util
 *
 * Built by @c Buffer::copyTo.  The framework resolves the inter-
 * MemSpace path through the @ref registerBufferCopy registry; if no
 * specialized copy is registered for the @c (srcMemSpace, dstMemSpace)
 * pair, the framework falls back to a host-side memcpy when both
 * ends are currently host-mapped, and returns @ref Error::NotSupported
 * otherwise.  The command carries the bytes and source / destination
 * offsets that the registered function needs to satisfy the
 * transfer.
 */
class BufferCopyCommand : public BufferCommand {
                PROMEKI_BUFFER_COMMAND(BufferCopyCommand, Copy)
        public:
                // ---- Inputs ----

                /** @brief Number of bytes to copy. */
                size_t bytes = 0;
                /** @brief Source offset in bytes from the source buffer's data() base. */
                size_t srcOffset = 0;
                /** @brief Destination offset in bytes from the destination buffer's data() base. */
                size_t dstOffset = 0;
};

/**
 * @brief Command to release a previously-acquired domain mapping.
 * @ingroup util
 *
 * Built by @c Buffer::mapRelease.  The backend decrements the
 * per-domain refcount and, when the count drops to zero, performs
 * any required write-back / unmap.  When the buffer was never mapped
 * to @ref target, the backend completes the command with
 * @ref Error::Invalid.
 */
class BufferUnmapCommand : public BufferCommand {
                PROMEKI_BUFFER_COMMAND(BufferUnmapCommand, Unmap)
        public:
                // ---- Inputs ----

                /** @brief Domain the caller is releasing. */
                MemDomain target;
};

PROMEKI_NAMESPACE_END
