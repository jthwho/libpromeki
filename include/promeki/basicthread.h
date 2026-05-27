/**
 * @file      basicthread.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/config.h>
#if PROMEKI_ENABLE_CORE
#include <chrono>
#include <thread>
#include <promeki/atomic.h>
#include <promeki/duration.h>
#include <promeki/error.h>
#include <promeki/function.h>
#include <promeki/set.h>
#include <promeki/string.h>
#include <promeki/uniqueptr.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Scheduling policy for thread priority control.
 * @ingroup events
 *
 * Wraps the OS-level scheduling policies.  Not all policies are
 * available on every platform — unsupported values map to Default.
 */
enum class SchedulePolicy {
        Default,    ///< Normal time-sharing (SCHED_OTHER on POSIX).
        RoundRobin, ///< Real-time round-robin (SCHED_RR).
        Fifo,       ///< Real-time first-in-first-out (SCHED_FIFO).
        Batch,      ///< Batch scheduling, Linux only (SCHED_BATCH).
        Idle        ///< Idle scheduling, Linux only (SCHED_IDLE).
};

/**
 * @brief Lightweight std::thread wrapper.
 * @ingroup events
 *
 * BasicThread owns a single OS thread and the standalone machinery
 * that does not need ObjectBase — name, native ID, schedule policy,
 * CPU affinity, OS-name propagation — plus the static helpers like
 * @c sleepMs / @c yield / @c idealThreadCount that used to hang off
 * @ref Thread.  Construct one, hand @c start a callable, and let the
 * destructor join it (or join explicitly).
 *
 * BasicThread is move-only.  It uses a Pimpl so the wrapper itself
 * can be cheaply moved even though it owns a non-movable mutex and
 * atomics inside.
 *
 * For event-loop-driven threads with signals, parent/child trees, or
 * adopted main-thread semantics, see @ref Thread.
 *
 * @par Thread safety
 * The BasicThread instance is owned by its creating thread.  @c start,
 * @c setName, @c setPriority, @c setAffinity, @c join etc. should be
 * called from there.  Static helpers may be invoked from any thread.
 *
 * @par Example
 * @code
 * BasicThread bt("worker#1");
 * bt.start([this]{ workerMain(); });
 * // ... later ...
 * bt.join();
 * @endcode
 */
class BasicThread {
        public:
                /** @brief Platform-specific native thread handle type. */
                using NativeHandle = std::thread::native_handle_type;

                /** @brief Callable signature for the thread entry. */
                using Entry = Function<void()>;

                /**
                 * @brief Returns the OS-native thread ID of the calling thread.
                 *
                 * On Linux this is the kernel TID (gettid()), on macOS the
                 * pthread thread ID (pthread_threadid_np()), and on Windows
                 * the Win32 thread ID (GetCurrentThreadId()).
                 *
                 * @return The native thread ID as a 64-bit unsigned integer.
                 */
                static uint64_t currentNativeId();

                /**
                 * @brief Sets the OS-level name and logger name for the calling thread.
                 *
                 * Sets the OS-level thread name (visible in debuggers,
                 * @c htop, @c ps&nbsp;-L) and updates the Logger's cached
                 * thread name.  On Linux/macOS the name is silently
                 * truncated to 15 characters by the OS.
                 *
                 * @param name The name to assign to the calling thread.
                 */
                static void setCurrentThreadName(const String &name);

                /**
                 * @brief Returns the minimum priority for a scheduling policy.
                 * @param policy The scheduling policy to query.
                 * @return The minimum valid priority value.
                 */
                static int priorityMin(SchedulePolicy policy = SchedulePolicy::Default);

                /**
                 * @brief Returns the maximum priority for a scheduling policy.
                 * @param policy The scheduling policy to query.
                 * @return The maximum valid priority value.
                 */
                static int priorityMax(SchedulePolicy policy = SchedulePolicy::Default);

                /**
                 * @brief Returns the number of hardware threads available.
                 *
                 * Wraps @c std::thread::hardware_concurrency().  Returns 0
                 * if the value cannot be determined.
                 *
                 * @return The number of concurrent threads supported.
                 */
                static unsigned int idealThreadCount();

