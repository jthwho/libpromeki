/**
 * @file      threadpool.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_CORE
#include <thread>
#include <functional>
#include <memory>
#include <type_traits>
#include <promeki/function.h>
#include <promeki/namespace.h>
#include <promeki/atomic.h>
#include <promeki/duration.h>
#include <promeki/string.h>
#include <promeki/error.h>
#include <promeki/hashmap.h>
#include <promeki/mutex.h>
#include <promeki/readwritelock.h>
#include <promeki/stringregistry.h>
#include <promeki/timestamp.h>
#include <promeki/uniqueptr.h>
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
 * Non-copyable and non-movable.
 *
 * @par Thread Safety
 * Fully thread-safe.  @c submit, @c setThreadCount,
 * @c setThreadNamePrefix, and the introspection accessors may be
 * called concurrently from any thread.  Submitted callables run on
 * worker threads; their interaction with shared state is the
 * caller's responsibility.
 */
class ThreadPool {
        public:
                /**
                 * @brief Interned identifier for a category of work
                 *        submitted to this pool.
                 *
                 * Backed by the shared @ref StringRegistry pattern so
                 * each distinct name (e.g. @c "RtpMediaIO",
                 * @c "CscMediaIO", @c "RtpMediaIO[name=mySink]")
                 * receives a stable integer id that the worker
                 * accumulates wall-time / CPU-time / count totals
                 * against.  Untagged tasks (the old @ref submit
                 * overload, or any caller passing
                 * @c WorkTag()) fold into the synthetic
                 * @c "(untagged)" bucket.
                 *
                 * Tags are cheap to copy (an aligned 64-bit id) and
                 * suitable for compile-time constants via
                 * @c WorkTag::literal("…"), or for runtime
                 * registration via the @c WorkTag(const String&)
                 * constructor.  Reverse lookup (@c WorkTag::name)
                 * returns an empty string for @c literal()-only
                 * tags — the snapshot path falls back to the raw id
                 * in that case so stats are always identifiable.
                 */
                using WorkTag = StringRegistry<"ThreadPoolWorkTag">::Item;

                /**
                 * @brief One row of @ref snapshotWorkStats output.
                 *
                 * Cumulative counters covering every task observed
                 * since the pool was created or @ref resetWorkStats
                 * was last called, grouped by @ref WorkTag.
                 */
                struct WorkStats {
                                WorkTag  tag;          ///< The tag this row aggregates.
                                String   name;         ///< Resolved registry name (or "(untagged)" / "id:N").
                                Duration totalWall;    ///< Sum of wall-clock execute durations.
                                Duration totalCpu;     ///< Sum of @c CLOCK_THREAD_CPUTIME_ID deltas.
                                Duration totalQueueWait; ///< Sum of (dispatch − enqueue) durations.
                                int64_t  count = 0;    ///< Number of tasks observed.
                };

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
                 * @brief Submits an untagged callable for asynchronous execution.
                 *
                 * Equivalent to @ref submit(WorkTag, F&&) with an
                 * invalid (untagged) @ref WorkTag — task time
                 * accumulates under the synthetic @c "(untagged)"
                 * row in @ref snapshotWorkStats.  Callers that want
                 * per-category attribution should use the tagged
                 * overload (typically via @ref Strand which does
                 * the tagging on their behalf).
                 *
                 * If the thread count is 0, the callable is executed immediately
                 * on the calling thread.
                 *
                 * @tparam F A callable type.
                 * @param callable The callable to execute.
                 * @return A Future whose result type matches the callable's return type.
                 */
                template <typename F> auto submit(F &&callable) -> Future<std::invoke_result_t<F>> {
                        return submit(WorkTag(), std::forward<F>(callable));
                }

