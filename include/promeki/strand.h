/**
 * @file      strand.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <deque>
#include <functional>
#include <type_traits>
#include <memory>
#include <promeki/namespace.h>
#include <promeki/mutex.h>
#include <promeki/waitcondition.h>
#include <promeki/future.h>
#include <promeki/promise.h>
#include <promeki/threadpool.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Serialized executor backed by a ThreadPool.
 * @ingroup concurrency
 *
 * A Strand provides serial execution semantics: tasks submitted to the
 * same Strand are guaranteed to run one at a time, in submission order.
 * Internally, each task is dispatched as a separate ThreadPool
 * submission, so the underlying pool thread is returned to the pool
 * between tasks rather than being held for the lifetime of the Strand.
 *
 * Multiple Strand instances backed by the same ThreadPool run
 * independently and may execute concurrently on different pool threads.
 *
 * @par Example
 * @code
 * ThreadPool pool;
 * Strand strand(pool);
 *
 * Future<int> a = strand.submit([] { return 1; });
 * Future<int> b = strand.submit([] { return 2; });
 * // 'a' is guaranteed to run before 'b', even though both are submitted
 * // to the pool.  Pool threads are returned between executions.
 * @endcode
 *
 * Non-copyable and non-movable.
 */
class Strand {
        public:
                /** @brief Convenience alias for the void-returning task functions used by submit/cancel. */
                using TaskFunc = std::function<void()>;

                /**
                 * @brief Constructs a Strand backed by the given ThreadPool.
                 * @param pool The pool that will host the strand's tasks.
                 */
                explicit Strand(ThreadPool &pool) : _pool(pool) {}

                /**
                 * @brief Destructor.  Waits for all pending and in-flight
                 *        tasks to complete before returning.
                 *
                 * It is the caller's responsibility to ensure no new tasks
                 * are submitted after the destructor begins.
                 */
                ~Strand() {
                        waitForIdle();
                }

                Strand(const Strand &) = delete;
                Strand &operator=(const Strand &) = delete;
                Strand(Strand &&) = delete;
                Strand &operator=(Strand &&) = delete;

                /**
                 * @brief Submits a callable for serialized execution.
                 *
                 * The callable will be executed on a pool thread, after
                 * any previously-submitted tasks on this Strand have
                 * completed.  Optionally, a cancel callback can be
                 * provided that runs if the task is cancelled before it
                 * has a chance to execute (in addition to setting
                 * Error::Cancelled on the returned Future).  The cancel
                 * callback is useful for releasing resources or
                 * decrementing reference counts that the run callable
                 * would otherwise have handled.
                 *
                 * @tparam F A callable type.
                 * @param callable The callable to execute.
                 * @param onCancel Optional cleanup invoked on cancellation.
                 * @return A Future for the callable's result.
                 */
                template <typename F>
                auto submit(F &&callable,
                            TaskFunc onCancel = {}) -> Future<std::invoke_result_t<F>> {
                        using R = std::invoke_result_t<F>;
                        auto promise = std::make_shared<Promise<R>>();
                        Future<R> future = promise->future();

                        Entry entry;
                        entry.run = [promise, f = std::forward<F>(callable)]() mutable {
                                if constexpr (std::is_void_v<R>) {
                                        f();
                                        promise->setValue();
                                } else {
                                        promise->setValue(f());
                                }
                        };
                        entry.cancel = [promise, onCancel = std::move(onCancel)]() mutable {
                                if(onCancel) onCancel();
                                promise->setError(Error::Cancelled);
                        };

                        bool needSpawn = false;
                        {
                                Mutex::Locker lock(_mutex);
                                _queue.emplace_back(std::move(entry));
                                if(!_running) {
                                        _running = true;
                                        needSpawn = true;
                                }
                        }
                        if(needSpawn) {
                                _pool.submit([this] { runNext(); });
                        }
                        return future;
                }

                /**
                 * @brief Cancels all pending (not-yet-running) tasks.
                 *
                 * Each cancelled task's Future is fulfilled with
                 * Error::Cancelled, so callers waiting on those futures
                 * unblock with an error rather than hanging.  Any task
                 * currently in flight is left to complete normally.
                 *
                 * @return The number of tasks that were cancelled.
                 */
                size_t cancelPending() {
                        std::deque<Entry> toCancel;
                        {
                                Mutex::Locker lock(_mutex);
                                toCancel = std::move(_queue);
                                _queue.clear();
                        }
                        size_t count = toCancel.size();
                        for(auto &entry : toCancel) {
                                entry.cancel();
                        }
                        return count;
                }

                /**
                 * @brief Blocks until the strand has no pending or in-flight tasks.
                 */
                void waitForIdle() {
                        Mutex::Locker lock(_mutex);
                        _idleCv.wait(_mutex, [this] { return !_running; });
                }

                /**
                 * @brief Returns true if the strand is currently processing a task
                 *        or has tasks queued.
                 */
                bool isBusy() const {
                        Mutex::Locker lock(_mutex);
                        return _running;
                }

        private:
                /**
                 * @brief A single queued task with its run + cancel hooks.
                 *
                 * The cancel hook fulfills the task's promise with
                 * Error::Cancelled so any future waiter unblocks cleanly.
                 */
                struct Entry {
                        TaskFunc run;
                        TaskFunc cancel;
                };

                /**
                 * @brief Pool entry point: pops the next task and runs it,
                 *        then either re-submits itself for the next task or
                 *        marks the strand idle if the queue is empty.
                 */
                void runNext() {
                        Entry entry;
                        bool haveTask = false;
                        {
                                Mutex::Locker lock(_mutex);
                                if(!_queue.empty()) {
                                        entry = std::move(_queue.front());
                                        _queue.pop_front();
                                        haveTask = true;
                                }
                        }

                        // Possible if cancelPending() emptied the queue
                        // between spawn and execution.
                        if(haveTask) entry.run();

                        // Either re-submit (more work pending) or mark idle.
                        bool reSpawn = false;
                        {
                                Mutex::Locker lock(_mutex);
                                if(_queue.empty()) {
                                        _running = false;
                                        _idleCv.wakeAll();
                                } else {
                                        reSpawn = true;
                                }
                        }

                        if(reSpawn) {
                                _pool.submit([this] { runNext(); });
                        }
                }

                ThreadPool                              &_pool;
                mutable Mutex                            _mutex;
                WaitCondition                            _idleCv;
                std::deque<Entry>                        _queue;
                bool                                     _running = false;
};

PROMEKI_NAMESPACE_END