                /**
                 * @brief Sleeps the calling thread for @p ns nanoseconds.
                 *
                 * Sub-millisecond precision is subject to OS scheduling
                 * resolution.  Negative or zero values return immediately.
                 *
                 * @param ns Number of nanoseconds to sleep.
                 */
                static void sleepNs(int64_t ns);

                /**
                 * @brief Sleeps the calling thread for @p us microseconds.
                 * @param us Number of microseconds to sleep.
                 */
                static void sleepUs(int64_t us);

                /**
                 * @brief Sleeps the calling thread for @p ms milliseconds.
                 * @param ms Number of milliseconds to sleep.
                 */
                static void sleepMs(int64_t ms);

                /**
                 * @brief Sleeps the calling thread for the given @ref Duration.
                 *
                 * Negative, zero, or invalid durations return immediately.
                 *
                 * @param d Duration to sleep.
                 */
                static void sleep(const Duration &d) { sleepNs(d.nanoseconds()); }

                /**
                 * @brief Yields the calling thread to the OS scheduler.
                 *
                 * Prefer @ref sleepNs / @ref sleepUs for back-off waits —
                 * busy-yielding burns CPU.
                 */
                static void yield();

                /** @brief Constructs an unnamed BasicThread (not yet started). */
                BasicThread();

                /**
                 * @brief Constructs a BasicThread with a name (not yet started).
                 *
                 * The name is applied to the OS thread on @ref start.
                 *
                 * @param name The thread name.
                 */
                explicit BasicThread(const String &name);

                /** @brief Move constructor.  Source is left empty. */
                BasicThread(BasicThread &&other) noexcept;

                /**
                 * @brief Move assignment.  Joins the current thread first.
                 * @return @c *this.
                 */
                BasicThread &operator=(BasicThread &&other) noexcept;

                BasicThread(const BasicThread &) = delete;
                BasicThread &operator=(const BasicThread &) = delete;

                /** @brief Destructor.  Joins the OS thread if still joinable. */
                ~BasicThread();

                /**
                 * @brief Spawns the OS thread and runs @p entry on it.
                 *
                 * Captures the native ID and std::thread::id, applies the
                 * OS-level name (if previously set), marks the thread
                 * running, then invokes @p entry.  When @p entry returns,
                 * running is cleared.
                 *
                 * @param entry     Callable to run on the new thread.
                 * @param stackSize Stack size in bytes.  Zero (the default)
                 *                  uses the system default.  Non-zero takes
                 *                  the pthread path on POSIX.
                 * @return Error::Ok on success, Error::Busy if already
                 *         started, Error::Invalid if @p entry is empty,
                 *         Error::LibraryFailure if the OS rejected thread
                 *         creation.
                 */
                Error start(Entry entry, size_t stackSize = 0);

                /**
                 * @brief Waits for the thread to finish.
                 *
                 * Returns immediately if not joinable.  Forever-blocking;
                 * use @ref Thread for timed waits.
                 *
                 * @return Error::Ok on success, Error::LibraryFailure if
                 *         the OS reports a join failure (e.g. the handle
                 *         was previously detached).
                 */
                Error join();

                /** @brief @c true if the thread is joinable (started, not yet joined). */
                bool isJoinable() const;

                /** @brief @c true between successful @c start and entry-function return. */
                bool isRunning() const;

                /** @brief @c true if the calling thread is the thread this object owns. */
                bool isCurrentThread() const;

                /**
                 * @brief Returns the name of this thread.
                 * @return The thread name.
                 */
                String name() const;

                /**
                 * @brief Sets the name of this thread.
                 *
                 * The name is stored locally and, if the thread is
                 * running, applied to the OS.  If called before @ref
                 * start, the name is applied when the thread starts.
                 * On macOS the OS name can only be set from the thread
                 * itself, so cross-thread setName updates the local
                 * name but not the OS name.
                 *
                 * @par Logger interaction
                 * The Logger's per-thread name cache is keyed by the
                 * @em caller's native thread ID, so it is only updated
                 * when @c setName is called from the thread that owns
                 * the BasicThread (i.e. the worker calling
                 * @c setName(...) on its own BasicThread).  A
                 * cross-thread @c setName updates the OS-level name
                 * but does not touch the Logger cache — the rename
                 * surfaces in @c htop / debuggers, but log lines
                 * already emitted from the worker keep their previous
                 * tag.  This is intentional: updating the cache from
                 * a different thread would attribute the new name to
                 * the wrong TID.
                 *
                 * @param name The name to assign.
                 */
                void setName(const String &name);

