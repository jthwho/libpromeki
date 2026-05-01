/**
 * @file      dedicatedthreadmediaio.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/atomic.h>
#include <promeki/commandmediaio.h>
#include <promeki/list.h>
#include <promeki/mutex.h>
#include <promeki/thread.h>
#include <promeki/timestamp.h>
#include <promeki/waitcondition.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Strategy class that runs commands on a dedicated worker thread.
 * @ingroup mediaio_backend
 *
 * Used by backends whose @c executeCmd may block on syscalls or
 * external I/O without relief — V4L2, QuickTime, AudioFile, ImageFile,
 * RTP, MjpegStream.  Each instance owns one worker thread and one
 * command queue; @ref submit enqueues, the worker dequeues and runs
 * @ref dispatch + @ref MediaIO::completeCommand.
 *
 * @par Why dedicate a thread?
 * @ref SharedThreadMediaIO posts onto the process-wide thread pool
 * via a @ref Strand.  When many file or device backends are active
 * simultaneously, all of them blocking inside @c executeCmd can
 * starve the pool — every pool thread sits in a syscall and there
 * are none left to service unrelated MediaIOs.  Dedicated threads
 * decouple per-backend blocking from the pool's parallelism budget.
 *
 * @par Cancellation
 * Pre-dispatch cancellation works (the cancel flag is checked inside
 * the worker before calling @ref dispatch).  Backends that want
 * pre-emptive interruption of an in-flight blocking syscall override
 * @ref MediaIO::cancelBlockingWork; the framework calls it from a
 * non-worker thread so backends must treat it as an asynchronous
 * signal (typically by setting a thread-safe atomic that the
 * blocking call checks).
 *
 * @par Urgency
 * Commands marked @ref MediaIOCommand::urgent jump the head of the
 * queue — useful for stats polls that should not sit behind a deep
 * backlog of read/write work.
 */
class DedicatedThreadMediaIO : public CommandMediaIO {
                PROMEKI_OBJECT(DedicatedThreadMediaIO, CommandMediaIO)
        public:
                /**
                 * @brief Constructs and starts the worker thread.
                 *
                 * The worker is spawned in the constructor so the
                 * MediaIO is ready to accept @ref submit immediately.
                 */
                DedicatedThreadMediaIO(ObjectBase *parent = nullptr);

                /**
                 * @brief Destructor.  Drains the queue and joins the
                 *        worker before returning.
                 */
                ~DedicatedThreadMediaIO() override;

                /** @brief True when the queue is empty and the worker is idle. */
                bool isIdle() const override;

        protected:
                /**
                 * @brief Posts @p cmd to the worker queue.
                 *
                 * Honors @ref MediaIOCommand::urgent (urgent commands
                 * jump the head of the queue) and checks
                 * @ref MediaIOCommand::cancelled before dispatch.
                 * Per-command timing telemetry is populated around the
                 * dispatch hook.
                 */
                void submit(MediaIOCommand::Ptr cmd) override;

        private:
                struct QueueEntry {
                                MediaIOCommand::Ptr cmd;
                                TimeStamp           submitTime;
                };

                // Private nested @ref Thread subclass so we get the
                // library's OS-name / priority / affinity controls
                // without dragging an EventLoop into the worker (the
                // override of @c run drains the queue directly).
                class Worker : public Thread {
                                public:
                                        Worker(DedicatedThreadMediaIO *owner) : _owner(owner) {}

                                protected:
                                        void run() override { _owner->workerMain(); }

                                private:
                                        DedicatedThreadMediaIO *_owner;
                };

                void workerMain();

                mutable Mutex            _mutex;
                WaitCondition            _cond;
                List<QueueEntry>         _queue;
                List<QueueEntry>         _urgentQueue;
                Atomic<bool>             _shutdown{false};
                Atomic<bool>             _busy{false};
                Worker                   _worker;
};

PROMEKI_NAMESPACE_END
