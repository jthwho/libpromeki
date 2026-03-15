/**
 * @file      core/threadpool.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <thread>
#include <functional>
#include <type_traits>
#include <promeki/core/namespace.h>
#include <promeki/core/error.h>
#include <promeki/core/mutex.h>
#include <promeki/core/waitcondition.h>
#include <promeki/core/future.h>
#include <promeki/core/list.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief General-purpose thread pool for submitting callable tasks.
 *
 * Tasks are submitted via submit() which returns a Future for the result.
 * The pool manages a set of worker threads that pull tasks from an internal
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
                 * @brief Constructs a ThreadPool with the given number of threads.
                 * @param threadCount Number of worker threads.  Defaults to
                 *        std::thread::hardware_concurrency().  A value of 0
                 *        means tasks run inline on the calling thread.
                 */
                ThreadPool(int threadCount = -1);

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
                                if(_threadCount == 0) {
                                        runInline = true;
                                } else {
                                        _tasks.pushToBack([task]() { (*task)(); });
                                        _cv.wakeOne();
                                }
                        }
                        if(runInline) (*task)();
                        return fut;
                }

                /**
                 * @brief Resizes the thread pool.
                 *
                 * If @p count is less than the current thread count, excess
                 * threads are stopped and joined.  If greater, new threads are
                 * spawned.  A count of 0 means subsequent tasks run inline.
                 *
                 * @param count The new thread count.
                 */
                void setThreadCount(int count);

                /**
                 * @brief Returns the current thread count.
                 * @return The number of worker threads.
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

                void workerFunc();
                void spawnThreads(int count);

                mutable Mutex           _mutex;
                WaitCondition           _cv;
                WaitCondition           _doneCv;
                List<Task>              _tasks;
                List<std::thread>       _threads;
                int                     _threadCount = 0;
                int                     _activeCount = 0;
                bool                    _shutdown = false;
};

PROMEKI_NAMESPACE_END