                /**
                 * @brief Submits a tagged callable for asynchronous execution.
                 *
                 * Wall-clock and per-thread CPU-time deltas around
                 * the callable are accumulated into a per-tag
                 * @ref WorkStats record.  Time the task spends
                 * waiting in the queue between submission and
                 * dispatch is captured as @c totalQueueWait.
                 *
                 * If the pool has zero threads the callable runs
                 * inline on the calling thread; the @c totalCpu /
                 * @c totalWall counters still update so synchronous
                 * pools (e.g. WASM) report consistent numbers.
                 *
                 * @tparam F A callable type.
                 * @param tag      Identifier for this category of work.
                 * @param callable The callable to execute.
                 * @return A Future whose result type matches the callable's return type.
                 */
                template <typename F> auto submit(WorkTag tag, F &&callable) -> Future<std::invoke_result_t<F>> {
                        using R = std::invoke_result_t<F>;
                        auto      task = std::make_shared<std::packaged_task<R()>>(std::forward<F>(callable));
                        Future<R> fut(task->get_future());
                        bool      runInline = false;
                        TaggedTask entry;
                        entry.tag = tag;
                        entry.enqueuedAt = TimeStamp::now();
                        entry.callable = [task]() { (*task)(); };
                        {
                                Mutex::Locker locker(_mutex);
                                if (_maxThreadCount == 0) {
                                        runInline = true;
                                } else {
                                        _tasks.pushToBack(std::move(entry));
                                        maybeSpawnOne();
                                        _cv.wakeOne();
                                }
                        }
                        if (runInline) {
                                runTaskWithStats(entry);
                        }
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

                /**
                 * @brief Sets a human-readable identity for this pool.
                 *
                 * Used by reporting code that lists every live pool
                 * — see @ref allPools and @ref CpuMonitor.  Distinct
                 * from @ref setNamePrefix, which only controls the
                 * OS-level worker thread names.  Callers may set
                 * both (and typically do — the convention is to
                 * pass the same string to both).  An empty name is
                 * tolerated; the report falls back to a hex address.
                 */
                void setName(const String &name);

                /// @brief Returns the pool's human-readable identity, or empty.
                String name() const;

                /**
                 * @brief Returns a copy of every per-tag accumulator.
                 *
                 * The returned list is a point-in-time snapshot
                 * sorted by @c totalCpu descending so the heaviest
                 * consumer lands first.  The pool keeps
                 * accumulating after the snapshot — call
                 * @ref resetWorkStats to start the next interval
                 * from zero.
                 */
                List<WorkStats> snapshotWorkStats() const;

                /**
                 * @brief Zeros every per-tag accumulator.
                 *
                 * Tag identities are preserved (the bucket
                 * survives), only the totals reset.  Useful for
                 * "how much work happened in the last N seconds"
                 * deltas where the caller calls
                 * @ref snapshotWorkStats then immediately
                 * @ref resetWorkStats.
                 */
                void resetWorkStats();

                /**
                 * @brief Returns every ThreadPool instance currently alive.
                 *
                 * Each pool registers itself in this list at
                 * construction and removes itself at destruction.
                 * The returned list is a copy taken under the
                 * registry mutex — safe to iterate without further
                 * synchronization.  Used by reporting code (such
                 * as @ref CpuMonitor) that wants per-pool, per-tag
                 * accounting without each caller plumbing pools
                 * through explicitly.
                 */
                static List<ThreadPool *> allPools();

        private:
                using Task = Function<void()>;

                /// One internal queue entry: caller's callable plus the
                /// metadata the worker needs to attribute time.
                struct TaggedTask {
                                Task      callable;
                                WorkTag   tag;
                                TimeStamp enqueuedAt;
                };

                /// Per-tag cumulative counters.  Atomics so worker
                /// threads and snapshot readers don't fight a mutex
                /// on the hot path.  Insertion of a new tag still
                /// takes the @ref _statsLock under a write hold.
                struct WorkRecord {
                                Atomic<int64_t> totalWallNs{0};
                                Atomic<int64_t> totalCpuNs{0};
                                Atomic<int64_t> totalQueueWaitNs{0};
                                Atomic<int64_t> count{0};
                                String          name;
                                WorkTag         tag;
                };

                void workerFunc(int index);
                void spawnThreads(int count);
                void maybeSpawnOne();

                /// Runs @p t and accumulates per-tag stats around
                /// the dispatch.  Called from worker threads (and
                /// from the inline-no-thread path).  The wall + CPU
                /// timestamps are taken on the executing thread so
                /// CPU-time uses @c CLOCK_THREAD_CPUTIME_ID for the
                /// thread that actually runs the work.
                void runTaskWithStats(TaggedTask &t);

                /// Look up or create the WorkRecord for @p tag.
                /// Returns a non-owning pointer; the record lives
                /// for the lifetime of the pool.
                WorkRecord *recordFor(WorkTag tag);

                /// Static pool registry helpers.  The list lives in
                /// a function-local static so its destruction order
                /// is well-defined relative to user pools.
                static Mutex              &registryMutex();
                static List<ThreadPool *> &registry();

                mutable Mutex                                  _mutex;
                WaitCondition                                  _cv;
                WaitCondition                                  _doneCv;
                List<TaggedTask>                               _tasks;
                List<std::thread>                              _threads;
                String                                         _namePrefix;
                String                                         _name;
                int                                            _maxThreadCount = 0;
                int                                            _threadCount = 0;
                int                                            _activeCount = 0;
                int                                            _waitingCount = 0;
                bool                                           _shutdown = false;

                mutable ReadWriteLock                          _statsLock;
                HashMap<uint64_t, UniquePtr<WorkRecord>> _stats;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_CORE
