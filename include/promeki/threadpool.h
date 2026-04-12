/**
 * @file      threadpool.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <thread>
#include <functional>
#include <type_traits>
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/error.h>
#include <promeki/mutex.h>
#include <promeki/waitcondition.h>
#include <promeki/future.h>
#include <promeki/list.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief General-purpose thread pool for submitting callable tasks.
 * @ingroup concurrency
 *
 * Tasks are submitted via submit() which returns a Future for the result.
 * The pool manages a set of worker threads that pull tasks from an internal
 *
 * By default, worker threads are spawned lazily — the first submit()
 * that finds all existing workers busy spawns a new thread, up to the
 * configured maximum.  Pass @c lazy=false to the constructor (or call
 * setThreadCount()) to pre-spawn all threads immediately.
 *
 * @par Example
 * @code
 * ThreadPool pool(4);  // up to 4 worker threads, spawned on demand
 * Future<int> result = pool.submit([]() {
 *     return expensiveComputation();
 * });
 * int value = result.get();  // blocks until ready
 * @endcode
 * queue.  When the thread count is set to 0, tasks run inline on the
 * calling thread (useful for WASM graceful degradation).
 *
 * All public methods are thread-safe.
 *
 * Non-copyable and non-movable.
 */
class ThreadPool {
        public:
                /**
                 * @brief Constructs a ThreadPool with the given maximum thread count.
                 *
                 * @param maxThreadCount Maximum number of worker threads.
                 *        Defaults to std::thread::hardware_concurrency().
                 *        A value of 0 means tasks run inline on the calling
                 *        thread.
                 * @param lazy When @c true (default), threads are spawned on
                 *        demand as tasks arrive.  When @c false, all threads
                 *        are pre-spawned immediately.
                 */
                ThreadPool(int maxThreadCount = -1, bool lazy = true);

                /**
                 * @brief Destructor.  Signals shutdown and joins all worker threads.
                 */
                ~ThreadPool();

                ThreadPool(const ThreadPool &) = delete;
                ThreadPool &operator=(const ThreadPool &) = delete;
                ThreadPool(ThreadPool &&) = delete;
                ThreadPool &operator=(ThreadPool &&) = delete;

                /**
                 * @brief Submits a callable for asynchronous execution.
                 *
                 * If the thread count is 0, the callable is executed immediately
                 * on the calling thread.
                 *
                 * @tparam F A callable type.
                 * @param callable The callable to execute.
                 * @return A Future whose result type matches the callable's return type.
                 */
                template <typename F>
                auto submit(F &&callable) -> Future<std::invoke_result_t<F>> {
                        using R = std::invoke_result_t<F>;
                        auto task = std::make_shared<std::packaged_task<R()>>(
                                std::forward<F>(callable));
                        Future<R> fut(task->get_future());
                        bool runInline = false;
                        {
                                Mutex::Locker locker(_mutex);
                                if(_maxThreadCount == 0) {
                                        runInline = true;
                                } else {
                                        _tasks.pushToBack([task]() { (*task)(); });
                                        maybeSpawnOne();
                                        _cv.wakeOne();
                                }
                        }
                        if(runInline) (*task)();
                        return fut;
                }

                /**
                 * @brief Sets the name prefix for worker threads.
                 *
                 * Each worker is named @c prefix0, @c prefix1, etc.  The
                 * name is applied to the OS thread (visible in debuggers,
                 * @c htop, @c ps&nbsp;-L).  Must be called before threads
                 * are spawned to take effect on all workers.
                 *
                 * @param prefix The name prefix (e.g. "media").
                 */
                void setNamePrefix(const String &prefix);

                /**
                 * @brief Returns the current name prefix.
                 * @return The name prefix, or an empty string if none.
                 */
                String namePrefix() const;

                /**
                 * @brief Resizes the thread pool.
                 *
                 * Shuts down all existing threads, then respawns up to
                 * @p count threads.  When @p lazy is true (default), no
                 * threads are spawned until tasks arrive.  A count of 0
                 * means subsequent tasks run inline.
                 *
                 * @param count The new maximum thread count.
                 * @param lazy  When true, threads are spawned on demand.
                 */
                void setThreadCount(int count, bool lazy = true);

                /**
                 * @brief Returns the maximum thread count.
                 * @return The maximum number of worker threads.
                 */
                int maxThreadCount() const;

                /**
                 * @brief Returns the current number of spawned threads.
                 * @return The number of live worker threads.
                 */
                int threadCount() const;

                /**
                 * @brief Returns the number of threads currently executing tasks.
                 * @return The active thread count.
                 */
                int activeThreadCount() const;

                /**
                 * @brief Blocks until all submitted tasks have completed.
                 */
                void waitForDone();

                /**
                 * @brief Blocks until all submitted tasks have completed or the timeout expires.
                 * @param timeoutMs Maximum time to wait in milliseconds.
                 * @return Error::Ok if all tasks completed, Error::Timeout if the timeout elapsed.
                 */
                Error waitForDone(unsigned int timeoutMs);

                /**
                 * @brief Removes all pending (not yet running) tasks from the queue.
                 */
                void clear();

        private:
                using Task = std::function<void()>;

                void workerFunc(int index);
                void spawnThreads(int count);
                void maybeSpawnOne();

                mutable Mutex           _mutex;
                WaitCondition           _cv;
                WaitCondition           _doneCv;
                List<Task>              _tasks;
                List<std::thread>       _threads;
                String                  _namePrefix;
                int                     _maxThreadCount = 0;
                int                     _threadCount = 0;
                int                     _activeCount = 0;
                int                     _waitingCount = 0;
                bool                    _shutdown = false;
};

PROMEKI_NAMESPACE_END
