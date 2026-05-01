/**
 * @file      sharedthreadmediaio.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/commandmediaio.h>
#include <promeki/strand.h>

PROMEKI_NAMESPACE_BEGIN

class ThreadPool;

/**
 * @brief Strategy class that runs commands on a shared-pool strand.
 * @ingroup mediaio_backend
 *
 * The most common strategy: backends that don't need a dedicated
 * worker thread (compute backends like CSC, SRC, FrameSync, Burn,
 * NullPacing, TPG, DebugMedia, RawBitstream, encoders, decoders)
 * inherit @ref SharedThreadMediaIO and let the framework manage their
 * execution on a per-instance @ref Strand backed by the process-wide
 * @ref SharedThreadMediaIO::pool ThreadPool.
 *
 * Each instance has its own Strand, so different MediaIOs run
 * concurrently while their own commands stay serialized — backends
 * may assume single-threaded callbacks inside their @c executeCmd
 * overrides without any extra synchronization.
 *
 * @par Cancellation
 * Pre-dispatch cancellation works (the cancel flag is checked inside
 * the runner before calling @ref dispatch).  In-flight cancellation
 * is not supported by this strategy — a long-running @c executeCmd
 * always runs to completion.  Backends that need pre-emptive
 * interruption should inherit @ref DedicatedThreadMediaIO instead.
 *
 * @par Per-command telemetry
 * @ref MediaIOStats::QueueWaitDuration and
 * @ref MediaIOStats::ExecuteDuration are populated automatically
 * around the dispatch hook.
 */
class SharedThreadMediaIO : public CommandMediaIO {
                PROMEKI_OBJECT(SharedThreadMediaIO, CommandMediaIO)
        public:
                /** @brief Constructs with optional parent. */
                SharedThreadMediaIO(ObjectBase *parent = nullptr);

                /** @brief Destructor. Waits for the strand to drain before returning. */
                ~SharedThreadMediaIO() override;

                /**
                 * @brief Returns true when the strand has no pending or
                 *        in-flight commands.
                 */
                bool isIdle() const override;

                /**
                 * @brief Drops every queued-but-not-yet-dispatched
                 *        command on the strand.
                 *
                 * Each dropped command resolves with
                 * @c Error::Cancelled via the strand's onCancel hook.
                 */
                void cancelPendingWork() override;

        protected:
                /**
                 * @brief Posts @p cmd to the per-instance strand.
                 *
                 * Honors @ref MediaIOCommand::urgent (urgent commands
                 * jump the queue), checks @ref MediaIOCommand::cancelled
                 * before dispatch, and routes through
                 * @ref MediaIO::completeCommand after.  Per-command
                 * timing telemetry is populated around the dispatch
                 * hook.
                 */
                void submit(MediaIOCommand::Ptr cmd) override;

                /**
                 * @brief Returns a reference to the per-instance strand.
                 *
                 * Available to subclasses that need to schedule
                 * additional work on the same serial executor (e.g.
                 * deferred housekeeping).  Most backends never need
                 * this.
                 */
                Strand &strand() { return _strand; }

                /**
                 * @brief Returns the shared thread pool used by every
                 *        @ref SharedThreadMediaIO instance.
                 *
                 * Each instance has its own @ref Strand on top of this
                 * pool, so different MediaIOs run concurrently while
                 * each instance's own commands stay serialized.
                 *
                 * @par Sizing
                 *
                 * The default thread count is
                 * @c std::thread::hardware_concurrency().  Resize before
                 * opening the first @c SharedThreadMediaIO via:
                 *
                 * @code
                 * SharedThreadMediaIO::pool().setThreadCount(N);
                 * @endcode
                 *
                 * Backends inheriting @ref DedicatedThreadMediaIO have
                 * their own per-instance worker thread and don't share
                 * this pool — sizing it only affects shared-strand
                 * compute backends (CSC, SRC, FrameSync, encoders,
                 * decoders, etc.).
                 *
                 * @return A reference to the static ThreadPool instance.
                 */
                static ThreadPool &pool();

        private:
                Strand _strand{pool()};
};

PROMEKI_NAMESPACE_END