                /**
                 * @brief Returns the platform-specific native handle.
                 *
                 * Returns a default-constructed handle if the thread has
                 * not been started.
                 *
                 * @return The native handle.
                 */
                NativeHandle nativeHandle() const;

                /**
                 * @brief Returns the OS-native thread ID.
                 *
                 * Captured at thread start.  Returns 0 if the thread has
                 * not started.
                 *
                 * @return The native thread ID.
                 */
                uint64_t nativeId() const;

                /**
                 * @brief Returns the C++ std::thread::id of this thread.
                 *
                 * Captured at thread start.  Returns a default-constructed
                 * @c std::thread::id if not started.
                 *
                 * @return The std::thread::id.
                 */
                std::thread::id id() const;

                /**
                 * @brief Returns the current scheduling policy of this thread.
                 *
                 * The thread must be running.  Returns SchedulePolicy::Default
                 * if not running or on error.
                 *
                 * @return The current scheduling policy.
                 */
                SchedulePolicy schedulePolicy() const;

                /**
                 * @brief Returns the current priority of this thread.
                 *
                 * The thread must be running.  Returns 0 if not running
                 * or on error.
                 *
                 * @return The current priority value.
                 */
                int priority() const;

                /**
                 * @brief Sets the scheduling policy and priority for this thread.
                 *
                 * The thread must be running.  Real-time policies (Fifo,
                 * RoundRobin) typically require elevated privileges.
                 *
                 * @param prio   The priority value.
                 * @param policy The scheduling policy to apply.
                 * @return Error::Ok on success, Error::Invalid if the
                 *         thread is not running, Error::LibraryFailure on
                 *         OS-level failure.
                 */
                Error setPriority(int prio, SchedulePolicy policy = SchedulePolicy::Default);

                /**
                 * @brief Returns the set of CPU cores this thread is allowed to run on.
                 *
                 * The thread must be running.  Returns an empty set if not
                 * running, on error, or if the platform does not support
                 * CPU affinity queries.
                 *
                 * @return A set of zero-based CPU core indices.
                 */
                Set<int> affinity() const;

                /**
                 * @brief Restricts this thread to run on the specified CPU cores.
                 *
                 * The thread must be running.  An empty set clears any
                 * affinity restriction, allowing the thread to run on all
                 * available cores.
                 *
                 * @param cpus A set of zero-based CPU core indices.
                 * @return Error::Ok on success, Error::Invalid if not
                 *         running, Error::LibraryFailure on OS-level
                 *         failure, Error::NotSupported on unsupported
                 *         platforms.
                 */
                Error setAffinity(const Set<int> &cpus);

        private:
                struct Data;
                UniquePtr<Data> d;
};

/**
 * @brief Returns a unique instance index for the given tag type.
 * @ingroup events
 *
 * Convenience for classes that want a process-wide monotonic counter
 * to derive unique worker-thread names (or other per-instance IDs).
 * Each distinct @p Tag has its own counter, so different classes do
 * not collide.  Typical use is to call this from the owning class's
 * constructor and store the result; pass the result through to a
 * @c BasicThread name like @c "v4l2-video" + @c String::number(id).
 *
 * @par Example
 * @code
 * class MyWorkerOwner {
 *         public:
 *                 MyWorkerOwner() : _instanceId(nextInstanceId<MyWorkerOwner>()) {}
 *                 int instanceID() const { return _instanceId; }
 *         private:
 *                 int _instanceId;
 * };
 * @endcode
 *
 * @tparam Tag Type used to select the counter (typically the owning class).
 * @return The next instance ID for @p Tag.
 */
template <typename Tag> int nextInstanceId() {
        static Atomic<int> counter{0};
        return counter.fetchAndAdd(1);
}

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_CORE
